#include "syscall.h"
#include "string.h"
#define IOCTL_SETPOS        4

void main(void) {
    int result;
    result = _fsopen(0, "story.txt");

    if (result < 0) {
        _msgout("_fsopen failed");
        _exit();
    }

    void * buf[100];

    result = _read(0, buf, 100);

    if (result < 0){
        _msgout("_read failed");
        _exit();
    }

    _msgout((char *)buf);

    size_t pos = 100; // set position to another value later in the story
    size_t * arg = &pos;


    _ioctl(0, IOCTL_SETPOS, arg);

    result = _read(0, buf, 100);

    if (result < 0){
        _msgout("_read after ioctl failed");
        _exit();
    }

    _msgout((char *)buf);


    _close(0);


}