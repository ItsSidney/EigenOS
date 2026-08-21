/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

/* posixtest.c — exercises the libc/posix.c shims used by EFL:
 * mmap (anon + file), setenv/getenv, localtime/strftime,
 * opendir/readdir, getpid, getcwd, dlopen stub. */

#include "posix.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

static int fails = 0;
#define CHECK(cond, msg) do { \
    if (cond) printf("  [PASS] %s\n", msg); \
    else { printf("  [FAIL] %s\n", msg); fails++; } \
} while (0)

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("[posixtest] ring-3 posix shim tests\n");

    /* ---- mmap anonymous ---- */
    void* p = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(p != MAP_FAILED && p != 0, "mmap anonymous");
    if (p != MAP_FAILED && p) {
        memset(p, 0xAB, 4096);
        int ok = 1;
        for (int i = 0; i < 4096; i++) if (((unsigned char*)p)[i] != 0xAB) { ok = 0; break; }
        CHECK(ok, "anon mmap writable/readable");
        CHECK(munmap(p, 4096) == 0, "munmap");
    }

    /* ---- mmap file-backed ---- */
    FILE* f = fopen("/tmp/mapme.txt", "w");
    if (f) {
        fputs("hello mmap file\n", f);
        fclose(f);
    }
    int fd = open("/tmp/mapme.txt", O_RDONLY);
    CHECK(fd >= 0, "open file for mmap");
    if (fd >= 0) {
        char* m = (char*)mmap(0, 32, PROT_READ, MAP_PRIVATE, fd, 0);
        CHECK(m != MAP_FAILED && m && strncmp(m, "hello mmap file", 15) == 0,
              "file-backed mmap content");
        if (m != MAP_FAILED && m) munmap(m, 32);
        close(fd);
    }

    /* ---- environment ---- */
    CHECK(setenv("EFL_TEST", "works", 1) == 0, "setenv");
    char* v = getenv("EFL_TEST");
    CHECK(v && strcmp(v, "works") == 0, "getenv round-trip");
    CHECK(putenv("PUTENV_VAR=1") == 0 && getenv("PUTENV_VAR"), "putenv");
    CHECK(unsetenv("EFL_TEST") == 0 && !getenv("EFL_TEST"), "unsetenv");

    /* ---- time ---- */
    time_t now = time(0);
    struct tm* tmv = localtime(&now);
    CHECK(tmv != 0, "localtime");
    if (tmv) {
        char buf[64];
        strftime(buf, sizeof buf, "%a %b %d %H:%M:%S %Y", tmv);
        printf("  [INFO] localtime strftime -> %s\n", buf);
        CHECK(strlen(buf) > 5, "strftime produced a date");
    }

    /* ---- process / cwd ---- */
    CHECK(getpid() > 0, "getpid");
    char cwd[256];
    CHECK(getcwd(cwd, sizeof cwd) != 0, "getcwd");

    /* ---- directory listing ---- */
    DIR* d = opendir("/");
    CHECK(d != 0, "opendir");
    if (d) {
        int n = 0;
        struct dirent* e;
        while ((e = readdir(d)) != 0) { n++; }
        CHECK(n > 0, "readdir returned entries");
        closedir(d);
    }

    /* ---- dlopen stub ---- */
    CHECK(dlopen("nope.so", RTLD_NOW) == 0, "dlopen stub returns NULL");

    if (fails == 0) printf("[posixtest] ALL TESTS PASSED\n");
    else            printf("[posixtest] %d TEST(S) FAILED\n", fails);
    return fails ? 1 : 0;
}