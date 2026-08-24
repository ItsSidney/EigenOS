/* EigenOS syscall implementations — bridges musl's internal calls
 * to EigenOS's int 0x80 ABI.
 *
 * Musl expects Linux-style syscall numbers but EigenOS uses its own
 * table. We intercept at the __syscall level and translate.
 */
#include <stdint.h>

/* EigenOS syscall numbers (from include/user/eigen.h) */
#define E_OPEN   0
#define E_WRITE  1
#define E_READ   2
#define E_CLOSE  3
#define E_SLEEP  5
#define E_EXIT   9
#define E_LSEEK  22

/* raw int 0x80 */
static long esys(long n, long a, long b, long c) {
    long ret;
    register long rbx __asm__("rbx") = a;
    register long rcx __asm__("rcx") = b;
    register long rdx __asm__("rdx") = c;
    __asm__ volatile ("int $0x80" : "=a"(ret)
                      : "a"(n), "r"(rbx), "r"(rcx), "r"(rdx));
    return ret;
}

/* these are called by musl's internal layers */
long _eigen_write(int fd, const void* buf, unsigned len) {
    return esys(E_WRITE, fd, (long)buf, len);
}
long _eigen_read(int fd, void* buf, unsigned len) {
    return esys(E_READ, fd, (long)buf, len);
}
long _eigen_open(const char* path, int flags) {
    return esys(E_OPEN, (long)path, flags, 0);
}
long _eigen_close(int fd) {
    return esys(E_CLOSE, fd, 0, 0);
}
long _eigen_lseek(int fd, long off, int whence) {
    return esys(E_LSEEK, fd, off, whence);
}
void _eigen_exit(int code) {
    esys(E_EXIT, code, 0, 0);
    __asm__ volatile("hlt");
}
