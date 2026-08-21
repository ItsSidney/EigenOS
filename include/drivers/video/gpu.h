/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#ifndef GPU_H
#define GPU_H

#include <stdint.h>
#include <stddef.h>

// GPU types
#define GPU_TYPE_INTEL   0x8086
#define GPU_TYPE_NVIDIA  0x10DE
#define GPU_TYPE_AMD     0x1002
#define GPU_TYPE_VIRTIO  0x1AF4

// Intel Specific Device IDs
#define INTEL_HD_4000     0x0166  // Ivy Bridge HD 4000
#define INTEL_HD_4000_ALT 0x0162  // Ivy Bridge HD 4000 (alternative)
#define INTEL_HD_520      0x1916  // Skylake HD 520
#define INTEL_HD_520_ALT  0x1921  // Skylake HD 520 (alternative)
#define INTEL_HD_520_MIN  0x1906  // Skylake HD 515/520 (min)
#define INTEL_HD_620      0x5916  // Kaby Lake HD 620
#define INTEL_UHD_620     0x5917  // Kaby Lake R UHD 620

// GPU device structure
typedef struct gpu_device {
    uint16_t vendor_id;
    uint16_t device_id;
    uint64_t mmio_base;   // Virtual BAR0
    uint64_t fb_base;     // Virtual BAR2
    uint64_t phys_fb_base;// Physical BAR2
    uint32_t vram_size;
    uint32_t mmio_size;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    int initialized;
    char name[64];
    struct gpu_driver* driver;
} gpu_device_t;

// 2D Acceleration operations
typedef struct {
    int op;
    int src_x, src_y;
    int dst_x, dst_y;
    int width, height;
    uint32_t color;
    void* src_ptr;
    void* dst_ptr;
    int alpha; // 0-255 for "fully rendered" effects
} gpu_2d_params_t;

#define GPU_OP_FILL  1
#define GPU_OP_BLIT  2
#define GPU_OP_BLEND 3
#define GPU_OP_3D_TRI 4
#define GPU_OP_LINE  5
#define GPU_OP_RECT  6
#define GPU_OP_COPY  7

#define GPU_CAP_2D    (1 << 0)
#define GPU_CAP_3D    (1 << 1)
#define GPU_CAP_BLEND (1 << 2)

// Driver operations
typedef struct gpu_driver {
    int (*init)(gpu_device_t* gpu);
    int (*set_mode)(gpu_device_t* gpu, int width, int height, int bpp);
    void* (*get_framebuffer)(gpu_device_t* gpu);
    void (*flip)(gpu_device_t* gpu);
    int (*accel_2d)(gpu_device_t* gpu, gpu_2d_params_t* params);
    int (*accel_3d)(gpu_device_t* gpu, void* cmd_buffer, size_t size);
    uint32_t (*get_caps)(gpu_device_t* gpu);
} gpu_driver_t;

// 3D Math & Engine
typedef struct { float x, y, z; } vec3_t;
typedef struct { float m[4][4]; } mat4_t;
typedef struct { vec3_t p[3]; uint32_t color; } triangle_t;

// API
void gpu_init(void);
gpu_device_t* gpu_get_primary(void);
int gpu_accel_fill(int x, int y, int w, int h, uint32_t color);
int gpu_accel_blit(void* src, int sx, int sy, int dx, int dy, int w, int h);
int gpu_accel_blend(void* src, int dx, int dy, int w, int h, int alpha);
int gpu_accel_line(int x0, int y0, int x1, int y1, uint32_t color);
int gpu_accel_rect(int x, int y, int w, int h, uint32_t color);
int gpu_accel_copy(int sx, int sy, int dx, int dy, int w, int h);
int gpu_accel_3d_tri(void* tri, size_t size);
int gpu_accel_3d_test(void);
uint32_t gpu_get_capabilities(void);
void gpu_present(void);
int gpu_detect_3d(void);

#endif
