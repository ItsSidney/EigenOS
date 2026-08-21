;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;                                                             ;;
;; Copyright (C) Sidney 2023. All rights reserved.             ;;
;; Written by Sidney.                                          ;;
;; Distributed under terms of the GNU General Public License.  ;;
;;                                                             ;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; gdt.asm: Global Descriptor Table
gdt_start:
    dq 0x0 ; Null descriptor

gdt_code:
    dw 0xffff    ; Limit (0-15)
    dw 0x0       ; Base (0-15)
    db 0x0       ; Base (16-23)
    db 10011010b ; 1st flags, type flags (Ring 0)
    db 11001111b ; 2nd flags, Limit (16-19)
    db 0x0       ; Base (24-31)

gdt_data:
    dw 0xffff    ; Limit (0-15)
    dw 0x0       ; Base (0-15)
    db 0x0       ; Base (16-23)
    db 10010010b ; 1st flags, type flags (Ring 0)
    db 11001111b ; 2nd flags, Limit (16-19)
    db 0x0       ; Base (24-31)

gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1 ; Size
    dd gdt_start               ; Address

; Constants for segment selectors
CODE_SEG equ gdt_code - gdt_start
DATA_SEG equ gdt_data - gdt_start

; switch_pm.asm: Switch to Protected Mode
[bits 16]
switch_to_pm:
    cli ; Disable interrupts
    lgdt [gdt_descriptor] ; Load GDT
    mov eax, cr0
    or eax, 0x1 ; Set PE (Protection Enable) bit
    mov cr0, eax
    jmp CODE_SEG:init_pm ; Long jump to flush CPU pipeline

[bits 32]
init_pm:
    mov ax, DATA_SEG ; Update segment registers
    mov ds, ax
    mov ss, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    mov ebp, 0x90000 ; Update stack
    mov esp, ebp

    call BEGIN_PM

; print_pm.asm: Print string in Protected Mode
VIDEO_MEMORY equ 0xb8000
WHITE_ON_BLACK equ 0x0f

print_string_pm:
    pusha
    mov edx, VIDEO_MEMORY

print_string_pm_loop:
    mov al, [ebx]
    mov ah, WHITE_ON_BLACK

    cmp al, 0
    je print_string_pm_done

    mov [edx], ax
    add ebx, 1
    add edx, 2

    jmp print_string_pm_loop

print_string_pm_done:
    popa
    ret
