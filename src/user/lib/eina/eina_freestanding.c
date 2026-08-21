/* eina_freestanding.c — stub implementations of Eina subsystems that are
 * excluded from the freestanding EigenOS build (eina_module.c, eina_debug.c,
 * eina_evlog.c, eina_benchmark.c, eina_file_posix.c). Those modules need
 * dynamic loading / real filesystems / host debug facilities we do not have
 * in ring-3. The symbols are still referenced via eina_main.c's subsystem
 * table, so we provide no-op stand-ins here. None of these are exercised by
 * the smoke test; they only need to resolve at link time. */

#include "eina_config.h"
#include "eina_types.h"
#include "eina_array.h"
#include "eina_hash.h"
#include "eina_module.h"
#include "eina_mempool.h"
#include "eina_file.h"
#include "eina_file_common.h"

#include <stdlib.h>
#include <stdarg.h>

/* ---- Module subsystem (no dynamic loading in the freestanding port) ---- */
Eina_Bool eina_module_init(void) { return EINA_TRUE; }
Eina_Bool eina_module_shutdown(void) { return EINA_TRUE; }
Eina_Array *eina_module_arch_list_get(Eina_Array *array,
                                       const char *path,
                                       const char *arch) {
    (void)path; (void)arch;
    /* eina_mempool_init() bails out if this returns NULL, so hand back a
     * dummy, never-iterated array to let the static backend path proceed. */
    if (!array) array = eina_array_new(4);
    return array;
}
char *eina_module_symbol_path_get(const void *symbol, const char *sub_dir) {
    (void)symbol; (void)sub_dir; return NULL;
}
void eina_module_list_load(Eina_Array *array) { (void)array; }
void eina_module_list_free(Eina_Array *array) { (void)array; }

/* ---- Debug subsystem (stubbed) ---- */
Eina_Bool eina_debug_init(void) { return EINA_TRUE; }
Eina_Bool eina_debug_shutdown(void) { return EINA_TRUE; }
void _eina_debug_thread_add(void *th) { (void)th; }
void _eina_debug_thread_del(void *th) { (void)th; }

/* ---- Evlog subsystem (stubbed no-op) ---- */
Eina_Bool eina_evlog_init(void) { return EINA_TRUE; }
Eina_Bool eina_evlog_shutdown(void) { return EINA_TRUE; }
void eina_evlog(const char *event, void *obj, double srctime, const char *detail) {
    (void)event; (void)obj; (void)srctime; (void)detail;
}

/* ---- Benchmark subsystem (stubbed) ---- */
Eina_Bool eina_benchmark_init(void) { return EINA_TRUE; }
Eina_Bool eina_benchmark_shutdown(void) { return EINA_TRUE; }

/* ---- File subsystem POSIX backend (stubbed — no real FS mapping) ---- */
int _eina_file_log_dom = 0;

Eina_File *eina_file_open(const char *name, Eina_Bool shared) {
    (void)name; (void)shared; return NULL;
}
void *eina_file_map_all(Eina_File *file, Eina_File_Populate rule) {
    (void)file; (void)rule; return NULL;
}
void eina_file_map_free(Eina_File *file, void *map) {
    (void)file; (void)map;
}
void eina_file_real_close(Eina_File *file) { (void)file; }
Eina_Bool eina_xattr_fd_copy(int src, int dst) {
    (void)src; (void)dst; return EINA_FALSE;
}
int eina_file_mkstemp(const char *templatename, Eina_Tmpstr **path) {
    (void)templatename; (void)path; return -1;
}
Eina_Bool eina_file_path_relative(const char *path) {
    (void)path; return EINA_FALSE;
}
Eina_Tmpstr *eina_file_current_directory_get(const char *path, size_t len) {
    (void)path; (void)len; return NULL;
}

/* ---- Mempool static "pass_through" backend ----
 * The freestanding port ships no separate mempool module .so files, so we
 * register a malloc-backed backend directly (this is exactly what the real
 * "pass_through" backend does). Registered under every historical backend
 * name so any eina_mempool_add("chained_pool"|"one_big"|...) caller resolves
 * to a working allocator. */
#ifdef EINA_STATIC_BUILD_PASS_THROUGH
static void *_pt_init(const char *context, const char *options, va_list args) {
    (void)context; (void)options; (void)args;
    return (void *)1;
}
static void _pt_free(void *data, void *element) { (void)data; free(element); }
static void *_pt_alloc(void *data, unsigned int size) {
    (void)data; return malloc(size ? size : 1);
}
static void *_pt_realloc(void *data, void *element, unsigned int size) {
    (void)data; return realloc(element, size ? size : 1);
}
static void _pt_shutdown(void *data) { (void)data; }

Eina_Bool pass_through_init(void) {
    static const char *_pt_names[] = {
        "pass_through", "chained_mempool", "chained_pool",
        "one_big", "buddy", "fixed_bitmap", NULL
    };
    int i;
    for (i = 0; _pt_names[i]; i++) {
        static Eina_Mempool_Backend be;
        be.name             = _pt_names[i];
        be.init             = _pt_init;
        be.free             = _pt_free;
        be.alloc            = _pt_alloc;
        be.realloc          = _pt_realloc;
        be.garbage_collect  = NULL;
        be.statistics       = NULL;
        be.shutdown         = _pt_shutdown;
        be.repack           = NULL;
        be.from             = NULL;
        be.iterator         = NULL;
        be.alloc_near       = NULL;
        eina_mempool_register(&be);
    }
    return EINA_TRUE;
}
void pass_through_shutdown(void) { /* nothing to tear down */ }
#endif
