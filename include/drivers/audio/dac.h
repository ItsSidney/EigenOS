/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

// ============================================================
//  Eigen — DAC Controller
//  Single source of truth for master volume + mute. Applies a
//  smooth volume gradient and routes samples to the active
//  output backend (PC speaker by default; AC97/HD Audio when
//  present). No floating point is used (kernel has no FPU).
// ============================================================
#ifndef DAC_H
#define DAC_H

#include <stdint.h>

/* A DAC output backend. The controller owns volume/mute; the
 * backend realizes them on real hardware. */
typedef struct dac_backend {
    const char* name;
    void (*play_tone)(uint32_t freq);   /* start/refresh a continuous tone */
    void (*stop)(void);                 /* silence output */
    void (*set_volume)(int vol);        /* 0-100, backend-specific */
    void (*set_muted)(int mute);
    void (*submit)(int16_t sample);     /* optional PCM streaming */
    void (*poll)(void);                 /* periodic PCM refill (polled DACs) */
} dac_backend_t;

void dac_init(void);
void dac_late_init(void);               /* call after pci_init() */
void dac_register_backend(const dac_backend_t* b);
const dac_backend_t* dac_active_backend(void);

void dac_set_volume(int volume);        /* 0-100 */
int  dac_get_volume(void);
void dac_set_muted(int mute);
int  dac_is_muted(void);

/* Volume gradient: map 0-100 -> 0..32767 amplitude scale. */
int dac_curve(int volume);
/* Apply the gradient to a single 16-bit sample. */
int16_t dac_scale_sample(int16_t s);

/* PCM streaming path (used by real DAC backends). */
void dac_submit(int16_t s);
void dac_flush(void);

/* Tone output (used by the engine's hardware sync). */
void dac_play_tone(uint32_t freq);
void dac_stop(void);

/* Shared PC-speaker PWM tone driver (used by backends). */
void dac_speaker_drive(uint32_t freq);

/* AC97/HD Audio DAC backend probe (defined in dac_ac97.c). */
void dac_ac97_init(void);

/* Intel HD Audio DAC backend probe (defined in dac_hda.c). */
void dac_hda_init(void);

#endif
