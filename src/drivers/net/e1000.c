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
#include "kernel/mem/kheap.h"
#include "kernel/mem/vmm.h"
#include "kernel/task/task.h"
#include "drivers/video/framebuffer.h"
#include <string.h>
#include <stdint.h>
#include "kernel/lib/stdio.h"

#define E1000_REG_CTRL      0x0000
#define E1000_REG_STATUS    0x0008
#define E1000_REG_EERD      0x0014
#define E1000_REG_ICR       0x00C0
#define E1000_REG_IMS       0x00D0
#define E1000_REG_RCTL      0x0100
#define E1000_REG_TCTL      0x0400
#define E1000_REG_RDBAL     0x2800
#define E1000_REG_RDBAH     0x2804
#define E1000_REG_RDLEN     0x2808
#define E1000_REG_RDH       0x2810
#define E1000_REG_RDT       0x2818
#define E1000_REG_TDBAL     0x3800
#define E1000_REG_TDBAH     0x3804
#define E1000_REG_TDLEN     0x3808
#define E1000_REG_TDH       0x3810
#define E1000_REG_TDT       0x3818
#define E1000_REG_TIPG      0x0410
#define E1000_REG_MTA       0x5200
#define E1000_REG_RAL       0x5400
#define E1000_REG_RAH       0x5404

#define RCTL_EN             (1 << 1)
#define RCTL_SBP            (1 << 2)
#define RCTL_UPE            (1 << 3)
#define RCTL_MPE            (1 << 4)
#define RCTL_LBM_NONE       (0 << 6)
#define RCTL_LBM_MAC        (1 << 6)
#define RCTL_RDMTS_HALF     (0 << 8)
#define RCTL_BAM            (1 << 15)
#define RCTL_SZ_2048        (0 << 16)   /* BSIZE=00 with BSEX=0 -> 2048 B (11 -> 256 B!) */
#define RCTL_SECRC          (1 << 26)

#define TCTL_EN             (1 << 1)
#define TCTL_PSP            (1 << 3)
#define TCTL_CT             (0x10 << 4)
#define TCTL_COLD           (0x40 << 12)

#define TDESC_CMD_EOP       (1 << 0)
#define TDESC_CMD_IFCS      (1 << 1)
#define TDESC_CMD_RS        (1 << 3)
#define TDESC_STAT_DD       (1 << 0)

#define E1000_CTRL_RST      (1 << 26)
#define E1000_CTRL_SLU      (1 << 6)
#define E1000_CTRL_ASDE     (1 << 5)

#define E1000_STATUS_LU     (1 << 1)

struct e1000_rx_desc {
    uint64_t addr;
    uint16_t len;
    uint16_t checksum;
    uint8_t  status;
    uint8_t  errors;
    uint16_t special;
} __attribute__((packed));

struct e1000_tx_desc {
    uint64_t addr;
    uint16_t len;
    uint8_t  cso;
    uint8_t  cmd;
    uint8_t  status;
    uint8_t  css;
    uint16_t special;
} __attribute__((packed));

#define RX_DESC_COUNT 128
#define TX_DESC_COUNT 128

struct e1000_softc {
    struct ifnet* ifp;
    pci_device_t* pci_dev;
    uint64_t      mmio_base;
    uint32_t      io_base;
    
    volatile struct e1000_rx_desc* rx_descs;
    volatile struct e1000_tx_desc* tx_descs;
    void*                 rx_buffers[RX_DESC_COUNT];
    void*                 tx_bounce[TX_DESC_COUNT];
    uint64_t              tx_bounce_phys[TX_DESC_COUNT];
    
    uint16_t rx_cur;
    uint16_t tx_cur;
};

extern uint64_t hhdm_offset;
extern uint64_t vmm_get_phys(uint64_t virt);

static inline void e1000_write(struct e1000_softc* sc, uint32_t reg, uint32_t val) {
    *(volatile uint32_t*)(sc->mmio_base + reg) = val;
}

static inline uint32_t e1000_read(struct e1000_softc* sc, uint32_t reg) {
    return *(volatile uint32_t*)(sc->mmio_base + reg);
}

extern void boot_log_add(const char* tag, const char* msg, uint32_t tag_color, uint32_t msg_color);
extern void draw_boot_log(void);

static int e1000_init(struct ifnet* ifp) {
    struct e1000_softc* sc = ifp->if_softc;

    boot_log_add("e1000", "Starting init...", 0xF0883E, 0x3FB950);

    // Disable interrupts
    e1000_write(sc, E1000_REG_IMS, 0);
    e1000_write(sc, E1000_REG_ICR, 0xFFFFFFFF);

    // Reset device - skip for I219/I225 as they may not support standard reset
    uint16_t dev_id = sc->pci_dev->device_id;
    if (dev_id != 0x1570 && dev_id != 0x15F3 && dev_id != 0x24F3) {
        e1000_write(sc, E1000_REG_CTRL, E1000_CTRL_RST);

        int timeout = 1000;
        while ((e1000_read(sc, E1000_REG_CTRL) & E1000_CTRL_RST) && timeout-- > 0) {
            __asm__("pause");
        }
        if (timeout <= 0) {
            boot_log_add("e1000", "Reset timeout", 0xF0883E, 0xF0883E);
        } else {
            boot_log_add("e1000", "Reset done", 0xF0883E, 0x3FB950);
        }
    } else {
        boot_log_add("e1000", "Skipping reset for I219/I225", 0xF0883E, 0x3FB950);
    }

    // Re-enable PCI bus mastering after reset
    pci_enable_bus_mastering(sc->pci_dev);
    boot_log_add("e1000", "Bus mastering OK", 0xF0883E, 0x3FB950);

    // Force Link Up immediately
    uint32_t ctrl = e1000_read(sc, E1000_REG_CTRL);
    e1000_write(sc, E1000_REG_CTRL, ctrl | E1000_CTRL_SLU | E1000_CTRL_ASDE);

    for (volatile int i = 0; i < 5000; i++) __asm__("pause");
    if (e1000_read(sc, E1000_REG_STATUS) & E1000_STATUS_LU) {
        boot_log_add("e1000", "Link UP", 0xF0883E, 0x3FB950);
    } else {
        boot_log_add("e1000", "Link forced", 0xF0883E, 0xF0883E);
    }
    draw_boot_log();

    // Re-initialize MAC address registers
    uint32_t ral;
    uint32_t rah;
    memcpy(&ral, &ifp->if_hwaddr[0], 4);
    memcpy(&rah, &ifp->if_hwaddr[4], 2);
    {
        char dbg[64];
        snprintf(dbg, sizeof(dbg), "  E1000: MAC=%02x:%02x:%02x:%02x:%02x:%02x\n",
            ifp->if_hwaddr[0], ifp->if_hwaddr[1], ifp->if_hwaddr[2],
            ifp->if_hwaddr[3], ifp->if_hwaddr[4], ifp->if_hwaddr[5]);
//        print_string(dbg);
    }
    e1000_write(sc, E1000_REG_RAL, ral);
    e1000_write(sc, E1000_REG_RAH, rah | 0x80000000);

    // Clear other receive address registers (RAL1-RAL15)
    for (int i = 1; i < 16; i++) {
        e1000_write(sc, E1000_REG_RAL + i * 8, 0);
        e1000_write(sc, E1000_REG_RAH + i * 8, 0);
    }

    // Clear Multicast Table Array (MTA)
    for (int i = 0; i < 128; i++) {
        e1000_write(sc, E1000_REG_MTA + i * 4, 0);
    }

    // (Re)initialize the RX descriptor ring and buffers after the device reset.
    // kmalloc_aligned does not zero memory, so the descriptors inherit garbage
    // in their status/errors/addr fields from boot time. If any descriptor's DD
    // bit reads as 1 (hardware-owned), the e1000 hardware refuses to fill the
    // whole ring, which manifests as RX being completely dead (RDH never
    // advances, no incoming packets). Zero everything, then (re)publish each
    // buffer address so the hardware sees a clean, consistent ring.
    memset((void*)sc->rx_descs, 0, RX_DESC_COUNT * sizeof(struct e1000_rx_desc));
    for (int i = 0; i < RX_DESC_COUNT; i++) {
        memset(sc->rx_buffers[i], 0, 2048);
        sc->rx_descs[i].addr = vmm_get_phys((uint64_t)sc->rx_buffers[i]);
        sc->rx_descs[i].len = 0;
        sc->rx_descs[i].status = 0;
        sc->rx_descs[i].errors = 0;
    }
    // Hardware read barrier: ensure descriptor updates reach memory before RDT
    __sync_synchronize();

    uint64_t rx_phys = vmm_get_phys((uint64_t)sc->rx_descs);
    e1000_write(sc, E1000_REG_RDBAL, rx_phys & 0xFFFFFFFF);
    e1000_write(sc, E1000_REG_RDBAH, rx_phys >> 32);
    e1000_write(sc, E1000_REG_RDLEN, RX_DESC_COUNT * sizeof(struct e1000_rx_desc));
    e1000_write(sc, E1000_REG_RDH, 0);
    e1000_write(sc, E1000_REG_RDT, RX_DESC_COUNT - 1);
    uint32_t rctl_val = RCTL_EN | RCTL_SBP | RCTL_UPE | RCTL_MPE | RCTL_BAM | RCTL_SZ_2048 | RCTL_SECRC;
    e1000_write(sc, E1000_REG_RCTL, rctl_val);
    uint32_t rctl_check = e1000_read(sc, E1000_REG_RCTL);
    if (rctl_check != rctl_val) {
        char dbg[64];
        snprintf(dbg, sizeof(dbg), "  E1000: RCTL mismatch! wrote 0x%08X got 0x%08X\n", rctl_val, rctl_check);
//        print_string(dbg);
    } else {
//        print_string("  E1000: RCTL OK\n");
    }
    // Final check before return
    uint32_t rctl_final = e1000_read(sc, E1000_REG_RCTL);
    if (rctl_final != rctl_val) {
        char dbg[64];
        snprintf(dbg, sizeof(dbg), "  E1000: RCTL CHANGED at init end! was 0x%08X now 0x%08X\n", rctl_val, rctl_final);
//        print_string(dbg);
    }

    uint64_t tx_phys = vmm_get_phys((uint64_t)sc->tx_descs);

    e1000_write(sc, E1000_REG_TDBAL, tx_phys & 0xFFFFFFFF);
    e1000_write(sc, E1000_REG_TDBAH, tx_phys >> 32);
    e1000_write(sc, E1000_REG_TDLEN, TX_DESC_COUNT * sizeof(struct e1000_tx_desc));
    e1000_write(sc, E1000_REG_TDH, 0);
    e1000_write(sc, E1000_REG_TDT, 0);
    
    // TIPG: Transmit Inter-Packet Gap
    e1000_write(sc, E1000_REG_TIPG, (6 << 20) | (8 << 10) | 10);

    // TCTL: Enable | Pad Short Packets | Collision Threshold | Collision Distance
    e1000_write(sc, E1000_REG_TCTL, TCTL_EN | TCTL_PSP | (0x10 << 4) | (0x40 << 12));

    if (!(e1000_read(sc, E1000_REG_STATUS) & E1000_STATUS_LU)) {
        uint32_t ctrl2 = e1000_read(sc, E1000_REG_CTRL);
        e1000_write(sc, E1000_REG_CTRL, ctrl2 | E1000_CTRL_SLU);
    }

    ifp->if_flags |= IFF_RUNNING;
    return 0;
}

static volatile int e1000_lock = 0;
static volatile int e1000_lock_depth = 0;
static volatile int e1000_lock_owner = -1;

static void e1000_acquire(void);
static void e1000_release(void);

static int e1000_output(struct ifnet* ifp, struct mbuf* m) {
    struct e1000_softc* sc = ifp->if_softc;

    /* Calculate total length of mbuf chain */
    int total_len = 0;
    struct mbuf* n;
    for (n = m; n != NULL; n = n->m_next) {
        total_len += n->m_len;
    }

    if (total_len > 2048) {
        m_freem(m);
        return -1;
    }

    /* The whole TX critical section is under the lock: descriptor slot
       claim, packet copy, ring advance. Each slot owns its own bounce
       buffer, so concurrent senders (worker ACKs + app fetches) cannot
       overwrite each other's packet data before the NIC reads it. */
    e1000_acquire();
    uint16_t cur = sc->tx_cur;
    m_copydata(m, 0, total_len, sc->tx_bounce[cur]);
    sc->tx_descs[cur].addr = sc->tx_bounce_phys[cur];
    sc->tx_descs[cur].len = total_len;
    sc->tx_descs[cur].status = 0;
    sc->tx_descs[cur].cmd = TDESC_CMD_EOP | TDESC_CMD_IFCS | TDESC_CMD_RS;

    __asm__ volatile("" : : : "memory");

    sc->tx_cur = (cur + 1) % TX_DESC_COUNT;
    e1000_write(sc, E1000_REG_TDT, sc->tx_cur);
    e1000_release();

    /* Wait for the NIC to send. Watch only our private slot `cur`,
       which no other sender touches until the ring wraps. The descriptor
       ring lives in cached memory and the NIC sets DD via DMA, so each
       check must clflush the line to see the hardware's write (pause-only
       spinning re-reads a stale cached line and never observes DD). The
       wait is bounded; under the preemptive timer a µs-scale stall is fine. */
    volatile uint8_t* dd_status = &sc->tx_descs[cur].status;
    int spin = 1000;
    int total_waits = 0;
    while (!(*dd_status & TDESC_STAT_DD)) {
        __asm__ volatile("clflush (%0)" :: "r"(dd_status) : "memory");
        __asm__ volatile("pause" : : : "memory");
        if (--spin <= 0) {
            spin = 1000;
            total_waits++;
            if (total_waits > 500) {
                extern void serial_puts(const char* s);
                serial_puts("[TX] DD timeout\n");
                m_freem(m);
                return -1;
            }
        }
    }
    /* Make sure the (already-transmitted) descriptor writebacks are visible
       before we let another sender reuse the slot. */
    __asm__ volatile("mfence" ::: "memory");
    
    m_freem(m);
    return 0;
}

static void e1000_acquire(void) {
    int me = get_current_task_id();
    if (e1000_lock_owner == me) {
        e1000_lock_depth++;
        return;
    }
    int spins = 0;
    while (__atomic_test_and_set(&e1000_lock, __ATOMIC_ACQUIRE)) {
        __asm__ volatile("pause" : : : "memory");
        /* Safety net: if the owner is READY but parked (nested DD wait),
           handing the CPU to it is the only way it can finish. */
        if (++spins > 4000) {
            spins = 0;
            extern void yield(void);
            yield();
        }
    }
    e1000_lock_owner = me;
    e1000_lock_depth = 1;
}

static void e1000_release(void) {
    if (e1000_lock_depth > 1) {
        e1000_lock_depth--;
        return;
    }
    e1000_lock_owner = -1;
    e1000_lock_depth = 0;
    __atomic_clear(&e1000_lock, __ATOMIC_RELEASE);
}

/* Exported wrappers so the socket layer can serialize socket struct
   lifetime (close/free, recv drain) against tcp_input's RX processing. */
void e1000_mutex_lock(void) { e1000_acquire(); }
void e1000_mutex_unlock(void) { e1000_release(); }

void e1000_poll(struct ifnet* ifp) {
    struct e1000_softc* sc = ifp->if_softc;
    e1000_acquire();
    uint32_t rdh = e1000_read(sc, E1000_REG_RDH);
    int processed = 0;

    while (processed < 32 && sc->rx_cur != (uint16_t)rdh) {
        uint16_t cur = sc->rx_cur;

        if (!(sc->rx_descs[cur].status & 0x1)) {
            volatile uint8_t* rx_st = &sc->rx_descs[cur].status;
            int retry = 0;
            for (; retry < 2000; retry++) {
                __asm__ volatile("clflush (%0)" :: "r"(rx_st) : "memory");
                __asm__ volatile("pause" : : : "memory");
                if (*rx_st & 0x1) {
                    break;
                }
            }
            if (!(*rx_st & 0x1)) {
                break;
            }
        }

        uint16_t len = sc->rx_descs[cur].len;
        struct mbuf* m = m_getcl(MT_DATA);
        if (m) {
            memcpy(m->m_data, sc->rx_buffers[cur], len);
            m->m_len = len;
            m->m_pkthdr.len = len;
            m->m_pkthdr.rcvif = ifp;
            m->m_flags |= M_PKTHDR;
            ether_input(ifp, m);
        }

        sc->rx_descs[cur].status = 0;
        sc->rx_cur = (cur + 1) % RX_DESC_COUNT;
        e1000_write(sc, E1000_REG_RDT, cur);
        processed++;
        rdh = e1000_read(sc, E1000_REG_RDH);
    }
    e1000_release();
}

void e1000_attach(pci_device_t* dev) {
    pci_enable_bus_mastering(dev);
    struct e1000_softc* sc = kmalloc(sizeof(struct e1000_softc));
    struct ifnet* ifp = kmalloc(sizeof(struct ifnet));
    memset(sc, 0, sizeof(struct e1000_softc));
    memset(ifp, 0, sizeof(struct ifnet));

    sc->pci_dev = dev;
    sc->mmio_base = dev->bar_64[0] + hhdm_offset;
    sc->rx_descs = kmalloc_aligned(RX_DESC_COUNT * sizeof(struct e1000_rx_desc), 4096);
    sc->tx_descs = kmalloc_aligned(TX_DESC_COUNT * sizeof(struct e1000_tx_desc), 4096);
    for (int i = 0; i < TX_DESC_COUNT; i++) {
        sc->tx_bounce[i] = kmalloc_aligned(2048, 2048);
        sc->tx_bounce_phys[i] = vmm_get_phys((uint64_t)sc->tx_bounce[i]);
    }
    
    for (int i = 0; i < RX_DESC_COUNT; i++) {
        sc->rx_buffers[i] = kmalloc_aligned(2048, 2048);
        sc->rx_descs[i].addr = vmm_get_phys((uint64_t)sc->rx_buffers[i]);
    }

    strcpy(ifp->if_xname, "em0");
    ifp->if_softc = sc;
    ifp->if_init = e1000_init;
    ifp->if_output = e1000_output;
    
    uint32_t ral = e1000_read(sc, E1000_REG_RAL);
    uint32_t rah = e1000_read(sc, E1000_REG_RAH);
    memcpy(&ifp->if_hwaddr[0], &ral, 4);
    memcpy(&ifp->if_hwaddr[4], &rah, 2);

    if_attach(ifp);
    e1000_init(ifp);
    boot_log_add("e1000", "em0 attached and initialized", 0xF0883E, 0x3FB950);
    draw_boot_log();
}
