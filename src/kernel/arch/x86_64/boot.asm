;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;                                                             ;;
;; Copyright (C) Sidney 2023. All rights reserved.             ;;
;; Written by Sidney.                                          ;;
;; Distributed under terms of the GNU General Public License.  ;;
;;                                                             ;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

section .bss
align 16
stack_bottom:
    resb 65536 ; 64KB stack
stack_top:

section .text
bits 64
global _start
extern _start_c
extern install_gdt

_start:
    ; Set up the stack
    mov rsp, stack_top
    
    ; Ensure 16-byte alignment for ABI compliance
    and rsp, -16
    
    ; Initialize GDT first to ensure we are in a known state
    call install_gdt

    ; Enable SSE so the compiler may use it from the first C instruction
    mov rax, cr4
    or rax, (3 << 9)    ; Set OSFXSR (bit 9) and OSXMMEXCPT (bit 10)
    mov cr4, rax

    ; Call the C entry point
    call _start_c
    
    ; Hang if returned
.hang:
    cli
    hlt
    jmp .hang
section .note.GNU-stack noalloc noexec nowrite progbits
