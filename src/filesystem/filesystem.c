/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#include "filesystem/filesystem.h"
#include "drivers/time/rtc.h"
#include "kernel/mem/kheap.h"

static file_t files[MAX_FILES];
static int current_dir = -1;
static int home_dir_index = -1;
static int trash_dir_index = -1;
static int desktop_dir_index = -1;
static int bin_dir_index = -1;

/* Ensure a file's heap buffer can hold at least `need` bytes (grows, never shrinks). */
static int file_ensure_cap(int fd, int need) {
    if (need > MAX_FILE_CAP) return 0;
    if (files[fd].capacity >= need && files[fd].data) return 1;
    int newcap = files[fd].capacity ? files[fd].capacity : 256;
    while (newcap < need) newcap *= 2;
    if (newcap > MAX_FILE_CAP) newcap = MAX_FILE_CAP;
    uint8_t* nd = (uint8_t*)kmalloc((size_t)newcap);
    if (!nd) return 0;
    for (int i = 0; i < files[fd].size; i++) nd[i] = files[fd].data ? files[fd].data[i] : 0;
    if (files[fd].data) kfree(files[fd].data);
    files[fd].data = nd;
    files[fd].capacity = newcap;
    return 1;
}

static void file_free_data(int fd) {
    if (files[fd].data) { kfree(files[fd].data); files[fd].data = 0; }
    files[fd].capacity = 0; files[fd].size = 0;
}

/* Seed a small text file (used for boot/EFI placeholder entries). */
static void seed_text(int fd, const char* txt) {
    if (fd < 0) return;
    int len = 0; while (txt[len]) len++;
    for (int i = 0; i < len; i++) {
        char c = txt[i];
        fs_write(fd, &c, 1);
    }
}

extern int strcmp(const char* s1, const char* s2);
extern char* strcpy(char* dst, const char* src);

int get_user_dir() { return home_dir_index; }
int fs_get_current_dir() { return current_dir; }
int fs_get_home_dir() { return home_dir_index; }
int fs_get_trash_dir() { return trash_dir_index; }
int fs_get_desktop_dir() { return desktop_dir_index; }
int fs_get_bin_dir() { return bin_dir_index; }

static int find_free_slot() {
    for (int i = 0; i < MAX_FILES; i++)
        if (!files[i].in_use) return i;
    return -1;
}

static int find_in_dir(int dir_idx, const char* name) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (files[i].in_use && files[i].parent_dir == dir_idx) {
            const char* a = files[i].name;
            const char* b = name;
            int eq = 1;
            while (*a && *b) { if (*a++ != *b++) { eq = 0; break; } }
            if (eq && *a == *b) return i;
        }
    }
    return -1;
}

static int create_entry(const char* name, fs_type_t type, int parent, uint8_t flags) {
    int i = find_free_slot();
    if (i < 0) return -1;
    files[i].in_use = 1;
    files[i].type = type;
    files[i].parent_dir = parent;
    files[i].flags = flags;
    files[i].size = 0;
    files[i].capacity = 0;
    files[i].data = 0;
    files[i].creation_time = 1;
    files[i].modified_time = 1;
    int j = 0;
    while (name[j] && j < MAX_FILENAME - 1) { files[i].name[j] = name[j]; j++; }
    files[i].name[j] = 0;
    return i;
}

int ensure_dir_chain(const char* path) {
    int parent = -1;
    const char* p = path;
    if (*p == '/') p++;
    char comp[MAX_FILENAME];
    while (*p) {
        int ci = 0;
        while (*p && *p != '/') { comp[ci++] = *p++; }
        comp[ci] = 0;
        int found = find_in_dir(parent, comp);
        if (found < 0) {
            found = create_entry(comp, FS_DIRECTORY, parent, 0);
            if (found < 0) return -1;
        }
        parent = found;
        if (*p == '/') p++;
    }
    return parent;
}

void init_filesystem() {
    for (int i = 0; i < MAX_FILES; i++) {
        files[i].in_use = 0; files[i].size = 0;
        files[i].name[0] = 0; files[i].parent_dir = -1;
        files[i].type = FS_FILE; files[i].flags = 0;
        files[i].creation_time = 0; files[i].modified_time = 0;
    }

    int boot_dir = ensure_dir_chain("boot");
    int efi_dir = ensure_dir_chain("EFI");
    int efi_boot = ensure_dir_chain("EFI/boot");
    int home_dir = ensure_dir_chain("home");
    int user_dir = ensure_dir_chain("home/user");
    home_dir_index = user_dir;
    int docs = ensure_dir_chain("home/user/Documents");
    int dloads = ensure_dir_chain("home/user/Downloads");
    int music = ensure_dir_chain("home/user/Music");
    int pics = ensure_dir_chain("home/user/Pictures");
    int trash_dir = ensure_dir_chain("home/user/Trash");
    trash_dir_index = trash_dir;
    int desktop_dir = ensure_dir_chain("home/user/Desktop");
    desktop_dir_index = desktop_dir;
    int bin_dir = ensure_dir_chain("home/user/bin");
    bin_dir_index = bin_dir;
    int tmp_dir = ensure_dir_chain("tmp");
    (void)tmp_dir;

    int f;
    if (boot_dir >= 0) {
        f = create_entry("grub.cfg", FS_FILE, boot_dir, FS_FLAG_READONLY | FS_FLAG_SYSTEM);
        if (f >= 0) seed_text(f, "# GRUB configuration file\nset timeout=5\nset default=0\nmenuentry \"Eigen\" {\n  multiboot2 /boot/kernel.bin\n  module2 /boot/initrd.img\n}");
        f = create_entry("kernel.bin", FS_FILE, boot_dir, FS_FLAG_READONLY | FS_FLAG_SYSTEM);
        if (f >= 0) seed_text(f, "[Eigen Kernel v1.0]\nMultiboot2-compatible kernel image.");
        f = create_entry("initrd.img", FS_FILE, boot_dir, FS_FLAG_READONLY | FS_FLAG_SYSTEM);
        if (f >= 0) seed_text(f, "[Initial RAM Disk]\nContains essential drivers and startup scripts.");
    }

    if (efi_boot >= 0) {
        f = create_entry("bootx64.efi", FS_FILE, efi_boot, FS_FLAG_READONLY | FS_FLAG_SYSTEM | FS_FLAG_HIDDEN);
        if (f >= 0) seed_text(f, "[EFI x64 Bootloader]\nLimine UEFI x64 bootloader image.");
        f = create_entry("bootia32.efi", FS_FILE, efi_boot, FS_FLAG_READONLY | FS_FLAG_SYSTEM | FS_FLAG_HIDDEN);
        if (f >= 0) seed_text(f, "[EFI ia32 Bootloader]\nLimine UEFI ia32 bootloader image.");
    }

    f = create_entry("System Volume Information", FS_DIRECTORY, -1, FS_FLAG_HIDDEN | FS_FLAG_SYSTEM);

    /* Image-management: seed wallpaper packages into home/user/wallpaper/ + load manifest. */
    /* wallpaper_mgr_init() removed with src/apps/system/wallpaper.c */

    current_dir = user_dir;
}

/* Resolve a path (absolute if it starts with '/', otherwise relative to
 * `base_dir`) down to the parent directory of the final component. The final
 * component name is copied into `out_name` (empty if the path denotes a
 * directory itself). Returns the parent dir index, or -1 if an intermediate
 * directory does not exist. */
static int resolve_parent(int base_dir, const char* path, char* out_name, int name_cap) {
    if (name_cap > 0) out_name[0] = 0;
    if (!path || !*path) return base_dir;
    int dir = base_dir;
    const char* p = path;
    if (*p == '/') { dir = -1; p++; }
    char comp[MAX_FILENAME];
    while (*p) {
        int ci = 0;
        while (*p && *p != '/') {
            if (ci < MAX_FILENAME - 1) comp[ci++] = *p++;
            else p++;
        }
        comp[ci] = 0;
        int ended = (*p == 0);
        if (*p == '/') p++;
        if (ended) {
            int j = 0;
            while (j < name_cap - 1 && comp[j]) { out_name[j] = comp[j]; j++; }
            out_name[j] = 0;
            return dir;
        }
        if (ci == 0 || strcmp(comp, ".") == 0) continue;
        if (strcmp(comp, "..") == 0) {
            dir = (dir < 0) ? -1 : files[dir].parent_dir;
            continue;
        }
        int found = find_in_dir(dir, comp);
        if (found < 0) return -1;
        dir = found;
    }
    return dir;
}

int fs_open(const char* filename, int flags) {
    (void)flags;
    char name[MAX_FILENAME];
    int dir = resolve_parent(current_dir, filename, name, MAX_FILENAME);
    if (name[0] == 0) return -1;
    return find_in_dir(dir, name);
}

int fs_read(int fd, char* buf, int count) {
    if (fd < 0 || fd >= MAX_FILES || !files[fd].in_use) return -1;
    if (!files[fd].data) return 0;
    int n = count < files[fd].size ? count : files[fd].size;
    for (int i = 0; i < n; i++) buf[i] = (char)files[fd].data[i];
    return n;
}

/* Offset-aware read (used by the per-fd lseek machinery in task.c). */
int fs_read_at(int fd, char* buf, int count, int offset) {
    if (fd < 0 || fd >= MAX_FILES || !files[fd].in_use) return -1;
    if (!files[fd].data || offset >= files[fd].size) return 0;
    int avail = files[fd].size - offset;
    int n = count < avail ? count : avail;
    for (int i = 0; i < n; i++) buf[i] = (char)files[fd].data[offset + i];
    return n;
}

int fs_write(int fd, const char* buf, int count) {
    if (fd < 0 || fd >= MAX_FILES || !files[fd].in_use) return -1;
    int start = files[fd].size;
    int avail = MAX_FILE_CAP - start;
    int n = (count < avail) ? count : avail;
    if (n <= 0) return 0;
    if (!file_ensure_cap(fd, start + n)) return 0;
    for (int i = 0; i < n; i++) files[fd].data[start + i] = (uint8_t)buf[i];
    files[fd].size += n;
    files[fd].modified_time = 1;
    return n;
}

int fs_close(int fd) { (void)fd; return 0; }

int fs_create(const char* filename) {
    char name[MAX_FILENAME];
    int dir = resolve_parent(current_dir, filename, name, MAX_FILENAME);
    if (name[0] == 0) return -1;
    if (find_in_dir(dir, name) >= 0) return -1;
    return create_entry(name, FS_FILE, dir, 0);
}

int fs_delete(const char* filename) {
    char name[MAX_FILENAME];
    int parent = resolve_parent(current_dir, filename, name, MAX_FILENAME);
    if (name[0] == 0) return -1;
    int idx = find_in_dir(parent, name);
    if (idx < 0) return -1;

    if (parent == trash_dir_index || parent < 0) {
        file_free_data(idx);
        files[idx].in_use = 0;
        files[idx].name[0] = 0;
        return 0;
    }

    if (trash_dir_index >= 0) {
        files[idx].parent_dir = trash_dir_index;
        char trash_name[MAX_FILENAME];
        int ti = 0;
        const char* src = filename;
        while (*src && ti < MAX_FILENAME - 1) trash_name[ti++] = *src++;
        trash_name[ti] = 0;
        if (find_in_dir(trash_dir_index, trash_name) >= 0) {
            for (int d = 0; d < 1000; d++) {
                ti = 0; src = filename;
                while (*src && ti < MAX_FILENAME - 6) trash_name[ti++] = *src++;
                trash_name[ti++] = '.';
                if (d >= 100) { trash_name[ti++] = '0' + d / 100; }
                if (d >= 10) { trash_name[ti++] = '0' + (d / 10) % 10; }
                trash_name[ti++] = '0' + d % 10;
                trash_name[ti] = 0;
                if (find_in_dir(trash_dir_index, trash_name) < 0) break;
            }
        }
        int ni = 0;
        while (trash_name[ni]) { files[idx].name[ni] = trash_name[ni]; ni++; }
        files[idx].name[ni] = 0;
        return 0;
    }
    return -1;
}

int fs_exists(const char* filename) {
    char name[MAX_FILENAME];
    int dir = resolve_parent(current_dir, filename, name, MAX_FILENAME);
    if (name[0] == 0) return 0;
    return find_in_dir(dir, name) >= 0 ? 1 : 0;
}

/* Colorized Directory Listing with ANSI Colors */
int fs_list(char* output, int max_len) {
    int pos = 0;
    if (current_dir >= 0) {
        const char* dotdot = "\e[94m..\e[0m/\n";
        while (*dotdot && pos < max_len - 1) output[pos++] = *dotdot++;
    }

    for (int i = 0; i < MAX_FILES; i++) {
        if (files[i].in_use && files[i].parent_dir == current_dir) {
            const char* n = files[i].name;
            const char* color_code = "\e[97m"; /* Default White */

            if (files[i].type == FS_DIRECTORY) {
                color_code = "\e[94m"; /* Vivid Cyan/Blue for directories */
            } else {
                int len = 0; while (n[len]) len++;
                if (len > 4 && strcmp(n + len - 4, ".bin") == 0) color_code = "\e[92m"; /* Vivid Green for binaries */
                else if (len > 3 && strcmp(n + len - 3, ".ec") == 0) color_code = "\e[92m";
                else if (len > 3 && strcmp(n + len - 3, ".sh") == 0) color_code = "\e[92m";
                else if (len > 4 && strcmp(n + len - 4, ".bmp") == 0) color_code = "\e[95m"; /* Vivid Pink for images */
                else if (len > 4 && strcmp(n + len - 4, ".png") == 0) color_code = "\e[95m";
                else if (len > 2 && strcmp(n + len - 2, ".c") == 0) color_code = "\e[93m"; /* Vivid Yellow for C code */
                else if (len > 2 && strcmp(n + len - 2, ".h") == 0) color_code = "\e[93m";
                else if (len > 4 && strcmp(n + len - 4, ".txt") == 0) color_code = "\e[96m"; /* Bright Cyan for text */
            }

            while (*color_code && pos < max_len - 1) output[pos++] = *color_code++;
            while (*n && pos < max_len - 1) output[pos++] = *n++;
            if (files[i].type == FS_DIRECTORY && pos < max_len - 1) output[pos++] = '/';
            const char* reset_code = "\e[0m\n";
            while (*reset_code && pos < max_len - 1) output[pos++] = *reset_code++;
        }
    }
    if (pos < max_len) output[pos] = 0;
    return pos;
}

int fs_mkdir(const char* dirname) {
    if (find_in_dir(current_dir, dirname) >= 0) return -1;
    return create_entry(dirname, FS_DIRECTORY, current_dir, 0);
}

int fs_rmdir(const char* dirname) {
    int idx = find_in_dir(current_dir, dirname);
    if (idx < 0) return -1;
    if (files[idx].type != FS_DIRECTORY) return -1;
    for (int i = 0; i < MAX_FILES; i++)
        if (files[i].in_use && files[i].parent_dir == idx) return -1;
    files[idx].in_use = 0;
    files[idx].size = 0;
    files[idx].name[0] = 0;
    return 0;
}

int fs_cd(const char* dirname) {
    if (!dirname || !*dirname || dirname[0] == '/') {
        if (dirname && dirname[0] == '/') {
            if (dirname[1] == 0) { current_dir = -1; return 0; }
            int saved = current_dir;
            current_dir = -1;
            const char* p = dirname + 1;
            char comp[MAX_FILENAME];
            while (*p) {
                int ci = 0;
                while (*p && *p != '/') { comp[ci++] = *p++; }
                comp[ci] = 0;
                if (strcmp(comp, "..") == 0) {
                    current_dir = files[current_dir].parent_dir;
                } else if (strcmp(comp, ".") == 0) {
                } else {
                    int idx = find_in_dir(current_dir, comp);
                    if (idx < 0 || files[idx].type != FS_DIRECTORY) {
                        current_dir = saved;
                        return -1;
                    }
                    current_dir = idx;
                }
                if (*p == '/') p++;
            }
            return 0;
        }
        current_dir = -1;
        return 0;
    }
    if (strcmp(dirname, "..") == 0) {
        if (current_dir >= 0) current_dir = files[current_dir].parent_dir;
        return 0;
    }
    if (strcmp(dirname, ".") == 0) return 0;
    int idx = find_in_dir(current_dir, dirname);
    if (idx < 0 || files[idx].type != FS_DIRECTORY) return -1;
    current_dir = idx;
    return 0;
}

int fs_pwd(char* output, int max_len) {
    if (current_dir < 0) {
        output[0] = '/';
        output[1] = 0;
        return 1;
    }
    char stack[32][MAX_FILENAME]; int depth = 0;
    int d = current_dir;
    while (d >= 0) {
        int ni = 0;
        while (files[d].name[ni]) { stack[depth][ni] = files[d].name[ni]; ni++; }
        stack[depth][ni] = 0;
        depth++;
        d = files[d].parent_dir;
    }
    int pos = 0;
    output[pos++] = '/';
    for (int s = depth - 1; s >= 0; s--) {
        for (int ci = 0; stack[s][ci]; ci++) {
            if (pos < max_len - 1) output[pos++] = stack[s][ci];
        }
        if (s > 0 && pos < max_len - 1) output[pos++] = '/';
    }
    output[pos] = 0;
    return pos;
}

int fs_cat(const char* filename, char* output, int max_len) {
    int fd = find_in_dir(current_dir, filename);
    if (fd < 0) return -1;
    if (!files[fd].data) return 0;
    int n = files[fd].size < max_len ? files[fd].size : max_len - 1;
    for (int i = 0; i < n; i++) output[i] = (char)files[fd].data[i];
    output[n] = 0;
    return n;
}

int fs_touch(const char* filename) {
    int fd = find_in_dir(current_dir, filename);
    if (fd >= 0) { files[fd].modified_time = 1; return 0; }
    return fs_create(filename);
}

int fs_rename(const char* oldname, const char* newname) {
    int idx = find_in_dir(current_dir, oldname);
    if (idx < 0) return -1;
    int ni = 0;
    while (newname[ni] && ni < MAX_FILENAME - 1) { files[idx].name[ni] = newname[ni]; ni++; }
    files[idx].name[ni] = 0;
    return 0;
}

int fs_move(const char* src, const char* dst) {
    int idx = find_in_dir(current_dir, src);
    if (idx < 0) return -1;
    int ddir = find_in_dir(current_dir, dst);
    if (ddir >= 0 && files[ddir].type == FS_DIRECTORY) {
        files[idx].parent_dir = ddir;
        return 0;
    }
    return fs_rename(src, dst);
}

int fs_truncate(const char* filename) {
    char name[MAX_FILENAME];
    int dir = resolve_parent(current_dir, filename, name, MAX_FILENAME);
    if (name[0] == 0) return -1;
    int idx = find_in_dir(dir, name);
    if (idx < 0) return -1;
    files[idx].size = 0;
    file_free_data(idx);
    files[idx].modified_time = 1;
    return 0;
}

int fs_get_node(int idx, char* name, int* size, int* type, int* parent, uint8_t* flags, uint32_t* mod_time) {
    if (idx < 0 || idx >= MAX_FILES || !files[idx].in_use) return -1;
    if (name) {
        int i = 0;
        while (files[idx].name[i]) { name[i] = files[idx].name[i]; i++; }
        name[i] = 0;
    }
    if (size) *size = files[idx].size;
    if (type) *type = files[idx].type;
    if (parent) *parent = files[idx].parent_dir;
    if (flags) *flags = files[idx].flags;
    if (mod_time) *mod_time = files[idx].modified_time;
    return 0;
}

int fs_set_flags(const char* filename, uint8_t flags) {
    int idx = find_in_dir(current_dir, filename);
    if (idx < 0) return -1;
    files[idx].flags = flags;
    return 0;
}

int fs_trash_file(const char* filename) { return fs_delete(filename); }

int fs_empty_trash() {
    if (trash_dir_index < 0) return -1;
    int count = 0;
    for (int i = 0; i < MAX_FILES; i++) {
        if (files[i].in_use && files[i].parent_dir == trash_dir_index) {
            files[i].in_use = 0;
            files[i].size = 0;
            files[i].name[0] = 0;
            count++;
        }
    }
    return count;
}

int fs_restore_from_trash(const char* filename) {
    int idx = find_in_dir(trash_dir_index, filename);
    if (idx < 0) return -1;
    if (home_dir_index >= 0) {
        files[idx].parent_dir = home_dir_index;
        int ni = 0;
        while (files[idx].name[ni]) ni++;
        while (ni > 0 && files[idx].name[ni-1] >= '0' && files[idx].name[ni-1] <= '9') ni--;
        if (ni > 0 && files[idx].name[ni-1] == '.') ni--;
        files[idx].name[ni] = 0;
        return 0;
    }
    return -1;
}

int fs_get_free_count() {
    int c = 0;
    for (int i = 0; i < MAX_FILES; i++) if (!files[i].in_use) c++;
    return c;
}

int fs_copy_file(const char* src_name, const char* dst_name) {
    int src = find_in_dir(current_dir, src_name);
    if (src < 0) return -1;
    if (find_in_dir(current_dir, dst_name) >= 0) return -1;
    int dst = create_entry(dst_name, files[src].type, current_dir, files[src].flags);
    if (dst < 0) return -1;
    if (files[src].size > 0 && file_ensure_cap(dst, files[src].size)) {
        for (int i = 0; i < files[src].size; i++)
            files[dst].data[i] = files[src].data[i];
    }
    files[dst].size = files[src].size;
    files[dst].modified_time = 1;
    return dst;
}

int fs_copy_file_to_dir(int src_dir, const char* src_name, int dst_dir, const char* dst_name) {
    int src = find_in_dir(src_dir, src_name);
    if (src < 0) return -1;
    if (find_in_dir(dst_dir, dst_name) >= 0) return -1;
    int dst = create_entry(dst_name, files[src].type, dst_dir, files[src].flags);
    if (dst < 0) return -1;
    if (files[src].size > 0 && file_ensure_cap(dst, files[src].size)) {
        for (int i = 0; i < files[src].size; i++)
            files[dst].data[i] = files[src].data[i];
    }
    files[dst].size = files[src].size;
    files[dst].modified_time = 1;
    return dst;
}

int fs_get_dir_count(int dir_idx) {
    int c = 0;
    for (int i = 0; i < MAX_FILES; i++)
        if (files[i].in_use && files[i].parent_dir == dir_idx) c++;
    return c;
}

int fs_find_child(int dir_idx, int child_idx) {
    int c = 0;
    for (int i = 0; i < MAX_FILES; i++) {
        if (files[i].in_use && files[i].parent_dir == dir_idx) {
            if (c == child_idx) return i;
            c++;
        }
    }
    return -1;
}

void fs_get_permission_string(uint8_t flags, char* out) {
    out[0] = (flags & FS_FLAG_READONLY) ? 'r' : 'r';
    out[1] = (flags & FS_FLAG_READONLY) ? '-' : 'w';
    out[2] = (flags & FS_FLAG_EXECUTABLE) ? 'x' : '-';
    out[3] = ' ';
    out[4] = (flags & FS_FLAG_SYSTEM) ? 'S' : '-';
    out[5] = (flags & FS_FLAG_HIDDEN) ? 'H' : '-';
    out[6] = 0;
}

void fs_format_time(uint32_t ms, char* out, int max_len) {
    (void)ms;
    const char* s = "Today";
    int i = 0;
    while (s[i] && i < max_len - 1) { out[i] = s[i]; i++; }
    out[i] = 0;
}

int fs_is_directory_empty(int dir_idx) {
    for (int i = 0; i < MAX_FILES; i++)
        if (files[i].in_use && files[i].parent_dir == dir_idx) return 0;
    return 1;
}

int fs_find_by_index(int dir_idx, int nth, char* name, int* size, int* type, uint8_t* flags, uint32_t* mod_time) {
    int c = 0;
    for (int i = 0; i < MAX_FILES; i++) {
        if (files[i].in_use && files[i].parent_dir == dir_idx) {
            if (c == nth) {
                return fs_get_node(i, name, size, type, 0, flags, mod_time);
            }
            c++;
        }
    }
    return -1;
}
