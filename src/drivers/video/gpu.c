/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#include "drivers/video/gpu.h"
#include "drivers/bus/pci.h"
#include "drivers/video/framebuffer.h"
#include "drivers/video/gfx.h"
#include "kernel/mem/vmm.h"

extern gpu_driver_t intel_gpu_driver;
extern gpu_driver_t virtio_gpu_driver;

static gpu_device_t primary_gpu;
static int has_gpu = 0;

extern uint64_t hhdm_offset;

#define GPU_MMIO_VIRT    0xFFFF900000000000
#define GPU_FB_VIRT      0xFFFFA00000000000

extern void serial_puts(const char* s);
extern void itoa(uint64_t n, char* s);

static inline int abs_int(int v) { return v < 0 ? -v : v; }

static uint64_t get_pci_bar_addr(pci_device_t* pci, int bar_idx) {
    uint32_t bar_val = pci->bar[bar_idx];
    if ((bar_val & 0x6) == 0x04) { // 64-bit BAR
        return (uint64_t)(bar_val & ~0xF) | ((uint64_t)pci->bar[bar_idx + 1] << 32);
    }
    return (uint64_t)(bar_val & ~0xF);
}

void gpu_init(void) {
    int pci_count = pci_get_device_count();
    char buf[32];
    for (int i = 0; i < pci_count; i++) {
        pci_device_t* pci = pci_get_device(i);
        
        if (pci->vendor_id == 0x8086 && pci->class_id == 0x03) {
            
            primary_gpu.vendor_id = pci->vendor_id;
            primary_gpu.device_id = pci->device_id;
            
            uint64_t mmio_phys = get_pci_bar_addr(pci, 0);
            uint64_t fb_phys = get_pci_bar_addr(pci, 2);

            /* Real machines can report bogus/zero BARs we must not map. */
            if (mmio_phys == 0 || (mmio_phys & ~0xFFFFFFFFFFULL) != 0) {
                serial_puts("[GPU] Intel GPU BAR0 invalid, using software rendering\n");
                has_gpu = 0;
                return;
            }

            primary_gpu.mmio_size = 16 * 1024 * 1024;
            
            vmm_map_range(GPU_MMIO_VIRT, mmio_phys, primary_gpu.mmio_size, VMM_WRITE | VMM_PCD);
            if (fb_phys != 0 && (fb_phys & ~0xFFFFFFFFFFULL) == 0) {
                /* Map only a sane window of the framebuffer aperture. */
                vmm_map_range(GPU_FB_VIRT, fb_phys, 16 * 1024 * 1024, VMM_WRITE | VMM_PCD);
            }

            primary_gpu.mmio_base = GPU_MMIO_VIRT;
            primary_gpu.fb_base = GPU_FB_VIRT;
            primary_gpu.phys_fb_base = fb_phys;
            
            extern uint32_t get_fb_width();
            extern uint32_t get_fb_height();
            extern uint32_t gfx_get_stride();
            
            primary_gpu.width = get_fb_width();
            primary_gpu.height = get_fb_height();
            primary_gpu.pitch = gfx_get_stride() * 4; // Assuming 32bpp

            primary_gpu.initialized = 0;
            primary_gpu.driver = &intel_gpu_driver;
            
            const char* name = "Intel Graphics (Compatible Mode)";
            if (pci->device_id == INTEL_HD_4000 || pci->device_id == INTEL_HD_4000_ALT)
                name = "Intel HD Graphics 4000 (Ivy Bridge)";
            else if (pci->device_id == INTEL_HD_620)
                name = "Intel HD Graphics 620 (Kaby Lake)";
            else if (pci->device_id == INTEL_UHD_620)
                name = "Intel UHD Graphics 620 (Kaby Lake R)";
            else if (pci->device_id == INTEL_HD_520 || pci->device_id == INTEL_HD_520_ALT || pci->device_id == INTEL_HD_520_MIN)
                name = "Intel HD Graphics 520 (Skylake)";

            int k = 0;
            while (name[k]) { primary_gpu.name[k] = name[k]; k++; }
            primary_gpu.name[k] = 0;

            if (primary_gpu.driver->init(&primary_gpu) == 0) {
                has_gpu = 1;
                return;
            }
        }

        if (pci->vendor_id == 0x1AF4 && (pci->device_id == 0x1010 || pci->device_id == 0x1050 || pci->device_id == 0x1009)) {
            primary_gpu.vendor_id = pci->vendor_id;
            primary_gpu.device_id = pci->device_id;

            uint64_t mmio_phys = get_pci_bar_addr(pci, 4); // BAR4: Common Config / Notify
            uint64_t fb_phys = get_pci_bar_addr(pci, 2);   // BAR2: Framebuffer

            // Map VirtIO BARs
            vmm_map_range(GPU_MMIO_VIRT, mmio_phys, 16 * 1024 * 1024, VMM_WRITE | VMM_PCD);
            vmm_map_range(GPU_FB_VIRT, fb_phys, 256 * 1024 * 1024, VMM_WRITE | VMM_PCD);

            primary_gpu.mmio_base = GPU_MMIO_VIRT;
            primary_gpu.fb_base = GPU_FB_VIRT;
            primary_gpu.phys_fb_base = fb_phys;

            extern uint32_t get_fb_width();
            extern uint32_t get_fb_height();
            extern uint32_t gfx_get_stride();
            
            primary_gpu.width = get_fb_width();
            primary_gpu.height = get_fb_height();
            primary_gpu.pitch = gfx_get_stride() * 4;

            primary_gpu.initialized = 0;
            primary_gpu.driver = &virtio_gpu_driver;

            const char* name = "VirtIO GPU (Compatible Mode)";
            int k = 0;
            while (name[k]) { primary_gpu.name[k] = name[k]; k++; }
            primary_gpu.name[k] = 0;
            
            if (primary_gpu.driver->init(&primary_gpu) == 0) {
                has_gpu = 1;
                return;
            }
        }

        // 3. Check for QEMU VGA
        if (pci->vendor_id == 0x1234 && pci->device_id == 0x1111) {
            primary_gpu.vendor_id = pci->vendor_id;
            primary_gpu.device_id = pci->device_id;
            primary_gpu.initialized = 1;
            primary_gpu.driver = NULL;

            const char* name = "Standard VGA (Compatible Mode)";
            int k = 0;
            while (name[k]) { primary_gpu.name[k] = name[k]; k++; }
            primary_gpu.name[k] = 0;

            extern uint32_t get_fb_width();
            extern uint32_t get_fb_height();
            extern uint32_t gfx_get_stride();
            primary_gpu.width = get_fb_width();
            primary_gpu.height = get_fb_height();
            primary_gpu.pitch = gfx_get_stride() * 4;

            has_gpu = 1;
            return;
        }

        // 4. Generic fallback for any display controller (Boxes, VMware, etc.)
        if (pci->class_id == 0x03 && !has_gpu) {
            primary_gpu.vendor_id = pci->vendor_id;
            primary_gpu.device_id = pci->device_id;
            primary_gpu.initialized = 1;
            primary_gpu.driver = NULL;

            const char* name = "Generic Virtual Display Controller (Compatible Mode)";
            int k = 0;
            while (name[k]) { primary_gpu.name[k] = name[k]; k++; }
            primary_gpu.name[k] = 0;

            extern uint32_t get_fb_width();
            extern uint32_t get_fb_height();
            extern uint32_t gfx_get_stride();
            primary_gpu.width = get_fb_width();
            primary_gpu.height = get_fb_height();
            primary_gpu.pitch = gfx_get_stride() * 4;

            has_gpu = 1;
            return;
        }
    }
}

gpu_device_t* gpu_get_primary(void) { return has_gpu ? &primary_gpu : NULL; }

int gpu_accel_fill(int x, int y, int w, int h, uint32_t color) {
    gfx_fill_rect(x, y, w, h, color);
    return 0;
}

int gpu_accel_blit(void* src, int sx, int sy, int dx, int dy, int w, int h) {
    gpu_2d_params_t p = { .op = GPU_OP_BLIT, .src_x = sx, .src_y = sy, .dst_x = dx, .dst_y = dy, .width = w, .height = h, .src_ptr = src };
    if (has_gpu && primary_gpu.driver && primary_gpu.driver->accel_2d) {
        int r = primary_gpu.driver->accel_2d(&primary_gpu, &p);
        if (r == 0) return 0;
    }
    extern uint32_t* gfx_get_back_buffer(void);
    extern uint32_t  gfx_get_stride(void);
    uint32_t* bb = gfx_get_back_buffer();
    uint32_t  stride = gfx_get_stride();
    uint32_t* s = (uint32_t*)src;
    for (int i = 0; i < h; i++) {
        int fy = dy + i;
        int fsy = sy + i;
        if (fy < 0 || fy >= (int)gfx_get_fb_height()) continue;
        if (fsy < 0 || fsy >= (int)gfx_get_fb_height()) continue;
        uint32_t* dst_row = bb + fy * stride + dx;
        uint32_t* src_row = s + fsy * w + sx;
        for (int j = 0; j < w; j++) {
            int fx = dx + j;
            int fsx = sx + j;
            if (fx < 0 || fx >= (int)gfx_get_fb_width()) continue;
            if (fsx < 0 || fsx >= (int)gfx_get_fb_width()) continue;
            dst_row[j] = src_row[j];
        }
    }
    return 0;
}

int gpu_accel_blend(void* src, int dx, int dy, int w, int h, int alpha) {
    gpu_2d_params_t p = { .op = GPU_OP_BLEND, .dst_x = dx, .dst_y = dy, .width = w, .height = h, .src_ptr = src, .alpha = alpha };
    if (has_gpu && primary_gpu.driver && primary_gpu.driver->accel_2d) {
        int r = primary_gpu.driver->accel_2d(&primary_gpu, &p);
        if (r == 0) return 0;
    }
    extern uint32_t* gfx_get_back_buffer(void);
    extern uint32_t  gfx_get_stride(void);
    uint32_t* bb = gfx_get_back_buffer();
    uint32_t  stride = gfx_get_stride();
    uint32_t* s = (uint32_t*)src;
    uint8_t a = alpha, inv_a = 255 - a;
    for (int i = 0; i < h; i++) {
        int fy = dy + i;
        if (fy < 0 || fy >= (int)gfx_get_fb_height()) continue;
        uint32_t* dst_row = bb + fy * stride + dx;
        uint32_t src_pix = s ? s[i * w] : 0;
        uint8_t sr = (src_pix >> 16) & 0xFF, sg = (src_pix >> 8) & 0xFF, sb = src_pix & 0xFF;
        for (int j = 0; j < w; j++) {
            int fx = dx + j;
            if (fx < 0 || fx >= (int)gfx_get_fb_width()) continue;
            uint32_t d = dst_row[j];
            uint8_t dr = (d >> 16) & 0xFF, dg = (d >> 8) & 0xFF, db = d & 0xFF;
            uint8_t r = (sr * a + dr * inv_a) / 255;
            uint8_t g = (sg * a + dg * inv_a) / 255;
            uint8_t b = (sb * a + db * inv_a) / 255;
            dst_row[j] = (r << 16) | (g << 8) | b;
        }
    }
    return 0;
}

int gpu_accel_line(int x0, int y0, int x1, int y1, uint32_t color) {
    gpu_2d_params_t p = { .op = GPU_OP_LINE, .dst_x = x0, .dst_y = y0, .src_x = x1, .src_y = y1, .width = 0, .height = 0, .color = color };
    if (has_gpu && primary_gpu.driver && primary_gpu.driver->accel_2d) {
        int r = primary_gpu.driver->accel_2d(&primary_gpu, &p);
        if (r == 0) return 0;
    }
    extern uint32_t* gfx_get_back_buffer(void);
    extern uint32_t  gfx_get_stride(void);
    uint32_t* bb = gfx_get_back_buffer();
    uint32_t  stride = gfx_get_stride();
    int dx = abs_int(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs_int(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (1) {
        if (x0 >= 0 && x0 < (int)gfx_get_fb_width() && y0 >= 0 && y0 < (int)gfx_get_fb_height())
            bb[y0 * stride + x0] = color;
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
    return 0;
}

int gpu_accel_rect(int x, int y, int w, int h, uint32_t color) {
    gpu_2d_params_t p = { .op = GPU_OP_RECT, .dst_x = x, .dst_y = y, .width = w, .height = h, .color = color };
    if (has_gpu && primary_gpu.driver && primary_gpu.driver->accel_2d) {
        int r = primary_gpu.driver->accel_2d(&primary_gpu, &p);
        if (r == 0) return 0;
    }
    gfx_draw_rect_outline(x, y, w, h, 1, color);
    return 0;
}

int gpu_accel_copy(int sx, int sy, int dx, int dy, int w, int h) {
    gpu_2d_params_t p = { .op = GPU_OP_COPY, .src_x = sx, .src_y = sy, .dst_x = dx, .dst_y = dy, .width = w, .height = h };
    if (has_gpu && primary_gpu.driver && primary_gpu.driver->accel_2d) {
        int r = primary_gpu.driver->accel_2d(&primary_gpu, &p);
        if (r == 0) return 0;
    }
    extern uint32_t* gfx_get_back_buffer(void);
    extern uint32_t  gfx_get_stride(void);
    uint32_t* bb = gfx_get_back_buffer();
    uint32_t  stride = gfx_get_stride();
    for (int i = 0; i < h; i++) {
        int fy = dy + i;
        int fsy = sy + i;
        if (fy < 0 || fy >= (int)gfx_get_fb_height()) continue;
        if (fsy < 0 || fsy >= (int)gfx_get_fb_height()) continue;
        uint32_t* dst_row = bb + fy * stride + dx;
        uint32_t* src_row = bb + fsy * stride + sx;
        for (int j = 0; j < w; j++) {
            int fx = dx + j;
            int fsx = sx + j;
            if (fx < 0 || fx >= (int)gfx_get_fb_width()) continue;
            if (fsx < 0 || fsx >= (int)gfx_get_fb_width()) continue;
            dst_row[j] = src_row[j];
        }
    }
    return 0;
}

int gpu_accel_3d_tri(void* tri, size_t size) {
    if (has_gpu && primary_gpu.driver && primary_gpu.driver->accel_3d) {
        int r = primary_gpu.driver->accel_3d(&primary_gpu, tri, size);
        if (r == 0) return 0;
    }
    // software fallback: solid-colored bounding box
    if (!tri || size < sizeof(triangle_t)) return -1;
    triangle_t* t = (triangle_t*)tri;
    extern uint32_t* gfx_get_back_buffer(void);
    extern uint32_t  gfx_get_stride(void);
    uint32_t* bb = gfx_get_back_buffer();
    uint32_t  stride = gfx_get_stride();
    int min_x = t->p[0].x; if (t->p[1].x < min_x) min_x = t->p[1].x; if (t->p[2].x < min_x) min_x = t->p[2].x;
    int min_y = t->p[0].y; if (t->p[1].y < min_y) min_y = t->p[1].y; if (t->p[2].y < min_y) min_y = t->p[2].y;
    int max_x = t->p[0].x; if (t->p[1].x > max_x) max_x = t->p[1].x; if (t->p[2].x > max_x) max_x = t->p[2].x;
    int max_y = t->p[0].y; if (t->p[1].y > max_y) max_y = t->p[1].y; if (t->p[2].y > max_y) max_y = t->p[2].y;
    if (min_x < 0) min_x = 0;
    if (min_y < 0) min_y = 0;
    if (max_x >= (int)gfx_get_fb_width()) max_x = gfx_get_fb_width() - 1;
    if (max_y >= (int)gfx_get_fb_height()) max_y = gfx_get_fb_height() - 1;
    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            bb[y * stride + x] = t->color;
        }
    }
    return 0;
}

uint32_t gpu_get_capabilities(void) {
    if (!has_gpu || !primary_gpu.driver || !primary_gpu.driver->get_caps) return 0;
    return primary_gpu.driver->get_caps(&primary_gpu);
}

int gpu_accel_3d_test(void) {
    if (!has_gpu || !primary_gpu.driver || !primary_gpu.driver->accel_3d) return -1;
    return primary_gpu.driver->accel_3d(&primary_gpu, NULL, 0);
}

void gpu_present(void) {
    if (!has_gpu || !primary_gpu.driver || !primary_gpu.driver->flip) return;
    primary_gpu.driver->flip(&primary_gpu);
}

int gpu_detect_3d(void) {
    if (!has_gpu) return 0;
    uint32_t caps = gpu_get_capabilities();
    return (caps & GPU_CAP_3D) ? 1 : 0;
}
