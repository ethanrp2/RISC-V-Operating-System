// process.c - user process
//

#include "process.h"

#ifdef PROCESS_TRACE
#define TRACE
#endif

#ifdef PROCESS_DEBUG
#define DEBUG
#endif


// COMPILE-TIME PARAMETERS
//

// NPROC is the maximum number of processes

#ifndef NPROC
#define NPROC 16
#endif

// INTERNAL FUNCTION DECLARATIONS
//

// INTERNAL GLOBAL VARIABLES
//

#define MAIN_PID 0

// The main user process struct

static struct process main_proc;

// A table of pointers to all user processes in the system

struct process * proctab[NPROC] = {
    [MAIN_PID] = &main_proc
};

// EXPORTED GLOBAL VARIABLES
//

char procmgr_initialized = 0;

// EXPORTED FUNCTION DEFINITIONS
//

/*
Inputs: none
Outputs: none
Purpose: Initializes the proces manager by wrapping the main_thread in a process. Sets the values of its process struct and signals
        initialization.
*/

void procmgr_init(void){
    main_proc.id = MAIN_PID;            // set process ID
    main_proc.tid = running_thread();       // set thread ID
    main_proc.mtag = active_memory_space();     // set mtag
    thread_set_process(main_proc.tid, &main_proc);      // set process to this process

    procmgr_initialized = 1;
}

/*
Inputs: exeio
Outputs: int status
Purpose: Executes process from given elf io_intf pointer. First unmaps all previous user mappings. Then loads the elf file, and jumps to user
        to begin running the user file. Exits the process gracefully upon finish.
*/

int process_exec(struct io_intf *exeio){
    if (procmgr_initialized == 0){
        return -1;
    }
    void (*exe_entry)(void);

    memory_space_reclaim();               // unmap previous mappings

    int result = elf_load(exeio, &exe_entry);           // load the elf file from the io_intf

    if (result < 0){
        return result;
    }

    // console_printf("%x\n", exe_entry);

    thread_jump_to_user(USER_STACK_VMA, (uintptr_t)exe_entry);      // jump to user using the user stack and the entry pointer from the ELF

    process_exit();             // call process exit to complete
    return 0;
}

/*
Inputs: none
Outputs: none
Purpose: Reclaims the memory space that was used by this process, and resets all of the device io table entries. Exit the thread 
        to finish process completely.
*/

void process_exit(void){

    memory_space_reclaim();             // unmap this process' mappings

    struct process * proc = current_process();          // find the current process

    

    for (int i = 0; i < PROCESS_IOMAX; i++){            // loop to close and reset all io_table

        if (proc->iotab[i]){    
        ioclose(proc->iotab[i]);
        proc->iotab[i] = NULL;
        }   
    }


    proctab[proc->id] = NULL;               // remove this process from the process table

    thread_exit();          // call thread_exit


}
