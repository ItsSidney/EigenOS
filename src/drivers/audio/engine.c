/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

// ============================================================
//  Eigen — Sound Engine Implementation
//  Advanced 48kHz Synthesis with ADSR and LFO
// ============================================================
#ifndef AUDIO_H
#include "drivers/audio/audio.h"
#endif
#include "drivers/audio/dac.h"
#include "kernel/time/timer.h"

static audio_channel_t g_channels[AUDIO_MAX_CHANNELS];

const int8_t sine_table[256] = {
    0, 3, 6, 9, 12, 15, 18, 21, 24, 28, 31, 34, 37, 40, 43, 46,
    48, 51, 54, 57, 60, 63, 65, 68, 71, 73, 76, 78, 81, 83, 85, 88,
    90, 92, 94, 96, 98, 100, 102, 104, 106, 107, 109, 111, 112, 113, 115, 116,
    117, 118, 120, 121, 122, 122, 123, 124, 125, 125, 126, 126, 126, 127, 127, 127,
    127, 127, 127, 127, 126, 126, 126, 125, 125, 124, 123, 122, 122, 121, 120, 118,
    117, 116, 115, 113, 112, 111, 109, 107, 106, 104, 102, 100, 98, 96, 94, 92,
    90, 88, 85, 83, 81, 78, 76, 73, 71, 68, 65, 63, 60, 57, 54, 51,
    48, 46, 43, 40, 37, 34, 31, 28, 24, 21, 18, 15, 12, 9, 6, 3,
    0, -3, -6, -9, -12, -15, -18, -21, -24, -28, -31, -34, -37, -40, -43, -46,
    -48, -51, -54, -57, -60, -63, -65, -68, -71, -73, -76, -78, -81, -83, -85, -88,
    -90, -92, -94, -96, -98, -100, -102, -104, -106, -107, -109, -111, -112, -113, -115, -116,
    -117, -118, -120, -121, -122, -122, -123, -124, -125, -125, -126, -126, -126, -127, -127, -127,
    -127, -127, -127, -127, -126, -126, -126, -125, -125, -124, -123, -122, -122, -121, -120, -118,
    -117, -116, -115, -113, -112, -111, -109, -107, -106, -104, -102, -100, -108, -96, -94, -92,
    -90, -88, -85, -83, -81, -78, -76, -73, -71, -68, -65, -63, -60, -57, -54, -51,
    -48, -46, -43, -40, -37, -34, -31, -28, -24, -21, -18, -15, -12, -9, -6, -3
};

static uint32_t audio_seed = 0x1234;
static int16_t audio_rand() {
    audio_seed = (audio_seed * 1103515245 + 12345) & 0x7fffffff;
    return (int16_t)(audio_seed & 0xFFFF);
}

void audio_init(void) {
    dac_init();
    for (int i = 0; i < AUDIO_MAX_CHANNELS; i++) {
        g_channels[i].active = 0;
        g_channels[i].attack_ms = 10;
        g_channels[i].decay_ms = 50;
        g_channels[i].sustain_vol = 128;
        g_channels[i].release_ms = 100;
        g_channels[i].lfo_freq = 0;
    }
}

void audio_set_mute(int mute) { dac_set_muted(mute); }
int audio_is_muted() { return dac_is_muted(); }

void audio_set_master_volume(int volume) { dac_set_volume(volume); }
int audio_get_master_volume(void) { return dac_get_volume(); }

uint32_t audio_get_note_freq(int note_num) {
    static const uint32_t scale[] = { NOTE_C4, NOTE_D4, NOTE_E4, NOTE_F4, NOTE_G4, NOTE_A4, NOTE_B4, NOTE_C5, NOTE_D5 };
    if (note_num >= 1 && note_num <= 9) return scale[note_num - 1];
    return 0;
}

void audio_set_adsr(int channel, uint32_t a, uint32_t d, uint32_t s, uint32_t r) {
    if (channel >= 0 && channel < AUDIO_MAX_CHANNELS) {
        g_channels[channel].attack_ms = a;
        g_channels[channel].decay_ms = d;
        g_channels[channel].sustain_vol = s;
        g_channels[channel].release_ms = r;
    }
}

void audio_set_lfo(int channel, uint32_t freq, uint32_t depth) {
    if (channel >= 0 && channel < AUDIO_MAX_CHANNELS) {
        g_channels[channel].lfo_freq = freq;
        g_channels[channel].lfo_depth = depth;
    }
}

void audio_play_note(int channel, wave_type_t type, uint32_t freq, uint32_t vol, uint32_t duration_ms) {
    if (channel < 0 || channel >= AUDIO_MAX_CHANNELS) return;
    g_channels[channel].type = type;
    g_channels[channel].frequency = freq;
    g_channels[channel].volume = vol;
    g_channels[channel].duration_ms = duration_ms;
    g_channels[channel].start_ticks = timer_get_ms();
    g_channels[channel].state_start = g_channels[channel].start_ticks;
    g_channels[channel].state = 0; // Attack
    g_channels[channel].phase = 0;
    g_channels[channel].lfo_phase = 0;
    g_channels[channel].active = 1;
}

void audio_stop_channel(int channel) {
    if (channel >= 0 && channel < AUDIO_MAX_CHANNELS) {
        if (g_channels[channel].state != 3) {
            g_channels[channel].state = 3; // Go to Release
            g_channels[channel].state_start = timer_get_ms();
        }
    }
}

void audio_update() {
    uint32_t now = timer_get_ms();
    int any_active = 0;
    for (int i = 0; i < AUDIO_MAX_CHANNELS; i++) {
        if (!g_channels[i].active) continue;
        uint32_t elapsed = now - g_channels[i].state_start;
        
        switch (g_channels[i].state) {
            case 0: // Attack
                if (elapsed >= g_channels[i].attack_ms) {
                    g_channels[i].state = 1; g_channels[i].state_start = now;
                }
                break;
            case 1: // Decay
                if (elapsed >= g_channels[i].decay_ms) {
                    g_channels[i].state = 2; g_channels[i].state_start = now;
                }
                break;
            case 2: // Sustain
                if (g_channels[i].duration_ms > 0 && (now - g_channels[i].start_ticks) > g_channels[i].duration_ms) {
                    g_channels[i].state = 3; g_channels[i].state_start = now;
                }
                break;
            case 3: // Release
                if (elapsed >= g_channels[i].release_ms) {
                    g_channels[i].active = 0;
                }
                break;
        }
        if (g_channels[i].active) any_active = 1;
    }
}

int16_t audio_mix_next_sample(void) {
    if (dac_is_muted()) return 0;
    int32_t mixed = 0;
    uint32_t now = timer_get_ms();
    
    for (int i = 0; i < AUDIO_MAX_CHANNELS; i++) {
        if (!g_channels[i].active) continue;
        
        // ADSR Gain Calculation
        uint32_t gain = 256;
        uint32_t elapsed = now - g_channels[i].state_start;
        switch (g_channels[i].state) {
            case 0: gain = (elapsed * 256) / (g_channels[i].attack_ms + 1); break;
            case 1: gain = 256 - ((elapsed * (256 - g_channels[i].sustain_vol)) / (g_channels[i].decay_ms + 1)); break;
            case 2: gain = g_channels[i].sustain_vol; break;
            case 3: gain = g_channels[i].sustain_vol - ((elapsed * g_channels[i].sustain_vol) / (g_channels[i].release_ms + 1)); break;
        }

        // LFO Modulation (Frequency)
        int32_t freq = (int32_t)g_channels[i].frequency;
        if (g_channels[i].lfo_freq > 0) {
            int8_t lfo_val = sine_table[(g_channels[i].lfo_phase >> 16) & 0xFF];
            freq += (lfo_val * (int)g_channels[i].lfo_depth) >> 8;
            g_channels[i].lfo_phase += (uint32_t)(((uint64_t)g_channels[i].lfo_freq << 24) / AUDIO_SAMPLE_RATE);
        }
        if (freq < 1) freq = 1;            /* guard against LFO underflow wrap */

        int16_t sample = 0;
        uint32_t phase_idx = (g_channels[i].phase >> 16) & 0xFF;
        switch (g_channels[i].type) {
            case WAVE_SINE: sample = sine_table[phase_idx] << 8; break;
            case WAVE_SQUARE: sample = (phase_idx < 128) ? 32767 : -32768; break;
            case WAVE_SAWTOOTH: sample = (int16_t)((phase_idx << 8) - 32768); break;
            case WAVE_TRIANGLE: 
                if (phase_idx < 128) sample = (int16_t)((phase_idx << 9) - 32768);
                else sample = (int16_t)(32767 - ((phase_idx - 128) << 9));
                break;
            case WAVE_NOISE: sample = audio_rand(); break;
        }
        
        mixed += (sample * (int)g_channels[i].volume * (int)gain) >> 16;
        g_channels[i].phase += (uint32_t)(((uint64_t)(uint32_t)freq << 24) / AUDIO_SAMPLE_RATE);
    }
    
    if (mixed > 32767) mixed = 32767; else if (mixed < -32768) mixed = -32768;
    return dac_scale_sample((int16_t)mixed);
}

// ── Hardware Sync ─────────────────────────────────────────────
// Called from the main GUI loop to drive the output hardware from
// the synthesizer. Polled PCM backends (AC97) refill their DMA
// buffers here; the PC speaker is driven from the active channel
// the classic way.
void audio_hardware_sync(void) {
    const dac_backend_t* b = dac_active_backend();
    if (b && b->poll) {
        b->poll();
        return;
    }

    if (dac_is_muted() || dac_get_volume() == 0) {
        dac_stop();
        return;
    }

    int best = -1;
    uint32_t best_time = 0;
    for (int i = 0; i < AUDIO_MAX_CHANNELS; i++) {
        if (!g_channels[i].active) continue;
        if (best < 0 || g_channels[i].start_ticks > best_time) {
            best = i;
            best_time = g_channels[i].start_ticks;
        }
    }

    if (best < 0) {
        dac_stop();
    } else {
        dac_play_tone(g_channels[best].frequency);
    }
}
