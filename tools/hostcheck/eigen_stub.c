/* eigen_stub.c — host-only stubs for the EigenOS syscall wrappers.
 * Lets us link-check imgui_impl_eigen + imguitest on Linux without
 * the kernel. NEVER builds into an ISO; only for `make hostcheck`. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <user/eigen.h>   // eigen_ev_t + EIGEN_EV_* (host link test)

static unsigned char g_winbuf[640*420*4];

int eigen_win_create(int x, int y, int w, int h, const char* title) {
    (void)x;(void)y;(void)w;(void)h;(void)title;
    return 1;
}
void* eigen_win_map(int id) { (void)id; return g_winbuf; }
void eigen_win_flush(int id) { (void)id; }
void eigen_win_close(int id) { (void)id; }
int  eigen_win_getsize(int id, uint32_t* w, uint32_t* h) {
    (void)id; *w = 640; *h = 420; return 0;
}
int  eigen_win_poll(int win, eigen_ev_t* evs, int max) {
    (void)win; (void)evs; (void)max;
    return 0;
}
void eigen_draw_fillrect(unsigned int* fb, int w, int h, int x, int y, int rw, int rh, unsigned int color) {
    for (int py=y; py<y+rh; py++)
        for (int px=x; px<x+rw; px++)
            fb[py*w+px] = color;
}
int eigen_load_module(const char* name, void* buf, uint64_t cap) {
    (void)name;(void)buf;(void)cap;
    return -1;
}
int eigen_spawn(const char* name) { (void)name; return 1; }
void eigen_sleep_ms(unsigned int ms) { (void)ms; }
unsigned int eigen_gettime_ms(void) { return 0; }
void* eigen_malloc(unsigned long sz) { return malloc(sz); }
void  eigen_free(void* p) { free(p); }
