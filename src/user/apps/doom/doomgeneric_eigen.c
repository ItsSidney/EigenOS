/* doomgeneric_eigen.c — Eigen OS ring-3 platform layer for DOOM.
 *
 * Implements the doomgeneric DG_* hooks, the small set of i_* platform
 * shims DOOM needs (zone base, fatal error, input, sound stubs), an
 * in-memory WAD file class backed by the EIGEN_SYS_MODLOAD boot module,
 * and main().
 *
 * The engine itself (r_*, g_*, p_*, d_*, w_*, z_*, m_*, v_*, f_*, hu_*,
 * st_*, am_*, wi_*, s_*, i_timer, i_video) is compiled from
 * src/user/doom/engine/. This file replaces the engine's platform files
 * (i_system.c, i_input.c, i_sound.c, w_file.c, etc.).
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "userlib.h"        /* ring-3 syscalls: windows, input, time, modules */
#include "user/eigen.h"     /* EIGEN_* syscall numbers, eigen_ev_t, etc.       */

#include "doomgeneric.h"    /* DG_ScreenBuffer, DG_* prototypes                */
#include "doomkeys.h"
#include "i_video.h"
#include "i_system.h"
#include "i_sound.h"
#include "w_wad.h"
#include "z_zone.h"

/* ===== globals ===== */
static int      g_win = -1;
static uint32_t* g_pix = 0;       /* window content buffer (mapped)        */
/* The engine draws into g_frame (fixed DOOMGENERIC_RESX*RESY). We blit it
   into the window content buffer g_pix, which is (win_w) x (win_h - 32)
   because the WM reserves a 32px titlebar. Decoupling the two keeps the
   engine resolution stable regardless of the window chrome. */
static uint32_t* g_frame = 0;     /* engine framebuffer (640x400)          */
static int      g_cw = 640, g_ch = 400;  /* window content size (px)     */
static int      g_sw = 320, g_sh = 200;   /* DOOM internal res            */
static int      g_scale = 2;              /* integer upscale to window   */
static int      g_ww = 640, g_wh = 400;   /* requested window size       */

/* In-memory WAD loaded from the boot module "doom1.wad". Note: the kernel
   registers boot modules by basename WITHOUT the extension ("doom1"), so
   the lookup name drops the ".wad". */
static uint8_t* g_wad = 0;
static uint64_t g_wad_len = 0;

/* Key translation: array indexed by scancode-set-1 code -> DOOM keycode.
   The WM enqueues set-1 scancodes in ev->a, so index == scancode. */
/* Kernel key code -> DOOM keycode. The kernel delivers ASCII characters
   (shift already applied) plus nav keys 128-134, and maps ctrl+letter to
   1-26 (see include/drivers/input/keyboard.h). Releases arrive from the
   WM as code|0x100. */
static unsigned char k2doom(unsigned char sc) {
    if (sc == 27)                 return KEY_ESCAPE;   /* KEY_ESC */
    if (sc == '\n')              return KEY_ENTER;
    if (sc == '\t')              return KEY_TAB;
    if (sc == '\b')              return KEY_BACKSPACE;
    if (sc == 128)                return KEY_UPARROW;  /* KEY_UP */
    if (sc == 129)                return KEY_DOWNARROW;/* KEY_DOWN */
    if (sc == 130)                return KEY_LEFTARROW;/* KEY_LEFT */
    if (sc == 131)                return KEY_RIGHTARROW;/* KEY_RIGHT */
    if (sc == 137)                return KEY_HOME;     /* KEY_HOME */
    if (sc == 138)                return KEY_END;      /* KEY_END */
    if (sc == 133)                return KEY_PGUP;     /* KEY_PAGE_UP */
    if (sc == 134)                return KEY_PGDN;     /* KEY_PAGE_DOWN */
    if (sc == ' ')                return KEY_USE;      /* space = use */
    if (sc >= 1 && sc <= 26)      return KEY_FIRE;     /* ctrl+letter = fire */
    if (sc == 29)                 return KEY_FIRE;     /* plain ctrl (sc 0x1D) */
    if (sc == 'w' || sc == 'W')   return KEY_UPARROW;  /* WASD */
    if (sc == 's' || sc == 'S')   return KEY_DOWNARROW;
    if (sc == 'a' || sc == 'A')   return KEY_LEFTARROW;
    if (sc == 'd' || sc == 'D')   return KEY_RIGHTARROW;
    if (sc >= 'A' && sc <= 'Z')   return sc + 32;      /* kernel sends shifted chars */
    return sc;                                          /* digits, punctuation */
}

/* ===== DG_* hooks ===== */
static void draw_error(const char* msg) {
    /* Show the failure inside the window instead of a silent black frame. */
    if (!g_pix || g_cw <= 0 || g_ch <= 0) return;
    uint32_t px = (uint32_t)g_cw * (uint32_t)g_ch;
    for (uint32_t i = 0; i < px; i++) g_pix[i] = 0x0B0E14;
    eigen_draw_text(g_pix, g_cw, g_ch, 24, g_ch / 2 - 40, "[DOOM] ",
                    (uint32_t)0xFF5555);
    eigen_draw_text(g_pix, g_cw, g_ch, 24, g_ch / 2 - 16, msg, 0xCCCCCC);
    eigen_draw_text(g_pix, g_cw, g_ch, 24, g_ch / 2 + 8,
                    "Close this window and fix the problem.", 0x888888);
    eigen_win_flush(g_win);
}

void DG_Init(void) {
    /* Request height = engine height + titlebar so the content buffer is
       exactly 640x400 (the WM subtracts a 32px titlebar from the height). */
    g_win = eigen_win_create(60, 50, g_ww, g_wh + 32, "DOOM");
    if (g_win < 0) { eigen_printf("[DOOM] window create failed\n"); return; }
    g_pix = (uint32_t*)eigen_win_map(g_win);
    if (!g_pix) { eigen_printf("[DOOM] map failed\n"); return; }

    /* The engine needs a fixed 640x400 framebuffer; we blit it into the
       real window content area (which may differ by the titlebar). */
    g_frame = (uint32_t*)eigen_malloc((uint64_t)g_ww * g_wh * 4);
    if (!g_frame) {
        draw_error("out of memory (frame buffer)");
        eigen_printf("[DOOM] frame alloc failed\n");
        return;
    }
    DG_ScreenBuffer = g_frame;

    /* Query the actual content size the WM gave us. */
    eigen_win_getsize(g_win, (uint32_t*)&g_cw, (uint32_t*)&g_ch);
    eigen_printf("[DOOM] window content %dx%d (frame %dx%d)\n", g_cw, g_ch, g_ww, g_wh);

    /* Load the WAD from a boot module. */
    g_wad = (uint8_t*)eigen_malloc(8 * 1024 * 1024);
    if (g_wad) {
        long n = eigen_load_module("doom1", g_wad, 8 * 1024 * 1024);
        if (n > 0) { g_wad_len = (uint64_t)n; eigen_printf("[DOOM] loaded WAD: %ld bytes\n", n); }
        else {
            eigen_printf("[DOOM] FATAL: doom1.wad module not found.\n");
            eigen_printf("[DOOM] Place doom1.wad (shareware) in bin/iso_root/ and add:\n");
            eigen_printf("        module_path: boot():/user/doom1.wad\n");
            eigen_printf("        to config/limine.conf, then rebuild.\n");
            draw_error("doom1.wad not found in boot modules.");
            for (;;) eigen_sleep_ms(1000);   /* stay open so the error is visible */
        }
    }
    eigen_printf("[DOOM] DG_Init done\n");
}

void DG_DrawFrame(void) {
    if (g_win < 0 || !g_pix || !g_frame) return;

    /* Blit the engine frame (640x400) into the window content buffer
       (g_cw x g_ch). Keep aspect ratio, letterbox, integer scale. The
       window content is normally exactly 640x400 (we sized the window for
       it), so this is typically a straight 1:1 copy. */
    int fw = g_ww, fh = g_wh;          /* engine frame: 640x400 */
    int scale = 1;
    if (fw > 0 && fh > 0 && g_cw > 0 && g_ch > 0) {
        int sx = g_cw / fw;
        int sy = g_ch / fh;
        scale = sx < sy ? sx : sy;
        if (scale < 1) scale = 1;
    }
    int dw = fw * scale, dh = fh * scale;
    int ox = (g_cw - dw) / 2;
    int oy = (g_ch - dh) / 2;
    if (ox < 0) ox = 0;
    if (oy < 0) oy = 0;

    for (int y = 0; y < dh && (y + oy) < g_ch; y++) {
        const uint32_t* src = g_frame + (y / scale) * fw;
        uint32_t* dst = g_pix + (uint64_t)(y + oy) * g_cw + ox;
        for (int x = 0; x < dw && (x + ox) < g_cw; x++) {
            dst[x] = src[x / scale];
        }
    }

    eigen_win_flush(g_win);
}

void DG_SleepMs(uint32_t ms) { eigen_sleep_ms(ms); }

uint32_t DG_GetTicksMs(void) { return eigen_gettime_ms(); }

static int keyq[64];
static int keyq_w = 0, keyq_r = 0;

static void push_key(int dk) { if (keyq_w < 64) keyq[keyq_w++] = dk; }

int DG_GetKey(int* pressed, unsigned char* key) {
    static eigen_ev_t evs[16];
    /* Drain queued ring-3 window key events. */
    int got = eigen_win_poll(g_win, evs, 16);
    for (int i = 0; i < got; i++) {
        if (evs[i].type == EIGEN_EV_CLOSE) {
            /* Close button: the WM waits for the app to exit. */
            eigen_exit(0);
            return 0;
        }
        if (evs[i].type != EIGEN_EV_KEY) continue;
        int a = evs[i].a;                              /* code | 0x100 = release */
        int released = (a & 0x100) != 0;
        unsigned char dk = k2doom((unsigned char)(a & 0xFF));
        if (dk == 0) continue;
        push_key(released ? (dk | 0x100) : dk);
    }
    if (keyq_r >= keyq_w) return 0;
    int k = keyq[keyq_r++];
    if (keyq_r >= keyq_w) { keyq_r = keyq_w = 0; }
    *pressed = (k & 0x100) ? 0 : 1;
    *key = (unsigned char)(k & 0xFF);
    return 1;
}

void DG_SetWindowTitle(const char* title) { (void)title; }

/* ===== i_system shims ===== */
byte* I_ZoneBase(int* size) {
    /* 6 MiB zone, as the engine default. */
    byte* z = (byte*)eigen_malloc(6 * 1024 * 1024);
    *size = 6 * 1024 * 1024;
    return z;
}
void I_Error(char* error, ...) {
    char msg[256];
    va_list ap;
    va_start(ap, error);
    vsnprintf(msg, sizeof(msg), error, ap);
    va_end(ap);
    eigen_printf("[DOOM FATAL] ");
    eigen_printf(msg);
    eigen_printf("\n");
    draw_error(msg);               /* show the reason inside the window */
    for (;;) eigen_sleep_ms(1000); /* stay open so the message is readable */
}
void I_Quit(void) { eigen_exit(0); }
boolean I_ConsoleStdout(void) { return 0; }
void I_AtExit(void (*func)(void), boolean run_on_error) { (void)func; (void)run_on_error; }
void I_Init(void) {}
void I_PrintBanner(char* m) { (void)m; }
void I_PrintDivider(void) {}
void I_PrintStartupBanner(char* g) { (void)g; }

/* i_video glue expected by the engine (declared in i_video.h).
   I_GetEvent and I_InitInput are provided by the engine's i_input.c
   (the doomgeneric event feeder: drains DG_GetKey -> D_PostEvent). */

/* ===== i_sound stubs (FEATURE_SOUND off; no audio) ===== */
void I_InitSound(boolean use_sfx_prefix) { (void)use_sfx_prefix; }
void I_UpdateSound(void) {}
void I_ShutdownSound(void) {}
int  I_GetSfxLumpNum(sfxinfo_t* sfxinfo) { (void)sfxinfo; return 0; }
int  I_StartSound(sfxinfo_t* sfxinfo, int c, int v, int s) { (void)sfxinfo;(void)c;(void)v;(void)s; return 0; }
void I_StopSound(int c) { (void)c; }
boolean I_SoundIsPlaying(int c) { (void)c; return 0; }
void I_UpdateSoundParams(int c, int v, int s) { (void)c;(void)v;(void)s; }
void I_PrecacheSounds(sfxinfo_t* s, int n) { (void)s;(void)n; }
void I_InitMusic(void) {}
void I_ShutdownMusic(void) {}
void I_SetMusicVolume(int v) { (void)v; }
void I_PauseSong(void) {}
void I_ResumeSong(void) {}
void* I_RegisterSong(void* d, int l) { (void)d;(void)l; return 0; }
void I_UnRegisterSong(void* h) { (void)h; }
void I_PlaySong(void* h, boolean l) { (void)h;(void)l; }
void I_StopSong(void) {}
boolean I_MusicIsPlaying(void) { return 0; }
void I_BindSoundVariables(void) {}

/* ===== WAD: in-memory class backed by the boot module ===== */
static uint8_t* g_wad_base = 0;
static uint64_t g_wad_len0 = 0;

static size_t eigen_wad_read(wad_file_t* wad, unsigned int offset, void* buf, size_t n);
static void eigen_wad_close(wad_file_t* wad);
static wad_file_t* eigen_wad_open(char* path);

static wad_file_class_t eigen_wad_class = {
    eigen_wad_open, eigen_wad_close, eigen_wad_read
};

static wad_file_t* eigen_wad_open(char* path) {
    (void)path;
    if (!g_wad || g_wad_len == 0) return 0;
    g_wad_base = g_wad;
    g_wad_len0 = g_wad_len;
    wad_file_t* wf = (wad_file_t*)eigen_malloc(sizeof(wad_file_t));
    if (!wf) return 0;
    wf->file_class = &eigen_wad_class;
    wf->mapped = 0;
    wf->length = (unsigned int)g_wad_len;
    return wf;
}
static void eigen_wad_close(wad_file_t* wad) { if (wad) eigen_free(wad); }
static size_t eigen_wad_read(wad_file_t* wad, unsigned int offset, void* buf, size_t n) {
    (void)wad;
    if (offset >= g_wad_len0) return 0;
    if (offset + n > g_wad_len0) n = (size_t)(g_wad_len0 - offset);
    memcpy(buf, g_wad_base + offset, n);
    return n;
}

/* Override the engine's W_OpenFile/W_Read/W_CloseFile (we exclude w_file.c). */
wad_file_t* W_OpenFile(char* path) {
    return eigen_wad_open(path);
}
void W_CloseFile(wad_file_t* wad) { if (wad) eigen_free(wad); }
size_t W_Read(wad_file_t* wad, unsigned int offset, void* buffer, size_t buffer_len) {
    return wad->file_class->Read(wad, offset, buffer, buffer_len);
}

/* ===== main ===== */
int main(void) {
    /* DOOM expects argv[0] = program name, no IWAD arg needed because the
       WAD class loads the boot module directly regardless of the path. */
    static char* argv[2] = { "doom", 0 };
    eigen_printf("[DOOM] starting\n");
    doomgeneric_Create(1, argv);

    /* The game loop: doomgeneric_Create -> D_DoomMain -> D_DoomLoop runs
       exactly ONE frame (the title screen), then returns. The platform is
       responsible for pumping doomgeneric_Tick() forever after that.
       TryRunTics() inside paces itself to 35 game tics/sec, so a small
       sleep here just keeps CPU usage sane. */
    for (;;) {
        doomgeneric_Tick();
        DG_SleepMs(4);
    }
    return 0;
}
