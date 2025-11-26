//           main.c - Main function: runs shell to load executable
//          

#include "console.h"
#include "thread.h"
#include "device.h"
#include "uart.h"
#include "timer.h"
#include "intr.h"
#include "heap.h"
#include "virtio.h"
#include "halt.h"
#include "elf.h"
#include "fs.h"
#include "string.h"


//           end of kernel image (defined in kernel.ld)
extern char _kimg_end[];

extern char _companion_f_start [];
extern char _companion_f_end [];

#define RAM_SIZE (8*1024*1024)
#define RAM_START 0x80000000UL
#define KERN_START RAM_START
#define USER_START 0x80100000UL

#define UART0_IOBASE 0x10000000
#define UART1_IOBASE 0x10000100
#define UART0_IRQNO 10

#define TEST_SUCCESS 1
#define TEST_FAILURE 0

#define VIRT0_IOBASE 0x10001000
#define VIRT1_IOBASE 0x10002000
#define VIRT0_IRQNO 1
#define TEST_BUF_SIZE 1022  // Assume 512-byte blocks for testing

static void read_test(struct io_intf *blkio, int bufsz);
static void write_test(struct io_intf *blkio, int bufsz);
static void getlen_test(struct io_intf *blkio);
static void getpos_test(struct io_intf *blkio);
static void setpos_test(struct io_intf *blkio);
static void getblksz_test(struct io_intf *blkio);


int run_all_tests_fs();
int test_fs_open();
int test_fs_read_write_open_close();
int test_mount();
int io_lit_ops_test();
int elf_test();
int elf_test_deny();


void main(void) {
    struct io_intf *blkio;
    void *mmio_base;
    int i;

    console_init();
    intr_init();
    devmgr_init();
    timer_init();
    thread_init();
    heap_init(_kimg_end, (void*)USER_START);

    // Attach serial devices (UARTs)
    for (i = 0; i < 2; i++) {
        mmio_base = (void*)UART0_IOBASE;
        mmio_base += (UART1_IOBASE - UART0_IOBASE) * i;
        uart_attach(mmio_base, UART0_IRQNO + i);
    }
    
    // Attach virtio block devices
    for (i = 0; i < 8; i++) {
        mmio_base = (void*)VIRT0_IOBASE;
        mmio_base += (VIRT1_IOBASE - VIRT0_IOBASE) * i;
        virtio_attach(mmio_base, VIRT0_IRQNO + i);
    }

    intr_enable();
    timer_start();

    console_printf("\n*******  Device Open  *******\n\n");

    int result = device_open(&blkio, "blk", 0);
    if (result != 0) {
        console_printf("Error %d: Block Device Cannot be Opened\n", result);
    } else {
        console_printf("DEVICE OPEN SUCCESS...");
    }

    // VIOBLK TEST CODE

    read_test(blkio, 512);
    write_test(blkio, 512);
    getlen_test(blkio);
    getpos_test(blkio);
    setpos_test(blkio);
    getpos_test(blkio);
    getblksz_test(blkio);
    run_all_tests_fs();
    elf_test();
    elf_test_deny();

    // Close the block device
    blkio->ops->close(blkio);
}

static void read_test(struct io_intf *blkio, int bufsz) {
    char buffer[bufsz];
    int result;

    memset(&buffer, 0, bufsz); // clear buffer
    console_printf("\n*******  Read Test  *******\n\n");
    result = blkio->ops->read(blkio, buffer, bufsz);
    if (result >= 0) {
        console_printf("READ SUCCESS... BYTES READ: %d\n", result);
    } else {
        console_printf("READ FAILURE... ERROR CODE: %d\n", result);
    }
    console_printf("Buffer Content (32-bytes): ");
    for (int i = 0; i < 32; i++) { console_printf("%02x ", buffer[i]); }
}

static void write_test(struct io_intf *blkio, int bufsz) {
    char buf[bufsz];
    int result;
    
    memset(&buf, 0, bufsz); // clear buffer
    buf[0] = 'h';
    buf[1] = 'a';
    buf[2] = 'd';
    buf[3] = 'd';
    buf[4] = 'i';

    console_printf("\n*******  Write Test  *******\n\n");
    result = blkio->ops->write(blkio, buf, bufsz);
    if (result >= 0) {
        console_printf("WRITE SUCCESS... BYTES WRITTEN: %d\n", result);
    } else {
        console_printf("READ FAILURE... ERROR CODE: %d\n", result);
    }
    console_printf("Buffer Content (32-bytes): ");
    for (int i = 0; i < 32; i++) { console_printf("%02x ", buf[i]); }
    console_printf("\n");
}

static void getlen_test(struct io_intf *blkio) {
    uint64_t ptr = 0;
    blkio->ops->ctl(blkio, IOCTL_GETLEN, &ptr);

    console_printf("\n*******  GetLen Test  *******\n\n");
    if (ptr) {
        console_printf("GETLEN SUCCESS... LEN: %d", ptr);
    } else {
        console_printf("GETLEN FAILURE... ");
    }
    console_printf("\n");
}

static void getpos_test(struct io_intf *blkio) {
    uint64_t ptr = 0;
    blkio->ops->ctl(blkio, IOCTL_GETPOS, &ptr);

    console_printf("\n*******  GetPos Test  *******\n\n");
    if (ptr) {
        console_printf("GETPOS SUCCESS... LEN: %d", ptr);
    } else {
        console_printf("GETPOS FAILURE... ");
    }
    console_printf("\n");
}

static void setpos_test(struct io_intf *blkio) {
    uint64_t ptr = 43;
    blkio->ops->ctl(blkio, IOCTL_SETPOS, &ptr);

    console_printf("\n*******  SETPOS Test  *******\n\n");
    if (ptr) {
        console_printf("SETPOS SUCCESS... LEN: %d", ptr);
    } else {
        console_printf("SETPOS FAILURE... ");
    }
    console_printf("\n");
}

static void getblksz_test(struct io_intf *blkio) {
    uint64_t ptr = 0;
    blkio->ops->ctl(blkio, IOCTL_GETBLKSZ, &ptr);

    console_printf("\n*******  GETBLKSZ Test  *******\n\n");
    if (ptr) {
        console_printf("GETBLKSZ SUCCESS... LEN: %d", ptr);
    } else {
        console_printf("GETBLKSZ FAILURE... ");
    }
    console_printf("\n");
}



int io_lit_ops_test(){
    int test_result = 0;
    struct io_intf * test = NULL;
    test_result = fs_mount(test);
    if (test_result != -1){
        return TEST_FAILURE;
    }

    char buf[4096];
    buf[25] = 't';
    buf[27] = 'a';

    struct io_lit test_lit;
    

    
    test = iolit_init(&test_lit, &buf, 4096);
    

    char t[4096];
    for (int i = 0; i < 4096; i++){
        t[i] = 'a';
    }

    size_t size = 4090;

    test_result = test_lit.io_intf.ops->ctl(&test_lit.io_intf, IOCTL_SETPOS, &size);

    test_result = test_lit.io_intf.ops->write(&test_lit.io_intf, &t, 10);

    test_result = test_lit.io_intf.ops->ctl(&test_lit.io_intf, IOCTL_SETPOS, &size);

    
    test_result = test_lit.io_intf.ops->read(&test_lit.io_intf, &t, 10);

    console_printf("%d\n", test_result);
    console_printf("%u\n", t[0]);
    console_printf("%u\n", t[1]);
    console_printf("%u\n", t[2]);


    // test_result = fs_mount(test);

    return TEST_SUCCESS;
}

int test_mount(){
    int mount_result = 0;
    struct io_intf * test = NULL;
    mount_result = fs_mount(test);
    if (mount_result != -1){
        return TEST_FAILURE;
    }

    void * buf = _companion_f_start;
    size_t size = _companion_f_end - _companion_f_start;

    struct io_lit fslit;


    struct io_intf * fs_io = iolit_init(&fslit, buf, size);

    mount_result = fs_mount(fs_io);

    if (mount_result == 0){
        return TEST_SUCCESS;
    }
    return TEST_FAILURE;
}

int test_fs_open(){

    void * buf = _companion_f_start;
    size_t size = _companion_f_end - _companion_f_start;

    struct io_lit fslit;


    struct io_intf * fs_io = iolit_init(&fslit, buf, size);

    fs_mount(fs_io);

    int open_result = fs_open("text.txt", &fs_io);

    fs_close(fs_io);
    fs_io = iolit_init(&fslit, buf, size);
    open_result = fs_open("text.txt", &fs_io);

    if (open_result == 0 && fs_io != NULL){
        return TEST_SUCCESS;
    }

    return TEST_FAILURE;
}

int test_fs_read_write_open_close(){

    void * buf = _companion_f_start;
    size_t size = _companion_f_end - _companion_f_start;

    struct io_lit fslit;
    struct io_intf * fs_io = iolit_init(&fslit, buf, size);

    fs_mount(fs_io);
    fs_open("text.txt", &fs_io);

    fs_close(fs_io);
    fs_io = iolit_init(&fslit, buf, size);
    fs_open("text.txt", &fs_io);

    char buffer[15];
    int bytes = 15;

    for (int i = 0; i < 15; i++){
        buffer[i] = 'a';
    }

    fs_ioctl(fs_io, IOCTL_SETPOS, 0);
    int write_bytes = fs_write(fs_io, buffer, bytes);

    if (write_bytes != bytes){
        return TEST_FAILURE;
    }

    char buffer_read[15];

    int ioctl = fs_ioctl(fs_io, IOCTL_SETPOS, 0);
    if (ioctl != 0){
        return TEST_FAILURE;
    }
    int read_bytes = fs_read(fs_io, buffer, bytes);

    if (read_bytes != bytes){
        return TEST_FAILURE;
    }

    for (int i = 0; i < 15; i++){
        if (buffer[i] != buffer_read[i]){
            return TEST_FAILURE;
        }
    }

    return TEST_SUCCESS;
}

// Function to run all tests and display overall results
int run_all_tests_fs() {
    console_printf("Running all fs/iolit tests...\n");
    int status = TEST_SUCCESS;
    

    status &= test_mount();
    status &= test_fs_open();
    status &= test_fs_read_write_open_close();
    

    if (status == TEST_SUCCESS) {
        console_printf("All tests passed successfully.\n");
    } 
    else {
        console_printf("Some tests failed.\n");
    }
    return status;
}

int elf_test(){
    console_printf("ELF TEST: ENRTRY POINTER\n");
    struct io_lit elflit;
    struct io_intf * elfio;
    void (*exe_entry)(struct io_intf*);

    void * buf = _companion_f_start;
    size_t size = _companion_f_end - _companion_f_start;

    elfio = iolit_init(&elflit, buf, size);
  
    int result = elf_load(elfio, &exe_entry);

    if (result == 0){
        console_printf("ELF Load entry pointer: %p\n", exe_entry);
        console_printf("ELF TEST: ENTRY POINTER PASSED\n");
        console_printf("---------------------------------\n");
        return 1;
    }
    else{
        console_printf("ELF TEST: ENTRY POINTER FAILED\n");
        console_printf("---------------------------------\n");
        return 0;
    }
}

int elf_test_deny(){
    console_printf("ELF TEST: DENY LSB\n");
    struct io_lit elflit;
    struct io_intf * elfio;
    void (*exe_entry)(struct io_intf*);

    char buf[66]; 
    buf[0] = 0x7f; // magic nums
    buf[1] = 'E';
    buf[2] = 'L';
    buf[3] = 'F';
    buf[4] = 2; // ELFCLASS64
    buf[5] = 0; // Not little endian(1)
    buf[6] = 1; // CURRENT
    buf[16] = 2; // EXEC

    elfio = iolit_init(&elflit, buf, 66);
    int result = elf_load(elfio, &exe_entry);
     
    if (result < 0){
        console_printf("error code of invalid elf: %d\n", result);
        console_printf("ELF TEST: DENY PASSED\n");
        console_printf("---------------------------------\n");
        return 1;
    }
    else{
        console_printf("error code: %d\n", result);
        console_printf("ELF TEST: DENY FAILED\n");
        console_printf("---------------------------------\n");
        return 0;
    }
}


