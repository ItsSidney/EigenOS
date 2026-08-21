/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#include "kernel/task/task.h"
#include "kernel/mem/kheap.h"
#include "kernel/mem/vmm.h"
#include "kernel/time/timer.h"
#include "kernel/log.h"
#include "filesystem/vfs.h"
#include "filesystem/filesystem.h"
#include "kernel/arch/x86_64/tss.h"
#include "user/eigen.h"
#include "string.h"

extern uint64_t hhdm_offset;
extern uint64_t vmm_get_phys(uint64_t virt);

// Forward declarations
extern void syscall_entry_stub();

static uint64_t idle_rsp = 0;
static uint64_t idle_stack[512];

/* Task kernel-stack alignment.
   kstack_top is the address we use to BUILD the initial iretq + GPR frame.
   TSS.RSP0 is set to (kernel_stack_base + KERNEL_STACK_SIZE), which is
   8 bytes ABOVE kstack_top.  That deliberate gap is critical: when a timer
   or other interrupt fires just after a task is created (but before it has
   ever been scheduled), the CPU pushes the hardware iretq frame at
   [RSP0-40 … RSP0).  If RSP0 == kstack_top the new frame OVERLAPS the
   pre-built initial frame and corrupts the entry RIP / CS, producing an
   ISR 13 with CS=0x8 (kernel mode) and RBP=0x28 (=40 = iretq frame size).
   The -8 keeps RSP0 8 bytes above kstack_top so the two frames never meet.
   The resulting kstack_top ≡ 8 (mod 16): after pushing 20 qwords (160 B)
   the saved RSP also ≡ 8 (mod 16), which is correct for POP_ALL + iretq. */
static inline uint64_t abi_stack_top(uint64_t base, uint64_t size) {
    return ((base + size + 15) & ~(uint64_t)15) - 8;
}

volatile uint64_t cpu_sched_ticks = 0;
volatile uint64_t cpu_busy_ticks = 0;

static void idle_loop(void) {
    while (1) {
        __asm__ volatile("sti; hlt");
    }
}

static task_t tasks[MAX_TASKS];
static int current_task_idx = -1;
static int next_task_id = 1;
static int tasking_enabled = 0;

// Syscall numbers
#define SYS_OPEN        0
#define SYS_READ        1
#define SYS_WRITE       2
#define SYS_CLOSE       3
#define SYS_YIELD       4
#define SYS_SLEEP       5
#define SYS_EXIT        6

task_t* get_current_task(void) {
    if (current_task_idx < 0) return 0;
    return &tasks[current_task_idx];
}

void init_tasking(void) {
    for (int i = 0; i < MAX_TASKS; i++) {
        tasks[i].state = TASK_FREE;
        tasks[i].id = 0;
        for (int f = 0; f < MAX_FDS; f++) { tasks[i].fds[f] = 0; tasks[i].fd_flags[f] = 0; tasks[i].fd_offsets[f] = 0; tasks[i].fd_types[f] = 0; tasks[i].fd_pipe[f] = 0; tasks[i].fd_flags_extra[f] = 0; }
    }
    
    // Create the kernel idle/main task (ring 0)
    tasks[0].id = 0;
    tasks[0].ppid = 0;
    tasks[0].state = TASK_RUNNING;
    tasks[0].ring = TASK_RING0;
    tasks[0].kernel_stack_base = (uint64_t)kmalloc(KERNEL_STACK_SIZE);
    tasks[0].kernel_stack_top = tasks[0].kernel_stack_base + KERNEL_STACK_SIZE;
    tasks[0].user_stack_base = 0;
    tasks[0].user_stack_top = 0;
    tasks[0].name[0] = 'm'; tasks[0].name[1] = 'a'; tasks[0].name[2] = 'i'; tasks[0].name[3] = 'n'; tasks[0].name[4] = '\0';
    
    // Setup idle state
    uint64_t* stack = &idle_stack[512];
    *(--stack) = 0x10;  // SS
    *(--stack) = (uint64_t)&idle_stack[512]; // RSP
    *(--stack) = 0x202; // RFLAGS
    *(--stack) = 0x08;  // CS
    *(--stack) = (uint64_t)idle_loop; // RIP
    for (int i=0; i<15; i++) *(--stack) = 0;
    idle_rsp = (uint64_t)stack;

    current_task_idx = 0;
    tasking_enabled = 1;
}

// Kernel-task entry wrapper: create_task() starts tasks with a bare iretq
// frame and no return path, so a task whose entry RETURNS would `ret` into
// garbage on its abandoned kernel stack (#GP). Route every entry through
// this trampoline and exit cleanly once it returns.
static void task_kernel_entry_wrapper(void) {
    void (*entry)(void);
    __asm__ volatile("mov %%rdi, %0" : "=r"(entry));
    if (entry) entry();
    exit_task(0);
}

// Create a kernel thread (ring 0)
int create_task(void (*entry)(void), const char* name) {
    int slot = -1;
    for (int i = 1; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_FREE) {
            slot = i;
            break;
        }
    }
    if (slot == -1) return -1;
    
    tasks[slot].id = next_task_id++;
    tasks[slot].ppid = (current_task_idx >= 0) ? tasks[current_task_idx].id : 0;
    tasks[slot].ring = TASK_RING0;
    tasks[slot].sleep_until = 0;
    for (int f = 0; f < MAX_FDS; f++) { tasks[slot].fds[f] = 0; tasks[slot].fd_flags[f] = 0; tasks[slot].fd_offsets[f] = 0; }
    
    int j = 0;
    while (name[j] && j < 31) { tasks[slot].name[j] = name[j]; j++; }
    tasks[slot].name[j] = '\0';
    
    // Allocate kernel stack
    tasks[slot].kernel_stack_base = (uint64_t)kmalloc(KERNEL_STACK_SIZE + 16);
    if (!tasks[slot].kernel_stack_base) {
        tasks[slot].state = TASK_FREE;
        return -1;
    }
    
    tasks[slot].kernel_stack_top = abi_stack_top(tasks[slot].kernel_stack_base, KERNEL_STACK_SIZE);
    tasks[slot].user_stack_base = 0;
    tasks[slot].user_stack_top = 0;
    
    // Setup initial stack frame (kernel mode)
    uint64_t stack_top = tasks[slot].kernel_stack_top;
    uint64_t* stack = (uint64_t*)stack_top;
    
    *(--stack) = 0x10;  // SS = kernel data segment
    *(--stack) = stack_top; // RSP
    *(--stack) = 0x202; // RFLAGS (IF=1)
    *(--stack) = 0x08;  // CS = kernel code segment
    *(--stack) = (uint64_t)task_kernel_entry_wrapper; // RIP

    // 15 general purpose registers (will be popped by POP_ALL).
    // rdi (slot 4) carries the real entry to the trampoline.
    for (int i = 0; i < 15; i++) *(--stack) = (i == 4) ? (uint64_t)entry : 0;
    
    tasks[slot].rsp = (uint64_t)stack;
    tasks[slot].state = TASK_READY;
    
    return tasks[slot].id;
}

// Create a user process (ring 3)
int create_user_process(void (*entry)(void), const char* name) {
    int slot = -1;
    for (int i = 1; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_FREE) {
            slot = i;
            break;
        }
    }
    if (slot == -1) return -1;
    
    tasks[slot].id = next_task_id++;
    tasks[slot].ppid = (current_task_idx >= 0) ? tasks[current_task_idx].id : 0;
    tasks[slot].ring = TASK_RING3;
    tasks[slot].sleep_until = 0;
    for (int f = 0; f < MAX_FDS; f++) { tasks[slot].fds[f] = 0; tasks[slot].fd_flags[f] = 0; tasks[slot].fd_offsets[f] = 0; }
    
    int j = 0;
    while (name[j] && j < 31) { tasks[slot].name[j] = name[j]; j++; }
    tasks[slot].name[j] = '\0';
    
    // Allocate kernel stack
    tasks[slot].kernel_stack_base = (uint64_t)kmalloc(KERNEL_STACK_SIZE + 16);
    if (!tasks[slot].kernel_stack_base) {
        tasks[slot].state = TASK_FREE;
        return -1;
    }
    tasks[slot].kernel_stack_top = abi_stack_top(tasks[slot].kernel_stack_base, KERNEL_STACK_SIZE);
    
    // Allocate user stack
    tasks[slot].user_stack_base = (uint64_t)kmalloc(USER_STACK_SIZE + 16);
    if (!tasks[slot].user_stack_base) {
        kfree((void*)tasks[slot].kernel_stack_base);
        tasks[slot].state = TASK_FREE;
        return -1;
    }
    tasks[slot].user_stack_top = abi_stack_top(tasks[slot].user_stack_base, USER_STACK_SIZE);
    
    // Mark user stack pages as user-accessible (U/S bit) so ring 3 can access them.
    // Align to page boundaries to avoid exposing adjacent kernel-only data.
    uint64_t user_stack_page = tasks[slot].user_stack_base & ~(PAGE_SIZE - 1);
    uint64_t user_stack_end = tasks[slot].user_stack_top;
    uint64_t user_stack_size = user_stack_end - user_stack_page;
    vmm_add_flags(user_stack_page, user_stack_size, VMM_USER);

    // Mark the user code pages (.utext section) as user-accessible (U/S bit)
    // so ring 3 can fetch instructions from them.
    extern char __user_text_start[];
    extern char __user_text_end[];
    uint64_t code_start = (uint64_t)(uintptr_t)__user_text_start;
    uint64_t code_size = (uint64_t)(uintptr_t)__user_text_end - code_start;
    if (code_size > 0) {
        vmm_add_flags(code_start, code_size, VMM_USER);
    }

    // Mark the user read-only data pages (.urodata section) as user-accessible
    extern char __user_data_start[];
    extern char __user_data_end[];
    uint64_t data_start = (uint64_t)(uintptr_t)__user_data_start;
    uint64_t data_size = (uint64_t)(uintptr_t)__user_data_end - data_start;
    if (data_size > 0) {
        vmm_add_flags(data_start, data_size, VMM_USER);
    }
    
    // Build iretq frame on kernel stack for initial entry to user mode
    uint64_t kstack_top = tasks[slot].kernel_stack_top;
    uint64_t* kstack = (uint64_t*)kstack_top;
    
    // Build iretq frame on kernel stack for initial entry to user mode
    *(--kstack) = 0x23;              // SS = user data segment (selector 0x20 + RPL 3)
    *(--kstack) = tasks[slot].user_stack_top; // RSP = top of user stack
    *(--kstack) = 0x202;             // RFLAGS = IF=1
    *(--kstack) = 0x1B;              // CS = user code segment (selector 0x18 + RPL 3)
    *(--kstack) = (uint64_t)entry;   // RIP = entry point
    
    // 15 general purpose registers (initialize to 0)
    for (int i = 0; i < 15; i++) *(--kstack) = 0;
    
    tasks[slot].rsp = (uint64_t)kstack;
    tasks[slot].state = TASK_READY;
    
    return tasks[slot].id;
}

/* Ring-3 user stack placement: fixed virtual range, physical pages from the
   PMM (NOT the kernel heap, which lives in supervisor-only pages). */
#define USER_STACK_VADDR 0x70000000ULL

/* Per-task TLS and thread-stack VMA region (private to each process's pml4).
   Each spawned thread gets its own kernel stack (kmalloc), its own 512 KB
   user stack and a 4 KiB TLS page, all keyed off its task SLOT so sibling
   threads never collide. The main thread's TLS lives at the same layout. */
#define THREAD_TLS_VADDR     0x60000000ULL   /* slot * 0x1000   -> 0x6003F000 */
#define THREAD_TLS_SELF_OFF  520             /* tid stored at this TLS offset */
#define THREAD_STACK_VADDR   0x60100000ULL   /* slot * 0x80000  -> 0x62080000 */
#define THREAD_STACK_SIZE    USER_STACK_SIZE /* 512 KB, same as the main stack */

extern int user_module_find(const char* name, const void** data, uint64_t* size);
extern int elf_load(const void* data, uint64_t size, uint64_t* entry);
extern uint64_t vmm_new_user_pml4(void);

/* Create a user process from a bundled user ELF.
   The process gets its OWN copy of the page tables (private address space):
   the ELF and user stack are mapped there, so concurrent apps do not clobber
   each other's 0x400000-based images. */
int create_user_process_elf(const char* name) {
    return create_user_process_elf_args(name, 0, 0);
}

/* Create a ring-3 process from a boot module, optionally passing a
   POSIX-style argv. argv strings must live in the CALLER's address space
   (kernel memory for ring-0 callers, validated user memory for syscalls);
   they are copied into the new process's stack before it starts. */
int create_user_process_elf_args(const char* name, int argc, char* const argv[]) {
    const void* data = 0;
    uint64_t size = 0;
    uint64_t entry = 0;
    if (user_module_find(name, &data, &size) != 0) {
        klog("[SPAWN] module not found");
        return -2;
    }

    /* Phase 1: copy argv strings into kernel scratch while the caller's
       address space is still active (the pml4 switch comes later). */
    char* args_copy[MAX_ARGS] = {0};
    size_t arg_len[MAX_ARGS] = {0};
    if (argc > MAX_ARGS) argc = MAX_ARGS;
    int nargs = argc > 0 ? argc : 0;
    for (int i = 0; i < nargs; i++) {
        const char* s = argv[i];
        if (!s) { nargs = i; break; }
        size_t len = 0;
        while (s[len] && len < 1023) len++;
        char* k = (char*)kmalloc(len + 1);
        if (!k) { nargs = i; break; }
        for (size_t j = 0; j < len; j++) k[j] = s[j];
        k[len] = 0;
        args_copy[i] = k;
        arg_len[i] = len;
    }

    extern void serial_puts(const char* s);
    extern void serial_u64(uint64_t v);
    uint32_t t0 = timer_get_ms();
    serial_puts("[T] spawn start "); serial_u64((uint64_t)t0); serial_puts(" size="); serial_u64(size); serial_puts("\n");

    int slot = -1;
    for (int i = 1; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_FREE) {
            slot = i;
            break;
        }
    }
    if (slot == -1) { klog("[SPAWN] no free task slot"); return -1; }

    tasks[slot].id = next_task_id++;
    tasks[slot].ppid = (current_task_idx >= 0) ? tasks[current_task_idx].id : 0;
    tasks[slot].group = tasks[slot].id;   /* the leader of its own group */
    tasks[slot].ring = TASK_RING3;
    tasks[slot].sleep_until = 0;
    tasks[slot].exit_code = 0;
    tasks[slot].fs_base = 0;
    tasks[slot].futex_addr = 0;
    for (int f = 0; f < MAX_FDS; f++) { tasks[slot].fds[f] = 0; tasks[slot].fd_flags[f] = 0; tasks[slot].fd_offsets[f] = 0; }

    int j = 0;
    while (name[j] && j < 31) { tasks[slot].name[j] = name[j]; j++; }
    tasks[slot].name[j] = '\0';

    tasks[slot].kernel_stack_base = (uint64_t)kmalloc(KERNEL_STACK_SIZE + 16);
    if (!tasks[slot].kernel_stack_base) {
        tasks[slot].state = TASK_FREE;
        return -1;
    }
    tasks[slot].kernel_stack_top = abi_stack_top(tasks[slot].kernel_stack_base, KERNEL_STACK_SIZE);

    /* Interrupts must stay DISABLED for the entire window from vmm_activate_pml4
       (new_pml4) to vmm_activate_pml4 (old_pml4).  If a timer interrupt fires
       mid-elf_load() the scheduler saves the spawner's RSP and, on return,
       restores tasks[spawner].pml4_phys (kernel pml4 — not new_pml4).  The
       spawner then continues running elf_load() in the KERNEL address space:
       ELF segments get mapped there instead of the new process's pml4, so the
       process launches with a completely empty page table and faults on its
       very first instruction (ISR 13 or ISR 14, task STATE=READY).
       The "lag" this causes is real but unavoidable with a single-CPU pml4
       switch; the correct long-term fix is a kernel-thread spawner. */
    __asm__ volatile("cli" ::: "memory");
    uint64_t new_pml4 = vmm_new_user_pml4();
    if (!new_pml4) {
        __asm__ volatile("sti" ::: "memory");
        kfree((void*)tasks[slot].kernel_stack_base);
        tasks[slot].state = TASK_FREE;
        return -1;
    }
    uint64_t old_pml4 = vmm_get_current_pml4();
    vmm_activate_pml4(new_pml4);
    tasks[slot].pml4_phys = new_pml4;

    /* The ELF image was already loaded into the (shared) caller address
       space; copy its bytes into fresh pages in the new address space. */
    if (elf_load(data, size, &entry) != 0) {
        klog("[SPAWN] elf_load failed");
        vmm_activate_pml4(old_pml4);
        tasks[slot].pml4_phys = 0;
        kfree((void*)tasks[slot].kernel_stack_base);
        tasks[slot].state = TASK_FREE;
        for (int i = 0; i < nargs; i++) if (args_copy[i]) kfree(args_copy[i]);
        return -3;
    }

    /* Map a dedicated user stack from fresh physical pages. */
    uint64_t ustack_pages = USER_STACK_SIZE / 4096;
    for (uint64_t p = 0; p < ustack_pages; p++) {
        uint64_t phys = pmm_alloc();
        if (!phys) {
            vmm_activate_pml4(old_pml4);
            tasks[slot].pml4_phys = 0;
            kfree((void*)tasks[slot].kernel_stack_base);
            tasks[slot].state = TASK_FREE;
            for (int i = 0; i < nargs; i++) if (args_copy[i]) kfree(args_copy[i]);
            return -1;
        }
        uint64_t vaddr = USER_STACK_VADDR + p * 4096;
        vmm_map_range(vaddr, phys, 4096, VMM_PRESENT | VMM_WRITE | VMM_USER);
        __builtin_memset((void*)(uintptr_t)vaddr, 0, 4096);
    }
    tasks[slot].user_stack_base = USER_STACK_VADDR;

    /* Map the process's TLS page (thread-local storage base, loaded into
       IA32_FS_BASE when the scheduler switches to this task). Slots are
       unique per process, so the per-slot VMA never collides. The tid is
       stored at THREAD_TLS_SELF_OFF so pthread_self() works for the main
       thread without a syscall. */
    uint64_t tls_vaddr = THREAD_TLS_VADDR + (uint64_t)slot * 0x1000;
    uint64_t tls_phys = pmm_alloc();
    if (!tls_phys) {
        vmm_activate_pml4(old_pml4);
        tasks[slot].pml4_phys = 0;
        kfree((void*)tasks[slot].kernel_stack_base);
        tasks[slot].state = TASK_FREE;
        for (int i = 0; i < nargs; i++) if (args_copy[i]) kfree(args_copy[i]);
        return -1;
    }
    vmm_map_range(tls_vaddr, tls_phys, 4096, VMM_PRESENT | VMM_WRITE | VMM_USER);
    __builtin_memset((void*)(uintptr_t)tls_vaddr, 0, 4096);
    *(uint64_t*)(uintptr_t)(tls_vaddr + THREAD_TLS_SELF_OFF) = (uint64_t)tasks[slot].id;
    *(uint64_t*)(uintptr_t)tls_vaddr = tls_vaddr;
    tasks[slot].fs_base = tls_vaddr;

    /* Place [strings][argv ptrs][NULL][argc] at the top of the user stack,
       with the initial RSP pointing AT argc and everything above it.
       argv[0] is the program name, then the real args (POSIX layout), so
       ported programs (awk) see main(argc, argv) exactly as on Linux.
       Calls grow the stack DOWNWARD from RSP into free space, so the
       argument block is never clobbered by main()'s frames. RSP % 16 == 8
       at entry keeps every `call` site in main() aligned per SysV. */
    uint64_t top = USER_STACK_VADDR + USER_STACK_SIZE;
    size_t nlen = 0;
    while (name[nlen]) nlen++;
    size_t slen = nlen + 1;
    for (int i = 0; i < nargs; i++) slen += arg_len[i] + 1;
    /* argv block below the strings, 16 bytes of slack so rounding down
       can never collide with them; RSP must start ≡ 8 (mod 16) per SysV
       (arr itself is ≡ 0 after the mask, +8 gives the ≡ 8 entry state). */
    uint64_t arr = top - slen - (uint64_t)(nargs + 3) * 8 - 16;
    arr &= ~(uint64_t)15;
    arr += 8;
    uint64_t* a = (uint64_t*)arr;
    a[0] = (uint64_t)(nargs + 1);                   /* argc includes argv[0] */
    uint64_t sp = top - slen;                       /* strings: [sp, top) */
    for (size_t j = 0; j < nlen; j++) ((char*)sp)[j] = name[j];
    ((char*)sp)[nlen] = 0;
    a[1] = sp;                                      /* argv[0] = program name */
    sp += nlen + 1;
    for (int i = 0; i < nargs; i++) {
        for (size_t j = 0; j < arg_len[i]; j++) ((char*)sp)[j] = args_copy[i][j];
        ((char*)sp)[arg_len[i]] = 0;
        a[i + 2] = sp;                              /* argv[1..n] = real args */
        sp += arg_len[i] + 1;
    }
    a[nargs + 2] = 0;                               /* argv[n+1] = NULL */
    tasks[slot].user_stack_top = arr;

    /* Restore the caller's address space; the new task runs under its own
       pml4 once the scheduler switches to it. */
    vmm_activate_pml4(old_pml4);
    for (int i = 0; i < nargs; i++) if (args_copy[i]) kfree(args_copy[i]);

    /* Build iretq frame on kernel stack for initial entry to user mode. */
    uint64_t kstack_top = tasks[slot].kernel_stack_top;
    uint64_t* kstack = (uint64_t*)kstack_top;

    *(--kstack) = 0x23;              // SS = user data segment
    *(--kstack) = tasks[slot].user_stack_top; // RSP = top of user stack
    *(--kstack) = 0x202;             // RFLAGS = IF=1
    *(--kstack) = 0x1B;              // CS = user code segment
    *(--kstack) = entry;             // RIP = ELF entry point
    for (int i = 0; i < 15; i++) *(--kstack) = 0;

    tasks[slot].rsp = (uint64_t)kstack;
    tasks[slot].state = TASK_READY;

    __asm__ volatile("sti" ::: "memory");
    extern void serial_puts(const char* s);
    extern void serial_u64(uint64_t v);
    serial_puts("[T] spawn done "); serial_u64((uint64_t)(timer_get_ms() - t0)); serial_puts(" ms\n");
    return tasks[slot].id;
}

// Dedicated static stack for dying tasks. exit_task frees the task's own
// kernel stack below, so the final yield() must run on memory that will not
// be freed. Dead tasks are never scheduled again, so one shared stack is safe.
static uint8_t zombie_stack[KERNEL_STACK_SIZE] __attribute__((aligned(16)));

/* Mark every task of a process group dead so the scheduler never picks it.
   The leader itself is handled by its own exit path; this just strands the
   other threads (their stacks are intentionally leaked — they are simply
   never resumed). */
void kill_thread_group(int group) {
    for (int i = 1; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_FREE) continue;
        if (tasks[i].group == group || tasks[i].id == group) {
            tasks[i].state = TASK_DEAD;
            tasks[i].futex_addr = 0;
        }
    }
}

/* Create a ring-3 THREAD that shares the current process's address space
   (pml4) but has its own kernel stack, user stack and TLS page. Runs while
   the caller's pml4 is active (syscalls execute in the caller's context), so
   the new mappings land in the shared process page tables. The initial iretq
   frame enters user mode at RIP = trampoline with rdi = arg1, rsi = arg2
   (SysV), so the libc trampoline is `void f(void*, void*)`. Returns the tid. */
int create_user_thread(void* trampoline, uint64_t arg1, uint64_t arg2) {
    if (!tasking_enabled) return -1;
    if (!trampoline || (uint64_t)trampoline >= 0x800000000000ULL) return EIGEN_ERR_INVAL;
    task_t* leader = get_current_task();
    if (!leader || leader->ring != TASK_RING3 || leader->group <= 0) return EIGEN_ERR_INVAL;
    int group = leader->group;

    int slot = -1;
    for (int i = 1; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_FREE) { slot = i; break; }
    }
    if (slot == -1) return EIGEN_ERR_NOMEM;

    task_t* t = &tasks[slot];
    t->id = next_task_id++;
    t->ppid = leader->id;
    t->group = group;
    t->ring = TASK_RING3;
    t->pml4_phys = leader->pml4_phys;   /* threads share the process's pml4 */
    t->sleep_until = 0;
    t->exit_code = 0;
    t->futex_addr = 0;
    for (int f = 0; f < MAX_FDS; f++) { t->fds[f] = 0; t->fd_flags[f] = 0; t->fd_offsets[f] = 0; t->fd_types[f] = 0; t->fd_pipe[f] = 0; t->fd_flags_extra[f] = 0; }
    int j = 0;
    while ("thread"[j] && j < 5) { t->name[j] = "thread"[j]; j++; }
    t->name[j] = 0;

    t->kernel_stack_base = (uint64_t)kmalloc(KERNEL_STACK_SIZE + 16);
    if (!t->kernel_stack_base) { t->state = TASK_FREE; return EIGEN_ERR_NOMEM; }
    t->kernel_stack_top = abi_stack_top(t->kernel_stack_base, KERNEL_STACK_SIZE);

    /* Thread user stack: fresh PMM pages at the per-slot VMA (in the shared
       process pml4, since we are running under it right now). */
    uint64_t tstack_vaddr = THREAD_STACK_VADDR + (uint64_t)slot * THREAD_STACK_SIZE;
    uint64_t npages = THREAD_STACK_SIZE / 4096;
    for (uint64_t p = 0; p < npages; p++) {
        uint64_t phys = pmm_alloc();
        if (!phys) { t->state = TASK_FREE; kfree((void*)t->kernel_stack_base); return EIGEN_ERR_NOMEM; }
        vmm_map_range(tstack_vaddr + p * 4096, phys, 4096, VMM_PRESENT | VMM_WRITE | VMM_USER);
        __builtin_memset((void*)(uintptr_t)(tstack_vaddr + p * 4096), 0, 4096);
    }
    t->user_stack_base = tstack_vaddr;
    t->user_stack_top  = abi_stack_top(tstack_vaddr, THREAD_STACK_SIZE);

    /* Thread TLS page, zeroed, with the tid stored so pthread_self() works. */
    uint64_t tls_vaddr = THREAD_TLS_VADDR + (uint64_t)slot * 0x1000;
    uint64_t tls_phys = pmm_alloc();
    if (!tls_phys) { t->state = TASK_FREE; kfree((void*)t->kernel_stack_base); return EIGEN_ERR_NOMEM; }
    vmm_map_range(tls_vaddr, tls_phys, 4096, VMM_PRESENT | VMM_WRITE | VMM_USER);
    __builtin_memset((void*)(uintptr_t)tls_vaddr, 0, 4096);
    *(uint64_t*)(uintptr_t)(tls_vaddr + THREAD_TLS_SELF_OFF) = (uint64_t)t->id;
    *(uint64_t*)(uintptr_t)tls_vaddr = tls_vaddr;
    t->fs_base = tls_vaddr;

    /* Initial iretq frame: RIP = trampoline, rdi = arg1, rsi = arg2.
       Slots: first push (i=0) is the LAST pop (rax); i=3 -> rsi, i=4 -> rdi. */
    uint64_t* kstack = (uint64_t*)t->kernel_stack_top;
    *(--kstack) = 0x23;                       /* SS = user data segment */
    *(--kstack) = t->user_stack_top;          /* RSP = top of thread stack */
    *(--kstack) = 0x202;                      /* RFLAGS (IF=1) */
    *(--kstack) = 0x1B;                       /* CS = user code segment */
    *(--kstack) = (uint64_t)trampoline;       /* RIP */
    for (int i = 0; i < 15; i++) {
        uint64_t v = 0;
        if (i == 3) v = arg2;                 /* rsi */
        else if (i == 4) v = arg1;            /* rdi */
        *(--kstack) = v;
    }
    t->rsp = (uint64_t)kstack;
    t->state = TASK_READY;
    return t->id;
}

/* Thread exit: frees this thread's own kernel/user stacks and TLS page.
   For the leader (or any thread whose exit equals a process exit) the whole
   group is stranded and the normal process-exit path runs. */
void thread_exit_task(int code) {
    __asm__ volatile("cli");
    if (current_task_idx <= 0) { while (1) { __asm__ volatile("cli; hlt"); } }
    task_t* t = &tasks[current_task_idx];

    if (t->id == t->group || t->group <= 0) {
        /* Leader: stranding the group turns this into a process exit. */
        kill_thread_group(t->group);
        exit_task(code);
        for (;;) { __asm__ volatile("cli; hlt"); }
    }

    int slot = current_task_idx;
    uint64_t old_kernel_stack = t->kernel_stack_base;
    uint64_t old_thread_stack  = t->user_stack_base;
    uint64_t old_tls_vaddr     = t->fs_base;
    t->state = TASK_DEAD;
    t->exit_code = code;
    t->kernel_stack_base = 0;
    t->kernel_stack_top = 0;
    t->rsp = 0;
    t->user_stack_base = 0;
    t->user_stack_top = 0;
    t->futex_addr = 0;
    t->fs_base = 0;

    /* Jump off the dying thread's kernel stack before freeing it. */
    __asm__ volatile("mov %0, %%rsp" : : "r"((uint64_t)(uintptr_t)&zombie_stack[KERNEL_STACK_SIZE]) : "memory");
    kfree((void*)old_kernel_stack);

    /* Free the thread's PMM-backed user stack and TLS page (we still run
       under the shared process pml4, so unmap + free directly). */
    uint64_t tstack_vaddr = THREAD_STACK_VADDR + (uint64_t)slot * THREAD_STACK_SIZE;
    for (uint64_t p = 0; p < THREAD_STACK_SIZE / 4096; p++) {
        uint64_t v = tstack_vaddr + p * 4096;
        uint64_t phys = vmm_get_phys(v);
        vmm_unmap(v);
        if (phys) pmm_free(phys);
    }
    if (old_tls_vaddr) {
        uint64_t phys = vmm_get_phys(old_tls_vaddr);
        vmm_unmap(old_tls_vaddr);
        if (phys) pmm_free(phys);
    }
    yield();
    for (;;) { __asm__ volatile("cli; hlt"); }
}

void exit_task(int code) {
    __asm__ volatile("cli");
    if (current_task_idx > 0) {
        task_t* t = &tasks[current_task_idx];
        // Close all FDs
        for (int i = 0; i < MAX_FDS; i++) {
            if (t->fds[i]) {
                vfs_close(t->fds[i]);
                t->fds[i] = 0;
            }
            if (t->fd_flags[i] > 0) {
                fs_close(t->fd_flags[i] - 1);
                t->fd_flags[i] = 0;
            }
            if (t->fd_types[i] == FD_PIPE) {
                int pno = t->fd_pipe[i];
                if (pno > 0 && pno < MAX_PIPES) {
                    if (t->fd_flags_extra[i] & 1) pipes[pno].writer_count--;
                    else                          pipes[pno].reader_count--;
                    if (pipes[pno].writer_count < 0) pipes[pno].writer_count = 0;
                    if (pipes[pno].reader_count  < 0) pipes[pno].reader_count = 0;
                }
                t->fd_types[i] = FD_VFS;
                t->fd_pipe[i] = 0;
                t->fd_flags_extra[i] = 0;
            }
        }
        // Remember the stacks so we can free them after switching away.
        uint64_t old_kernel_stack = t->kernel_stack_base;
        uint64_t old_user_stack = t->user_stack_base;
        // Mark the task fully non-runnable BEFORE freeing its stacks: the
        // round-robin selector only picks TASK_READY, so the slot will never be
        // reselected and schedule() won't point TSS.rsp0 at the freed block.
        t->state = TASK_DEAD;
        t->exit_code = code;
        t->kernel_stack_base = 0;
        t->kernel_stack_top = 0;
        t->rsp = 0;
        t->user_stack_base = 0;
        t->user_stack_top = 0;
        // Jump off the task's kernel stack before freeing it: we are still
        // executing on it, and yield()'s int $0x20 pushes a frame on the
        // current stack. The abandoned iretq frame at the top of this stack
        // must never be iretq'd again.
        __asm__ volatile("mov %0, %%rsp" : : "r"((uint64_t)(uintptr_t)&zombie_stack[KERNEL_STACK_SIZE]) : "memory");
        kfree((void*)old_kernel_stack);
        if (old_user_stack) {
            kfree((void*)old_user_stack);
        }
        // Hand the CPU back to the scheduler. With state == TASK_DEAD and
        // kernel_stack_base == 0, the task is never reselected (round-robin
        // only picks TASK_READY) and schedule() will not point TSS.rsp0 at the
        // freed block, so the abandoned iretq frame can never be resumed.
        yield();
        // Safety net: a DEAD task is never rescheduled, so this is unreachable.
        for (;;) { __asm__ volatile("cli; hlt"); }
    }
    // Non-user task: just yield the CPU.
    while (1) { __asm__ volatile("sti"); yield(); }
}

// Syscall implementations
/* File descriptors resolve against the in-memory filesystem (files[] table)
 * first; only /mnt/* paths fall back to the VFS mount layer. fd_flags[i]
 * holds fs index + 1 for fs-backed fds, 0 for VFS-backed/closed fds. */
int sys_open(const char* path, int flags) {
    int idx = fs_open(path, flags);
    /* O_CREAT: create the file (parent directory must already exist) when it
       is not present. Without this, fopen("w")/open(...,O_CREAT) silently
       fail and the file is never created. Value matches libc's fcntl.h ABI. */
#ifndef O_CREAT
#define O_CREAT 0x40
#endif
    if (idx < 0 && (flags & O_CREAT) && path) {
        int created = fs_create(path);
        if (created >= 0) idx = created;
    }
    task_t* cur = get_current_task();
    for (int i = 3; i < MAX_FDS; i++) {     /* skip reserved stdin/stdout/stderr */
        if (cur->fds[i]) continue;
        if (idx >= 0) {
            cur->fd_flags[i] = idx + 1;
            cur->fd_offsets[i] = 0;
            return i;
        }
        if (path && strncmp(path, "/mnt/", 5) == 0) {
            struct vfs_node* node = vfs_open_path(path);
            if (node) {
                cur->fds[i] = node;
                cur->fd_offsets[i] = 0;
                return i;
            }
        }
        return -1;
    }
    return -1;
}

/* Pipe ring buffers live globally for the whole kernel session. */
struct pipe_buf pipes[MAX_PIPES];

static int alloc_fd(task_t* t) {
    int s = 3;   /* 0/1/2 are reserved for stdin/stdout/stderr */
    for (; s < MAX_FDS; s++) {
        if (!t->fds[s] && !t->fd_flags[s] && t->fd_types[s] == FD_VFS) return s;
    }
    return -1;
}

static int pipe_read(int pno, char* buf, uint32_t count) {
    struct pipe_buf* p = &pipes[pno];
    if (p->head == p->tail) {                 /* no data available */
        if (p->writer_count == 0) return 0;   /* EOF */
        return -1;                            /* would block: caller handles sleep */
    }
    uint32_t avail = (p->head - p->tail + PIPE_BUF_SIZE) % PIPE_BUF_SIZE;
    uint32_t n = avail < count ? avail : count;
    for (uint32_t i = 0; i < n; i++)
        buf[i] = p->data[(p->tail + i) % PIPE_BUF_SIZE];
    p->tail = (p->tail + n) % PIPE_BUF_SIZE;
    return (int)n;
}

static int pipe_write(int pno, const char* buf, uint32_t count) {
    struct pipe_buf* p = &pipes[pno];
    if (p->reader_count == 0) return -1;      /* writers get EPIPE/SIGPIPE */
    uint32_t free = (p->tail - p->head + PIPE_BUF_SIZE - 1) % PIPE_BUF_SIZE;
    uint32_t n = free < count ? free : count;
    for (uint32_t i = 0; i < n; i++)
        p->data[(p->head + i) % PIPE_BUF_SIZE] = buf[i];
    p->head = (p->head + n) % PIPE_BUF_SIZE;
    return (int)n;
}

int sys_pipe(int pipefd[2]) {
    if (!pipefd) return -1;
    int pno = 0;
    while (pno < MAX_PIPES && (pipes[pno].writer_count || pipes[pno].reader_count)) pno++;
    if (pno >= MAX_PIPES) return -1;
    struct pipe_buf* p = &pipes[pno];
    p->head = 0; p->tail = 0; p->writer_count = 0; p->reader_count = 0;
    task_t* cur = get_current_task();
    int rd = alloc_fd(cur);
    if (rd < 0) return -1;
    cur->fd_types[rd] = FD_PIPE;
    cur->fd_flags_extra[rd] = 0;
    int wr = alloc_fd(cur);
    if (wr < 0) { cur->fd_types[rd] = FD_VFS; return -1; }
    cur->fd_types[rd] = FD_PIPE; cur->fd_pipe[rd] = (uint8_t)pno; cur->fd_flags_extra[rd] = 0;
    cur->fd_types[wr] = FD_PIPE; cur->fd_pipe[wr] = (uint8_t)pno; cur->fd_flags_extra[wr] = 1; /* 1 = write end */
    p->reader_count++; p->writer_count++;
    pipefd[0] = rd; pipefd[1] = wr;
    p->reader_count++; p->writer_count++;
    pipefd[0] = rd; pipefd[1] = wr;
    return 0;
}

/* fcntl(2): limited to F_GETFL / F_SETFL (O_NONBLOCK). Returns -1/errno via
   return value. */
int sys_fcntl(int fd, int cmd, int arg) {
    if (fd < 0 || fd >= MAX_FDS) return -1;
    task_t* cur = get_current_task();
    switch (cmd) {
    case 3:  /* F_GETFL */
        if (cur->fd_types[fd] == FD_PIPE) return (int)cur->fd_flags_extra[fd]; /* O_RDONLY/O_WRONLY | O_NONBLOCK */
        if (cur->fd_flags[fd] > 0 || cur->fds[fd]) return 0;
        return -(int)EBADF;
    case 4:  /* F_SETFL */
        if (cur->fd_types[fd] != FD_PIPE) {
            if (cur->fd_flags[fd] > 0 || cur->fds[fd]) { cur->fd_flags_extra[fd] = arg & 0x800; return 0; }
            return -1;
        }
        if (arg & 0x800) cur->fd_flags_extra[fd] |= 0x800;   /* set O_NONBLOCK */
        else            cur->fd_flags_extra[fd] &= ~0x800;    /* clear          */
        return 0;
    default:
        return -1;
    }
}

int sys_read(int fd, char* buf, uint32_t count) {
    if (fd < 0 || fd >= MAX_FDS) return -1;
    task_t* cur = get_current_task();
    if (cur->fd_types[fd] == FD_PIPE) {
        int nonblock = cur->fd_flags_extra[fd] & O_NONBLOCK;
        for (;;) {
            int n = pipe_read(cur->fd_pipe[fd], buf, count);
            if (n >= 0) return n;                       /* data or EOF */
            if (n == -1 && !nonblock) sleep_task(5);    /* wait for producer */
            else return -1;                             /* nonblock empty => EAGAIN */
        }
    }
    if (cur->fd_flags[fd] > 0) {
        int idx = cur->fd_flags[fd] - 1;
        int n = fs_read_at(idx, buf, (int)count, (int)cur->fd_offsets[fd]);
        if (n > 0) cur->fd_offsets[fd] += (uint32_t)n;
        return n;
    }
    if (!cur->fds[fd]) return -1;
    return vfs_read(cur->fds[fd], buf, count, 0);
}

/* lseek: adjust the per-fd read position. whence: 0=SET, 1=CUR, 2=END.
 * The flat filesystem cannot write at arbitrary offsets (writes always
 * append), so seeking affects reads only. Returns the new offset, or -1. */
int sys_lseek(int fd, int offset, int whence) {
    if (fd < 0 || fd >= MAX_FDS) return -1;
    task_t* cur = get_current_task();
    if (cur->fd_flags[fd] <= 0) return -1;
    int idx = cur->fd_flags[fd] - 1;
    int base = 0;
    if (whence == 1) base = (int)cur->fd_offsets[fd];
    else if (whence == 2) {
        int sz = 0;
        fs_get_node(idx, 0, &sz, 0, 0, 0, 0);
        base = sz;
    }
    int off = base + offset;
    if (off < 0) return -1;
    cur->fd_offsets[fd] = (uint32_t)off;
    return off;
}

int sys_write(int fd, const char* buf, uint32_t count) {
    if (fd < 0 || fd >= MAX_FDS) return -1;
    task_t* cur = get_current_task();
    if (cur->fd_types[fd] == FD_PIPE) {
        if (cur->fd_flags_extra[fd] == 0) return -1;   /* read end is not writable */
        int n = pipe_write(cur->fd_pipe[fd], buf, count);
        if (n < 0) return -1;                            /* no readers */
        return n;
    }
    if (cur->fd_flags[fd] > 0)
        return fs_write(cur->fd_flags[fd] - 1, buf, (int)count);
    if (!cur->fds[fd]) return -1;
    return vfs_write(cur->fds[fd], buf, count, 0);
}

void sys_close(int fd) {
    if (fd < 0 || fd >= MAX_FDS) return;
    task_t* cur = get_current_task();
    cur->fd_offsets[fd] = 0;
    if (cur->fd_flags[fd] > 0) {
        fs_close(cur->fd_flags[fd] - 1);
        cur->fd_flags[fd] = 0;
        return;
    }
    if (cur->fds[fd]) {
        vfs_close(cur->fds[fd]);
        cur->fds[fd] = 0;
    }
    if (cur->fd_types[fd] == FD_PIPE) {
        int pno = cur->fd_pipe[fd];
        if (pno > 0 && pno < MAX_PIPES) {
            if (cur->fd_flags_extra[fd] & 1) pipes[pno].writer_count--;
            else                             pipes[pno].reader_count--;
            if (pipes[pno].writer_count < 0) pipes[pno].writer_count = 0;
            if (pipes[pno].reader_count  < 0) pipes[pno].reader_count = 0;
        }
        cur->fd_types[fd] = FD_VFS;
        cur->fd_pipe[fd] = 0;
        cur->fd_flags_extra[fd] = 0;
    }
}

void yield(void) {
    __asm__ volatile("int $0x20");
}

void sleep_task(uint32_t ms) {
    if (tasking_enabled) {
        tasks[current_task_idx].sleep_until = timer_get_ms() + ms;
        tasks[current_task_idx].state = TASK_SLEEPING;
        yield();
    } else {
        sleep_ms(ms);
    }
}

int get_current_task_id(void) {
    task_t* cur = get_current_task();
    return cur ? cur->id : -1;
}

int get_task_count(void) {
    int count = 0;
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state != TASK_FREE) count++;
    }
    return count;
}

task_t* get_task_by_index(int index) {
    if (index < 0 || index >= MAX_TASKS) return 0;
    if (tasks[index].state == TASK_FREE) return 0;
    return &tasks[index];
}

task_t* get_task_by_id(int id) {
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].id == id && tasks[i].state != TASK_FREE) return &tasks[i];
    }
    return 0;
}

uint64_t schedule(uint64_t current_rsp) {
    if (!tasking_enabled) return current_rsp;
    
    if (current_task_idx >= 0) {
        task_t* cur = &tasks[current_task_idx];
        cur->rsp = current_rsp;
        if (cur->state == TASK_RUNNING) {
            cur->state = TASK_READY;
        }
    } else {
        idle_rsp = current_rsp;
    }
    
    // Wake up sleeping tasks
    uint64_t current_time = timer_get_ms();
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_SLEEPING && current_time >= tasks[i].sleep_until) {
            tasks[i].state = TASK_READY;
        }
    }
    
    // Find next ready task (Round Robin)
    int start_idx = (current_task_idx >= 0) ? current_task_idx : 0;
    int next_task = start_idx;
    int found_ready = 0;
    do {
        next_task = (next_task + 1) % MAX_TASKS;
        if (tasks[next_task].state == TASK_READY) {
            found_ready = 1;
            break;
        }
    } while (next_task != start_idx);
    
    if (!found_ready && current_task_idx >= 0 && tasks[current_task_idx].state == TASK_READY) {
        next_task = current_task_idx;
        found_ready = 1;
    }

    cpu_sched_ticks++;

    if (!found_ready) {
        current_task_idx = -1; // Switch to idle
        tss_set_user_rsp0((uint64_t)&idle_stack[512]);
        extern uint64_t vmm_kernel_pml4_phys;
        if (vmm_kernel_pml4_phys) {
            __asm__ volatile("mov %0, %%cr3" : : "r"(vmm_kernel_pml4_phys) : "memory");
        }
        return idle_rsp;
    }

    if (next_task != current_task_idx) {
        cpu_busy_ticks++;
    }
    
    current_task_idx = next_task;
    task_t* next = &tasks[current_task_idx];
    next->state = TASK_RUNNING;
    
    // Update TSS with the TOP of next task's kernel stack buffer.
    // Use (base + KERNEL_STACK_SIZE), NOT kernel_stack_top: this sits 8 bytes
    // ABOVE kstack_top (see abi_stack_top comment) — the gap prevents the
    // CPU's hardware interrupt-frame push from clobbering the initial task frame.
    if (next->kernel_stack_base) {
        tss_set_user_rsp0(next->kernel_stack_base + KERNEL_STACK_SIZE);
    }

    // Switch address spaces: per-process CR3 for ring3 apps, shared kernel
    // page tables for everything else. The CR3 write also flushes the TLB.
    extern uint64_t vmm_kernel_pml4_phys;
    uint64_t target_pml4 = next->pml4_phys ? next->pml4_phys : vmm_kernel_pml4_phys;
    if (target_pml4) {
        __asm__ volatile("mov %0, %%cr3" : : "r"(target_pml4) : "memory");
    }

    // Load the task's user TLS base into IA32_FS_BASE (0xC0000100). Kernel
    // code never touches %fs, so a single write on every switch is enough;
    // the value is always the CURRENT task's fs_base after this point.
    extern void wrmsr(uint32_t msr, uint64_t val);
    wrmsr(0xC0000100, next->fs_base);

    /* DIAGNOSTIC: a task's saved rsp must point into the kernel image/heap
       (kernel stacks are kmalloc'd there). A hhdm/user/zero rsp means the
       popped iretq frame is garbage -> ISR 13 at irq0_handler's iretq. */
    if (next->rsp < 0xffffffff80000000ULL || next->rsp > 0xffffffffc0000000ULL) {
        extern void serial_puts(const char* s);
        extern void serial_u64(uint64_t v);
        serial_puts("[SCHED] BAD RSP next task="); serial_u64(next->id);
        serial_puts(" name="); serial_puts(next->name);
        serial_puts(" state="); serial_u64(next->state);
        serial_puts(" rsp="); serial_u64(next->rsp);
        serial_puts(" kbase="); serial_u64(next->kernel_stack_base);
        serial_puts(" ktop="); serial_u64(next->kernel_stack_top);
        serial_puts(" pml4="); serial_u64(next->pml4_phys);
        serial_puts(" cur_task_idx="); serial_u64(current_task_idx);
        serial_puts(" cur_rsp="); serial_u64(current_rsp);
        serial_puts("\n");
    }

    return next->rsp;
}
