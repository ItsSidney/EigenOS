/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#ifndef TASK_H
#define TASK_H

#include <stdint.h>
#include <stddef.h>

/* O_NONBLOCK flag stored in fd_flags_extra[]; mirrored from libc <fcntl.h> so
   ring-3 task code can test it. */
#define O_NONBLOCK 0x800

/* POSIX errno values for fd/syscall error returns (mirror libc errno.h). */
#define EBADF  9
#define EINVAL 22

#define MAX_TASKS 64
#define MAX_ARGS  64  // max argv entries passed to a ring-3 process
#define MAX_FDS 16
#define PIPE_BUF_SIZE 4096   /* per-pipe ring; shared by reader & writer */
/* fd_types[] values */
#define FD_VFS   0  /* regular / files[] index-backed */
#define FD_PIPE  1  /* pipe pair */
#define FD_PTY   3  /* pseudo-terminal (fd_flags_extra bit0 = master) */
#define FD_SOCKET 2 /* network socket (sockets[] index) */
#define KERNEL_STACK_SIZE 65536  // 64 KB kernel stack (TLS/BearSSL handshake needs deep stack)
#define USER_STACK_SIZE   524288  // 512 KB user stack (DOOM/ported games are deep)

/* POSIX-style signals (tier 1) */
#ifndef NSIG
#define NSIG      32
#endif
#define SIGHUP    1
#define SIGINT    2
#define SIGKILL   9
#define SIGUSR1  10
#define SIGSEGV  11
#define SIGUSR2  12
#define SIGTERM  15
#define SIGWINCH 28

typedef enum {
    TASK_FREE,
    TASK_READY,
    TASK_RUNNING,
    TASK_SLEEPING,
    TASK_DEAD
} task_state_t;

typedef enum {
    TASK_RING0 = 0,    // Kernel thread
    TASK_RING3 = 3     // User process
} task_ring_t;

typedef struct {
    uint64_t r15, r14, r13, r12, rbp, rbx, r11, r10, r9, r8, rdi, rsi, rdx, rcx, rax;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} __attribute__((packed)) registers_t;

struct vfs_node;

typedef struct {
    int id;
    int ppid;
    int group;              // Process group: the leader (main thread) task id.
                            // Threads share it; kernel tasks have 0.
    int exit_code;          // Set by exit_task (meaningful when state == TASK_DEAD)
    task_state_t state;
    task_ring_t ring;           // 0 = kernel, 3 = user
    uint64_t rsp;               // Saved RSP (points to registers_t on stack)
    uint64_t kernel_stack_base; // Kernel stack base
    uint64_t kernel_stack_top;  // Top of kernel stack (current RSP in kernel)
    uint64_t user_stack_base;   // User stack base (for ring3 tasks)
    uint64_t user_stack_top;    // User stack top (initial RSP for user mode)
    uint64_t pml4_phys;         // Per-process page tables (0 = shared kernel space)
    uint64_t fs_base;           // User TLS base (IA32_FS_BASE MSR, loaded on switch)
    uint64_t futex_addr;        // Futex wait address (0 = not waiting)
    /* FPU/SSE state (FXSAVE needs 512 B, 16-byte aligned) */
    __attribute__((aligned(64))) uint64_t fpu_state[64];
    uint8_t   fpu_valid;        /* 0 until first save */

    /* signal state (tier 1) */
    void*     sig_handlers[32];
    uint32_t  sig_pending;
    uint8_t   sig_in_handler;
    registers_t sig_save;
    uint64_t sleep_until;       // For sleep scheduling
    char name[32];
    
    // Unix-like File Descriptor Table
    struct vfs_node* fds[MAX_FDS];
    uint32_t fd_flags[MAX_FDS];   // 0 = VFS/closed, else files[] index + 1
    uint32_t fd_offsets[MAX_FDS]; // per-fd read position (lseek target)
    uint8_t  fd_types[MAX_FDS];   // FD_VFS, FD_PIPE or FD_PTY
    // Per-fd pipe handle: index into the global pipe table (valid iff fd_types==FD_PIPE)
    uint8_t  fd_pipe[MAX_FDS];
    // Per-fd pty handle: index into the global pty table (valid iff fd_types==FD_PTY)
    uint8_t  fd_pty[MAX_FDS];
    int      fd_flags_extra[MAX_FDS]; // O_NONBLOCK etc. (fcntl F_GETFL/SETFL)
    uint64_t clear_child_tid;         // set_tid_address target
    int      cwd_node;                // filesystem cwd (node index, 0=root)
} task_t;

// Pipe pair: a 1-page ring buffer with a read fd and a write fd in the same
// process group. Shared by all processes (fork copies the fds array but the
// pipe persists until both ends close).
struct pipe_buf {
    uint32_t head;            // write offset (bytes produced)
    uint32_t tail;            // read offset (bytes consumed)
    int      writer_count;    // live write ends (0 => reader sees EOF/HUP)
    int      reader_count;    // live read ends
    uint8_t  data[PIPE_BUF_SIZE];
};
#define MAX_PIPES 32
extern struct pipe_buf pipes[MAX_PIPES];

// Ring 0 (kernel) task creation
int create_task(void (*entry)(void), const char* name);

// Ring 3 (user) process creation
int create_user_process(void (*entry)(void), const char* name);

// Ring 3 process from an ELF entry point (see kernel/elf.h).
// Looks up the module by name, clones the address space, loads the ELF into
// it, maps a private user stack, and returns the new pid.
int create_user_process_elf(const char* name);
int create_user_process_elf_args(const char* name, int argc, char* const argv[]);
/* Spawn with fd inheritance: parent_redir[0..2] are PARENT fds the child
   should receive as its stdin/stdout/stderr (-1 = leave closed). */
int create_user_process_elf_redir(const char* name, int argc, char* const argv[],
                                   const int parent_redir[3]);

void init_tasking(void);
void exit_task(int code);
void yield(void);
void sleep_task(uint32_t ms);

// Threads: a ring-3 task sharing the current process's pml4 and TLS VMA
// region. The kernel builds the initial iretq frame with RIP = trampoline,
// rdi = arg1, rsi = arg2 (see create_user_thread). Returns the tid or <0.
int create_user_thread(void* trampoline, uint64_t arg1, uint64_t arg2);
// Thread-only exit (leader exit kills the whole process group).
void thread_exit_task(int code);
// Mark every task of the group dead so they are never scheduled again.
void kill_thread_group(int group);

// FD operations
int sys_open(const char* path, int flags);
int sys_read(int fd, char* buf, uint32_t count);
int sys_lseek(int fd, int offset, int whence);
int sys_write(int fd, const char* buf, uint32_t count);
void sys_close(int fd);

// ACPI syscalls (from ring3)
int sys_acpi_get_battery_info(void* info);
int sys_acpi_get_battery_status(void* status);

// CPU usage tracking (updated by scheduler on each timer tick)
extern volatile uint64_t cpu_sched_ticks;
extern volatile uint64_t cpu_busy_ticks;

// Called by interrupt handler
uint64_t schedule(uint64_t current_rsp);
task_t* get_current_task(void);
task_t* get_task_by_index(int index);
task_t* get_task_by_id(int id);
int get_current_task_id(void);
int get_task_count(void);

void init_tasking(void);

#endif
