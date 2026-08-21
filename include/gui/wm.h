/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

// ============================================================
//  Eigen — Window Manager
//  Manages multiple windows with drag, z-order, and taskbar
// ============================================================
#ifndef WM_H
#define WM_H

#include <stdint.h>

#define WM_MAX_WINDOWS 8
#define WM_TITLEBAR_H  32
#define WM_TASKBAR_H   36
#define WM_MIN_W       200
#define WM_MIN_H       120

// Window flags
/* ── Window flag bits ──────────────────────────────────────── */
#define WM_FLAG_VISIBLE   0x01
#define WM_FLAG_DRAGGING  0x02
#define WM_FLAG_FOCUSED   0x04
#define WM_FLAG_CLOSABLE  0x08
#define WM_FLAG_RESIZING  0x10
#define WM_FLAG_MINIMIZED 0x20
#define WM_FLAG_MAXIMIZED 0x40
#define WM_FLAG_MAX_H     0x80   /* maximized to top half    */
#define WM_FLAG_MAX_V     0x100  /* maximized to bottom half  */
#define WM_FLAG_HUNG     0x200  /* render watchdog tripped (app render too slow) */
#define WM_FLAG_CLOSING  0x400  /* close animation in progress (deferred free) */
#define WM_FLAG_FLOATING 0x800  /* excluded from tiling layout (free-floating) */
#define WM_FLAG_INSTRUMENTED 0x1000
#define WM_FLAG_USER    0x1000  /* ring3 app window: content buffer + event queue */

/* ── Ring-3 user-window events ────────────────────────────── */
#define WM_USER_EV_RENDER 1   /* a:0 b:0 c:0 d:0   (requested redraw)  */
#define WM_USER_EV_KEY    2   /* a:keycode           */
#define WM_USER_EV_MMOVE  3   /* a:x b:y (content-relative)           */
#define WM_USER_EV_MDOWN  4   /* a:x b:y c:button(1 left,2 right)     */
#define WM_USER_EV_MUP    5   /* a:x b:y c:button                     */
#define WM_USER_EV_CLOSE  6   /* close button clicked                 */
#define WM_USER_EV_QMAX   64

typedef struct {
    uint32_t type;
    int32_t a, b, c, d;
} wm_user_ev_t;

/* ── Window open-animation styles ─────────────────────────── */
#define WM_ANIM_NONE   0   /* no animation                       */
#define WM_ANIM_ZOOM   1   /* scale up from center + fade in     */
#define WM_ANIM_SLIDE  2   /* slide in from the left + fade in   */
#define WM_ANIM_DROP   3   /* drop down from the top + fade in   */

/* ── Snap / maximize directions ────────────────────────────── */
#define WM_MAX_FULL   0   /* restore / maximize full   */
#define WM_MAX_VERT   1   /* snap to top half          */
#define WM_MAX_HORIZ  2   /* snap to left half         */
#define WM_MAX_RIGHT  3   /* snap to right half        */
#define WM_MAX_BOTTOM 4   /* snap to bottom half       */

typedef void (*wm_render_cb)(int id, int x, int y, int w, int h, int vx, int vy);
typedef void (*wm_key_cb)(int id, char key);
typedef void (*wm_click_cb)(int id, int btn_id);
typedef void (*wm_resize_cb)(int id, int w, int h);

typedef struct {
    int id;
    int x, y, w, h;
    int view_x, view_y;       // Viewport offset for scrolling
    int content_w, content_h; // Total size of content
    char title[64];
    uint32_t accent_color;
    int flags;
    int z_order;
    
        int desktop;  /* Virtual desktop this window belongs to          */

    /* ── Restored geometry (saved when maximized/minimized) ── */
    int saved_x, saved_y, saved_w, saved_h;

    /* ── Close-button hit area ─────────────────────────────── */
    int close_btn_x, close_btn_y, close_btn_w, close_btn_h;

    /* ── Drag / Resize state ────────────────────────────────── */
    int drag_offset_x;
    int drag_offset_y;
    int start_w, start_h;

    /* ── Double-click tracking (title bar) ──────────────────── */
    uint32_t last_title_click_ms;
    int title_click_x, title_click_y;

    /* ── Open animation ────────────────────────────────── */
    uint32_t open_ms;     /* timer_get_ms() when the window opened */
    int anim_type;       /* WM_ANIM_* chosen at open time        */
    int close_ms;        /* timer_get_ms() when close started (closing anim) */
    uint8_t hung_already; /* watchdog already reported this window as slow */
    uint8_t slow_frames;  /* consecutive over-budget render frames (watchdog) */

    /* ── Geometry tween (maximize / restore / minimize) ── */
    int anim_from_x, anim_from_y, anim_from_w, anim_from_h;
    int anim_to_x, anim_to_y, anim_to_w, anim_to_h;
    uint32_t geom_anim_ms; /* start of the current geometry tween */
    int geom_anim;         /* 0=none, 1=resize/maximize/restore, 2=minimize */
    // App callbacks
    wm_render_cb on_render;
    wm_key_cb on_key;
    wm_resize_cb on_resize;
    
    // Button system (reuse ui_button_t concept)
    int button_count;
    struct {
        int id;
        int x, y, w, h;
        char label[32];
        uint32_t bg_color, fg_color;
        wm_click_cb on_click;
        int is_hovered;
        int is_active;
    } buttons[512];
    
    // App-specific data pointer
    void* app_data;

    /* ── Ring-3 user window (WM_FLAG_USER) ─────────────────── */
    uint32_t* user_buf;      /* content buffer the app draws into (mapped to app) */
    uint32_t* user_cbuf;     /* composite buffer: flush copies user_buf→cbuf,
                                WM composites cbuf. Decouples app writes (async)
                                from WM reads (per-frame) so no torn frames. */
    int user_buf_w, user_buf_h;
    int user_pending_w, user_pending_h;  /* resize requested by the WM frame;
                                            applied atomically in wm_user_flush
                                            so user_buf_w/h never change while
                                            the app is mid-frame */
    int user_pid;            /* owning ring-3 task (0 = none); WM closes the
                                window when that task dies (black-window fix) */
    uint32_t user_ev_head, user_ev_tail;
    wm_user_ev_t user_ev_q[WM_USER_EV_QMAX];
    int user_lmx, user_lmy;  /* last enqueued mouse pos (move throttle) */
    uint32_t* user_old_buf;   /* pre-resize buffers, freed on the next flush
                                 (the app may still be drawing the previous
                                 frame's dimensions when a resize happens) */
    uint32_t* user_old_cbuf;
} wm_window_t;

/* ── Tiling window manager ──────────────────────────────────── */
void wm_tile_all(void);        /* (re)lay out all windows without overlap */
void wm_set_tiling(int on);    /* enable/disable tiling mode             */
int  wm_get_tiling(void);      /* 1 if tiling mode is active             */

/* ── Core lifecycle ─────────────────────────────────────────── */
void wm_init(void);

/* Open a new window, returns window ID or -1 if full.
   Flags: pass WM_FLAG_CLOSABLE to enable the close button. */
int wm_window_count(void);
int wm_open_window(int x, int y, int w, int h, const char* title,
                   uint32_t accent, wm_render_cb render, wm_key_cb key_handler,
                   wm_resize_cb resize_handler);

/* ── Ring-3 user windows ───────────────────────────────────── */
int  wm_open_window_user(int x, int y, int w, int h, const char* title, uint32_t accent);
void* wm_user_map_buffer(int id);      /* user VMA of the content buffer */
int  wm_user_flush(int id);
int  wm_user_poll(int id, wm_user_ev_t* out, int max);
int  wm_user_enqueue(int id, uint32_t type, int a, int b, int c, int d);

/* ── Window open-animation selection ───────────────────────── */
void wm_set_open_anim(int type);   /* type: WM_ANIM_*   */
int  wm_get_open_anim(void);
void wm_load_anim_cfg(void);       /* restore cfg/anim.cfg at boot */

void wm_close_window(int id);
void wm_user_cleanup_dead(void);   /* close user windows whose task died */
void wm_mark_crashed(int pid, uint64_t rip, uint64_t fault, uint64_t err);

/* ── Window state queries ──────────────────────────────────── */
wm_window_t* wm_get_window(int id);
wm_window_t* wm_get_window_by_index(int index);  /* slot index, may be NULL */
int wm_get_focused(void);
int wm_get_window_count(void);
int wm_window_at_point(int px, int py);
int wm_get_window_title(int id, char* buf, int bufsize);
uint32_t wm_get_window_accent(int id);
int wm_window_is_minimized(int id);
int wm_window_is_maximized(int id);     /* 1 if fully or half-maximized */
int wm_window_is_visible(int id);

/* ── Window state manipulation ────────────────────────────── */
void wm_bring_to_front(int win_id);
void wm_set_window_title(int win_id, const char* title);
void wm_set_window_accent(int win_id, uint32_t accent);
void wm_minimize_window(int win_id);
void wm_maximize_window(int win_id, int mode);  /* mode: WM_MAX_FULL/VERT/HORIZ... */
void wm_restore_window(int win_id);             /* restore from minimized/maximized */
void wm_toggle_maximize(int win_id);            /* toggle full maximize */

/* ── Buttons ──────────────────────────────────────────────── */
void wm_add_button(int win_id, int btn_id, int x, int y, int w, int h,
                   const char* label, uint32_t bg, uint32_t fg, wm_click_cb cb);
void wm_clear_buttons(int win_id);
void wm_set_button_active(int win_id, int btn_id, int active);
void wm_refresh_all_button_theme(void);

/* ── App data ─────────────────────────────────────────────── */
void wm_set_app_data(int win_id, void* data);
void* wm_get_app_data(int win_id);

/* ── Window snapping ─────────────────────────────────────── */
void wm_snap_window(int win_id, int mode);

/* ── Single-window blocking run (backward compat) ────────── */
void wm_run_single(int win_id);

/* ── Virtual desktop support ───────────────────────────────── */
int wm_get_current_desktop(void);
void wm_set_current_desktop(int d);
int wm_get_previous_desktop(void);

/* ── Unified per-frame mouse snapshot ────────────────────────
   Computed once per wm_tick.  Use these from app render callbacks
   so every app sees identical input state.  Coordinates are
   screen-absolute. */
int wm_mouse_x(void);
int wm_mouse_y(void);
int wm_mouse_buttons(void);         /* live button state (1=left,2=right,4=mid) */
int wm_mouse_left_clicked(void);    /* left  press edge (0→1) this frame */
int wm_mouse_left_released(void);   /* left  release edge (1→0) this frame */
int wm_mouse_left_held(void);       /* left  button currently held         */
int wm_mouse_right_clicked(void);   /* right press edge this frame         */
int wm_mouse_right_released(void);  /* right release edge this frame       */
int wm_mouse_right_held(void);      /* right button currently held         */
int wm_mouse_any_clicked(void);     /* any button press edge this frame    */
int wm_mouse_any_released(void);    /* any button release edge this frame  */

/* ── Main tick ─────────────────────────────────────────────── */
/* Process input, update state, and render all visible windows.
   Returns 0 if desktop should continue, 1 if terminal requested. */
int wm_tick(void);

#endif
