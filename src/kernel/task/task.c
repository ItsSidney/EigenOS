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

/* POSIX-style signals (tier 1) */
extern void serial_puts(const char* s);
extern void serial_u64(uint64_t v);
int send_signal(int pid, int sig) {
    if (sig <= 0 || sig >= NSIG) return -1;
    for (int i = 0; i < MAX_TASKS; i++) {
        task_t* t = &tasks[i];
        if (t->state == TASK_FREE || t->id != pid) continue;
        if (t->state == TASK_DEAD) return -1;
        __atomic_fetch_or (&t->sig_pending, 1u << sig, __ATOMIC_RELAXED);
        return 0;
    }
    return -1;
}

/* Called by the scheduler with CR3 already switched to `t`'s address
 * space, before its saved frame is resumed. Injects a handler trampoline
 * onto the user stack or applies default termination. */
void task_deliver_signals(task_t* t) {
    if (!t->sig_pending || t->ring != TASK_RING3) return;
    if (t->sig_in_handler) return;          /* queue until handler returns */

    registers_t* r = (registers_t*)t->rsp;
    if ((uint64_t)r < 0xffffffff80000000ULL) return;

    while (t->sig_pending) {
        int sig = __builtin_ctz(t->sig_pending);
        void* h = t->sig_handlers[sig];

        /* SIGKILL ignores handlers; unhandled -> default terminate */
        if (sig == SIGKILL || !h) {
            __atomic_fetch_and(&t->sig_pending, ~(1u << sig), __ATOMIC_RELAXED);
            serial_puts("[SIG] kill pid=");
            serial_u64((uint64_t)t->id);
            serial_puts(" sig=");
            serial_u64((uint64_t)sig);
            serial_puts("\n");
            t->exit_code = 128 + sig;
            t->state = TASK_DEAD;
            if (t->kernel_stack_base) {
                kfree((void*)t->kernel_stack_base);
                t->kernel_stack_base = 0; t->kernel_stack_top = 0;
            }
            if (t->user_stack_base) {
                kfree((void*)t->user_stack_base);
                t->user_stack_base = 0; t->user_stack_top = 0;
            }
            t->rsp = 0;
            return;
        }

        __atomic_fetch_and(&t->sig_pending, ~(1u << sig), __ATOMIC_RELAXED);

        /* Build a tiny trampoline on the user stack:
             b8 22 00 00 00   mov eax, SYS_SIGRETURN
             cd 80            int  0x80
             90               nop                                   */
        uint64_t usp = r->rsp & ~0xFULL;
        usp -= 16;
        uint64_t tc = usp - 64;
        uint8_t code[8] = { 0xB8, 0, 0, 0, 0, 0xCD, 0x80, 0x90 };
        code[1] = 34; code[2] = 0; code[3] = 0; code[4] = 0;
        for (int i = 0; i < 8; i++)
            ((volatile uint8_t*)tc)[i] = code[i];
        *(volatile uint64_t*)usp = tc;      /* handler's RET address */

        t->sig_save = *r;
        r->rip    = (uint64_t)h;
        r->rdi    = (uint64_t)sig;
        r->rsp    = usp;
        r->rflags |= 0x200;                  /* IF */
        r->rflags &= ~0x400ULL;              /* clear DF */
        t->sig_in_handler = 1;
        return;
    }
}

int g_fpu_disabled = 1;   /* set to 1 to debug FPU-related crashes */
#define KSTACK_CANARY 0xC0DEC0DEFEEDFACEULL

/* FPU/SSE context (asm helpers in cpu_state.asm) */
extern void cpu_save_fpu(void* buf);
extern void cpu_restore_fpu(const void* buf);
static __attribute__((aligned(64))) uint64_t g_master_fpu[64];
static int g_master_ready = 0;

static void kstack_canary_set(uint64_t base) {
    if (base) *(volatile uint64_t*)base = KSTACK_CANARY;
}
static void kstack_canary_check(const task_t* t) {
    if (!t->kernel_stack_base) return;
    if (*(volatile uint64_t*)t->kernel_stack_base != KSTACK_CANARY) {
        extern void serial_puts(const char* s);
        extern void serial_u64(uint64_t v);
        serial_puts("[KSTACK] CANARY SMASHED task=");
        serial_u64((uint64_t)t->id);
        serial_puts(" name="); serial_puts(t->name);
        serial_puts(" -- halting to contain corruption\n");
        __asm__ volatile("cli; hlt");
        for(;;){}
    }
}

task_t* get_current_task(void) {
    if (current_task_idx < 0) return 0;
    return &tasks[current_task_idx];
}

void init_tasking(void) {
    /* Capture a pristine FPU/SSE image as the template every new task
     * starts from (init_fpu has already run by the time we're called). */
    cpu_save_fpu(g_master_fpu);
    g_master_ready = 1;
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
    kstack_canary_set(tasks[0].kernel_stack_base);
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
    kstack_canary_set(tasks[slot].kernel_stack_base);
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
    kstack_canary_set(tasks[slot].kernel_stack_base);
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
int create_user_process_elf_redir(const char* name, int argc, char* const argv[],
                                   const int parent_redir[3]) {
    const void* data = 0;
    uint64_t size = 0;
    uint64_t entry = 0;
    /* Accept "/path/name.elf", "name.elf" or bare "name": modules are
       registered under their basename without extension. */
    char modname[32];
    {
        const char* slash = 0;
        for (const char* q = name; q && *q; q++) if (*q == '/') slash = q;
        const char* base = slash ? slash + 1 : name;
        size_t bi = 0;
        while (base[bi] && base[bi] != '.' && bi < sizeof(modname)-1) {
            modname[bi] = base[bi]; bi++;
        }
        modname[bi] = 0;
    }
    if (user_module_find(modname, &data, &size) != 0) {
        klog("[SPAWN] module not found");
        return -2;
    }

    /* Phase 0: snapshot the parent's fd metadata for slots the caller wants
       the child to inherit (stdin/stdout/stderr). Pipe fds are refcounted
       kernel objects (pipes[]), so the child gets its own fd table entries
       pointing at the same pipe; file fds share the fs index. */
    struct redir_snap { int type; int pno; int end; int fflags; int foff; } rs[3] = {
        { -1, 0, 0, 0, 0 }, { -1, 0, 0, 0, 0 }, { -1, 0, 0, 0, 0 } };
    if (parent_redir) {
        task_t* parent = get_current_task();
        for (int i = 0; i < 3; i++) {
            int pfd = parent_redir[i];
            if (pfd < 0 || pfd >= MAX_FDS) continue;
            if (parent->fd_types[pfd] == FD_PIPE) {
                rs[i].type = FD_PIPE;
                rs[i].pno  = parent->fd_pipe[pfd];
                rs[i].end  = parent->fd_flags_extra[pfd] & 1;
            } else if (parent->fd_types[pfd] == FD_PTY) {
                rs[i].type = FD_PTY;
                rs[i].pno  = parent->fd_pty[pfd];
                rs[i].end  = parent->fd_flags_extra[pfd] & 1;
            } else if (parent->fd_flags[pfd] > 0) {
                rs[i].type  = 100; /* file fd marker */
                rs[i].fflags = parent->fd_flags[pfd];
                rs[i].foff   = parent->fd_offsets[pfd];
            }
        }
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
    while (modname[j] && j < 31) { tasks[slot].name[j] = name[j]; j++; }
    tasks[slot].name[j] = '\0';

    /* Install inherited stdin/stdout/stderr (slots 0..2). */
    for (int i = 0; i < 3; i++) {
        if (rs[i].type == FD_PIPE) {
            tasks[slot].fd_types[i]      = FD_PIPE;
            tasks[slot].fd_pipe[i]       = (uint8_t)rs[i].pno;
            tasks[slot].fd_flags_extra[i]= rs[i].end;
            if (rs[i].end & 1) pipes[rs[i].pno].writer_count++;
            else               pipes[rs[i].pno].reader_count++;
        } else if (rs[i].type == FD_PTY) {
            extern void pty_dup_ref(int idx, int is_master);
            tasks[slot].fd_types[i]       = FD_PTY;
            tasks[slot].fd_pty[i]         = (uint8_t)rs[i].pno;
            tasks[slot].fd_flags_extra[i] = rs[i].end;
            pty_dup_ref(rs[i].pno, rs[i].end);
            { extern void serial_puts(const char*); extern void serial_u64(uint64_t);
              serial_puts("[PTY] child fd"); serial_u64((uint64_t)i);
              serial_puts(" -> idx"); serial_u64((uint64_t)rs[i].pno);
              serial_puts(" end="); serial_u64((uint64_t)rs[i].end); serial_puts("\n"); }
        } else if (rs[i].type == 100) {
            tasks[slot].fd_flags[i]  = rs[i].fflags;
            tasks[slot].fd_offsets[i]= rs[i].foff;
        }
    }

    tasks[slot].kernel_stack_base = (uint64_t)kmalloc(KERNEL_STACK_SIZE + 16);
    kstack_canary_set(tasks[slot].kernel_stack_base);
    if (!tasks[slot].kernel_stack_base) {
        tasks[slot].state = TASK_FREE;
        return -1;
    }
    /* Install inherited stdin/stdout/stderr (slots 0..2). */
    for (int i = 0; i < 3; i++) {
        if (rs[i].type == FD_PIPE) {
            tasks[slot].fd_types[i]       = FD_PIPE;
            tasks[slot].fd_pipe[i]        = (uint8_t)rs[i].pno;
            tasks[slot].fd_flags_extra[i] = rs[i].end;
            if (rs[i].end & 1) pipes[rs[i].pno].writer_count++;
            else               pipes[rs[i].pno].reader_count++;
        } else if (rs[i].type == FD_PTY) {
            extern void pty_dup_ref(int idx, int is_master);
            tasks[slot].fd_types[i]       = FD_PTY;
            tasks[slot].fd_pty[i]         = (uint8_t)rs[i].pno;
            tasks[slot].fd_flags_extra[i] = rs[i].end;
            pty_dup_ref(rs[i].pno, rs[i].end);
        } else if (rs[i].type == 100) {
            tasks[slot].fd_flags[i]   = rs[i].fflags;
            tasks[slot].fd_offsets[i] = rs[i].foff;
        }
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

    /* default environment for boot/spawned processes (musl getenv reads
       envp from the stack; execve supplies its own). */
    static const char* def_env[] = {
        "PATH=/user:/bin", "HOME=/home/eigen", "USER=eigen",
        "TERM=eigen", "SHELL=/user/sh.elf", 0 };
    int nenv = 0;
    while (def_env[nenv]) nenv++;
    size_t elen = 0;
    for (int i = 0; i < nenv; i++) {
        size_t l = 0; while (def_env[i][l]) l++;
        elen += l + 1;
    }

    size_t nlen = 0;
    while (name[nlen]) nlen++;
    size_t slen = nlen + 1;
    for (int i = 0; i < nargs; i++) slen += arg_len[i] + 1;
    slen += elen;
    /* argv block below the strings, 16 bytes of slack so rounding down
       can never collide with them; RSP must start ≡ 8 (mod 16) per SysV
       (arr itself is ≡ 0 after the mask, +8 gives the ≡ 8 entry state). */
    uint64_t arr = top - slen - (uint64_t)(nargs + nenv + 6) * 8 - 16;
    arr &= ~(uint64_t)15;
    arr += 8;
    uint64_t* a = (uint64_t*)arr;
    a[0] = (uint64_t)(nargs + 1);                   /* argc includes argv[0] */
    uint64_t sp = top - slen;                       /* argv strings: [top-slen, top-elen) */
    /* POSIX exec: argv[0] is whatever the CALLER passed, not the path.
       BusyBox-style multi-call binaries dispatch on it. Fall back to the
       module name when the caller passed no argv. */
    int argv_present = (argv && nargs > 0 && argv[0] && argv[0][0]) ? 1 : 0;
    const char* argv0 = argv_present ? argv[0] : name;
    size_t a0len = 0;
    while (argv0[a0len]) a0len++;
    if (a0len > nlen) a0len = nlen;                  /* strings region sized for name */
    for (size_t j = 0; j < a0len; j++) ((char*)sp)[j] = argv0[j];
    ((char*)sp)[a0len] = 0;
    a[1] = sp;                                      /* argv[0] = caller's argv[0] */
    sp += nlen + 1;                                  /* advance by name's slot size */
    /* When the caller supplied argv (SPAWN_FDS), argv[0] is already on the
       stack as a[1] — copy only argv[1..] as a[2..]. The legacy SYS_SPAWN
       path passes args WITHOUT a program name, so its argv[0..] all land
       after a[1]... except its argv[0] slot must stay empty: it passes
       argc real args and we've already used `name` as argv[0]. */
    int arg_start = argv_present ? 1 : 0;
    for (int i = arg_start; i < nargs; i++) {
        for (size_t j = 0; j < arg_len[i]; j++) ((char*)sp)[j] = args_copy[i][j];
        ((char*)sp)[arg_len[i]] = 0;
        a[i - arg_start + 2] = sp;                  /* argv[1..n] */
        sp += arg_len[i] + 1;
    }
    /* argv NULL then envp block then auxv terminator — musl _start walks
       argv until NULL, takes &argv[argc+1] as envp, then scans auxv. */
    uint64_t wi = nargs + 2;
    a[wi++] = 0;                                    /* argv NULL */
    uint64_t eslot = wi;
    wi += nenv + 1;                                 /* env ptrs + NULL */
    a[wi++] = 0; a[wi++] = 0;                       /* auxv: AT_NULL pair */
    /* write env strings + pointers */
    uint64_t esp = top - elen;                      /* env strings: [top-elen, top) */
    for (int i = 0; i < nenv; i++) {
        const char* ev = def_env[i];
        size_t l = 0; while (ev[l]) l++;
        for (size_t j = 0; j <= l; j++) ((char*)esp)[j] = ev[j];
        a[eslot + i] = esp;
        esp += l + 1;
    }
    a[eslot + nenv] = 0;
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
    { extern void serial_puts(const char*); extern void serial_u64(uint64_t);
      task_t* ct = get_current_task();
      serial_puts("[EXIT] task="); serial_u64(ct ? (uint64_t)ct->id : 0);
      serial_puts(" name="); serial_puts(ct ? ct->name : "?");
      serial_puts(" code="); serial_u64((uint64_t)(uint32_t)code); serial_puts("\n"); }

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
        // Release the task's user-heap blocks so the arena doesn't leak
        // across process lifetimes (DOOM's 64MB WAD cache etc.).
        {
            extern void user_heap_release_owner(int owner);
            user_heap_release_owner(t->id);
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

int alloc_fd(task_t* t) {
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
    if (cur->fd_types[fd] == FD_PTY) {
        extern int pty_set_fg(int idx, uint32_t pid);
        extern int pty_set_raw(int idx, int raw);
        extern int pty_set_winsize(int idx, uint32_t rows, uint32_t cols);
        switch (cmd) {
        case 1000: return pty_set_fg(cur->fd_pty[fd], (uint32_t)arg);
        case 1001: return pty_set_raw(cur->fd_pty[fd], (int)arg);
        case 1002:
            return pty_set_winsize(cur->fd_pty[fd],
                                   (uint32_t)(arg >> 16),
                                   (uint32_t)(arg & 0xFFFF));
        default: break;
        }
    }
    switch (cmd) {
    case 3:  /* F_GETFL */
        if (cur->fd_types[fd] == FD_PIPE || cur->fd_types[fd] == FD_PTY)
            return (int)cur->fd_flags_extra[fd] & 0x801;
        if (cur->fd_flags[fd] > 0 || cur->fds[fd]) return 0;
        return -(int)EBADF;
    case 4:  /* F_SETFL: pipes AND ptys carry O_NONBLOCK in bit 0x800 */
        if (cur->fd_types[fd] == FD_PIPE || cur->fd_types[fd] == FD_PTY) {
            if (arg & 0x800) cur->fd_flags_extra[fd] |= 0x800;
            else             cur->fd_flags_extra[fd] &= ~0x800;
            return 0;
        }
        if (cur->fd_flags[fd] > 0 || cur->fds[fd]) { cur->fd_flags_extra[fd] = arg & 0x800; return 0; }
        return -1;
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
    if (cur->fd_types[fd] == FD_PTY) {
        extern int pty_read(int, int, char*, uint32_t, int);
        return pty_read(cur->fd_pty[fd], cur->fd_flags_extra[fd] & 1, buf,
                        count, cur->fd_flags_extra[fd] & 0x800);
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
    if (cur->fd_types[fd] == FD_PTY) {
        extern int pty_write(int, int, const char*, uint32_t);
        return pty_write(cur->fd_pty[fd], cur->fd_flags_extra[fd] & 1, buf, count);
    }
    if (cur->fd_flags[fd] > 0) {
        int n = fs_write(cur->fd_flags[fd] - 1, buf, (int)count);
        if (n > 0) {
            extern void persist_mark_dirty(void);
            persist_mark_dirty();
        }
        return n;
    }
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
    if (cur->fd_types[fd] == FD_PTY) {
        extern void pty_close(int, int);
        pty_close(cur->fd_pty[fd], cur->fd_flags_extra[fd] & 1);
        cur->fd_types[fd] = FD_VFS;
        cur->fd_pty[fd] = 0;
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
        kstack_canary_check(cur);
        /* Eager FPU save: without this, every preempted app leaves its
         * XMM state live for the next task — silent cross-app corruption
         * for anything float-heavy (ImGui, DOOM, TinyGL). */
        extern int g_fpu_disabled;
        if (!g_fpu_disabled) { cpu_save_fpu(cur->fpu_state); cur->fpu_valid = 1; }
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
        if (tasks[next_task].state == TASK_READY &&
            tasks[next_task].kernel_stack_base != 0 &&
            tasks[next_task].rsp != 0) {
            found_ready = 1;
            break;
        }
        if (tasks[next_task].state == TASK_READY &&
            (tasks[next_task].kernel_stack_base == 0 ||
             tasks[next_task].rsp == 0)) {
            /* corpse with a stale READY: neutralize + report once */
            tasks[next_task].state = TASK_DEAD;
            static int corpse_reported = 0;
            if (!corpse_reported) {
                corpse_reported = 1;
                extern void serial_puts(const char*);
                extern void serial_u64(uint64_t);
                serial_puts("[SCHED] neutralized corpse task ");
                serial_u64((uint64_t)tasks[next_task].id);
                serial_puts("\n");
            }
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
    { static int sh_dispatch_seen = 0;
      if (!sh_dispatch_seen && next->ring == TASK_RING3 && next->ppid > 1) {
          sh_dispatch_seen = 1;
          extern void serial_puts(const char*);
          extern void serial_u64(uint64_t);
          serial_puts("[SCHED] dispatch r3child id="); serial_u64((uint64_t)next->id);
          serial_puts(" rip="); serial_u64(*(uint64_t*)(next->rsp + 15*8));
          serial_puts("\n");
      } }
    
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

    /* Deliver pending signals now: CR3 targets `next`, and its saved
       frame is still on its kernel stack, fully writable. */
    task_deliver_signals(next);

    /* Restore incoming task's FPU/SSE (fresh tasks get the master image). */
    if (!g_fpu_disabled) {
        if (next->fpu_valid) cpu_restore_fpu(next->fpu_state);
        else if (g_master_ready) {
            memcpy(next->fpu_state, g_master_fpu, sizeof(g_master_fpu));
            next->fpu_valid = 1;
            cpu_restore_fpu(next->fpu_state);
        }
    }

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
        /* Never iretq into a frame we do not trust: drop to idle. */
        next->state = TASK_DEAD;
        current_task_idx = -1;
        tss_set_user_rsp0((uint64_t)&idle_stack[512]);
        extern uint64_t vmm_kernel_pml4_phys;
        if (vmm_kernel_pml4_phys)
            __asm__ volatile("mov %0, %%cr3" : : "r"(vmm_kernel_pml4_phys) : "memory");
        return idle_rsp;
    }

    return next->rsp;
}

int create_user_process_elf_args(const char* name, int argc, char* const argv[]) {
    static const int no_redir[3] = { -1, -1, -1 };
    return create_user_process_elf_redir(name, argc, argv, no_redir);
}

/* ================================================================== */
/* THE POSIX PROCESS LAYER — what each syscall actually does here       */
/*                                                                      */
/* fork:      walks the caller's user page tables through the hhdm and  */
/*            copies every mapped page into a fresh address space, then */
/*            clones the in-flight syscall frame so the child resumes   */
/*            at the same RIP with rax=0. No COW yet: eager copy.       */
/* execve:    reads the ELF through the fs table, builds a brand new    */
/*            address space in-place and REWRITES the saved int80 frame */
/*            so the returning iretq lands straight at the new entry.   */
/* wait4:     scans for DEAD children, reports WEXITSTATUS-style codes. */
/* getdents64: walks a directory's children in slot order into Linux    */
/*            dirent64 records; the fd offset doubles as the cursor.    */
/* arch_prctl: ARCH_SET_FS loads the task's TLS base into IA32_FS_BASE; */
/*            the scheduler re-loads it on every context switch.        */
/* ================================================================== */
/* POSIX process syscalls (fork/execve/wait4/getdents64/readv/writev/ */
/* dup2/arch_prctl/set_tid_address) — the foundation for a hosted musl.*/
/* ================================================================== */

extern uint64_t hhdm_offset;
extern int elf_load(const void* data, uint64_t size, uint64_t* entry);

/* Copy every present user page (vaddr < 0x800000000000) from `src_pml4`
   into `dst_pml4`, building intermediate tables as needed. Both PML4s are
   accessed through the hhdm so no CR3 switching is required. Returns 0 on
   success, -1 on OOM. */
static int copy_user_as(uint64_t src_pml4_phys, uint64_t dst_pml4_phys) {
    uint64_t* src = (uint64_t*)(src_pml4_phys + hhdm_offset);
    uint64_t* dst = (uint64_t*)(dst_pml4_phys + hhdm_offset);
    const uint64_t USER_HALF = 256;
    for (uint64_t p4 = 0; p4 < USER_HALF; p4++) {
        uint64_t e4 = src[p4];
        if (!(e4 & 1)) continue;
        uint64_t* l3s = (uint64_t*)((e4 & 0x000FFFFFFFFFF000ULL) + hhdm_offset);
        /* child L3 page */
        uint64_t d3p = pmm_alloc(); if (!d3p) return -1;
        uint64_t* l3d = (uint64_t*)(d3p + hhdm_offset);
        for (int i = 0; i < 512; i++) l3d[i] = 0;
        dst[p4] = d3p | (e4 & 0xFFF & ~(uint64_t)1 ? (e4 & 0xFFF) : (e4 & 0xFFF));
        for (uint64_t p3 = 0; p3 < 512; p3++) {
            uint64_t e3 = l3s[p3];
            if (!(e3 & 1)) continue;
            if (e3 & 0x80) { /* 1GB leaf — not used by user mappings today */
                continue;
            }
            uint64_t* l2s = (uint64_t*)((e3 & 0x000FFFFFFFFFF000ULL) + hhdm_offset);
            uint64_t d2p = pmm_alloc(); if (!d2p) return -1;
            uint64_t* l2d = (uint64_t*)(d2p + hhdm_offset);
            for (int i = 0; i < 512; i++) l2d[i] = 0;
            l3d[p3] = d2p | (e3 & 0xFFF);
            for (uint64_t p2 = 0; p2 < 512; p2++) {
                uint64_t e2 = l2s[p2];
                if (!(e2 & 1)) continue;
                if (e2 & 0x80) { /* 2MB leaf: split not needed by ELF loader */
                    continue;
                }
                uint64_t* l1s = (uint64_t*)((e2 & 0x000FFFFFFFFFF000ULL) + hhdm_offset);
                uint64_t d1p = pmm_alloc(); if (!d1p) return -1;
                uint64_t* l1d = (uint64_t*)(d1p + hhdm_offset);
                for (int i = 0; i < 512; i++) l1d[i] = 0;
                l2d[p2] = d1p | (e2 & 0xFFF);
                for (uint64_t p1 = 0; p1 < 512; p1++) {
                    uint64_t e1 = l1s[p1];
                    if (!(e1 & 1)) continue;
                    uint64_t sp_phys = e1 & 0x000FFFFFFFFFF000ULL;
                    uint64_t dp_phys = pmm_alloc(); if (!dp_phys) return -1;
                    __builtin_memcpy((void*)(dp_phys + hhdm_offset),
                                     (void*)(sp_phys + hhdm_offset), 4096);
                    l1d[p1] = dp_phys | (e1 & 0xFFF);
                }
            }
        }
    }
    return 0;
}

/* Duplicate fd table with pipe/pty refcount bookkeeping. */
static void dup_fd_table(task_t* from, task_t* to) {
    for (int f = 0; f < MAX_FDS; f++) {
        to->fds[f]          = from->fds[f];
        to->fd_flags[f]     = from->fd_flags[f];
        to->fd_offsets[f]   = from->fd_offsets[f];
        to->fd_types[f]     = from->fd_types[f];
        to->fd_pipe[f]      = from->fd_pipe[f];
        to->fd_pty[f]       = from->fd_pty[f];
        to->fd_flags_extra[f]= from->fd_flags_extra[f];
        if (to->fd_types[f] == FD_PIPE) {
            if (from->fd_flags_extra[f] & 1) pipes[from->fd_pipe[f]].writer_count++;
            else                             pipes[from->fd_pipe[f]].reader_count++;
        } else if (to->fd_types[f] == FD_PTY) {
            extern void pty_dup_ref(int idx, int is_master);
            pty_dup_ref(to->fd_pty[f], from->fd_flags_extra[f] & 1);
        }
    }
}

long sys_fork(void) {
    task_t* cur = get_current_task();
    if (!cur || cur->ring != TASK_RING3) return -1;

    int slot = -1;
    for (int i = 1; i < MAX_TASKS; i++)
        if (tasks[i].state == TASK_FREE) { slot = i; break; }
    if (slot < 0) return -EIGEN_ERR_NOMEM;

    task_t* ch = &tasks[slot];
    memset(ch, 0, sizeof(*ch));
    ch->id = next_task_id++;
    ch->ppid = cur->id;
    ch->group = ch->id;
    ch->ring = TASK_RING3;
    ch->state = TASK_FREE;
    { size_t ni=0; while (cur->name[ni] && ni < sizeof(ch->name)-1) { ch->name[ni]=cur->name[ni]; ni++; } ch->name[ni]=0; }

    ch->kernel_stack_base = (uint64_t)kmalloc(KERNEL_STACK_SIZE + 16);
    kstack_canary_set(ch->kernel_stack_base);
    if (!ch->kernel_stack_base) return -EIGEN_ERR_NOMEM;
    ch->kernel_stack_top = abi_stack_top(ch->kernel_stack_base, KERNEL_STACK_SIZE);

    /* Address-space copy under cli (same discipline as spawn). */
    __asm__ volatile("cli" ::: "memory");
    uint64_t new_pml4 = vmm_new_user_pml4();
    if (!new_pml4) {
        __asm__ volatile("sti" ::: "memory");
        kfree((void*)ch->kernel_stack_base);
        return -EIGEN_ERR_NOMEM;
    }
    if (copy_user_as(cur->pml4_phys, new_pml4) != 0) {
        __asm__ volatile("sti" ::: "memory");
        /* leak tables on OOM (tier-1) */
        kfree((void*)ch->kernel_stack_base);
        return -EIGEN_ERR_NOMEM;
    }
    ch->pml4_phys = new_pml4;

    dup_fd_table(cur, ch);

    /* Clone the parent's saved user frame onto the child's kernel stack.
       The int80 stub pushes registers_t at a fixed depth below rsp0, so
       copy [g_syscall_frame_rsp .. kernel_stack_top). */
    extern uint64_t g_syscall_frame_rsp;
    uint64_t parent_frame = g_syscall_frame_rsp;
    size_t frame_bytes = (size_t)(cur->kernel_stack_top - parent_frame);
    if (frame_bytes > KERNEL_STACK_SIZE) frame_bytes = sizeof(registers_t);
    uint64_t child_frame = ch->kernel_stack_top - frame_bytes;
    memcpy((void*)child_frame, (void*)parent_frame, frame_bytes);

    registers_t* fr = (registers_t*)child_frame;
    fr->rax = 0;                       /* child sees fork() == 0 */
    ch->rsp = child_frame;
    ch->fs_base = cur->fs_base;
    ch->cwd_node = cur->cwd_node;
    ch->user_stack_base = cur->user_stack_base;
    ch->user_stack_top = cur->user_stack_top;
    ch->state = TASK_READY;
    __asm__ volatile("sti" ::: "memory");
    return ch->id;
}

/* execve(path, argv, envp): replace the CALLING task's image in place. The
   int80 return path pops the mutated saved frame and iretq's straight into
   the new program at its entry point. Old address space is leaked (tier-1:
   add vmm_destroy_pml4 later). */
long sys_execve(const char* path_u, char* const argv_u[], char* const envp_u[]) {
    task_t* cur = get_current_task();
    if (!cur || cur->ring != TASK_RING3 || !path_u || !path_u[0]) return -EIGEN_ERR_INVAL;

    /* copy path from user memory (we ARE in the user's address space) */
    char path[256];
    size_t pl = 0;
    while (path_u[pl] && pl < sizeof(path)-1) { path[pl] = path_u[pl]; pl++; }
    path[pl] = 0;

    /* read the ELF through the flat fs table */
    int idx = fs_open(path, 0);
    if (idx < 0) return -EIGEN_ERR_NOENT;
    int sz = 0;
    fs_get_node(idx, 0, &sz, 0, 0, 0, 0);
    if (sz <= 0 || sz > 32*1024*1024) { fs_close(idx); return -EIGEN_ERR_NOMEM; }

    static uint8_t elfbuf[8*1024*1024]; /* shared staging buffer; cli guards it */
    int got = fs_read_at(idx, (char*)elfbuf, sz, 0);
    fs_close(idx);
    if (got != sz) return -EIGEN_ERR_NOENT;

    /* count argv/envp (user pointers valid now, before we switch CR3) */
    int nargs = 0, nenv = 0;
    size_t slen = pl + 1;
    if (argv_u) while (argv_u[nargs]) {
        const char* s = argv_u[nargs]; size_t l=0; while (s[l]) l++;
        slen += l+1; nargs++;
        if (nargs >= 63) break;
    } else slen += pl + 1;
    if (envp_u) while (envp_u[nenv]) {
        const char* s = envp_u[nenv]; size_t l=0; while (s[l]) l++;
        slen += l+1; nenv++;
        if (nenv >= 63) break;
    }

    __asm__ volatile("cli" ::: "memory");
    uint64_t new_pml4 = vmm_new_user_pml4();
    if (!new_pml4) { __asm__ volatile("sti" ::: "memory"); return -EIGEN_ERR_NOMEM; }
    uint64_t old_cr3 = vmm_get_current_pml4();
    (void)old_cr3;
    vmm_activate_pml4(new_pml4);
    cur->pml4_phys = new_pml4;

    uint64_t entry = 0;
    if (elf_load(elfbuf, sz, &entry) != 0) {
        klog("[EXECVE] elf_load failed");
        __asm__ volatile("sti" ::: "memory");
        return -EIGEN_ERR_NOENT;
    }

    /* fresh user stack */
    uint64_t ustack_pages = USER_STACK_SIZE / 4096;
    for (uint64_t p = 0; p < ustack_pages; p++) {
        uint64_t phys = pmm_alloc();
        if (!phys) break;
        vmm_map_range(USER_STACK_VADDR + p*4096, phys, 4096,
                      VMM_PRESENT | VMM_WRITE | VMM_USER);
        memset((void*)(uintptr_t)(USER_STACK_VADDR + p*4096), 0, 4096);
    }
    cur->user_stack_base = USER_STACK_VADDR;
    cur->user_stack_top  = USER_STACK_VADDR + USER_STACK_SIZE;

    /* fresh TLS page (same per-slot layout as spawn) */
    uint64_t tls_vaddr = THREAD_TLS_VADDR + (uint64_t)(cur - tasks) * 0x1000;
    uint64_t tls_phys = pmm_alloc();
    if (tls_phys) {
        vmm_map_range(tls_vaddr, tls_phys, 4096, VMM_PRESENT | VMM_WRITE | VMM_USER);
        memset((void*)(uintptr_t)tls_vaddr, 0, 4096);
        *(uint64_t*)(uintptr_t)(tls_vaddr + THREAD_TLS_SELF_OFF) = (uint64_t)cur->id;
        *(uint64_t*)(uintptr_t)tls_vaddr = tls_vaddr;
        cur->fs_base = tls_vaddr;
    }
    extern void wrmsr(uint32_t msr, uint64_t val);
    wrmsr(0xC0000100, cur->fs_base);

    /* stack layout: [argc][argv...][NULL][envp...][NULL][auxv AT_NULL pair]
       then strings above — matches musl _start expectations. */
    uint64_t top = cur->user_stack_top;
    size_t total = slen + (size_t)(nargs + nenv + 5) * 8 + 64;
    uint64_t arr = (top - total) & ~(uint64_t)15;
    arr += 8;
    uint64_t* a = (uint64_t*)arr;
    uint64_t sp = top - slen;
    /* argv[0] is the path itself when argv absent */
    a[0] = (uint64_t)(nargs + 1);
    a[1] = sp;                                   /* argv[0] = path string */
    sp += pl + 1;
    uint64_t wi = 2;
    for (int i = 0; i < nargs; i++) {
        const char* s = argv_u[i]; size_t l = 0; while (s[l]) l++;
        for (size_t j = 0; j <= l; j++) ((char*)sp)[j] = s[j];
        a[wi++] = sp; sp += l + 1;
    }
    a[wi++] = 0;                                  /* argv NULL */
    uint64_t envp_base = wi;
    wi += nenv + 1;                               /* reserve env block incl NULL */
    a[wi++] = 0; a[wi++] = 0;                     /* auxv: AT_NULL pair */
    /* fill env pointers AFTER their strings exist */
    uint64_t esp = sp;
    if (nenv) {
        /* strings for env go after argv strings */
        for (int i = 0; i < nenv; i++) {
            const char* s = envp_u[i]; size_t l = 0; while (s[l]) l++;
            for (size_t j = 0; j <= l; j++) ((char*)esp)[j] = s[j];
            a[envp_base + i] = esp; esp += l + 1;
        }
    }
    a[envp_base + nenv] = 0;

    /* Mutate the CURRENT syscall frame in place: on int80 return the CPU
       iretq's directly into the new image. */
    extern uint64_t g_syscall_frame_rsp;
    registers_t* fr = (registers_t*)g_syscall_frame_rsp;
    for (int i = 0; i < 15; i++) ((uint64_t*)fr)[i] = 0;
    fr->rip = entry;
    fr->rsp = arr;                 /* %rsp ≡ 8 (mod 16): SysV entry state */
    fr->cs = 0x1B;
    fr->ss = 0x23;
    fr->rflags = 0x202;

    cur->state = TASK_RUNNING;
    __asm__ volatile("sti" ::: "memory");     /* iretq restores IF anyway */
    return 0;                                  /* never actually returns */
}

long sys_wait4(int pid, int* status, int options) {
    (void)options;
    task_t* cur = get_current_task();
    if (!cur) return -1;
    for (;;) {
        for (int i = 0; i < MAX_TASKS; i++) {
            task_t* t = &tasks[i];
            if (t->state == TASK_DEAD) continue;
            if (t->ppid != cur->id) continue;
            if (pid > 0 && t->id != pid) continue;
            int code = t->exit_code;
            if (status)
                *status = (code << 8);         /* WEXITSTATUS layout */
            int rpid = t->id;
            t->state = TASK_FREE;      /* reap: slot reusable (stack already
                                          handled by exit_task) */
            return rpid;
        }
        sleep_task(10);
    }
}

/* linux_dirent64 layout musl getdents64 expects. */
struct eigen_dirent64 {
    uint64_t d_ino;
    int64_t  d_off;
    uint16_t d_reclen;
    uint8_t  d_type;
    char     d_name[];
};

extern file_t* fs_table_entry(int i);

long sys_getdents64(int fd, void* ubuf, unsigned int count) {
    task_t* cur = get_current_task();
    if (!cur || fd < 0 || fd >= MAX_FDS || !ubuf) return -EIGEN_ERR_INVAL;
    if (cur->fd_flags[fd] <= 0) return -EBADF;      /* need a files[] fd */
    int dir = cur->fd_flags[fd] - 1;

    extern int fs_get_dir_count(int dir_idx);
    extern int fs_find_by_index(int dir_idx, int nth, char* name,
                                int* size, int* type, uint8_t* flags,
                                uint32_t* mod_time);

    uint32_t start = cur->fd_offsets[fd];           /* entries already emitted */
    uint32_t emitted = 0;
    char name[128]; int size = 0; int type = 0;
    uint64_t nth = start;
    for (;; nth++) {
        name[0] = 0;
        if (fs_find_by_index(dir, (int)nth, name, &size, &type, 0, 0) != 0)
            break;                                   /* past last child */
        if (!name[0]) continue;
        uint16_t nl = 0; while (name[nl]) nl++;
        uint16_t reclen = (uint16_t)(19 + nl + 1);
        reclen = (uint16_t)((reclen + 7) & ~7u);
        if (emitted + reclen > count) break;
        struct eigen_dirent64* d = (void*)((char*)ubuf + emitted);
        d->d_ino = nth + 2;
        d->d_off = (int64_t)(nth + 1);
        d->d_reclen = reclen;
        d->d_type = (type == 1) ? 4 : 8;            /* DT_DIR / DT_REG */
        for (uint16_t j = 0; j <= nl; j++) d->d_name[j] = name[j];
        emitted += reclen;
    }
    cur->fd_offsets[fd] = (uint32_t)nth;
    return (long)emitted;
}

struct iovec_e { void* iov_base; uint64_t iov_len; };

long sys_readv(int fd, const struct iovec_e* iov, int iovcnt) {
    if (!iov || iovcnt <= 0) return -EIGEN_ERR_INVAL;
    long total = 0;
    for (int i = 0; i < iovcnt; i++) {
        long n = sys_read(fd, (char*)iov[i].iov_base, (uint32_t)iov[i].iov_len);
        if (n < 0) return total ? total : n;
        total += n;
        if ((uint32_t)n < iov[i].iov_len) break;
    }
    return total;
}

long sys_writev(int fd, const struct iovec_e* iov, int iovcnt) {
    if (!iov || iovcnt <= 0) return -EIGEN_ERR_INVAL;
    long total = 0;
    for (int i = 0; i < iovcnt; i++) {
        long n = sys_write(fd, (const char*)iov[i].iov_base, (uint32_t)iov[i].iov_len);
        if (n < 0) return total ? total : n;
        total += n;
        if ((uint32_t)n < iov[i].iov_len) break;
    }
    return total;
}

long sys_dup2(int oldfd, int newfd) {
    task_t* cur = get_current_task();
    if (!cur || oldfd < 0 || oldfd >= MAX_FDS) return -EBADF;
    if (newfd < 0 || newfd >= MAX_FDS) return -EBADF;
    if (oldfd == newfd) return newfd;
    if (!cur->fd_types[oldfd] && !cur->fd_flags[oldfd] &&
        !(cur->fd_types[oldfd])) {
        /* FD_VFS==0 is ambiguous; treat empty slots as closed unless fds[] set */
        if (!cur->fds[oldfd] && !cur->fd_flags[oldfd]) return -EBADF;
    }
    /* close target first (dup2 semantics) */
    if (cur->fd_types[newfd] == FD_PIPE || cur->fd_types[newfd] == FD_PTY ||
        cur->fds[newfd] || cur->fd_flags[newfd])
        sys_close(newfd);
    cur->fds[newfd]           = cur->fds[oldfd];
    cur->fd_flags[newfd]      = cur->fd_flags[oldfd];
    cur->fd_offsets[newfd]    = cur->fd_offsets[oldfd];
    cur->fd_types[newfd]      = cur->fd_types[oldfd];
    cur->fd_pipe[newfd]       = cur->fd_pipe[oldfd];
    cur->fd_pty[newfd]        = cur->fd_pty[oldfd];
    cur->fd_flags_extra[newfd]= cur->fd_flags_extra[oldfd];
    /* extra refcounts for kernel objects */
    if (cur->fd_types[newfd] == FD_PIPE) {
        if (cur->fd_flags_extra[newfd] & 1) pipes[cur->fd_pipe[newfd]].writer_count++;
        else                                pipes[cur->fd_pipe[newfd]].reader_count++;
    } else if (cur->fd_types[newfd] == FD_PTY) {
        extern void pty_dup_ref(int idx, int is_master);
        pty_dup_ref(cur->fd_pty[newfd], cur->fd_flags_extra[newfd] & 1);
    }
    return newfd;
}

long sys_arch_prctl(long code, uint64_t addr) {
    task_t* cur = get_current_task();
    if (!cur) return -1;
    switch (code) {
    case 0x1001: /* ARCH_SET_GSBASE — unsupported */
        return -EIGEN_ERR_INVAL;
    case 0x1002: /* ARCH_SET_FS */
        cur->fs_base = addr;
        extern void wrmsr(uint32_t msr, uint64_t val);
        wrmsr(0xC0000100, addr);
        return 0;
    case 0x1003: /* ARCH_GET_FS */
        if (!addr) return -EIGEN_ERR_INVAL;
        *(uint64_t*)(uintptr_t)addr = cur->fs_base;
        return 0;
    case 0x1004: /* ARCH_GET_GS */
        if (!addr) return -EIGEN_ERR_INVAL;
        *(uint64_t*)(uintptr_t)addr = 0;
        return 0;
    default:
        return -EIGEN_ERR_INVAL;
    }
}

long sys_set_tid_address(uint64_t tidptr) {
    task_t* cur = get_current_task();
    if (!cur) return -1;
    cur->clear_child_tid = tidptr;
    return cur->id;
}

/* ---- filesystem cwd bridge (used by filesystem.c) ---- */
void* ktask_current(void) { return (void*)get_current_task(); }
int   ktask_cwd_node(void* t) { return t ? ((task_t*)t)->cwd_node : 0; }
int   ktask_set_cwd(int idx) {
    task_t* cur = get_current_task();
    if (!cur) return -1;
    cur->cwd_node = idx < 0 ? 0 : idx;
    return 0;
}

/* ================================================================== */
/* BusyBox-readiness syscalls: stat family, chdir/getcwd, ioctl,       */
/* access, uname, rt_sigaction/procmask, getppid, gettimeofday,        */
/* clock_gettime.                                                      */
/* ================================================================== */

/* musl x86_64 struct stat layout (packed, verified offsets) */
struct eigen_kstat {
    uint64_t dev, ino, nlink;
    uint32_t mode, uid, gid, pad0;
    uint64_t rdev, size, blksize, blocks;
    uint64_t atim[2], mtim[2], ctim[2];
    uint64_t unused[3];
} __attribute__((packed));

static void fill_kstat(struct eigen_kstat* st, int idx) {
    file_t* f = fs_table_entry(idx);
    memset(st, 0, sizeof(*st));
    if (!f) return;
    st->dev = 1;
    st->ino = (uint64_t)idx + 2;
    st->nlink = 1;
    st->mode = (f->type == FS_DIRECTORY ? 0x4000 /*S_IFDIR*/ : 0x8000 /*S_IFREG*/)
             | 0755;
    st->uid = 0; st->gid = 0;
    st->size = (uint64_t)(f->size < 0 ? 0 : f->size);
    st->blksize = 4096;
    st->blocks = (st->size + 511) / 512;
    st->mtim[0] = f->modified_time; st->ctim[0] = f->modified_time;
}

extern int fs_resolve_path(const char* path);   /* exported resolver */

long sys_stat(const char* path, void* out) {
    if (!path || !out) return -EIGEN_ERR_INVAL;
    int e = fs_resolve_path(path);
    if (e < 0) return -EIGEN_ERR_NOENT;
    fill_kstat((struct eigen_kstat*)out, e);
    return 0;
}

long sys_lstat(const char* path, void* out) { return sys_stat(path, out); }

long sys_fstat(int fd, void* out) {
    task_t* cur = get_current_task();
    if (!cur || fd < 0 || fd >= MAX_FDS || !out) return -EIGEN_ERR_INVAL;
    if (cur->fd_flags[fd] <= 0) return -EIGEN_ERR_INVAL;
    fill_kstat((struct eigen_kstat*)out, cur->fd_flags[fd] - 1);
    return 0;
}

long sys_chdir(const char* path) {
    return fs_cd(path);          /* existing per-task cwd logic */
}

long sys_getcwd(char* buf, uint64_t size) {
    if (!buf || !size) return -EIGEN_ERR_INVAL;
    int n = fs_pwd(buf, (int)size);
    return n > 0 ? (long)(uintptr_t)buf : -EIGEN_ERR_INVAL;
}

/* ioctl: terminal winsize + pgrp over ptys */
struct winsize_k { uint16_t rows, cols, xpixel, ypixel; };

long sys_ioctl(int fd, uint64_t req, void* arg) {
    task_t* cur = get_current_task();
    if (!cur || fd < 0 || fd >= MAX_FDS) return -EIGEN_ERR_INVAL;
    if (cur->fd_types[fd] != FD_PTY) return -EIGEN_ERR_INVAL;
    int idx = cur->fd_pty[fd];
    switch (req) {
    case 0x5413: { /* TIOCGWINSZ */
        if (!arg) return -EIGEN_ERR_INVAL;
        extern int pty_get_winsize(int idx, uint32_t* rows, uint32_t* cols);
        uint32_t r = 24, c = 80;
        pty_get_winsize(idx, &r, &c);
        struct winsize_k* w = (struct winsize_k*)arg;
        w->rows = (uint16_t)r; w->cols = (uint16_t)c;
        w->xpixel = 0; w->ypixel = 0;
        return 0; }
    case 0x540F: { /* TIOCGPGRP */
        extern int pty_get_fg(int idx);
        if (!arg) return -EIGEN_ERR_INVAL;
        *(uint32_t*)arg = (uint32_t)pty_get_fg(idx);
        return 0; }
    case 0x5410: /* TIOCSPGRP */
        extern int pty_set_fg(int idx, uint32_t pid);
        return pty_set_fg(idx, arg ? *(uint32_t*)arg : 0);
    default:
        return -EIGEN_ERR_INVAL;
    }
}

long sys_access(const char* path, int mode) {
    (void)mode;   /* F_OK/R_OK/W_OK/X_OK: existence is all we model */
    if (!path) return -EIGEN_ERR_INVAL;
    return fs_resolve_path(path) >= 0 ? 0 : -EIGEN_ERR_NOENT;
}

long sys_uname(void* buf) {
    /* struct utsname: 6 x char[65] */
    if (!buf) return -EIGEN_ERR_INVAL;
    static const char* fields[6] = {
        "EigenOS", "eigenos", "1.0.0-eigen",
        "#1 EIGEN", "x86_64", ""
    };
    char* out = (char*)buf;
    for (int i = 0; i < 6; i++) {
        int j = 0;
        while (fields[i][j] && j < 64) { out[i*65 + j] = fields[i][j]; j++; }
        out[i*65 + j] = 0;
    }
    return 0;
}

/* musl struct sigaction: handler at offset 0 (union), flags at 8,
   restorer at 16, mask at 24 (128 bytes). We only need the handler. */
long sys_rt_sigaction(int sig, void* act, void* oact, uint64_t sigsetsize) {
    (void)sigsetsize;
    task_t* cur = get_current_task();
    if (!cur || sig <= 0 || sig >= NSIG) return -EIGEN_ERR_INVAL;
    if (oact) *(uint64_t*)oact = (uint64_t)(uintptr_t)cur->sig_handlers[sig];
    if (act) {
        uint64_t h = *(uint64_t*)act;
        cur->sig_handlers[sig] = (void (*)(int))(uintptr_t)h;
    }
    return 0;
}

long sys_rt_sigprocmask(int how, void* set, void* oldset, uint64_t size) {
    (void)how; (void)set;
    if (oldset && size) memset(oldset, 0, size);   /* empty mask */
    return 0;
}

long sys_getppid(void) {
    task_t* cur = get_current_task();
    return cur ? cur->ppid : 0;
}

/* days->seconds since Unix epoch for RTC wall clock (UTC-ish) */
static uint64_t wall_seconds(void) {
    typedef struct { int hour, minute, second, day, month, year; } rtc_time_t;
    extern void get_time(rtc_time_t* t);
    rtc_time_t t;
    get_time(&t);
    if (t.year < 2020) return 0;   /* RTC not ready */
    static const int mdays[13] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
    uint64_t days = 0;
    for (int y = 1970; y < t.year; y++)
        days += ((y%4==0 && y%100!=0) || y%400==0) ? 366 : 365;
    for (int m = 1; m < t.month && m <= 12; m++) days += mdays[m];
    if (t.month > 2 && ((t.year%4==0 && t.year%100!=0) || t.year%400==0))
        days += 1;
    days += t.day - 1;
    return days*86400ULL + t.hour*3600ULL + t.minute*60ULL + t.second;
}

long sys_gettimeofday(void* tv, void* tz) {
    (void)tz;
    if (!tv) return 0;
    extern uint32_t timer_get_ms(void);
    uint64_t ms = (uint64_t)timer_get_ms();
    uint64_t sec = ms / 1000;
    uint64_t usec = (ms % 1000) * 1000;
    uint64_t wsec = wall_seconds();
    if (wsec) sec = wsec;
    uint64_t* p = (uint64_t*)tv;
    p[0] = sec; p[1] = usec;
    return 0;
}

long sys_clock_gettime(int clk, void* tp) {
    if (!tp) return 0;
    extern uint32_t timer_get_ms(void);
    uint64_t ms = (uint64_t)timer_get_ms();
    uint64_t sec = ms / 1000, nsec = (ms % 1000) * 1000000;
    if (clk == 0) { /* CLOCK_REALTIME: wall clock */
        uint64_t wsec = wall_seconds();
        if (wsec) sec = wsec;
    }
    uint64_t* p = (uint64_t*)tp;
    p[0] = sec; p[1] = nsec;
    return 0;
}
