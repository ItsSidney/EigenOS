;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;                                                             ;;
;; Copyright (C) Sidney 2024-2026. All rights reserved.        ;;
;; Written by Sidney.                                          ;;
;; Distributed under terms of the GNU General Public License.  ;;
;;                                                             ;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

[bits 64]

extern keyboard_handler
extern mouse_handler
extern timer_handler
extern syscall_handler
extern schedule

global irq0_handler
global irq1_handler
global irq12_handler
global gdt_flush
global exception_handler_stub
global syscall_handler_stub
global syscall_entry_stub

; GDT Flush routine
gdt_flush:
    lgdt [rdi]
    mov ax, 0x10      ; 0x10 is kernel data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    
    ; Far return to reload CS
    pop rax           ; Pop return address
    mov rdi, 0x08     ; 0x08 is kernel code segment
    push rdi          ; Push new CS
    push rax          ; Push return address
    retfq

; Full register save/restore macro
%macro PUSH_ALL 0
    push rax
    push rcx
    push rdx
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
%endmacro

%macro POP_ALL 0
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rax
%endmacro

extern schedule

irq0_handler:
    PUSH_ALL
    call timer_handler
    mov rdi, rsp
    call schedule
    mov rsp, rax
    mov al, 0x20
    out 0x20, al
    POP_ALL
    iretq

irq1_handler:
    PUSH_ALL
    call keyboard_handler
    mov al, 0x20
    out 0x20, al
    POP_ALL
    iretq

irq12_handler:
    PUSH_ALL
    call mouse_handler
    mov al, 0x20
    out 0xA0, al
    out 0x20, al
    POP_ALL
    iretq

; Generic Exception Handler Stub
extern core_exception_handler
exception_handler_stub:
    PUSH_ALL
    mov rdi, rsp ; Pass registers to C handler
    call core_exception_handler
    POP_ALL
    ; Error code and ISR number are pushed by ISR macros
    add rsp, 16
    iretq

; Individual exception stubs
%macro ISR_NOERR 1
global isr%1
isr%1:
    push qword 0
    push qword %1
    jmp exception_handler_stub
%endmacro

%macro ISR_ERR 1
global isr%1
isr%1:
    push qword %1
    jmp exception_handler_stub
%endmacro

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_NOERR 30
ISR_NOERR 31

; Syscall interrupt handler (int 0x80) - Ring 3 to Ring 0
; On entry from ring 3 via int 0x80:
;   - CPU auto-switches to ring 0 stack via TSS.RSP0
;   - CPU pushes SS, user RSP, RFLAGS, CS, RIP onto kernel stack
;   - rax = syscall number, rdi = arg1, rsi = arg2, rdx = arg3
global syscall_handler_stub
extern syscall_handler
extern g_syscall_frame_rsp
syscall_handler_stub:
    PUSH_ALL
    mov [rel g_syscall_frame_rsp], rsp
    ; Stack after PUSH_ALL (low to high): r15, r14, r13, r12, rbp, rbx, r11,
    ; r10, r9, r8, rdi, rsi, rdx, rcx, rax  (so rax is at [rsp + 112]).
    ; int $0x80 ABI: rax=num, rdi=arg1, rsi=arg2, rdx=arg3, r8=arg4.
    mov rdi, [rsp + 112]   ; saved rax = syscall number
    mov rsi, [rsp + 80]    ; saved rdi = arg1
    mov rdx, [rsp + 88]    ; saved rsi = arg2
    mov rcx, [rsp + 96]    ; saved rdx = arg3
    mov r8,  [rsp + 72]    ; saved r8  = arg4
    call syscall_handler
    mov [rsp + 112], rax   ; replace saved rax with return value
    POP_ALL
    iretq                 ; Return to user mode (pops RIP, CS, RFLAGS, RSP, SS)

; User mode entry point - used to start a ring3 task
; This is called from kernel mode to start a ring3 task for the first time
global syscall_entry_stub
syscall_entry_stub:
    ; Called from scheduler when starting a ring3 task
    ; RDI = user RSP, RSI = user RIP
    ; We need to set up iretq frame on kernel stack
    mov rax, rsi        ; RIP
    mov rdx, rdi        ; RSP
    
    ; Build iretq frame on current kernel stack
    push qword 0x23     ; SS = user data segment (0x20 + 3)
    push rdx            ; RSP = user stack
    push qword 0x202    ; RFLAGS (IF=1)
    push qword 0x1B     ; CS = user code segment (0x18 + 3)
    push rax            ; RIP
    
    iretq               ; Jump to user mode
section .note.GNU-stack noalloc noexec nowrite progbits
