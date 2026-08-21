/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#include "kernel/net/if.h"
#include "kernel/net/mbuf.h"
#include "kernel/net/ethernet.h"
#include "drivers/bus/pci.h"
#include "drivers/input/keyboard.h"
#include "kernel/mem/kheap.h"
#include "kernel/mem/vmm.h"
#include "kernel/task/task.h"
#include "kernel/lib/stdio.h"
#include <string.h>
#include <stdint.h>

#define PCNET_WIO_RDP    0x10
#define PCNET_WIO_RAP    0x12
#define PCNET_WIO_RESET  0x14
#define PCNET_WIO_BDP    0x16

#define PCNET_CSR0_STOP  0x0004
#define PCNET_CSR0_STRT  0x0002
#define PCNET_CSR0_INIT  0x0001
#define PCNET_CSR0_TDMD  0x0008
#define PCNET_CSR0_IDON  0x0100
#define PCNET_CSR0_ERR   0x8000

#define PCNET_STATUS_OWN 0x8000
#define PCNET_STATUS_ERR 0x4000
#define PCNET_STATUS_STP 0x0200
#define PCNET_STATUS_ENP 0x0100

#define PCNET_RX_DESC_COUNT 64
#define PCNET_TX_DESC_COUNT 64
#define PCNET_BUFSIZE       2048

struct pcnet_initblk {
    uint16_t mode;
    uint8_t  rlen;
    uint8_t  tlen;
    uint16_t padr[3];
    uint16_t rsvd1;
    uint16_t ladrf[4];
    uint32_t rdra;
    uint32_t tdra;
} __attribute__((packed));

struct pcnet_bufdesc {
    uint32_t info;
    uint16_t length;
    uint16_t status;
    uint32_t buffer;
    uint32_t rsvd;
} __attribute__((packed));

struct pcnet_softc {
    struct ifnet* ifp;
    pci_device_t* pci_dev;
    uint16_t      io_base;
    uint8_t       mac[6];

    struct pcnet_initblk*  init_blk;
    volatile struct pcnet_bufdesc* rx_descs;
    volatile struct pcnet_bufdesc* tx_descs;
    void*                 rx_buffers[PCNET_RX_DESC_COUNT];
    void*                 tx_bounce;

    uint16_t rx_cur;
    uint16_t tx_cur;

    uint32_t stats_rx_frames;
    uint32_t stats_rx_err;
    uint32_t stats_rx_mbuf_fail;
    uint32_t stats_tx_frames;
    uint32_t stats_tx_fail;
};

static inline void pcnet_csr_write(struct pcnet_softc* sc, uint16_t reg, uint16_t val) {
    port_word_out(sc->io_base + PCNET_WIO_RAP, reg);
    port_word_out(sc->io_base + PCNET_WIO_RDP, val);
}

static inline uint16_t pcnet_csr_read(struct pcnet_softc* sc, uint16_t reg) {
    port_word_out(sc->io_base + PCNET_WIO_RAP, reg);
    return port_word_in(sc->io_base + PCNET_WIO_RDP);
}

/* Read PCnet missed-frame counter (CSR112). The counter saturates at 0xFFFF
   and is reset on read, so this also clears it. */
uint16_t pcnet_missed_frames(struct ifnet* ifp) {
    struct pcnet_softc* sc = ifp->if_softc;
    return pcnet_csr_read(sc, 112);
}

void pcnet_get_stats(struct ifnet* ifp, uint32_t* rx_frames, uint32_t* rx_err,
                     uint32_t* rx_mbuf_fail, uint32_t* tx_frames, uint32_t* tx_fail,
                     uint16_t* missed) {
    struct pcnet_softc* sc = ifp->if_softc;
    if (rx_frames) *rx_frames = sc->stats_rx_frames;
    if (rx_err) *rx_err = sc->stats_rx_err;
    if (rx_mbuf_fail) *rx_mbuf_fail = sc->stats_rx_mbuf_fail;
    if (tx_frames) *tx_frames = sc->stats_tx_frames;
    if (tx_fail) *tx_fail = sc->stats_tx_fail;
    if (missed) *missed = pcnet_csr_read(sc, 112);
}


extern void boot_log_add(const char* tag, const char* msg, uint32_t tag_color, uint32_t msg_color);
extern void draw_boot_log(void);

static int pcnet_init(struct ifnet* ifp) {
    struct pcnet_softc* sc = ifp->if_softc;

    boot_log_add("pcnet", "Starting init...", 0xF0883E, 0x3FB950);

    port_word_in(sc->io_base + PCNET_WIO_RESET);
    pcnet_csr_write(sc, 0, PCNET_CSR0_STOP);

    pcnet_csr_write(sc, 58, 0x0003);
    pcnet_csr_write(sc, 0, PCNET_CSR0_STOP);

    struct pcnet_initblk* ib = sc->init_blk;
    memset(ib, 0, sizeof(struct pcnet_initblk));
    ib->mode = 0x0000;
    ib->rlen = 0x66;
    ib->tlen = 0x66;
    memcpy(ib->padr, sc->mac, 6);
    ib->rdra = (uint32_t)vmm_get_phys((uint64_t)sc->rx_descs);
    ib->tdra = (uint32_t)vmm_get_phys((uint64_t)sc->tx_descs);

    uint32_t init_phys = (uint32_t)vmm_get_phys((uint64_t)ib);
    pcnet_csr_write(sc, 1, init_phys & 0xFFFF);
    pcnet_csr_write(sc, 2, (init_phys >> 16) & 0xFFFF);
    pcnet_csr_write(sc, 3, (uint16_t)((uint64_t)init_phys >> 32));

    for (int i = 0; i < PCNET_RX_DESC_COUNT; i++) {
        sc->rx_descs[i].info = 0;
        sc->rx_descs[i].length = 0xF000 | (4096 - PCNET_BUFSIZE);
        sc->rx_descs[i].status = PCNET_STATUS_OWN;
        sc->rx_descs[i].buffer = (uint32_t)vmm_get_phys((uint64_t)sc->rx_buffers[i]);
        sc->rx_descs[i].rsvd = 0;
    }
    sc->rx_cur = 0;
    sc->tx_cur = 0;

    pcnet_csr_write(sc, 0, PCNET_CSR0_INIT);

    int timeout = 20000;
    uint16_t csr0 = pcnet_csr_read(sc, 0);
    while (!(csr0 & PCNET_CSR0_IDON) && timeout-- > 0) {
        for (volatile int i = 0; i < 100; i++) __asm__("pause");
        csr0 = pcnet_csr_read(sc, 0);
    }
    if (timeout <= 0) {
        boot_log_add("pcnet", "INIT timeout", 0xF0883E, 0xF85149);
        draw_boot_log();
        return -1;
    }
    if (csr0 & PCNET_CSR0_ERR) {
        boot_log_add("pcnet", "INIT reported error", 0xF0883E, 0xF85149);
        draw_boot_log();
    }

    pcnet_csr_write(sc, 0, PCNET_CSR0_STRT);

    ifp->if_flags |= IFF_RUNNING;
    boot_log_add("pcnet", "pn0 up, receiving", 0xF0883E, 0x3FB950);
    draw_boot_log();
    return 0;
}

static int pcnet_output(struct ifnet* ifp, struct mbuf* m) {
    if (!ifp || !ifp->if_softc || !m) {
        if (m) m_freem(m);
        return -1;
    }
    struct pcnet_softc* sc = ifp->if_softc;
    if (!sc->tx_bounce) {
        m_freem(m);
        return -1;
    }

    int total_len = 0;
    int depth = 0;
    for (struct mbuf* n = m; n != NULL && depth < 64; n = n->m_next, depth++) {
        if (n->m_len < 0 || n->m_len > PCNET_BUFSIZE) break;
        total_len += n->m_len;
    }
    if (total_len <= 0 || total_len > PCNET_BUFSIZE) {
        m_freem(m);
        return -1;
    }

    m_copydata(m, 0, total_len, sc->tx_bounce);

    int len = total_len;
    if (len < 60) {
        memset((uint8_t*)sc->tx_bounce + len, 0, 60 - len);
        len = 60;
    }

    uint16_t cur = sc->tx_cur;
    volatile struct pcnet_bufdesc* td = &sc->tx_descs[cur];

    td->info = 0;
    td->length = 0xF000 | (4096 - len);
    td->status = PCNET_STATUS_OWN | PCNET_STATUS_STP | PCNET_STATUS_ENP;
    td->buffer = (uint32_t)vmm_get_phys((uint64_t)sc->tx_bounce);
    td->rsvd = 0;

    __asm__ volatile("" : : : "memory");

    pcnet_csr_write(sc, 0, PCNET_CSR0_TDMD);

    int spins = 5000;
    int waits = 0;
    while ((td->status & PCNET_STATUS_OWN)) {
        if (--spins > 0) {
            __asm__ volatile("pause" : : : "memory");
        } else {
            extern void yield(void);
            yield();
            spins = 1000;
            if (++waits > 50) {
                sc->stats_tx_fail++;
                m_freem(m);
                return -1;
            }
        }
    }

    sc->stats_tx_frames++;

    sc->tx_cur = (cur + 1) % PCNET_TX_DESC_COUNT;
    m_freem(m);
    return 0;
}

void pcnet_poll(struct ifnet* ifp) {
    struct pcnet_softc* sc = ifp->if_softc;
    int processed = 0;
    static uint32_t idle_polls = 0;
    static uint32_t poll_serial_counter = 0;

    while (processed < PCNET_RX_DESC_COUNT) {
        uint16_t cur = sc->rx_cur;
        volatile struct pcnet_bufdesc* rd = &sc->rx_descs[cur];

        if (rd->status & PCNET_STATUS_OWN) {
            break;
        }

        uint32_t mcnt = rd->info & 0x0FFF;
        uint16_t len = (mcnt >= 4) ? (uint16_t)(mcnt - 4) : 0;
        if (len > PCNET_BUFSIZE) len = PCNET_BUFSIZE;

        if (len > 0 && !(rd->status & PCNET_STATUS_ERR)) {
            struct mbuf* m = m_getcl(MT_DATA);
            if (m) {
                sc->stats_rx_frames++;
                memcpy(m->m_data, sc->rx_buffers[cur], len);
                m->m_len = len;
                m->m_pkthdr.len = len;
                m->m_pkthdr.rcvif = ifp;
                m->m_flags |= M_PKTHDR;
                ether_input(ifp, m);
            } else {
                sc->stats_rx_mbuf_fail++;
            }
        } else if (rd->status & PCNET_STATUS_ERR) {
            sc->stats_rx_err++;
        }

        rd->info = 0;
        rd->length = 0xF000 | (4096 - PCNET_BUFSIZE);
        rd->status = PCNET_STATUS_OWN;
        sc->rx_cur = (cur + 1) % PCNET_RX_DESC_COUNT;
        processed++;
    }

    /* Diagnostics: report a stuck ring (idle for a long time) and catch-up bursts */
    if (processed == 0) {
        if (++idle_polls > 1000 && (poll_serial_counter++ % 40) == 0) {
            extern void serial_puts(const char* s);
            uint16_t c = sc->rx_cur;
            char buf[160];
            snprintf(buf, sizeof(buf),
                "[PCNET] stuck: rx_cur=%u cur_st=%04x next1=%04x next2=%04x n3=%04x rxfr=%u\n",
                c, sc->rx_descs[c].status, sc->rx_descs[(c+1)%64].status,
                sc->rx_descs[(c+2)%64].status, sc->rx_descs[(c+3)%64].status,
                sc->stats_rx_frames);
            serial_puts(buf);
        }
    } else {
        idle_polls = 0;
        if (processed >= 50) {
            extern void serial_puts(const char* s);
            char buf[96];
            snprintf(buf, sizeof(buf), "[PCNET] catchup burst: %d frames, rx_cur now %u\n",
                processed, sc->rx_cur);
            serial_puts(buf);
        }
    }
}

void pcnet_attach(pci_device_t* dev) {
    pci_enable_bus_mastering(dev);

    struct pcnet_softc* sc = kmalloc(sizeof(struct pcnet_softc));
    struct ifnet* ifp = kmalloc(sizeof(struct ifnet));
    memset(sc, 0, sizeof(struct pcnet_softc));
    memset(ifp, 0, sizeof(struct ifnet));

    sc->pci_dev = dev;
    sc->io_base = (uint16_t)(dev->bar_64[0] & 0xFFFF);

    for (int i = 0; i < 6; i++) {
        sc->mac[i] = port_byte_in((unsigned short)(sc->io_base + i));
    }
    memcpy(ifp->if_hwaddr, sc->mac, 6);

    sc->init_blk = kmalloc_aligned(sizeof(struct pcnet_initblk), 16);
    sc->rx_descs = kmalloc_aligned(PCNET_RX_DESC_COUNT * sizeof(struct pcnet_bufdesc), 16);
    sc->tx_descs = kmalloc_aligned(PCNET_TX_DESC_COUNT * sizeof(struct pcnet_bufdesc), 16);
    sc->tx_bounce = kmalloc_aligned(PCNET_BUFSIZE, 16);

    for (int i = 0; i < PCNET_RX_DESC_COUNT; i++) {
        sc->rx_buffers[i] = kmalloc_aligned(PCNET_BUFSIZE, 16);
    }

    strcpy(ifp->if_xname, "pn0");
    ifp->if_softc = sc;
    ifp->if_init = pcnet_init;
    ifp->if_output = pcnet_output;

    if_attach(ifp);
    pcnet_init(ifp);
    boot_log_add("pcnet", "AMD PCnet/PCI II attached as pn0", 0xF0883E, 0x3FB950);
    draw_boot_log();
}