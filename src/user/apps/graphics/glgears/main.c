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
#define GLG_PI 3.14159265358979323846f

static int win_id = -1;
static uint32_t* win_fb = NULL;
static uint32_t cur_w = WIN_W, cur_h = WIN_H;
static ZBuffer* zb = NULL;
static ui_t g_ui;

static float view_rotx = 20.0f, view_roty = 30.0f, view_rotz = 0.0f;
static float gear_spin = 0.0f;
static int anim_paused = 0;
static GLuint gear1, gear2, gear3;

static void gear(GLfloat inner_radius, GLfloat outer_radius,
                 GLfloat width, GLint teeth, GLfloat tooth_depth) {
    GLint i;
    GLfloat r0, r1, r2;
    GLfloat angle, da;
    GLfloat u, v;

    r0 = inner_radius;
    r1 = outer_radius - tooth_depth / 2.0f;
    r2 = outer_radius + tooth_depth / 2.0f;
    da = 2.0f * GLG_PI / teeth / 4.0f;

    glShadeModel(GL_FLAT);
    glNormal3f(0.0f, 0.0f, 1.0f);

    /* front face */
    glBegin(GL_QUAD_STRIP);
    for (i = 0; i <= teeth; i++) {
        angle = i * 2.0f * GLG_PI / teeth;
        glVertex3f(r0 * cosf(angle), r0 * sinf(angle), width * 0.5f);
        glVertex3f(r1 * cosf(angle), r1 * sinf(angle), width * 0.5f);
        if (i < teeth) {
            glVertex3f(r0 * cosf(angle), r0 * sinf(angle), width * 0.5f);
            glVertex3f(r1 * cosf(angle + 3 * da), r1 * sinf(angle + 3 * da), width * 0.5f);
        }
    }
    glEnd();

    /* front sides of teeth */
    glBegin(GL_QUADS);
    for (i = 0; i < teeth; i++) {
        angle = i * 2.0f * GLG_PI / teeth;
        glVertex3f(r1 * cosf(angle), r1 * sinf(angle), width * 0.5f);
        glVertex3f(r2 * cosf(angle + da), r2 * sinf(angle + da), width * 0.5f);
        glVertex3f(r2 * cosf(angle + 2 * da), r2 * sinf(angle + 2 * da), width * 0.5f);
        glVertex3f(r1 * cosf(angle + 3 * da), r1 * sinf(angle + 3 * da), width * 0.5f);
    }
    glEnd();

    /* back face */
    glNormal3f(0.0f, 0.0f, -1.0f);
    glBegin(GL_QUAD_STRIP);
    for (i = 0; i <= teeth; i++) {
        angle = i * 2.0f * GLG_PI / teeth;
        glVertex3f(r1 * cosf(angle), r1 * sinf(angle), -width * 0.5f);
        glVertex3f(r0 * cosf(angle), r0 * sinf(angle), -width * 0.5f);
        if (i < teeth) {
            glVertex3f(r1 * cosf(angle + 3 * da), r1 * sinf(angle + 3 * da), -width * 0.5f);
            glVertex3f(r0 * cosf(angle), r0 * sinf(angle), -width * 0.5f);
        }
    }
    glEnd();

    /* back sides of teeth */
    glBegin(GL_QUADS);
    for (i = 0; i < teeth; i++) {
        angle = i * 2.0f * GLG_PI / teeth;
        glVertex3f(r1 * cosf(angle + 3 * da), r1 * sinf(angle + 3 * da), -width * 0.5f);
        glVertex3f(r2 * cosf(angle + 2 * da), r2 * sinf(angle + 2 * da), -width * 0.5f);
        glVertex3f(r2 * cosf(angle + da), r2 * sinf(angle + da), -width * 0.5f);
        glVertex3f(r1 * cosf(angle), r1 * sinf(angle), -width * 0.5f);
    }
    glEnd();

    /* outward faces of teeth */
    glBegin(GL_QUAD_STRIP);
    for (i = 0; i < teeth; i++) {
        angle = i * 2.0f * GLG_PI / teeth;
        glVertex3f(r1 * cosf(angle), r1 * sinf(angle), width * 0.5f);
        glVertex3f(r1 * cosf(angle), r1 * sinf(angle), -width * 0.5f);
        u = r2 * cosf(angle + da) - r1 * cosf(angle);
        v = r2 * sinf(angle + da) - r1 * sinf(angle);
        glNormal3f(v, -u, 0.0f);
        glVertex3f(r2 * cosf(angle + da), r2 * sinf(angle + da), width * 0.5f);
        glVertex3f(r2 * cosf(angle + da), r2 * sinf(angle + da), -width * 0.5f);
        glNormal3f(cosf(angle), sinf(angle), 0.0f);
        glVertex3f(r2 * cosf(angle + 2 * da), r2 * sinf(angle + 2 * da), width * 0.5f);
        glVertex3f(r2 * cosf(angle + 2 * da), r2 * sinf(angle + 2 * da), -width * 0.5f);
        u = r1 * cosf(angle + 3 * da) - r2 * cosf(angle + 2 * da);
        v = r1 * sinf(angle + 3 * da) - r2 * sinf(angle + 2 * da);
        glNormal3f(v, -u, 0.0f);
        glVertex3f(r1 * cosf(angle + 3 * da), r1 * sinf(angle + 3 * da), width * 0.5f);
        glVertex3f(r1 * cosf(angle + 3 * da), r1 * sinf(angle + 3 * da), -width * 0.5f);
        glNormal3f(cosf(angle), sinf(angle), 0.0f);
    }
    glEnd();

    /* inside radius cylinder */
    glBegin(GL_QUAD_STRIP);
    for (i = 0; i <= teeth; i++) {
        angle = i * 2.0f * GLG_PI / teeth;
        glNormal3f(-cosf(angle), -sinf(angle), 0.0f);
        glVertex3f(r0 * cosf(angle), r0 * sinf(angle), -width * 0.5f);
        glVertex3f(r0 * cosf(angle), r0 * sinf(angle), width * 0.5f);
    }
    glEnd();
}

static void init_scene(int w, int h) {
    if (zb) ZB_close(zb);
    zb = ZB_open(w, h, ZB_MODE_RGBA, 0, 0, 0, 0);
    glInit(zb);

    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    float aspect = (float)w / (float)h;
    glFrustum(-aspect, aspect, -1.0, 1.0, 5.0, 60.0);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0, 0.0, -40.0);

    static GLfloat pos[4] = { 5.0, 5.0, 10.0, 0.0 };
    static GLfloat red[4] = { 0.8, 0.1, 0.0, 1.0 };
    static GLfloat green[4] = { 0.0, 0.8, 0.2, 1.0 };
    static GLfloat blue[4] = { 0.2, 0.2, 1.0, 1.0 };

    glLightfv(GL_LIGHT0, GL_POSITION, pos);
    glEnable(GL_CULL_FACE);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_DEPTH_TEST);

    gear1 = glGenLists(1);
    glNewList(gear1, GL_COMPILE);
    glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, red);
    gear(1.0, 4.0, 1.0, 20, 0.7);
    glEndList();

    gear2 = glGenLists(1);
    glNewList(gear2, GL_COMPILE);
    glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, green);
    gear(0.5, 2.0, 2.0, 10, 0.7);
    glEndList();

    gear3 = glGenLists(1);
    glNewList(gear3, GL_COMPILE);
    glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, blue);
    gear(1.3, 2.0, 0.5, 10, 0.7);
    glEndList();

    glEnable(GL_NORMALIZE);
}

static void draw_gears(void) {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glPushMatrix();
    glRotatef(view_rotx, 1.0, 0.0, 0.0);
    glRotatef(view_roty, 0.0, 1.0, 0.0);
    glRotatef(view_rotz, 0.0, 0.0, 1.0);

    glPushMatrix();
    glTranslatef(-3.0, -2.0, 0.0);
    glRotatef(gear_spin, 0.0, 0.0, 1.0);
    glCallList(gear1);
    glPopMatrix();

    glPushMatrix();
    glTranslatef(3.1, -2.0, 0.0);
    glRotatef(-2.0 * gear_spin - 9.0, 0.0, 0.0, 1.0);
    glCallList(gear2);
    glPopMatrix();

    glPushMatrix();
    glTranslatef(-3.1, 4.2, 0.0);
    glRotatef(-2.0 * gear_spin - 25.0, 0.0, 0.0, 1.0);
    glCallList(gear3);
    glPopMatrix();

    glPopMatrix();
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    win_id = eigen_win_create(70, 50, WIN_W, WIN_H, "GL Gears 3D");
    if (win_id < 0) return 1;

    win_fb = (uint32_t*)eigen_win_map(win_id);
    eigen_win_getsize(win_id, &cur_w, &cur_h);

    init_scene(cur_w, cur_h);

    eigen_ev_t evs[MAX_EVS];
    int running = 1;
    int dragging = 0, last_mx = 0, last_my = 0;

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
                view_roty += (float)(ev->a - last_mx);
                view_rotx += (float)(ev->b - last_my);
                last_mx = ev->a; last_my = ev->b;
            }
            if (ev->type == EIGEN_EV_KEY) {
                if (ev->a >= 0x100 || (ev->a & 0x100)) continue;
                char k = (char)ev->a;
                int code = ev->b;
                if (code == 0x4B) view_roty -= 5.0f;
                else if (code == 0x4D) view_roty += 5.0f;
                else if (code == 0x48) view_rotx -= 5.0f;
                else if (code == 0x50) view_rotx += 5.0f;
                else if (k == ' ') anim_paused = !anim_paused;
                else if (k == 'r' || k == 'R') { view_rotx = 20.0f; view_roty = 30.0f; }
            }
        }

        if (!anim_paused) gear_spin += 2.0f;

        draw_gears();

        /* Copy TinyGL frame buffer into window frame buffer */
        if (zb && zb->pbuf && win_fb) {
            uint32_t* src = (uint32_t*)zb->pbuf;
            memcpy(win_fb, src, cur_w * cur_h * 4);
        }

        /* Top HUD */
        eigen_draw_fillrect(win_fb, cur_w, cur_h, 0, 0, cur_w, 32, 0x161B22);
        eigen_draw_fillrect(win_fb, cur_w, cur_h, 0, 31, cur_w, 1, 0x30363D);
        eigen_draw_text(win_fb, cur_w, cur_h, 12, 8, "GL GEARS 3D — TinyGL Software Rasterizer", 0x58A6FF);
        eigen_draw_text(win_fb, cur_w, cur_h, cur_w - 240, 8, "Drag/Arrows:rotate | Space:pause", 0x8B949E);

        ui_end(&g_ui);
        eigen_win_flush(win_id);
        eigen_sleep_ms(20);
    }

    if (zb) ZB_close(zb);
    eigen_win_close(win_id);
    return 0;
}
