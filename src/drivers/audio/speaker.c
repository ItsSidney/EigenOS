/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

// ============================================================
//  Eigen — PC Speaker Advanced Driver
//  Uses PIT Channel 2 for frequency-accurate notes
// ============================================================
#include "drivers/audio/audio.h"
#include <stdint.h>

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %w1" : : "a"(val), "d"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %w1, %0" : "=a"(ret) : "d"(port));
    return ret;
}

void speaker_play_freq(uint32_t freq) {
    if (freq == 0) {
        outb(0x61, inb(0x61) & 0xFC);
        return;
    }
    
    uint32_t div = 1193180 / freq;
    outb(0x43, 0xB6);
    outb(0x42, (uint8_t)div);
    outb(0x42, (uint8_t)(div >> 8));
    
    uint8_t tmp = inb(0x61);
    if ((tmp & 3) != 3) {
        outb(0x61, tmp | 3);
    }
}

void speaker_stop() {
    outb(0x61, inb(0x61) & 0xFC);
}

void play_beep() {
    speaker_play_freq(1000);
    for (volatile uint32_t i = 0; i < 15000000; i++);
    speaker_stop();
}

// Integration with Audio Engine:
// Since we don't have a DAC (like AC97) yet, we map the first active
// channel of the engine to the physical speaker.
// audio_hardware_sync is implemented in engine.c and selects the
// dominant active channel to drive the PC speaker.
