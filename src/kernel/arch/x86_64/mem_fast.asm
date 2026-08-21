;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; EigenOS — Fast Memory ASM Routines (x86_64 NASM)           ;;
;; Copyright (C) Sidney 2024-2026. All rights reserved.        ;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

[bits 64]
section .text

global fast_memcpy
global fast_memzero
global fast_fill32

; void fast_memcpy(void* dst, const void* src, size_t n)
; rdi = dst, rsi = src, rdx = n
fast_memcpy:
    test rdx, rdx
    jz .ret
    mov rcx, rdx
    shr rcx, 3          ; count of 8-byte qwords
    rep movsq           ; fast copy 64-bit blocks
    mov rcx, rdx
    and rcx, 7          ; remaining 0-7 bytes
    rep movsb
.ret:
    ret

; void fast_memzero(void* dst, size_t n)
; rdi = dst, rsi = n
fast_memzero:
    test rsi, rsi
    jz .ret
    xor eax, eax
    mov rcx, rsi
    shr rcx, 3          ; count of 8-byte qwords
    rep stosq           ; zero 64-bit blocks
    mov rcx, rsi
    and rcx, 7
    rep stosb
.ret:
    ret

; void fast_fill32(void* dst, uint32_t val, size_t count)
; rdi = dst, esi = val, rdx = count
fast_fill32:
    test rdx, rdx
    jz .ret
    mov eax, esi
    mov rcx, rdx
    rep stosd           ; fill 32-bit pixel blocks
.ret:
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
