/***************************************************************/
/* EigenOS filesystem — a small, predictable, Linux-flavoured RAM FS.  */
/*                                                                     */
/* HOW IT IS LAID OUT                                                  */
/*   Everything is a node in files[MAX_FILES]. Slot 0 is always "/".    */
/*   A directory owns its children purely by their parent_dir slot,     */
/*   so slot order doubles as a stable readdir order and the slot       */
/*   number itself serves as the inode number for getdents64.           */
/*                                                                     */
/* PATHS                                                               */
/*   One resolver (fs_resolve_impl) handles absolute and relative       */
/*   paths, "." and "..". Each task carries its own working directory   */
/*   (task_t.cwd_node) so chdir in one process never leaks into         */
/*   another — fork() copies it, execve() keeps it.                     */
/*                                                                     */
/* LAYOUT AT BOOT                                                       */
/*   /bin /user /cfg /etc /tmp /proc /dev/{null,zero,console,tty}       */
/*   /home/eigen/{Desktop,.Trash}  — boot modules land in /user.        */
/*                                                                     */
/* PERSISTENCE                                                          */
/*   persist.c snapshots the node table verbatim; external nodes        */
/*   (capacity==0, e.g. /user/*.elf aliased straight to Limine module   */
/*   memory) are recorded as metadata only, never copied.               */
/*                                                                     */
/* Copyright (C) Sidney 2024-2026. All rights reserved.                 */
/* Written by Sidney.                                                   */
/* Distributed under terms of the GNU General Public License.           */
/***************************************************************/

#include <filesystem/filesystem.h>
#include <string.h>
#include <stdint.h>

#ifndef MAX_FILES
#define MAX_FILES 256
#endif
#ifndef MAX_FILENAME
#define MAX_FILENAME 64
#endif

static file_t files[MAX_FILES];

/* well-known nodes (indices, -1 = unset) */
static int home_dir_index   = -1;
static int trash_dir_index  = -1;
static int desktop_dir_index= -1;
static int bin_dir_index    = -1;

/* ------------------------------------------------------------------ */
/* per-task cwd (kernel tasks)                                         */
/* ------------------------------------------------------------------ */
static int task_cwd(void) {
    /* kernel-side current task; ring0 callers get root */
    extern void* ktask_current(void);
    void* t = ktask_current();
    if (!t) return 0;
    extern int ktask_cwd_node(void* t);
    return ktask_cwd_node(t);
}

/* ------------------------------------------------------------------ */
/* primitives                                                          */
/* ------------------------------------------------------------------ */
file_t* fs_table_entry(int i) {
    if (i < 0 || i >= MAX_FILES) return 0;
    return &files[i];
}

static int file_owns_data(const file_t* f);   /* owns heap buffer iff cap>0 */

static int find_free_slot(void) {
    for (int i = 1; i < MAX_FILES; i++)          /* 0 = root, never freed */
        if (!files[i].in_use) return i;
    return -1;
}

/* first child of `dir` at-or-after `from` (stable slot order) */
static int find_in_dir(int dir, const char* name) {
    if (dir < 0) dir = 0;
    for (int i = 1; i < MAX_FILES; i++) {
        if (!files[i].in_use) continue;
        if (files[i].parent_dir != dir) continue;
        if (strncmp(files[i].name, name, MAX_FILENAME) == 0) return i;
    }
    return -1;
}

static int nth_child(int dir, int nth) {
    if (dir < 0) dir = 0;
    int seen = 0;
    for (int i = 1; i < MAX_FILES; i++) {
        if (!files[i].in_use || files[i].parent_dir != dir) continue;
        if (seen++ == nth) return i;
    }
    return -1;
}

static void now_stamp(file_t* f) {
    extern unsigned long long timer_get_ms(void);
    uint32_t t = (uint32_t)timer_get_ms();
    f->creation_time = f->modified_time = t;
}

/* tiny bounded concat/copy helpers (kernel has no libc here) */
static int sn_copy(char* dst, int cap, const char* src) {
    int i = 0;
    while (src[i] && i < cap-1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
    return i;
}
static int sn_append_int(char* dst, int cap, const char* a, int v) {
    int i = sn_copy(dst, cap, a);
    char num[8]; int ni = 0;
    if (v == 0) num[ni++] = '0';
    while (v > 0 && ni < 7) { num[ni++] = '0' + v % 10; v /= 10; }
    for (int j = ni-1; j >= 0 && i < cap-1; j--) dst[i++] = num[j];
    dst[i] = 0;
    return i;
}

static int create_entry(const char* name, fs_type_t type, int parent, uint8_t flags) {
    if (!name || !name[0]) return -1;
    if (find_in_dir(parent, name) >= 0) return -1;      /* EEXIST */
    int idx = find_free_slot();
    if (idx < 0) return -1;
    file_t* f = &files[idx];
    memset(f, 0, sizeof(*f));
    strncpy(f->name, name, MAX_FILENAME - 1);
    f->type = type;
    f->parent_dir = parent < 0 ? 0 : parent;
    f->flags = flags;
    f->in_use = 1;
    now_stamp(f);
    return idx;
}

int ensure_dir_chain(const char* path) {                /* "a/b/c" from root */
    int dir = 0;
    char comp[MAX_FILENAME];
    const char* p = path;
    while (*p) {
        int ci = 0;
        while (*p && *p != '/' && ci < MAX_FILENAME-1) comp[ci++] = *p++;
        comp[ci] = 0;
        if (*p == '/') p++;
        if (!comp[0]) continue;
        int e = find_in_dir(dir, comp);
        if (e < 0) e = create_entry(comp, FS_DIRECTORY, dir, 0);
        if (e < 0) return -1;
        dir = e;
    }
    return dir;
}

/* ------------------------------------------------------------------ */
/* THE path resolver — every public API goes through here.            */
/*                                                                    */
/* fs_resolve_impl(path, cwd):                                             */
/*   returns node index of the FINAL component, or -1 (ENOENT).       */
/* fs_resolve_parent(path, cwd, out_leaf):                            */
/*   returns parent dir index, copies final component into out_leaf.  */
/* ------------------------------------------------------------------ */
static int walk(int dir, const char* p, int* out_is_final_dir,
                char* leaf, int leaf_cap) {
    /* walk components; caller pre-set start dir */
    int last = dir;
    char comp[MAX_FILENAME];
    if (leaf && leaf_cap > 0) leaf[0] = 0;

    while (*p) {
        int ci = 0;
        while (*p && *p != '/') {
            if (ci < MAX_FILENAME-1) comp[ci++] = *p++;
            else p++;
        }
        comp[ci] = 0;
        if (*p == '/') p++;
        if (!comp[0] || strcmp(comp, ".") == 0) continue;

        if (strcmp(comp, "..") == 0) {
            dir = files[dir].parent_dir;
            if (dir < 0) dir = 0;
            last = dir;
            continue;
        }

        int e = find_in_dir(dir, comp);
        if (e < 0) {
            /* not found: valid only as the FINAL component */
            while (*p) {                       /* more components => ENOENT */
                if (*p == '/') return -1;
                p++;
            }
            if (leaf) {
                int j = 0;
                while (comp[j] && j < leaf_cap-1) { leaf[j] = comp[j]; j++; }
                leaf[j] = 0;
            }
            *out_is_final_dir = 0;
            return dir;                        /* parent + missing leaf */
        }
        dir = e;
        last = dir;
    }
    if (leaf && leaf_cap > 0) leaf[0] = 0;
    *out_is_final_dir = 1;
    return dir;
}

static int fs_resolve_impl(const char* path) {
    if (!path || !path[0]) return 0;
    int start = (path[0] == '/') ? 0 : task_cwd();
    int isfinal = 0;
    char leaf[MAX_FILENAME];                    /* detect missing-last-comp */
    int r = walk(start, path, &isfinal, leaf, sizeof(leaf));
    if (!isfinal) return -1;                    /* final component absent */
    return r;
}


/* exported for kernel callers (sys_stat/access) */
int fs_resolve_path(const char* path) { return fs_resolve_impl(path); }

static int fs_resolve_parent(const char* path, char* leaf, int leaf_cap) {
    if (!path || !path[0]) return -1;
    int start = (path[0] == '/') ? 0 : task_cwd();
    int isfinal = 0;
    return walk(start, path, &isfinal, leaf, leaf_cap);
}

/* ------------------------------------------------------------------ */
/* getters                                                             */
/* ------------------------------------------------------------------ */
int fs_get_current_dir(void)              { return task_cwd(); }
int fs_get_home_dir(void)                 { return home_dir_index; }
int fs_get_trash_dir(void)                { return trash_dir_index; }
int fs_get_desktop_dir(void)              { return desktop_dir_index; }
int fs_get_bin_dir(void)                  { return bin_dir_index; }

int fs_get_free_count(void) {
    int n = 0;
    for (int i = 1; i < MAX_FILES; i++) if (!files[i].in_use) n++;
    return n;
}

int fs_get_node(int idx, char* name, int* size, int* type, int* parent,
                uint8_t* flags, uint32_t* mod_time) {
    if (idx < 0 || idx >= MAX_FILES || !files[idx].in_use) return -1;
    file_t* f = &files[idx];
    if (name)     strncpy(name, f->name, MAX_FILENAME);
    if (size)     *size = f->size;
    if (type)     *type = (int)f->type;
    if (parent)   *parent = f->parent_dir;
    if (flags)    *flags = f->flags;
    if (mod_time) *mod_time = f->modified_time;
    return 0;
}

/* children of `dir_idx`, in slot order */
int fs_find_by_index(int dir_idx, int nth, char* name, int* size, int* type,
                     uint8_t* flags, uint32_t* mod_time) {
    int c = nth_child(dir_idx, nth);
    if (c < 0) return -1;
    return fs_get_node(c, name, size, type, 0, flags, mod_time);
}

int fs_get_dir_count(int dir_idx) {
    int n = 0;
    for (int i = 1; i < MAX_FILES; i++)
        if (files[i].in_use && files[i].parent_dir == dir_idx) n++;
    return n;
}

int fs_is_directory_empty(int dir_idx) {
    return fs_get_dir_count(dir_idx) == 0;
}

int fs_find_child(int dir_idx, int child_idx) {
    if (child_idx < 0 || child_idx >= MAX_FILES) return -1;
    if (!files[child_idx].in_use) return -1;
    return files[child_idx].parent_dir == dir_idx ? child_idx : -1;
}

/* ------------------------------------------------------------------ */
/* fd-less metadata ops                                                */
/* ------------------------------------------------------------------ */
int fs_exists(const char* filename) { return fs_resolve_impl(filename) >= 0; }

int fs_mkdir(const char* path) {
    char leaf[MAX_FILENAME];
    int parent = fs_resolve_parent(path, leaf, sizeof(leaf));
    if (parent < 0 || !leaf[0]) return -1;
    return create_entry(leaf, FS_DIRECTORY, parent, 0);
}

int fs_create(const char* filename) {
    /* exists already? open-style success keeps legacy callers happy */
    int e = fs_resolve_impl(filename);
    if (e >= 0) return e;
    char leaf[MAX_FILENAME];
    int parent = fs_resolve_parent(filename, leaf, sizeof(leaf));
    if (parent < 0 || !leaf[0]) return -1;
    uint8_t fl = 0;
    int ll = sn_copy((char*)leaf, MAX_FILENAME, leaf); (void)ll;
    if (ll >= 4 && leaf[ll-4]=='.' && leaf[ll-3]=='e' &&
        leaf[ll-2]=='l' && leaf[ll-1]=='f')
        fl = FS_FLAG_EXECUTABLE;
    return create_entry(leaf, FS_FILE, parent, fl);
}

int fs_touch(const char* filename) {
    int e = fs_resolve_impl(filename);
    if (e >= 0) { now_stamp(&files[e]); return e; }
    return fs_create(filename);
}

/* recursive delete helper: depth-first so children vanish first */
static int delete_recursive(int idx) {
    if (idx <= 0 || !files[idx].in_use) return -1;
    /* delete children first (dirs may nest) */
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 1; i < MAX_FILES; i++) {
            if (!files[i].in_use) continue;
            if (files[i].parent_dir != idx) continue;
            if (files[i].type == FS_DIRECTORY) delete_recursive(i);
            else if (pass == 1) {
                if (files[i].data && file_owns_data(&files[i])) {
                    extern void kfree(void*); kfree(files[i].data);
                }
                memset(&files[i], 0, sizeof(files[i]));
            }
        }
        if (pass == 0 && files[idx].type != FS_DIRECTORY) break;
    }
    if (files[idx].data && file_owns_data(&files[idx])) {
        extern void kfree(void*); kfree(files[idx].data);
    }
    memset(&files[idx], 0, sizeof(files[idx]));
    return 0;
}

int fs_delete(const char* filename) {
    int e = fs_resolve_impl(filename);
    if (e <= 0) return -1;                    /* no deleting "/" */
    return delete_recursive(e);
}

int fs_rmdir(const char* path) {
    int e = fs_resolve_impl(path);
    if (e <= 0) return -1;
    if (files[e].type != FS_DIRECTORY) return -1;
    if (!fs_is_directory_empty(e)) return -1; /* rmdir: must be empty */
    memset(&files[e], 0, sizeof(files[e]));
    return 0;
}

int fs_truncate(const char* filename) {
    int e = fs_resolve_impl(filename);
    if (e < 0) return -1;
    file_t* f = &files[e];
    if (f->type != FS_FILE) return -1;
    if (f->data && file_owns_data(f)) { extern void kfree(void*); kfree(f->data); }
    f->data = 0;
    f->size = 0;
    f->capacity = 0;
    now_stamp(f);
    return 0;
}

int fs_rename(const char* oldname, const char* newname) {
    int e = fs_resolve_impl(oldname);
    if (e <= 0) return -1;
    char leaf[MAX_FILENAME];
    int parent = fs_resolve_parent(newname, leaf, sizeof(leaf));
    if (parent < 0 || !leaf[0]) return -1;
    int dst = find_in_dir(parent, leaf);
    if (dst >= 0 && dst != e) return -1;      /* target exists */
    strncpy(files[e].name, leaf, MAX_FILENAME-1);
    files[e].parent_dir = parent;
    now_stamp(&files[e]);
    return 0;
}

int fs_move(const char* src, const char* dst) { return fs_rename(src, dst); }

int fs_copy_file(const char* src_name, const char* dst_name) {
    int s = fs_resolve_impl(src_name);
    if (s < 0 || files[s].type != FS_FILE) return -1;
    int d = fs_resolve_impl(dst_name);
    if (d >= 0 && files[d].type == FS_DIRECTORY) {
        /* copy INTO directory keeping basename */
        d = create_entry(files[s].name, FS_FILE, d, files[s].flags);
    } else {
        char leaf[MAX_FILENAME];
        int parent = fs_resolve_parent(dst_name, leaf, sizeof(leaf));
        if (parent < 0 || !leaf[0]) return -1;
        d = create_entry(leaf, FS_FILE, parent, files[s].flags);
    }
    if (d < 0) return -1;
    file_t* sf = &files[s];
    file_t* df = &files[d];
    if (sf->size > 0 && sf->data) {
        extern void* kmalloc(unsigned);
        df->data = kmalloc((unsigned)sf->size);
        if (!df->data) return -1;
        memcpy(df->data, sf->data, (size_t)sf->size);
        df->size = sf->size;
        df->capacity = sf->size;
    }
    now_stamp(df);
    return 0;
}

int fs_copy_file_to_dir(int src_dir, const char* src_name,
                        int dst_dir, const char* dst_name) {
    char a[MAX_FILENAME*2], b[MAX_FILENAME*2];
    /* build paths via indices' parents: simpler—resolve names directly */
    (void)a; (void)b;
    int s = find_in_dir(src_dir, src_name);
    if (s < 0) return -1;
    int d = create_entry(dst_name && dst_name[0] ? dst_name : src_name,
                         FS_FILE, dst_dir, files[s].flags);
    if (d < 0) return -1;
    if (files[s].size > 0 && files[s].data) {
        extern void* kmalloc(unsigned);
        files[d].data = kmalloc((unsigned)files[s].size);
        if (!files[d].data) return -1;
        memcpy(files[d].data, files[s].data, (size_t)files[s].size);
        files[d].size = files[s].size;
        files[d].capacity = files[s].size;
    }
    now_stamp(&files[d]);
    return 0;
}

/* ------------------------------------------------------------------ */
/* trash helpers                                                       */
/* ------------------------------------------------------------------ */
int fs_trash_file(const char* filename) {
    if (trash_dir_index < 0) return -1;
    int e = fs_resolve_impl(filename);
    if (e <= 0) return -1;
    char leaf[MAX_FILENAME];
    strncpy(leaf, files[e].name, MAX_FILENAME-1);
    leaf[MAX_FILENAME-1] = 0;
    int parent = files[e].parent_dir;
    /* move node into trash (relink), collision → suffix */
    char nm[MAX_FILENAME+8];
    for (int attempt = 0; attempt < 64; attempt++) {
        if (attempt) sn_append_int(nm, (int)sizeof(nm), leaf, attempt);
        else         sn_copy(nm, (int)sizeof(nm), leaf);
        if (find_in_dir(trash_dir_index, nm) < 0) {
            strncpy(files[e].name, nm, MAX_FILENAME-1);
            files[e].parent_dir = trash_dir_index;
            now_stamp(&files[e]);
            (void)parent;
            return 0;
        }
    }
    return -1;
}

int fs_empty_trash(void) {
    if (trash_dir_index < 0) return -1;
    for (int i = 1; i < MAX_FILES; i++) {
        if (files[i].in_use && files[i].parent_dir == trash_dir_index)
            delete_recursive(i);
    }
    return 0;
}

int fs_restore_from_trash(const char* filename) {
    if (trash_dir_index < 0) return -1;
    int e = find_in_dir(trash_dir_index, filename);
    if (e < 0) return -1;
    files[e].parent_dir = home_dir_index >= 0 ? home_dir_index : 0;
    now_stamp(&files[e]);
    return 0;
}

/* ------------------------------------------------------------------ */
/* fd-based I/O (fd = node index + 1, kept for ABI stability)          */
/* ------------------------------------------------------------------ */
static int file_ensure_cap(int fd, int need) {
    file_t* f = fs_table_entry(fd);
    if (!f || !f->in_use) return -1;
    if (need <= f->capacity) return 0;
    /* capacity==0 with data != NULL marks EXTERNAL storage (boot-module
       alias). First write detaches: copy bytes into owned heap memory. */
    int base = f->capacity ? f->capacity : (f->size > 0 ? f->size : 128);
    int cap = base ? base : 128;
    while (cap < need) cap *= 2;
    extern void* kmalloc(unsigned);
    extern void kfree(void*);
    uint8_t* nd = kmalloc((unsigned)cap);
    if (!nd) return -1;
    if (f->data && f->size > 0) memcpy(nd, f->data, (size_t)f->size);
    if (f->capacity > 0 && f->data) kfree(f->data);   /* own only */
    f->data = nd;
    f->capacity = cap;
    return 0;
}

/* node owns its buffer iff capacity > 0 */
static int file_owns_data(const file_t* f) { return f->capacity > 0; }

int fs_open(const char* filename, int flags) {
    (void)flags;
    int e = fs_resolve_impl(filename);
    if (e >= 0) {
        if (files[e].type == FS_DIRECTORY) return e;   /* dirs openable (getdents) */
        return e;
    }
    return -1;
}

int fs_read(int fd, char* buf, int count) {
    return fs_read_at(fd, buf, count, 0);
}

int fs_read_at(int fd, char* buf, int count, int offset) {
    file_t* f = fs_table_entry(fd);
    if (!f || !f->in_use) return -1;
    if (f->type != FS_FILE && f->size == 0) { /* dir opened for getdents */ }
    if (offset < 0) offset = 0;
    if (offset >= f->size) return 0;
    int avail = f->size - offset;
    int n = count < avail ? count : avail;
    if (n > 0 && f->data) memcpy(buf, f->data + offset, (size_t)n);
    return n;
}

int fs_write(int fd, const char* buf, int count) {
    file_t* f = fs_table_entry(fd);
    if (!f || !f->in_use || f->type != FS_FILE) return -1;
    /* writes append (flat-file ABI: no per-fd write cursor) */
    if (file_ensure_cap(fd, f->size + count) != 0) return -1;
    memcpy(f->data + f->size, buf, (size_t)count);
    f->size += count;
    now_stamp(f);
    extern void persist_mark_dirty(void);
    persist_mark_dirty();
    return count;
}

int fs_close(int fd) { (void)fd; return 0; }

int fs_cat(const char* filename, char* output, int max_len) {
    int e = fs_resolve_impl(filename);
    if (e < 0 || files[e].type != FS_FILE) return -1;
    int n = files[e].size < max_len-1 ? files[e].size : max_len-1;
    if (n > 0 && files[e].data) memcpy(output, files[e].data, (size_t)n);
    output[n] = 0;
    return n;
}

/* ------------------------------------------------------------------ */
/* listing / cwd                                                       */
/* ------------------------------------------------------------------ */
int fs_list(char* output, int max_len) {
    int dir = task_cwd();
    int used = 0;
    for (int i = 1; i < MAX_FILES; i++) {
        if (!files[i].in_use || files[i].parent_dir != dir) continue;
        int nl = 0;
        while (files[i].name[nl]) nl++;
        if (used + nl + 2 > max_len) break;
        memcpy(output + used, files[i].name, (size_t)nl);
        used += nl;
        output[used++] = files[i].type == FS_DIRECTORY ? '/' : '\n';
        if (files[i].type == FS_DIRECTORY) output[used++] = '\n';
    }
    output[used] = 0;
    return used;
}

int fs_cd(const char* dirname) {
    if (!dirname || !dirname[0] ||
        (dirname[0] == '/' && dirname[1] == 0)) {
        extern int ktask_set_cwd(int);
        ktask_set_cwd(0);
        return 0;
    }
    int e = fs_resolve_impl(dirname);
    if (e < 0 || files[e].type != FS_DIRECTORY) return -1;
    extern int ktask_set_cwd(int);
    return ktask_set_cwd(e);
}

int fs_pwd(char* output, int max_len) {
    int dir = task_cwd();
    if (dir <= 0) { if (max_len > 1) { output[0]='/'; output[1]=0; } return 1; }
    /* build reversed then flip */
    char tmp[MAX_FILENAME*8];
    int tl = 0;
    while (dir > 0) {
        int nl = 0;
        while (files[dir].name[nl]) nl++;
        if (tl + nl + 1 >= (int)sizeof(tmp)) break;
        for (int j = nl-1; j >= 0; j--) tmp[tl++] = files[dir].name[j];
        tmp[tl++] = '/';
        dir = files[dir].parent_dir;
    }
    int ol = 0;
    output[ol++] = '/';
    for (int i = tl-1; i >= 0 && ol < max_len-1; i--) output[ol++] = tmp[i];
    output[ol] = 0;
    return ol;
}

/* ------------------------------------------------------------------ */
/* misc                                                                */
/* ------------------------------------------------------------------ */
void fs_get_permission_string(uint8_t flags, char* out) {
    out[0] = 'r'; out[1] = 'w';
    out[2] = (flags & FS_FLAG_READONLY) ? '-' : 'x';
    out[3] = 0;
}

void fs_format_time(uint32_t ms, char* out, int max_len) {
    uint32_t s = ms / 1000;
    int len = 0;
    uint32_t v = s; int digits = 1;
    while (v >= 10) { v /= 10; digits++; }
    if (digits >= max_len) digits = max_len - 1;
    v = s;
    for (int i = digits - 1; i >= 0; i--) { out[i] = '0' + (v % 10); v /= 10; }
    len = digits;
    out[len] = 0;
}

int fs_set_flags(const char* filename, uint8_t flags) {
    int e = fs_resolve_impl(filename);
    if (e < 0) return -1;
    files[e].flags = flags;
    return 0;
}

/* ------------------------------------------------------------------ */
/* init                                                                */
/* ------------------------------------------------------------------ */
void init_filesystem(void) {
    extern void serial_puts(const char*);
    serial_puts("[FS] init begin\n");

    memset(files, 0, sizeof(files));
    files[0].in_use = 1;
    files[0].type = FS_DIRECTORY;
    files[0].parent_dir = 0;                   /* "/" is its own parent */
    strncpy(files[0].name, "/", MAX_FILENAME-1);

    /* Restore persisted tree BEFORE seeding defaults (warm boots become
       no-op ensures). */
    {
        extern void persist_load(void);
        persist_load();
    }
    serial_puts("[FS] tree restored\n");

    /* FHS-ish layout; ensure_* are idempotent so warm boots keep state. */
    if (ensure_dir_chain("bin")  < 0) serial_puts("[FS] warn bin\n");
    if (ensure_dir_chain("cfg")  < 0) {}                       /* gui config */
    ensure_dir_chain("home/user");                             /* legacy alias */
    ensure_dir_chain("home/user/wallpaper");
    if (ensure_dir_chain("user") < 0) serial_puts("[FS] warn user\n");
    if (ensure_dir_chain("tmp")  < 0) {}
    if (ensure_dir_chain("etc")  < 0) {}
    if (ensure_dir_chain("proc") < 0) {}
    if (ensure_dir_chain("dev/null") < 0) {}
    if (ensure_dir_chain("dev/zero") < 0) {}
    if (ensure_dir_chain("dev/console") < 0) {}
    if (ensure_dir_chain("dev/tty") < 0) {}
    if (ensure_dir_chain("etc") < 0) {}
    /* seed /etc/passwd + /etc/group once (id/whoami/login applets) */
    if (fs_resolve_impl("/etc/passwd") < 0) {
        int fd = fs_create("/etc/passwd");
        if (fd >= 0) {
            const char* pw = "root:x:0:0:root:/home/eigen:/user/sh.elf\n"
                             "eigen:x:1000:1000:eigen:/home/eigen:/user/sh.elf\n";
            int n = 0; while (pw[n]) n++;
            fs_write(fd, pw, n);
            fs_close(fd);
        }
    }
    if (fs_resolve_impl("/cfg/starticon.cfg") < 0) {
        int fd = fs_create("/cfg/starticon.cfg");
        if (fd >= 0) {
            const char* v = "cfg/logo.bmp\n";
            int n = 0; while (v[n]) n++;
            fs_write(fd, v, n);
            fs_close(fd);
        }
    }
    if (fs_resolve_impl("/etc/group") < 0) {
        int fd = fs_create("/etc/group");
        if (fd >= 0) {
            const char* gr = "root:x:0:\neigen:x:1000:\n";
            int n = 0; while (gr[n]) n++;
            fs_write(fd, gr, n);
            fs_close(fd);
        }
    }

    home_dir_index = ensure_dir_chain("home/eigen");
    desktop_dir_index = ensure_dir_chain("home/eigen/Desktop");
    trash_dir_index   = ensure_dir_chain("home/eigen/.Trash");
    bin_dir_index     = find_in_dir(0, "bin");

    /* boot modules -> /user/<name>.elf */
    serial_puts("[FS] seeds\n");
    {
        extern int user_module_total(void);
        extern int user_module_at(int, const char**, const void**, uint64_t*);
        int total = user_module_total();
        int userdir = find_in_dir(0, "user");
        for (int i = 0; i < total; i++) {
            const char* nm = 0; const void* addr = 0; uint64_t sz = 0;
            if (user_module_at(i, &nm, &addr, &sz) != 0) continue;
            if (!nm || !addr) continue;
            if (userdir < 0) break;
            /* skip non-ELF payloads (fonts, wads stay out of the tree) */
            int nl = 0; while (nm[nl]) nl++;
            if (nl < 4 || strncmp(nm + nl - 4, ".elf", 4) != 0) {
                /* keep .wad etc too — everything except font/ttf */
                if (nl >= 4 && strncmp(nm + nl - 4, ".ttf", 4) == 0) continue;
            }
            char full[MAX_FILENAME];
            int fl = 0;
            while (nm[fl] && fl < MAX_FILENAME-1) { full[fl] = nm[fl]; fl++; }
            full[fl] = 0;
            if (find_in_dir(userdir, full) >= 0) continue;   /* restored */
            int fidx = create_entry(full, FS_FILE, userdir,
                                    FS_FLAG_EXECUTABLE | FS_FLAG_READONLY |
                                    FS_FLAG_SYSTEM);
            if (fidx < 0) continue;
            /* ZERO-COPY: alias the Limine module mapping (higher-half,
               page-aligned, lives forever). capacity==0 marks external. */
            file_t* f = &files[fidx];
            f->data = (uint8_t*)(uintptr_t)addr;
            f->size = (int)sz;
            f->capacity = 0;
        }
    }

    serial_puts("[FS] init done\n");
}
