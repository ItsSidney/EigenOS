/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

/* userlib.h — libeigen: C-facing userland API (API81 wrappers). */
#ifndef EIGEN_USERLIB_H
#define EIGEN_USERLIB_H

#include <stdint.h>
#include <stddef.h>
#include <user/eigen.h>

/* _start -> main() bootstrap provided by libeigen, apps define main(). */
void _start(void);

/* FD based I/O (fd 0 = stdin, 1 = stdout, 2 = stderr) */
int  eigen_open(const char* path, int flags);
int  eigen_write(int fd, const void* buf, uint32_t count);
int  eigen_read(int fd, void* buf, uint32_t count);
void eigen_close(int fd);
int  eigen_lseek(int fd, int offset, int whence); /* -> new offset, <0 error */
int  eigen_pipe(int pipefd[2]);                  /* -> 0 ok, <0 err */
int  eigen_fcntl(int fd, int cmd, int arg);      /* -> 0/val, <0 err */

/* System settings (EIGEN_SYS_SETTINGS) — themes, accent, wallpaper. */
int  eigen_settings(uint64_t op, uint64_t a, uint64_t b, uint64_t c);
int  eigen_theme_get(int id, eigen_theme_info_t* out);  /* 0 ok, <0 error */
int  eigen_theme_add(const char* name, const uint32_t* palette, int accent_idx); /* -> id */
int  eigen_wall_thumb_raw(int idx, uint8_t* dst96x54);  /* 0 ok */

/* Process */
int  eigen_getpid(void);
int eigen_spawn(const char* name); /* spawn a boot module by basename        */
int eigen_spawn_args(const char* name, int argc, char* const argv[]);            /* returns child PID or <0  */
/* Spawn with fd inheritance: fds[0..2] = parent fds for child stdin/stdout/
   stderr (-1 = closed). Pipes make this the shell/pipe building block.     */
int eigen_spawn_fds(const char* name, int argc, char* const argv[], const int fds[3]);
int  eigen_wait(int pid, int* exit_code_out);  /* returns 0 on success     */
void eigen_exit(int code);

/* Time */
uint32_t eigen_gettime_ms(void);
void     eigen_sleep_ms(uint32_t ms);

/* Memory (kmalloc-backed for now; subject to API81 revision) */
void* eigen_malloc(size_t size);
void  eigen_free(void* ptr);

/* System information */
int eigen_sysinfo(struct eigen_sysinfo* info);

/* Graphics */
int  eigen_gfx_dims(uint32_t* w, uint32_t* h);
int  eigen_gfx_putpixel(int x, int y, uint32_t rgb);
int  eigen_gfx_fillrect(int x, int y, int w, int h, uint32_t rgb);
int  eigen_gfx_swap(void);

/* ── Ring-3 windows ─────────────────────────────────────────── */
int   eigen_win_create(int x, int y, int w, int h, const char* title);
void* eigen_win_map(int id);                 /* user VMA of content buffer */
int   eigen_win_flush(int id);               /* commit drawn frame to WM (copy) */
void  eigen_win_close(int id);
int  eigen_win_poll(int id, eigen_ev_t* out, int max);
void eigen_win_settitle(int id, const char* title);
/* Query a user window's actual content-buffer dimensions (px).
   The buffer is content_w*content_h uint32_t; may differ from the
   requested window size (title bar / scaling). ALWAYS draw to these. */
int  eigen_win_getsize(int id, uint32_t* w, uint32_t* h);
/* Toggle the window between full-screen and its saved size. 0 on success. */
int  eigen_win_maximize(int id);
/* Pull the live OS theme palette into a user array of EIGEN_THEME_COUNT
   uint32_t (see EIGEN_THEME_* indices). Call once at startup so ring-3
   apps match the shell's look. Returns the entry count copied, <0 on error. */
int  eigen_win_gettheme(uint32_t* out, int max);

/* Map a raw ARGB/0xRRGGBB tint toward an accent for per-app flavour.
   out[i] = lerp(base[i], accent, amt/255). Convenience for ui_sync_theme. */
uint32_t eigen_theme_tint(uint32_t base, uint32_t accent, int amt);

/* Time-of-day: fills int out[6] = {hour, minute, second, day, month, year} */
int  eigen_time_get(int out[6]);
void* eigen_font_map(void);                  /* user VMA of font8x16 */

/* Input (non-blocking) */
int  eigen_kbhit(void);
int  eigen_getchar(void);
int  eigen_mouse_delta(int* dx, int* dy, int* buttons);

/* Audio (PC speaker) */
int  eigen_beep(uint32_t freq_hz, uint32_t dur_ms);

/* Boot-module loader: copy a named Limine boot module (e.g. a WAD) into a
   user buffer. Returns bytes copied (>0) or a negative EIGEN_ERR_* code. */
long eigen_load_module(const char* name, void* dest, uint64_t maxbytes);

/* ── Filesystem (ring-3 files & folders) ─────────────────────── */
int  eigen_mkdir(const char* path);             /* create a directory       */
int  eigen_fs_create(const char* path);         /* create an empty file     */
int  eigen_fs_delete(const char* path);         /* delete a file            */
int  eigen_fs_rmdir(const char* path);          /* remove a directory       */
int  eigen_fs_exists(const char* path);         /* 1 exists, 0 not          */
int  eigen_fs_truncate(const char* path);       /* empty an existing file   */
int  eigen_fs_rename(const char* oldp, const char* newp);
int  eigen_fs_list(char* out, int maxlen);      /* names (ANSI coloured)    */
long eigen_fs_size(const char* path);           /* file size, <0 on error   */
int  eigen_fs_stat(const char* path, struct eigen_stat* st); /* 0 ok, <0 error */
int  eigen_fs_chdir(const char* path);          /* change directory         */
int  eigen_fs_pwd(char* out, int maxlen);       /* get current directory    */

/* One-shot file helpers (open/write/close under the hood). */
int  eigen_fs_write_file(const char* path, const void* data, int len);
int  eigen_fs_read_file(const char* path, void* buf, int maxlen);  /* returns bytes read */

/* ── Ring-3 drawing helpers (draw straight into the window buffer) ──
   The buffer is content_w*content_h uint32_t; you got it from
   eigen_win_map(). All helpers clip to the buffer, so drawing off the
   edge is safe. Pixel format is 0x00RRGGBB. */
void eigen_draw_pixel(uint32_t* buf, int w, int h, int x, int y, uint32_t rgb);
void eigen_draw_fillrect(uint32_t* buf, int w, int h, int x, int y, int rw, int rh, uint32_t rgb);
void eigen_draw_rect(uint32_t* buf, int w, int h, int x, int y, int rw, int rh, uint32_t rgb);
void eigen_draw_fillcircle(uint32_t* buf, int w, int h, int cx, int cy, int r, uint32_t rgb);
void eigen_draw_circle(uint32_t* buf, int w, int h, int cx, int cy, int r, uint32_t rgb);
void eigen_draw_text(uint32_t* buf, int w, int h, int x, int y, const char* s, uint32_t rgb);

/* Console helpers */
int  eigen_printf(const char* fmt, ...);
int  eigen_puts(const char* s);

/* string helpers (independent of kernel libc) */
size_t eigen_strlen(const char* s);
void   eigen_memset(void* p, int c, size_t n);
void   eigen_memcpy(void* dst, const void* src, size_t n);
/* ── Signals (POSIX names over SYS_SIGNAL/SYS_KILL) ─────────── */
extern int eigen_getpid(void);
int kill(int pid, int sig);
int raise(int sig);

/* ── Networking ─────────────────────────────────────────────── */
int  eigen_socket(int domain, int type, int protocol);
int  eigen_connect(int sock, uint32_t ip, uint16_t port);
int  eigen_send(int sock, const void* buf, int len, int flags);
int  eigen_recv(int sock, void* buf, int len, int flags);
int  eigen_socket_close(int sock);
int  eigen_dns_resolve(const char* host, uint32_t* ip_out);
void eigen_net_poll(void);

#endif /* EIGEN_USERLIB_H */