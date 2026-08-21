/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#include "kernel/time/timer.h"
#include "drivers/input/keyboard.h"
#include "drivers/time/rtc.h"
#include "drivers/audio/audio.h"

extern unsigned char read_cmos(unsigned char reg);
extern void splash_anim_advance(void);

volatile uint64_t timer_ticks = 0;
volatile int timer_detected = 0;
uint32_t timer_hz = 1000;

void timer_handler(void) {
    timer_ticks++;
    /* Keep the splash loading dots/bar moving even between boot milestones. */
    splash_anim_advance();
    /* Audio is serviced on a steady 1 ms cadence here, NOT from the GUI loop.
     * The AC'97 DMA refills on demand (only when a buffer is consumed), so the
     * per-tick cost is a few port reads when audio is idle and one 2048-sample
     * synth burst (~23x/sec at 48 kHz) when it is — both negligible. Decoupling
     * from the (variable) desktop redraw rate is what eliminates the
     * "plays on dots" buffer-underrun choppiness. Single caller => no synth
     * phase/lfo race with the main thread. */
    audio_update();
    audio_hardware_sync();
}

void init_timer(uint32_t freq) {
    uint32_t divisor = 1193180 / freq;
    port_byte_out(0x43, 0x36);
    port_byte_out(0x40, (uint8_t)(divisor & 0xFF));
    port_byte_out(0x40, (uint8_t)((divisor >> 8) & 0xFF));
    timer_hz = freq;
    timer_detected = 1;
}

void sleep_ms(uint32_t ms) {
    if (!timer_detected) {
        uint64_t start = timer_ticks;
        for (volatile long i = 0; i < 100000L; i++) {
            if (timer_ticks != start) break;
        }
        timer_detected = 1;
    }

    uint64_t end_ticks = timer_ticks + (uint64_t)ms * timer_hz / 1000;
    if (end_ticks == timer_ticks) end_ticks++;
    while (timer_ticks < end_ticks) {
        __asm__ volatile("hlt");
    }
}

uint32_t timer_get_ms(void) {
    return (uint32_t)timer_ticks;
}

void timer_calibrate(void) {
    // Fast calibration over ~10ms using RTC update cycle
    while (is_updating());
    unsigned char base = read_cmos(0x00);
    do { while (is_updating()); } while (read_cmos(0x00) == base);
    uint64_t start = timer_ticks;
    uint64_t end = start + (timer_hz / 100);  // aim for ~10ms
    while (timer_ticks < end) { __asm__ volatile("pause"); }
    timer_detected = 1;
}
