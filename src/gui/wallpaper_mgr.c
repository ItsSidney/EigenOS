/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

/* Image-management / wallpaper package manager for Eigen OS.
 * See include/gui/wallpaper_mgr.h for the public API and design notes.
 *
 * Wallpapers ship as Limine MODULES (boot():/wallpapers/wpN.bmp) so they do
 * NOT bloat the kernel's .data segment (which previously broke the Limine
 * load). At boot we copy each module's bytes into the user's wallpaper folder
 * (home/user/wallpaper/) and treat them as installable, real VFS files. The config
 * manifest (wallpapers.cfg) also lives in home/user/wallpaper/. Personalization /
 * File Explorer both auto-detect any *.bmp in that folder.
 */
#include "gui/wallpaper_mgr.h"
#include "gui/gui.h"
#include "libs/bmp.h"
#include "filesystem/filesystem.h"
#include "kernel/mem/kheap.h"
#include "drivers/video/gfx.h"
#include "drivers/video/framebuffer.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* from kernel.c: look up a Limine module by its file basename (no extension) */
extern int user_module_find(const char* name, const void** data, uint64_t* size);
/* from kernel.c: enumerate every registered Limine module (dynamic wallpapers) */
extern int user_module_total(void);
extern int user_module_at(int idx, const char** name, const void** data, uint64_t* size);
/* from gui.c: triggers the circular wallpaper-reveal animation on apply */
extern void gui_wallpaper_apply_anim(void);

/* forward declarations */
static void load_manifest(void);

/* ── package modules ──
 * The fixed wp1..wp5 list is GONE: seeding now walks the Limine module
 * registry and installs every "wp*" module (see seed_packages below). */

/* Wallpapers + config live in the user's home/wallpaper folder (so they show
 * up in File Explorer's Home view). Paths are ABSOLUTE (leading '/') because
 * seeding/rescan run from init_filesystem() while current_dir is still -1, so
 * relative paths would fail to resolve from root. */
#define WP_FOLDER       "/home/user/wallpaper"
#define WP_SEED_PREFIX  "/home/user/wallpaper/wp"
#define WP_MANIFEST     "/home/user/wallpaper/wallpapers.cfg"

static wp_entry_t g_list[WP_MAX];
static int g_count = 0;

/* Decoded-once thumbnails (small RGB copies) so the Wallpaper app never
 * re-decodes ~6 MB bitmaps every frame — that churned the kernel heap and
 * made the app lag. Invalidated whenever the folder is re-scanned. */
#define THUMB_W 96
#define THUMB_H 54
static uint8_t g_thumb_px[WP_MAX][THUMB_W * THUMB_H * 3];
static uint8_t g_thumb_ok[WP_MAX];
static void thumb_invalidate(void) { memset(g_thumb_ok, 0, sizeof(g_thumb_ok)); }

/* ── helpers ─────────────────────────────────────────────────── */
static int ends_with(const char* s, const char* suf) {
    int ls = 0, lsu = 0; while (s[ls]) ls++; while (suf[lsu]) lsu++;
    if (lsu > ls) return 0;
    return strncmp(s + ls - lsu, suf, lsu) == 0;
}
static void basename_noext(const char* path, char* out, int outsz) {
    int len = 0; while (path[len]) len++;
    int sl = len; while (sl > 0 && path[sl-1] != '/') sl--;
    int i = 0, j = sl;
    while (path[j] && path[j] != '.' && i < outsz - 1) out[i++] = path[j++];
    out[i] = 0;
}

/* ── seeding: copy each Limine module's bytes into home/wallpaper/ ── */
/* Dynamic: EVERY registered boot module whose basename starts with "wp"
 * (wp1, wp2, wpw1, …) is seeded as /home/user/wallpaper/<name>.bmp.
 * Dropping a new src/assets/wallpapers/wp*.bmp into the build is enough —
 * build.sh registers it as a Limine module and it shows up here.        */
static void seed_packages(void) {
    if (ensure_dir_chain(WP_FOLDER) < 0) return;   /* folder must exist */

    int total = user_module_total();
    for (int i = 0; i < total; i++) {
        const char* name = 0;
        const void* mdata = 0;
        uint64_t msize = 0;
        if (user_module_at(i, &name, &mdata, &msize) != 0) continue;
        if (!name || name[0] != 'w' || name[1] != 'p' || !name[2]) continue;
        if (!mdata || msize == 0) continue;

        /* path: /home/user/wallpaper/<name>.bmp */
        char path[128];
        int k = 0;
        const char* base = WP_SEED_PREFIX;
        while (*base && k < 110) path[k++] = *base++;
        int p = 0; while (name[p] && k < 118) path[k++] = name[p++];
        path[k++] = '.'; path[k++] = 'b'; path[k++] = 'm'; path[k++] = 'p';
        path[k] = 0;

        if (fs_exists(path)) continue;           /* already installed */

        int fd = fs_create(path);
        if (fd < 0) continue;
        /* write in 4 KiB chunks */
        unsigned long off = 0;
        while (off < msize) {
            int chunk = (msize - off > 4096) ? 4096 : (int)(msize - off);
            fs_write(fd, (const char*)((const uint8_t*)mdata + off), chunk);
            off += chunk;
        }
        fs_close(fd);                       /* commit the file before rescan sees it */
    }
}

/* ── scan the wallpaper folder for *.bmp into g_list (auto-detect) ── */
static void rescan(void) {
    g_count = 0;
    thumb_invalidate();
    if (ensure_dir_chain(WP_FOLDER) < 0) return;   /* create if missing */
    int ddir = fs_open(WP_FOLDER, 0);
    if (ddir < 0) return;
    int n = fs_get_dir_count(ddir);
    for (int i = 0; i < n && g_count < WP_MAX; i++) {
        char nm[64]; int sz, tp; uint8_t fl; uint32_t mt;
        if (fs_find_by_index(ddir, i, nm, &sz, &tp, &fl, &mt) != 0) continue;
        if (tp != FS_FILE) continue;
        if (!ends_with(nm, ".bmp") && !ends_with(nm, ".BMP")) continue;
        wp_entry_t* e = &g_list[g_count];
        int p = 0; const char* base = WP_FOLDER "/";
        while (*base && p < WP_PATH_MAX - 1) e->path[p++] = *base++;
        int q = 0; while (nm[q] && p < WP_PATH_MAX - 1) e->path[p++] = nm[q++];
        e->path[p] = 0;
        basename_noext(e->path, e->name, WP_NAME_MAX);
        e->last_used = 0;
        e->installed = 0;
        g_count++;
    }
    load_manifest();
}

/* ── manifest persistence (home/wallpaper/wallpapers.cfg) ─────── */
static int g_manifest_mode = 0;
static char g_manifest_active[WP_PATH_MAX];

static void load_manifest(void) {
    int fd = fs_open(WP_MANIFEST, 0);
    if (fd < 0) return;
    char buf[512]; int n = fs_read(fd, buf, sizeof(buf) - 1); fs_close(fd);
    if (n <= 0) return;
    buf[n] = 0;
    /* parse lines: "mode=0|1", "active=<path>", "used=<path>:<time>", "inst=<path>" */
    char* line = buf;
    while (*line) {
        char* nl = line; while (*nl && *nl != '\n') nl++;
        char tmp[256]; int ti = 0;
        char* p = line; while (p < nl && ti < 255) tmp[ti++] = *p++; tmp[ti] = 0;
        if (ti == 0) { line = (*nl ? nl + 1 : nl); continue; }
        if (strncmp(tmp, "used=", 5) == 0) {
            char* cp = tmp + 5; char* colon = cp; while (*colon && *colon != ':') colon++;
            if (*colon) {
                uint32_t t = 0; char* e = colon + 1; while (*e >= '0' && *e <= '9') { t = t*10 + (*e - '0'); e++; }
                *colon = 0;
                for (int i = 0; i < g_count; i++)
                    if (strcmp(g_list[i].path, cp) == 0) { g_list[i].last_used = t; break; }
            }
        } else if (strncmp(tmp, "inst=", 5) == 0) {
            for (int i = 0; i < g_count; i++)
                if (strcmp(g_list[i].path, tmp + 5) == 0) { g_list[i].installed = 1; break; }
        } else if (strncmp(tmp, "active=", 7) == 0) {
            int k = 0; const char* s = tmp + 7; while (*s && k < WP_PATH_MAX - 1) g_manifest_active[k++] = *s++;
            g_manifest_active[k] = 0;
        } else if (strncmp(tmp, "mode=", 5) == 0) {
            g_manifest_mode = (tmp[5] == '1') ? 1 : 0;
        }
        line = (*nl ? nl + 1 : nl);
    }
}

static void save_manifest(void) {
    ensure_dir_chain(WP_FOLDER);
    int fd = fs_create(WP_MANIFEST);
    if (fd < 0) return;
    personalization_t* p = get_personalization();
    char line[256];
    int l = 0;
    const char* m = (p->wallpaper_mode == 1) ? "1" : "0";
    while (*m) line[l++] = *m++;
    line[l++] = '\n';
    if (p->wallpaper_mode == 1) {
        const char* a = "active="; while (*a) line[l++] = *a++;
        const char* pp = p->wallpaper_file; while (*pp && l < 250) line[l++] = *pp++;
        line[l++] = '\n';
    }
    for (int i = 0; i < g_count; i++) {
        if (g_list[i].installed) {
            const char* s = "inst="; while (*s) line[l++] = *s++;
            const char* pp = g_list[i].path; while (*pp && l < 230) line[l++] = *pp++;
            line[l++] = '\n';
        }
        if (g_list[i].last_used) {
            const char* s = "used="; while (*s) line[l++] = *s++;
            const char* pp = g_list[i].path; while (*pp && l < 200) line[l++] = *pp++;
            line[l++] = ':';
            uint32_t t = g_list[i].last_used; char num[12]; int ni = 0;
            if (t == 0) num[ni++] = '0'; else { char tmp2[12]; int ti = 0; while (t) { tmp2[ti++] = '0' + (t % 10); t /= 10; } while (ti) num[ni++] = tmp2[--ti]; }
            num[ni] = 0; int x = 0; while (num[x]) line[l++] = num[x++];
            line[l++] = '\n';
        }
    }
    fs_write(fd, line, l);
    fs_close(fd);
}

/* ── public API ──────────────────────────────────────────────── */
void wallpaper_mgr_rescan(void) { rescan(); }

void wallpaper_mgr_init(void) {
    seed_packages();
    rescan();
    /* restore active selection from manifest/prefs */
    personalization_t* p = get_personalization();
    if (p->wallpaper_mode == 1 && p->wallpaper_file[0]) {
        /* verify it still exists; if not, fall back to procedural */
        if (!fs_exists(p->wallpaper_file)) { p->wallpaper_mode = 0; p->wallpaper_file[0] = 0; }
    }
}

/* Called from theme_init after prefs are reset: restores the wallpaper the
 * user last applied (from the manifest), or the first shipped package (wp5)
 * on a fresh boot so the OS never starts on the plain procedural gradient. */
static int blit_probe_module(const char* mod);
void wallpaper_mgr_apply_default(void) {
    personalization_t* p = get_personalization();

    /* Shipped packs live as Limine modules - pick wp5 first, straight
       from module memory. This path cannot be broken by VFS races. */
    {
        static const char* modpref[] = { "wp5","wp4","wp3","wp2","wp1",0 };
        for (int i = 0; modpref[i]; i++) {
            if (blit_probe_module(modpref[i])) {
                p->wallpaper_mode = 2;
                int k = 0;
                while (modpref[i][k] && k < 15)
                    { p->wallpaper_mod[k] = modpref[i][k]; k++; }
                p->wallpaper_mod[k] = 0;
                return;
            }
        }
    }
    if (g_manifest_mode == 1 && g_manifest_active[0] && fs_exists(g_manifest_active)) {
        p->wallpaper_mode = 1;
        int k = 0; while (g_manifest_active[k] && k < 95) { p->wallpaper_file[k] = g_manifest_active[k]; k++; }
        p->wallpaper_file[k] = 0;
        return;
    }
    /* prefer wp5 as boot default */
    static const char* defaults[] = { "wp5", "wp4", "wp3", "wp2", "wp1", NULL };
    for (int i = 0; defaults[i]; i++) {
        char path[128];
        snprintf(path, sizeof(path), "/home/user/wallpaper/%s.bmp", defaults[i]);
        if (fs_exists(path)) {
            p->wallpaper_mode = 1;
            int k = 0; while (path[k] && k < 95) { p->wallpaper_file[k] = path[k]; k++; }
            p->wallpaper_file[k] = 0;
            break;
        }
    }
}

int wallpaper_mgr_count(void) { return g_count; }
int wallpaper_mgr_get(int i, wp_entry_t* out) {
    if (i < 0 || i >= g_count) return 0;
    *out = g_list[i];
    return 1;
}

int wallpaper_mgr_mode(void) { return get_personalization()->wallpaper_mode; }
int wallpaper_mgr_procedural_id(void) { return get_personalization()->wallpaper_id; }
void wallpaper_mgr_active_path(char* out, int out_sz) {
    personalization_t* p = get_personalization();
    int i = 0;
    while (p->wallpaper_file[i] && i < out_sz - 1) { out[i] = p->wallpaper_file[i]; i++; }
    out[i] = 0;
}

static uint32_t g_clock = 0;
static void touch_used(const char* path) {
    for (int i = 0; i < g_count; i++)
        if (strcmp(g_list[i].path, path) == 0) { g_list[i].last_used = ++g_clock + 1000; break; }
}

void wallpaper_mgr_apply(int i) {
    if (i < 0 || i >= g_count) return;
    personalization_t* p = get_personalization();
    gui_wallpaper_apply_anim();   /* snapshot current wallpaper for the reveal */
    p->wallpaper_mode = 1;
    int k = 0; while (g_list[i].path[k] && k < 63) { p->wallpaper_file[k] = g_list[i].path[k]; k++; }
    p->wallpaper_file[k] = 0;
    touch_used(g_list[i].path);
    gui_set_wallpaper_dirty();
    save_manifest();
}
void wallpaper_mgr_apply_path(const char* vfs_path) {
    if (!vfs_path || !*vfs_path) return;
    /* already registered? just apply */
    for (int i = 0; i < g_count; i++) {
        if (strcmp(g_list[i].path, vfs_path) == 0) { wallpaper_mgr_apply(i); return; }
    }
    /* A freshly chosen file (e.g. a bitmap the user drew in Bitmap Maker) is
     * not yet a registered wallpaper. Copy it into the wallpaper folder so it
     * becomes a real, persisted wallpaper with a canonical absolute path. */
    char name[64]; basename_noext(vfs_path, name, sizeof(name));
    char dst[160];
    int k = 0; const char* base = WP_FOLDER "/";
    while (*base && k < 140) dst[k++] = *base++;
    int q = 0; while (name[q] && k < 150) dst[k++] = name[q++];
    const char* ext = ".bmp"; int e = 0; while (ext[e] && k < 156) dst[k++] = ext[e++];
    dst[k] = 0;
    /* avoid clobbering an existing registered file with the same basename */
    if (fs_exists(dst)) {
        /* pick a unique name */
        int n = 1; char tmp[160];
        do {
            int t = 0; const char* b2 = WP_FOLDER "/";
            while (*b2 && t < 120) tmp[t++] = *b2++;
            int s = 0; while (name[s] && t < 130) tmp[t++] = name[s++];
            char num[8]; int ni = 0; if (n == 0) num[ni++]='0'; else { char tt[8]; int ti=0; int nn=n; while(nn){tt[ti++]= '0'+(nn%10); nn/=10;} while(ti) num[ni++]=tt[--ti]; }
            num[ni]=0; int x=0; while(num[x]) tmp[t++]=num[x++];
            const char* x2=".bmp"; int x3=0; while(x2[x3]&&t<155) tmp[t++]=x2[x3++];
            tmp[t]=0;
            if (!fs_exists(tmp)) { int j=0; while(tmp[j]&&j<159) dst[j++]=tmp[j]; dst[j]=0; break; }
            n++;
            if (n > 9999) break;
        } while (1);
    }
    /* copy bytes from source file into the destination */
    int sfd = fs_open(vfs_path, 0);
    if (sfd >= 0) {
        int sz=0,tp; uint8_t fl; uint32_t mt;
        fs_get_node(sfd, 0, &sz, &tp, 0, &fl, &mt);
        if (sz > 0) {
            uint8_t* buf = (uint8_t*)kmalloc((size_t)sz);
            if (buf) {
                int rd = fs_read(sfd, (char*)buf, sz);
                fs_close(sfd);
                int dfd = fs_create(dst);
                if (dfd >= 0) { fs_write(dfd, (const char*)buf, rd > 0 ? rd : sz); fs_close(dfd); }
                kfree(buf);
            } else fs_close(sfd);
        } else fs_close(sfd);
    }
    /* register + apply the canonical (now-resident) path */
    if (g_count < WP_MAX) {
        g_thumb_ok[g_count] = 0;   /* slot may hold a stale thumbnail */
        wp_entry_t* e = &g_list[g_count];
        int p = 0; while (dst[p] && p < WP_PATH_MAX - 1) e->path[p++] = dst[p];
        e->path[p] = 0;
        basename_noext(e->path, e->name, WP_NAME_MAX);
        e->last_used = 0; e->installed = 0;
        g_count++;
        wallpaper_mgr_apply(g_count - 1);
    }
}
void wallpaper_mgr_apply_procedural(int id) {
    personalization_t* p = get_personalization();
    gui_wallpaper_apply_anim();   /* snapshot current wallpaper for the reveal */
    p->wallpaper_mode = 0;
    p->wallpaper_id = id;
    p->wallpaper_file[0] = 0;
    gui_set_wallpaper_dirty();
    save_manifest();
}

/* ── decode + draw ───────────────────────────────────────────── */
int wallpaper_mgr_decode_file(const char* vfs_path, void* out_img) {
    int fd = fs_open(vfs_path, 0);
    if (fd < 0) return 0;
    int size = 0; int tp; uint8_t fl; uint32_t mt;
    fs_get_node(fd, 0, &size, &tp, 0, &fl, &mt);
    if (size <= 0) { fs_close(fd); return 0; }
    uint8_t* buf = (uint8_t*)kmalloc((size_t)size);
    if (!buf) { fs_close(fd); return 0; }
    int rd = fs_read(fd, (char*)buf, size);
    fs_close(fd);
    int ok = 0;
    if (rd == size) ok = bmp_decode(buf, size, (bmp_image_t*)out_img);
    kfree(buf);
    return ok;
}

/* Decode-once thumbnail: fills out_img pointing into the static cache.
 * The caller must NOT bmp_free it (the big decode is freed here). */
int wallpaper_mgr_get_thumb(int i, void* out_img) {
    if (i < 0 || i >= g_count) return 0;
    bmp_image_t* out = (bmp_image_t*)out_img;
    if (g_thumb_ok[i]) {
        out->width = THUMB_W; out->height = THUMB_H;
        out->pixels = g_thumb_px[i];
        return 1;
    }
    bmp_image_t full; full.pixels = 0;
    if (!wallpaper_mgr_decode_file(g_list[i].path, &full)) return 0;
    if (full.width <= 0 || full.height <= 0) { bmp_free(&full); return 0; }
    int iw = full.width, ih = full.height;
    for (int yy = 0; yy < THUMB_H; yy++) {
        int sy = (yy * ih + ih / 2) / THUMB_H;
        const uint8_t* row = full.pixels + (long)sy * iw * 3;
        uint8_t* d = g_thumb_px[i] + yy * THUMB_W * 3;
        for (int xx = 0; xx < THUMB_W; xx++) {
            int sx = (xx * iw + iw / 2) / THUMB_W;
            const uint8_t* p = row + sx * 3;
            d[xx * 3] = p[0]; d[xx * 3 + 1] = p[1]; d[xx * 3 + 2] = p[2];
        }
    }
    bmp_free(&full);
    g_thumb_ok[i] = 1;
    out->width = THUMB_W; out->height = THUMB_H;
    out->pixels = g_thumb_px[i];
    return 1;
}

void wallpaper_mgr_draw_region(const void* img, int x, int y, int w, int h) {
    const bmp_image_t* im = (const bmp_image_t*)img;
    uint32_t stride = gfx_get_stride();
    uint32_t* bb = gfx_get_back_buffer();
    if (!bb) return;
    if (!im || im->width <= 0 || im->height <= 0) {
        /* fallback: subtle gradient so the slot is never pure black */
        for (int yy = 0; yy < h; yy++) {
            uint32_t* d = bb + (y + yy) * stride + x;
            uint32_t c = gfx_rgb_to_pixel(RGB(0x16, 0x1A, 0x22) + (yy * 0x10 / h));
            for (int xx = 0; xx < w; xx++) d[xx] = c;
        }
        return;
    }
    int iw = im->width, ih = im->height;
    /* cover fit: the image always fills the whole box (cropped when the
     * aspect ratios differ) — thumbnails and preview show no bars */
    int sw = w, sh = h, ox = 0, oy = 0;
    if ((long)h * iw >= (long)w * ih) { sh = h; sw = (int)((long)h * iw / ih); ox = (w - sw) / 2; }
    else                              { sw = w; sh = (int)((long)w * ih / iw); oy = (h - sh) / 2; }
    for (int yy = 0; yy < h; yy++) {
        int sy = (yy - oy) * ih / sh;
        if (sy < 0) sy = 0; else if (sy >= ih) sy = ih - 1;
        const uint8_t* row = im->pixels + sy * iw * 3;
        uint32_t* dst = bb + (y + yy) * stride + x;
        for (int xx = 0; xx < w; xx++) {
            int sx = (xx - ox) * iw / sw;
            if (sx < 0) sx = 0; else if (sx >= iw) sx = iw - 1;
            const uint8_t* p = row + sx * 3;
            dst[xx] = 0xFF000000U | ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
        }
    }

}
static int wp_blit_cover(const bmp_image_t* im, uint32_t* dst, int stride, int cw, int ch);

/* Blit a shipped wallpaper straight from its Limine module bytes -
 * no VFS involvement, immune to seed/path races. */
extern int user_module_total(void);
extern int user_module_at(int idx, const char** name, const void** data,
                          uint64_t* size);
static int blit_module_named(const char* mod, uint32_t* dst, int stride,
                             int cw, int ch) {
    int total = user_module_total();
    for (int i = 0; i < total; i++) {
        const char* name = 0; const void* data = 0; uint64_t size = 0;
        if (user_module_at(i, &name, &data, &size) != 0) continue;
        if (!name || !data || strcmp(name, mod) != 0) continue;
        if (size < 64 || ((const uint8_t*)data)[0] != 'B'
                      || ((const uint8_t*)data)[1] != 'M') return 0;
        bmp_image_t img; img.pixels = 0;
        if (!bmp_decode((const uint8_t*)data, (int)size, &img)) return 0;
        int rc = wp_blit_cover(&img, dst, stride, cw, ch);
        bmp_free(&img);
        return rc;
    }
    return 0;
}

int wallpaper_mgr_blit_active(uint32_t* dst, int stride, int cw, int ch) {
    personalization_t* p = get_personalization();
    if (p->wallpaper_mode == 1 && p->wallpaper_file[0]) {
        bmp_image_t img; img.pixels = 0;
        if (wallpaper_mgr_decode_file(p->wallpaper_file, &img)) {
            int rc = wp_blit_cover(&img, dst, stride, cw, ch);
            bmp_free(&img);
            if (rc) return 1;
        }
    }
    /* active file failed: try every other registered package before
     * giving up, so the desktop always gets a real photo if one exists */
    for (int i = 0; i < g_count; i++) {
        if (!g_list[i].path[0]) continue;
        if (p->wallpaper_mode == 1 &&
            strcmp(g_list[i].path, p->wallpaper_file) == 0) continue;
        bmp_image_t img; img.pixels = 0;
        if (!wallpaper_mgr_decode_file(g_list[i].path, &img)) continue;
        int rc = wp_blit_cover(&img, dst, stride, cw, ch);
        bmp_free(&img);
        if (rc) {
            /* adopt this one as active so it persists visually */
            int k = 0; while (g_list[i].path[k] && k < 95)
                { p->wallpaper_file[k] = g_list[i].path[k]; k++; }
            p->wallpaper_file[k] = 0;
            p->wallpaper_mode = 1;
            return 1;
        }
    }
    /* last resort gradient */
    for (int yy = 0; yy < ch; yy++) {
        uint32_t* d = dst + yy * stride;
        uint32_t c = 0xFF000000U | (0x101820 + (yy * 0x20 / ch));
        for (int xx = 0; xx < cw; xx++) d[xx] = c;
    }
    return 0;
}

static int blit_probe_module(const char* mod) {
    int total = user_module_total();
    for (int i = 0; i < total; i++) {
        const char* name = 0; const void* data = 0; uint64_t size = 0;
        if (user_module_at(i, &name, &data, &size) != 0) continue;
        if (name && strcmp(name, mod) == 0 && data && size > 64 &&
            ((const uint8_t*)data)[0]=='B' && ((const uint8_t*)data)[1]=='M')
            return 1;
    }
    return 0;
}

static int wp_blit_cover(const bmp_image_t* im, uint32_t* dst, int stride,
                         int cw, int ch) {
    int iw = im->width, ih = im->height;
    /* Full-screen COVER: the photo always fills the ENTIRE screen (cropped
     * when the aspect ratios differ) — no bars, no letterbox, no mid-screen
     * slab. Renders once per wallpaper change, then is cached. */
    if (iw <= 0 || ih <= 0 || !im->pixels) return 0;
    int sw, sh, ox = 0, oy = 0;
    if ((long)ch * iw >= (long)cw * ih) { sh = ch; sw = (int)((long)ch * iw / ih); ox = (cw - sw) / 2; }
    else                                { sw = cw; sh = (int)((long)cw * ih / iw); oy = (ch - sh) / 2; }
    for (int yy = 0; yy < ch; yy++) {
        int sy = (yy - oy) * ih / sh;
        if (sy < 0) sy = 0; else if (sy >= ih) sy = ih - 1;
        const uint8_t* row = im->pixels + (long)sy * iw * 3;
        uint32_t* d = dst + yy * stride;
        for (int xx = 0; xx < cw; xx++) {
            int sx = (xx - ox) * iw / sw;
            if (sx < 0) sx = 0; else if (sx >= iw) sx = iw - 1;
            const uint8_t* pp = row + sx * 3;
            d[xx] = 0xFF000000U | ((uint32_t)pp[0] << 16) | ((uint32_t)pp[1] << 8) | pp[2];
        }
    }
}
