;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;                                                             ;;
;; Copyright (C) Sidney 2023. All rights reserved.             ;;
;; Written by Sidney.                                          ;;
;; Distributed under terms of the GNU General Public License.  ;;
;;                                                             ;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; 16-bit Bootloader for Eigen OS
[org 0x7c00]
KERNEL_OFFSET equ 0x1000 ; Memory offset to load kernel to

mov [BOOT_DRIVE], dl ; BIOS stores boot drive in DL

mov bp, 0x9000
mov sp, bp

mov bx, MSG_REAL_MODE
call print_string

call load_kernel ; Load kernel from disk
call switch_to_pm ; Switch to 32-bit Protected Mode
jmp $ ; Infinite loop (should never reach here)

%include "src/boot/print_16.asm"
%include "src/boot/gdt.asm"

[bits 16]
load_kernel:
    mov bx, MSG_LOAD_KERNEL
    call print_string

    mov bx, KERNEL_OFFSET ; Load to 0x1000
    mov dh, 15            ; Load 15 sectors (increase if kernel grows)
    mov dl, [BOOT_DRIVE]
    call disk_load
    ret

[bits 32]
BEGIN_PM:
    mov ebx, MSG_PROT_MODE
    call print_string_pm
    call KERNEL_OFFSET ; Jump to kernel entry
    jmp $

; Global variables
BOOT_DRIVE db 0
MSG_REAL_MODE db "Started in 16-bit Real Mode", 0
MSG_PROT_MODE db "Successfully landed in 32-bit Protected Mode", 0
MSG_LOAD_KERNEL db "Loading kernel into memory...", 0

; Bootsector padding
times 510-($-$$) db 0
dw 0xaa55