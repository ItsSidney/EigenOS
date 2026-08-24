/* virtio_blk.c — VirtIO block device driver (legacy spec, polling mode).
 *
 * Finds PCI 1AF4:1001, negotiates a single-page vring, exposes
 * read/write sector ops through block_device_t for persist.c.
 * All DMA addresses are converted from virtual to physical via vmm_get_phys.
 */

#include "drivers/storage/storage.h"
#include "drivers/bus/pci.h"
#include "kernel/mem/vmm.h"
#include <stdint.h>
#include <string.h>

extern void serial_puts(const char* s);
extern void serial_u64(uint64_t v);
void* kmalloc(unsigned long sz);
void kfree(void* p);

/* legacy virtio PCI BAR0 offsets (spec §4.1.4 table) */
#define VIRTIO_PCI_HOST_FEATURES   0x00
#define VIRTIO_PCI_GUEST_FEATURES  0x04
#define VIRTIO_PCI_QUEUE_PFN       0x08
#define VIRTIO_PCI_QUEUE_NUM       0x0C
#define VIRTIO_PCI_QUEUE_SEL       0x0E
#define VIRTIO_PCI_QUEUE_NOTIFY    0x10
#define VIRTIO_PCI_STATUS          0x12
#define VIRTIO_PCI_ISR             0x13
#define PAGE_SIZE_4K               4096
#define VIRTIO_PCI_DEVICE_CFG      0x14

/* status bits */
#define VS_ACK        1
#define VS_DRIVER     2
#define VS_DRIVER_OK  4

/* request types */
#define VT_IN   0
#define VT_OUT  1

/* descriptor flags */
#define VF_NEXT   1
#define VF_WRITE  2

#define VQ_SIZE   8    /* tiny queue: one request at a time */

typedef struct __attribute__((packed)) {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} vq_desc_t;

typedef struct __attribute__((packed)) {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[VQ_SIZE];
} vq_avail_t;

typedef struct __attribute__((packed)) {
    uint32_t id;
    uint32_t len;
} vq_used_elem_t;

typedef struct __attribute__((packed)) {
    uint16_t flags;
    uint16_t idx;
    vq_used_elem_t ring[VQ_SIZE];
} vq_used_t;

static struct {
    uint16_t io_base;
    uint32_t qsize;
    vq_desc_t*  descs;
    vq_avail_t* avail;
    vq_used_t*  used;
    int ready;
} vb;

/* I/O accessors */
static inline uint32_t vinl(uint16_t p){ uint32_t r; __asm__ volatile("inl %1,%0":"=a"(r):"Nd"(p)); return r; }
static inline void voutl(uint16_t p, uint32_t v){ __asm__ volatile("outl %0,%1"::"a"(v),"Nd"(p)); }
static inline void voutw(uint16_t p, uint16_t v){ __asm__ volatile("outw %0,%1"::"a"(v),"Nd"(p)); }
static inline void voutb(uint16_t p, uint8_t v){ __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p)); }
static inline uint16_t vinw(uint16_t p){ uint16_t r; __asm__ volatile("inw %1,%0":"=a"(r):"Nd"(p)); return r; }
static inline uint8_t vinb(uint16_t p){ uint8_t r; __asm__ volatile("inb %1,%0":"=a"(r):"Nd"(p)); return r; }

/* transfer ≤7 sectors in one chain */
static int vblk_xfer(int write, uint64_t lba, uint32_t count, void* buf) {
    if (!vb.ready || !buf || count == 0 || count > 6) return -1;

    /* request header (must be accessible to device via DMA) */
    struct { uint32_t type, _rsvd; uint64_t sector; } hdr = {
        write ? VT_OUT : VT_IN, 0, lba
    };
    uint8_t dev_st = 255;

    /* convert virtual→physical for DMA */
    uint64_t hdr_phys = vmm_get_phys((uint64_t)(uintptr_t)&hdr);
    uint64_t buf_phys = vmm_get_phys((uint64_t)(uintptr_t)buf);
    uint64_t st_phys  = vmm_get_phys((uint64_t)(uintptr_t)&dev_st);

    if (!hdr_phys || !buf_phys || !st_phys) return -1;

    /* build 3-descriptor chain */
    vb.descs[0].addr  = hdr_phys;
    vb.descs[0].len   = sizeof(hdr);
    vb.descs[0].flags = VF_NEXT;
    vb.descs[0].next  = 1;

    vb.descs[1].addr  = buf_phys;
    vb.descs[1].len   = count * 512;
    vb.descs[1].flags = VF_NEXT | (write ? 0 : VF_WRITE);
    vb.descs[1].next  = 2;

    vb.descs[2].addr  = st_phys;
    vb.descs[2].len   = 1;
    vb.descs[2].flags = VF_WRITE;
    vb.descs[2].next  = 0;

    /* push head descriptor index into available ring */
    uint16_t last = vb.used->idx;
    uint16_t ai = vb.avail->idx % vb.qsize;
    vb.avail->ring[ai] = 0;
    __atomic_store_n(&vb.avail->idx, (uint16_t)(vb.avail->idx + 1), __ATOMIC_RELEASE);

    /* notify */
    voutw(vb.io_base + VIRTIO_PCI_QUEUE_NOTIFY, 0);

    /* poll used ring */
    int spins = 0;
    while (__atomic_load_n(&vb.used->idx, __ATOMIC_ACQUIRE) == last) {
        if (++spins > 100000000) {
            serial_puts("[VBLK] poll timeout\n");
            return -1;
        }
    }

    if (dev_st != 0) {
        serial_puts("[VBLK] status err\n");
        return -1;
    }
    return 0;
}

static int vblk_read(block_device_t* d, uint64_t lba, uint32_t count, void* buf) {
    (void)d;
    while (count > 0) {
        uint32_t c = count > 6 ? 6 : count;
        if (vblk_xfer(0, lba, c, buf) != 0) return -1;
        lba += c; buf = (uint8_t*)buf + c * 512; count -= c;
    }
    return 0;
}

static int vblk_write(block_device_t* d, uint64_t lba, uint32_t count, const void* buf) {
    (void)d;
    while (count > 0) {
        uint32_t c = count > 6 ? 6 : count;
        if (vblk_xfer(1, lba, c, (void*)buf) != 0) return -1;
        lba += c; buf = (const uint8_t*)buf + c * 512; count -= c;
    }
    return 0;
}

static block_device_t vblk_dev;

void virtio_blk_init(void) {
    vb.ready = 0;

    pci_device_t* pd = 0;
    for (int i = 0; i < pci_get_device_count(); i++) {
        pci_device_t* d = pci_get_device(i);
        if (d && d->vendor_id == 0x1AF4 && d->device_id == 0x1001) { pd = d; break; }
    }
    if (!pd) { serial_puts("[VBLK] no PCI device\n"); return; }

    serial_puts("[VBLK] found ");
    serial_u64((uint64_t)pd->bus); serial_puts(":");
    serial_u64((uint64_t)pd->slot); serial_puts("\n");

    uint32_t bar0 = pd->bar[0];
    if (!(bar0 & 1)) { serial_puts("[VBLK] BAR0 not I/O\n"); return; }
    vb.io_base = (uint16_t)(bar0 & ~3u);

    /* reset + ack + driver */
    voutb(vb.io_base + VIRTIO_PCI_STATUS, 0);
    vinb(vb.io_base + VIRTIO_PCI_STATUS); /* flush */
    voutb(vb.io_base + VIRTIO_PCI_STATUS, VS_ACK | VS_DRIVER);

    /* no special features needed */
    voutl(vb.io_base + VIRTIO_PCI_GUEST_FEATURES, 0);

    /* select q0, read max size */
    voutw(vb.io_base + VIRTIO_PCI_QUEUE_SEL, 0);
    uint32_t qmax = vinw(vb.io_base + VIRTIO_PCI_QUEUE_NUM);
    vb.qsize = (qmax > 0 && qmax <= VQ_SIZE) ? qmax : VQ_SIZE;

    /* allocate vring: single page (descriptors + avail fit easily,
       used ring packed after avail — fine for VQ_SIZE=8) */
    /* Two contiguous pages from BSS: guaranteed physically contiguous
     * because the kernel image is loaded as one contiguous block. */
    static __attribute__((aligned(4096))) uint8_t vring_pages[8192];
    memset(vring_pages, 0, sizeof(vring_pages));
    uint8_t* vr = vring_pages;

    uint64_t vr_phys = vmm_get_phys((uint64_t)(uintptr_t)vr);
    if (!vr_phys) { kfree(vr); serial_puts("[VBLK] no phys\n"); return; }

    vb.descs = (vq_desc_t*)vr;
    vb.avail = (vq_avail_t*)(vr + sizeof(vq_desc_t) * vb.qsize);
    /* used ring: align to even boundary right after avail */
    uint64_t avail_end = sizeof(vq_desc_t) * vb.qsize +
                         sizeof(uint16_t) * 3 + sizeof(uint16_t) * vb.qsize;
    uint64_t uoff = (avail_end + PAGE_SIZE_4K - 1) & ~(PAGE_SIZE_4K - 1);
    vb.used = (vq_used_t*)(vr + uoff);

    /* set PFN */
    voutl(vb.io_base + VIRTIO_PCI_QUEUE_PFN, (uint32_t)(vr_phys / PAGE_SIZE_4K));

    /* driver OK */
    uint8_t st = vinb(vb.io_base + VIRTIO_PCI_STATUS);
    voutb(vb.io_base + VIRTIO_PCI_STATUS, st | VS_DRIVER_OK);

    /* capacity from device config (legacy blk cfg @ BAR0+0x14) */
    uint32_t lo = vinl(vb.io_base + VIRTIO_PCI_DEVICE_CFG);
    uint32_t hi = vinl(vb.io_base + VIRTIO_PCI_DEVICE_CFG + 4);
    uint64_t cap = ((uint64_t)hi << 32) | lo;

    strcpy(vblk_dev.name, "vblk0");
    vblk_dev.block_size = 512;
    vblk_dev.size_sectors = cap;
    vblk_dev.read  = vblk_read;
    vblk_dev.write = vblk_write;
    storage_register_device(&vblk_dev);

    vb.ready = 1;
    serial_puts("[VBLK] ready cap=");
    serial_u64(cap);
    serial_puts(" sectors\n");
}
