//           elf.c 
//   

#include "elf.h"
#include "error.h"
#include "string.h"

#define EI_MAG0 0x7f
#define EI_MAG1 'E'
#define EI_MAG2 'L'
#define EI_MAG3 'F'
#define ELFCLASS64 2
#define ELFDATA2LSB 1
#define EV_CURRENT 1
#define ET_EXEC 2
#define PT_LOAD 1
#define EI_NIDENT 16

typedef struct {
  unsigned char	e_ident[EI_NIDENT];	
  uint16_t e_type;
  uint16_t e_machine;
  uint32_t e_version;
  uint64_t e_entry;		
  uint64_t e_phoff;		
  uint64_t e_shoff;		
  uint32_t e_flags;
  uint16_t e_ehsize;
  uint16_t e_phentsize;
  uint16_t e_phnum;
  uint16_t e_shentsize;
  uint16_t e_shnum;
  uint16_t e_shstrndx;
} Elf64_Ehdr;

typedef struct {
  uint32_t p_type;
  uint32_t p_flags;
  uint64_t p_offset;		
  uint64_t p_vaddr;		
  uint64_t p_paddr;		
  uint64_t p_filesz;		
  uint64_t p_memsz;		
  uint64_t p_align;		
} Elf64_Phdr;


// int elf_load(struct io_intf *io, void (**entryptr)(struct io_intf *io))
// Inputs: pointer to struct io and double pointer to void io intf
// Outputs: int, 0 on success or negative on error
// Side Effects: Alters the entry pointer paramter and loads elf program header data into memory
// Description: Reads the elf header into a struct and checks if it is a valid elf file with processing
// the other data. If it is valid, then it iterates through the program headers, and loads the corresponding
// program into memeory if it is loadable.

int elf_load(struct io_intf *io, void (**entryptr)(void)){
    if (io == NULL){
        return -EINVAL; // return invalid input
    }

    Elf64_Ehdr elfhead;

    

    

    ioseek(io, 0); // set pos to 0 and read into elfhead
    if (ioread(io, &elfhead, sizeof(Elf64_Ehdr)) < 0){ // check if read
        return -EIO; // return io error
    }

    if (elfhead.e_ident[0] != EI_MAG0 || elfhead.e_ident[1] != EI_MAG1 || elfhead.e_ident[2] != EI_MAG2 || elfhead.e_ident[3] != EI_MAG3){ // check magic nums
        return -EBADFMT; // return bad fmt
    }
   
    if (elfhead.e_ident[4] != ELFCLASS64){ // check fifth byte(binary)
        return -EBADFMT; // return bad fmt
    }
    
    if (elfhead.e_ident[5] != ELFDATA2LSB){ // check sixth byte(data encoding)
        return -EBADFMT; // return bad fmt
    }

    if (elfhead.e_ident[6] != EV_CURRENT){ // check seventh byte(version)
        return -EBADFMT; // return bad fmt
    }

    if (elfhead.e_type != ET_EXEC){ // check if executable
        return -EBADFMT; // return bad fmt
    }

    
    uint16_t count; // variable for the for loop
    for (count = 0; count < elfhead.e_phnum; count++){ // loop through all program headers
        Elf64_Phdr elfp; // make elf program header struct
        uint64_t phoffset = elfhead.e_phoff + ((uint64_t)count * (uint64_t)elfhead.e_phentsize); // get offset
        ioseek(io, phoffset); // seek and read to elfp
        if (ioread(io, &elfp, sizeof(Elf64_Phdr)) < 0){ // check if read
            return -EIO; // return io error
        }

        if (elfp.p_type != PT_LOAD){ // check if can load
            continue; // next iteration
        }
      
        if (elfp.p_vaddr < USER_START_VMA || elfp.p_vaddr + elfp.p_memsz > USER_END_VMA){ // check if valid address
            return -EBADFMT;
        }

        memory_alloc_and_map_range(elfp.p_vaddr, elfp.p_filesz, PTE_W | PTE_R | PTE_U);     // allocate pages for this section
        
        ioseek(io, elfp.p_offset); // seek and read into memory

        uint8_t flags = 0;
        flags |= PTE_U;
        if (elfp.p_flags & 0b1) {           // use bit mask 0b1 which checks for X flag in ELF flags
            flags |= PTE_X;  // Set execute flag
        }
        if (elfp.p_flags & 0b10) {          // use bit mask 0b10 which checks for W flag in ELF flags
            flags |= PTE_W;  // Set write flag
        }
        if (elfp.p_flags & 0b100) {         // use bit mask 0b100 which checks for R flag in ELF flags
            flags |= PTE_R;  // Set read flag
        }

        

        if (ioread(io, (void*)elfp.p_vaddr, elfp.p_filesz) < 0){ // check if read
            return -EIO; // return io error
        }

        



        
        if (elfp.p_filesz < elfp.p_memsz){ // need to make rest 0
            memset((void *)(elfp.p_vaddr + elfp.p_filesz), 0, elfp.p_memsz - elfp.p_filesz);
        }

        memory_set_range_flags((void*)elfp.p_vaddr, elfp.p_filesz, flags);  // change flags based on the elf header flags
    }

    
    *entryptr = (void (*)(void))elfhead.e_entry; // set entry point

    
    
    return 0; // return
}


