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
#include "drivers/bus/virtio.h"
#include "kernel/mem/kheap.h"
#include "kernel/mem/vmm.h"
#include <string.h>
#include <stddef.h>

#define RX_QUEUE_SIZE 256
#define TX_QUEUE_SIZE 256

struct virtio_net_config {
    uint16_t mac[3];
    uint16_t status;
    uint16_t max_virtqueue_pairs;
    uint16_t mtu;
} __attribute__((packed));

struct virtio_net_hdr {
    uint8_t flags;
    uint8_t gso_type;
    uint16_t hdr_len;
    uint16_t gso_size;
    uint16_t csum_start;
    uint16_t csum_offset;
    uint16_t num_buffers;
} __attribute__((packed));

#define VIRTIO_NET_F_CSUM  (1 << 0)
#define VIRTIO_NET_F_GUEST_CSUM (1 << 1)
#define VIRTIO_NET_F_MAC   (1 << 5)
#define VIRTIO_NET_F_GSO   (1 << 6)
#define VIRTIO_NET_F_GUEST_TSO4 (1 << 7)
#define VIRTIO_NET_F_GUEST_TSO6 (1 << 8)
#define VIRTIO_NET_F_GUEST_ECN  (1 << 9)
#define VIRTIO_NET_F_GUEST_UFO  (1 << 10)
#define VIRTIO_NET_F_HOST_TSO4  (1 << 11)
#define VIRTIO_NET_F_HOST_TSO6  (1 << 12)
#define VIRTIO_NET_F_HOST_ECN   (1 << 13)
#define VIRTIO_NET_F_HOST_UFO   (1 << 14)
#define VIRTIO_NET_F_MRG_RXBUF  (1 << 15)
#define VIRTIO_NET_F_STATUS     (1 << 16)
#define VIRTIO_NET_F_CTRL_VQ    (1 << 17)
#define VIRTIO_NET_F_CTRL_RX    (1 << 18)
#define VIRTIO_NET_F_CTRL_VLAN  (1 << 19)
#define VIRTIO_NET_F_CTRL_RX_EXTRA (1 << 20)
#define VIRTIO_NET_F_GUEST_ANNOUNCE (1 << 21)
#define VIRTIO_NET_F_MQ         (1 << 22)
#define VIRTIO_NET_F_HASH_REPORT (1 << 23)

struct virtio_net_softc {
    struct ifnet* ifp;
    pci_device_t* pci_dev;
    uint64_t mmio_base;
    
    struct virtio_pci_common_cfg* common_cfg;
    uint32_t* notify_reg;
    uint8_t* isr_status;
    struct virtio_net_config* dev_cfg;
    
    virtqueue_t rx_vq;
    virtqueue_t tx_vq;
    virtqueue_t ctrl_vq;
    
    void* rx_buffers[RX_QUEUE_SIZE];
    void* tx_bounce_buffer;
    uint16_t rx_cur;
    uint16_t tx_cur;
};

extern uint64_t hhdm_offset;
extern uint64_t vmm_get_phys(uint64_t virt);
extern void boot_log_add(const char* tag, const char* msg, uint32_t tag_color, uint32_t msg_color);
extern void draw_boot_log(void);

static inline void viowrite32(struct virtio_net_softc* sc, uint32_t offset, uint32_t val) {
    *(volatile uint32_t*)(sc->mmio_base + offset) = val;
}

static inline uint32_t viorread32(struct virtio_net_softc* sc, uint32_t offset) {
    return *(volatile uint32_t*)(sc->mmio_base + offset);
}

static inline void viowrite64(struct virtio_net_softc* sc, uint32_t offset, uint64_t val) {
    *(volatile uint64_t*)(sc->mmio_base + offset) = val;
}

static inline void vq_setup(struct virtio_net_softc* sc, virtqueue_t* vq, uint16_t qidx, uint16_t max_size) {
    sc->common_cfg->queue_select = qidx;
    uint16_t qsize = sc->common_cfg->queue_size;
    if (qsize == 0 || qsize > max_size) qsize = max_size;
    
    vq->size = qsize;
    vq->desc = kmalloc_aligned(qsize * sizeof(struct vring_desc), 4096);
    vq->avail = kmalloc_aligned(sizeof(struct vring_avail) + qsize * sizeof(uint16_t), 4096);
    vq->used = kmalloc_aligned(sizeof(struct vring_used) + qsize * sizeof(struct vring_used_elem), 4096);
    vq->avail_idx = 0;
    vq->last_used_idx = 0;
    vq->free_head = 0;
    vq->num_free = qsize;
    for (int i = 0; i < qsize; i++) vq->desc[i].next = i + 1;
    vq->desc[qsize - 1].next = 0xFFFF;
    
    sc->common_cfg->queue_desc = vmm_get_phys((uint64_t)vq->desc);
    sc->common_cfg->queue_avail = vmm_get_phys((uint64_t)vq->avail);
    sc->common_cfg->queue_used = vmm_get_phys((uint64_t)vq->used);
    sc->common_cfg->queue_enable = 1;
}

static void vq_add_desc(virtqueue_t* vq, uint64_t addr, uint32_t len, uint16_t flags, uint16_t next) {
    uint16_t idx = vq->free_head;
    vq->free_head = vq->desc[idx].next;
    vq->num_free--;
    vq->desc[idx].addr = addr;
    vq->desc[idx].len = len;
    vq->desc[idx].flags = flags;
    vq->desc[idx].next = next;
    vq->avail->ring[vq->avail_idx % vq->size] = idx;
    vq->avail_idx++;
    vq->avail->idx = vq->avail_idx;
}

static void vq_add_buf(virtqueue_t* vq, void* buf, uint32_t len, uint16_t flags) {
    uint64_t phys = vmm_get_phys((uint64_t)buf);
    vq_add_desc(vq, phys, len, flags, 0xFFFF);
}

static int vq_poll_used_nonblock(virtqueue_t* vq, uint16_t* out_idx, uint32_t* out_len) {
    if (vq->last_used_idx == vq->used->idx) return -1; /* No entries available */
    struct vring_used_elem* elem = &vq->used->ring[vq->last_used_idx % vq->size];
    *out_idx = elem->id;
    *out_len = elem->len;
    vq->last_used_idx++;
    return 0;
}

static void vq_free_desc(virtqueue_t* vq, uint16_t idx) {
    vq->desc[idx].next = vq->free_head;
    vq->free_head = idx;
    vq->num_free++;
}

static int virtio_net_init(struct ifnet* ifp) {
    struct virtio_net_softc* sc = ifp->if_softc;

    /* Step 1: Reset device */
    sc->common_cfg->device_status = 0;
    for (volatile int i = 0; i < 1000; i++) __asm__ volatile("pause");

    /* Step 2: ACK + DRIVER */
    sc->common_cfg->device_status = 1; /* ACKNOWLEDGE */
    sc->common_cfg->device_status |= 2; /* DRIVER */

    /* Step 3: Feature negotiation - only request what we need */
    sc->common_cfg->device_feature_select = 0;
    uint32_t features = sc->common_cfg->device_feature;
    /* Accept MAC, STATUS; reject offload features to avoid complexity */
    uint32_t wanted = VIRTIO_NET_F_MAC | VIRTIO_NET_F_STATUS;
    sc->common_cfg->driver_feature_select = 0;
    sc->common_cfg->driver_feature = features & wanted;

    /* Step 4: FEATURES_OK — must check device accepted our features */
    sc->common_cfg->device_status |= 8; /* FEATURES_OK */
    for (volatile int i = 0; i < 1000; i++) __asm__ volatile("pause");
    if (!(sc->common_cfg->device_status & 8)) {
        boot_log_add("virtio-net", "FEATURES_OK not set by device!", 0xF0883E, 0xF85149);
        /* Retry with no features */
        sc->common_cfg->device_status = 1 | 2;
        sc->common_cfg->driver_feature_select = 0;
        sc->common_cfg->driver_feature = 0;
        sc->common_cfg->device_status |= 8;
    }

    /* Step 5: Set up virtqueues */
    vq_setup(sc, &sc->rx_vq, 0, RX_QUEUE_SIZE);
    vq_setup(sc, &sc->tx_vq, 1, TX_QUEUE_SIZE);

    /* Step 6: Read MAC from device config space (byte-by-byte) */
    /* The device-specific config region follows common_cfg in the BAR */
    volatile uint8_t* dev_cfg_bytes = (volatile uint8_t*)(sc->mmio_base + 0x100);
    for (int i = 0; i < 6; i++) {
        ifp->if_hwaddr[i] = dev_cfg_bytes[i];
    }

    /* Step 7: Pre-fill RX queue with buffers */
    for (int i = 0; i < RX_QUEUE_SIZE; i++) {
        sc->rx_buffers[i] = kmalloc_aligned(2048, 2048);
        vq_add_buf(&sc->rx_vq, sc->rx_buffers[i], 2048, VRING_DESC_F_WRITE);
    }
    /* Kick RX queue to notify device */
    *sc->notify_reg = 0;

    sc->tx_bounce_buffer = kmalloc_aligned(2048, 2048);

    /* Step 8: DRIVER_OK */
    sc->common_cfg->device_status |= 4; /* DRIVER_OK */

    ifp->if_flags |= IFF_RUNNING;
    boot_log_add("virtio-net", "vn0 attached and initialized", 0xF0883E, 0x3FB950);
    draw_boot_log();
    return 0;
}

static int virtio_net_output(struct ifnet* ifp, struct mbuf* m) {
    struct virtio_net_softc* sc = ifp->if_softc;

    int total_len = 0;
    struct mbuf* n;
    for (n = m; n != NULL; n = n->m_next) total_len += n->m_len;
    if (total_len > 2048) { m_freem(m); return -1; }
    m_copydata(m, 0, total_len, sc->tx_bounce_buffer);

    /* virtio-net requires a 12-byte header prepended before the ethernet frame */
    static struct virtio_net_hdr tx_hdr;
    tx_hdr.flags = 0;
    tx_hdr.gso_type = 0;
    tx_hdr.hdr_len = 0;
    tx_hdr.gso_size = 0;
    tx_hdr.csum_start = 0;
    tx_hdr.csum_offset = 0;
    tx_hdr.num_buffers = 0;

    if (sc->tx_vq.num_free < 2) {
        /* Drain used TX descriptors to free up space */
        uint16_t used_idx; uint32_t used_len;
        while (vq_poll_used_nonblock(&sc->tx_vq, &used_idx, &used_len) == 0) {
            vq_free_desc(&sc->tx_vq, used_idx);
        }
        if (sc->tx_vq.num_free < 2) {
            m_freem(m);
            return -1; /* TX queue full */
        }
    }

    vq_add_buf(&sc->tx_vq, &tx_hdr, sizeof(tx_hdr), 0);
    vq_add_buf(&sc->tx_vq, sc->tx_bounce_buffer, total_len, 0);
    *sc->notify_reg = 1; /* Kick TX queue */

    /* Poll for TX completion non-blocking — don't spin */
    uint32_t t0 = 0;
    uint16_t used_idx; uint32_t used_len;
    int spins = 5000;
    while (spins-- > 0) {
        if (vq_poll_used_nonblock(&sc->tx_vq, &used_idx, &used_len) == 0) {
            vq_free_desc(&sc->tx_vq, used_idx);
            vq_free_desc(&sc->tx_vq, (used_idx + 1) % sc->tx_vq.size);
            break;
        }
        __asm__ volatile("pause");
    }
    (void)t0;

    m_freem(m);
    return 0;
}

void virtio_net_poll(struct ifnet* ifp) {
    struct virtio_net_softc* sc = ifp->if_softc;
    int max_packets = 32;
    
    while (sc->rx_vq.last_used_idx != sc->rx_vq.used->idx && max_packets-- > 0) {
        uint16_t used_idx; uint32_t used_len;
        if (vq_poll_used_nonblock(&sc->rx_vq, &used_idx, &used_len) != 0) break;
        
        struct mbuf* m = m_getcl(MT_DATA);
        if (m) {
            void* buf = sc->rx_buffers[used_idx];
            struct virtio_net_hdr* hdr = buf;
            uint8_t* payload = (uint8_t*)buf + sizeof(struct virtio_net_hdr);
            uint16_t payload_len = used_len > sizeof(struct virtio_net_hdr) ? used_len - sizeof(struct virtio_net_hdr) : 0;
            
            if (payload_len > 0 && payload_len < 2048) {
                memcpy(m->m_data, payload, payload_len);
                m->m_len = payload_len;
                m->m_pkthdr.len = payload_len;
                m->m_pkthdr.rcvif = ifp;
                m->m_flags |= M_PKTHDR;
                ether_input(ifp, m);
            } else {
                m_free(m);
            }
        }
        
        vq_add_buf(&sc->rx_vq, sc->rx_buffers[used_idx], 2048, VRING_DESC_F_WRITE);
        vq_free_desc(&sc->rx_vq, used_idx);
        *sc->notify_reg = 0;
    }
}

void virtio_net_attach(pci_device_t* dev) {
    pci_enable_bus_mastering(dev);
    struct virtio_net_softc* sc = kmalloc(sizeof(struct virtio_net_softc));
    struct ifnet* ifp = kmalloc(sizeof(struct ifnet));
    memset(sc, 0, sizeof(struct virtio_net_softc));
    memset(ifp, 0, sizeof(struct ifnet));
    
    sc->pci_dev = dev;
    sc->mmio_base = dev->bar_64[0] + hhdm_offset;
    sc->common_cfg = (struct virtio_pci_common_cfg*)sc->mmio_base;
    sc->notify_reg = (uint32_t*)(sc->mmio_base + 0x3000);
    sc->isr_status = (uint8_t*)(sc->mmio_base + 0x3004);
    
    ifp->if_xname[0] = 'v';
    ifp->if_xname[1] = 'n';
    ifp->if_xname[2] = '0';
    ifp->if_xname[3] = 0;
    ifp->if_softc = sc;
    ifp->if_init = virtio_net_init;
    ifp->if_output = virtio_net_output;
    ifp->if_mtu = 1500;
    
    if_attach(ifp);
    virtio_net_init(ifp);
}
