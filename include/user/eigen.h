/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

/* eigen.h — API81: the formal Eigen userland ABI.
 *
 * Stable contract between user-mode programs and the Eigen kernel.
 * - Syscall numbers are permanent: never renumber, only append.
 * - The syscall ABI is int $0x80: rax = number, rdi/rsi/rdx/r10 = args.
 * - Return values < 0 (as signed) are errors from the kernel.
 *
 * Userland programs link libeigen (src/user/lib/userlib) which provides
 * the portable wrappers declared below.
 */
#ifndef EIGEN_H
#define EIGEN_H

#include <stdint.h>
#include <stddef.h>

#define EIGEN_API_VERSION 8101u

/* ------------------------------------------------------------------ */
/* Stable syscall numbers                                              */
/* ------------------------------------------------------------------ */
#define EIGEN_SYS_OPEN      0
#define EIGEN_SYS_WRITE     1
#define EIGEN_SYS_READ      2
#define EIGEN_SYS_CLOSE     3
#define EIGEN_SYS_GETPID    4
#define EIGEN_SYS_SLEEP     5
#define EIGEN_SYS_GETTIME   6
#define EIGEN_SYS_ALLOC     7
#define EIGEN_SYS_FREE      8
#define EIGEN_SYS_EXIT      9
#define EIGEN_SYS_SPAWN    10
#define EIGEN_SYS_WAIT     11
#define EIGEN_SYS_SYSINFO  12
#define EIGEN_SYS_GFX      13
#define EIGEN_SYS_INPUT    14
#define EIGEN_SYS_BEEP     15
#define EIGEN_SYS_WIN      16   /* ring-3 window: create/map/flush/close/poll */
#define EIGEN_SYS_TIME     17   /* fill a time_t (RTC) */
#define EIGEN_SYS_FONTMAP  18   /* map the kernel font into user space */
#define EIGEN_SYS_MODLOAD  19   /* copy a boot module's bytes into a user buffer */
#define EIGEN_SYS_FS       20   /* filesystem: mkdir/create/delete/rename/... */
#define EIGEN_SYS_GET_THEME 21  /* copy the live OS theme palette into a user array */
#define EIGEN_SYS_LSEEK    22   /* seek a file descriptor: (fd, offset, whence) -> new offset */
#define EIGEN_SYS_SETTINGS 23   /* system settings: themes, accent, wallpaper, desktop icons */
#define EIGEN_SYS_NET      24   /* network sockets & DNS */
#define EIGEN_SYS_THREAD   25   /* threads: create/exit/join/yield (same process) */
#define EIGEN_SYS_FUTEX    26   /* futex: wait/wake for userland sync primitives */
#define EIGEN_SYS_POLL     27   /* poll(2): (pollfd*, nfds, timeout_ms) -> ready count */
#define EIGEN_SYS_EPOLL    28   /* epoll: (sub-op, ...) level-triggered monitors */
#define EIGEN_SYS_PIPE     29   /* pipe(2): (int[2] pipefd) -> 0 ok */
#define EIGEN_SYS_FCNTL    30   /* fcntl(2): (fd, cmd, arg) -> val */
#define EIGEN_SYS_COUNT    31

/* EIGEN_SYS_EPOLL sub-operations */
/* CREATE: () -> epoll id (1..32), owned by the calling process group */
#define EIGEN_EPOLL_CREATE 0
/* CTL: (epid, ctl_op, fd, eigen_epoll_ctl* arg) — ADD=1/MOD=2/DEL=3 */
#define EIGEN_EPOLL_CTL    1
/* WAIT: (epid, epoll_event* out, (maxevents<<32)|timeout_ms) -> ready count */
#define EIGEN_EPOLL_WAIT   2

/* EIGEN_SYS_THREAD sub-operations */
/* CREATE: (trampoline, entry, arg) -> tid. The kernel builds an iretq frame
   that enters user mode at RIP = trampoline with rdi = entry, rsi = arg
   (SysV), giving each thread its own kernel stack, 512 KB user stack and a
   4 KiB TLS page (FS base) inside the process's shared address space. */
#define EIGEN_THREAD_CREATE 0
#define EIGEN_THREAD_EXIT   1  /* (retval) -> never returns (leader exit = process exit) */
#define EIGEN_THREAD_JOIN   2  /* (tid) -> exit_code once the thread is dead */
#define EIGEN_THREAD_YIELD  3  /* () -> hand the CPU to the scheduler */

/* EIGEN_SYS_FUTEX sub-operations */
/* WAIT: (addr, expected, timeout_ms) — sleep while *(int*)addr == expected.
   0 = woken by WAKE, 1 = timed out (or value already differs -> no sleep). */
#define EIGEN_FUTEX_WAIT 0
/* WAKE: (addr, n) — wake up to n tasks waiting on addr; returns count. */
#define EIGEN_FUTEX_WAKE 1

/* EIGEN_SYS_NET sub-operations */
#define EIGEN_NET_SOCKET   0    /* (domain, type, proto) -> sock fd */
#define EIGEN_NET_CONNECT  1    /* (sock, ip, port) -> 0 ok, <0 err */
#define EIGEN_NET_SEND     2    /* (sock, buf, len, flags) -> bytes sent */
#define EIGEN_NET_RECV     3    /* (sock, buf, len, flags) -> bytes rcvd */
#define EIGEN_NET_CLOSE    4    /* (sock) -> 0 ok */
#define EIGEN_NET_RESOLVE  5    /* (hostname, uint32_t* ip_out) -> 0 ok */
#define EIGEN_NET_POLL     6    /* () -> 0 */

/* EIGEN_SYS_SETTINGS sub-operations */
#define EIGEN_SETTINGS_THEME_APPLY   0  /* (id)                          — apply a theme        */
#define EIGEN_SETTINGS_THEME_TOTAL   1  /* ()  -> total themes           — built-in + user      */
#define EIGEN_SETTINGS_THEME_GET     2  /* (id, eigen_theme_info_t*)     — name+palette+accent  */
#define EIGEN_SETTINGS_THEME_ADD     3  /* (name, palette[29]*, accent)  — -> new theme id      */
#define EIGEN_SETTINGS_THEME_DEL     4  /* (id)                          — remove a user theme  */
#define EIGEN_SETTINGS_ACCENT_SET    5  /* (idx)                         — live persistent      */
#define EIGEN_SETTINGS_ACCENT_GET    6  /* () -> current accent index                             */
#define EIGEN_SETTINGS_ACCENT_COLOR  7  /* (idx) -> accent table color (0xRRGGBB) or -1          */
#define EIGEN_SETTINGS_WALL_COUNT    8  /* () -> number of wallpapers                             */
#define EIGEN_SETTINGS_WALL_GET      9  /* (i, wp_entry_t*)              — name/path/installed  */
#define EIGEN_SETTINGS_WALL_APPLY    10 /* (i)                           — apply by index       */
#define EIGEN_SETTINGS_WALL_DEFAULT  11 /* ()                            — restore default      */
#define EIGEN_SETTINGS_WALL_PROC     12 /* (procedural_id)               — apply procedural     */
#define EIGEN_SETTINGS_WALL_THUMB    13 /* (i, fb, fbw, packed x/y)      — draw scaled thumb    */
#define EIGEN_SETTINGS_WALL_MODE     14 /* () -> 0 procedural, 1 file                             */
#define EIGEN_SETTINGS_WALL_PROC_ID  15 /* () -> active procedural id (mode 0)                    */
#define EIGEN_SETTINGS_WALL_PATH     16 /* (out, max)                    — active file path      */
#define EIGEN_SETTINGS_DESKTOP_MASK  17 /* (mask)                        — set desktop icons    */
#define EIGEN_SETTINGS_DESKTOP_GET   18 /* () -> current desktop icons mask                        */
#define EIGEN_SETTINGS_THEME_ACTIVE  19 /* () -> active theme id                                   */
#define EIGEN_SETTINGS_WALL_THUMB_RAW 20 /* (i, dst)  — copy 96x54 RGB thumb into user buffer     */
#define EIGEN_SETTINGS_WALL_RESCAN   21 /* ()       — re-scan the wallpaper folder now           */

/* Theme snapshot exported to ring 3 (Settings → Themes) */
typedef struct {
    char name[24];
    uint32_t palette[36];   /* first THEME_ROLE_COUNT (29) slots valid */
    int accent_idx;
    int is_user;
} eigen_theme_info_t;

/* ------------------------------------------------------------------ */
/* Error codes                                                         */
/* ------------------------------------------------------------------ */
#define EIGEN_ERR_BADFD    -1
#define EIGEN_ERR_NOENT    -2
#define EIGEN_ERR_NOMEM    -3
#define EIGEN_ERR_INVAL    -4
#define EIGEN_ERR_NOPROC   -5
#define EIGEN_ERR_BADSCR   -6

/* ------------------------------------------------------------------ */
/* GFX syscall sub-operations (arg1)                                   */
/* ------------------------------------------------------------------ */
#define EIGEN_GFX_GETDIMS   0   /* args: (op, &w_user, &h_user, 0)  */
#define EIGEN_GFX_PUTPIXEL  1   /* args: (op, x, y, rgb)             */
#define EIGEN_GFX_FILLRECT  2   /* args: (op, x, y, w<<16|h)  c=a4? -> use r10 */
#define EIGEN_GFX_SWAP      3   /* present the back buffer           */

/* Input syscall opcodes (r10 = opcode) */
#define EIGEN_INPUT_KBHIT   0   /* returns 1 if a key is pending      */
#define EIGEN_INPUT_GETCHAR 1   /* returns next key (0 if none)       */

/* ------------------------------------------------------------------ */
/* Window syscall sub-operations (arg1)                                */
/* ------------------------------------------------------------------ */
#define EIGEN_WIN_CREATE   0   /* args: (op, x<<16|y, w<<16|h, r8=title ptr) -> win id */
#define EIGEN_WIN_MAP      1   /* args: (op, id, 0, 0) -> user VMA of buffer */
#define EIGEN_WIN_FLUSH    2   /* args: (op, id, 0, 0) -> 0 (buffer is live) */
#define EIGEN_WIN_CLOSE    3   /* args: (op, id, 0, 0) */
#define EIGEN_WIN_POLL     4   /* args: (op, id, out_user, max) -> n */
#define EIGEN_WIN_SETTITLE 5   /* args: (op, id, title ptr, 0) */
#define EIGEN_WIN_GETSIZE  6   /* args: (op, id, &w_user, &h_user) -> 0 */
#define EIGEN_WIN_MAXIMIZE 7   /* args: (op, id, 0, 0): toggle full screen */

/* Live theme palette (mirrors THEME_ROLE_* in gui.h). A ring-3 app calls
   eigen_win_gettheme() once at startup so it draws with the SAME colours
   as the shell (theme-aware), instead of a hard-coded off-brand palette.
   The kernel copies EIGEN_THEME_COUNT uint32_t entries; unused high slots
   are filled with a safe default. */
#define EIGEN_THEME_COUNT  64   /* >= THEME_ROLE_COUNT; extra slots unused */
/* Role indices — MUST match the THEME_ROLE_* ordering in gui.h. */
#define EIGEN_THEME_BG         0   /* background / window bg      */
#define EIGEN_THEME_SURFACE    1   /* raised surface              */
#define EIGEN_THEME_SURFACE_VAR 2  /* sunken surface / panel      */
#define EIGEN_THEME_PRIMARY    3   /* primary text                */
#define EIGEN_THEME_ON_PRIMARY 4
#define EIGEN_THEME_SECONDARY  5   /* dim / secondary text        */
#define EIGEN_THEME_TERTIARY   7
#define EIGEN_THEME_ERROR      8   /* danger                      */
#define EIGEN_THEME_OUTLINE    9   /* border                      */
#define EIGEN_THEME_DISABLED   16
#define EIGEN_THEME_WINTITLE   24
#define EIGEN_THEME_WINBORDER  25
#define EIGEN_THEME_ACCENT     28  /* primary accent               */

/* ------------------------------------------------------------------ */
/* Time / font syscall sub-operations                                  */
/* ------------------------------------------------------------------ */
#define EIGEN_TIME_GET     0   /* args: (op, out_user, 0, 0): time_t {hour,minute,second,day,month,year} */
#define EIGEN_FONTMAP_MAP  0   /* args: (op, 0, 0, 0) -> user VMA of font8x16 */

/* ------------------------------------------------------------------ */
/* Filesystem syscall sub-operations (arg1)                            */
/* Paths are relative to the filesystem working directory (the same    */
/* one the kernel commands use). All return 0 on success, <0 = error.  */
/* ------------------------------------------------------------------ */
#define EIGEN_FS_MKDIR   0   /* args: (op, path)                 create a directory      */
#define EIGEN_FS_CREATE  1   /* args: (op, path)                 create a file (empty)   */
#define EIGEN_FS_DELETE  2   /* args: (op, path)                 delete a file           */
#define EIGEN_FS_RMDIR   3   /* args: (op, path)                 remove a directory      */
#define EIGEN_FS_EXISTS  4   /* args: (op, path) -> 1 if exists, 0 if not               */
#define EIGEN_FS_RENAME  5   /* args: (op, oldpath, newpath)     rename a file or dir    */
#define EIGEN_FS_LIST    6   /* args: (op, out_user, maxlen) -> bytes written (ANSI coloured names) */
#define EIGEN_FS_SIZE    7   /* args: (op, path, &size_user)     file size in bytes      */
#define EIGEN_FS_TRUNCATE 8  /* args: (op, path)                 empty an existing file  */
#define EIGEN_FS_STAT    9   /* args: (op, path, &eigen_stat)    file metadata            */
#define EIGEN_FS_CHDIR   10  /* args: (op, path)                 change directory        */
#define EIGEN_FS_PWD     11  /* args: (op, out_user, maxlen)     get current directory   */

/* lseek whence values (POSIX-compatible) */
#define EIGEN_SEEK_SET 0
#define EIGEN_SEEK_CUR 1
#define EIGEN_SEEK_END 2

/* File metadata returned by EIGEN_FS_STAT */
struct eigen_stat {
    char     name[32];
    uint32_t size;
    uint32_t type;          /* 0 = file, 1 = directory (fs_type_t) */
    uint32_t mtime;
};

/* Ring-3 window events (mirrors wm_user_ev_t) */
#define EIGEN_EV_RENDER 1
#define EIGEN_EV_KEY    2
#define EIGEN_EV_MMOVE  3
#define EIGEN_EV_MDOWN  4
#define EIGEN_EV_MUP    5
#define EIGEN_EV_CLOSE  6

typedef struct {
    uint32_t type;
    int32_t a, b, c, d;
} eigen_ev_t;
#define EIGEN_INPUT_DRAIN   2   /* discard all pending keys           */

/* ------------------------------------------------------------------ */
/* Kernel-provided system information                                  */
/* ------------------------------------------------------------------ */
struct eigen_sysinfo {
    uint32_t api_version;
    uint32_t total_mem_kb;
    uint32_t timer_hz;
    uint32_t screen_w;
    uint32_t screen_h;
    uint32_t task_count;
    uint64_t uptime_ms;
};

typedef struct {
    int pid;
    int exit_code;
} eigen_wait_result;

/* Raw syscall entry (single inline asm, no stack clobbering beyond int).
   ABI: rax=num, rdi=a1, rsi=a2, rdx=a3, r8=a4. The kernel stub saves all
   registers and only reads those five slots. */
static inline uint64_t eigen_syscall(uint64_t num, uint64_t a1, uint64_t a2,
                                     uint64_t a3, uint64_t a4) {
    register uint64_t a4_reg __asm__("r8") = a4;
    uint64_t ret;
    __asm__ volatile ("int $0x80"
        : "=a"(ret)
        : "a"(num), "D"(a1), "S"(a2), "d"(a3), "r"(a4_reg)
        : "rcx", "r11", "memory");
    return ret;
}

#endif /* EIGEN_H */