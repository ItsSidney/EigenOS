/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#ifndef VIRTIO_H
#define VIRTIO_H

#include <stdint.h>

// VirtIO PCI Capability types
#define VIRTIO_PCI_CAP_COMMON_CFG        1
#define VIRTIO_PCI_CAP_NOTIFY_CFG        2
#define VIRTIO_PCI_CAP_ISR_CFG           3
#define VIRTIO_PCI_CAP_DEVICE_CFG        4
#define VIRTIO_PCI_CAP_PCI_CFG           5

struct virtio_pci_cap {
    uint8_t cap_vndr;
    uint8_t cap_next;
    uint8_t cap_len;
    uint8_t cfg_type;
    uint8_t bar;
    uint8_t padding[3];
    uint32_t offset;
    uint32_t length;
};

struct virtio_pci_common_cfg {
    uint32_t device_feature_select;
    uint32_t device_feature;
    uint32_t driver_feature_select;
    uint32_t driver_feature;
    uint16_t config_msix_vector;
    uint16_t num_queues;
    uint8_t device_status;
    uint8_t config_generation;
    uint16_t queue_select;
    uint16_t queue_size;
    uint16_t queue_msix_vector;
    uint16_t queue_enable;
    uint16_t queue_notify_off;
    uint64_t queue_desc;
    uint64_t queue_avail;
    uint64_t queue_used;
};

// Virtqueue structures
struct vring_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
};

#define VRING_DESC_F_NEXT  1
#define VRING_DESC_F_WRITE 2

struct vring_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[256]; // Variable size, but 256 is common
};

struct vring_used_elem {
    uint32_t id;
    uint32_t len;
};

struct vring_used {
    uint16_t flags;
    uint16_t idx;
    struct vring_used_elem ring[256];
};

typedef struct {
    struct vring_desc* desc;
    struct vring_avail* avail;
    struct vring_used* used;
    uint16_t size;
    uint16_t free_head;
    uint16_t num_free;
    uint16_t last_used_idx;
    uint16_t avail_idx;
} virtqueue_t;

#endif
