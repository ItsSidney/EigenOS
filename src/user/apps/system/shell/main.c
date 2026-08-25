/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

/* sh — the EigenOS command shell.
 *
 * Design in one paragraph: the shell lives on the SLAVE end of a pty that
 * the Terminal app owns the master of. It reads one cooked line at a time
 * from stdin, splits it into words, runs builtins (cd/pwd/echo/clear/
 * exit) itself, and everything else is searched in /user then handed to
 * /user/busybox with argv[0] set to the typed command so BusyBox dispatch
 * the right applet. ^C is delivered to the shell, which relays it to the
 * child it is currently waiting on.
 */
/* EigenOS shell (sh) — runs on a PTY slave, spawns programs via
 * EIGEN_SYS_SPAWN_FDS with inherited stdin/stdout/stderr.
 * Builtins: cd pwd exit. Everything else: PATH lookup in /bin,/userapp,
 * or direct path; final fallback /bin/eigenbox <cmd>. */
#include "userlib.h"
#include "eigen.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>

static char cwd[128] = "/";

#define MAXA 16
static int split(char* line, char* argv[MAXA]) {
    int n = 0; char* p = line;
    while (*p && n < MAXA - 1) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        if (*p == '"') {                     /* quoted arg */
            p++; argv[n++] = p;
            while (*p && *p != '"') p++;
            if (*p) *p++ = 0;
            continue;
        }
        argv[n++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) *p++ = 0;
    }
    argv[n] = 0;
    return n;
}

static void set_cwd(const char* p) {
    if (!p || !p[0]) return;
    if (p[0] == '/') { strncpy(cwd, p, sizeof(cwd)-1); cwd[sizeof(cwd)-1]=0; return; }
    if (strcmp(p, "..") == 0) {
        char* s = strrchr(cwd, '/');
        if (s && s != cwd) *s = 0;
        return;
    }
    int l = strlen(cwd);
    if (l && cwd[l-1] != '/') { cwd[l]='/'; cwd[l+1]=0; }
    strncat(cwd, p, sizeof(cwd)-strlen(cwd)-1);
}

static volatile int fg_pid = 0;

static void sigint_handler(int sig) {
    (void)sig;
    if (fg_pid > 0) eigen_kill(fg_pid, SIGINT);
}

/* returns 1 if spawned */
static int try_spawn(const char* path, int argc, char** argv, int bg) {
    int pid = eigen_spawn_fds(path, argc, argv, (const int[]){0,1,2});
    if (pid <= 0) return 0;
    if (!bg) {
        fg_pid = pid;                       /* ^C now targets the child */
        int code = -1;
        while (eigen_wait(pid, &code) != 0) eigen_sleep_ms(10);
        fg_pid = 0;
        if (code > 0)
            printf("\r\n[exit %d]\r\n", code);
    }
    return 1;
}

int main(int argc, char* argv[]) {
    (void)argc;(void)argv;
    /* probe: raw write before ANY libc machinery — if this byte never
       reaches the pty master, the failure is pre-main / fd wiring. */
    write(1, "SH-ALIVE\n", 9);
    eigen_signal(SIGINT, sigint_handler);

    static char line[512];
    static char copy[512];

    for (;;) {
        /* prompt: user@host:cwd$ */
        printf("\x1b[36meigen\x1b[90m@\x1b[36meigenos\x1b[90m:\x1b[34m%s\x1b[90m$\x1b[0m ", cwd);
        fflush(stdout);

        int len = 0;
        for (;;) {
            char c;
            ssize_t n = read(0, &c, 1);
            if (n <= 0) return 0;         /* EOF / pty closed */
            if (c == '\n') break;
            if (c == '\b' || c == 0x7F) { if (len) len--; continue; }
            if ((unsigned char)c >= 0x20 && len < (int)sizeof(line)-1)
                line[len++] = c;
        }
        line[len] = 0;

        memcpy(copy, line, len+1);
        char* argvv[MAXA];
        int na = split(copy, argvv);
        if (!na) continue;

        const char* cmd = argvv[0];

        /* ---- builtins ---- */
        if (!strcmp(cmd, "exit")) return 0;
        if (!strcmp(cmd, "cd")) {
            set_cwd(na>1 ? argvv[1] : "/");
            continue;
        }
        if (!strcmp(cmd, "pwd")) { printf("%s\r\n", cwd); continue; }
        if (!strcmp(cmd, "echo")) {
            for (int i = 1; i < na; i++) printf("%s%s", argvv[i], i+1<na?" ":"");
            printf("\r\n");
            continue;
        }
        if (!strcmp(cmd, "clear")) { printf("\x1b[2J\x1b[H"); fflush(stdout); continue; }

        /* rebuild argv with absolute cwd-relative resolution later */
        int bg = 0;
        if (na > 1 && !strcmp(argvv[na-1], "&")) { bg = 1; argvv[na-1] = 0; na--; }

        /* direct path? */
        if (cmd[0] == '/' ) {
            try_spawn(cmd, na, argvv, bg);
            continue;
        }

        /* PATH search */
        static const char* dirs[] = { "/user/", "/userapp/", "/user/", 0 };
        char path[256];
        int ok = 0;
        for (int i = 0; dirs[i] && !ok; i++) {
            snprintf(path, sizeof(path), "%s%s", dirs[i], cmd);
            ok = try_spawn(path, na, argvv, bg);
        }
        if (!ok) {
            /* BusyBox dispatch: /user/busybox with argv[0]=applet name */
            ok = try_spawn("/user/busybox", na, argvv, bg);
        }
        if (!ok) {
            printf("sh: %s: not found\r\n", cmd);
        }
    }
}
