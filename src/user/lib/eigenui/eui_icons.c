/***************************************************************/
/*  EigenUI Icons — implementation                               */
/***************************************************************/
#include "eui_icons.h"

/* local float trig (no libm dependency) */
static float cosf_approx(float x) {
    while (x > 3.14159265f) x -= 6.2831853f;
    while (x < -3.14159265f) x += 6.2831853f;
    float xx = x * x;
    return 1.0f - xx / 2.0f + xx * xx / 24.0f - xx * xx * xx / 720.0f;
}
static float sinf_approx(float x) { return cosf_approx(1.5707963f - x); }

/* helper: filled circle (round rect with max radius == circle) */
static void disc(eui_canvas* c, int cx, int cy, int r, eui_color col) {
    eui_canvas_round(c, cx - r, cy - r, 2 * r, 2 * r, r, col);
}
static void ring(eui_canvas* c, int cx, int cy, int r, int t, eui_color col) {
    eui_canvas_round_stroke(c, cx - r, cy - r, 2 * r, 2 * r, r, t, col);
}

void eui_icon_draw(eui_canvas* c, eui_icon_id id, int x, int y, int size, eui_color col) {
    int s = size;
    if (s <= 0) return;
    int cx = x + s / 2, cy = y + s / 2;
    eui_canvas_push_clip(c, &EUI_RECT(x, y, s, s));

    switch (id) {
    case EUI_ICON_CLOSE:
        eui_canvas_line(c, x + s*0.30, y + s*0.30, x + s*0.70, y + s*0.70, col);
        eui_canvas_line(c, x + s*0.70, y + s*0.30, x + s*0.30, y + s*0.70, col);
        break;
    case EUI_ICON_MINIMIZE:
        eui_canvas_line(c, x + s*0.30, y + s*0.70, x + s*0.70, y + s*0.70, col);
        break;
    case EUI_ICON_MAXIMIZE:
        eui_canvas_round_stroke(c, x + s*0.22, y + s*0.22, s*0.56, s*0.56, 2, 1, col);
        break;
    case EUI_ICON_FILE:
        eui_canvas_round(c, x + s*0.24, y + s*0.20, s*0.40, s*0.60, 3, col);
        eui_canvas_line(c, x + s*0.44, y + s*0.20, x + s*0.64, y + s*0.40, col);
        eui_canvas_line(c, x + s*0.64, y + s*0.40, x + s*0.64, y + s*0.80, col);
        break;
    case EUI_ICON_FOLDER:
        eui_canvas_round(c, x + s*0.20, y + s*0.34, s*0.60, s*0.42, 3, col);
        eui_canvas_round(c, x + s*0.20, y + s*0.26, s*0.26, s*0.12, 2, col);
        break;
    case EUI_ICON_SETTINGS: {
        disc(c, cx, cy, s*0.12, col);
        for (int i = 0; i < 8; i++) {
            float a = i * 3.14159f / 4;
            int x1 = cx + (int)(cosf_approx(a) * s*0.22), y1 = cy + (int)(sinf_approx(a) * s*0.22);
            int x2 = cx + (int)(cosf_approx(a) * s*0.34), y2 = cy + (int)(sinf_approx(a) * s*0.34);
            eui_canvas_line(c, x1, y1, x2, y2, col);
        }
        break; }
    case EUI_ICON_SEARCH:
        ring(c, cx - s*0.04, cy - s*0.04, s*0.18, 2, col);
        eui_canvas_line(c, cx + s*0.10, cy + s*0.10, cx + s*0.30, cy + s*0.30, col);
        break;
    case EUI_ICON_PLUS:
        eui_canvas_line(c, cx, y + s*0.28, cx, y + s*0.72, col);
        eui_canvas_line(c, x + s*0.28, cy, x + s*0.72, cy, col);
        break;
    case EUI_ICON_MINUS:
        eui_canvas_line(c, x + s*0.28, cy, x + s*0.72, cy, col);
        break;
    case EUI_ICON_TRASH:
        eui_canvas_round(c, x + s*0.30, y + s*0.40, s*0.40, s*0.42, 2, col);
        eui_canvas_line(c, x + s*0.24, y + s*0.40, x + s*0.76, y + s*0.40, col);
        eui_canvas_line(c, x + s*0.40, y + s*0.32, x + s*0.60, y + s*0.32, col);
        break;
    case EUI_ICON_INFO:
        disc(c, cx, cy - s*0.18, s*0.05, col);
        eui_canvas_line(c, cx, cy - s*0.02, cx, cy + s*0.28, col);
        break;
    case EUI_ICON_WARNING:
        eui_canvas_line(c, cx, y + s*0.22, x + s*0.22, y + s*0.74, col);
        eui_canvas_line(c, cx, y + s*0.22, x + s*0.78, y + s*0.74, col);
        eui_canvas_line(c, x + s*0.22, y + s*0.74, x + s*0.78, y + s*0.74, col);
        eui_canvas_line(c, cx, cy + s*0.02, cx, cy + s*0.26, col);
        disc(c, cx, cy - s*0.06, s*0.04, col);
        break;
    case EUI_ICON_ERROR:
        disc(c, cx, cy, s*0.30, col);
        eui_canvas_line(c, cx - s*0.12, cy - s*0.12, cx + s*0.12, cy + s*0.12, 0xFFFFFF);
        eui_canvas_line(c, cx + s*0.12, cy - s*0.12, cx - s*0.12, cy + s*0.12, 0xFFFFFF);
        break;
    case EUI_ICON_CHECK:
        eui_canvas_line(c, x + s*0.24, cy, cx, y + s*0.70, col);
        eui_canvas_line(c, cx, y + s*0.70, x + s*0.76, y + s*0.28, col);
        break;
    case EUI_ICON_CHECKBOX:
        eui_canvas_round_stroke(c, x + s*0.24, y + s*0.24, s*0.52, s*0.52, 3, 2, col);
        break;
    case EUI_ICON_ARROW_LEFT:
        eui_canvas_line(c, x + s*0.66, cy, x + s*0.34, cy, col);
        eui_canvas_line(c, x + s*0.34, cy, x + s*0.50, y + s*0.34, col);
        eui_canvas_line(c, x + s*0.34, cy, x + s*0.50, y + s*0.66, col);
        break;
    case EUI_ICON_ARROW_RIGHT:
        eui_canvas_line(c, x + s*0.34, cy, x + s*0.66, cy, col);
        eui_canvas_line(c, x + s*0.66, cy, x + s*0.50, y + s*0.34, col);
        eui_canvas_line(c, x + s*0.66, cy, x + s*0.50, y + s*0.66, col);
        break;
    case EUI_ICON_ARROW_UP:
        eui_canvas_line(c, cx, y + s*0.66, cx, y + s*0.34, col);
        eui_canvas_line(c, cx, y + s*0.34, x + s*0.34, y + s*0.50, col);
        eui_canvas_line(c, cx, y + s*0.34, x + s*0.66, y + s*0.50, col);
        break;
    case EUI_ICON_ARROW_DOWN:
        eui_canvas_line(c, cx, y + s*0.34, cx, y + s*0.66, col);
        eui_canvas_line(c, cx, y + s*0.66, x + s*0.34, y + s*0.50, col);
        eui_canvas_line(c, cx, y + s*0.66, x + s*0.66, y + s*0.50, col);
        break;
    case EUI_ICON_APP:
        eui_canvas_round(c, x + s*0.20, y + s*0.20, s*0.60, s*0.60, s*0.14, col);
        disc(c, cx, cy, s*0.12, 0x000000);
        break;
    case EUI_ICON_POWER:
        ring(c, cx, cy - s*0.04, s*0.20, 2, col);
        eui_canvas_line(c, cx, cy - s*0.18, cx, cy - s*0.02, col);
        break;
    case EUI_ICON_WIFI:
        ring(c, cx, y + s*0.74, s*0.10, 2, col);
        ring(c, cx, y + s*0.74, s*0.22, 2, col);
        ring(c, cx, y + s*0.74, s*0.34, 2, col);
        disc(c, cx, y + s*0.66, s*0.05, col);
        break;
    case EUI_ICON_VOLUME:
        eui_canvas_line(c, x + s*0.22, cy, x + s*0.36, y + s*0.36, col);
        eui_canvas_line(c, x + s*0.22, cy, x + s*0.36, y + s*0.64, col);
        eui_canvas_line(c, x + s*0.36, y + s*0.36, x + s*0.36, y + s*0.64, col);
        ring(c, x + s*0.52, cy, s*0.10, 2, col);
        ring(c, x + s*0.62, cy, s*0.18, 2, col);
        break;
    case EUI_ICON_MENU:
        eui_canvas_line(c, x + s*0.24, y + s*0.32, x + s*0.76, y + s*0.32, col);
        eui_canvas_line(c, x + s*0.24, y + s*0.50, x + s*0.76, y + s*0.50, col);
        eui_canvas_line(c, x + s*0.24, y + s*0.68, x + s*0.76, y + s*0.68, col);
        break;
    case EUI_ICON_REFRESH:
        ring(c, cx, cy, s*0.22, 2, col);
        eui_canvas_line(c, cx + s*0.22, cy, cx + s*0.06, cy - s*0.16, col);
        eui_canvas_line(c, cx + s*0.22, cy, cx + s*0.22, cy - s*0.16, col);
        break;
    case EUI_ICON_EDIT:
        eui_canvas_line(c, x + s*0.30, y + s*0.70, x + s*0.62, y + s*0.38, col);
        eui_canvas_line(c, x + s*0.62, y + s*0.38, x + s*0.72, y + s*0.48, col);
        eui_canvas_line(c, x + s*0.72, y + s*0.48, x + s*0.40, y + s*0.80, col);
        break;
    case EUI_ICON_SAVE:
        eui_canvas_round(c, x + s*0.22, y + s*0.24, s*0.56, s*0.52, 3, col);
        eui_canvas_round(c, x + s*0.36, y + s*0.24, s*0.28, s*0.20, 2, 0x000000);
        break;
    case EUI_ICON_COPY:
        eui_canvas_round_stroke(c, x + s*0.26, y + s*0.26, s*0.40, s*0.40, 3, 2, col);
        eui_canvas_round(c, x + s*0.40, y + s*0.40, s*0.40, s*0.40, 3, col);
        break;
    case EUI_ICON_USER:
        disc(c, cx, y + s*0.36, s*0.14, col);
        eui_canvas_round(c, x + s*0.24, y + s*0.54, s*0.52, s*0.24, s*0.12, col);
        break;
    case EUI_ICON_LOCK:
        ring(c, cx, y + s*0.46, s*0.16, 2, col);
        eui_canvas_round(c, x + s*0.30, y + s*0.48, s*0.40, s*0.34, 3, col);
        break;
    case EUI_ICON_IMAGE:
        eui_canvas_round_stroke(c, x + s*0.20, y + s*0.24, s*0.60, s*0.52, 3, 2, col);
        disc(c, x + s*0.38, y + s*0.42, s*0.06, col);
        eui_canvas_line(c, x + s*0.24, y + s*0.70, x + s*0.46, y + s*0.50, col);
        eui_canvas_line(c, x + s*0.46, y + s*0.50, x + s*0.60, y + s*0.64, col);
        eui_canvas_line(c, x + s*0.60, y + s*0.64, x + s*0.78, y + s*0.46, col);
        break;
    case EUI_ICON_MUSIC:
        disc(c, x + s*0.36, y + s*0.66, s*0.07, col);
        disc(c, x + s*0.64, y + s*0.66, s*0.07, col);
        eui_canvas_line(c, x + s*0.40, y + s*0.66, x + s*0.40, y + s*0.26, col);
        eui_canvas_line(c, x + s*0.68, y + s*0.66, x + s*0.68, y + s*0.30, col);
        eui_canvas_line(c, x + s*0.40, y + s*0.26, x + s*0.68, y + s*0.30, col);
        break;
    case EUI_ICON_HELP:
        ring(c, cx, cy, s*0.26, 2, col);
        disc(c, cx, y + s*0.62, s*0.05, col);
        eui_canvas_line(c, cx, y + s*0.30, cx, y + s*0.50, col);
        eui_canvas_line(c, cx, y + s*0.50, cx + s*0.10, y + s*0.44, col);
        break;
    case EUI_ICON_STAR:
        for (int i = 0; i < 5; i++) {
            float a1 = -1.5708f + i * 2 * 3.14159f / 5;
            float a2 = a1 + 3.14159f / 5;
            int x1 = cx + (int)(cosf_approx(a1) * s*0.30), y1 = cy + (int)(sinf_approx(a1) * s*0.30);
            int x2 = cx + (int)(cosf_approx(a2) * s*0.13), y2 = cy + (int)(sinf_approx(a2) * s*0.13);
            eui_canvas_line(c, cx, cy, x1, y1, col);
            eui_canvas_line(c, x1, y1, x2, y2, col);
        }
        break;
    case EUI_ICON_HOME:
        eui_canvas_line(c, cx, y + s*0.26, x + s*0.22, y + s*0.48, col);
        eui_canvas_line(c, cx, y + s*0.26, x + s*0.78, y + s*0.48, col);
        eui_canvas_line(c, x + s*0.22, y + s*0.48, x + s*0.22, y + s*0.74, col);
        eui_canvas_line(c, x + s*0.78, y + s*0.48, x + s*0.78, y + s*0.74, col);
        eui_canvas_line(c, x + s*0.22, y + s*0.74, x + s*0.78, y + s*0.74, col);
        break;

    /* ── custom-shape demos (triangle / polygon primitives) ───── */
    case EUI_ICON_TRIANGLE: {
        eui_point p[3] = { {x + s*0.50f, y + s*0.15f}, {x + s*0.85f, y + s*0.80f}, {x + s*0.15f, y + s*0.80f} };
        eui_canvas_polygon(c, p, 3, col);
        return;
    }
    case EUI_ICON_STARFILL: {
        eui_point p[10]; float cx2 = x + s*0.5f, cy2 = y + s*0.5f, R = s*0.42f, r = s*0.18f;
        for (int i = 0; i < 5; i++) {
            float a1 = -1.5707963f + i * 2 * 3.14159265f / 5;
            float a2 = a1 + 3.14159265f / 5;
            p[i*2]   = (eui_point){ cx2 + cosf_approx(a1) * R, cy2 + sinf_approx(a1) * R };
            p[i*2+1] = (eui_point){ cx2 + cosf_approx(a2) * r, cy2 + sinf_approx(a2) * r };
        }
        eui_canvas_polygon(c, p, 10, col);
        return;
    }
    case EUI_ICON_HEART: {
        eui_point p[40]; float cx2 = x + s*0.5f, cy2 = y + s*0.46f, sc = s*0.34f;
        for (int i = 0; i < 40; i++) {
            float t = (float)i / 40 * 2 * 3.14159265f;
            float ct = cosf_approx(t), st = sinf_approx(t);
            float px = ct * ct * ct;                       /* |cos|^3, sign preserved */
            float py = 13*st - 5*sinf_approx(2*t) - 2*sinf_approx(3*t) - sinf_approx(4*t);
            p[i] = (eui_point){ cx2 + px * sc, cy2 - (py / 16.0f) * sc };
        }
        eui_canvas_polygon(c, p, 40, col);
        return;
    }
    default: break;
    }
    eui_canvas_pop_clip(c);
}
