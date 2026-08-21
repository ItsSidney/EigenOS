/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#ifndef GUI_H
#define GUI_H

#include <stdint.h>

#define TASKBAR_H 36

void start_gui(void);
void gui_shell(void);
void draw_premium_wallpaper(void);
void gui_set_wallpaper_dirty(void);
void gui_wallpaper_apply_anim(void);   /* trigger circle-reveal on wallpaper change */
void draw_taskbar(void);
void draw_taskbar_mac(void);
void desktop_tick(void);
void desk_add_app_icon(int app_idx);
void desk_add_file_icon(const char* path, const char* display_name);
void desk_remove_icon_by_path(const char* path);

/* ── App launcher entry (shared between gui.c and taskbar_mac.c) ── */
typedef struct {
    const char* name;
    void (*launch_func)(void);
    int  category;    /* 0=All, 1=Productivity, 2=System, 3=Games, 4=Graphics, 5=Debug, 6=Accessibility, 7=Networking */
    int  global_idx;
    const char* user_elf; /* ring3 app: name of bundled user module (or NULL) */
} app_item_t;
extern app_item_t menu_app_entries[];

// ── Monolithic Theme System ─────────────────────────────────────────
typedef enum {
    THEME_ROLE_BACKGROUND = 0,
    THEME_ROLE_SURFACE,
    THEME_ROLE_SURFACE_VARIANT,
    THEME_ROLE_PRIMARY,
    THEME_ROLE_ON_PRIMARY,
    THEME_ROLE_SECONDARY,
    THEME_ROLE_ON_SECONDARY,
    THEME_ROLE_TERTIARY,
    THEME_ROLE_ERROR,
    THEME_ROLE_OUTLINE,
    THEME_ROLE_OVERLAY,
    THEME_ROLE_SURFACE_TINT,
    THEME_ROLE_INVERSE_SURFACE,
    THEME_ROLE_INVERSE_ON_SURFACE,
    THEME_ROLE_SHADOW,
    THEME_ROLE_SCROLLBAR,
    THEME_ROLE_DISABLED,
    THEME_ROLE_BUTTON_BG,
    THEME_ROLE_BUTTON_TEXT,
    THEME_ROLE_BUTTON_HOVER,
    THEME_ROLE_MENU_BG,
    THEME_ROLE_MENU_ITEM_HOVER,
    THEME_ROLE_MENU_ITEM_SELECTED,
    THEME_ROLE_WINDOW_BG,
    THEME_ROLE_WINDOW_TITLE,
    THEME_ROLE_WINDOW_BORDER,
    THEME_ROLE_TASKBAR_BG,
    THEME_ROLE_TASKBAR_TEXT,
    THEME_ROLE_ACCENT,
    THEME_ROLE_COUNT
} theme_role_t;

// ── Named Themes (palette + WM layout params) ───────────────
typedef enum {
    THEME_VOID      = 0,   /* Void       — pure deep black, cool greys       */
    THEME_EMBER,           /* Ember       — dark warm charcoal, amber accents */
    THEME_SLATE,           /* Slate       — steel-blue dark, ice highlights   */
    THEME_CARBON,          /* Carbon      — graphite panels, white chrome     */
    THEME_AURORA,          /* Aurora      — deep navy, teal-green accents     */
    THEME_CRIMSON,         /* Crimson     — very dark, deep red highlights    */
    THEME_OBSIDIAN,        /* Obsidian    — near-black purple-dark, silver    */
    THEME_COUNT
} theme_id_t;


/* WM layout parameters that change with the theme */
typedef struct {
    int    titlebar_h;     /* window title bar height              */
    int    corner_radius;  /* window/content corner radius         */
    int    btn_style;      /* 0=square-minimal 1=rounded 2=classic */
    int    font_scale;     /* 0=100% 1=110% 2=125%                */
    uint32_t title_grad_a; /* title bar gradient top (0=none)      */
    uint32_t title_grad_b; /* title bar gradient bottom           */
    int    winbtn_layout;  /* 0=min-left/max+close-right 1=all-left(haiku) 2=all-right(xp) */
    uint32_t close_hover;  /* colour shown on close hover (0=use default red) */
    int    close_only;     /* 1 = draw only the close button (Haiku) */
} wm_theme_params_t;

/* A complete named theme: full role palette + WM params */
typedef struct {
    const char* name;
    uint32_t palette[THEME_ROLE_COUNT];
    wm_theme_params_t wm;
    int accent_idx;       /* index into the global accent table    */
} theme_def_t;

theme_def_t* theme_def(theme_id_t id);
void theme_apply(theme_id_t id);     /* switch active theme + accent */
theme_id_t theme_current(void);
const char* theme_name(theme_id_t id);
void theme_get_wm_params(wm_theme_params_t* out);
void theme_load_cfg(void);           /* restore cfg/theme.cfg at boot */

/* ── User-defined themes (created in Settings → Themes) ───────────── */
#define USER_THEME_MAX 8
int  theme_total(void);              /* built-in + user themes */
int  theme_user_count(void);         /* number of user-defined themes */
int  theme_is_user(theme_id_t id);
/* Adds a user theme (palette copied). Returns its theme_id, or -1. */
theme_id_t theme_user_add(const char* name, const uint32_t* palette,
                          const wm_theme_params_t* wm, int accent_idx);
void theme_user_del(theme_id_t id);  /* removes a user theme (not active) */
void theme_load_accent_cfg(void);    /* restore cfg/accent.cfg at boot */
void theme_set_accent(int idx);      /* live, persistent system-wide accent */

// Personalization
typedef struct {
    int accent_color_idx;
    int clock_24h;
    int mouse_sensitivity;
    int theme;                // 0=Light, 1=Dark, 2=Blue Default
    int bg_idx;                // background palette index
    int bg_pattern;            // background pattern index
    int bg_pattern_size;       // 1,2,4
    int accent_idx;            // accent palette index
    int font_idx;              // font color palette index
    int btn_idx;               // button palette index
    int corner_radius;         // 0=sharp, 2, 4, 6, 8
    int font_size;             // 0=small, 1=medium, 2=large
    int contrast;              // 0=normal, 1=high
    int saturation;            // 0=normal, 1=vivid
    int transparency;          // 0=off, values 1-10
    int wallpaper_id;          // procedural preset id (when wallpaper_mode==0)
    int wallpaper_mode;        // 0=procedural preset (wallpaper_id), 1=image file (wallpaper_file)
    char wallpaper_file[64];   // VFS path of active wallpaper image (mode==1)
    uint64_t taskbar_pinned_mask; // Bitmask of pinned apps (bits indexed by global_idx)
    uint64_t desktop_icons_mask;  // Bitmask of apps shown as desktop icons (bits indexed by global_idx)
} personalization_t;

/* ── Taskbar Layout Manager ─────────────────────────────────
   Global, live-editable taskbar layout. The Layout app edits a
   working copy and "Apply" commits it here; the taskbar reads this
   every frame. TASKBAR_DEFAULT_* are the canonical defaults. */
#define TASKBAR_DEFAULT_POS   0   /* 0=bottom 1=top 2=left 3=right */
#define TASKBAR_DEFAULT_SIZE  36
#define TASKBAR_DEFAULT_OPAC  255
#define TASKBAR_DEFAULT_AH    0   /* autohide off */
#define TASKBAR_DEFAULT_MON   1   /* live CPU/RAM monitor on */
#define TASKBAR_DEFAULT_CLK   1   /* clock shown */
#define TASKBAR_DEFAULT_TB    1   /* toolbar (start/desktops/pins) shown */

typedef struct {
    int pos;        /* 0 bottom, 1 top, 2 left, 3 right */
    int size;       /* thickness in px (24..96) */
    int opacity;    /* 64..255 */
    int autohide;   /* 0 off, 1 on */
    int monitor;    /* 0 off, 1 on (live CPU/RAM graph) */
    int show_clock; /* 0/1 */
    int show_toolbar; /* 0/1 start+desktops+pinned+window tabs */
    int monitor_w;  /* graph width when on taskbar (px) */
    int acrylic;    /* 0/1 layered glass (accent hairline + top highlight) */
    int glow;      /* 0/1 animated running-app glow indicator */
    int search;    /* 0/1 glassy search pill in tray */
    int bell;      /* 0/1 notification bell with badge */
} layout_t;

layout_t* gui_get_layout(void);          /* live global layout (edit/apply) */
void gui_get_work_area(int* wx, int* wy, int* ww, int* wh); /* screen bounds minus taskbar */
void gui_apply_layout(const layout_t* l); /* commit + persist */
void gui_load_layout(void);              /* load persisted layout at boot */
void gui_reset_layout(void);             /* restore defaults + persist */
layout_t* gui_layout_preset(int i);      /* 4 preset slots (0..3) */
const char* gui_layout_preset_name(int i);
void gui_layout_save_preset(int i, const layout_t* l, const char* name);

personalization_t* get_personalization(void);
uint32_t theme_get_color(theme_role_t role);
void theme_set_custom_color(theme_role_t role, uint32_t color);
void theme_reset_custom(void);
uint32_t get_accent_color(void);

// Apps/Settings
void show_sound_settings(void);
void gui_system_shutdown(void);
void gui_toggle_start_menu(void);
void gui_toggle_search(void);
void gui_open_taskbar_layout(void);
void launch_item(int global_idx);
int app_name_to_global_idx(const char* name);
extern const char* all_items[];

/* live taskbar monitor samples (filled by gui each frame, read by taskbar + Layout app) */
void gui_sample_sysmon(void);
extern int  g_sysmon_cpu;     /* last CPU % */
extern int  g_sysmon_ram;     /* last RAM (heap) % */

/* New helpers for audio UI */
void gui_toggle_mute(void);
int gui_is_audio_hovered(int x, int y, int w, int h);

#endif
