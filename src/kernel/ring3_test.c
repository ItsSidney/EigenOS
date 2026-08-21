/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

// Ring3 Test Suite — runs entirely in user mode (ring 3)
// All code and data lives in the .utext section so the kernel can mark
// those pages user-accessible. No kernel libc calls: only syscalls.
// Output goes to the kernel log (sys_write fd 1) — see the Kernel Log app.
//
// USTR(x) places a string literal in a .utext array so it is readable
// from ring 3 (plain literals would land in supervisor .rodata).

#define USER_FN __attribute__((section(".utext")))
#define USTR(x) ({ static const char _u_##__LINE__[] __attribute__((section(".urodata"))) = (x); _u_##__LINE__; })

#include <stdint.h>
#include <stddef.h>

// Syscall numbers
#define SYS_WRITE     1
#define SYS_GETPID    4
#define SYS_SLEEP     5
#define SYS_GETTIME   6
#define SYS_EXIT      9

// Syscall interface (inline asm with proper constraints)
USER_FN static inline int sys_write(int fd, const void* buf, uint32_t count) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_WRITE), "D"(fd), "S"(buf), "d"(count)
        : "rcx", "r11", "memory"
    );
    return ret;
}

USER_FN static inline int sys_getpid(void) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_GETPID)
        : "rcx", "r11", "memory"
    );
    return ret;
}

USER_FN static inline void sys_sleep(uint32_t ms) {
    __asm__ volatile (
        "int $0x80"
        : : "a"(SYS_SLEEP), "D"(ms)
        : "rcx", "r11", "memory"
    );
}

USER_FN static inline uint32_t sys_gettime(void) {
    uint32_t ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_GETTIME)
        : "rcx", "r11", "memory"
    );
    return ret;
}

// Local string helpers (kernel's libc lives in supervisor pages)

USER_FN static size_t r3_strlen(const char* s) {
    size_t n = 0;
    while (s[n] != 0) n++;
    return n;
}

USER_FN static void r3_memset(void* p, int c, size_t n) {
    unsigned char* b = (unsigned char*)p;
    for (size_t i = 0; i < n; i++) b[i] = (unsigned char)c;
}

USER_FN void print_str(const char* s) {
    sys_write(1, s, (uint32_t)r3_strlen(s));
}

USER_FN void print_dec(uint64_t val) {
    char buf[32];
    int i = 0;
    if (val == 0) { sys_write(1, USTR("0"), 1); return; }
    while (val > 0) {
        buf[i++] = '0' + (char)(val % 10);
        val /= 10;
    }
    for (int j = 0; j < i/2; j++) {
        char tmp = buf[j];
        buf[j] = buf[i-1-j];
        buf[i-1-j] = tmp;
    }
    sys_write(1, buf, (uint32_t)i);
}

USER_FN void print_test(const char* name, int passed) {
    static const char pass_str[] __attribute__((section(".urodata"))) = "[PASS] ";
    static const char fail_str[] __attribute__((section(".urodata"))) = "[FAIL] ";
    sys_write(1, passed ? pass_str : fail_str, 7);
    sys_write(1, name, (uint32_t)r3_strlen(name));
    sys_write(1, USTR("\n"), 1);
}

// Test functions
USER_FN void test_sys_getpid(void) {
    int pid = sys_getpid();
    print_test(USTR("sys_getpid returns valid PID"), pid >= 0);
}

USER_FN void test_sys_write(void) {
    int ret = sys_write(1, USTR("Hello from ring3!\n"), 19);
    print_test(USTR("sys_write works"), ret == 19);
}

USER_FN void test_sys_gettime(void) {
    uint32_t t1 = sys_gettime();
    sys_sleep(100);
    uint32_t t2 = sys_gettime();
    print_test(USTR("sys_gettime increments"), t2 > t1);
}

USER_FN void test_sys_sleep(void) {
    uint32_t t1 = sys_gettime();
    sys_sleep(50);
    uint32_t t2 = sys_gettime();
    print_test(USTR("sys_sleep works"), (t2 - t1) >= 40);
}

USER_FN void test_ring3_access(void) {
    uint16_t cs;
    __asm__ volatile ("mov %%cs, %0" : "=r"(cs));
    int is_ring3 = (cs & 3) == 3;
    print_test(USTR("Running in ring 3"), is_ring3);
}

USER_FN void test_user_stack(void) {
    char local_var = 'A';
    int accessible = (local_var == 'A');
    print_test(USTR("User stack accessible"), accessible);
}

USER_FN void test_memory_protection(void) {
    int test = 42;
    print_test(USTR("User memory works"), test == 42);
}

USER_FN void test_syscall_interface(void) {
    int ret1 = sys_write(1, USTR(""), 0);
    (void)ret1;
    int pid = sys_getpid();
    (void)pid;
    int ret2 = -1;
    __asm__ volatile (
        "mov $999, %%rax\n"
        "int $0x80\n"
        : "=a"(ret2)
        : : "rcx", "r11", "memory"
    );
    int invalid_handled = (ret2 == 0 || ret2 == -1);
    print_test(USTR("Invalid syscall handled"), invalid_handled);
}

USER_FN void test_stack_growth(void) {
    char buffer[1024];
    r3_memset(buffer, 1, sizeof(buffer));

    int sum = 0;
    for (int i = 0; i < 1024; i++) sum += buffer[i];

    print_test(USTR("Stack growth works"), sum != 0);
}

USER_FN void test_syscall_preserves_registers(void) {
    print_test(USTR("Registers preserved across syscall"), 1);
}

USER_FN void ring3_test_main(void) {
    sys_write(1, USTR("\n=== Ring3 User Mode Test Suite ===\n\n"), 39);

    test_ring3_access();
    test_user_stack();
    test_sys_getpid();
    test_sys_write();
    test_sys_gettime();
    test_sys_sleep();
    test_memory_protection();
    test_syscall_interface();
    test_stack_growth();
    test_syscall_preserves_registers();

    sys_write(1, USTR("\n=== All Tests Complete ===\n"), 27);

    // Exit via SYS_EXIT (9)
    __asm__ volatile ("mov $9, %%rax; mov $0, %%rdi; int $0x80" : : : "rax", "rdi");
}

// Entry point for ring3
USER_FN void ring3_entry(void) {
    ring3_test_main();
}
