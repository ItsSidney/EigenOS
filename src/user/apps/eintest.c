/* eintest.c — Eina (EFL) freestanding smoke test for EigenOS.
 * Exercises the core Eina containers/logging we ported. Run from the
 * terminal app in the VM; all output goes to the serial console. */

#include <stdio.h>
#include <string.h>
#include "userlib.h"
#include "eina_config.h"
#include "eina_types.h"
#include "eina_error.h"
#include "eina_main.h"
#include "eina_stringshare.h"
#include "eina_array.h"
#include "eina_list.h"
#include "eina_log.h"

static int fails = 0;
#define CHECK(cond, msg) do {                                            \
        if (cond) printf("  [PASS] " msg "\n");                         \
        else { printf("  [FAIL] " msg "\n"); fails++; }                 \
    } while (0)

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

    eigen_puts("[EINTEST] START\n");

    if (!eina_init()) {
        eigen_puts("[EINTEST] eina_init() FAILED\n");
        printf("[EINTEST] eina_init() FAILED\n");
        return 1;
    }
    eigen_puts("[EINTEST] eina_init() OK\n");
    printf("[EINTEST] eina_init() OK\n");

    /* ---- stringshare ---- */
    const char* s1 = eina_stringshare_add("hello-eigen");
    const char* s2 = eina_stringshare_add("hello-eigen");
    CHECK(s1 != NULL && s1 == s2, "stringshare dedups identical strings");
    const char* r = eina_stringshare_ref(s1);
    CHECK(r == s1, "stringshare_ref returns same pointer");
    eina_stringshare_del(s1);
    eina_stringshare_del(s2);

    /* ---- array ---- */
    Eina_Array* arr = eina_array_new(4);
    CHECK(arr != NULL, "eina_array_new");
    eina_array_push(arr, (void*)"a");
    eina_array_push(arr, (void*)"b");
    eina_array_push(arr, (void*)"c");
    CHECK(eina_array_count(arr) == 3, "eina_array_count == 3");
    CHECK(strcmp((const char*)eina_array_data_get(arr, 1), "b") == 0,
          "eina_array_data_get index 1 == 'b'");
    eina_array_free(arr);

    /* ---- list ---- */
    Eina_List* list = NULL;
    list = eina_list_append(list, (void*)"x");
    list = eina_list_append(list, (void*)"y");
    list = eina_list_append(list, (void*)"z");
    CHECK(eina_list_count(list) == 3, "eina_list_count == 3");
    eina_list_free(list);

    eina_shutdown();

    if (fails == 0) {
        eigen_puts("[EINTEST] ALL PASS\n");
        printf("[EINTEST] ALL PASS (%d fails)\n", fails);
        return 0;
    }
    eigen_puts("[EINTEST] SOME FAILS\n");
    printf("[EINTEST] %d FAIL(S)\n", fails);
    return 1;
}
