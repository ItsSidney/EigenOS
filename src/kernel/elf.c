/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#include "kernel/elf.h"
#include "kernel/mem/vmm.h"
#include "kernel/log.h"

static void zero_pages(uint64_t virt, uint64_t n) {
    for (uint64_t i = 0; i < n; i++) {
        uint64_t* p = (uint64_t*)(virt + i * 4096);
        for (int j = 0; j < 512; j++) p[j] = 0;
    }
}

int elf_load(const void* data, uint64_t size, uint64_t* entry_out) {
    if (!data || size < sizeof(elf64_header_t)) return -1;

    const elf64_header_t* eh = (const elf64_header_t*)data;
    uint32_t magic;
    __builtin_memcpy(&magic, eh->e_ident, 4);
    if (magic != ELF_MAGIC || eh->e_ident[4] != ELF_CLASS64) return -1;
    if (eh->e_type != ELF_ET_EXEC) return -1;
    /* x86_64-only, sane phdr table, no interp (static binaries only). */
    if (eh->e_machine != 62 /* EM_X86_64 */) return -1;
    if (eh->e_phentsize != sizeof(elf64_phdr_t)) return -1;
    if (eh->e_phnum == 0 || eh->e_phnum > 1024) return -1;

    uint64_t total_memsz = 0;

    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        const elf64_phdr_t* phchk =
            (const elf64_phdr_t*)((const uint8_t*)data + eh->e_phoff
                                  + (uint64_t)i * sizeof(elf64_phdr_t));
        if (phchk->p_type == 3 /* PT_INTERP */) return -1;   /* no dynamic loader */

        if (phchk->p_type == PT_LOAD) {
            if (phchk->p_memsz > (256ULL << 20)) return -1;      /* 256 MB/seg */
            total_memsz += phchk->p_memsz;
            if (total_memsz > (512ULL << 20)) return -1;         /* 512 MB/img */
        }
    }

    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        uint64_t phoff = eh->e_phoff + (uint64_t)i * eh->e_phentsize;
        if (phoff + sizeof(elf64_phdr_t) > size) return -1;
        const elf64_phdr_t* ph = (const elf64_phdr_t*)((const uint8_t*)data + phoff);
        if (ph->p_type != PT_LOAD) continue;

        uint64_t vaddr = ph->p_vaddr & ~(0xFFFULL);
        uint64_t vend  = (ph->p_vaddr + ph->p_memsz + 0xFFF) & ~(0xFFFULL);
        if (vend <= vaddr) continue;

        /* User range only: keep the null page unmapped and stay out of
           both the kernel region and any wrap-around fantasy lands. */
        if (vaddr < 0x400000ULL || vend > 0x800000000000ULL) return -1;

        for (uint64_t cur = vaddr; cur < vend; cur += 4096) {
            uint64_t phys = pmm_alloc();
            if (!phys) return -1;
            vmm_map_range(cur, phys, 4096, VMM_PRESENT | VMM_WRITE | VMM_USER);
        }
        zero_pages(vaddr, (vend - vaddr) / 4096);

        /* Copy file-backed bytes. */
        uint64_t copy = ph->p_filesz;
        if (copy > ph->p_memsz) copy = ph->p_memsz;
        if (ph->p_offset + copy > size) copy = size > ph->p_offset ? size - ph->p_offset : 0;
        if (copy > 0) {
            __builtin_memcpy((void*)(uintptr_t)ph->p_vaddr,
                             (const uint8_t*)data + ph->p_offset, copy);
        }
    }

    if (entry_out) *entry_out = eh->e_entry;
    klog("[ELF] image loaded\n");
    return 0;
}