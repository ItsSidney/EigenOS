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
#include "teapot_data.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <math.h>

#define WIN_W 640
#define WIN_H 480
#define MAX_EVS 32
#define PATCH_STEPS 4

static int win_id = -1;
static uint32_t* win_fb = NULL;
static uint32_t cur_w = WIN_W, cur_h = WIN_H;
static ZBuffer* zb = NULL;
static ui_t g_ui;

static float rot_x = 25.0f, rot_y = 45.0f;
static float spin = 0.0f;
static int anim_paused = 0;
static int wireframe = 0;
static GLuint teapot_list = 0;

static void bernstein(float t, float* b) {
    float inv = 1.0f - t;
    b[0] = inv * inv * inv;
    b[1] = 3.0f * t * inv * inv;
    b[2] = 3.0f * t * t * inv;
    b[3] = t * t * t;
}

static void eval_patch(int p_idx, float u, float v, float* pt) {
    float bu[4], bv[4];
    bernstein(u, bu);
    bernstein(v, bv);

    pt[0] = 0.0f; pt[1] = 0.0f; pt[2] = 0.0f;

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            int v_idx = teapot_patches[p_idx][i][j];
            float weight = bu[i] * bv[j];
            pt[0] += weight * teapot_verts[v_idx][0];
            pt[1] += weight * teapot_verts[v_idx][1];
            pt[2] += weight * teapot_verts[v_idx][2];
        }
    }
}

static void build_teapot(void) {
    teapot_list = glGenLists(1);
    glNewList(teapot_list, GL_COMPILE);

    for (int p = 0; p < TEAPOT_NUM_PATCHES; p++) {
        for (int iu = 0; iu < PATCH_STEPS; iu++) {
            float u0 = (float)iu / PATCH_STEPS;
            float u1 = (float)(iu + 1) / PATCH_STEPS;

            glBegin(GL_QUAD_STRIP);
            for (int iv = 0; iv <= PATCH_STEPS; iv++) {
                float v = (float)iv / PATCH_STEPS;
                float p0[3], p1[3];
                eval_patch(p, u0, v, p0);
                eval_patch(p, u1, v, p1);

                /* Approximate normal */
                float du[3], dv[3];
                eval_patch(p, u0 + 0.01f, v, du);
                eval_patch(p, u0, v + 0.01f, dv);
                float nx = (du[1] - p0[1]) * (dv[2] - p0[2]) - (du[2] - p0[2]) * (dv[1] - p0[1]);
                float ny = (du[2] - p0[2]) * (dv[0] - p0[0]) - (du[0] - p0[0]) * (dv[2] - p0[2]);
                float nz = (du[0] - p0[0]) * (dv[1] - p0[1]) - (du[1] - p0[1]) * (dv[0] - p0[0]);
                float len = sqrtf(nx * nx + ny * ny + nz * nz);
                if (len > 0.0001f) { nx /= len; ny /= len; nz /= len; }

                glNormal3f(nx, ny, nz);
                glVertex3fv(p0);
                glVertex3fv(p1);
            }
            glEnd();
        }
    }

    glEndList();
}

static void init_scene(int w, int h) {
    if (zb) ZB_close(zb);
    zb = ZB_open(w, h, ZB_MODE_RGBA, 0, 0, 0, 0);
    glInit(zb);

    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    float aspect = (float)w / (float)h;
    glFrustum(-aspect * 1.5f, aspect * 1.5f, -1.5, 1.5, 3.0, 30.0);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    static GLfloat pos[4] = { 4.0, 6.0, 5.0, 0.0 };
    static GLfloat amb[4] = { 0.2, 0.2, 0.2, 1.0 };
    static GLfloat diff[4] = { 0.9, 0.5, 0.2, 1.0 };
    static GLfloat spec[4] = { 1.0, 0.9, 0.8, 1.0 };

    glLightfv(GL_LIGHT0, GL_POSITION, pos);
    glLightfv(GL_LIGHT0, GL_AMBIENT, amb);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, diff);
    glLightfv(GL_LIGHT0, GL_SPECULAR, spec);

    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_AUTO_NORMAL);
    glEnable(GL_NORMALIZE);

    build_teapot();
}

static void draw_scene(void) {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glPushMatrix();
    glTranslatef(0.0, -1.0, -8.0);
    glRotatef(rot_x, 1.0, 0.0, 0.0);
    glRotatef(rot_y + spin, 0.0, 1.0, 0.0);

    if (wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    else glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    if (teapot_list) glCallList(teapot_list);

    glPopMatrix();
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    win_id = eigen_win_create(70, 50, WIN_W, WIN_H, "GL Teapot 3D");
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
                else if (k == 'w' || k == 'W') wireframe = !wireframe;
                else if (k == 'r' || k == 'R') { rot_x = 25.0f; rot_y = 45.0f; }
            }
        }

        if (!anim_paused) spin += 1.5f;

        draw_scene();

        if (zb && zb->pbuf && win_fb) {
            uint32_t* src = (uint32_t*)zb->pbuf;
            memcpy(win_fb, src, cur_w * cur_h * 4);
        }

        /* Top HUD */
        eigen_draw_fillrect(win_fb, cur_w, cur_h, 0, 0, cur_w, 32, 0x161B22);
        eigen_draw_fillrect(win_fb, cur_w, cur_h, 0, 31, cur_w, 1, 0x30363D);
        eigen_draw_text(win_fb, cur_w, cur_h, 12, 8, "GL UTAH TEAPOT — Bicubic Bezier Patches", 0x58A6FF);
        eigen_draw_text(win_fb, cur_w, cur_h, cur_w - 240, 8, "W:wireframe | Space:pause | Drag:rot", 0x8B949E);

        ui_end(&g_ui);
        eigen_win_flush(win_id);
        eigen_sleep_ms(20);
    }

    if (zb) ZB_close(zb);
    eigen_win_close(win_id);
    return 0;
}
