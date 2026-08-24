/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

/* userlib.c — libeigen: C-facing userland API (API81 wrappers) + console.
 * Freestanding, links against no libc. <stdarg.h> is compiler-provided.
 * Entry `_start` calls main(argc, argv) then eigen_exit().
 */
#include "userlib.h"
#include <stdint.h>
#include <stdarg.h>

/* ------------------------------------------------------------------ */
/* Process bootstrap                                                   */
/* ------------------------------------------------------------------ */
int main(int argc, char** argv);

/* Entry trampoline: the kernel places [argc][argv[0..n-1]][NULL] at the
   top of the user stack and jumps here with RSP pointing at argc (SysV:
   RSP % 16 == 8 at entry). The iretq entry leaves RSP ≡ 8 as if just
   returned from a call, so a bare `call main` would drop main's entry
   RSP to ≡ 0 and misalign every frame — the first `movaps` (any printf
   with a float argument) #GPs. Align first; argc/argv are already in
   registers so the arg block below the aligned RSP is untouched. */
__asm__(
    ".global _start\n"
    "_start:\n"
    "  mov (%rsp), %edi\n"      /* argc */
    "  lea 8(%rsp), %rsi\n"     /* argv */
    "  and $-16, %rsp\n"        /* -> ≡ 0 so `call main` enters at ≡ 8 */
    "  call main\n"
    "  mov %eax, %edi\n"
    "  call eigen_exit\n"
    "_start_hang:\n"
    "  jmp _start_hang\n");

/* ------------------------------------------------------------------ */
/* Syscall wrappers (int $0x80 ABI)                                    */
/* ------------------------------------------------------------------ */
int eigen_getpid(void)    { return (int)eigen_syscall(EIGEN_SYS_GETPID, 0, 0, 0, 0); }
int eigen_open(const char* path, int flags) { return (int)eigen_syscall(EIGEN_SYS_OPEN, (uint64_t)path, (uint64_t)flags, 0, 0); }
int eigen_write(int fd, const void* buf, uint32_t count) { return (int)eigen_syscall(EIGEN_SYS_WRITE, (uint64_t)fd, (uint64_t)buf, count, 0); }
int eigen_read(int fd, void* buf, uint32_t count)  { return (int)eigen_syscall(EIGEN_SYS_READ, (uint64_t)fd, (uint64_t)buf, count, 0); }
void eigen_close(int fd)  { eigen_syscall(EIGEN_SYS_CLOSE, (uint64_t)fd, 0, 0, 0); }
int eigen_lseek(int fd, int offset, int whence) {
    return (int)eigen_syscall(EIGEN_SYS_LSEEK, (uint64_t)fd, (uint64_t)offset, (uint64_t)whence, 0);
}
int eigen_pipe(int pipefd[2]) {
    return (int)eigen_syscall(EIGEN_SYS_PIPE, (uint64_t)(uintptr_t)pipefd, 0, 0, 0);
}
int eigen_fcntl(int fd, int cmd, int arg) {
    return (int)eigen_syscall(EIGEN_SYS_FCNTL, (uint64_t)fd, (uint64_t)cmd, (uint64_t)arg, 0);
}

int eigen_settings(uint64_t op, uint64_t a, uint64_t b, uint64_t c) {
    return (int)eigen_syscall(EIGEN_SYS_SETTINGS, op, a, b, c);
}
int eigen_theme_get(int id, eigen_theme_info_t* out) {
    return (int)eigen_syscall(EIGEN_SYS_SETTINGS, EIGEN_SETTINGS_THEME_GET,
                              (uint64_t)id, (uint64_t)out, 0);
}
int eigen_theme_add(const char* name, const uint32_t* palette, int accent_idx) {
    return (int)eigen_syscall(EIGEN_SYS_SETTINGS, EIGEN_SETTINGS_THEME_ADD,
                              (uint64_t)name, (uint64_t)palette, (uint64_t)accent_idx);
}
int eigen_wall_thumb_raw(int idx, uint8_t* dst) {
    return (int)eigen_syscall(EIGEN_SYS_SETTINGS, EIGEN_SETTINGS_WALL_THUMB_RAW,
                              (uint64_t)idx, (uint64_t)(uintptr_t)dst, 0);
}

int eigen_spawn(const char* name) { return (int)eigen_syscall(EIGEN_SYS_SPAWN, (uint64_t)name, 0, 0, 0); }
/* Spawn with a POSIX-style argv (user-space array, NULL-terminated).
 * The kernel copies the strings into the new process's stack. */
int eigen_spawn_args(const char* name, int argc, char* const argv[]) {
    return (int)eigen_syscall(EIGEN_SYS_SPAWN, (uint64_t)name, 0, (uint64_t)argv, (uint64_t)argc);
}

int eigen_spawn_fds(const char* name, int argc, char* const argv[], const int fds[3]) {
    return (int)eigen_syscall(EIGEN_SYS_SPAWN_FDS, (uint64_t)name,
                              (uint64_t)(uintptr_t)fds,
                              (uint64_t)(uintptr_t)argv, (uint64_t)argc);
}
int eigen_wait(int pid, int* exit_code_out) {
    int r = (int)eigen_syscall(EIGEN_SYS_WAIT, (uint64_t)pid, 0, 0, 0);
    if (exit_code_out) *exit_code_out = r;
    return r < 0 ? -1 : 0;
}
void eigen_exit(int code) { eigen_syscall(EIGEN_SYS_EXIT, (uint64_t)code, 0, 0, 0); for (;;) {} }

uint32_t eigen_gettime_ms(void) { return (uint32_t)eigen_syscall(EIGEN_SYS_GETTIME, 0, 0, 0, 0); }
void eigen_sleep_ms(uint32_t ms) { eigen_syscall(EIGEN_SYS_SLEEP, ms, 0, 0, 0); }

void* eigen_malloc(size_t size) { return (void*)eigen_syscall(EIGEN_SYS_ALLOC, size, 0, 0, 0); }
void  eigen_free(void* ptr)     { eigen_syscall(EIGEN_SYS_FREE, (uint64_t)ptr, 0, 0, 0); }

int eigen_sysinfo(struct eigen_sysinfo* info) {
    return (int)eigen_syscall(EIGEN_SYS_SYSINFO, (uint64_t)info, 0, 0, 0);
}

/* ── Ring-3 windows ─────────────────────────────────────────── */
int eigen_win_create(int x, int y, int w, int h, const char* title) {
    uint64_t xy = ((uint64_t)(uint16_t)x << 16) | (uint64_t)(uint16_t)y;
    uint64_t wh = ((uint64_t)(uint16_t)w << 16) | (uint64_t)(uint16_t)h;
    return (int)eigen_syscall(EIGEN_SYS_WIN, EIGEN_WIN_CREATE, xy, wh, (uint64_t)title);
}
void* eigen_win_map(int id) {
    return (void*)eigen_syscall(EIGEN_SYS_WIN, EIGEN_WIN_MAP, (uint64_t)id, 0, 0);
}
int eigen_win_flush(int id) {
    return (int)eigen_syscall(EIGEN_SYS_WIN, EIGEN_WIN_FLUSH, (uint64_t)id, 0, 0);
}
void eigen_win_close(int id) {
    eigen_syscall(EIGEN_SYS_WIN, EIGEN_WIN_CLOSE, (uint64_t)id, 0, 0);
}
int eigen_win_poll(int id, eigen_ev_t* out, int max) {
    return (int)eigen_syscall(EIGEN_SYS_WIN, EIGEN_WIN_POLL,
                              (uint64_t)id, (uint64_t)out, (uint64_t)max);
}
void eigen_win_settitle(int id, const char* title) {
    eigen_syscall(EIGEN_SYS_WIN, EIGEN_WIN_SETTITLE, (uint64_t)id, (uint64_t)title, 0);
}
int eigen_win_getsize(int id, uint32_t* w, uint32_t* h) {
    return (int)eigen_syscall(EIGEN_SYS_WIN, EIGEN_WIN_GETSIZE,
                              (uint64_t)id, (uint64_t)w, (uint64_t)h);
}
int eigen_win_maximize(int id) {
    return (int)eigen_syscall(EIGEN_SYS_WIN, EIGEN_WIN_MAXIMIZE, (uint64_t)id, 0, 0);
}
int eigen_win_gettheme(uint32_t* out, int max) {
    return (int)eigen_syscall(EIGEN_SYS_GET_THEME, (uint64_t)out, (uint64_t)max, 0, 0);
}
uint32_t eigen_theme_tint(uint32_t base, uint32_t accent, int amt) {
    int r = (base >> 16) & 0xFF, g = (base >> 8) & 0xFF, b = base & 0xFF;
    int ar = (accent >> 16) & 0xFF, ag = (accent >> 8) & 0xFF, ab = accent & 0xFF;
    if (amt < 0) amt = 0; if (amt > 255) amt = 255;
    r = r + (ar - r) * amt / 255;
    g = g + (ag - g) * amt / 255;
    b = b + (ab - b) * amt / 255;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

/* ── Time-of-day / font ─────────────────────────────────────── */
int eigen_time_get(int out[6]) {
    return (int)eigen_syscall(EIGEN_SYS_TIME, EIGEN_TIME_GET, (uint64_t)out, 0, 0);
}
void* eigen_font_map(void) {
    return (void*)eigen_syscall(EIGEN_SYS_FONTMAP, EIGEN_FONTMAP_MAP, 0, 0, 0);
}

int eigen_gfx_dims(uint32_t* w, uint32_t* h) {
    return (int)eigen_syscall(EIGEN_SYS_GFX, EIGEN_GFX_GETDIMS, (uint64_t)w, (uint64_t)h, 0);
}
int eigen_gfx_putpixel(int x, int y, uint32_t rgb) {
    return (int)eigen_syscall(EIGEN_SYS_GFX, EIGEN_GFX_PUTPIXEL, (uint64_t)x, (uint64_t)y, rgb);
}
int eigen_gfx_fillrect(int x, int y, int w, int h, uint32_t rgb) {
    uint64_t xy = ((uint64_t)((uint32_t)x & 0xFFFF) << 16) | ((uint32_t)y & 0xFFFF);
    uint64_t wh = ((uint64_t)((uint32_t)w & 0xFFFF) << 16) | ((uint32_t)h & 0xFFFF);
    return (int)eigen_syscall(EIGEN_SYS_GFX, EIGEN_GFX_FILLRECT, xy, wh, rgb);
}
int eigen_gfx_swap(void) { return (int)eigen_syscall(EIGEN_SYS_GFX, EIGEN_GFX_SWAP, 0, 0, 0); }

int eigen_kbhit(void)  { return (int)eigen_syscall(EIGEN_SYS_INPUT, EIGEN_INPUT_KBHIT, 0, 0, 0); }
int eigen_getchar(void){ return (int)eigen_syscall(EIGEN_SYS_INPUT, EIGEN_INPUT_GETCHAR, 0, 0, 0); }
int eigen_mouse_delta(int* dx, int* dy, int* buttons) {
    return (int)eigen_syscall(EIGEN_SYS_INPUT, EIGEN_INPUT_MOUSE_DELTA,
        (uint64_t)(uintptr_t)dx, (uint64_t)(uintptr_t)dy, (uint64_t)(uintptr_t)buttons);
}

int eigen_beep(uint32_t freq_hz, uint32_t dur_ms) {
    return (int)eigen_syscall(EIGEN_SYS_BEEP, freq_hz, dur_ms, 0, 0);
}

long eigen_load_module(const char* name, void* dest, uint64_t maxbytes) {
    return (long)eigen_syscall(EIGEN_SYS_MODLOAD,
                               (uint64_t)name, (uint64_t)dest, maxbytes, 0);
}

/* ------------------------------------------------------------------ */
/* Filesystem (EIGEN_SYS_FS)                                           */
/* ------------------------------------------------------------------ */
int eigen_mkdir(const char* path) {
    return (int)eigen_syscall(EIGEN_SYS_FS, EIGEN_FS_MKDIR, (uint64_t)path, 0, 0);
}
int eigen_fs_create(const char* path) {
    return (int)eigen_syscall(EIGEN_SYS_FS, EIGEN_FS_CREATE, (uint64_t)path, 0, 0);
}
int eigen_fs_delete(const char* path) {
    return (int)eigen_syscall(EIGEN_SYS_FS, EIGEN_FS_DELETE, (uint64_t)path, 0, 0);
}
int eigen_fs_rmdir(const char* path) {
    return (int)eigen_syscall(EIGEN_SYS_FS, EIGEN_FS_RMDIR, (uint64_t)path, 0, 0);
}
int eigen_fs_exists(const char* path) {
    return (int)eigen_syscall(EIGEN_SYS_FS, EIGEN_FS_EXISTS, (uint64_t)path, 0, 0);
}
int eigen_fs_truncate(const char* path) {
    return (int)eigen_syscall(EIGEN_SYS_FS, EIGEN_FS_TRUNCATE, (uint64_t)path, 0, 0);
}
int eigen_fs_rename(const char* oldp, const char* newp) {
    return (int)eigen_syscall(EIGEN_SYS_FS, EIGEN_FS_RENAME, (uint64_t)oldp, (uint64_t)newp, 0);
}
int eigen_fs_list(char* out, int maxlen) {
    return (int)eigen_syscall(EIGEN_SYS_FS, EIGEN_FS_LIST, (uint64_t)out, (uint64_t)maxlen, 0);
}
long eigen_fs_size(const char* path) {
    uint32_t sz = 0;
    int r = (int)eigen_syscall(EIGEN_SYS_FS, EIGEN_FS_SIZE, (uint64_t)path, (uint64_t)&sz, 0);
    if (r != 0) return r;
    return (long)sz;
}

/* File metadata (name/size/type/mtime). Returns 0 on success, <0 on error. */
int eigen_fs_stat(const char* path, struct eigen_stat* st) {
    return (int)eigen_syscall(EIGEN_SYS_FS, EIGEN_FS_STAT, (uint64_t)path, (uint64_t)st, 0);
}
int eigen_fs_chdir(const char* path) {
    return (int)eigen_syscall(EIGEN_SYS_FS, EIGEN_FS_CHDIR, (uint64_t)path, 0, 0);
}
int eigen_fs_pwd(char* out, int maxlen) {
    return (int)eigen_syscall(EIGEN_SYS_FS, EIGEN_FS_PWD, (uint64_t)out, (uint64_t)maxlen, 0);
}

/* One-shot write: truncate (or create) + write + close. */
int eigen_fs_write_file(const char* path, const void* data, int len) {
    if (eigen_fs_exists(path) > 0) {
        if (eigen_fs_truncate(path) != 0) return -1;
    } else {
        if (eigen_fs_create(path) != 0) return -1;
    }
    int fd = eigen_open(path, 0);
    if (fd < 0) return -1;
    int r = eigen_write(fd, data, (uint32_t)len);
    eigen_close(fd);
    return r;
}

/* One-shot read: open + read up to maxlen bytes + close. */
int eigen_fs_read_file(const char* path, void* buf, int maxlen) {
    int fd = eigen_open(path, 0);
    if (fd < 0) return -1;
    int r = eigen_read(fd, buf, (uint32_t)maxlen);
    eigen_close(fd);
    return r;
}

/* ------------------------------------------------------------------ */
/* Ring-3 drawing helpers (clip into the window content buffer)        */
/* ------------------------------------------------------------------ */
void eigen_draw_pixel(uint32_t* buf, int w, int h, int x, int y, uint32_t rgb) {
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    buf[y * w + x] = rgb;
}

void eigen_draw_fillrect(uint32_t* buf, int w, int h, int x, int y, int rw, int rh, uint32_t rgb) {
    if (rw <= 0 || rh <= 0) return;
    for (int py = 0; py < rh; py++) {
        int yy = y + py;
        if (yy < 0 || yy >= h) continue;
        for (int px = 0; px < rw; px++) {
            int xx = x + px;
            if (xx < 0 || xx >= w) continue;
            buf[yy * w + xx] = rgb;
        }
    }
}

void eigen_draw_rect(uint32_t* buf, int w, int h, int x, int y, int rw, int rh, uint32_t rgb) {
    eigen_draw_fillrect(buf, w, h, x, y, rw, 1, rgb);
    eigen_draw_fillrect(buf, w, h, x, y + rh - 1, rw, 1, rgb);
    eigen_draw_fillrect(buf, w, h, x, y, 1, rh, rgb);
    eigen_draw_fillrect(buf, w, h, x + rw - 1, y, 1, rh, rgb);
}

void eigen_draw_fillcircle(uint32_t* buf, int w, int h, int cx, int cy, int r, uint32_t rgb) {
    if (r <= 0) return;
    for (int py = -r; py <= r; py++) {
        for (int px = -r; px <= r; px++) {
            if (px * px + py * py > r * r) continue;
            eigen_draw_pixel(buf, w, h, cx + px, cy + py, rgb);
        }
    }
}

void eigen_draw_circle(uint32_t* buf, int w, int h, int cx, int cy, int r, uint32_t rgb) {
    if (r <= 0) return;
    for (int py = -r; py <= r; py++) {
        for (int px = -r; px <= r; px++) {
            int d = px * px + py * py;
            if (d > r * r || d < (r - 1) * (r - 1)) continue;
            eigen_draw_pixel(buf, w, h, cx + px, cy + py, rgb);
        }
    }
}

/* Text via the kernel font8x16 (8x16 px glyphs, one byte per row). */
void eigen_draw_text(uint32_t* buf, int w, int h, int x, int y, const char* s, uint32_t rgb) {
    const uint8_t* font = (const uint8_t*)eigen_font_map();
    if (!font) return;
    for (int i = 0; s[i]; i++) {
        const uint8_t* glyph = &font[(uint8_t)s[i] * 16];
        for (int row = 0; row < 16; row++) {
            uint8_t bits = glyph[row];
            for (int bit = 0; bit < 8; bit++)
                if (bits & (0x80 >> bit))
                    eigen_draw_pixel(buf, w, h, x + i * 8 + bit, y + row, rgb);
        }
    }
}

/* ------------------------------------------------------------------ */
/* string helpers                                                      */
/* ------------------------------------------------------------------ */
size_t eigen_strlen(const char* s) { size_t n = 0; while (s[n]) n++; return n; }

void eigen_memset(void* p, int c, size_t n) {
    unsigned char* b = (unsigned char*)p;
    for (size_t i = 0; i < n; i++) b[i] = (unsigned char)c;
}

void eigen_memcpy(void* dst, const void* src, size_t n) {
    const unsigned char* s = (const unsigned char*)src;
    unsigned char* d = (unsigned char*)dst;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
}

int eigen_strcmp(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

/* ------------------------------------------------------------------ */
/* Console                                                             */
/* ------------------------------------------------------------------ */
static void emit_str(const char* s) { eigen_write(1, s, (uint32_t)eigen_strlen(s)); }

int eigen_puts(const char* s) { emit_str(s); return 0; }

static void emit_unsigned(uint64_t v, int base, int upper) {
    char buf[24];
    const char* d = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int i = 0;
    if (v == 0) { eigen_write(1, "0", 1); return; }
    while (v > 0 && i < 23) { buf[i++] = d[v % base]; v /= base; }
    for (int j = i - 1; j >= 0; j--) eigen_write(1, &buf[j], 1);
}

static void emit_dec(int64_t v, int upper) {
    /* sign for signed formats is handled by %d caller; this emits magnitude */
    uint64_t u = (uint64_t)(v < 0 ? -v : v);
    emit_unsigned(u, 10, upper);
}

int eigen_printf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int count = 0;
    for (const char* p = fmt; *p; p++) {
        if (*p != '%') {
            eigen_write(1, p, 1);
            count++;
            continue;
        }
        p++;
        if (*p == 0) break;
        if (*p == 'l') { p++; }
        switch (*p) {
            case '%': eigen_write(1, "%", 1); count++; break;
            case 'c': {
                char c = (char)va_arg(ap, int);
                eigen_write(1, &c, 1); count++; break;
            }
            case 's': { const char* s = va_arg(ap, const char*); emit_str(s); count += (int)eigen_strlen(s); break; }
            case 'd': case 'i': {
                int v = va_arg(ap, int);
                if (v < 0) { eigen_write(1, "-", 1); count++; }
                emit_dec(v, 0); break;
            }
            case 'u': case 'x': case 'X': {
                unsigned int v = va_arg(ap, unsigned int);
                if (*p == 'u') emit_unsigned(v, 10, 0);
                else emit_unsigned(v, 16, *p == 'X');
                break;
            }
            default: eigen_write(1, "?", 1); count++; break;
        }
    }
    va_end(ap);
    return count;
}

/* ── Networking Syscalls ─────────────────────────────────────── */
int eigen_socket(int domain, int type, int protocol) {
    return (int)eigen_syscall(EIGEN_SYS_NET, EIGEN_NET_SOCKET, domain, type, protocol);
}

int eigen_connect(int sock, uint32_t ip, uint16_t port) {
    return (int)eigen_syscall(EIGEN_SYS_NET, EIGEN_NET_CONNECT, sock, ip, port);
}

int eigen_send(int sock, const void* buf, int len, int flags) {
    (void)flags;
    return (int)eigen_syscall(EIGEN_SYS_NET, EIGEN_NET_SEND, sock, (uint64_t)(uintptr_t)buf, len);
}

int eigen_recv(int sock, void* buf, int len, int flags) {
    (void)flags;
    return (int)eigen_syscall(EIGEN_SYS_NET, EIGEN_NET_RECV, sock, (uint64_t)(uintptr_t)buf, len);
}

int eigen_socket_close(int sock) {
    return (int)eigen_syscall(EIGEN_SYS_NET, EIGEN_NET_CLOSE, sock, 0, 0);
}

int eigen_dns_resolve(const char* host, uint32_t* ip_out) {
    return (int)eigen_syscall(EIGEN_SYS_NET, EIGEN_NET_RESOLVE, (uint64_t)(uintptr_t)host, (uint64_t)(uintptr_t)ip_out, 0);
}

void eigen_net_poll(void) {
    eigen_syscall(EIGEN_SYS_NET, EIGEN_NET_POLL, 0, 0, 0);
}
/* ── Signals ────────────────────────────────────────────────── */
#include <user/eigen.h>
int kill(int pid, int sig) {
    return (int)eigen_syscall(EIGEN_SYS_KILL,
                              (unsigned long)pid,
                              (unsigned long)sig, 0, 0);
}
int raise(int sig) { return kill(eigen_getpid(), sig); }
