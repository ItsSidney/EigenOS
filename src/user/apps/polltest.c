/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

/* polltest.c — exercises poll(2), select(2) and epoll on EigenOS.
 * All backing objects are synchronous, so a readable/writable fd always
 * reports ready and an unbacked fd (stdin without a tty) reports
 * POLLNVAL. Verifies ready bits, timeouts and epoll ADD/MOD/DEL/WAIT. */

#include <poll.h>
#include <sys/select.h>
#include <sys/epoll.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <userlib.h>
#include <stdarg.h>

static int fails = 0;
static void checkf(int ok, const char* fmt, ...)
    __attribute__((format(printf, 2, 3)));
static void checkf(int ok, const char* fmt, ...) {
    if (ok) printf("  [PASS] "); else { printf("  [FAIL] "); fails++; }
    va_list ap; va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
}
#define CHECK(cond, ...) checkf((cond), __VA_ARGS__)

static void clear_file(void) {
    FILE* f = fopen("/polltest.txt", "w");
    if (f) fclose(f);
}

int main(void) {
    printf("[POLLTEST] poll/select/epoll\n");

    clear_file();
    int fd = open("/polltest.txt", O_RDONLY);
    CHECK(fd >= 0, "open /polltest.txt");
    if (fd < 0) { printf("  [FAIL] aborting, no fd\n"); return 1; }

    /* ---- poll: ready bits on a real file ---- */
    int badfd = 13;                     /* a free, unopened fd -> POLLNVAL */
    struct pollfd p[4];
    p[0].fd = fd;       p[0].events = POLLIN | POLLOUT; p[0].revents = 0;
    p[1].fd = 1;        p[1].events = POLLOUT;          p[1].revents = 0;  /* stdout */
    p[2].fd = badfd;    p[2].events = POLLIN;           p[2].revents = 0;  /* POLLNVAL */
    p[3].fd = -1;       p[3].events = POLLIN;           p[3].revents = 0;  /* skipped */
    int n = poll(p, 4, 0);
    CHECK(n == 3, "poll(0) returns 3 ready (file, stdout, nval)");
    CHECK(p[0].revents & POLLIN && p[0].revents & POLLOUT, "file fd POLLIN|POLLOUT");
    CHECK(p[1].revents & POLLOUT, "stdout POLLOUT");
    CHECK(p[2].revents & POLLNVAL, "unopened fd POLLNVAL");
    CHECK(p[3].revents == 0, "fd -1 ignored");

    /* ---- poll: timeout ---- */
    uint32_t t0 = eigen_gettime_ms();
    n = poll(p + 1, 1, 80);   /* stdout only, never blocks */
    uint32_t dt = eigen_gettime_ms() - t0;
    CHECK(n == 1 && dt < 40, "poll stdout immediate (~%ums)", dt);
    t0 = eigen_gettime_ms();
    n = poll(0, 0, 80);       /* no fds -> pure sleep */
    dt = eigen_gettime_ms() - t0;
    CHECK(n == 0 && dt >= 70 && dt < 300, "poll timeout 0 after ~%ums", dt);

    /* ---- select ---- */
    fd_set r, w;
    FD_ZERO(&r); FD_SET(fd, &r);
    FD_ZERO(&w); FD_SET(1, &w);
    struct timeval tv = { 0, 0 };
    int snfds = (fd > 1) ? fd + 1 : 2;          /* cover stdout (fd 1) */
    n = select(snfds, &r, &w, 0, &tv);
    CHECK(n == 2, "select returns 2 (read fd + stdout)");
    CHECK(FD_ISSET(fd, &r) && FD_ISSET(1, &w), "select bitmaps set");
    FD_ZERO(&r); FD_SET(fd, &r);
    FD_ZERO(&w);
    n = select(snfds, &r, &w, 0, &tv);
    CHECK(n == 1 && FD_ISSET(fd, &r) && !FD_ISSET(fd, &w), "select read-only");

    /* ---- epoll ---- */
    int efd = epoll_create1(0);
    CHECK(efd > 0, "epoll_create1");
    if (efd > 0) {
        struct epoll_event ev;
        ev.events = EPOLLIN | EPOLLOUT;
        ev.data = 0xABCD;
        CHECK(epoll_ctl(efd, EPOLL_CTL_ADD, fd, &ev) == 0, "epoll_ctl ADD");
        int e1;
        (void)epoll_ctl(efd, EPOLL_CTL_ADD, fd, &ev);   /* duplicate */
        e1 = errno;
        CHECK(errno == EEXIST && epoll_ctl(efd, EPOLL_CTL_ADD, fd, &ev) == -1,
              "epoll_ctl ADD dup -> EEXIST (errno=%d)", e1);
        int e2 = (epoll_ctl(efd, EPOLL_CTL_DEL, 9999, &ev), errno);
        CHECK(epoll_ctl(efd, EPOLL_CTL_DEL, 9999, &ev) == -1 && e2 == EBADF,
              "epoll_ctl DEL bad fd -> EBADF (errno=%d)", e2);
        ev.events = EPOLLOUT;
        ev.data = 0xBEEF;
        CHECK(epoll_ctl(efd, EPOLL_CTL_MOD, fd, &ev) == 0, "epoll_ctl MOD");

        struct epoll_event out[4];
        n = epoll_wait(efd, out, 4, 0);
        CHECK(n == 1, "epoll_wait returns 1");
        CHECK(out[0].data == 0xBEEF, "epoll_wait data preserved");
        CHECK(out[0].events & EPOLLOUT, "epoll_wait EPOLLOUT");

        t0 = eigen_gettime_ms();
        n = epoll_wait(efd, out, 4, 60);
        dt = eigen_gettime_ms() - t0;
        CHECK(n == 1, "epoll_wait EPOLLOUT still ready");
        CHECK(epoll_ctl(efd, EPOLL_CTL_DEL, fd, 0) == 0, "epoll_ctl DEL");
        n = epoll_wait(efd, out, 4, 0);
        CHECK(n == 0, "epoll_wait 0 after DEL");

        CHECK(epoll_wait(9999, out, 4, 0) == -1 && errno == EBADF,
              "epoll_wait bad id -> EBADF (errno=%d)", errno);
        close(efd);
    }

    /* ---- pipe + fcntl ---- */
    int pfd[2] = {-1, -1};
    if (pipe(pfd) != 0) { printf("[POLLTEST] pipe create failed errno=%d\n", errno); fails++; }
    CHECK(pfd[0] >= 0 && pfd[1] >= 0, "pipe create (r=%d w=%d)", pfd[0], pfd[1]);
    if (pfd[0] >= 0 && pfd[1] >= 0) {
        int fl = fcntl(pfd[0], F_GETFL);
        CHECK(fl >= 0 && (fl & O_NONBLOCK) == 0, "fcntl F_GETFL read end (0x%x)", fl);
        CHECK(fcntl(pfd[1], F_SETFL, O_NONBLOCK) == 0, "fcntl F_SETFL write end O_NONBLOCK");
        CHECK((fcntl(pfd[1], F_GETFL) & O_NONBLOCK), "write end is nonblocking now");

        /* write end immediately writable, read end not yet */
        struct pollfd p2[2];
        p2[0].fd = pfd[0]; p2[0].events = POLLIN;
        p2[1].fd = pfd[1]; p2[1].events = POLLOUT;
        CHECK(poll(p2, 2, 0) == 1 && p2[1].revents & POLLOUT && !(p2[0].revents & POLLIN),
              "empty pipe: w ready, r not");

        /* push bytes: now read side ready; write end stays writable (small payload) */
        CHECK(write(pfd[1], "hello", 5) == 5, "pipe write 5 bytes");
        CHECK(poll(p2, 2, 0) == 2 && (p2[0].revents & POLLIN) && (p2[1].revents & POLLOUT),
              "after write: r POLLIN, w still POLLOUT");
        char c[8]; n = (int)read(pfd[0], c, 5);
        CHECK(n == 5, "pipe read got 5 bytes");
        CHECK(poll(p2, 2, 0) == 1 && (p2[1].revents & POLLOUT) && !(p2[0].revents & (POLLIN|POLLHUP)),
              "after drain: r not-ready, w ready");

    close(pfd[0]); close(pfd[1]);
        CHECK(fcntl(pfd[0], F_GETFL) == -1 && errno == EBADF,
              "fcntl on closed fd -> EBADF (errno=%d)", errno);
    }

    /* select with pipe + timeout */
    int p2[2];
    CHECK(pipe(p2) == 0, "pipe for select");
    if (p2[0] >= 0) {
        FD_ZERO(&r); FD_SET(p2[0], &r); FD_ZERO(&w);
        tv.tv_sec = 0; tv.tv_usec = 50000;   /* 50ms */
        n = select(p2[1] + 1, &r, &w, 0, &tv);
        CHECK(n == 0, "select timeout 50ms with empty pipe -> 0");
        (void)write(p2[1], "z", 1);
        FD_ZERO(&r); FD_SET(p2[0], &r); FD_ZERO(&w);
        tv.tv_sec = 0; tv.tv_usec = 50000;
        n = select(p2[1] + 1, &r, &w, 0, &tv);
        CHECK(n == 1 && FD_ISSET(p2[0], &r), "select returns readable after pipe write");
        close(p2[0]); close(p2[1]);
    }

    close(fd);
    printf("[POLLTEST] %s (%d fails)\n", fails ? "FAILED" : "ALL PASS", fails);
    return fails ? 1 : 0;
}