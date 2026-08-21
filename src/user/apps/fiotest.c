/* fiotest.c — ring-3 self-test for the shared libc FILE* layer.
 * Exercises fopen/fgets/fread/fwrite/fseek/feof/ftell, writes results
 * to stdout (kernel log). Run it from the Start menu / search. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

int main(void) {
    int fails = 0;
    printf("[FIOTEST] FILE* layer self-test\n");

    FILE* f = fopen("/nonexistent_fiotest.txt", "r");
    if (f != NULL || errno != ENOENT) {
        printf("[FIOTEST] FAIL fopen(missing) errno=%d (%s)\n", errno, strerror(errno)); fails++;
    }

    f = fopen("/fiotest.txt", "w");
    if (!f) { printf("[FIOTEST] FAIL fopen(w)\n"); return 1; }
    fputs("line one\n", f);
    fputs("line two\n", f);
    fwrite("raw bytes 123", 1, 13, f);
    fputc('\n', f);
    if (fclose(f) != 0) { printf("[FIOTEST] FAIL fclose\n"); fails++; }

    f = fopen("/fiotest.txt", "r");
    if (!f) { printf("[FIOTEST] FAIL fopen(r)\n"); return 1; }

    char buf[128];
    if (!fgets(buf, sizeof buf, f) || strcmp(buf, "line one\n") != 0) {
        printf("[FIOTEST] FAIL fgets 1: \"%s\"\n", buf); fails++;
    }
    if (!fgets(buf, sizeof buf, f) || strcmp(buf, "line two\n") != 0) {
        printf("[FIOTEST] FAIL fgets 2: \"%s\"\n", buf); fails++;
    }

    char raw[32] = {0};
    size_t n = fread(raw, 1, 13, f);
    if (n != 13 || memcmp(raw, "raw bytes 123", 13) != 0) {
        printf("[FIOTEST] FAIL fread (%zu: %s)\n", n, raw); fails++;
    }

    if (fseek(f, 0, SEEK_SET) != 0 || ftell(f) != 0) {
        printf("[FIOTEST] FAIL fseek/ftell\n"); fails++;
    }
    if (fgetc(f) != 'l') { printf("[FIOTEST] FAIL fgetc after rewind\n"); fails++; }

    char all[64]; size_t m = fread(all, 1, sizeof all, f);
    if (m != 31) { printf("[FIOTEST] FAIL whole-file read (%zu)\n", m); fails++; }

    if (!feof(f)) { printf("[FIOTEST] FAIL feof\n"); fails++; }

    /* raw-fd POSIX layer: read, lseek rewind, stat */
    int fd = open("/fiotest.txt", O_RDONLY);
    if (fd < 0) { printf("[FIOTEST] FAIL open\n"); fails++; }
    else {
        char b1[16];
        if (read(fd, b1, 9) != 9 || memcmp(b1, "line one\n", 9) != 0) {
            printf("[FIOTEST] FAIL read\n"); fails++;
        }
        if (lseek(fd, 0, SEEK_SET) != 0) { printf("[FIOTEST] FAIL lseek\n"); fails++; }
        if (read(fd, b1, 9) != 9 || memcmp(b1, "line one\n", 9) != 0) {
            printf("[FIOTEST] FAIL read after rewind\n"); fails++;
        }
        struct stat st;
        if (stat("/fiotest.txt", &st) != 0 || st.st_size != 32 || !S_ISREG(st.st_mode)) {
            printf("[FIOTEST] FAIL stat (%lu)\n", st.st_size); fails++;
        }
        close(fd);
    }

    if (remove("/fiotest.txt") != 0) { printf("[FIOTEST] FAIL remove\n"); fails++; }
    if (fclose(f) != 0) { printf("[FIOTEST] FAIL fclose(r)\n"); fails++; }

    if (fails == 0) printf("[FIOTEST] ALL PASS\n");
    else printf("[FIOTEST] %d FAILURES\n", fails);
    return fails ? 1 : 0;
}