//           vioblk.c - VirtIO serial port (console)
//          

#include "virtio.h"
#include "intr.h"
#include "halt.h"
#include "heap.h"
#include "io.h"
#include "device.h"
#include "error.h"
#include "string.h"
#include "thread.h"
#include "lock.h"

//           COMPILE-TIME PARAMETERS
//          

#define VIOBLK_IRQ_PRIO 1
#define VIRTQ_ID 0

//           INTERNAL CONSTANT DEFINITIONS
//          

//           VirtIO block device feature bits (number, *not* mask)

#define VIRTIO_BLK_F_SIZE_MAX       1
#define VIRTIO_BLK_F_SEG_MAX        2
#define VIRTIO_BLK_F_GEOMETRY       4
#define VIRTIO_BLK_F_RO             5
#define VIRTIO_BLK_F_BLK_SIZE       6
#define VIRTIO_BLK_F_FLUSH          9
#define VIRTIO_BLK_F_TOPOLOGY       10
#define VIRTIO_BLK_F_CONFIG_WCE     11
#define VIRTIO_BLK_F_MQ             12
#define VIRTIO_BLK_F_DISCARD        13
#define VIRTIO_BLK_F_WRITE_ZEROES   14

//           INTERNAL TYPE DEFINITIONS
//          

//           All VirtIO block device requests consist of a request header, defined below,
//           followed by data, followed by a status byte. The header is device-read-only,
//           the data may be device-read-only or device-written (depending on request
//           type), and the status byte is device-written.

struct vioblk_request_header {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
};

struct lock vioblk_lock;

//           Request type (for vioblk_request_header)

#define VIRTIO_BLK_T_IN             0
#define VIRTIO_BLK_T_OUT            1

//           Status byte values

#define VIRTIO_BLK_S_OK         0
#define VIRTIO_BLK_S_IOERR      1
#define VIRTIO_BLK_S_UNSUPP     2

//           Main device structure.
//          
//           FIXME You may modify this structure in any way you want. It is given as a
//           hint to help you, but you may have your own (better!) way of doing things.

struct vioblk_device {
    volatile struct virtio_mmio_regs * regs;
    struct io_intf io_intf;
    uint16_t instno;
    uint16_t irqno;
    int8_t opened;
    int8_t readonly;

    //           optimal block size
    uint32_t blksz;
    //           current position
    uint64_t pos;
    //           sizeo of device in bytes
    uint64_t size;
    //           size of device in blksz blocks
    uint64_t blkcnt;

    struct {
        //           signaled from ISR
        struct condition used_updated;

        //           We use a simple scheme of one transaction at a time.

        union {
            struct virtq_avail avail;
            char _avail_filler[VIRTQ_AVAIL_SIZE(1)];
        };

        union {
            volatile struct virtq_used used;
            char _used_filler[VIRTQ_USED_SIZE(1)];
        };

        //           The first descriptor is an indirect descriptor and is the one used in
        //           the avail and used rings. The second descriptor points to the header,
        //           the third points to the data, and the fourth to the status byte.

        struct virtq_desc desc[4];
        struct vioblk_request_header req_header;
        uint8_t req_status;
    } vq;

    //           Block currently in block buffer
    uint64_t bufblkno;
    //           Block buffer
    char * blkbuf;
};

//           INTERNAL FUNCTION DECLARATIONS
//          

static int vioblk_open(struct io_intf ** ioptr, void * aux);

static void vioblk_close(struct io_intf * io);

static long vioblk_read (
    struct io_intf * restrict io,
    void * restrict buf,
    unsigned long bufsz);

static long vioblk_write (
    struct io_intf * restrict io,
    const void * restrict buf,
    unsigned long n);

static int vioblk_ioctl (
    struct io_intf * restrict io, int cmd, void * restrict arg);

static void vioblk_isr(int irqno, void * aux);

//           IOCTLs

static int vioblk_getlen(const struct vioblk_device * dev, uint64_t * lenptr);
static int vioblk_getpos(const struct vioblk_device * dev, uint64_t * posptr);
static int vioblk_setpos(struct vioblk_device * dev, const uint64_t * posptr);
static int vioblk_getblksz (
    const struct vioblk_device * dev, uint32_t * blkszptr);

//           EXPORTED FUNCTION DEFINITIONS
//          

//           Attaches a VirtIO block device. Declared and called directly from virtio.c.
/*
Purpose: Initializes virtio block device, attaches virtq queues, register isr and device
Arguments: regs (64-bit), irqno (32-bit)
Side Effects: Fills out descriptors in virtq struct, sets up IO Operation, feature bits set, isr and device registration
*/
void vioblk_attach(volatile struct virtio_mmio_regs * regs, int irqno) {
    //           FIXME add additional declarations here if needed

    virtio_featset_t enabled_features, wanted_features, needed_features;
    struct vioblk_device * dev;
    uint_fast32_t blksz;
    int result;

    assert (regs->device_id == VIRTIO_ID_BLOCK);

    //           Signal device that we found a driver

    regs->status |= VIRTIO_STAT_DRIVER;
    //           fence o,io
    __sync_synchronize();

    //           Negotiate features. We need:
    //            - VIRTIO_F_RING_RESET and
    //            - VIRTIO_F_INDIRECT_DESC
    //           We want:
    //            - VIRTIO_BLK_F_BLK_SIZE and
    //            - VIRTIO_BLK_F_TOPOLOGY.

    virtio_featset_init(needed_features);
    virtio_featset_add(needed_features, VIRTIO_F_RING_RESET);
    virtio_featset_add(needed_features, VIRTIO_F_INDIRECT_DESC);
    virtio_featset_init(wanted_features);
    virtio_featset_add(wanted_features, VIRTIO_BLK_F_BLK_SIZE);
    virtio_featset_add(wanted_features, VIRTIO_BLK_F_TOPOLOGY);
    result = virtio_negotiate_features(regs,
        enabled_features, wanted_features, needed_features);

    if (result != 0) {
        kprintf("%p: virtio feature negotiation failed\n", regs);
        return;
    }

    //           If the device provides a block size, use it. Otherwise, use 512.

    if (virtio_featset_test(enabled_features, VIRTIO_BLK_F_BLK_SIZE))
        blksz = regs->config.blk.blk_size;
    else
        blksz = 512;

    debug("%p: virtio block device block size is %lu", regs, (long)blksz);

    //           Allocate initialize device struct

    dev = kmalloc(sizeof(struct vioblk_device) + blksz);
    memset(dev, 0, sizeof(struct vioblk_device));

    //           FIXME Finish initialization of vioblk device here
    dev->blksz = blksz;
    dev->regs = regs;
    dev->irqno = irqno;
    dev->opened = 0;
    dev->blkbuf = (char *) (dev + 1);
    dev->bufblkno = 0;
    dev->blkcnt = regs->config.blk.capacity;
    __sync_synchronize();
    dev->size = dev->blkcnt * blksz;

    dev->instno = device_register("blk", vioblk_open, dev);
    virtio_attach_virtq(regs, VIRTQ_ID, 1, (uint64_t)&dev->vq.desc, (uint64_t)&dev->vq.used, (uint64_t)&dev->vq.avail);
    intr_register_isr(irqno, VIOBLK_IRQ_PRIO, vioblk_isr, dev);

    regs->status |= VIRTIO_STAT_DRIVER_OK;    
    //           fence o,oi
    __sync_synchronize();
}

/*
Purpose: Sets the virtq avail and virtq used queues such that they are available for use. (Hint, read
virtio.h) Enables the interupt line for the virtio device and sets necessary flags in vioblk device.
Returns the IO operations to ioptr.
Arguments: ioptr (64-bit), aux (64-bit)
Side Effects: Opens the device, enables virtq queues, initializes condition for isr
*/
int vioblk_open(struct io_intf ** ioptr, void * aux) {
    struct vioblk_device * dev = (struct vioblk_device *)aux;

    if (dev->opened) { // Check if device is already open
        return -EBUSY;
    }
    lock_init(&vioblk_lock, "vioblk_lock");
    static const struct io_ops vioblk_ops = {
        .close = vioblk_close,
        .read = vioblk_read,
        .write = vioblk_write,
        .ctl = vioblk_ioctl,
    };
    dev->io_intf.ops = &vioblk_ops;

    intr_enable_irq(dev->irqno); // Enable interrupts for the device
    virtio_enable_virtq(dev->regs, VIRTQ_ID); // Enable the virtual queue
    condition_init(&(dev->vq.used_updated), "used_updated"); // Initialize condition variable
    // ioref(&dev->io_intf);
    dev->io_intf.refcnt = 1;
    *ioptr = &dev->io_intf; // Return IO operations to caller
    dev->opened = 1; // Mark device as opened

    return 0;
}

//           Must be called with interrupts enabled to ensure there are no pending
//           interrupts (ISR will not execute after closing).

/*
Purpose: Resets the virtq avail and virtq used queues and sets necessary flags in vioblk device.
Arguments: io (64-bit)
Side Effects: Closes the device, disables isr for source and resets virtq queues
*/
void vioblk_close(struct io_intf * io) {
    if (--io->refcnt == 0) {
        struct vioblk_device * dev = (void *)io - offsetof(struct vioblk_device, io_intf);

        virtio_reset_virtq(dev->regs, VIRTQ_ID); // Reset the virtual queue
        intr_disable_irq(dev->irqno); // Disable interrupts for the device
        dev->opened = 0; // Mark device as closed
    }
}


/*
Purpose: Reads bufsz number of bytes from the disk and writes them to buf. Achieves this by repeatedly
setting the appropriate registers to request a block from the disk, waiting until the data has been
populated in block buffer cache, and then writes that data out to buf. Thread sleeps while waiting for
the disk to service the request. Returns the number of bytes successfully read from the disk.
Arguments: io (64-bit), buf (64-bit), bufsz (64-bit)
Side Effects: The buf is populated with data coming from block device for bufsz num of bytes
*/
long vioblk_read(struct io_intf * restrict io, void * restrict buf, unsigned long bufsz) {
    lock_acquire(&vioblk_lock);
    struct vioblk_device * const dev = (struct vioblk_device *)((void *)io - offsetof(struct vioblk_device, io_intf));

    if (bufsz == 0) { return -EINVAL; } // Invalid buffer size
    if (dev->opened == 0){ return -ENODEV; } // Device not open
    if (dev->pos > dev->size) { return 0; } // Position exceeds device size

    int total_read = 0;
    
    while (total_read < bufsz && dev->pos < dev->size) {
        int sector = dev->pos / dev->blksz; // Calculate current sector
        int offset = dev->pos % dev->blksz; // Calculate offset within the block

        // Request Header
        dev->vq.req_header.type = VIRTIO_BLK_T_IN;
        dev->vq.req_header.reserved = 0;
        dev->vq.req_header.sector = sector;
        dev->bufblkno = sector;

        // Descriptor 0 - Indirect
        dev->vq.desc[0].addr = (uint64_t)&dev->vq.desc[1];  
        dev->vq.desc[0].len = sizeof(struct virtq_desc) * 3;  
        dev->vq.desc[0].flags = VIRTQ_DESC_F_INDIRECT;      
        dev->vq.desc[0].next = -1; // End of chain

        // Descriptor 1 - Request 
        dev->vq.desc[1].addr = (uint64_t)&dev->vq.req_header;   
        dev->vq.desc[1].len = sizeof(struct vioblk_request_header); 
        dev->vq.desc[1].flags = VIRTQ_DESC_F_NEXT;         
        dev->vq.desc[1].next = 1;

        // Descriptor 2 - Data
        dev->vq.desc[2].addr = (uint64_t)dev->blkbuf;      
        dev->vq.desc[2].len = dev->blksz;                    
        dev->vq.desc[2].flags = VIRTQ_DESC_F_WRITE | VIRTQ_DESC_F_NEXT; 
        dev->vq.desc[2].next = 2;

        // Descriptor 3 - Status
        dev->vq.desc[3].addr = (uint64_t)&dev->vq.req_status;   
        dev->vq.desc[3].len = sizeof(dev->vq.req_status);       
        dev->vq.desc[3].flags = VIRTQ_DESC_F_WRITE;
        dev->vq.desc[3].next = -1; // End of chain

        dev->vq.avail.ring[0] = 0; // Add descriptor to available ring
        __sync_synchronize();
        dev->vq.avail.idx++;
        __sync_synchronize();

        virtio_notify_avail(dev->regs, VIRTQ_ID); // Notify the device about the available descriptor
        __sync_synchronize();

        int s = intr_disable();
        while (dev->vq.used.idx != dev->vq.avail.idx) { // Wait until the device processes the request
            condition_wait(&dev->vq.used_updated);
        }
        intr_restore(s);

        if (dev->vq.req_status != VIRTIO_BLK_S_OK) { return -EIO; } // Check for read errors

        int read_size = ((dev->blksz - offset) < (bufsz - total_read)) ? dev->blksz - offset : bufsz - total_read;
        memcpy((char *)buf + total_read, dev->blkbuf + offset, read_size); // Copy data to buffer
        
        total_read += read_size;
        dev->pos += read_size; // Update position
    }
    lock_release(&vioblk_lock);
    return total_read;
}

/*
Purpose: Writes n number of bytes from the parameter buf to the disk. The size of the virtio device should
not change. You should only overwrite existing data. Write should also not create any new files.
Achieves this by filling up the block buffer cache and then setting the appropriate registers to request
the disk write the contents of the cache to the specified block location. Thread sleeps while waiting
for the disk to service the request. Returns the number of bytes successfully written to the disk.
Arguments: io (64-bit), buf (64-bit), n (64-bit)
Side Effects: block device receives n bytes from the buf overwritten on its memory
*/
long vioblk_write(struct io_intf * restrict io, const void * restrict buf, unsigned long n) {
    lock_acquire(&vioblk_lock);
    struct vioblk_device * const dev = (struct vioblk_device *)((void *)io - offsetof(struct vioblk_device, io_intf));
    
    if (dev->readonly == 1) { return -EIO; } // Check if the device is read-only
    if (dev->pos > dev->size) { return 0; } // Position exceeds device size
    if (dev->opened == 0){ return -ENODEV; } // Device not open
    if (n == 0) { return -EINVAL; } // Invalid buffer size

    int total_written = 0;
    
    while (total_written < n) {
        int sector = dev->pos / dev->blksz; // Calculate current sector
        int offset = dev->pos % dev->blksz; // Calculate offset within the block
        
        int write_size = ((dev->blksz - offset) < (n - total_written)) ? dev->blksz - offset : n - total_written;
        memcpy(dev->blkbuf + offset, (const char *)buf + total_written, write_size); // Copy data to block buffer at offset

        // Request Header
        dev->vq.req_header.type = VIRTIO_BLK_T_OUT;
        dev->vq.req_header.reserved = 0;
        dev->vq.req_header.sector = sector;

        // Descriptor 0 - Indirect
        dev->vq.desc[0].addr = (uint64_t)&dev->vq.desc[1];  
        dev->vq.desc[0].len = sizeof(dev->vq.desc[1]) * 3;  
        dev->vq.desc[0].flags = VIRTQ_DESC_F_INDIRECT;      
        dev->vq.desc[0].next = -1; // End of chain

        // Descriptor 1 - Request
        dev->vq.desc[1].addr = (uint64_t)&dev->vq.req_header;
        dev->vq.desc[1].len = sizeof(struct vioblk_request_header);
        dev->vq.desc[1].flags = VIRTQ_DESC_F_NEXT;
        dev->vq.desc[1].next = 1;

        // Descriptor 2 - Data
        dev->vq.desc[2].addr = (uint64_t)dev->blkbuf;
        dev->vq.desc[2].len = dev->blksz;
        dev->vq.desc[2].flags = VIRTQ_DESC_F_NEXT;
        dev->vq.desc[2].next = 2;

        // Descriptor 3 - Status
        dev->vq.desc[3].addr = (uint64_t)&dev->vq.req_status;
        dev->vq.desc[3].len = 1;
        dev->vq.desc[3].flags = VIRTQ_DESC_F_WRITE;
        dev->vq.desc[3].next = -1; // End of chain

        dev->vq.avail.ring[dev->vq.avail.idx % 1] = 0; // Add descriptor to available ring
        __sync_synchronize();
        dev->vq.avail.idx++;
        __sync_synchronize();

        virtio_notify_avail(dev->regs, VIRTQ_ID); // Notify the device about the available descriptor

        int s = intr_disable();
        while (dev->vq.used.idx != dev->vq.avail.idx) { // Wait until the device processes the request
            condition_wait(&dev->vq.used_updated);
        }
        intr_restore(s);

        if (dev->vq.req_status != VIRTIO_BLK_S_OK) { return -EIO; } // Check for write errors

        total_written += write_size;
        dev->pos += write_size; // Update position
    }
    lock_release(&vioblk_lock);
    return total_written;
}


int vioblk_ioctl(struct io_intf * restrict io, int cmd, void * restrict arg) {
    struct vioblk_device * const dev = (void*)io -
        offsetof(struct vioblk_device, io_intf);
    
    trace("%s(cmd=%d,arg=%p)", __func__, cmd, arg);
    
    switch (cmd) {
    case IOCTL_GETLEN:
        return vioblk_getlen(dev, arg);
    case IOCTL_GETPOS:
        return vioblk_getpos(dev, arg);
    case IOCTL_SETPOS:
        return vioblk_setpos(dev, arg);
    case IOCTL_GETBLKSZ:
        return vioblk_getblksz(dev, arg);
    default:
        return -ENOTSUP;
    }
}

/*
Purpose: Sets the appropriate device registers and wakes the thread up after waiting for the disk to finish ser-
vicing a request.
Arguments: irqno (32-bit), aux (64-bit)
Side Effects: runs with isr is triggered, broadcasts condition to continue operation for read/write, ackowledge interrupts to registers
*/
void vioblk_isr(int irqno, void * aux) {
    struct vioblk_device *dev = (struct vioblk_device *)aux;

    // Check if the interrupt status indicates used buffer notification
    if (dev->regs->interrupt_status & VIRTQ_USED_F_NO_NOTIFY) {
        condition_broadcast(&dev->vq.used_updated); // Wake up waiting thread
    }

    dev->regs->interrupt_ack |= VIRTIO_STAT_ACKNOWLEDGE; // Acknowledge the interrupt
}

/*
Purpose: Ioctl helper function which provides the device size in bytes.
Arguments: dev (64-bit), lenptr (64-bit)
Side Effects: sets lenptr with the return value
*/
int vioblk_getlen(const struct vioblk_device * dev, uint64_t * lenptr) {
    *lenptr = dev->size; // Set the length pointer to device size
    return dev->size; // Return the device size
}


/*
Purpose: Ioctl helper function which gets the current position in the disk which is currently being written to or
read from.
Arguments: dev (64-bit), posptr (64-bit)
Side Effects: sets posptr with the return value
*/
int vioblk_getpos(const struct vioblk_device * dev, uint64_t * posptr) {
    *posptr = dev->pos; // Set the position pointer to current position
    return dev->pos; // Return the current position
}


/*
Purpose: Ioctl helper function which sets the current position in the disk which is currently being written to or
read from.
Arguments: dev (64-bit), posptr (64-bit)
Side Effects: sets posptr to dev->pos
*/
int vioblk_setpos(struct vioblk_device * dev, const uint64_t * posptr) {
    if (posptr == NULL || *posptr > dev->size) { // Validate the position
        return -EINVAL; // Return error for invalid position
    }

    dev->pos = *posptr; // Set the device position
    return 0;
}


/*
Purpose: Ioctl helper function which provides the device block size.
Arguments: dev (64-bit), blkszptr (64-bit)
Side Effects: sets blkszptr with the return value
*/
int vioblk_getblksz(const struct vioblk_device * dev, uint32_t * blkszptr) {
    *blkszptr = dev->blksz; // Set the block size pointer
    return dev->blksz; // Return the block size
}

