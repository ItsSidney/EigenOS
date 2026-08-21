/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

// ============================================================
//  Eigen — Intel HD Audio (Azalia) Controller
//  Full PCM playback backend for modern Intel HD Audio
//  controllers (ICH6/ICH9/ICH10, PCH, Ice Lake "SST"…):
//
//   • Probes the PCI bus for a class 04/03 HD Audio
//     controller (EliteBook I219/I225-generation machines,
//     any Azalia) and takes over as the output DAC
//   • Resets the controller, programs CORB/RIRB DMA rings
//     and speaks codec verbs through them (no IRQ: the
//     RIRB status bit is polled and cleared, exactly like
//     the hardware's response-interrupt handshake)
//   • Scans the codec widget graph: finds the first Audio
//     Output converter (DAC), unmutes its output amplifier
//     at 0 dB, and enables every analogue output pin
//     (internal speaker / headphone) + EAPD where present
//   • Streams 16-bit stereo 48 kHz PCM from the synth
//     engine through a self-looping BDL DMA chain
//   • Polled refill from the GUI loop (LPIB based) — no
//     IRQ required, mirrors the AC'97 backend design
//   • Volume/mute stay in the dac_ controller: the mixer
//     applies the gradient, the codec stays at 0 dB
//   • Falls back to doing nothing on machines without a
//     matching controller (PC speaker backend keeps working)
// ============================================================
#include "drivers/audio/dac.h"
#include "drivers/audio/audio.h"
#include "drivers/bus/pci.h"
#include "kernel/mem/vmm.h"
#include <stdint.h>

extern void boot_log_add(const char* tag, const char* msg, uint32_t tag_color, uint32_t msg_color);
extern void serial_puts(const char* s);

/* ── MMIO mapping ─────────────────────────────────────────── */
#define HDA_MMIO_VIRT   0xFFFFB00000000000ULL
#define HDA_MMIO_SIZE   0x4000        /* 16 KB register window */

/* ── Global registers ─────────────────────────────────────── */
#define REG_GCAP        0x00          /* capabilities            */
#define   GCAP_OSS      (0x0F << 12)  /* output streams          */
#define REG_GCTL        0x08
#define   GCTL_RESET    (1 << 0)      /* controller reset        */
#define   GCTL_UNSOL    (1 << 8)      /* accept unsolicited      */
#define REG_WAKEEN      0x0C
#define REG_STATESTS    0x0E          /* SD0-SD3 codec presence  */
#define REG_INTCTL      0x20
#define REG_INTSTS      0x24

/* ── CORB (command ring) ──────────────────────────────────── */
#define REG_CORBLBASE   0x40
#define REG_CORBUBASE   0x44
#define REG_CORBWP      0x48
#define REG_CORBRP      0x4A
#define REG_CORBCTL     0x4C
#define   CORBCTL_RUN   (1 << 1)
#define REG_CORBSTS     0x4D
#define REG_CORBSIZE    0x4E

/* ── RIRB (response ring) ─────────────────────────────────── */
#define REG_RIRBLBASE   0x50
#define REG_RIRBUBASE   0x54
#define REG_RIRBWP      0x58
#define REG_RINTCNT     0x5A
#define REG_RIRBCTL     0x5C
#define   RIRBCTL_IRQEN (1 << 0)      /* response interrupt en  */
#define   RIRBCTL_DMAEN (1 << 1)      /* RIRB DMA enable        */
#define REG_RIRBSTS     0x5D
#define   RIRBSTS_IRQ   (1 << 0)      /* response interrupt     */
#define   RIRBSTS_OVRN  (1 << 2)      /* overrun                */
#define REG_RIRBSIZE    0x5E

/* ── Stream descriptors ─────────────────────────────────────
 * Intel layout: SD0-SD3 are the INPUT streams at 0x80-0xE0,
 * SD4-SD7 are the OUTPUT streams at 0x100-0x160. */
#define SD_BASE(n)      (0x80 + (n) * 0x20)
#define SD_OUT0         SD_BASE(4)    /* first output stream   */
#define SD_CTL          0x00
#define   SD_CTL_RESET  0x01
#define   SD_CTL_RUN    0x02
#define   SD_CTL_TAG(t) (((t) & 0xF) << 20)   /* stream tag   */
#define SD_STS          0x03
#define   SD_STS_FIFORDY 0x20
#define SD_LPIB         0x04
#define SD_CBL          0x08
#define SD_LVI          0x0C
#define SD_FIFOSIZE     0x10
#define SD_FORMAT       0x12
#define SD_BDLPL        0x18
#define SD_BDLPU        0x1C

/* 16-bit stereo 48 kHz PCM (format register value). */
#define HDA_FORMAT_PCM16_STEREO_48K  0x0011

/* ── Codec verbs ──────────────────────────────────────────── */
#define AC_VERB_PARAMETERS           0x0F00
#define AC_VERB_SET_STREAM_FORMAT    0x0200
#define AC_VERB_SET_AMP_GAIN_MUTE    0x0300
#define AC_VERB_SET_POWER_STATE      0x0705
#define AC_VERB_SET_CHANNEL_STREAMID 0x0706
#define AC_VERB_SET_PIN_WIDGET_CTRL  0x0707
#define AC_VERB_SET_EAPD             0x070C
#define AC_VERB_GET_CONFIG_DEFAULT   0x0F1C

#define AC_PAR_VENDOR_ID       0x00
#define AC_PAR_SUBSYSTEM_ID    0x01
#define AC_PAR_REV_ID          0x02
#define AC_PAR_NODE_COUNT      0x04
#define AC_PAR_FUNCTION_TYPE   0x05
#define AC_PAR_AUDIO_WIDGET_CAP 0x09
#define AC_PAR_PIN_CAP         0x0C
#define AC_PAR_AMP_OUT_CAP     0x12

#define AC_WIDGET_TYPE         (0xF << 20)
#define AC_WID_AUD_OUT         0x0
#define AC_WID_PIN             0x4

#define AC_PINCAP_OUT          (1 << 4)
#define AC_PINCAP_EAPD         (1 << 16)

#define AC_DEFCFG_DEVICE       (0xF << 20)
#define AC_DEVTYPE_LINE_OUT    0x0
#define AC_DEVTYPE_SPEAKER     0x1
#define AC_DEVTYPE_HP_OUT      0x2
#define AC_DEVTYPE_SPDIF_OUT   0x4
#define AC_DEVTYPE_DIG_OTHER   0x5
#define AC_DEVTYPE_SPDIF_IN    0xC
#define AC_DEVTYPE_DIG_IN_OTH  0xD

#define AC_AMP_SET_OUTPUT      (1 << 15)
#define AC_AMP_SET_LEFT        (1 << 13)
#define AC_AMP_SET_RIGHT       (1 << 12)
#define AC_AMP_GAIN_MASK       0x7F

#define AC_PINCTL_OUT_EN       0x40

/* Amp capabilities (AC_PAR_AMP_OUT_CAP). */
#define AC_AMPCAP_OFFSET       0x7F
#define AC_AMPCAP_NUM_STEPS    (0x7F << 8)
#define AC_AMPCAP_MUTE         (1 << 31)

/* ── DMA geometry ─────────────────────────────────────────── */
#define HDA_BDL_ENTRIES  8
#define HDA_ENTRY_FRAMES 256                        /* per BDL buffer   */
#define HDA_ENTRY_BYTES  (HDA_ENTRY_FRAMES * 4)     /* stereo 16-bit    */
#define HDA_CBL_BYTES    (HDA_BDL_ENTRIES * HDA_ENTRY_BYTES)
#define HDA_RING_ENTRIES 256                        /* CORB/RIRB slots  */

typedef struct {
    uint64_t addr;      /* buffer physical address       */
    uint32_t len;       /* buffer size in bytes          */
    uint32_t ioc;       /* bit0 = interrupt on complete  */
} __attribute__((packed)) hda_bdle_t;

static int g_present = 0;
static int g_running = 0;
static int g_prev_cur = -1;
static int g_consumed[HDA_BDL_ENTRIES];
static int g_codec_cad = -1;
static int g_dac_nid = 2;
static uint32_t g_amp_gain = 0;
static int g_pins[HDA_BDL_ENTRIES];
static int g_npins = 0;

static int16_t hda_bufs[HDA_BDL_ENTRIES][HDA_ENTRY_FRAMES * 2] __attribute__((aligned(128)));
static hda_bdle_t hda_bdl[HDA_BDL_ENTRIES] __attribute__((aligned(128)));
static uint32_t hda_corb[HDA_RING_ENTRIES] __attribute__((aligned(128)));
static uint32_t hda_rirb[HDA_RING_ENTRIES * 2] __attribute__((aligned(128)));

/* ── MMIO accessors ───────────────────────────────────────── */
static inline uint32_t hda_in32(uint32_t off) {
    return *(volatile uint32_t*)(HDA_MMIO_VIRT + off);
}
static inline uint16_t hda_in16(uint32_t off) {
    return *(volatile uint16_t*)(HDA_MMIO_VIRT + off);
}
static inline uint8_t hda_in8(uint32_t off) {
    return *(volatile uint8_t*)(HDA_MMIO_VIRT + off);
}
static inline void hda_out32(uint32_t off, uint32_t val) {
    *(volatile uint32_t*)(HDA_MMIO_VIRT + off) = val;
}
static inline void hda_out16(uint32_t off, uint16_t val) {
    *(volatile uint16_t*)(HDA_MMIO_VIRT + off) = val;
}
static inline void hda_out8(uint32_t off, uint8_t val) {
    *(volatile uint8_t*)(HDA_MMIO_VIRT + off) = val;
}

/* Bounded busy-wait. dac_late_init() runs before the kernel
 * enables interrupts, so timer_get_ms() never advances there —
 * all waits in this driver must be plain CPU loops. */
static void delay_busy(uint32_t iters) {
    for (volatile uint32_t i = 0; i < iters; i++)
        __asm__ volatile("pause");
}

static void hda_hex(uint32_t v) {
    char buf[9];
    static const char hx[] = "0123456789ABCDEF";
    for (int i = 0; i < 8; i++) { buf[i] = hx[(v >> (28 - i * 4)) & 0xF]; }
    buf[8] = 0;
    serial_puts(buf);
}

/* ── Codec verb over CORB/RIRB ──────────────────────────────
 * Polled: waits for the RIRB write pointer to advance, then
 * clears RIRBSTS.IRQ — QEMU's ICH6 model stops consuming CORB
 * until that status bit is acknowledged, and the real hardware
 * handshake is the same. Returns 0xFFFFFFFF on timeout. */
static uint32_t hda_verb(uint8_t cad, uint8_t nid, uint32_t verb, uint32_t payload) {
    uint32_t rp, wp, rwp, spins;

    /* Wait for a free CORB slot (ring of 256). */
    spins = 0;
    for (;;) {
        rp = hda_in16(REG_CORBRP) & 0xFF;
        wp = hda_in16(REG_CORBWP) & 0xFF;
        if (((wp + 1) & 0xFF) != rp) break;
        if (++spins > 2000000) return 0xFFFFFFFF;
        __asm__ volatile("pause");
    }

    uint32_t nwp = (wp + 1) & 0xFF;
    uint32_t last_rwp = hda_in16(REG_RIRBWP) & 0xFF;   /* snapshot BEFORE submit */
    hda_corb[nwp] = ((uint32_t)cad << 28) | ((uint32_t)nid << 20) |
                    (verb << 8) | (payload & 0xFFFF);
    __asm__ volatile("mfence");
    hda_out16(REG_CORBWP, (uint16_t)nwp);

    /* Wait for the response (RIRB write pointer advance). */
    spins = 0;
    for (;;) {
        __asm__ volatile("mfence");
        rwp = hda_in16(REG_RIRBWP) & 0xFF;
        if (rwp != last_rwp) break;
        if (++spins > 2000000) {
            serial_puts("[HDA] verb timeout: nid=");
            hda_hex(nid);
            serial_puts(" verb=");
            hda_hex(verb);
            serial_puts(" last_rwp=");
            hda_hex(last_rwp);
            serial_puts(" corb rp/wp=");
            hda_hex(rp);
            serial_puts("/");
            hda_hex(hda_in16(REG_CORBWP) & 0xFF);
            serial_puts("\n");
            return 0xFFFFFFFF;
        }
        __asm__ volatile("pause");
    }

    /* Acknowledge the response interrupt so CORB keeps running. */
    uint8_t sts = hda_in8(REG_RIRBSTS);
    if (sts & RIRBSTS_IRQ)
        hda_out8(REG_RIRBSTS, RIRBSTS_IRQ);
    if (sts & RIRBSTS_OVRN)
        hda_out8(REG_RIRBSTS, RIRBSTS_OVRN);

    __asm__ volatile("mfence");
    return hda_rirb[rwp * 2];
}

/* ── Controller + CORB/RIRB setup ─────────────────────────── */
static int hda_controller_init(void) {
    /* Take the controller out of reset. */
    hda_out32(REG_GCTL, GCTL_RESET);
    delay_busy(200000);
    hda_out32(REG_GCTL, 0);
    delay_busy(200000);
    hda_out32(REG_GCTL, GCTL_UNSOL);

    /* Wake the codec(s) and check presence. */
    hda_out16(REG_WAKEEN, 0x000F);

    uint32_t corb_phys = (uint32_t)vmm_get_phys((uint64_t)hda_corb);
    uint32_t rirb_phys = (uint32_t)vmm_get_phys((uint64_t)hda_rirb);

    /* CORB: base, size (0 = 256 entries), reset read ptr, run. */
    hda_out32(REG_CORBLBASE, corb_phys & 0xFFFFFF80);
    hda_out32(REG_CORBUBASE, 0);
    hda_out8(REG_CORBSIZE, 0x00);
    hda_out16(REG_CORBRP, 0x8000);          /* reset read pointer */
    delay_busy(200000);
    hda_out16(REG_CORBRP, 0x0000);
    hda_out8(REG_CORBSTS, 0x01);            /* clear CMEI         */
    hda_out16(REG_CORBWP, 0x0000);
    hda_out8(REG_CORBCTL, CORBCTL_RUN);

    /* RIRB: base, size, DMA + response interrupt handshake. */
    hda_out32(REG_RIRBLBASE, rirb_phys & 0xFFFFFF80);
    hda_out32(REG_RIRBUBASE, 0);
    hda_out8(REG_RIRBSIZE, 0x00);
    hda_out8(REG_RIRBSTS, RIRBSTS_IRQ | RIRBSTS_OVRN);   /* clear */
    hda_out16(REG_RINTCNT, 0x0001);         /* IRQ after 1 response */
    hda_out8(REG_RIRBCTL, RIRBCTL_IRQEN | RIRBCTL_DMAEN);
    delay_busy(100000);
    return 1;
}

/* ── Codec probe + configure ──────────────────────────────── */
static int is_digital_device(uint32_t dev) {
    return dev == AC_DEVTYPE_SPDIF_OUT || dev == AC_DEVTYPE_DIG_OTHER ||
           dev == AC_DEVTYPE_SPDIF_IN  || dev == AC_DEVTYPE_DIG_IN_OTH;
}

static int hda_codec_probe(int cad) {
    uint32_t v, vendor, fn_count, start, wcap, n;
    int dac = 0;

    g_codec_cad = cad;
    g_npins = 0;
    g_dac_nid = 2;

    vendor = hda_verb(cad, 0, AC_VERB_PARAMETERS, AC_PAR_VENDOR_ID);
    if (vendor == 0xFFFFFFFF || vendor == 0) return 0;

    /* Power the audio function group (NID 1) up to D0. */
    hda_verb(cad, 1, AC_VERB_SET_POWER_STATE, 0x00);
    v = hda_verb(cad, 1, AC_VERB_PARAMETERS, AC_PAR_NODE_COUNT);
    if (v == 0xFFFFFFFF) return 0;
    fn_count = (v >> 16) & 0xFF;
    start    = v & 0xFF;
    if (fn_count == 0 || fn_count > 0x30 || start == 0) return 0;

    /* Scan the widget graph. */
    for (n = 0; n < fn_count; n++) {
        uint32_t nid = start + n;
        wcap = hda_verb(cad, (uint8_t)nid, AC_VERB_PARAMETERS,
                        AC_PAR_AUDIO_WIDGET_CAP);
        if (wcap == 0xFFFFFFFF) continue;
        uint32_t type = (wcap & AC_WIDGET_TYPE) >> 20;
        if (type == AC_WID_AUD_OUT && dac == 0)
            dac = (int)nid;
        else if (type == AC_WID_PIN && g_npins < HDA_BDL_ENTRIES)
            g_pins[g_npins++] = (int)nid;
    }
    if (dac == 0) dac = 2;      /* QEMU virtual codec: DAC = NID 2 */
    g_dac_nid = dac;

    /* Unmute the DAC output amp at 0 dB. QEMU's virtual codec
     * treats the gain as a 0..nsteps volume (offset == nsteps),
     * so it wants the maximum; real codecs want steps - offset. */
    uint32_t acap = hda_verb(cad, (uint8_t)dac, AC_VERB_PARAMETERS,
                             AC_PAR_AMP_OUT_CAP);
    uint32_t steps = (acap & AC_AMPCAP_NUM_STEPS) >> 8;
    uint32_t offset = acap & AC_AMPCAP_OFFSET;
    uint32_t gain = 0;
    if (acap != 0xFFFFFFFF) {
        if ((vendor & 0xFFFF0000) == 0x1AF40000)   /* QEMU */
            gain = steps > 0 ? steps : 0;
        else
            gain = (steps > offset) ? (steps - offset) : 0;
    }
    g_amp_gain = gain;
    hda_verb(cad, (uint8_t)dac, AC_VERB_SET_AMP_GAIN_MUTE,
             AC_AMP_SET_OUTPUT | AC_AMP_SET_LEFT | AC_AMP_SET_RIGHT |
             (gain & AC_AMP_GAIN_MASK));

    /* Enable every analogue output pin (+ EAPD where present). */
    int enabled = 0;
    for (int i = 0; i < g_npins; i++) {
        uint32_t pin = (uint32_t)g_pins[i];
        uint32_t pcap = hda_verb(cad, (uint8_t)pin, AC_VERB_PARAMETERS,
                                 AC_PAR_PIN_CAP);
        uint32_t cfg  = hda_verb(cad, (uint8_t)pin,
                                 AC_VERB_GET_CONFIG_DEFAULT, 0);
        uint32_t dev  = (cfg >> 20) & 0xF;
        if (is_digital_device(dev)) continue;
        if (pcap != 0xFFFFFFFF && (pcap & AC_PINCAP_OUT) == 0) continue;
        hda_verb(cad, (uint8_t)pin, AC_VERB_SET_PIN_WIDGET_CTRL,
                 AC_PINCTL_OUT_EN);
        if (pcap != 0xFFFFFFFF && (pcap & AC_PINCAP_EAPD))
            hda_verb(cad, (uint8_t)pin, AC_VERB_SET_EAPD, 0x02);
        enabled = 1;
    }
    if (!enabled) {
        /* Fall back to the QEMU virtual line-out (NID 3). */
        hda_verb(cad, 3, AC_VERB_SET_PIN_WIDGET_CTRL, AC_PINCTL_OUT_EN);
    }

    /* Route stream tag 1 to the DAC at 16-bit stereo 48 kHz. */
    hda_verb(cad, (uint8_t)dac, AC_VERB_SET_STREAM_FORMAT,
             HDA_FORMAT_PCM16_STEREO_48K);
    hda_verb(cad, (uint8_t)dac, AC_VERB_SET_CHANNEL_STREAMID, (1 << 4) | 0);

    return 1;
}

/* ── DMA refill ───────────────────────────────────────────── */
static void hda_fill(int idx) {
    int16_t* p = hda_bufs[idx];
    for (int i = 0; i < HDA_ENTRY_FRAMES; i++) {
        int16_t s = audio_mix_next_sample();   /* mono synth mix  */
        p[i * 2]     = s;                       /* duplicate to L  */
        p[i * 2 + 1] = s;                       /* and R           */
    }
    g_consumed[idx] = 0;
}

static int hda_stream_start(void) {
    for (int i = 0; i < HDA_BDL_ENTRIES; i++) {
        hda_bdl[i].addr = vmm_get_phys((uint64_t)hda_bufs[i]);
        hda_bdl[i].len  = HDA_ENTRY_BYTES;
        hda_bdl[i].ioc  = (i == HDA_BDL_ENTRIES - 1) ? 0x01 : 0;
        g_consumed[i]   = 1;
        hda_fill(i);
    }
    g_prev_cur = -1;
    uint64_t bdl_phys = vmm_get_phys((uint64_t)hda_bdl);

    /* Stream reset, then program the descriptor. */
    hda_out32(SD_OUT0 + SD_CTL, SD_CTL_RESET);
    delay_busy(200000);
    hda_out32(SD_OUT0 + SD_CTL, 0);
    hda_out8(SD_OUT0 + SD_STS, 0x1C);        /* clear status       */
    hda_out32(SD_OUT0 + SD_BDLPL, (uint32_t)bdl_phys);
    hda_out32(SD_OUT0 + SD_BDLPU, (uint32_t)(bdl_phys >> 32));
    hda_out32(SD_OUT0 + SD_CBL, HDA_CBL_BYTES);
    hda_out16(SD_OUT0 + SD_LVI, HDA_BDL_ENTRIES - 1);
    hda_out16(SD_OUT0 + SD_FORMAT, HDA_FORMAT_PCM16_STEREO_48K);

    /* Wait for the FIFO to be ready, then run with stream tag 1. */
    uint32_t spins = 0;
    while (!(hda_in8(SD_OUT0 + SD_STS) & SD_STS_FIFORDY)) {
        if (++spins > 2000000) return 0;
        __asm__ volatile("pause");
    }
    hda_out32(SD_OUT0 + SD_CTL, SD_CTL_TAG(1) | SD_CTL_RUN);
    g_running = 1;
    return 1;
}

/* Polled refill: keep every buffer except the one currently
 * being DMA'd full of freshly mixed audio. */
static void hda_poll(void) {
    if (!g_present || !g_running) return;

    uint32_t lpib = hda_in32(SD_OUT0 + SD_LPIB) & (HDA_CBL_BYTES - 1);
    int cur = (int)(lpib >> 10);            /* 1024-byte entries  */
    if (cur >= HDA_BDL_ENTRIES) cur = HDA_BDL_ENTRIES - 1;

    if (cur != g_prev_cur) {
        if (g_prev_cur >= 0) g_consumed[g_prev_cur] = 1;
        g_prev_cur = cur;
    }

    for (int i = 0; i < HDA_BDL_ENTRIES; i++)
        if (i != cur && g_consumed[i]) hda_fill(i);
}

/* ── Backend glue ─────────────────────────────────────────── */
static void hda_play_tone(uint32_t freq) {
    (void)freq;   /* PCM stream carries the audio; nothing to do */
}
static void hda_stop(void) {
    /* Keep DMA running — the mixer emits silence when muted. */
}

static void hda_set_volume(int vol) {
    (void)vol;    /* mixer gradient (dac_scale_sample) owns volume */
    if (g_codec_cad < 0) return;
    uint32_t amp = AC_AMP_SET_OUTPUT | AC_AMP_SET_LEFT | AC_AMP_SET_RIGHT |
                   (g_amp_gain & AC_AMP_GAIN_MASK);
    if (dac_is_muted()) amp |= 0x80;    /* mute bit */
    hda_verb((uint8_t)g_codec_cad, (uint8_t)g_dac_nid,
             AC_VERB_SET_AMP_GAIN_MUTE, amp);
}
static void hda_set_muted(int mute) {
    (void)mute;
    hda_set_volume(dac_get_volume());
}

static dac_backend_t g_hda = {
    .name       = "Intel HD Audio PCM",
    .play_tone  = hda_play_tone,
    .stop       = hda_stop,
    .set_volume = hda_set_volume,
    .set_muted  = hda_set_muted,
    .submit     = 0,
    .poll       = hda_poll,
};

void dac_hda_init(void) {
    int n = pci_get_device_count();
    for (int i = 0; i < n; i++) {
        pci_device_t* d = pci_get_device(i);
        if (!d) continue;
        if (d->class_id != 0x04 || d->subclass != 0x03) continue;  /* HDA audio */

        uint64_t mmio_phys = d->bar[0] & 0xFFFFFFF0ULL;
        if (d->bar[0] & 0x4)          /* 64-bit BAR */
            mmio_phys |= (uint64_t)d->bar[1] << 32;
        if (mmio_phys == 0 || (mmio_phys & ~0xFFFFFFFFFFULL) != 0) continue;

        /* Enable MMIO + bus mastering. */
        uint32_t cmd = pci_config_read(d->bus, d->slot, d->func, 0x04);
        pci_config_write(d->bus, d->slot, d->func, 0x04, cmd | 0x7);

        vmm_map_range(HDA_MMIO_VIRT, mmio_phys, HDA_MMIO_SIZE,
                      VMM_WRITE | VMM_PCD);
        if (!hda_controller_init()) continue;

        /* Probe codec 0 first, then any other present codec. */
        uint16_t states = hda_in16(REG_STATESTS);
        int ok = hda_codec_probe(0);
        if (!ok && (states & 0x2)) ok = hda_codec_probe(1);
        if (!ok && (states & 0x4)) ok = hda_codec_probe(2);
        if (!ok && (states & 0x8)) ok = hda_codec_probe(3);
        if (!ok) {
            serial_puts("[HDA] no responsive codec on this controller\n");
            continue;
        }

        if (!hda_stream_start()) {
            serial_puts("[HDA] stream start failed\n");
            continue;
        }

        g_present = 1;
        dac_register_backend(&g_hda);
        serial_puts("[HDA] Intel HD Audio PCM ready (48 kHz stereo)\n");
        boot_log_add("HDA", "Intel HD Audio PCM ready (48 kHz stereo)", 0x58A6FF, 0x34D399);
        return;
    }
}
