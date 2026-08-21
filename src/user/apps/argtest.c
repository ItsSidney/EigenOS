/* argtest.c — ring-3 argv self-test: prints argc + each argument to the
 * terminal. Launch it from the shell:  spawn argtest one two three
 * Expect: [ARGTEST] argc=4, argv[0]=argtest argv[1]=one argv[2]=two
 * argv[3]=three (POSIX: argv[0] is the program name). */
#include <stdio.h>
#include <string.h>

int main(int argc, char** argv) {
    printf("[ARGTEST] argc=%d\n", argc);
    for (int i = 0; i < argc; i++)
        printf("[ARGTEST] argv[%d]=\"%s\"\n", i, argv[i]);
    if (argc >= 4 &&
        strcmp(argv[0], "argtest") == 0 &&
        strcmp(argv[1], "one") == 0 &&
        strcmp(argv[2], "two") == 0 &&
        strcmp(argv[3], "three") == 0)
        printf("[ARGTEST] PASS\n");
    else
        printf("[ARGTEST] FAIL\n");
    return 0;
}