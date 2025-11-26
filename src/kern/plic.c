// plic.c - RISC-V PLIC
//

#include "plic.h"
#include "console.h"

#include <stdint.h>

// COMPILE-TIME CONFIGURATION
//

// *** Note to student: you MUST use PLIC_IOBASE for all address calculations,
// as this will be used for testing!

#ifndef PLIC_IOBASE
#define PLIC_IOBASE 0x0C000000
#endif

#define PLIC_PEND_OFFSET 0x001000
#define PLIC_ENABLE_OFFST 0x002000
#define PLIC_ENB_CTX_OFFST 0x80
#define PLIC_CTX_THRESH_OFF 0x200000
#define PLIC_CTXNO_OFF 0x1000
#define PLIC_CLAIM_OFF 0x200004

#define PLIC_SRCCNT 0x400
#define PLIC_CTXCNT 1

// INTERNAL FUNCTION DECLARATIONS
//

// *** Note to student: the following MUST be declared extern. Do not change these
// function delcarations!

extern void plic_set_source_priority(uint32_t srcno, uint32_t level);
extern int plic_source_pending(uint32_t srcno);
extern void plic_enable_source_for_context(uint32_t ctxno, uint32_t srcno);
extern void plic_disable_source_for_context(uint32_t ctxno, uint32_t srcno);
extern void plic_set_context_threshold(uint32_t ctxno, uint32_t level);
extern uint32_t plic_claim_context_interrupt(uint32_t ctxno);
extern void plic_complete_context_interrupt(uint32_t ctxno, uint32_t srcno);

// Currently supports only single-hart operation. The low-level PLIC functions
// already understand contexts, so we only need to modify the high-level
// functions (plic_init, plic_claim, plic_complete).

// EXPORTED FUNCTION DEFINITIONS
// 

void plic_init(void) {
    int i;

    // Disable all sources by setting priority to 0, enable all sources for
    // context 0 (M mode on hart 0).

    for (i = 0; i < PLIC_SRCCNT; i++) {
        plic_set_source_priority(i, 0);
        plic_enable_source_for_context(1, i);

        plic_enable_source_for_context(1, i); // changed
        // plic_enable_source_for_context(2, i); // changed
    }
}

extern void plic_enable_irq(int irqno, int prio) {
    trace("%s(irqno=%d,prio=%d)", __func__, irqno, prio);
    plic_set_source_priority(irqno, prio);
}

extern void plic_disable_irq(int irqno) {
    if (0 < irqno)
        plic_set_source_priority(irqno, 0);
    else
        debug("plic_disable_irq called with irqno = %d", irqno);
}

extern int plic_claim_irq(void) {
    // Hardwired context 0 (M mode on hart 0)
    trace("%s()", __func__);
    return plic_claim_context_interrupt(1);     // changed
}

extern void plic_close_irq(int irqno) {
    // Hardwired context 0 (M mode on hart 0)
    trace("%s(irqno=%d)", __func__, irqno);
    plic_complete_context_interrupt(1, irqno);      // changed
}

// INTERNAL FUNCTION DEFINITIONS
//
/*
Purpose: Sets a priority level for a specific interrupt source
Arguments: srcno (32-bit), level (32-bit)
Side Effects: Accesses memory to update priority array
*/
void plic_set_source_priority(uint32_t srcno, uint32_t level) {
    // FIXME your code goes here
                                                                                            // 4 magic number is 4 bytes per srcno
    volatile uint32_t* pointer = (volatile uint32_t*)((uint64_t)(PLIC_IOBASE + 4 * srcno)); // calculate address for priority array and make pointer
    (*pointer) = level; // put data in priority array
}

/*
Purpose: Checks if an interrupt source is pending by inspecting the pending array
Arguments: srcno (32-bit)
Side Effects: Accesses memory to check pending array
*/
int plic_source_pending(uint32_t srcno) {
    // FIXME your code goes here
                                                                                                                    // 32 magic number resembles 32 bits in 4 bytes (how memory is stored)
    volatile uint32_t* pointer = (volatile uint32_t*)((uint64_t)(PLIC_IOBASE + PLIC_PEND_OFFSET + (srcno/32) * 4)); // calculate address for pending array and make pointer
    uint32_t val = *pointer & (1 << (srcno % 32)); // get value at respective position in array
    return val == 0 ? 0 : 1; //  return if there is a pending interrupt
}

/*
Purpose: Enables a specific interrupt for given context
Arguments: srcno (32-bit), ctxno (32-bit)
Side Effects: Accesses memory to update enable array
*/
void plic_enable_source_for_context(uint32_t ctxno, uint32_t srcno) {
    // FIXME your code goes here
    // 32 magic number resembles 32 bits in 4 bytes (how memory is stored)
    uint64_t addr = (uint64_t)(PLIC_IOBASE + PLIC_ENABLE_OFFST + (ctxno * PLIC_ENB_CTX_OFFST) + (srcno/32) * 4); // calculate addr in enable array in mememory
    volatile uint32_t* pointer = (volatile uint32_t*) addr; // make pointer to respective position in array
    (*pointer) |= (1 << (srcno % 32)); //  set respective bit
}

/*
Purpose: Disables a specific interrupt for given context
Arguments: srcid (32-bit), level (32-bit)
Side Effects: Accesses memory to update enable array
*/
void plic_disable_source_for_context(uint32_t ctxno, uint32_t srcid) {
    // FIXME your code goes here
    // 32 magic number resembles 32 bits in 4 bytes (how memory is stored)
    volatile uint32_t* pointer = (volatile uint32_t*)((uint64_t)(PLIC_IOBASE + PLIC_ENABLE_OFFST + (ctxno * PLIC_ENB_CTX_OFFST) + (srcid/32) * 4)); // calculate addr in enable array and make pointer
    (*pointer) &= ~(1 << (srcid % 32)); // Clear respective bits 
}

/*
Purpose: Sets the interrupt priority threshhold for a specific context
Arguments: ctxno (32-bit), level (32-bit)
Side Effects: Only allows interrupts of a certain level to pass through
*/
void plic_set_context_threshold(uint32_t ctxno, uint32_t level) {
    // FIXME your code goes here
    volatile uint32_t* pointer = (volatile uint32_t*)((uint64_t)(PLIC_IOBASE + PLIC_CTX_THRESH_OFF + (ctxno * PLIC_CTXNO_OFF))); // calc addr in memory for setting context threshold and make pointer
    (*pointer) = level; // set value at respective memory addr
}

/*
Purpose: Claims interrupts for a given context
Arguments: ctxno (32-bit)
Side Effects: Accesses memory to read claim register and returns interrupt ID
*/
uint32_t plic_claim_context_interrupt(uint32_t ctxno) {
    // FIXME your code goes here
    volatile uint32_t* pointer = (volatile uint32_t*)((uint64_t)(PLIC_IOBASE + PLIC_CLAIM_OFF + (ctxno * PLIC_CTXNO_OFF))); // calc addr to claim reg in memory and make pointer
    return *pointer; // return the value at that addr
}

/*
Purpose: Completes the handling of an interrupt for a given context
Arguments: srcno (32-bit), ctxno (32-bit)
Side Effects: Writes interrupt srcno back to claim register
*/
void plic_complete_context_interrupt(uint32_t ctxno, uint32_t srcno) {
    // FIXME your code goes here
    volatile uint32_t* pointer = (volatile uint32_t*)((uint64_t)(PLIC_IOBASE + PLIC_CLAIM_OFF + (ctxno * PLIC_CTXNO_OFF))); // calc address to claim reg in memory and make pointer
    (*pointer) = srcno; // set value to srcno to indicate that interrupt is serviced
}