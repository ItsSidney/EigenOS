/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

// ============================================================
//  Eigen — DAC Controller Implementation
//  Owns master volume + mute, applies a smooth volume gradient,
//  and routes output to the active backend. The PC speaker is
//  the default backend; a real AC97/HD Audio DAC can register
//  itself via dac_register_backend().
// ============================================================
#include "drivers/audio/dac.h"
#include "drivers/audio/speaker.h"
#include "kernel/time/timer.h"
#include <stdint.h>

static int          g_dac_volume = 80;   /* 0-100 */
static int          g_dac_muted  = 0;
static const dac_backend_t* g_backend = 0;

/* PCM ring buffer for real DAC backends. */
#define DAC_RING_SZ 4096
static int16_t g_ring[DAC_RING_SZ];
static int g_ring_head = 0, g_ring_tail = 0;

/* ── PC Speaker backend ────────────────────────────────────
 * The PC speaker is a 1-bit output, so volume is realized as a
 * PWM duty cycle: the speaker is enabled for (volume%) of each
 * short PWM window. This yields a real, continuous volume
 * gradient instead of the old binary on/off behaviour. */
void dac_speaker_drive(uint32_t freq) {
    if (freq == 0) { speaker_stop(); return; }
    uint32_t now  = timer_get_ms();
    uint32_t duty = (uint32_t)g_dac_volume * 10 / 100;   /* 0..10 */
    if ((now % 10) < duty) speaker_play_freq(freq);
    else speaker_stop();
}
static void pcspk_play_tone(uint32_t freq) { dac_speaker_drive(freq); }
static void pcspk_stop(void)               { speaker_stop(); }
static dac_backend_t g_pcspk = {
    .name      = "PC Speaker",
    .play_tone = pcspk_play_tone,
    .stop      = pcspk_stop,
    .set_volume = 0,
    .set_muted  = 0,
    .submit     = 0,
};

void dac_register_backend(const dac_backend_t* b) { g_backend = b; }
const dac_backend_t* dac_active_backend(void)     { return g_backend; }

void dac_init(void) {
    g_dac_volume = 80;
    g_dac_muted  = 0;
    /* Keep a previously registered hardware DAC (e.g. AC97 probed
     * after boot) — only default to the PC speaker the first time. */
    if (g_backend == 0) g_backend = &g_pcspk;
    g_ring_head  = g_ring_tail = 0;
}

void dac_late_init(void) {
    /* PCI is now scanned; probe for a real audio DAC. */
    dac_ac97_init();
    dac_hda_init();   /* HD Audio takes priority on modern machines */
}

void dac_set_volume(int volume) {
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    g_dac_volume = volume;
    if (g_backend && g_backend->set_volume) g_backend->set_volume(volume);
}
int dac_get_volume(void) { return g_dac_volume; }

void dac_set_muted(int mute) {
    g_dac_muted = mute ? 1 : 0;
    if (g_dac_muted) dac_stop();
    else if (g_backend && g_backend->set_muted) g_backend->set_muted(mute);
}
int dac_is_muted(void) { return g_dac_muted; }

/* Linear, continuous gradient: volume% maps directly to amplitude%. */
int dac_curve(int volume) {
    if (volume <= 0) return 0;
    if (volume >= 100) return 32767;
    return (int)((int32_t)volume * 32767 / 100);
}

int16_t dac_scale_sample(int16_t s) {
    if (g_dac_muted) return 0;
    return (int16_t)(((int32_t)s * dac_curve(g_dac_volume)) / 32767);
}

void dac_submit(int16_t s) {
    g_ring[g_ring_head] = s;
    g_ring_head = (g_ring_head + 1) % DAC_RING_SZ;
    if (g_ring_head == g_ring_tail)            /* overwrite oldest */
        g_ring_tail = (g_ring_tail + 1) % DAC_RING_SZ;
}
void dac_flush(void) {
    if (!g_backend || !g_backend->submit) return;
    while (g_ring_tail != g_ring_head) {
        g_backend->submit(g_ring[g_ring_tail]);
        g_ring_tail = (g_ring_tail + 1) % DAC_RING_SZ;
    }
}

void dac_play_tone(uint32_t freq) {
    if (g_dac_muted || g_dac_volume == 0 || !g_backend) { dac_stop(); return; }
    g_backend->play_tone(freq);
}
void dac_stop(void) {
    if (g_backend) g_backend->stop();
}
