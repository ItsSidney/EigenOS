/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

/*********************************************************************
 * Eigen OS — Screen Saver engine.
 *
 * Four fullscreen effects (starfield / matrix rain / plasma / orbit)
 * built on the Animation Engine. Idle detection lives in the GUI main
 * loop; this module only owns the effects, the canvases and the
 * "currently showing" flag.
 *********************************************************************/
#include "gui/screensaver.h"
#include "engine/anim/anim_engine.h"
#include "drivers/video/framebuffer.h"
#include "drivers/input/keyboard.h"
#include "kernel/time/timer.h"
#include <stdint.h>
#include <string.h>
#include <math.h>

int saver_enabled = 300;   /* 5 minutes by default */
int saver_effect  = SAVER_STARFIELD;
int saver_cycle   = 0;     /* 0 = no auto-cycle */
int saver_active  = 0;
uint32_t saver_armed_at = 0;     /* ms timestamp of last (manual/auto) activation */

/* ---------- canvases ---------- */
static anim_canvas_t cv_full;   /* starfield / matrix / orbit */
static anim_canvas_t cv_plasma; /* plasma runs at a low res, presented scaled */

static uint32_t rng_state = 0x5A17C0DE;
static float e_time = 0.0f;

/* ---------- starfield ---------- */
#define NSTARS 150
typedef struct { float x, y, z; } star_t;
static star_t stars[NSTARS];

/* ---------- matrix ---------- */
#define MCOL_MAX 176
static float m_head[MCOL_MAX], m_speed[MCOL_MAX];
static unsigned char m_glyph[MCOL_MAX];
static int m_cols = 0;
static const char m_chars[] = "ABCDEF0123456789<>/[]{}#$%&*+=;'\"`~^()";
static const uint32_t m_green[3] = {0x33FF66, 0x1FBF4F, 0x0F7F33};

/* ---------- orbit ---------- */
static anim_sprite_t orb;
static float sat_a[2];

static const uint32_t palette[5] = {
    0xE5352B, 0x1F6FEB, 0x8250DF, 0xED8936, 0x1F9CC8
};

/* draw one 8x16 font glyph into the canvas */
static void cv_char(anim_canvas_t* c, int x, int y, unsigned char ch, uint32_t rgb) {
    extern const uint8_t font8x16[];
    const uint8_t* g = &font8x16[(unsigned char)ch * 16];
    for (int gy = 0; gy < 16; gy++)
        for (int gx = 0; gx < 8; gx++) {
            if (!(g[gy] & (0x80 >> gx))) continue;
            int px = x + gx, py = y + gy;
            if (px >= 0 && py >= 0 && px < c->w && py < c->h)
                c->px[py * c->w + px] = rgb;
        }
}

anim_canvas_t* saver_canvas(void) {
    if (saver_effect == SAVER_PLASMA || saver_effect == SAVER_AURORA) {
        if (!cv_plasma.px) {
            anim_canvas_new(&cv_plasma, 320, 180);
        }
        return &cv_plasma;
    }
    if (!cv_full.px) {
        uint32_t fw = get_fb_width(), fh = get_fb_height();
        anim_canvas_new(&cv_full, (int)fw, (int)fh);
    }
    return &cv_full;
}

/* ===================== effects ===================== */

static void fx_starfield(anim_canvas_t* c, float dt) {
    anim_canvas_fade(c, 0.90f);
    for (int i = 0; i < NSTARS; i++) {
        star_t* s = &stars[i];
        s->z -= 0.0006f * dt;
        if (s->z <= 0.06f) {
            s->z = 1.0f;
            s->x = anim_randf(&rng_state, -1.0f, 1.0f);
            s->y = anim_randf(&rng_state, -1.0f, 1.0f);
        }
        int sx = (int)(c->w * 0.5f + (s->x / s->z) * c->w * 0.5f);
        int sy = (int)(c->h * 0.5f + (s->y / s->z) * c->h * 0.5f);
        if (sx < 0 || sy < 0 || sx >= c->w - 1 || sy >= c->h - 1) continue;
        uint8_t lum = (uint8_t)((1.0f - s->z) * 255.0f);
        uint32_t rgb = ((uint32_t)lum << 16) | ((uint32_t)lum << 8) | lum;
        c->px[sy * c->w + sx] = rgb;
        c->px[sy * c->w + sx + 1] = rgb;
        c->px[(sy + 1) * c->w + sx] = rgb;
        c->px[(sy + 1) * c->w + sx + 1] = rgb;
    }
}

static void fx_matrix(anim_canvas_t* c, float dt) {
    anim_canvas_fade(c, 0.94f);
    for (int i = 0; i < m_cols; i++) {
        m_head[i] += m_speed[i] * dt;
        int gx = i * 8;
        int hy = (int)m_head[i];
        cv_char(c, gx, hy, m_glyph[i], m_green[0]);
        cv_char(c, gx, hy - 16, m_glyph[i], m_green[1]);
        cv_char(c, gx, hy - 32, m_glyph[i], m_green[2]);
        if (hy > c->h) {
            m_head[i] = -(float)(anim_rng32(&rng_state) % 300) / 10.0f;
            m_speed[i] = 0.08f + (float)(anim_rng32(&rng_state) % 140) / 1000.0f;
            m_glyph[i] = (unsigned char)m_chars[anim_rng32(&rng_state) % (sizeof(m_chars) - 1)];
        }
    }
}

static void fx_plasma(anim_canvas_t* c, float dt) {
    (void)dt;
    float tt = e_time;
    int hue0 = (int)(tt * 30.0f);
    for (int y = 0; y < c->h; y++) {
        for (int x = 0; x < c->w; x++) {
            float v = (float)(sin((double)(x * 0.05 + tt * 1.1)) +
                              sin((double)(y * 0.06 + tt * 0.8)) +
                              sin((double)((x + y) * 0.04 + tt * 0.6)));
            int h = ((int)((v + 3.0f) * 42.5f) + hue0) % 360;
            c->px[y * c->w + x] = anim_hsv(h, 230, 235);
        }
    }
}

static void fx_orbit(anim_canvas_t* c, float dt) {
    anim_canvas_fade(c, 0.85f);

    int bits = anim_sprite_bounce(&orb, (float)c->w, (float)c->h, 0.0f, dt, NULL);
    if (bits) orb.spin = -orb.spin;

    uint32_t col = anim_hsv((int)(e_time * 60.0f) % 360, 230, 240);
    uint32_t halo = anim_lerp_rgb(col, 0x000000, 0.6f);
    uint32_t core = anim_lerp_rgb(col, 0xFFFFFF, 0.35f);

    anim_canvas_fill_circle(c, (int)orb.x, (int)orb.y, (int)orb.w / 2 + 8, halo);
    anim_canvas_fill_circle(c, (int)orb.x, (int)orb.y, (int)orb.w / 2, col);
    anim_canvas_fill_circle(c, (int)orb.x, (int)orb.y, (int)orb.w / 6, core);

    for (int i = 0; i < 2; i++) {
        sat_a[i] += 0.0028f * dt;
        float a = sat_a[i] + (i ? 3.14159f : 0.0f);
        int sx = (int)(orb.x + (float)cos((double)a) * orb.w * 1.6f);
        int sy = (int)(orb.y + (float)sin((double)a) * orb.w * 1.6f);
        anim_canvas_fill_circle(c, sx, sy, 4, palette[i]);
    }
}

static void fx_aurora(anim_canvas_t* c, float dt) {
    (void)dt;
    float t = e_time;
    int w = c->w, h = c->h;
    /* soft dark base with a faint vertical gradient */
    for (int y = 0; y < h; y++) {
        float yn = (float)y / (float)h;
        int base = (int)(8.0f + yn * 14.0f);
        for (int x = 0; x < w; x++) {
            /* three flowing ribbons whose centres drift over time */
            float ribbon = 0.0f;
            for (int k = 0; k < 3; k++) {
                float phase = t * (0.25f + 0.12f * k);
                float center = h * (0.30f + 0.22f * k)
                             + 26.0f * (float)sin((double)(x * 0.012f) + phase)
                             + 14.0f * (float)sin((double)(x * 0.03f) - phase * 1.7f);
                float dist = (float)y - center;
                float band = (float)exp(-(dist * dist) / (2.0f * 26.0f * 26.0f));
                float wave = 0.6f + 0.4f * (float)sin((double)(x * 0.02f + t * (0.6f + 0.2f * k)));
                ribbon += band * wave * (0.5f + 0.5f * (float)sin((double)(t * 0.5f + k)));
            }
            if (ribbon > 1.0f) ribbon = 1.0f;
            /* hue sweeps green -> cyan -> violet across the curtain */
            int hue = (int)(150.0f + 80.0f * (float)sin((double)(x * 0.006f + t * 0.4f)) + 60.0f * yn) % 360;
            if (hue < 0) hue += 360;
            uint32_t col = anim_hsv(hue, 200, (int)(base + ribbon * 210.0f));
            c->px[y * w + x] = col;
        }
    }
}

void saver_effect_update(float dt) {
    e_time += dt;
    anim_canvas_t* c = saver_canvas();
    if (!c->px) return;

    /* auto-cycle: advance through effects every saver_cycle seconds */
    if (saver_cycle > 0) {
        static uint32_t cycle_ms = 0;
        cycle_ms += (uint32_t)(dt * 1000.0f);
        if (cycle_ms >= (uint32_t)saver_cycle * 1000u) {
            cycle_ms = 0;
            saver_effect = (saver_effect + 1) % SAVER_EFFECTS;
            saver_effect_init();
            c = saver_canvas();
            if (!c->px) return;
        }
    }

    switch (saver_effect) {
        case SAVER_STARFIELD: fx_starfield(c, dt); break;
        case SAVER_MATRIX:    fx_matrix(c, dt);    break;
        case SAVER_PLASMA:    fx_plasma(c, dt);    break;
        case SAVER_AURORA:    fx_aurora(c, dt);    break;
        default:              fx_orbit(c, dt);     break;
    }
}

void saver_effect_init(void) {
    e_time = 0.0f;
    anim_canvas_t* c = saver_canvas();
    if (!c->px) return;
    anim_canvas_clear(c, 0x07090D);

    switch (saver_effect) {
        case SAVER_STARFIELD:
            for (int i = 0; i < NSTARS; i++) {
                stars[i].x = anim_randf(&rng_state, -1.0f, 1.0f);
                stars[i].y = anim_randf(&rng_state, -1.0f, 1.0f);
                stars[i].z = anim_randf(&rng_state, 0.06f, 1.0f);
            }
            break;
        case SAVER_MATRIX: {
            m_cols = c->w / 8;
            if (m_cols > MCOL_MAX) m_cols = MCOL_MAX;
            for (int i = 0; i < m_cols; i++) {
                m_head[i] = (float)(anim_rng32(&rng_state) % (c->h * 2)) - c->h;
                m_speed[i] = 0.08f + (float)(anim_rng32(&rng_state) % 140) / 1000.0f;
                m_glyph[i] = (unsigned char)m_chars[anim_rng32(&rng_state) % (sizeof(m_chars) - 1)];
            }
            break;
        }
        case SAVER_PLASMA:
            break;
        case SAVER_AURORA:
            break;
        default: {
            float r = (float)((c->w < c->h ? c->w : c->h)) * 0.05f;
            if (r < 14.0f) r = 14.0f;
            if (r > 40.0f) r = 40.0f;
            anim_sprite_init(&orb, c->w * 0.5f, c->h * 0.5f,
                             0.28f, 0.21f, r * 2.0f, r * 2.0f);
            orb.spin = 0.001f;
            orb.color = palette[0];
            sat_a[0] = 0.0f; sat_a[1] = 1.57f;
            break;
        }
    }
}

/* ===================== session control ===================== */

void screensaver_activate(void) {
    if (saver_active) return;
    saver_effect_init();
    saver_active = 1;
    saver_armed_at = timer_get_ms();   /* grace: ignore input briefly so the
                                         launching click can't wake it */
}

void screensaver_deactivate(void) {
    if (!saver_active) return;
    saver_active = 0;
    keyboard_drain();
}

void screensaver_frame(void) {
    saver_effect_update(anim_frame_dt());
    anim_canvas_t* c = saver_canvas();
    if (!c || !c->px) return;
    anim_canvas_present(c, 0, 0, (int)get_fb_width(), (int)get_fb_height());
}