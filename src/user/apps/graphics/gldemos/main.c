/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#include "userlib.h"
#include "userui.h"
#include "GL/gl.h"
#include "tinygl/zbuffer.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <math.h>

#define WIN_W 640
#define WIN_H 480
#define MAX_EVS 32
#define GL_PI 3.14159265358979323846f

static int win_id = -1;
static uint32_t* win_fb = NULL;
static uint32_t cur_w = WIN_W, cur_h = WIN_H;
static ZBuffer* zb = NULL;
static ui_t g_ui;

static float rot_x = 20.0f, rot_y = 30.0f;
static float spin = 0.0f;
static int shape_idx = 0; /* 0=Cube, 1=Sphere, 2=Torus, 3=Pyramid */
static int anim_paused = 0;

static void draw_cube(void) {
    glBegin(GL_QUADS);
    /* front */
    glNormal3f(0,0,1);
    glVertex3f(-1,-1, 1); glVertex3f( 1,-1, 1); glVertex3f( 1, 1, 1); glVertex3f(-1, 1, 1);
    /* back */
    glNormal3f(0,0,-1);
    glVertex3f(-1,-1,-1); glVertex3f(-1, 1,-1); glVertex3f( 1, 1,-1); glVertex3f( 1,-1,-1);
    /* top */
    glNormal3f(0,1,0);
    glVertex3f(-1, 1,-1); glVertex3f(-1, 1, 1); glVertex3f( 1, 1, 1); glVertex3f( 1, 1,-1);
    /* bottom */
    glNormal3f(0,-1,0);
    glVertex3f(-1,-1,-1); glVertex3f( 1,-1,-1); glVertex3f( 1,-1, 1); glVertex3f(-1,-1, 1);
    /* right */
    glNormal3f(1,0,0);
    glVertex3f( 1,-1,-1); glVertex3f( 1, 1,-1); glVertex3f( 1, 1, 1); glVertex3f( 1,-1, 1);
    /* left */
    glNormal3f(-1,0,0);
    glVertex3f(-1,-1,-1); glVertex3f(-1,-1, 1); glVertex3f(-1, 1, 1); glVertex3f(-1, 1,-1);
    glEnd();
}

static void draw_pyramid(void) {
    glBegin(GL_TRIANGLES);
    /* front */
    glNormal3f(0, 0.5f, 0.8f);
    glVertex3f( 0, 1.2f, 0); glVertex3f(-1,-1, 1); glVertex3f( 1,-1, 1);
    /* right */
    glNormal3f(0.8f, 0.5f, 0);
    glVertex3f( 0, 1.2f, 0); glVertex3f( 1,-1, 1); glVertex3f( 1,-1,-1);
    /* back */
    glNormal3f(0, 0.5f,-0.8f);
    glVertex3f( 0, 1.2f, 0); glVertex3f( 1,-1,-1); glVertex3f(-1,-1,-1);
    /* left */
    glNormal3f(-0.8f, 0.5f, 0);
    glVertex3f( 0, 1.2f, 0); glVertex3f(-1,-1,-1); glVertex3f(-1,-1, 1);
    glEnd();
}

static void draw_torus(float r, float R, int nsides, int rings) {
    for (int i = 0; i < rings; i++) {
        float u0 = (float)i * 2.0f * GL_PI / rings;
        float u1 = (float)(i + 1) * 2.0f * GL_PI / rings;
        glBegin(GL_QUAD_STRIP);
        for (int j = 0; j <= nsides; j++) {
            float v = (float)j * 2.0f * GL_PI / nsides;
            float nx0 = cosf(u0) * cosf(v), ny0 = sinf(u0) * cosf(v), nz0 = sinf(v);
            float nx1 = cosf(u1) * cosf(v), ny1 = sinf(u1) * cosf(v), nz1 = sinf(v);
            glNormal3f(nx0, ny0, nz0);
            glVertex3f((R + r * cosf(v)) * cosf(u0), (R + r * cosf(v)) * sinf(u0), r * sinf(v));
            glNormal3f(nx1, ny1, nz1);
            glVertex3f((R + r * cosf(v)) * cosf(u1), (R + r * cosf(v)) * sinf(u1), r * sinf(v));
        }
        glEnd();
    }
}

static void draw_sphere(float radius, int lats, int longs) {
    for (int i = 0; i <= lats; i++) {
        float lat0 = GL_PI * (-0.5f + (float)(i - 1) / lats);
        float z0 = radius * sinf(lat0);
        float zr0 = radius * cosf(lat0);

        float lat1 = GL_PI * (-0.5f + (float)i / lats);
        float z1 = radius * sinf(lat1);
        float zr1 = radius * cosf(lat1);

        glBegin(GL_QUAD_STRIP);
        for (int j = 0; j <= longs; j++) {
            float lng = 2.0f * GL_PI * (float)(j - 1) / longs;
            float x = cosf(lng), y = sinf(lng);
            glNormal3f(x * zr0, y * zr0, z0);
            glVertex3f(x * zr0, y * zr0, z0);
            glNormal3f(x * zr1, y * zr1, z1);
            glVertex3f(x * zr1, y * zr1, z1);
        }
        glEnd();
    }
}

static void init_scene(int w, int h) {
    if (zb) ZB_close(zb);
    zb = ZB_open(w, h, ZB_MODE_RGBA, 0, 0, 0, 0);
    glInit(zb);

    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    float aspect = (float)w / (float)h;
    glFrustum(-aspect, aspect, -1.0, 1.0, 3.0, 30.0);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    static GLfloat pos[4] = { 4.0, 5.0, 8.0, 0.0 };
    static GLfloat amb[4] = { 0.2, 0.2, 0.2, 1.0 };
    static GLfloat diff[4] = { 0.2, 0.6, 0.9, 1.0 };
    static GLfloat spec[4] = { 0.9, 0.9, 1.0, 1.0 };

    glLightfv(GL_LIGHT0, GL_POSITION, pos);
    glLightfv(GL_LIGHT0, GL_AMBIENT, amb);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, diff);
    glLightfv(GL_LIGHT0, GL_SPECULAR, spec);

    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_AUTO_NORMAL);
    glEnable(GL_NORMALIZE);
}

static void render_shape(void) {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glPushMatrix();
    glTranslatef(0.0, 0.0, -6.0);
    glRotatef(rot_x, 1.0, 0.0, 0.0);
    glRotatef(rot_y + spin, 0.0, 1.0, 0.0);

    switch (shape_idx) {
    case 0: draw_cube(); break;
    case 1: draw_sphere(1.5f, 16, 24); break;
    case 2: draw_torus(0.5f, 1.2f, 16, 24); break;
    default: draw_pyramid(); break;
    }

    glPopMatrix();
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    win_id = eigen_win_create(70, 50, WIN_W, WIN_H, "GL 3D Demos");
    if (win_id < 0) return 1;

    win_fb = (uint32_t*)eigen_win_map(win_id);
    eigen_win_getsize(win_id, &cur_w, &cur_h);

    init_scene(cur_w, cur_h);

    eigen_ev_t evs[MAX_EVS];
    int running = 1;
    int dragging = 0, last_mx = 0, last_my = 0;

    const char* shapes[] = { "Cube", "Sphere", "Torus", "Pyramid" };

    while (running) {
        int n = eigen_win_poll(win_id, evs, MAX_EVS);
        win_fb = (uint32_t*)eigen_win_map(win_id);
        eigen_win_getsize(win_id, &cur_w, &cur_h);

        ui_begin(&g_ui, win_fb, (int)cur_w, (int)cur_h);
        ui_feed(&g_ui, evs, n);

        for (int i = 0; i < n; i++) {
            eigen_ev_t* ev = &evs[i];
            if (ev->type == EIGEN_EV_CLOSE) { running = 0; break; }
            if (ev->type == EIGEN_EV_MDOWN) {
                dragging = 1; last_mx = ev->a; last_my = ev->b;
            }
            if (ev->type == EIGEN_EV_MUP) { dragging = 0; }
            if (ev->type == EIGEN_EV_MMOVE && dragging) {
                rot_y += (float)(ev->a - last_mx);
                rot_x += (float)(ev->b - last_my);
                last_mx = ev->a; last_my = ev->b;
            }
            if (ev->type == EIGEN_EV_KEY) {
                if (ev->a >= 0x100 || (ev->a & 0x100)) continue;
                char k = (char)ev->a;
                int code = ev->b;
                if (code == 0x4B) rot_y -= 5.0f;
                else if (code == 0x4D) rot_y += 5.0f;
                else if (code == 0x48) rot_x -= 5.0f;
                else if (code == 0x50) rot_x += 5.0f;
                else if (k == ' ') anim_paused = !anim_paused;
                else if (k == '\t' || k == 'n' || k == 'N') shape_idx = (shape_idx + 1) % 4;
            }
        }

        if (!anim_paused) spin += 1.5f;

        render_shape();

        if (zb && zb->pbuf && win_fb) {
            uint32_t* src = (uint32_t*)zb->pbuf;
            memcpy(win_fb, src, cur_w * cur_h * 4);
        }

        /* Top HUD */
        eigen_draw_fillrect(win_fb, cur_w, cur_h, 0, 0, cur_w, 32, 0x161B22);
        eigen_draw_fillrect(win_fb, cur_w, cur_h, 0, 31, cur_w, 1, 0x30363D);

        char title[64];
        snprintf(title, sizeof(title), "GL 3D DEMO — %s", shapes[shape_idx]);
        eigen_draw_text(win_fb, cur_w, cur_h, 12, 8, title, 0x58A6FF);
        eigen_draw_text(win_fb, cur_w, cur_h, cur_w - 260, 8, "Tab/N:shape | Space:pause | Drag:rot", 0x8B949E);

        ui_end(&g_ui);
        eigen_win_flush(win_id);
        eigen_sleep_ms(20);
    }

    if (zb) ZB_close(zb);
    eigen_win_close(win_id);
    return 0;
}
