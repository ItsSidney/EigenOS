/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#include "drivers/video/gpu.h"
#include "drivers/bus/pci.h"
#include "drivers/bus/virtio.h"
#include "drivers/video/virtio_gpu_hw.h"
#include "kernel/mem/vmm.h"
#include "kernel/mem/kheap.h"

static inline int abs_int(int v) { return v < 0 ? -v : v; }

static struct virtio_pci_common_cfg* common_cfg;
static uint32_t* notify_reg;
static virtqueue_t control_vq;
static uint32_t main_resource_id = 1;

static void virtio_gpu_send_cmd(void* cmd, size_t cmd_size, void* resp, size_t resp_size) {
    // 1. Fill descriptors
    control_vq.desc[0].addr = vmm_get_phys((uint64_t)cmd);
    control_vq.desc[0].len = cmd_size;
    control_vq.desc[0].flags = VRING_DESC_F_NEXT;
    control_vq.desc[0].next = 1;

    control_vq.desc[1].addr = vmm_get_phys((uint64_t)resp);
    control_vq.desc[1].len = resp_size;
    control_vq.desc[1].flags = VRING_DESC_F_WRITE;

    // 2. Add to available ring
    control_vq.avail->ring[control_vq.avail_idx % control_vq.size] = 0;
    control_vq.avail_idx++;
    control_vq.avail->idx = control_vq.avail_idx;

    // 3. Notify device
    *notify_reg = 0; // Queue 0

    // 4. Wait for used ring
    while (control_vq.used->idx != control_vq.avail_idx) {
        __asm__ volatile("pause");
    }
}

static int virtio_gpu_init(gpu_device_t* gpu) {
    if (gpu->mmio_base == 0) return -1;

    // For VirtIO, we assume BAR4 for common config in QEMU modern
    // We'll use the mapped mmio_base from gpu.c (which should be updated)
    common_cfg = (struct virtio_pci_common_cfg*)gpu->mmio_base;
    notify_reg = (uint32_t*)(gpu->mmio_base + 0x3000); // Typical QEMU offset

    // Reset and Initialize
    common_cfg->device_status = 0;
    common_cfg->device_status = 1; // ACK
    common_cfg->device_status |= 2; // DRIVER

    // Setup Virtqueue
    common_cfg->queue_select = 0;
    uint16_t qsize = common_cfg->queue_size;
    if (qsize == 0) return -1;
    
    control_vq.size = qsize;
    control_vq.desc = (struct vring_desc*)kmalloc_aligned(qsize * sizeof(struct vring_desc), 4096);
    control_vq.avail = (struct vring_avail*)kmalloc_aligned(sizeof(struct vring_avail), 4096);
    control_vq.used = (struct vring_used*)kmalloc_aligned(sizeof(struct vring_used), 4096);
    
    common_cfg->queue_desc = vmm_get_phys((uint64_t)control_vq.desc);
    common_cfg->queue_avail = vmm_get_phys((uint64_t)control_vq.avail);
    common_cfg->queue_used = vmm_get_phys((uint64_t)control_vq.used);
    common_cfg->queue_enable = 1;

    common_cfg->device_status |= 4; // DRIVER_OK

    // Query display info from device (may provide better resolution)
    struct virtio_gpu_resp_display_info disp_info = {0};
    struct virtio_gpu_ctrl_hdr get_disp = {0};
    get_disp.type = VIRTIO_GPU_CTRL_GET_DISPLAY_INFO;
    virtio_gpu_send_cmd(&get_disp, sizeof(get_disp), &disp_info, sizeof(disp_info));
    struct virtio_gpu_display_one* d0 = &disp_info.displays[0];
    if (d0->enabled && d0->r.width > 0 && d0->r.height > 0) {
        gpu->width = d0->r.width;
        gpu->height = d0->r.height;
    }

    // Create 2D Resource for Framebuffer
    struct virtio_gpu_resource_create_2d create = {0};
    create.hdr.type = VIRTIO_GPU_CTRL_RESOURCE_CREATE_2D;
    create.resource_id = main_resource_id;
    create.format = VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM;
    create.width = gpu->width;
    create.height = gpu->height;
    
    struct virtio_gpu_ctrl_hdr resp = {0};
    virtio_gpu_send_cmd(&create, sizeof(create), &resp, sizeof(resp));

    // Attach backing memory
    struct {
        struct virtio_gpu_resource_attach_backing attach;
        struct virtio_gpu_mem_entry entry;
    } attach_cmd = {0};

    attach_cmd.attach.hdr.type = VIRTIO_GPU_CTRL_RESOURCE_ATTACH_BACKING;
    attach_cmd.attach.resource_id = main_resource_id;
    attach_cmd.attach.nr_entries = 1;
    attach_cmd.entry.addr = gpu->phys_fb_base;
    attach_cmd.entry.length = gpu->pitch * gpu->height;
    
    virtio_gpu_send_cmd(&attach_cmd, sizeof(attach_cmd), &resp, sizeof(resp));

    // Set Scanout
    struct virtio_gpu_set_scanout scanout = {0};
    scanout.hdr.type = VIRTIO_GPU_CTRL_SET_SCANOUT;
    scanout.resource_id = main_resource_id;
    scanout.scanout_id = 0;
    scanout.r.x = 0;
    scanout.r.y = 0;
    scanout.r.width = gpu->width;
    scanout.r.height = gpu->height;
    
    virtio_gpu_send_cmd(&scanout, sizeof(scanout), &resp, sizeof(resp));

    gpu->initialized = 1;
    return 0;
}

static int virtio_gpu_accel_2d(gpu_device_t* gpu, gpu_2d_params_t* params) {
    if (!gpu->initialized) return -1;

    extern uint32_t* get_fb_ptr();
    extern uint32_t gfx_get_stride();
    uint32_t* fb = get_fb_ptr();
    uint32_t stride = gfx_get_stride();
    int x = params->dst_x;
    int y = params->dst_y;
    int w = params->width;
    int h = params->height;

    if (params->op == GPU_OP_FILL) {
        for (int i = 0; i < h; i++) {
            int fy = y + i;
            if (fy < 0 || fy >= (int)gpu->height) continue;
            uint32_t* row = fb + fy * stride + x;
            for (int j = 0; j < w; j++) {
                int fx = x + j;
                if (fx < 0 || fx >= (int)gpu->width) continue;
                row[j] = params->color;
            }
        }
    } else if (params->op == GPU_OP_BLIT) {
        uint32_t* src = (uint32_t*)params->src_ptr;
        if (!src) return -1;
        for (int i = 0; i < h; i++) {
            int fy = y + i;
            int sy = params->src_y + i;
            if (fy < 0 || fy >= (int)gpu->height) continue;
            if (sy < 0 || sy >= (int)gpu->height) continue;
            uint32_t* dst_row = fb + fy * stride + x;
            uint32_t* src_row = src + sy * w + params->src_x;
            for (int j = 0; j < w; j++) {
                int fx = x + j;
                int sx = params->src_x + j;
                if (fx < 0 || fx >= (int)gpu->width) continue;
                if (sx < 0 || sx >= (int)gpu->width) continue;
                dst_row[j] = src_row[j];
            }
        }
    } else if (params->op == GPU_OP_BLEND) {
        uint32_t* src = (uint32_t*)params->src_ptr;
        uint8_t alpha = params->alpha;
        uint8_t inv_a = 255 - alpha;
        for (int i = 0; i < h; i++) {
            int fy = y + i;
            if (fy < 0 || fy >= (int)gpu->height) continue;
            uint32_t* dst_row = fb + fy * stride + x;
            uint32_t s = src ? src[i * w] : params->color;
            uint8_t sr = (s >> 16) & 0xFF, sg = (s >> 8) & 0xFF, sb = s & 0xFF;
            for (int j = 0; j < w; j++) {
                int fx = x + j;
                if (fx < 0 || fx >= (int)gpu->width) continue;
                uint32_t d = dst_row[j];
                uint8_t dr = (d >> 16) & 0xFF, dg = (d >> 8) & 0xFF, db = d & 0xFF;
                uint8_t r = (sr * alpha + dr * inv_a) / 255;
                uint8_t g = (sg * alpha + dg * inv_a) / 255;
                uint8_t b = (sb * alpha + db * inv_a) / 255;
                dst_row[j] = (r << 16) | (g << 8) | b;
            }
        }
    } else if (params->op == GPU_OP_LINE) {
        int x0 = x, y0 = y, x1 = params->src_x, y1 = params->src_y;
        uint32_t color = params->color;
        int dx = abs_int(x1 - x0), sx = x0 < x1 ? 1 : -1;
        int dy = -abs_int(y1 - y0), sy = y0 < y1 ? 1 : -1;
        int err = dx + dy;
        while (1) {
            if (x0 >= 0 && x0 < (int)gpu->width && y0 >= 0 && y0 < (int)gpu->height)
                fb[y0 * stride + x0] = color;
            if (x0 == x1 && y0 == y1) break;
            int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    } else if (params->op == GPU_OP_RECT) {
        for (int i = 0; i < w; i++) {
            if (y >= 0 && y < (int)gpu->height) {
                int fx = x + i;
                if (fx >= 0 && fx < (int)gpu->width) fb[y * stride + fx] = params->color;
            }
            int by = y + h - 1;
            if (by >= 0 && by < (int)gpu->height) {
                int fx = x + i;
                if (fx >= 0 && fx < (int)gpu->width) fb[by * stride + fx] = params->color;
            }
        }
        for (int i = 0; i < h; i++) {
            if (x >= 0 && x < (int)gpu->width) {
                int fy = y + i;
                if (fy >= 0 && fy < (int)gpu->height) fb[fy * stride + x] = params->color;
            }
            int bx = x + w - 1;
            if (bx >= 0 && bx < (int)gpu->width) {
                int fy = y + i;
                if (fy >= 0 && fy < (int)gpu->height) fb[fy * stride + bx] = params->color;
            }
        }
    } else if (params->op == GPU_OP_COPY) {
        for (int i = 0; i < h; i++) {
            int fy = y + i;
            int sy = params->src_y + i;
            if (fy < 0 || fy >= (int)gpu->height) continue;
            if (sy < 0 || sy >= (int)gpu->height) continue;
            uint32_t* dst_row = fb + fy * stride + x;
            uint32_t* src_row = fb + sy * stride + params->src_x;
            for (int j = 0; j < w; j++) {
                int fx = x + j;
                int sx = params->src_x + j;
                if (fx < 0 || fx >= (int)gpu->width) continue;
                if (sx < 0 || sx >= (int)gpu->width) continue;
                dst_row[j] = src_row[j];
            }
        }
    } else {
        return -1;
    }

    struct virtio_gpu_transfer_to_host_2d xfer = {0};
    xfer.hdr.type = VIRTIO_GPU_CTRL_TRANSFER_TO_HOST_2D;
    xfer.resource_id = main_resource_id;
    xfer.r.x = x; xfer.r.y = y;
    xfer.r.width = w; xfer.r.height = h;

    struct virtio_gpu_ctrl_hdr resp = {0};
    virtio_gpu_send_cmd(&xfer, sizeof(xfer), &resp, sizeof(resp));

    struct virtio_gpu_resource_flush flush = {0};
    flush.hdr.type = VIRTIO_GPU_CTRL_RESOURCE_FLUSH;
    flush.resource_id = main_resource_id;
    flush.r = xfer.r;
    virtio_gpu_send_cmd(&flush, sizeof(flush), &resp, sizeof(resp));

    return 0;
}

static int virtio_gpu_accel_3d(gpu_device_t* gpu, void* cmd_buffer, size_t size) {
    if (!gpu->initialized) return -1;
    extern uint32_t* get_fb_ptr();
    extern uint32_t gfx_get_stride();
    uint32_t* fb = get_fb_ptr();
    uint32_t stride = gfx_get_stride();

    // Minimal 3D command: expect exactly one triangle_t in cmd_buffer
    if (!cmd_buffer || size < sizeof(triangle_t)) return -1;
    triangle_t* tri = (triangle_t*)cmd_buffer;

    // Simple bounding-box rasterization
    int min_x = tri->p[0].x; if (tri->p[1].x < min_x) min_x = tri->p[1].x; if (tri->p[2].x < min_x) min_x = tri->p[2].x;
    int min_y = tri->p[0].y; if (tri->p[1].y < min_y) min_y = tri->p[1].y; if (tri->p[2].y < min_y) min_y = tri->p[2].y;
    int max_x = tri->p[0].x; if (tri->p[1].x > max_x) max_x = tri->p[1].x; if (tri->p[2].x > max_x) max_x = tri->p[2].x;
    int max_y = tri->p[0].y; if (tri->p[1].y > max_y) max_y = tri->p[1].y; if (tri->p[2].y > max_y) max_y = tri->p[2].y;

    if (min_x < 0) min_x = 0;
    if (min_y < 0) min_y = 0;
    if (max_x >= (int)gpu->width) max_x = gpu->width - 1;
    if (max_y >= (int)gpu->height) max_y = gpu->height - 1;

    uint32_t color = tri->color;
    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            fb[y * stride + x] = color;
        }
    }

    struct virtio_gpu_transfer_to_host_2d xfer = {0};
    xfer.hdr.type = VIRTIO_GPU_CTRL_TRANSFER_TO_HOST_2D;
    xfer.resource_id = main_resource_id;
    xfer.r.x = min_x; xfer.r.y = min_y;
    xfer.r.width = (uint32_t)(max_x - min_x + 1);
    xfer.r.height = (uint32_t)(max_y - min_y + 1);

    struct virtio_gpu_ctrl_hdr resp = {0};
    virtio_gpu_send_cmd(&xfer, sizeof(xfer), &resp, sizeof(resp));

    struct virtio_gpu_resource_flush flush = {0};
    flush.hdr.type = VIRTIO_GPU_CTRL_RESOURCE_FLUSH;
    flush.resource_id = main_resource_id;
    flush.r = xfer.r;
    virtio_gpu_send_cmd(&flush, sizeof(flush), &resp, sizeof(resp));

    return 0;
}

static uint32_t virtio_gpu_get_caps(gpu_device_t* gpu) {
    (void)gpu;
    return GPU_CAP_2D | GPU_CAP_3D | GPU_CAP_BLEND;
}

static int virtio_gpu_set_mode(gpu_device_t* gpu, int width, int height, int bpp) {
    (void)gpu; (void)width; (void)height; (void)bpp;
    return 0; // Mode already set by firmware/Limine
}

static void* virtio_gpu_get_framebuffer(gpu_device_t* gpu) {
    return (void*)gpu->fb_base;
}

static void virtio_gpu_flip(gpu_device_t* gpu) {
    if (!gpu->initialized) return;
    struct virtio_gpu_resource_flush flush = {0};
    flush.hdr.type = VIRTIO_GPU_CTRL_RESOURCE_FLUSH;
    flush.resource_id = main_resource_id;
    flush.r.x = 0;
    flush.r.y = 0;
    flush.r.width = gpu->width;
    flush.r.height = gpu->height;
    struct virtio_gpu_ctrl_hdr resp = {0};
    virtio_gpu_send_cmd(&flush, sizeof(flush), &resp, sizeof(resp));
}

gpu_driver_t virtio_gpu_driver = {
    .init = virtio_gpu_init,
    .set_mode = virtio_gpu_set_mode,
    .get_framebuffer = virtio_gpu_get_framebuffer,
    .flip = virtio_gpu_flip,
    .accel_2d = virtio_gpu_accel_2d,
    .accel_3d = virtio_gpu_accel_3d,
    .get_caps = virtio_gpu_get_caps
};
