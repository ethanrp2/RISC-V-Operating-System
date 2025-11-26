
#include "io.h"
#include <string.h>
#include "console.h"
#include "lock.h"

#define IN_USE      1
#define UNUSED      0
#define FS_BLKSZ      4096
#define FS_NAMELEN    32

extern char _companion_f_start[];
extern char _companion_f_end[];

struct lock kfs_lock;

struct file_t{
    struct io_intf io_intf;
    uint64_t position;
    uint64_t file_size;
    uint64_t inode;
    uint64_t flags;
};

typedef struct dentry_t{
    char file_name[FS_NAMELEN];
    uint32_t inode;
    uint8_t reserved[28];
}__attribute((packed)) dentry_t; 

typedef struct boot_block_t{
    uint32_t num_dentry;
    uint32_t num_inodes;
    uint32_t num_data;
    uint8_t reserved[52];
    dentry_t dir_entries[63];
}__attribute((packed)) boot_block_t;

typedef struct inode_t{
    uint32_t byte_len;
    uint32_t data_block_num[1023];
}__attribute((packed)) inode_t;

int fs_mount(struct io_intf * blkio);
int fs_open(const char * name, struct io_intf ** ioptr);
void fs_close(struct io_intf * io);
long fs_write(struct io_intf * io, const void* buf, unsigned long n);
long fs_read(struct io_intf * io, void* buf, unsigned long n);
int fs_ioctl(struct io_intf * io, int cmd, void* arg);
int fs_getlen(struct file_t * fd, void* arg);
int fs_getpos(struct file_t * fd, void* arg);
int fs_setpos(struct file_t * fd, void* arg);
int fs_getblksz(struct file_t * fd, void* arg);


struct file_t files[32]; // global array for open files

boot_block_t boot_block;    // global boot_block to store info coming in

struct io_intf * vioblk;

static const struct io_ops file_ops = {         // ops are the same for all files so we can globally define this
    .close = fs_close,
    .ctl = fs_ioctl,
    .read = fs_read,
    .write = fs_write
};

/*
Inputs: blkio
Outputs: int status
Purpose: takes in a pointer and reads data from it into a filesystem. Can create a filesystem from any io_intf pointer.
        Reads  4096 bytes from device or io_lit to set up boot_block with all the necessary information.
*/

int fs_mount(struct io_intf * blkio){

    if (blkio == NULL){             // return error if NULL incoming pointer
        return -1;
    }

    ioseek(blkio, 0);               // set posiiton of device to 0
    vioblk = blkio;
    ioread(blkio, &boot_block, 4096);   // read in boot block from device

    return 0;
}

/*
Inputs: name, io_ptr
Outputs: int status
Purpose: Finds an open slot in the open files array and stores the information of the incoming file. Finds the file of the name by
        checking the directory entries. Stores file_t struct at open position in the array and returns a pointer through the 
        double pointer parameter to give caller access to operations on that file.
*/

int fs_open(const char * name, struct io_intf ** ioptr){

    
    lock_init(&kfs_lock, "kfs_lock");
    
    int i = 0;
    // console_printf("%s\n", boot_block.dir_entries[i].file_name);
    // console_printf("%s\n", name);
    while (i < 63){
        if (strcmp((char*)boot_block.dir_entries[i].file_name, (char*)name) == 0){      // check for the correct file using the name
            uint64_t inode = boot_block.dir_entries[i].inode;                           
            ioseek(vioblk, 4096 + FS_BLKSZ*inode);                                      // change position for current inode from the device
            inode_t inode_obj;
            ioread(vioblk, &inode_obj, 4096);                          // read in current inode
            
            uint64_t size = inode_obj.byte_len;
            int j = 0;
            while (j < 32){
                if (files[j].flags == UNUSED){                                          // check for open spot in array and store curr file info into there
                    files[j].position = 0;
                    files[j].flags = IN_USE;
                    files[j].inode = inode;
                    files[j].file_size = size;
                
                    files[j].io_intf.ops = &file_ops;
                    // files[j].io_intf.refcnt = 0;                                    
                    // ioref(&files[j].io_intf);
                    files[j].io_intf.refcnt = 1;                                       // Init ref count to 1
                   *ioptr = &files[j].io_intf;                                      // store io_intf pointer for current file in incoming dbl pointer
                    return 0;
                }

                j++;
            }

            return -2;                                                      // return error for full file struct
        }

        i++;
    }

    return -ENOENT;                                             // return error for not finding file
}

/*
Inputs: io
Outputs: int status
Purpose: Closes a file in the file system by setting its flag to UNUSED. The position will be marked as open in the file array.
*/

void fs_close(struct io_intf * io){
    if (--io->refcnt == 0) {                                // only close if ref count is 0
        int i = 0;

        while (i < 32){
            if (&files[i].io_intf == io){
                files[i].flags = UNUSED;                    // set current file from io_intf to Unused which opens up that slot
            }

            i++;
        }
    }
}

/*
Inputs: io, buf, n
Outputs: int bytes
Purpose: Takes in an io_intf pointer and writes n bytes to the file. Checks to make sure that writing is done across blocks that may not be contigious.
        Writes by finding offsets and writing directly to the original device that was mounted. Updates file position as needed and returns the amount 
        of bytes that was written.
*/

long fs_write(struct io_intf * io, const void* buf, unsigned long n){
    lock_acquire(&kfs_lock);
    int i = 0;
    struct file_t * curr;
    
    
    while (i < 32){
        if (&files[i].io_intf == io){           // find correct file based on pointer
            
            curr = &files[i];
            uint64_t size = files[i].file_size;     // find current file size
            
            if (size - files[i].position < n){
                n = size - files[i].position;               // clip the size based on the file size
                
            }

            
            uint64_t pos = curr->position;


        
            inode_t inode;

            ioseek(vioblk, curr->inode*FS_BLKSZ + 4096);            // read in the current inode for this file
            int inode_bytes = ioread(vioblk, &inode, 4096);
            if (inode_bytes != 4096){
                lock_release(&kfs_lock);
                return -EINVAL;
            }

            uint64_t inode_offset = pos/FS_BLKSZ;           // which block of this file
            uint64_t block_pos = pos % FS_BLKSZ;            // find position in said block
            uint64_t byte_count;
            uint64_t data_block_num = inode.data_block_num[inode_offset];   // get the correct data block number
            
            
            uint64_t write_loc = (FS_BLKSZ + (boot_block.num_inodes*FS_BLKSZ) + (data_block_num*FS_BLKSZ) + block_pos);     // calculate location based on offsets
            uint64_t write_bytes = 0;
            uint64_t total_bytes_write = 0;

            while (1){
                if (n + block_pos < FS_BLKSZ){                  // write will not reach end of block
                    byte_count = n;
                    
                }
                else{
                    byte_count = FS_BLKSZ - block_pos;          // write will reach end of block, cut off size
                }
               
                ioseek(vioblk, write_loc);                      // set position to location based on size
                write_bytes = iowrite(vioblk, buf + total_bytes_write, byte_count);     // write bytes

                if (write_bytes != byte_count){
                    lock_release(&kfs_lock);
                    return -EIO;
                }

                n -= write_bytes;                   // subtract bytes that were written
                total_bytes_write += write_bytes;
                if (n <= 0){                        // write is over, exit
                    break;
                }
                 
                inode_offset++;                     
                data_block_num = inode.data_block_num[inode_offset];            // get new data block
                block_pos = 0;
                write_loc = (FS_BLKSZ + (boot_block.num_inodes*FS_BLKSZ) + (data_block_num*FS_BLKSZ) + block_pos);      // recalculate read location
    
            }
            
            curr->position += total_bytes_write;                    // update position in file
            lock_release(&kfs_lock);
            return total_bytes_write;

        }

        i++;

    }
    lock_release(&kfs_lock);
    return -1;                      // file was not found
}

/*
Inputs: io, buf, n
Outputs: long bytes
Purpose: Takes in an io_intf pointer and reads n bytes to the file. Checks to make sure that reading is done across blocks that may not be contigious.
        Reads by finding offsets and writing directly to the original device that was mounted. Updates file position as needed and returns the amount 
        of bytes that was read.
*/

long fs_read(struct io_intf * io, void* buf, unsigned long n){
    lock_acquire(&kfs_lock);
    int i = 0;
    struct file_t * curr;
    
    
    while (i < 32){
        if (&files[i].io_intf == io){                   // find io_intf associated with file
            
            curr = &files[i];
            uint64_t size = files[i].file_size;
            
            if (size - files[i].position < n){
                n = size - files[i].position;
                
            }

            
            uint64_t pos = curr->position;

            inode_t inode;

            ioseek(vioblk, curr->inode*FS_BLKSZ + 4096);                // read in inode for file
            int inode_bytes = ioread(vioblk, &inode, 4096);             
            if (inode_bytes != 4096){
                lock_release(&kfs_lock);
                return -EINVAL;
            }

            

            uint64_t inode_offset = pos/FS_BLKSZ;                   // which data block in inode
            uint64_t block_pos = pos % FS_BLKSZ;                    // position within said block
            uint64_t byte_count;

            uint64_t data_block_num = inode.data_block_num[inode_offset];       // find data block from inode data
            
            uint64_t read_loc = (FS_BLKSZ + (boot_block.num_inodes*FS_BLKSZ) + (data_block_num*FS_BLKSZ) + block_pos);      // calculate read location based on offsets
            uint64_t read_bytes = 0;        
            uint64_t total_bytes_read = 0;

            
            

            while (1){
                if (n + block_pos < FS_BLKSZ){              // read with not reach end of block
                    byte_count = n;
                    
                }
                else{
                    byte_count = FS_BLKSZ - block_pos;              // read will reach end of block, cut off bytes
                }
               
                ioseek(vioblk, read_loc);                           // set position in device to read
                read_bytes = ioread(vioblk, buf + total_bytes_read, byte_count);            // read bytes


               

                if (read_bytes != byte_count){
                    lock_release(&kfs_lock);
                    return -EIO;
                }
                n -= read_bytes;                        // subtact of bytes that were already read
                total_bytes_read += read_bytes;         
                if (n <= 0){                            // read is over, exit
                    break;
                }
                 
                inode_offset++;
                data_block_num = inode.data_block_num[inode_offset];            // find next data block
                block_pos = 0;
                read_loc = (FS_BLKSZ + (boot_block.num_inodes*FS_BLKSZ) + (data_block_num*FS_BLKSZ) + block_pos);       // update location to read from device
    
            }
            
            curr->position += total_bytes_read;             // update position
            lock_release(&kfs_lock);
            return total_bytes_read;

        }

        i++;

    }
    lock_release(&kfs_lock);
    return -1;
}

/*
Inputs: io, cmd, arg
Outputs: int status
Purpose: Takes in a command and sends off to the correct helper function based on what the caller wants. Returns various information about the file
            through the arg parameter. 
*/

int fs_ioctl(struct io_intf * io, int cmd, void* arg){
    int i = 0;
    struct file_t * curr;

    while (i < 32){
        if (&files[i].io_intf == io){       // find io_intf for current file based on the paramters
            curr = &files[i];
            break;
    }   
    i++;
 }

    switch (cmd){                           // send to helper function based on cmd code
        case IOCTL_GETLEN: 
            return fs_getlen(curr, arg);
        case IOCTL_GETPOS: 
            return fs_getpos(curr, arg);
        case IOCTL_SETPOS:
            return fs_setpos(curr, arg);
        case IOCTL_GETBLKSZ:
            return fs_getblksz(curr, arg);
    }

    return 0;

}

/*
Inputs: fd, arg
Outputs: int status
Purpose: Returns the length of the file in bytes through the arg parameter.
*/

int fs_getlen(struct file_t * fd, void* arg){
    *(size_t *)arg = fd->file_size;                 // store file_size in arg
    return 0;
}

/*
Inputs: fd, arg
Outputs: int status
Purpose: Returns the position of the file in through the arg parameter.
*/

int fs_getpos(struct file_t * fd, void* arg){
    *(size_t *)arg = fd->position;                  // store position in file in arg
    return 0;
}

/*
Inputs: fd, arg
Outputs: int status
Purpose: Sets the position of the file in bytes through the arg parameter.
*/

int fs_setpos(struct file_t * fd, void* arg){
    fd->position = *(size_t *)arg;                  // set position to arg
    return 0;
}

/*
Inputs: fd, arg
Outputs: int status
Purpose: Returns the block size of the file system in bytes through the arg parameter.
*/

int fs_getblksz(struct file_t * fd, void* arg){     // store block size in arg
    *(size_t *)arg = FS_BLKSZ;
    return 0;
}




