#!/bin/bash
# ============================================================
#  Eigen OS — new_app.sh : ring-3 app scaffold
#
#  Usage:  ./tools/new_app.sh "My Game" [category]
#
#  Categories: 1=Productivity 2=System 3=Games 4=Graphics
#              5=Debug 6=Accessibility 7=Customization 8=Networking
#
#  What it does:
#   1. Writes src/user/apps/<category>/<name>.c — a complete, runnable
#      ring-3 app (window loop, events, drawing helpers) in the house
#      style. The app's ELF name is the FILE base name, so it shows up
#      wherever it lives.
#   2. Registers the ELF in config/limine.conf so the kernel ships it.
#   3. Adds a start-menu entry in src/gui/gui.c (launches the ELF).
#
#  That's it: rebuild and your app appears in the menu, running
#  fully at ring 3.
# ============================================================
set -e

NAME="$1"
CAT="${2:-3}"   # default: Games

if [ -z "$NAME" ]; then
    echo "usage: $0 \"App Name\" [category]"
    exit 1
fi

# ELF-safe name: lowercase, spaces/dashes -> underscores
SLUG=$(echo "$NAME" | tr '[:upper:]' '[:lower:]' | tr ' -' '__' | tr -cd 'a-z0-9_')
if [ -z "$SLUG" ]; then
    echo "bad name: '$NAME'"
    exit 1
fi

if ! [[ "$CAT" =~ ^[1-8]$ ]]; then
    echo "category must be 1..8"
    exit 1
fi

# category number -> folder under src/user/apps/
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
mkdir -p "src/user/apps/$CATDIR"

APP_FILE="src/user/apps/$CATDIR/$SLUG.c"
if [ -f "$APP_FILE" ]; then
    echo "error: $APP_FILE already exists"
    exit 1
fi

# Highest global_idx currently in use (dock mask is a 64-bit bitmask).
MAXIDX=$(awk -F',' '/^    \{/ { gsub(/[^0-9]/, "", $4); if ($4+0 > m) m = $4+0 } END { print m }' src/gui/gui.c)
IDX=$((MAXIDX + 1))

echo "[new_app] $NAME -> $APP_FILE (menu idx $IDX, cat $CAT)"

cat > "$APP_FILE" <<EOF
/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

/* $SLUG.c — ring-3 $NAME app (API81 userland).
 *
 * Runs at ring 3: this file is a userspace ELF (src/user/apps/*.c are
 * auto-compiled by build.sh, shipped as a Limine module and launched
 * from the start menu). Everything goes through int \$0x80 syscalls —
 * no kernel memory, no kernel calls. Crash it and only this app dies.
 *
 * The pattern is always the same:
 *   1. eigen_win_create()  -> a window
 *   2. eigen_win_map()     -> its pixel buffer (content_w x content_h u32)
 *   3. ui_begin + ui_feed  -> wire up the modern UI toolkit
 *   4. draw widgets (ui_button, ui_text_input, ...) and raw pixels
 *   5. eigen_win_flush()   -> present it
 *   6. eigen_sleep_ms(16)  -> ~60 fps, yields the CPU
 */
#include "userlib.h"
#include "userui.h"
#include <user/eigen.h>

#define WIN_W 360
#define WIN_H 240

int main(void) {
    eigen_printf("[$SLUG] starting\n");

    int id = eigen_win_create(120, 80, WIN_W, WIN_H, "$NAME");
    if (id < 0) {
        eigen_printf("[$SLUG] window failed\n");
        return 1;
    }
    uint32_t* buf = (uint32_t*)eigen_win_map(id);
    if (!buf) {
        eigen_win_close(id);
        return 1;
    }

    int W = WIN_W, H = WIN_H;
    eigen_win_getsize(id, (uint32_t*)&W, (uint32_t*)&H);

    char name[32] = "";        /* text input buffer  */
    int  vol = 70;             /* slider value      */
    int  fx  = 1;              /* toggle value      */
    int  clicks = 0;

    ui_t ui = { 0 };
    int quit = 0;
    while (!quit) {
        /* events FIRST, then widgets can react while drawing */
        eigen_ev_t evs[8];
        int got = eigen_win_poll(id, evs, 8);
        ui_begin(&ui, buf, W, H);
        ui_feed(&ui, evs, got);
        for (int i = 0; i < got; i++) {
            if (evs[i].type == EIGEN_EV_KEY && evs[i].a == 27 && !ui.focus) quit = 1;
            else if (evs[i].type == EIGEN_EV_CLOSE) quit = 1;
        }

        /* ---- draw: modern UI in a handful of lines ---- */
        ui_vgrad(buf, W, H, 0, 0, W, H, 0x0D1430, 0x0B1020);
        ui_glow(buf, W, H, W / 2, H, W / 2, 0x1D4ED8, 40);   /* floor glow */
        ui_header(&ui, "$NAME", "ring 3");

        if (ui_button(&ui, 14, 44, 110, 26, "Click me")) clicks++;

        char out[48]; int oi = 0;
        int n = clicks; char d[12]; int di = 0;
        do { d[di++] = '0' + (n % 10); n /= 10; } while (n);
        while (di) out[oi++] = d[--di];
        out[oi++] = ' '; out[oi] = 0;
        ui_label(&ui, 14, 84, "clicks", ui_theme.dim);
        ui_label(&ui, 62, 84, out, ui_theme.gold);

        ui_text_input(&ui, 1, 14, 110, 200, 24, name, 32);   /* Enter = done */
        ui_toggle(&ui, 14, 150, &fx);
        ui_label(&ui, 66, 154, fx ? "fx on" : "fx off", ui_theme.text);
        ui_slider(&ui, 14, 180, 200, &vol, 0, 100);

        ui_end(&ui);
        eigen_win_flush(id);
        eigen_sleep_ms(16);
    }

    eigen_win_close(id);
    eigen_printf("[$SLUG] bye\n");
    return 0;
}
EOF

# --- register the ELF as a Limine module -----------------------------
if ! grep -q "module_path: boot():/user/$SLUG.elf" config/limine.conf; then
    sed -i "s|module_path: boot():/user/winpixel.elf|&\n    module_path: boot():/user/$SLUG.elf|" config/limine.conf
fi

# --- start-menu entry (launch_func 0 + user_elf name = ring-3 app) ----
if ! grep -q "\"$NAME\", 0, $CAT, $IDX, \"$SLUG\"" src/gui/gui.c; then
    sed -i "s|^    {0, 0, 0, 0}$|    {\"$NAME\", 0, $CAT, $IDX, \"$SLUG\"},\n&|" src/gui/gui.c
    sed -i "s|^    \"Periodic Table\", 0$|    \"$NAME\", \n    \"Periodic Table\", 0|" src/gui/gui.c
fi

echo "[new_app] done:"
echo "   edit   $APP_FILE"
echo "   rebuild:  ./build.sh build"
echo "   note: the start-menu icon falls back to the generic tile until"
echo "         draw_app_icon() in src/gui/app_icons.c knows the name."
echo "   note: when porting an existing kernel app, DELETE the old file"
echo "         under src/apps/ — the OS is moving to ring 3, one app at a time."