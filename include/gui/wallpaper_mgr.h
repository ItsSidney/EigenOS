/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#ifndef WALLPAPER_MGR_H
#define WALLPAPER_MGR_H

#include <stdint.h>

/* Image-management / package-manager for desktop wallpapers.
 *
 * Wallpapers are real files in the VFS (home/user/wallpaper/*.bmp). A set of
 * 5 "packages" is embedded in the kernel (via objcopy in build.sh) and seeded
 * into home/user/wallpaper/ at boot. The Personalization app and File Explorer treat them
 * as installable wallpapers; applying one writes the active selection to
 * cfg/wallpapers.cfg so it persists and "Recent" can be tracked.
 *
 * Mode:
 *   0 = procedural preset (legacy gradient/patterns), identified by an id
 *   1 = image file (a .bmp in home/user/wallpaper/), identified by path
 */

#define WP_MAX        64
#define WP_NAME_MAX   64
#define WP_PATH_MAX   96

typedef struct {
    char path[WP_PATH_MAX];   /* VFS path, e.g. home/user/wallpaper/wp1.bmp */
    char name[WP_NAME_MAX];   /* display name (basename without .bmp) */
    uint32_t last_used;      /* for "Recent" ordering (0 = never) */
    int installed;           /* 1 if shipped as a package, 0 if user-added */
} wp_entry_t;

/* Call once at boot (after init_filesystem). Seeds packages + loads manifest. */
void wallpaper_mgr_init(void);
/* Re-scan the wallpaper folder (after files were added/removed at runtime). */
void wallpaper_mgr_rescan(void);

/* Called from theme_init after prefs are reset: restores the last-applied
 * wallpaper from the manifest, or defaults to the first shipped package (wp5). */
void wallpaper_mgr_apply_default(void);

/* Number of wallpapers currently in home/user/wallpaper/. */
int  wallpaper_mgr_count(void);
/* Nth wallpaper entry (0..count-1). Returns 0 if out of range. */
int  wallpaper_mgr_get(int i, wp_entry_t* out);

/* Mode / active selection. */
int  wallpaper_mgr_mode(void);          /* 0 procedural, 1 file */
int  wallpaper_mgr_procedural_id(void); /* valid when mode==0 */
void wallpaper_mgr_active_path(char* out, int out_sz); /* valid when mode==1 */

/* Apply a wallpaper (by wallpaper-folder index). Switches to file mode. */
void wallpaper_mgr_apply(int i);
/* Apply a specific VFS path (used by File Explorer "Set as background"). */
void wallpaper_mgr_apply_path(const char* vfs_path);
/* Apply a procedural preset id (legacy). */
void wallpaper_mgr_apply_procedural(int id);

/* Decode a VFS .bmp file into a bmp_image_t (caller must bmp_free it). 1 on ok. */
int  wallpaper_mgr_decode_file(const char* vfs_path, void* out_img /* bmp_image_t* */);

/* Decode-once thumbnail for list index i (96x54 RGB held in a static cache,
 * so rendering a wallpaper list never re-decodes multi-MB bitmaps per frame).
 * Fills out_img pointing into the cache; caller must NOT bmp_free it. 1 on ok. */
int  wallpaper_mgr_get_thumb(int i, void* out_img /* bmp_image_t* */);

/* Draw a decoded image scaled (letterboxed) into a screen region. */
void wallpaper_mgr_draw_region(const void* img /* bmp_image_t* */,
                               int x, int y, int w, int h);

/* Blit the active file wallpaper, COVER-fit (fills the area, crops overflow)
 * into a 32-bit RGBA buffer (e.g. the framebuffer) of cw x ch using the given
 * row stride. Used by the wallpaper pipeline. */
void wallpaper_mgr_blit_active(uint32_t* dst, int stride, int cw, int ch);

#endif
