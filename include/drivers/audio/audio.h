/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

// ============================================================
//  Eigen — High-Resolution Sound Engine
//  Supports 48kHz synthesis and multi-channel playback
// ============================================================
#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>

#define AUDIO_SAMPLE_RATE 48000
#define AUDIO_MAX_CHANNELS 8

typedef enum {
    WAVE_SINE,
    WAVE_SQUARE,
    WAVE_SAWTOOTH,
    WAVE_TRIANGLE,
    WAVE_NOISE
} wave_type_t;

extern const int8_t sine_table[256];

// Note frequencies (C4 to B5)
#define NOTE_C4 261
#define NOTE_D4 293
#define NOTE_E4 329
#define NOTE_F4 349
#define NOTE_G4 392
#define NOTE_A4 440
#define NOTE_B4 493
#define NOTE_C5 523
#define NOTE_D5 587
#define NOTE_E5 659
#define NOTE_F5 698
#define NOTE_G5 784
#define NOTE_A5 880
#define NOTE_B5 987

typedef struct {
    int active;
    wave_type_t type;
    uint32_t frequency; // in Hz
    uint32_t volume;    // 0-255
    uint32_t phase;     // Fixed-point phase
    uint32_t duration_ms;
    uint32_t start_ticks;
    
    // ADSR Envelope
    uint32_t attack_ms, decay_ms, sustain_vol, release_ms;
    int state; // 0:Attack, 1:Decay, 2:Sustain, 3:Release, 4:Off
    uint32_t state_start;
    
    // LFO Modulation
    uint32_t lfo_freq; // in Hz
    uint32_t lfo_depth;
    uint32_t lfo_phase;
} audio_channel_t;

// ── Audio Engine API ────────────────────────────────────────

void audio_init(void);
void audio_set_mute(int mute);
int audio_is_muted(void);
void audio_update(void);
uint32_t audio_get_note_freq(int note_num); // 1-9 for C Major scale
void audio_play_note(int channel, wave_type_t type, uint32_t freq, uint32_t vol, uint32_t duration_ms);
void audio_set_adsr(int channel, uint32_t a, uint32_t d, uint32_t s, uint32_t r);
void audio_set_lfo(int channel, uint32_t freq, uint32_t depth);
void audio_stop_channel(int channel);

// ── Low-Level Driver Hooks ──────────────────────────────────
// Called by the timer or a dedicated audio interrupt
int16_t audio_mix_next_sample(void);

// Master Volume
void audio_set_master_volume(int volume); // 0-100
int audio_get_master_volume(void);

// Hardware sync: drive PC speaker from active channel state
void audio_hardware_sync(void);

// ── Testing ─────────────────────────────────────────────────
void audio_test_sequence(void);

#endif
