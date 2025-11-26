#include "syscall.h"
#include "string.h"

void main(void) {

    int * kern_page = (int*)0x80000000;

    int fault_read = *kern_page;        // causes load page fault because address is in kernel

    fault_read = fault_read;



}