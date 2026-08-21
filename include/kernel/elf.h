/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#ifndef ELF_H
#define ELF_H

#include <stdint.h>

#define ELF_MAGIC    0x464C457FUL
#define ELF_CLASS64  2
#define ELF_ET_EXEC  2
#define PT_LOAD      1

typedef struct {
    uint8_t  e_ident[16];
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
} __attribute__((packed)) elf64_header_t;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} __attribute__((packed)) elf64_phdr_t;

/* Loads an ELF64 executable into the (shared) user address space.
 * Maps every PT_LOAD segment at its virtual address with USER|WRITE flags
 * and returns the entry point in *entry_out. Returns 0 on success. */
int elf_load(const void* data, uint64_t size, uint64_t* entry_out);

#endif /* ELF_H */