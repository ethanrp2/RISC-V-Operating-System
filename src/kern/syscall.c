// syscall.c - System calls
//

#include "../user/scnum.h"
#include "../user/syscall.h"
#include "error.h"
#include "trap.h"
#include "process.h"
#include "memory.h"
#include "fs.h"
#include "console.h"
#include "device.h"
#include "timer.h"
#include "thread.h"
#include "heap.h"

int64_t syscall(struct trap_frame * tfr); // declare helper function


/*
Inputs: void
Outputs: static int
Purpose: calls process exit and returns
*/
static int sysexit(void){
    process_exit(); // exit process
    return 0; // return
}

/*
Inputs: const char message
Outputs: static int
Purpose: prints the message to the console and returns
*/
static int sysmsgout(const char *msg){
    // int result; 
    trace("%s(msg=%p)", __func__, msg);

    // result = memory_validate_vstr(msg, PTE_U); // make sure msg is valid 

    // if (result != 0){ // check if invalid 
    //     return result; // return error code
    // }

    kprintf("Thread <%s,:%d> says: %s\n", thread_name(running_thread()), running_thread(), msg); // print message 
    return 0; // return 
}

/*
Inputs: int file descriptor, char name, and and int instance number
Outputs: static int
Purpose: checks if it is a valid fd, where if it is negative, it will find the next available fd. then it will call device open
         at the fd and set the new io interface in the iotab table. also returns error number if error occurs
*/
static int sysdevopen(int fd, const char *name, int instno){
    // case 1 if fd >= 0
    if (fd >= PROCESS_IOMAX){ // check if fd valid
        return EINVAL; // return error
    }
    // case 2 if fd < 0
    if (fd < 0){ // get next avaialble fd
        int i = 0; // variable for checking io_tab
        while (current_process()->iotab[i] != NULL || i >= PROCESS_IOMAX){ // loop through io_tab
            i++; // increment i
        }
        if (i >= PROCESS_IOMAX){ // check if io_tab full
            return EMFILE; // return error
        }
        fd = i; // set fd to new available fd
    }
    
    struct io_intf * new_io; // make new io
    int result; // variable for result
    result = device_open(&new_io, name, instno); // open device

    if (result < 0){ // check if opened
        return result; // return error
    }
    current_process()->iotab[fd] = new_io; // set new io in io_tab
    return fd; // return file descriptor
}


/*
Inputs: int file descriptor, char name
Outputs: static int
Purpose: checks if it is a valid fd, where if it is negative, it will find the next available fd. then it will call file system open
         at the fd and set the new io interface in the iotab table. also returns error number if error occurs
*/
static int sysfsopen(int fd, const char *name){
    // case 1 if fd >= 0
    if (fd >= PROCESS_IOMAX){ // check if fd valid
        return EINVAL; // return error
    }
    //case 2 if fd < 0
    if (fd < 0){ // get next avaialble fd
        int i = 0; // variable for checking io_tab
        while (current_process()->iotab[i] != NULL || i >= PROCESS_IOMAX){ // loop through io_tab
            i++; // increment i
        }
        if (i >= PROCESS_IOMAX){ // check if io_tab full
            return EMFILE; // return error
        }
        fd = i; // set fd to new available fd
    }
    
    struct io_intf * new_io; // make new io
    int result; // variable for result
    result = fs_open(name, &new_io); // open fs

    if (result < 0){ // check if opened
        return result; // return error
    }
    current_process()->iotab[fd] = new_io; // set new io in io_tab
    return fd; // return file descirptor 
}

/*
Inputs: int file descriptor
Outputs: static int
Purpose: checks if it is a valid fd and checks if the io interface of the fd is valid. then calls ioclose on the io interface adn sets it to null 
         in the iotab. also returns error if error occurs
*/
static int sysclose(int fd){
    if (fd < 0 || fd >= PROCESS_IOMAX){ // check if fd valid
        return EINVAL; // return error
    }

    struct io_intf * io_process = current_process()->iotab[fd]; // set variable to io interface of fd

    if (io_process == NULL){ // check if invalid 
        return EINVAL; // return error
    }
    
    ioclose(io_process); // close io
    current_process()->iotab[fd] = NULL; // set to NULL
    return 0; // return
}

/*
Inputs: int file descriptor, void pointer buf, and size_t buf size
Outputs: static long
Purpose: checks if it is a valid fd and checks if the io interface of the fd is valid. then calls ioread  with the 
         associated io interface and returns that value. also returns error if error occurs
*/
static long sysread(int fd, void *buf, size_t bufsz){
    if (fd < 0 || fd >= PROCESS_IOMAX){ // check if fd valid
        return EINVAL; // return error
    }
    // int validate_result; // variable for result

    // validate_result = memory_validate_vptr_len(buf, bufsz, PTE_W | PTE_U); // call validating function

    // if (validate_result != 0){ // check if invalid 
    //     return validate_result; // return error
    // }

    struct io_intf * io_process = current_process()->iotab[fd]; // set variable to io interface of fd

    if (io_process == NULL){ // check if invalid 
        return EINVAL; // return error
    }

    return ioread(io_process, buf, (unsigned long)bufsz); // return read
}


/*
Inputs: int file descriptor, void pointer buf, and size_t length
Outputs: static long
Purpose: checks if it is a valid fd and checks if the io interface of the fd is valid. then calls iowrite with the 
         associated io interface and returns that value. also returns error if error occurs
*/
static long syswrite(int fd, const void *buf, size_t len){
    if (fd < 0 || fd >= PROCESS_IOMAX){ // check if fd valid 
        return EINVAL; // return error
    }

    // int validate_result; // variable for result 

    // validate_result = memory_validate_vptr_len(buf, len, PTE_R | PTE_U); // call validating function

    // if (validate_result != 0){ // check if invalid
    //     return validate_result; // return error
    // }

    struct io_intf * io_process = current_process()->iotab[fd]; // set variable to io interface of fd

    if (io_process == NULL){ // check if invalid 
        return EINVAL; // return error
    }

    return iowrite(io_process, buf, (unsigned long)len); // return write
}


/*
Inputs: int file descriptor, itn command, and void pointer arg
Outputs: static int
Purpose: checks if it is a valid fd and checks if the io interface of the fd is valid. then calls ioctl with the 
         associated io interface and returns that value. also returns error if error occurs
*/
static int sysioctl(int fd, int cmd, void *arg){

    
    if (fd < 0 || fd >= PROCESS_IOMAX){ // check if fd valid
        return EINVAL; // return error
    }

    struct io_intf * io_process = current_process()->iotab[fd]; // set variable to io interface of fd

    if (io_process == NULL){ // check if invalid 
        return EINVAL; // return error
    }


    return ioctl(io_process, cmd, arg); // return ioctl
}

/*
Inputs: int file descriptor
Outputs: static int
Purpose: checks if it is a valid fd and checks if the io interface of the fd is valid. then calls process exec with the 
         associated io interface and returns that value. also returns error if error occurs
*/
static int sysexec(int fd){
    if (fd < 0 || fd >= PROCESS_IOMAX){ // check if fd valid
        return EINVAL; // return error
    }

    struct io_intf * io_process = current_process()->iotab[fd]; // set variable to io interface of fd

    if (io_process == NULL){ // check if invalid 
        return EINVAL; // return error
    }

    return process_exec(io_process); // return exec
}

/*
Inputs: struct trap frame pointer
Outputs: int
Purpose: Gets the current proccess and finds the next available process id. Then copies the 
iotab of the parent into the child. Finishes by calling the thread fork to user and returning the thread id.
*/
static int sysfork(const struct trap_frame * tfr) {
    struct process* proc = current_process();          // find the current process

    int i = 1;
    int pid = 1;
    while (i < 16) { // loop through proctab to find next available proccess id
        if (proctab[i] == NULL) {
            pid = i;
            break;
        }
        i++;
    }
    console_printf("reached kmalloc\n");
    proctab[pid] = kmalloc(sizeof(struct process));
    
    proctab[pid]->id = pid; // set the id

    struct process* child = proctab[pid]; // ptr to process

    for (int i = 0; i < PROCESS_IOMAX; i++){ // loop through and copy iotab
        if (proc->iotab[i]){    
            child->iotab[i] = proc->iotab[i];
            ioref(child->iotab[i]);
        }   
    }

    thread_fork_to_user(proctab[pid], tfr); // call thread fork
    return proctab[pid]->tid; // return thread id
}

/*
Inputs: tid (32 bit)
Outputs: child thread id
Purpose: Wait for certain child to exit before returning, if tid is main thread, wait for any child of current thread to exit
*/
static int syswait(int tid) {
    if (tid == 0 ) {
        return thread_join_any();
    } else {
        return thread_join(tid);
    }
}

/*
Inputs: us (64-bit)
Outputs: return code
Purpose: Sleep for us number of microseconds 
*/
static int sysusleep(unsigned long us) {
    struct alarm s_alarm;
    alarm_init(&s_alarm, "usleep");
    // uint64_t timer_count = (us * TIMER_FREQ) / 1000000;
    alarm_sleep_us(&s_alarm, us);
    alarm_reset(&s_alarm);

    return 0;
}

/*
Inputs: struct trap frame
Outputs: none
Purpose: sets a0 as the return value with a helper function. the helper function uses a switch by checking a7 to call the 
         associated syscall and returns the function
*/
extern void syscall_handler(struct trap_frame *tfr){

    if (tfr == NULL){ // check if trap frame invalid 
        return; // return
    }

    tfr->sepc += 4; // next instruction

    tfr->x[TFR_A0] = syscall(tfr); // call helper for handler
}

/*
Inputs: struct trap frame
Outputs: int64_t 
Purpose: checks a7 and calls the associated function with the code. also uses a0, a1, and a2 as inputs if needed for the parameters
         of the function call
*/
int64_t syscall(struct trap_frame * tfr){
    switch(tfr->x[TFR_A7]){

        // check each case of system calls and return respective function
        case SYSCALL_EXIT:
            return sysexit();
        
        case SYSCALL_MSGOUT:
            return sysmsgout((const char *)tfr->x[TFR_A0]);
        
        case SYSCALL_DEVOPEN:
            return sysdevopen((int)tfr->x[TFR_A0], (const char *)tfr->x[TFR_A1], (int)tfr->x[TFR_A2]);

        case SYSCALL_FSOPEN:
            return sysfsopen((int)tfr->x[TFR_A0], (const char *)tfr->x[TFR_A1]);

        case SYSCALL_CLOSE:
            return sysclose((int)tfr->x[TFR_A0]);
        
        case SYSCALL_READ:
            return sysread((int)tfr->x[TFR_A0], (void *)tfr->x[TFR_A1], (size_t)tfr->x[TFR_A2]);
        
        case SYSCALL_WRITE:
            return syswrite((int)tfr->x[TFR_A0], (void *)tfr->x[TFR_A1], (size_t)tfr->x[TFR_A2]);

        case SYSCALL_IOCTL:
            return sysioctl((int)tfr->x[TFR_A0], (int)tfr->x[TFR_A1], (void *)tfr->x[TFR_A2]);
        
        case SYSCALL_EXEC:
            return sysexec((int)tfr->x[TFR_A0]);

        case SYSCALL_USLEEP:
            return sysusleep((unsigned long)tfr->x[TFR_A0]);
        
        case SYSCALL_WAIT:
            return syswait((int)tfr->x[TFR_A0]);
        case SYSCALL_FORK:
            return sysfork(tfr);
        default:
            return EINVAL;

    }
}