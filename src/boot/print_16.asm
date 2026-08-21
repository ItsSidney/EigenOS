;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;                                                             ;;
;; Copyright (C) Sidney 2023. All rights reserved.             ;;
;; Written by Sidney.                                          ;;
;; Distributed under terms of the GNU General Public License.  ;;
;;                                                             ;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; print_16.asm: Print string in Real Mode
[bits 16]
print_string:
    pusha
.loop:
    mov al, [bx]
    cmp al, 0
    je .done
    mov ah, 0x0e
    int 0x10
    inc bx
    jmp .loop
.done:
    popa
    ret

; disk.asm: Load sectors from disk
disk_load:
    pusha
    push dx ; Save DX for later comparison
    mov ah, 0x02 ; BIOS read sector function
    mov al, dh ; Number of sectors to read
    mov ch, 0x00 ; Cylinder 0
    mov dh, 0x00 ; Head 0
    mov cl, 0x02 ; Start reading from second sector (after boot sector)
    int 0x13 ; BIOS interrupt
    jc disk_error ; Error if carry flag set

    pop dx
    cmp al, dh ; Check if number of sectors read matches
    jne disk_error
    popa
    ret

disk_error:
    mov bx, DISK_ERROR_MSG
    call print_string
    jmp $

DISK_ERROR_MSG db "Disk read error!", 0
