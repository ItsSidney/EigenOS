/* eet_lib.c — Eet core file I/O for EigenOS (freestanding ring-3 port).
 *
 * This is a from-scratch implementation of the Eet file-container layer,
 * replacing upstream's Eina_File + stdio based I/O (which is unavailable in
 * this environment). It uses the EigenOS VFS helpers (eigen_fs_*) for storage
 * and Emile (zlib) for compression. The serialization layer (eet_data.c,
 * eet_dictionary.c, eet_node.c, eet_alloc.c) is upstream and calls the raw
 * Eet_File API implemented here.
 *
 * On-disk container format (little-endian), magic 0x1EE7F00D:
 *   uint32 magic
 *   uint32 entry_count
 *   repeated entry_count times:
 *     uint32 name_len
 *     uint8  name[name_len]
 *     uint32 orig_size
 *     uint32 comp_size
 *     uint8  comp         (0 = stored, 1 = zlib)
 *     uint8  data[comp_size]
 */

#include <stdlib.h>
#include <string.h>

#include <Eina.h>
#include <Emile.h>

#include "Eet.h"
#include "Eet_private.h"

/* ---- EigenOS VFS (declared in userlib; we avoid pulling the whole header) ---- */
extern long  eigen_fs_size(const char *path);
extern int   eigen_fs_read_file(const char *path, void *buf, int maxlen);
extern int   eigen_fs_write_file(const char *path, const void *data, int len);
extern int   eigen_fs_exists(const char *path);
extern int   eigen_fs_delete(const char *path);
extern int   eigen_fs_create(const char *path);

/* ---- minimal math the serializer needs (no libm in freestanding) ---- */
double ldexp(double x, int exp) {
    if (exp == 0) return x;
    double f = (exp > 0) ? 2.0 : 0.5;
    int n = (exp > 0) ? exp : -exp;
    for (int i = 0; i < n; i++) x *= f;
    return x;
}

float ldexpf(float x, int exp) {
    if (exp == 0) return x;
    float f = (exp > 0) ? 2.0f : 0.5f;
    int n = (exp > 0) ? exp : -exp;
    for (int i = 0; i < n; i++) x *= f;
    return x;
}

/* ---- in-memory entry ---- */
typedef struct _Eet_LocEnt {
    char  *name;
    void  *data;     /* decompressed payload (owned) */
    int    size;     /* decompressed size */
} Eet_LocEnt;

static int _eet_init_count = 0;

/* ── little-endian helpers ───────────────────────────────────────────────── */
static void put32(unsigned char **p, uint32_t v) {
    unsigned char *q = *p;
    q[0] = v & 0xFF; q[1] = (v >> 8) & 0xFF;
    q[2] = (v >> 16) & 0xFF; q[3] = (v >> 24) & 0xFF;
    *p = q + 4;
}
static uint32_t get32(const unsigned char **p) {
    const unsigned char *q = *p;
    uint32_t v = (uint32_t)q[0] | ((uint32_t)q[1] << 8) |
                 ((uint32_t)q[2] << 16) | ((uint32_t)q[3] << 24);
    *p = q + 4;
    return v;
}

/* ── hash helpers expected by eet_data.c ─────────────────────────────────── */
int
_eet_hash_gen_len(const char *key, int hash_size, int *len_ret) {
    int i, num = 0;
    int len = (int)strlen(key);
    if (len_ret) *len_ret = len;
    for (i = 0; i < len; i++)
      num += (int)(unsigned char)key[i] * (i + 1);
    return num % hash_size;
}

int
_eet_hash_gen(const char *key, int hash_size) {
    return _eet_hash_gen_len(key, hash_size, NULL);
}

/* ── identity stubs (no crypto backend) ──────────────────────────────────── */
Eet_Key *
eet_identity_open(const char *certificate_file,
                  const char *private_key_file,
                  Eet_Key_Password_Callback cb) {
    (void)certificate_file; (void)private_key_file; (void)cb;
    return NULL;
}
void eet_identity_unref(Eet_Key *key) { (void)key; }
void eet_identity_ref(Eet_Key *key) { (void)key; }
const void *
eet_identity_check(const void *data_base, unsigned int data_length,
                   void **sha1, int *sha1_length,
                   const void *signature_base, unsigned int signature_length,
                   const void **raw_signature_base, unsigned int *raw_signature_length,
                   int *x509_length) {
    (void)data_base; (void)data_length; (void)sha1; (void)sha1_length;
    (void)signature_base; (void)signature_length; (void)raw_signature_base;
    (void)raw_signature_length; (void)x509_length;
    return NULL;
}
void *
eet_identity_compute_sha1(const void *data_base, unsigned int data_length,
                           int *sha1_length) {
    (void)data_base; (void)data_length; (void)sha1_length;
    return NULL;
}
Eet_Error
eet_identity_sign(FILE *fp, Eet_Key *key) {
    (void)fp; (void)key;
    return EET_ERROR_NONE;
}

/* ---- logging domain (referenced by the serializer via Eet_private.h) ---- */
int _eet_log_dom_global = -1;

/* ---- dictionary handle (our container has no shared dictionary) ---- */
Eet_Dictionary *
eet_dictionary_get(Eet_File *ef) {
    (void)ef;
    return NULL;
}

/* ---- cipher stubs (no crypto backend available) ---- */
Eet_Error
eet_cipher(const void *data, unsigned int size, const char *key,
           unsigned int length, void **result, unsigned int *result_length) {
    (void)data; (void)size; (void)key; (void)length; (void)result; (void)result_length;
    return EET_ERROR_NOT_IMPLEMENTED;
}
Eet_Error
eet_decipher(const void *data, unsigned int size, const char *key,
             unsigned int length, void **result, unsigned int *result_length) {
    (void)data; (void)size; (void)key; (void)length; (void)result; (void)result_length;
    return EET_ERROR_NOT_IMPLEMENTED;
}

/* ---- xattr stubs (Eina_xattr excluded from our Eina build) ---- */
void *
eina_xattr_get(const char *file, const char *attribute, ssize_t *size) {
    (void)file; (void)attribute;
    if (size) *size = 0;
    return NULL;
}
Eina_Bool
eina_xattr_set(const char *file, const char *attribute,
               const void *data, ssize_t length, Eina_Xattr_Flags flags) {
    (void)file; (void)attribute; (void)data; (void)length; (void)flags;
    return EINA_FALSE;
}

/* ── entry management ───────────────────────────────────────────────────── */
static Eet_LocEnt *
_eet_entry_find(Eet_File *ef, const char *name) {
    Eet_LocEnt *e;
    Eina_List *l;
    EINA_LIST_FOREACH(ef->entries, l, e)
      if (strcmp(e->name, name) == 0) return e;
    return NULL;
}

static void
_eet_entry_free(Eet_LocEnt *e) {
    if (!e) return;
    free(e->name);
    free(e->data);
    free(e);
}

static void
_eet_entries_clear(Eet_File *ef) {
    Eet_LocEnt *e;
    Eina_List *l, *n;
    EINA_LIST_FOREACH_SAFE(ef->entries, l, n, e) {
        ef->entries = eina_list_remove_list(ef->entries, l);
        _eet_entry_free(e);
    }
}

/* ── parsing / serializing the container ─────────────────────────────────── */
static int
_eet_parse(Eet_File *ef, const unsigned char *buf, int len) {
    const unsigned char *p = buf;
    if (len < 8) return -1;
    uint32_t magic = get32(&p);
    if (magic != 0x1EE7F00D) return -1;
    uint32_t count = get32(&p);
    for (uint32_t i = 0; i < count; i++) {
        if (p + 13 > buf + len) return -1;
        uint32_t name_len = get32(&p);
        if (p + name_len + 9 > buf + len) return -1;
        char *name = malloc(name_len + 1);
        if (!name) return -1;
        memcpy(name, p, name_len);
        name[name_len] = 0;
        p += name_len;
        uint32_t orig = get32(&p);
        uint32_t comp_size = get32(&p);
        uint8_t  comp = *p++;
        if (p + comp_size > buf + len) { free(name); return -1; }

        void *data = NULL;
        int   size = (int)orig;
        if (comp == 1) {
            Eina_Binbuf *in = eina_binbuf_new();
            eina_binbuf_append_length(in, (const unsigned char *)p, (size_t)comp_size);
            Eina_Binbuf *out = emile_decompress(in, EMILE_ZLIB, (unsigned int)orig);
            eina_binbuf_free(in);
            if (!out) { free(name); return -1; }
            size = (int)eina_binbuf_length_get(out);
            data = malloc(size ? size : 1);
            if (!data) { free(name); eina_binbuf_free(out); return -1; }
            memcpy(data, eina_binbuf_string_get(out), size);
            void *raw = eina_binbuf_release(out);
            free(raw);
            eina_binbuf_free(out);
        } else {
            data = malloc(orig ? orig : 1);
            if (!data) { free(name); return -1; }
            memcpy(data, p, orig);
        }
        p += comp_size;

        Eet_LocEnt *e = malloc(sizeof(Eet_LocEnt));
        if (!e) { free(name); free(data); return -1; }
        e->name = name; e->data = data; e->size = size;
        ef->entries = eina_list_append(ef->entries, e);
    }
    return 0;
}

static int
_eet_serialize(Eet_File *ef, unsigned char **out, int *out_len) {
    /* first pass: compress each entry to learn sizes */
    Eina_List *l;
    Eet_LocEnt *e;
    int total = 8;
    Eina_List *cl = NULL;
    EINA_LIST_FOREACH(ef->entries, l, e) {
        Eina_Binbuf *raw = eina_binbuf_new();
        eina_binbuf_append_length(raw, (const unsigned char *)e->data, (size_t)e->size);
        Eina_Binbuf *cb = emile_compress(raw, EMILE_ZLIB, EMILE_COMPRESSOR_BEST);
        eina_binbuf_free(raw);
        if (!cb) { /* fall back to stored */
            cb = eina_binbuf_new();
            eina_binbuf_append_length(cb, (const unsigned char *)e->data, (size_t)e->size);
        }
        cl = eina_list_append(cl, cb);
        total += 4 + (int)strlen(e->name);
        total += 4 + 4 + 1 + (int)eina_binbuf_length_get(cb);
    }

    unsigned char *buf = malloc(total ? total : 1);
    if (!buf) { eina_list_free(cl); return -1; }
    unsigned char *q = buf;
    put32(&q, 0x1EE7F00D);
    put32(&q, eina_list_count(ef->entries));

    int idx = 0;
    EINA_LIST_FOREACH(ef->entries, l, e) {
        Eina_Binbuf *cb = eina_list_nth(cl, idx++);
        int comp_size = (int)eina_binbuf_length_get(cb);
        int nl = (int)strlen(e->name);
        put32(&q, (uint32_t)nl);
        memcpy(q, e->name, nl); q += nl;
        put32(&q, (uint32_t)e->size);
        put32(&q, (uint32_t)comp_size);
        *q++ = 1; /* zlib compressed */
        memcpy(q, eina_binbuf_string_get(cb), comp_size);
        q += comp_size;
        void *raw = eina_binbuf_release(cb);
        free(raw);
        eina_binbuf_free(cb);
    }
    eina_list_free(cl);

    *out = buf; *out_len = (int)(q - buf);
    return 0;
}

/* ── public API ─────────────────────────────────────────────────────────── */
EAPI int
eet_init(void) {
    if (++_eet_init_count != 1) return _eet_init_count;
    emile_init();
    eet_mempool_init();
    eet_node_init();
    return _eet_init_count;
}

EAPI int
eet_shutdown(void) {
    if (--_eet_init_count != 0) return _eet_init_count;
    eet_node_shutdown();
    eet_mempool_shutdown();
    emile_shutdown();
    return _eet_init_count;
}

EAPI Eet_File_Mode
eet_mode_get(Eet_File *ef) {
    return ef ? ef->mode : EET_FILE_MODE_INVALID;
}

EAPI Eet_File *
eet_open(const char *file, Eet_File_Mode mode) {
    Eet_File *ef = calloc(1, sizeof(Eet_File));
    if (!ef) return NULL;
    ef->path = malloc(strlen(file) + 1);
    if (!ef->path) { free(ef); return NULL; }
    strcpy(ef->path, file);
    ef->mode = mode;
    ef->references = 1;
    ef->entries = NULL;

    if ((mode == EET_FILE_MODE_READ || mode == EET_FILE_MODE_READ_WRITE)
        && eigen_fs_exists(file) > 0) {
        long sz = eigen_fs_size(file);
        if (sz > 0) {
            unsigned char *buf = malloc((size_t)sz);
            if (buf) {
                int r = eigen_fs_read_file(file, buf, (int)sz);
                if (r > 0) _eet_parse(ef, buf, r);
                free(buf);
            }
        }
    }
    return ef;
}

EAPI Eet_File *
eet_memopen_read(const void *data, size_t size) {
    Eet_File *ef = calloc(1, sizeof(Eet_File));
    if (!ef) return NULL;
    ef->path = NULL;
    ef->mode = EET_FILE_MODE_READ;
    ef->references = 1;
    ef->entries = NULL;
    if (size > 0) _eet_parse(ef, (const unsigned char *)data, (int)size);
    return ef;
}

EAPI Eet_File *
eet_mmap(const Eina_File *file) {
    (void)file;
    return NULL;
}

EAPI Eet_Error
eet_sync(Eet_File *ef) {
    if (!ef) return EET_ERROR_BAD_OBJECT;
    if (ef->mode == EET_FILE_MODE_READ) return EET_ERROR_NOT_WRITABLE;

    unsigned char *buf = NULL;
    int len = 0;
    if (_eet_serialize(ef, &buf, &len) != 0) return EET_ERROR_WRITE_ERROR;
    if (!buf) return EET_ERROR_WRITE_ERROR;

    int r = eigen_fs_write_file(ef->path, buf, len);
    free(buf);
    if (r < 0) return EET_ERROR_WRITE_ERROR;
    return EET_ERROR_NONE;
}

EAPI Eet_Error
eet_close(Eet_File *ef) {
    if (!ef) return EET_ERROR_BAD_OBJECT;
    if (ef->references > 0) ef->references--;
    if (ef->references > 0) return EET_ERROR_NONE;

    if (ef->mode != EET_FILE_MODE_READ)
      eet_sync(ef);

    _eet_entries_clear(ef);
    free(ef->path);
    free(ef);
    return EET_ERROR_NONE;
}

EAPI void *
eet_read(Eet_File *ef, const char *name, int *size_ret) {
    if (!ef || !name) return NULL;
    Eet_LocEnt *e = _eet_entry_find(ef, name);
    if (!e) return NULL;
    void *out = malloc(e->size ? e->size : 1);
    if (!out) return NULL;
    memcpy(out, e->data, e->size);
    if (size_ret) *size_ret = e->size;
    return out;
}

EAPI int
eet_write(Eet_File *ef, const char *name, const void *data, int size, int compress) {
    (void)compress;
    if (!ef || !name || !data || size < 0) return 0;
    if (ef->mode == EET_FILE_MODE_READ) return 0;

    Eet_LocEnt *e = _eet_entry_find(ef, name);
    if (e) {
        free(e->data);
        e->data = malloc(size ? size : 1);
        if (!e->data) return 0;
        memcpy(e->data, data, size);
        e->size = size;
        return 1;
    }
    e = malloc(sizeof(Eet_LocEnt));
    if (!e) return 0;
    e->name = malloc(strlen(name) + 1);
    e->data = malloc(size ? size : 1);
    if (!e->name || !e->data) {
        free(e->name); free(e->data); free(e); return 0;
    }
    strcpy(e->name, name);
    memcpy(e->data, data, size);
    e->size = size;
    ef->entries = eina_list_append(ef->entries, e);
    return 1;
}

EAPI int
eet_delete(Eet_File *ef, const char *name) {
    if (!ef || !name) return 0;
    Eet_LocEnt *e = _eet_entry_find(ef, name);
    if (!e) return 0;
    ef->entries = eina_list_remove(ef->entries, e);
    _eet_entry_free(e);
    return 1;
}

EAPI char **
eet_list(Eet_File *ef, const char *glob, int *count_ret) {
    (void)glob;
    if (!ef) { if (count_ret) *count_ret = 0; return NULL; }
    int n = eina_list_count(ef->entries);
    char **names = malloc((n + 1) * sizeof(char *));
    if (!names) { if (count_ret) *count_ret = 0; return NULL; }
    Eet_LocEnt *e;
    Eina_List *l;
    int i = 0;
    EINA_LIST_FOREACH(ef->entries, l, e)
      names[i++] = strdup(e->name);
    names[i] = NULL;
    if (count_ret) *count_ret = n;
    return names;
}

EAPI Eina_Iterator *
eet_list_entries(Eet_File *ef) {
    (void)ef;
    return NULL;
}

EAPI void
eet_clearcache(void) { }

EAPI const void *
eet_read_direct(Eet_File *ef, const char *name, int *size_ret) {
    return (const void *)eet_read(ef, name, size_ret);
}

EAPI void *
eet_read_cipher(Eet_File *ef, const char *name, int *size_ret, const char *cipher_key) {
    (void)cipher_key;
    return eet_read(ef, name, size_ret);
}

EAPI int
eet_write_cipher(Eet_File *ef, const char *name, const void *data, int size,
                 int compress, const char *cipher_key) {
    (void)cipher_key;
    return eet_write(ef, name, data, size, compress);
}
