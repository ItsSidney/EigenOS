#!/bin/bash
# ============================================================
#  Eigen OS — new_folder_app.sh : ring-3 folder-app scaffold
#
#  Usage:  ./tools/new_folder_app.sh "My Game" [category]
#
#  Categories: 1=Productivity 2=System 3=Games 4=Graphics
#              5=Debug 6=Accessibility 7=Customization 8=Networking
#
#  Creates a folder-form app under src/user/apps/<category>/<slug>/
#  with a build.conf + main.c + ui.c/ui.h, matching the convention in
#  src/user/apps/README.md. build.sh discovers it automatically — no
#  edits to build.sh or gui.c are required. Rebuild and the ELF is
#  shipped as a Limine module (spawn "<slug>" / add a start-menu entry).
# ============================================================
set -e

NAME="$1"
CAT="${2:-3}"   # default: Games

if [ -z "$NAME" ]; then
    echo "usage: $0 \"App Name\" [category]"
    exit 1
fi

# ELF-safe slug: lowercase, spaces/dashes -> underscores
SLUG=$(echo "$NAME" | tr '[:upper:]' '[:lower:]' | tr ' -' '__' | tr -cd 'a-z0-9_')
if [ -z "$SLUG" ]; then
    echo "bad name: '$NAME'"
    exit 1
fi

if ! [[ "$CAT" =~ ^[1-8]$ ]]; then
    echo "category must be 1..8"
    exit 1
fi

case $CAT in
    1) CATDIR="productivity" ;;
    2) CATDIR="system" ;;
    3) CATDIR="games" ;;
    4) CATDIR="graphics" ;;
    5) CATDIR="debug" ;;
    6) CATDIR="accessibility" ;;
    7) CATDIR="customization" ;;
    8) CATDIR="networking" ;;
esac

APP_DIR="src/user/apps/$CATDIR/$SLUG"
if [ -d "$APP_DIR" ]; then
    echo "error: $APP_DIR already exists"
    exit 1
fi
mkdir -p "$APP_DIR"

echo "[new_folder_app] $NAME -> $APP_DIR/ (slug '$SLUG')"

cat > "$APP_DIR/build.conf" <<EOF
# build.conf — folder-app descriptor (consumed by build.sh::build_folders())
NAME=$SLUG
SRCS=main.c ui.c
INCS=.
FLAGS=-O2
EOF

cat > "$APP_DIR/ui.h" <<EOF
/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

/* ui.h — drawing helpers for the $SLUG app. */
#ifndef ${SLUG^^}_UI_H
#define ${SLUG^^}_UI_H

#include "userui.h"

void ${SLUG}_draw_card(ui_t* u, int x, int y, int w, int h, const char* title);

#endif /* ${SLUG^^}_UI_H */
EOF

cat > "$APP_DIR/ui.c" <<EOF
/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

/* ui.c — drawing helpers for the $SLUG folder app. */

#include "ui.h"

void ${SLUG}_draw_card(ui_t* u, int x, int y, int w, int h, const char* title) {
    ui_vgrad(u->buf, u->W, u->H, x, y, w, h, ui_theme.panel2, ui_theme.panel);
    ui_draw_round(u->buf, u->W, u->H, x, y, w, h, 8, ui_theme.border);
    eigen_draw_fillrect(u->buf, u->W, u->H, x + 1, y + 1, w - 2, 1, ui_theme.accent);
    ui_glow(u->buf, u->W, u->H, x + 8, y + 2, 16, ui_theme.accent2, 36);
    if (title) {
        int tw = 0; while (title[tw]) tw++;
        eigen_draw_text(u->buf, u->W, u->H, x + 12, y + (24 - 16) / 2, title, ui_theme.text);
    }
}
EOF

cat > "$APP_DIR/main.c" <<EOF
/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

/* $SLUG main.c — ring-3 folder app (API81 userland).
 *
 * Runs at ring 3: built by build.sh from this folder's build.conf, shipped
 * as a Limine module and launched via spawn "$SLUG" (or a Start-menu entry).
 * Everything goes through int \$0x80 syscalls — no kernel memory, no kernel
 * calls. Crash it and only this app dies. */

#include "userlib.h"
#include "userui.h"
#include <user/eigen.h>
#include "ui.h"

#define WIN_W 420
#define WIN_H 320

int main(void) {
    eigen_printf("[$SLUG] starting\n");

    int id = eigen_win_create(140, 90, WIN_W, WIN_H, "$NAME");
    if (id < 0) { eigen_printf("[$SLUG] window failed\n"); return 1; }

    ui_sync_theme();                       /* match the live shell palette */

    uint32_t* buf = (uint32_t*)eigen_win_map(id);
    if (!buf) { eigen_win_close(id); return 1; }

    int W = WIN_W, H = WIN_H;
    eigen_win_getsize(id, (uint32_t*)&W, (uint32_t*)&H);

    int clicks = 0;
    ui_t ui = { 0 };

    for (;;) {
        eigen_ev_t evs[16];
        int got = eigen_win_poll(id, evs, 16);

        /* Re-query size + re-map every frame: the WM may reallocate the
           buffer on resize/maximize, so the old pointer would be stale. */
        int nw = WIN_W, nh = WIN_H;
        eigen_win_getsize(id, (uint32_t*)&nw, (uint32_t*)&nh);
        W = nw; H = nh;
        buf = (uint32_t*)eigen_win_map(id);
        if (!buf) continue;

        ui_begin(&ui, buf, W, H);
        ui_feed(&ui, evs, got);

        int quit = 0, close_req = 0;
        for (int i = 0; i < got; i++) {
            if (evs[i].type == EIGEN_EV_CLOSE) close_req = 1;
            else if (evs[i].type == EIGEN_EV_KEY && evs[i].a == 27 && !ui.focus) quit = 1;
        }
        if (quit || close_req) break;

        ui_vgrad(buf, W, H, 0, 0, W, H, ui_theme.bg, 0x0B1020);
        ui_glow(buf, W, H, W / 2, H, W / 2, ui_theme.accent, 28);
        ui_header(&ui, "$NAME", "ring 3");

        ${SLUG}_draw_card(&ui, 14, 48, W - 28, 90, "Section");
        if (ui_button(&ui, 26, 80, 120, 28, "Click me")) clicks++;
        {
            char out[32]; int oi = 0;
            int n = clicks; if (n == 0) out[oi++] = '0';
            else while (n) { out[oi++] = '0' + (n % 10); n /= 10; }
            for (int a = 0, b = oi - 1; a < b; a++, b--) { char t = out[a]; out[a] = out[b]; out[b] = t; }
            out[oi++] = ' '; out[oi] = 0;
            ui_label(&ui, 158, 88, "clicks", ui_theme.dim);
            ui_label(&ui, 200, 88, out, ui_theme.gold);
        }

        ui_end(&ui);
        eigen_win_flush(id);
        eigen_sleep_ms(16);
    }

    eigen_win_close(id);
    eigen_printf("[$SLUG] bye\n");
    return 0;
}
EOF

echo "[new_folder_app] done:"
echo "   edit   $APP_DIR/"
echo "   rebuild:  ./build.sh build"
echo "   launch:  spawn $SLUG   (from the shell) or add a menu entry in src/gui/gui.c"
