/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

// ============================================================
//  Eigen — ICH AC'97 Audio Controller
//  Full PCM playback backend for Intel ICH-series AC'97 native
//  audio controllers:
//
//   • Probes the PCI bus for an Intel AC'97 controller
//     (ICH0–ICH7, 440MX, …) and takes over as the output DAC
//   • Programs the AC'97 codec: cold reset, 48 kHz sample
//     rate, master/PCM volume, power-down register
//   • Streams 16-bit stereo PCM from the synth engine through
//     a self-looping PRD (Physical Region Descriptor) DMA chain
//   • Polled refill from the GUI loop — no IRQ required: the
//     just-consumed DMA buffer is re-mixed every frame, so the
//     engine's ADSR/LFO/volume chain drives the real speakers
//   • Volume/mute stay in the dac_ controller: the mixer
//     applies the gradient, the codec stays at 0 dB (mute bit)
//   • Falls back to doing nothing on machines without a
//     matching controller (PC speaker backend keeps working)
// ============================================================
#include "drivers/audio/dac.h"
#include "drivers/audio/audio.h"
#include "drivers/bus/pci.h"
#include "drivers/input/keyboard.h"
#include "kernel/mem/vmm.h"
#include "kernel/time/timer.h"
#include <stdint.h>

extern void boot_log_add(const char* tag, const char* msg, uint32_t tag_color, uint32_t msg_color);
extern void serial_puts(const char* s);

/* ── ICH AC'97 NAC registers (NABMBAR) ────────────────────── */
#define ICH_REG_X_CR      0x00   /* PCM out control        */
#define ICH_CR_RUN        0x0001 /* run                     */
#define ICH_CR_RESET      0x0002 /* channel reset           */
#define ICH_CR_FMT_16BIT  0x0008 /* 16-bit samples          */
#define ICH_CR_DAC1       0x0400 /* PCM out stream -> DAC1  */
#define ICH_REG_X_SR      0x02   /* status                  */
#define ICH_REG_X_IER     0x04   /* interrupt control       */
#define ICH_REG_X_ISR     0x06   /* interrupt status        */
#define ICH_REG_GLOB_STA  0x1A   /* global status           */
#define ICH_REG_GLOB_CNT  0x1C   /* global control          */
#define ICH_PCR_BASE      0x20   /* PRD table (phys addr)   */
#define ICH_PCR_CURR      0x24   /* current PRD address     */
#define ICH_PCR_NEXT      0x28   /* next PRD address        */
#define ICH_PCR_LAST      0x2A   /* last valid PRD address  */

/* ── AC'97 codec registers (NAM) ──────────────────────────── */
#define AC97_REG_RESET      0x00 /* bit1 reset, bit15 ready */
#define AC97_REG_MASTER     0x02 /* master volume           */
#define AC97_REG_PCM        0x18 /* PCM out volume          */
#define AC97_REG_POWERDOWN  0x26 /* 0x0000 = all powered up */
#define AC97_REG_FRONT_RATE 0x2C /* PCM front DAC rate      */
#define AC97_RATE_48K       0xBB80

/* ── DMA geometry ─────────────────────────────────────────── */
#define AC97_BUFS        4
#define AC97_BUF_BYTES   8192                     /* per PRD buffer       */
#define AC97_BUF_FRAMES  (AC97_BUF_BYTES / 4)     /* stereo 16-bit frames */
#define AC97_PRDA_EOL    0x8000                   /* PRD flags: EOL       */

typedef struct {
    uint32_t addr;      /* buffer physical address       */
    uint16_t size;      /* buffer size in bytes          */
    uint16_t flags;     /* bit15 = end of list           */
} __attribute__((packed)) ac97_prd_t;

static int      g_present = 0;
static uint16_t g_nam  = 0;   /* codec (mixer) IO base  */
static uint16_t g_nabm = 0;   /* PCM channel IO base    */
static int      g_running = 0;
static int      g_prev_cur = -1;

static int16_t ac97_bufs[AC97_BUFS][AC97_BUF_FRAMES * 2] __attribute__((aligned(64)));
static ac97_prd_t ac97_prd[AC97_BUFS] __attribute__((aligned(16)));
static int ac97_consumed[AC97_BUFS];

static inline void outw(uint16_t port, uint16_t val) { port_word_out(port, val); }
static inline uint16_t inw(uint16_t port) { return port_word_in(port); }
static inline void outl(uint16_t port, uint32_t val) { port_long_out(port, val); }
static inline uint32_t inl(uint16_t port) { return port_long_in(port); }

/* Intel AC'97 native-audio controllers we can drive. */
static int is_ac97(uint16_t vid, uint16_t did) {
    if (vid != 0x8086) return 0;
    switch (did) {
        case 0x2415: case 0x2425: case 0x2445: case 0x2485:
        case 0x24C5: case 0x24D5: case 0x266E: case 0x27DE:
        case 0x7195: case 0x2410: case 0x2440: case 0x2480:
        case 0x24C0: case 0x27C0: case 0x2482:
            return 1;
    }
    return 0;
}

/* ── Codec access ─────────────────────────────────────────── */
static void codec_write(uint8_t reg, uint16_t val) {
    if (g_nam) outw((uint16_t)(g_nam + reg), val);
}
static uint16_t codec_read(uint8_t reg) {
    return g_nam ? inw((uint16_t)(g_nam + reg)) : 0xFFFF;
}

/* Bounded busy-wait. dac_late_init() runs before the kernel
 * enables interrupts, so timer_get_ms() never advances there —
 * all waits in this driver must be plain CPU loops. */
static void delay_busy(uint32_t iters) {
    for (volatile uint32_t i = 0; i < iters; i++)
        __asm__ volatile("pause");
}

static int codec_init(void) {
    /* Cold reset, then wait for codec-ready (CRDY, bit 15). */
    codec_write(AC97_REG_RESET, 0x0002);
    uint32_t spins = 0;
    while (!(codec_read(AC97_REG_RESET) & 0x8000)) {
        if (++spins > 2000000) return 0;   /* no hang if codec absent */
        __asm__ volatile("pause");
    }
    codec_write(AC97_REG_MASTER,     0x0000);   /* 0 dB, unmuted  */
    codec_write(AC97_REG_PCM,        0x0000);   /* PCM out 0 dB   */
    codec_write(AC97_REG_POWERDOWN,  0x0000);   /* everything on  */
    codec_write(AC97_REG_FRONT_RATE, AC97_RATE_48K);
    return 1;
}

/* ── DMA refill ───────────────────────────────────────────── */
static void ac97_fill(int idx) {
    int16_t* p = ac97_bufs[idx];
    for (int i = 0; i < AC97_BUF_FRAMES; i++) {
        int16_t s = audio_mix_next_sample();   /* mono synth mix  */
        p[i * 2]     = s;                       /* duplicate to L  */
        p[i * 2 + 1] = s;                       /* and R           */
    }
    ac97_consumed[idx] = 0;
}

static int dma_start(void) {
    for (int i = 0; i < AC97_BUFS; i++) {
        ac97_prd[i].addr  = (uint32_t)vmm_get_phys((uint64_t)ac97_bufs[i]);
        ac97_prd[i].size  = AC97_BUF_BYTES;
        ac97_prd[i].flags = (i == AC97_BUFS - 1) ? AC97_PRDA_EOL : 0;
        ac97_consumed[i]  = 1;
        ac97_fill(i);
    }
    uint32_t prd_phys = (uint32_t)vmm_get_phys((uint64_t)ac97_prd);

    /* Channel reset */
    outw(g_nabm + ICH_REG_X_CR, ICH_CR_RESET);
    delay_busy(200000);   /* let reset assert (no timer dependency) */
    outw(g_nabm + ICH_REG_X_CR, 0);

    /* Program PRD base + format, then run */
    outl(g_nabm + ICH_PCR_BASE, prd_phys);
    outw(g_nabm + ICH_REG_X_CR, ICH_CR_FMT_16BIT | ICH_CR_DAC1);
    outw(g_nabm + ICH_REG_X_CR, ICH_CR_RUN | ICH_CR_FMT_16BIT | ICH_CR_DAC1);
    g_running = 1;
    g_prev_cur = -1;
    return 1;
}

/* Polled refill: keep every buffer except the one currently
 * playing full of freshly mixed audio. */
static void ac97_poll(void) {
    if (!g_present || !g_running) return;

    uint32_t cur_addr = inl(g_nabm + ICH_PCR_CURR) & ~0xF;
    int cur = -1;
    for (int i = 0; i < AC97_BUFS; i++)
        if ((uint32_t)ac97_prd[i].addr == cur_addr) { cur = i; break; }
    if (cur < 0) return;

    /* The buffer that was playing before is now consumed. */
    if (g_prev_cur >= 0 && cur != g_prev_cur)
        ac97_consumed[g_prev_cur] = 1;
    g_prev_cur = cur;

    for (int i = 0; i < AC97_BUFS; i++)
        if (i != cur && ac97_consumed[i]) ac97_fill(i);
}

/* ── Backend glue ─────────────────────────────────────────── */
static void ac97_play_tone(uint32_t freq) {
    (void)freq;   /* PCM stream carries the audio; nothing to do */
}
static void ac97_stop(void) {
    /* Keep DMA running — the mixer emits silence when muted. */
}

static void ac97_set_volume(int vol) {
    (void)vol;    /* mixer gradient (dac_scale_sample) owns volume */
    uint16_t v = dac_is_muted() ? 0x8000 : 0x0000;
    codec_write(AC97_REG_MASTER, v);
    codec_write(AC97_REG_PCM,    v);
}
static void ac97_set_muted(int mute) {
    (void)mute;
    ac97_set_volume(dac_get_volume());
}

static dac_backend_t g_ac97 = {
    .name       = "ICH AC97 PCM",
    .play_tone  = ac97_play_tone,
    .stop       = ac97_stop,
    .set_volume = ac97_set_volume,
    .set_muted  = ac97_set_muted,
    .submit     = 0,
    .poll       = ac97_poll,
};

void dac_ac97_init(void) {
    int n = pci_get_device_count();
    for (int i = 0; i < n; i++) {
        pci_device_t* d = pci_get_device(i);
        if (!d) continue;
        if (d->class_id != 0x04 || d->subclass != 0x01) continue;  /* audio */
        if (!is_ac97(d->vendor_id, d->device_id)) continue;

        /* All ICH parts map NAM at BAR0 and NABMBAR at BAR1;
         * the 440MX kept them at BAR5/BAR4. */
        int nam_bar  = (d->device_id == 0x7195) ? 5 : 0;
        int nabm_bar = (d->device_id == 0x7195) ? 4 : 1;
        uint32_t nam  = d->bar[nam_bar] & 0xFFFC;
        uint32_t nabm = d->bar[nabm_bar] & 0xFFFC;
        if ((d->bar[nam_bar] & 0x1) == 0 || nam == 0) continue;
        if ((d->bar[nabm_bar] & 0x1) == 0 || nabm == 0) continue;

        /* Enable IO space + bus mastering. */
        uint32_t cmd = pci_config_read(d->bus, d->slot, d->func, 0x04);
        pci_config_write(d->bus, d->slot, d->func, 0x04, cmd | 0x7);

        g_nam  = (uint16_t)nam;
        g_nabm = (uint16_t)nabm;

        if (!codec_init()) { g_nam = 0; g_nabm = 0; continue; }
        if (!dma_start())  { g_nam = 0; g_nabm = 0; continue; }

        g_present = 1;
        dac_register_backend(&g_ac97);
        serial_puts("[AC97] ICH AC'97 PCM audio ready (48 kHz)\n");
        boot_log_add("AC97", "ICH AC'97 PCM audio ready (48 kHz)", 0x58A6FF, 0x34D399);
        return;
    }
}
