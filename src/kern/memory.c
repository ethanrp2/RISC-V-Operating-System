// memory.c - Memory management
//

#ifndef TRACE
#ifdef MEMORY_TRACE
#define TRACE
#endif
#endif

#ifndef DEBUG
#ifdef MEMORY_DEBUG
#define DEBUG
#endif
#endif

#include "config.h"

#include "memory.h"
#include "console.h"
#include "halt.h"
#include "heap.h"
#include "csr.h"
#include "string.h"
#include "error.h"
#include "thread.h"
#include "process.h"

#include <stdint.h>

// EXPORTED VARIABLE DEFINITIONS
//

char memory_initialized = 0;
uintptr_t main_mtag;

// IMPORTED VARIABLE DECLARATIONS
//

// The following are provided by the linker (kernel.ld)

extern char _kimg_start[];
extern char _kimg_text_start[];
extern char _kimg_text_end[];
extern char _kimg_rodata_start[];
extern char _kimg_rodata_end[];
extern char _kimg_data_start[];
extern char _kimg_data_end[];
extern char _kimg_end[];

// INTERNAL TYPE DEFINITIONS
//

union linked_page {
    union linked_page * next;
    char padding[PAGE_SIZE];
};

struct pte {
    uint64_t flags:8;
    uint64_t rsw:2;
    uint64_t ppn:44;
    uint64_t reserved:7;
    uint64_t pbmt:2;
    uint64_t n:1;
};

// INTERNAL MACRO DEFINITIONS
//

#define VPN2(vma) (((vma) >> (9+9+12)) & 0x1FF)
#define VPN1(vma) (((vma) >> (9+12)) & 0x1FF)
#define VPN0(vma) (((vma) >> 12) & 0x1FF)
#define MIN(a,b) (((a)<(b))?(a):(b))

// #define USER_P_START 0x80200000
#define USER_VMA_START 0xC0000000
#define USER_VMA_END 0xD0000000


// INTERNAL FUNCTION DECLARATIONS
//

static inline int wellformed_vma(uintptr_t vma);
static inline int wellformed_vptr(const void * vp);
static inline int aligned_addr(uintptr_t vma, size_t blksz);
static inline int aligned_ptr(const void * p, size_t blksz);
static inline int aligned_size(size_t size, size_t blksz);

static inline uintptr_t active_space_mtag(void);
static inline struct pte * mtag_to_root(uintptr_t mtag);
static inline struct pte * active_space_root(void);

static inline void * pagenum_to_pageptr(uintptr_t n);
static inline uintptr_t pageptr_to_pagenum(const void * p);

static inline void * round_up_ptr(void * p, size_t blksz);
static inline uintptr_t round_up_addr(uintptr_t addr, size_t blksz);
static inline size_t round_up_size(size_t n, size_t blksz);
static inline void * round_down_ptr(void * p, size_t blksz);
static inline size_t round_down_size(size_t n, size_t blksz);
static inline uintptr_t round_down_addr(uintptr_t addr, size_t blksz);

static struct pte* get_valid_ppn (uintptr_t vma);





static inline struct pte leaf_pte (
    const void * pptr, uint_fast8_t rwxug_flags);
static inline struct pte ptab_pte (
    const struct pte * ptab, uint_fast8_t g_flag);
static inline struct pte null_pte(void);

static inline void sfence_vma(void);

// INTERNAL GLOBAL VARIABLES
//

static union linked_page * free_list;
static struct pte to_clone;

static struct pte main_pt2[PTE_CNT]
    __attribute__ ((section(".bss.pagetable"), aligned(4096)));
static struct pte main_pt1_0x80000[PTE_CNT]
    __attribute__ ((section(".bss.pagetable"), aligned(4096)));
static struct pte main_pt0_0x80000[PTE_CNT]
    __attribute__ ((section(".bss.pagetable"), aligned(4096)));


// EXPORTED VARIABLE DEFINITIONS
//

// EXPORTED FUNCTION DEFINITIONS
// 

void memory_init(void) {
    const void * const text_start = _kimg_text_start;
    const void * const text_end = _kimg_text_end;
    const void * const rodata_start = _kimg_rodata_start;
    const void * const rodata_end = _kimg_rodata_end;
    const void * const data_start = _kimg_data_start;
    // union linked_page * page;
    void * heap_start;
    void * heap_end;
    size_t page_cnt;
    uintptr_t pma;
    const void * pp;

    trace("%s()", __func__);

    assert (RAM_START == _kimg_start);

    kprintf("           RAM: [%p,%p): %zu MB\n",
        RAM_START, RAM_END, RAM_SIZE / 1024 / 1024);
    kprintf("  Kernel image: [%p,%p)\n", _kimg_start, _kimg_end);

    // Kernel must fit inside 2MB megapage (one level 1 PTE)
    
    if (MEGA_SIZE < _kimg_end - _kimg_start)
        panic("Kernel too large");

    // Initialize main page table with the following direct mapping:
    // 
    //         0 to RAM_START:           RW gigapages (MMIO region)
    // RAM_START to _kimg_end:           RX/R/RW pages based on kernel image
    // _kimg_end to RAM_START+MEGA_SIZE: RW pages (heap and free page pool)
    // RAM_START+MEGA_SIZE to RAM_END:   RW megapages (free page pool)
    //
    // RAM_START = 0x80000000
    // MEGA_SIZE = 2 MB
    // GIGA_SIZE = 1 GB
    
    // Identity mapping of two gigabytes (as two gigapage mappings)
    for (pma = 0; pma < RAM_START_PMA; pma += GIGA_SIZE)
        main_pt2[VPN2(pma)] = leaf_pte((void*)pma, PTE_R | PTE_W | PTE_G);
    
    // Third gigarange has a second-level page table
    main_pt2[VPN2(RAM_START_PMA)] = ptab_pte(main_pt1_0x80000, PTE_G);

    // First physical megarange of RAM is mapped as individual pages with
    // permissions based on kernel image region.

    main_pt1_0x80000[VPN1(RAM_START_PMA)] =
        ptab_pte(main_pt0_0x80000, PTE_G);

    for (pp = text_start; pp < text_end; pp += PAGE_SIZE) {
        main_pt0_0x80000[VPN0((uintptr_t)pp)] =
            leaf_pte(pp, PTE_R | PTE_X | PTE_G);
    }

    for (pp = rodata_start; pp < rodata_end; pp += PAGE_SIZE) {
        main_pt0_0x80000[VPN0((uintptr_t)pp)] =
            leaf_pte(pp, PTE_R | PTE_G);
    }

    for (pp = data_start; pp < RAM_START + MEGA_SIZE; pp += PAGE_SIZE) {
        main_pt0_0x80000[VPN0((uintptr_t)pp)] =
            leaf_pte(pp, PTE_R | PTE_W | PTE_G);
    }

    // Remaining RAM mapped in 2MB megapages

    for (pp = RAM_START + MEGA_SIZE; pp < RAM_END; pp += MEGA_SIZE) {
        main_pt1_0x80000[VPN1((uintptr_t)pp)] =
            leaf_pte(pp, PTE_R | PTE_W | PTE_G);
    }

    // Enable paging. This part always makes me nervous.

    main_mtag =  // Sv39
        ((uintptr_t)RISCV_SATP_MODE_Sv39 << RISCV_SATP_MODE_shift) |
        pageptr_to_pagenum(main_pt2);
    
    csrw_satp(main_mtag);
    sfence_vma();

    // Give the memory between the end of the kernel image and the next page
    // boundary to the heap allocator, but make sure it is at least
    // HEAP_INIT_MIN bytes.

    heap_start = _kimg_end;
    heap_end = round_up_ptr(heap_start, PAGE_SIZE);
    if (heap_end - heap_start < HEAP_INIT_MIN) {
        heap_end += round_up_size (
            HEAP_INIT_MIN - (heap_end - heap_start), PAGE_SIZE);
    }

    if (RAM_END < heap_end)
        panic("Not enough memory");
    
    // Initialize heap memory manager

    heap_init(heap_start, heap_end);

    kprintf("Heap allocator: [%p,%p): %zu KB free\n",
        heap_start, heap_end, (heap_end - heap_start) / 1024);

    free_list = heap_end; // heap_end is page aligned
    page_cnt = (RAM_END - heap_end) / PAGE_SIZE;

    kprintf("Page allocator: [%p,%p): %lu pages free\n",
        free_list, RAM_END, page_cnt);

    // Put free pages on the free page list
    // TODO: FIXME implement this (must work with your implementation of
    // memory_alloc_page and memory_free_page).
    union linked_page * p = free_list;

    for (void * i = free_list; i < RAM_END; i += PAGE_SIZE){                // loop through rest of ram and add pages
        union linked_page * new_page = (union linked_page *)(i);
        p->next = new_page;                                             // add new page to the head of the free list
        p = new_page;
    }

    p->next = NULL;         // set last page next to NULL
    
    // Allow supervisor to access user memory. We could be more precise by only
    // enabling it when we are accessing user memory, and disable it at other
    // times to catch bugs.

    csrs_sstatus(RISCV_SSTATUS_SUM);

    memory_initialized = 1;
}

/*
Inputs: none
Outputs: none
Purpose: Reclaims the main memory space. In this case, since memory space is always the same, we can just unmap the user mappings,
        bringing us back to the original main memory mappings (1:1).
*/

void memory_space_reclaim(void){

    csrw_satp(main_mtag);    // Since memory space is always the same, we cna just unmap user to get back to main

}

/*
Inputs: none
Outputs: void * page
Purpose: Removes a page from the free pages list and returns the page for use. Updates the free_list head to remove that page from the free
        pages list.
*/

void * memory_alloc_page(void){

    if (free_list == NULL){
        panic("No Free Pages");             // free list has run out of space
    }

    void * return_page = (void *)(free_list);           // get new page from the free list

    free_list = free_list->next;                    // set head to the next page to remove the used page from the list

    return return_page;         // return the page

}

/*
Inputs: void * pp
Outputs: none
Purpose: Takes in a page pointer and adds it back to the free list. This page is added to the head of the list so that it can be
        used in the future if more free pages are needed.
*/

void memory_free_page(void *pp){

    union linked_page * free_page = (union linked_page *)(pp);      // make a page from the pointer that is coming in
    free_page->next = free_list->next;                  // set up the new page to be at the head of the free list
    free_list = free_page;
    
}

/*
Inputs: vma, flags
Outputs: void * vma
Purpose: Allocates a page from the free list and maps it using the page tables. Walks down the page table structure
        to find the correct location to map and then maps the allocated page to the given VMA. Sets PTE_V, PTE_A, and PTE_D flags
        along with given flags.
*/

void * memory_alloc_and_map_page(uintptr_t vma, uint_fast8_t rwxug_flags){

    void * page = memory_alloc_page();          // get a new page from the free list

    
    uintptr_t level_1;
    uintptr_t level_0;
    struct pte * pt1;
    struct pte * pt0;

    if ((main_pt2[VPN2(vma)].flags & PTE_V) == 0){                  // check level 2 table using VPN2
        struct pte * new_level_1_table = memory_alloc_page();       // make a new table if necessary
        main_pt2[VPN2(vma)] = ptab_pte(new_level_1_table, PTE_V);   // map the new table to the correct index
    }
        
    level_1 = main_pt2[VPN2(vma)].ppn << PAGE_ORDER;                // get access to the level 1 table based on the level 2 result
    pt1 = (struct pte *)(level_1);          // cast to pte


    if ((pt1[VPN1(vma)].flags & PTE_V) == 0){                       // check level 2 table using VPN1
        struct pte * new_level_0_table = memory_alloc_page();       // make a new table if necessary
        pt1[VPN1(vma)] = ptab_pte(new_level_0_table, PTE_V);        // map new table to correct index
    }

    level_0 = pt1[VPN1(vma)].ppn << PAGE_ORDER;                 // get access to the level 0 table that is corresponding to the index
    pt0 = (struct pte *)(level_0);              // cast to pte


    pt0[VPN0(vma)] = leaf_pte(page, rwxug_flags | PTE_A | PTE_D);       // map the new page with the appropriate flags at the correct index


    sfence_vma();       // flush tlb


    return (void*)vma;      
    
}

/*
Inputs: v_address, size, flags
Outputs: void * vma
Purpose: Allocates and maps a range of vma. Calculates how many pages are needed based on the size and calls memory_alloc_and_map
        for each page until finished. Flushes the tlb.
*/

void * memory_alloc_and_map_range(uint64_t v_address, size_t size, uint_fast8_t rwxug_flags){


    if (size <= 0){
        panic ("Size cannot be less than 0");       // size sanity check
    }

    int offset = 0;

    if (size % PAGE_SIZE != 0){             // see if a page is needed for remainder
        offset = 1;
    }

    uint64_t num_pages = (size / PAGE_SIZE) + offset;       // calculate number of pages that need to be allocated
    uint64_t i = 0;


    while (i < num_pages){
        memory_alloc_and_map_page(v_address, rwxug_flags);      // allocate each page 
        v_address += PAGE_SIZE;                                 // increment to next needed vma
        i++;
    }

    sfence_vma();       // flush tlb

    return (void*)v_address;


}

/*
Inputs: vp, flags
Outputs: none
Purpose: Sets flags for the given vma page. Walks down the page table structure to find the correct location to map 
        and then sets flags for that page. Sets PTE_V, along with given flags. If page is not found to be mapped, nothing happens.
*/

void memory_set_page_flags(const void *vp, uint_fast8_t rwxug_flags){

    uintptr_t vma = (uintptr_t)vp;

    uintptr_t level_1;
    uintptr_t level_0;
    struct pte * pt1;
    struct pte * pt0;

    if ((main_pt2[VPN2(vma)].flags & PTE_V) == 0){              // desired page is not found 
        return;
    }
        
    level_1 = main_pt2[VPN2(vma)].ppn << PAGE_ORDER;            // get access to to level 1 table
    pt1 = (struct pte *)(level_1);


    if ((pt1[VPN1(vma)].flags & PTE_V) == 0){                   // desired page is not found
        return;
    }

    level_0 = pt1[VPN1(vma)].ppn << PAGE_ORDER;         // get access to level 0 table
    pt0 = (struct pte *)(level_0);


    pt0[VPN0(vma)].flags = rwxug_flags | PTE_V | PTE_A | PTE_D;         /// set the flags and valid to make sure that it can still get accessed

    sfence_vma();           // flush tlb


}

/*
Inputs: vp, size, flags
Outputs: none
Purpose: Sets flags for the given range of vma pages. Walks down the page table structure to find the correct location to map 
        and then calls memory_set_page_flags for each page. If page is not found to be mapped, nothing happens.
*/

void memory_set_range_flags(const void *vp, size_t size, uint_fast8_t rwxug_flags){

    // uintptr_t vma = (uintptr_t)vp;
    int offset = 0;
    
    if (size % PAGE_SIZE != 0){         // check if we need to set extra page flags for remainder bytes
        offset = 1;
    }

    uint64_t num_pages = (size / PAGE_SIZE) + offset;           // calculate number of pages
    uint64_t i = 0;

    while (i < num_pages){
        memory_set_page_flags(vp, rwxug_flags);             // set flags for each page
        vp += PAGE_SIZE;                    // increment the address to get to next
        i++;
    }

}

void memory_unmap_and_free_user(void){

    uint64_t vma;

    uintptr_t level_1;
    uintptr_t level_0;
    struct pte * pt1;
    struct pte * pt0;


    for (vma = USER_START_VMA; vma < USER_END_VMA; vma += PAGE_SIZE){           // loop through all of user space addresses


        if ((main_pt2[VPN2(vma)].flags & PTE_V) == 0){                  // skip this if not mapped
            break;
        }
            
        level_1 = main_pt2[VPN2(vma)].ppn << PAGE_ORDER;            // get access to level 1 page table
        pt1 = (struct pte *)(level_1);                              // cast to pte


        if ((pt1[VPN1(vma)].flags & PTE_V) == 0){                       // skip this if not mapped
            break;
        }

        level_0 = pt1[VPN1(vma)].ppn << PAGE_ORDER;                 // get access to level 0 table
        pt0 = (struct pte *)(level_0);


        if ((pt0[VPN0(vma)].flags & PTE_V) == 0){                   // skip this if not mapped
            break;
        }

        uintptr_t final = pt0[VPN0(vma)].ppn << PAGE_ORDER;             

        pt0[VPN0(vma)].flags = 0;               // set all flags to 0
        pt0[VPN0(vma)].ppn = 0;                 // set ppn to 0 as well

        memory_free_page((void*)final);         // add free page back to free list

    }

    sfence_vma();               // flush tlb

    
    


}

/*
Inputs: vptr
Outputs: none
Purpose: Handles page fault exception from handler. If VMA is not within the user space bounds, panic to signal a fault. Otherwise, lazy
        allocate a page using the memory_alloc_and_page function.
*/

void memory_handle_page_fault(const void *vptr){

    uintptr_t vp = (uintptr_t)vptr;

    if (vp < USER_VMA_START || vp > USER_END_VMA){          // check bounds of virtual address
        panic("True page fault, not in user space");
    }
    

    memory_alloc_and_map_page(round_down_addr(vp, PAGE_SIZE), (PTE_R | PTE_W | PTE_U));         // lazy allocate page after aligning address

    sfence_vma();       // flush tlb



}

/*
Inputs: asid
Outputs: mtag
Purpose: Shallow copies the global mappings, and then loops through the user mappings to find valid mappings. If its valid, then the 
            vma is mapped in the new space and the data is copied over.
*/

uintptr_t memory_space_clone(uint_fast16_t asid){

    struct pte * mt = (struct pte *)active_space_root();        // get active space for the curent pt2

    struct pte * clone = memory_alloc_page();                   // allocate a new pt2 table


    for(uint16_t i = 0; i < VPN2(USER_START_VMA); i++)
    {
        clone[i] = mt[i];                                       // shallow copy into the new table
    }    

    for(uintptr_t vma = USER_START_VMA; vma <USER_END_VMA; vma += PAGE_SIZE)        // loop through all user addresses
    {
        struct pte * to_clone = get_valid_ppn(vma);                             // check if original had this address mapped

        if (to_clone != NULL){                      // if it was mapped before, deep copy it
            void * page = memory_alloc_page();          // get a new page from the free list

    
            uintptr_t level_1_new;
            uintptr_t level_0_new;
            struct pte * pt1_new;
            struct pte * pt0_new;

            if ((clone[VPN2(vma)].flags & PTE_V) == 0){                  // check level 2 table using VPN2
                struct pte * new_level_1_table = memory_alloc_page();       // make a new table if necessary
                clone[VPN2(vma)] = ptab_pte(new_level_1_table, PTE_V);   // map the new table to the correct index
            }
                
            level_1_new = clone[VPN2(vma)].ppn << PAGE_ORDER;                // get access to the level 1 table based on the level 2 result
            pt1_new = (struct pte *)(level_1_new);          // cast to pte


            if ((pt1_new[VPN1(vma)].flags & PTE_V) == 0){                       // check level 2 table using VPN1
                struct pte * new_level_0_table = memory_alloc_page();       // make a new table if necessary
                pt1_new[VPN1(vma)] = ptab_pte(new_level_0_table, PTE_V);        // map new table to correct index
            }

            level_0_new = pt1_new[VPN1(vma)].ppn << PAGE_ORDER;                 // get access to the level 0 table that is corresponding to the index
            pt0_new = (struct pte *)(level_0_new);              // cast to pte


            pt0_new[VPN0(vma)] = leaf_pte(page, to_clone->flags);       // map the new page with the appropriate flags at the correct index

            
            memcpy((struct pte *)page, pagenum_to_pageptr(to_clone->ppn), PAGE_SIZE);       // copy the memory
        }
        
    }

    console_printf("reached end of loop\n");


    uintptr_t new_mtag = 
        ((uintptr_t)RISCV_SATP_MODE_Sv39 << RISCV_SATP_MODE_shift) |                    // get a new mtag for the new space by shifting 
        ((uintptr_t)asid << RISCV_SATP_ASID_shift) |
        pageptr_to_pagenum(clone);

    return new_mtag;            // return the mtag


}

/*
Inputs: vma
Outputs: struct pte
Purpose: Checks if the vma is mapped in the current memory space. If it is, then return the pte, otherwise return NULL.
*/

struct pte* get_valid_ppn (uintptr_t vma) {

    if ((main_pt2[VPN2(vma)].flags & PTE_V) == 0){                  // skip this if not mapped
        return NULL;
    }
        
    uintptr_t level_1 = main_pt2[VPN2(vma)].ppn << PAGE_ORDER;            // get access to level 1 page table
    struct pte * pt1 = (struct pte *)(level_1);                              // cast to pte


    if ((pt1[VPN1(vma)].flags & PTE_V) == 0){                       // skip this if not mapped
        return NULL;
    }

    uintptr_t level_0 = pt1[VPN1(vma)].ppn << PAGE_ORDER;                 // get access to level 0 table
    struct pte * pt0 = (struct pte *)(level_0);


    if ((pt0[VPN0(vma)].flags & PTE_V) == 0){                   // skip this if not mapped
        return NULL;
    }
    to_clone = pt0[VPN0(vma)];
    return &to_clone;
}

// INTERNAL FUNCTION DEFINITIONS
//

static inline int wellformed_vma(uintptr_t vma) {
    // Address bits 63:38 must be all 0 or all 1
    uintptr_t const bits = (intptr_t)vma >> 38;
    return (!bits || !(bits+1));
}

static inline int wellformed_vptr(const void * vp) {
    return wellformed_vma((uintptr_t)vp);
}

static inline int aligned_addr(uintptr_t vma, size_t blksz) {
    return ((vma % blksz) == 0);
}

static inline int aligned_ptr(const void * p, size_t blksz) {
    return (aligned_addr((uintptr_t)p, blksz));
}

static inline int aligned_size(size_t size, size_t blksz) {
    return ((size % blksz) == 0);
}

static inline uintptr_t active_space_mtag(void) {
    return csrr_satp();
}

static inline struct pte * mtag_to_root(uintptr_t mtag) {
    return (struct pte *)((mtag << 20) >> 8);
}


static inline struct pte * active_space_root(void) {
    return mtag_to_root(active_space_mtag());
}

static inline void * pagenum_to_pageptr(uintptr_t n) {
    return (void*)(n << PAGE_ORDER);
}

static inline uintptr_t pageptr_to_pagenum(const void * p) {
    return (uintptr_t)p >> PAGE_ORDER;
}

static inline void * round_up_ptr(void * p, size_t blksz) {
    return (void*)((uintptr_t)(p + blksz-1) / blksz * blksz);
}

static inline uintptr_t round_up_addr(uintptr_t addr, size_t blksz) {
    return ((addr + blksz-1) / blksz * blksz);
}

static inline size_t round_up_size(size_t n, size_t blksz) {
    return (n + blksz-1) / blksz * blksz;
}

static inline void * round_down_ptr(void * p, size_t blksz) {
    return (void*)((uintptr_t)p / blksz * blksz);
}

static inline size_t round_down_size(size_t n, size_t blksz) {
    return n / blksz * blksz;
}

static inline uintptr_t round_down_addr(uintptr_t addr, size_t blksz) {
    return (addr / blksz * blksz);
}

static inline struct pte leaf_pte (
    const void * pptr, uint_fast8_t rwxug_flags)
{
    return (struct pte) {
        .flags = rwxug_flags | PTE_A | PTE_D | PTE_V,
        .ppn = pageptr_to_pagenum(pptr)
    };
}

static inline struct pte ptab_pte (
    const struct pte * ptab, uint_fast8_t g_flag)
{
    return (struct pte) {
        .flags = g_flag | PTE_V,
        .ppn = pageptr_to_pagenum(ptab)
    };
}

static inline struct pte null_pte(void) {
    return (struct pte) { };
}

static inline void sfence_vma(void) {
    asm inline ("sfence.vma" ::: "memory");
}
