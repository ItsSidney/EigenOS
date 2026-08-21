;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;                                                             ;;
;; Copyright (C) Sidney 2024-2026. All rights reserved.        ;;
;; Written by Sidney.                                          ;;
;; Distributed under terms of the GNU General Public License.  ;;
;;                                                             ;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

[bits 64]
section .text
global init_fpu

init_fpu:
    ; Enable FPU
    mov rax, cr0
    and ax, 0xFFFB      ; Clear TS (bit 3)
    or ax, 0x0002       ; Set MP (bit 1)
    mov cr0, rax
    finit               ; Initialize FPU

    ; Enable SSE
    mov rax, cr4
    or ax, (3 << 9)     ; Set OSFXSR (bit 9) and OSXMMEXCPT (bit 10)
    mov cr4, rax
    ret
section .note.GNU-stack noalloc noexec nowrite progbits
