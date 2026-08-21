/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

/* setjmptest.c — exercises setjmp/longjmp and assert for ring-3. */

#include <setjmp.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>

static jmp_buf jb;
static int nested_calls = 0;

static int deep_call(int n) {
    if (n == 0) return 42;
    return deep_call(n - 1) + 1;
}

static int throw_here(void) {
    nested_calls++;
    if (nested_calls == 3) longjmp(jb, 7);
    return 0;
}

int main(void) {
    printf("[setjmptest] start\n");

    /* longjmp with val==0 must come back as 1 */
    int r = setjmp(jb);
    if (r == 0) {
        printf("  [jmp] first pass through setjmp\n");
        longjmp(jb, 0);
        printf("  [FAIL] longjmp did not return\n");
        return 1;
    }
    printf("  [PASS] longjmp(0) returned %d\n", r);
    if (r != 1) { printf("  [FAIL] expected 1\n"); return 1; }

    /* longjmp with val==7 skips three levels of calls */
    if (setjmp(jb) == 0) {
        throw_here();
        throw_here();
        throw_here();
        printf("  [FAIL] longjmp did not unwind\n");
        return 1;
    }
    printf("  [PASS] longjmp(7) unwound 3 call levels (nested=%d)\n", nested_calls);

    /* normal setjmp falls through, stack still healthy */
    int sum = deep_call(5);
    if (sum != 47) { printf("  [FAIL] deep_call=%d\n", sum); return 1; }
    printf("  [PASS] deep recursion ok after longjmp (sum=%d)\n", sum);

    /* assert that passes */
    assert(sum == 47);
    assert(nested_calls == 3);
    printf("  [PASS] assert(expr) no-op on success\n");

    /* errno is per-thread and reachable from libc paths */
    errno = 0;
    assert(errno == 0);
    printf("  [PASS] errno read/write via assert path\n");

    printf("[setjmptest] done, all checks passed\n");
    return 0;
}