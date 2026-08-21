;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;                                                             ;;
;; Copyright (C) Sidney 2024-2026. All rights reserved.        ;;
;; Written by Sidney.                                          ;;
;; Distributed under terms of the GNU General Public License.  ;;
;;                                                             ;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

[bits 64]
; TSS loading and setup
global load_tss
extern tss

load_tss:
    ; Load TSS selector into task register
    mov ax, 0x28  ; TSS selector (index 5, TI=0, RPL=0)
    ltr ax
    ret
section .note.GNU-stack noalloc noexec nowrite progbits

