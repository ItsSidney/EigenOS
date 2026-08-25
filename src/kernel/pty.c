/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

/* pty.c — pseudo-terminal pairs for the EigenOS graphical terminal.
 *
 * A PTY is a connected pair of ring buffers:
 *
 *   [keyboard] -> MASTER  --(line discipline)-->  SLAVE  -> [shell stdin]
 *   [screen]   <- MASTER  <--(raw output)-------  SLAVE  <- [shell stdout]
 *
 * The GUI terminal holds the master end; the shell inherits the slave as
 * its stdin/stdout/stderr. Cooked mode (default) gives the shell clean
 * lines: we echo typed characters, handle backspace, map Enter to 
 and
 * deliver SIGINT on ^C. Raw mode (tcsetattr without ICANON) hands every
 * byte straight through so full-screen programs (vi, htop) see exactly
 * what is typed.
 */
/* PTY pseudo-terminal subsystem: master (emulator) <-> line discipline <->
 * slave (shell). Built on the same ring-buffer pattern as pipes. */
#include <kernel/task/task.h>
#include <kernel/task/pty.h>
#include <kernel/log.h>
#include <string.h>

struct pty ptys[MAX_PTYS];

extern int  send_signal(int pid, int sig);
extern int  alloc_fd(task_t* t);

void pty_init(void) { memset(ptys, 0, sizeof(ptys)); }

static uint32_t ring_avail(uint32_t h, uint32_t t) {
    return (h - t + PTY_BUF_SIZE) % PTY_BUF_SIZE;
}
static uint32_t ring_free(uint32_t h, uint32_t t) {
    return PTY_BUF_SIZE - 1 - ring_avail(h, t);
}

static void ring_put(volatile uint32_t* h, volatile uint32_t* t,
                     char* d, char c) {
    if (!ring_free(*h, *t)) return;
    d[*h] = c;
    *h = (*h + 1) % PTY_BUF_SIZE;
}

static int ring_get(volatile uint32_t* h, volatile uint32_t* t,
                    char* d, char* buf, int max) {
    uint32_t n = ring_avail(*h, *t);
    if (n > (uint32_t)max) n = (uint32_t)max;
    for (uint32_t i = 0; i < n; i++) {
        buf[i] = d[*t];
        *t = (*t + 1) % PTY_BUF_SIZE;
    }
    return (int)n;
}

/* Commit the pending edit line to the slave input buffer. */
static void pty_commit_line(struct pty* p) {
    if (p->line_len + 1 > ring_free(p->in_head, p->in_tail)) return; /* drop */
    for (uint32_t i = 0; i < p->line_len; i++)
        ring_put(&p->in_head, &p->in_tail, p->in_data, p->line[i]);
    ring_put(&p->in_head, &p->in_tail, p->in_data, '\n');
    p->line_len = 0;
}

static void pty_erase_char(struct pty* p) {
    if (!p->line_len) return;
    p->line_len--;
    if (p->echo) {
        static const char bs[3] = { '\b', ' ', '\b' };
        /* echo through out-ring so the emulator erases visually */
        for (int i = 0; i < 3; i++)
            ring_put(&p->out_head, &p->out_tail, p->out_data, bs[i]);
    }
}

/* Master write path: keyboard bytes -> line discipline -> slave input.
   In RAW mode bytes go straight through: no echo, no editing, no ^C. */
static void pty_input(struct pty* p, const char* buf, int n) {
    if (p->raw) {
        for (int i = 0; i < n; i++)
            ring_put(&p->in_head, &p->in_tail, p->in_data, buf[i]);
        return;
    }
    for (int i = 0; i < n; i++) {
        char c = buf[i];
        switch (c) {
        case '\r':
            c = '\n';
            /* fallthrough */
        case '\n':
            if (p->echo)
                ring_put(&p->out_head, &p->out_tail, p->out_data, '\r'),
                ring_put(&p->out_head, &p->out_tail, p->out_data, '\n');
            pty_commit_line(p);
            break;
        case 0x08:
        case 0x7F:
            pty_erase_char(p);
            break;
        case 0x03: /* ^C */
            p->line_len = 0;
            if (p->echo) {
                static const char ci[2] = { '^', 'C' };
                for (int k = 0; k < 2; k++)
                    ring_put(&p->out_head, &p->out_tail, p->out_data, ci[k]);
                ring_put(&p->out_head, &p->out_tail, p->out_data, '\r');
                ring_put(&p->out_head, &p->out_tail, p->out_data, '\n');
            }
            if (p->fg_pid) send_signal((int)p->fg_pid, SIGINT);
            break;
        case 0x04: /* ^D: EOF -> commit empty line so reader sees "\n" only */
            if (p->line_len == 0) {
                if (p->echo)
                    ring_put(&p->out_head, &p->out_tail, p->out_data, '\r'),
                    ring_put(&p->out_head, &p->out_tail, p->out_data, '\n');
                ring_put(&p->in_head, &p->in_tail, p->in_data, '\n');
            } else {
                pty_commit_line(p);
            }
            break;
        default:
            if (c >= 0x20 && (unsigned char)c < 0x7F) {
                if (p->line_len < PTY_LINE_MAX - 1) p->line[p->line_len++] = c;
                if (p->echo)
                    ring_put(&p->out_head, &p->out_tail, p->out_data, c);
            }
            break;
        }
    }
}

int sys_openpty(int fds[2]) {
    struct pty* p = 0;
    for (int i = 0; i < MAX_PTYS; i++)
        if (!ptys[i].used) { p = &ptys[i]; break; }
    if (!p) return -1;
    memset(p, 0, sizeof(*p));
    p->used = 1;
    p->echo = 1;
    p->raw = 0;
    p->cols = 80;
    p->rows = 24;

    task_t* cur = get_current_task();
    int m = alloc_fd(cur);
    if (m < 0) { p->used = 0; return -1; }
    /* Mark the master BEFORE allocating the slave: alloc_fd scans for
       FD_VFS slots, so an unconfigured master would be handed out twice
       (m == s) and the slave config would clobber the master. */
    int idx = (int)(p - ptys);
    cur->fd_types[m] = FD_PTY; cur->fd_pty[m] = (uint8_t)idx;
    cur->fd_flags_extra[m] = PTY_MASTER;
    int s = alloc_fd(cur);
    if (s < 0) { cur->fd_types[m] = FD_VFS; p->used = 0; return -1; }
    cur->fd_types[s] = FD_PTY; cur->fd_pty[s] = (uint8_t)idx;
    cur->fd_flags_extra[s] = PTY_SLAVE;

    p->master_refs = 1;
    p->slave_refs  = 1;
    fds[0] = m; fds[1] = s;
    extern void serial_puts(const char*);
    extern void serial_u64(uint64_t);
    serial_puts("[PTY] openpty idx="); serial_u64((uint64_t)idx);
    serial_puts(" m="); serial_u64((uint64_t)m);
    serial_puts(" s="); serial_u64((uint64_t)s); serial_puts("\n");
    return 0;
}

/* ---- dispatchers used by sys_read/sys_write/sys_close/poll ---- */

void pty_dup_ref(int idx, int is_master) {
    if (idx < 0 || idx >= MAX_PTYS || !ptys[idx].used) return;
    if (is_master) ptys[idx].master_refs++;
    else           ptys[idx].slave_refs++;
}

int pty_set_raw(int idx, int raw) {
    if (idx < 0 || idx >= MAX_PTYS || !ptys[idx].used) return -1;
    ptys[idx].raw = raw ? 1 : 0;
    if (!ptys[idx].raw) ptys[idx].line_len = 0;   /* flush partial edit */
    return 0;
}

int pty_set_winsize(int idx, uint32_t rows, uint32_t cols) {
    if (idx < 0 || idx >= MAX_PTYS || !ptys[idx].used) return -1;
    ptys[idx].rows = rows; ptys[idx].cols = cols;
    return 0;
}

int pty_get_winsize(int idx, uint32_t* rows, uint32_t* cols) {
    if (idx < 0 || idx >= MAX_PTYS || !ptys[idx].used) return -1;
    if (rows) *rows = ptys[idx].rows;
    if (cols) *cols = ptys[idx].cols;
    return 0;
}

int pty_get_fg(int idx) {
    if (idx < 0 || idx >= MAX_PTYS || !ptys[idx].used) return 0;
    return (int)ptys[idx].fg_pid;
}

int pty_set_fg(int idx, uint32_t pid) {
    if (idx < 0 || idx >= MAX_PTYS || !ptys[idx].used) return -1;
    ptys[idx].fg_pid = pid;
    return 0;
}

int pty_read(int idx, int is_master, char* buf, uint32_t count, int nonblock) {
    if (idx < 0 || idx >= MAX_PTYS || !ptys[idx].used) return -1;
    struct pty* p = &ptys[idx];
    for (;;) {
        int n;
        if (is_master)
            n = ring_get(&p->out_head, &p->out_tail, p->out_data, buf,
                         count > 4096 ? 4096 : (int)count);
        else
            n = ring_get(&p->in_head, &p->in_tail, p->in_data, buf,
                         count > 4096 ? 4096 : (int)count);
        if (n > 0) return n;
        if (nonblock) return -1;
        /* EOF when opposite end fully closed */
        if (is_master ? (p->slave_refs == 0) : (p->master_refs == 0)) return 0;
        sleep_task(4);
    }
}

int pty_write(int idx, int is_master, const char* buf, uint32_t count) {
    if (idx < 0 || idx >= MAX_PTYS || !ptys[idx].used) return -1;
    struct pty* p = &ptys[idx];
    if (is_master) {
        pty_input(p, buf, (int)count);
        return (int)count;
    }
    /* slave -> master output ring (cooked mode applies ONLCR: bare \n
       becomes \r\n so column tracking matches a real terminal) */
    if (!p->raw) {
        for (uint32_t i = 0; i < count; i++) {
            if (buf[i] == '\n')
                ring_put(&p->out_head, &p->out_tail, p->out_data, '\r');
            ring_put(&p->out_head, &p->out_tail, p->out_data, buf[i]);
        }
        { static int sw = 0;
          if (!sw) {
              sw = 1;
              extern void serial_puts(const char*);
              extern void serial_u64(uint64_t);
              serial_puts("[PTY] first slave write n="); serial_u64((uint64_t)count);
              serial_puts("\n");
          } }
        return (int)count;
    }
    { static int sw = 0;
      if (!sw && !is_master) {
          sw = 1;
          extern void serial_puts(const char*);
          extern void serial_u64(uint64_t);
          serial_puts("[PTY] first slave write n="); serial_u64((uint64_t)count);
          serial_puts("\n");
      } }
    uint32_t wrote = 0;
    while (wrote < count && ring_free(p->out_head, p->out_tail))
        ring_put(&p->out_head, &p->out_tail, p->out_data, buf[wrote++]);
    return (int)(wrote ? wrote : -1);
}

void pty_close(int idx, int is_master) {
    if (idx < 0 || idx >= MAX_PTYS || !ptys[idx].used) return;
    struct pty* p = &ptys[idx];
    if (is_master && p->master_refs) p->master_refs--;
    if (!is_master && p->slave_refs) p->slave_refs--;
    if (p->master_refs <= 0 && p->slave_refs <= 0) memset(p, 0, sizeof(*p));
}

/* poll readiness bits: POLLIN=1, POLLOUT=4 */
int pty_poll(int idx, int is_master) {
    if (idx < 0 || idx >= MAX_PTYS || !ptys[idx].used) return 0;
    struct pty* p = &ptys[idx];
    int bits = 0;
    if (is_master) {
        if (ring_avail(p->out_head, p->out_tail)) bits |= 1;
        bits |= 4;   /* always writable */
    } else {
        if (ring_avail(p->in_head, p->in_tail) || p->line_len) bits |= 1;
        bits |= 4;
    }
    return bits;
}
