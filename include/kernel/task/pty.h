#ifndef EIGEN_PTY_H
#define EIGEN_PTY_H

#include <stdint.h>

#define MAX_PTYS       4
#define PTY_BUF_SIZE   8192
#define PTY_LINE_MAX   512
#define PTY_MASTER     1
#define PTY_SLAVE      0

/* A pseudo-terminal: master end is the emulator (GUI terminal), slave end is
 * the shell. Input typed at the master passes through a cooked-mode line
 * discipline (echo, backspace, ^C -> SIGINT) before reaching the slave. */
struct pty {
    int used;
    /* master -> slave: cooked, complete lines ready for the slave */
    volatile uint32_t in_head, in_tail;
    char in_data[PTY_BUF_SIZE];
    /* line-edit scratch (not yet committed) */
    char line[PTY_LINE_MAX];
    uint32_t line_len;
    /* slave -> master: raw program output */
    volatile uint32_t out_head, out_tail;
    char out_data[PTY_BUF_SIZE];
    int echo;
    int raw;                  /* 1 = passthrough (no line discipline) */
    uint32_t cols, rows;      /* window size for TIOCGWINSZ */
    int master_refs, slave_refs;
    uint32_t fg_pid;          /* receives SIGINT on ^C */
};

int  sys_openpty(int fds[2]);
void pty_init(void);
int  pty_set_raw(int idx, int raw);
int  pty_set_winsize(int idx, uint32_t rows, uint32_t cols);

#endif
