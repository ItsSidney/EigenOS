;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; EigenOS — Atomic & Spinlock ASM Routines (x86_64 NASM)       ;;
;; Copyright (C) Sidney 2024-2026. All rights reserved.        ;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

[bits 64]
section .text

global atomic_spin_lock
global atomic_spin_unlock
global atomic_spin_trylock
global atomic_add32
global atomic_cas64

; void atomic_spin_lock(volatile uint32_t* lock)
; rdi = pointer to lock
atomic_spin_lock:
.spin:
    lock bts dword [rdi], 0
    jnc .acquired
.wait:
    pause
    test dword [rdi], 1
    jnz .wait
    jmp .spin
.acquired:
    ret

; void atomic_spin_unlock(volatile uint32_t* lock)
; rdi = pointer to lock
atomic_spin_unlock:
    mov dword [rdi], 0
    ret

; int atomic_spin_trylock(volatile uint32_t* lock)
; rdi = pointer to lock, returns 1 on success, 0 on busy
atomic_spin_trylock:
    xor eax, eax
    lock bts dword [rdi], 0
    setnc al
    ret

; int32_t atomic_add32(volatile int32_t* ptr, int32_t val)
; rdi = ptr, esi = val, returns previous value
atomic_add32:
    mov eax, esi
    lock xadd [rdi], eax
    ret

; uint64_t atomic_cas64(volatile uint64_t* ptr, uint64_t old_val, uint64_t new_val)
; rdi = ptr, rsi = old_val, rdx = new_val, returns previous value
atomic_cas64:
    mov rax, rsi
    lock cmpxchg [rdi], rdx
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
