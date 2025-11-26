#include "syscall.h"
#include "string.h"

void main(void) {

    int * kern_page = (int*)0x80000000;

    *kern_page = 0x10002000;            /// causes store fault because address is in kernel




}