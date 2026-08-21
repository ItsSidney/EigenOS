;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; EigenOS — CPU State & FPU Management (x86_64 NASM)          ;;
;; Copyright (C) Sidney 2024-2026. All rights reserved.        ;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

[bits 64]
section .text

global cpu_save_fpu
global cpu_restore_fpu
global cpu_invlpg
global cpu_flush_tlb
global cpu_read_msr
global cpu_write_msr
global cpu_wbinvd
global cpu_pause
global cpu_barrier

; void cpu_save_fpu(void* buf_512_aligned)
cpu_save_fpu:
    test rdi, rdi
    jz .done
    fxsave64 [rdi]
.done:
    ret

; void cpu_restore_fpu(const void* buf_512_aligned)
cpu_restore_fpu:
    test rdi, rdi
    jz .done
    fxrstor64 [rdi]
.done:
    ret

; void cpu_invlpg(uint64_t vaddr)
cpu_invlpg:
    invlpg [rdi]
    ret

; void cpu_flush_tlb(void)
cpu_flush_tlb:
    mov rax, cr3
    mov cr3, rax
    ret

; uint64_t cpu_read_msr(uint32_t msr)
cpu_read_msr:
    mov ecx, edi
    rdmsr
    shl rdx, 32
    or rax, rdx
    ret

; void cpu_write_msr(uint32_t msr, uint64_t val)
cpu_write_msr:
    mov ecx, edi
    mov eax, esi
    mov rdx, rsi
    shr rdx, 32
    wrmsr
    ret

; void cpu_wbinvd(void)
cpu_wbinvd:
    wbinvd
    ret

; void cpu_pause(void)
cpu_pause:
    pause
    ret

; void cpu_barrier(void)
cpu_barrier:
    mfence
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
