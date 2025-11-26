// lock.h - A sleep lock
//

#ifdef LOCK_TRACE
#define TRACE
#endif

#ifdef LOCK_DEBUG
#define DEBUG
#endif

#ifndef _LOCK_H_
#define _LOCK_H_

#include "thread.h"
#include "halt.h"
#include "console.h"
#include "intr.h"

struct lock {
    struct condition cond;
    int tid; // thread holding lock or -1
};

static inline void lock_init(struct lock * lk, const char * name);
static inline void lock_acquire(struct lock * lk);
static inline void lock_release(struct lock * lk);

// INLINE FUNCTION DEFINITIONS
//

static inline void lock_init(struct lock * lk, const char * name) {
    trace("%s(<%s:%p>", __func__, name, lk);
    condition_init(&lk->cond, name);
    lk->tid = -1;
}

/*
Inputs: struct lock pointer
Outputs: none
Purpose: Check if lock is unlocked, if locked then wait till it is unlocked. When is it is unlocked,
set owner to running thread. Also disable interrupts as it is a critcal section with shared resources
*/
static inline void lock_acquire(struct lock * lk) {
    // TODO: FIXME implement this
    int s = intr_disable(); // disable interrupts
    while (lk->tid != -1){ // loop to check lk is unlocked
        condition_wait(&lk->cond); // wait till condition
    }
    lk->tid = running_thread(); // set new owner
    intr_restore(s); // restore interrupts
    return; // ret
}

static inline void lock_release(struct lock * lk) {
    trace("%s(<%s:%p>", __func__, lk->cond.name, lk);

    assert (lk->tid == running_thread());
    
    lk->tid = -1;
    condition_broadcast(&lk->cond);

    debug("Thread <%s:%d> released lock <%s:%p>",
        thread_name(running_thread()), running_thread(),
        lk->cond.name, lk);
}

#endif // _LOCK_H_