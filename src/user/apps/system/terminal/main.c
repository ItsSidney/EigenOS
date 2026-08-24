/* EigenOS Terminal — a real VT100/xterm-style emulator over a kernel PTY.
 * The GUI side owns the pty master: it parses escape sequences into a cell
 * grid with scrollback and forwards keyboard bytes to the line discipline.
 * /bin/sh runs on the slave end. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include "userlib.h"
#include "eigen.h"

#define WIN_W  660
#define WIN_H  440
#define COLS   80
#define ROWS   24
#define SB_LINES 400            /* scrollback capacity */

/* palette: 0-7 normal, 8-15 bright */
static const uint32_t pal[16] = {
    0x2E3440, 0xBF616A, 0xA3BE8C, 0xEBCB8B,
    0x81A1C1, 0xB48EAD, 0x88C0D0, 0xD8DEE9,
    0x4C566A, 0xBF616A, 0xA3BE8C, 0xEBCB8B,
    0x81A1C1, 0xB48EAD, 0x8FBCBB, 0xECEFF4
};
#define BG_DEF 0x2E3440
#define FG_DEF 0xD8DEE9

typedef struct { uint32_t fg, bg; } style_t;
typedef struct { char ch; uint32_t fg; uint32_t bg; } cell_t;

static int      win_id = -1;
static uint32_t* win_fb;
static int       cur_w = WIN_W, cur_h = WIN_H;

#define CHAR_W 8
#define G_LINE 16

/* screen state */
static cell_t   grid[ROWS][COLS];
static int      cx, cy;                 /* cursor */
static style_t  cur_st = { FG_DEF, BG_DEF };
static int      scroll_top = 0, scroll_bot = ROWS - 1;

/* scrollback: finished rows pushed here when scrolling up */
static cell_t*  sb[SB_LINES];
static int      sb_head = 0, sb_count = 0;
static int      sb_off = 0;             /* how far back we're viewing */

/* pty */
static int mfd = -1, sfd = -1;
static int sh_pid = -1;
static int blink_vis = 1;

/* VT parser state machine */
enum { P_GROUND, P_ESC, P_CSI };
static int  pstate = P_GROUND;
static char pb[32]; static int pb_len;
static int  saved_cx, saved_cy;

/* ------------------------------------------------------------------ */
static void sb_push_row(void) {
    if (!sb[sb_head]) sb[sb_head] = malloc(sizeof(cell_t) * COLS);
    if (!sb[sb_head]) return;
    memcpy(sb[sb_head], grid[0], sizeof(cell_t) * COLS);
    sb_head = (sb_head + 1) % SB_LINES;
    if (sb_count < SB_LINES) sb_count++;
}

static void grid_scroll_up(void) {
    sb_push_row();
    memmove(grid[0], grid[1], sizeof(cell_t) * (ROWS - 1) * COLS);
    for (int x = 0; x < COLS; x++) {
        grid[scroll_bot][x].ch = ' ';
        grid[scroll_bot][x].fg = cur_st.fg;
        grid[scroll_bot][x].bg = cur_st.bg;
    }
}
static void grid_scroll_down(void) {
    memmove(&grid[1], &grid[0], sizeof(cell_t) * (ROWS - 1) * COLS);
    for (int x = 0; x < COLS; x++) {
        grid[0][x].ch = ' ';
        grid[0][x].fg = cur_st.fg;
        grid[0][x].bg = cur_st.bg;
    }
}
static void putc_cell(char c) {
    if (cx >= COLS) { cx = 0; if (cy == scroll_bot) grid_scroll_up(); else cy++; }
    grid[cy][cx].ch = c;
    grid[cy][cx].fg = cur_st.fg;
    grid[cy][cx].bg = cur_st.bg;
    cx++;
}

static int idx_of(uint32_t color) {
    for (int i = 0; i < 16; i++) if (pal[i] == color) return i % 8;
    return 7;
}
static void csi_dispatch(char fin) {
    int n[8] = {0}; int nn = 0; int def = 1;
    for (int i = 0; i < pb_len && nn < 8; i++) {
        if (pb[i] == ';') { n[nn++] = def ? 1 : n[nn]; def = 1; continue; }
        if (pb[i] >= '0' && pb[i] <= '9') {
            if (def) { n[nn++] = pb[i]-'0'; def = 0; }
            else n[nn-1] = n[nn-1]*10 + (pb[i]-'0');
        }
    }
    if (!nn) { n[0] = def ? 1 : n[0]; }
    switch (fin) {
    case 'A': cy -= n[0]; if (cy < 0) cy = 0; break;
    case 'B': cy += n[0]; if (cy > scroll_bot) cy = scroll_bot; break;
    case 'C': cx += n[0]; if (cx > COLS-1) cx = COLS-1; break;
    case 'D': cx -= n[0]; if (cx < 0) cx = 0; break;
    case 'H': case 'f':
        cy = (n[0]?n[0]:1) - 1; cx = (n[1]?n[1]:1) - 1;
        if (cy > scroll_bot) cy = scroll_bot; if (cy < 0) cy = 0;
        if (cx > COLS-1) cx = COLS-1; if (cx < 0) cx = 0;
        break;
    case 'J':
        if (n[0] == 2 || n[0] == 3 || !n[0] && pb_len == 0) {
            for (int r = 0; r < ROWS; r++)
                for (int c = 0; c < COLS; c++) {
                    grid[r][c].ch=' '; grid[r][c].fg=FG_DEF; grid[r][c].bg=BG_DEF;
                }
        } else if (n[0] == 0)
            for (int c = cx; c < COLS; c++){grid[cy][c].ch=' ';grid[cy][c].fg=cur_st.fg;}
        else if (n[0] == 1)
            for (int c = 0; c <= cx && c < COLS; c++){grid[cy][c].ch=' ';grid[cy][c].fg=cur_st.fg;}
        break;
    case 'K':
        if (n[0]==0) for(int c=cx;c<COLS;c++){grid[cy][c].ch=' ';grid[cy][c].fg=cur_st.fg;}
        else if(n[0]==1) for(int c=0;c<=cx;c++){grid[cy][c].ch=' ';grid[cy][c].fg=cur_st.fg;}
        else for(int c=0;c<COLS;c++){grid[cy][c].ch=' ';grid[cy][c].fg=cur_st.fg;}
        break;
    case 'm': { /* SGR colors */
        int i = 0;
        if (pb_len == 0) { cur_st.fg = FG_DEF; cur_st.bg = BG_DEF; break; }
        /* re-parse all params including empty ones */
        int v[16], vn = 0; char tmp[8]; int ti = 0;
        for (i = 0; i <= pb_len && vn < 16; i++) {
            if (i == pb_len || pb[i] == ';') { tmp[ti]=0; v[vn++] = ti?atoi(tmp):0; ti=0; }
            else if (ti < 7) tmp[ti++] = pb[i];
        }
        for (i = 0; i < vn; i++) {
            int p = v[i];
            if (p == 0) { cur_st.fg = FG_DEF; cur_st.bg = BG_DEF; }
            else if (p == 1) { if (cur_st.fg < pal[8]) cur_st.fg = pal[(cur_st.fg==FG_DEF?7:idx_of(cur_st.fg))] ; }
            else if (p >= 30 && p <= 37) cur_st.fg = pal[p-30];
            else if (p >= 90 && p <= 97) cur_st.fg = pal[p-90+8];
            else if (p >= 40 && p <= 47) cur_st.bg = pal[p-40];
            else if (p >= 100 && p <= 107) cur_st.bg = pal[p-100+8];
            else if (p == 39) cur_st.fg = FG_DEF;
            else if (p == 49) cur_st.bg = BG_DEF;
        }
        break; }
    case 's': saved_cx = cx; saved_cy = cy; break;
    case 'u': cx = saved_cx; cy = saved_cy; break;
    default: break;
    }
}


static void vt_feed(const char* s, int n) {
    for (int i = 0; i < n; i++) {
        char c = s[i];
        if (pstate == P_GROUND) {
            if (c == 0x1B) { pstate = P_ESC; pb_len = 0; }
            else if (c == '\r') cx = 0;
            else if (c == '\n') { if (cy == scroll_bot) grid_scroll_up(); else cy++; }
            else if (c == '\b') { if (cx) cx--; }
            else if (c == '\t') { cx = (cx + 8) & ~7; if (cx >= COLS) cx = COLS-1; }
            else if ((unsigned char)c >= 0x20) putc_cell(c);
        } else if (pstate == P_ESC) {
            if (c == '[') { pstate = P_CSI; pb_len = 0; }
            else if (c == 'c') { /* full reset */
                memset(grid, 0, sizeof(grid));
                for (int r=0;r<ROWS;r++) for(int cc=0;cc<COLS;cc++){
                    grid[r][cc].ch=' ';grid[r][cc].fg=FG_DEF;grid[r][cc].bg=BG_DEF;}
                cur_st.fg = FG_DEF; cur_st.bg = BG_DEF; cx=cy=0; pstate=P_GROUND;
            } else if (c == '7') { saved_cx=cx; saved_cy=cy; pstate=P_GROUND; }
            else if (c == '8') { cx=saved_cx; cy=saved_cy; pstate=P_GROUND; }
            else if (c == ')' || c == '(') { i++; pstate = P_GROUND; } /* charset */
            else pstate = P_GROUND;
        } else { /* CSI */
            if ((unsigned char)c >= 0x40 && (unsigned char)c <= 0x7E &&
                !(c >= '0' && c <= '9') && c != ';' && c != '?') {
                csi_dispatch(c);
                pstate = P_GROUND;
            } else if (pb_len < 30) {
                pb[pb_len++] = c;
            } else pstate = P_GROUND;
        }
    }
}

/* ------------------------------------------------------------------ */
/* rendering                                                            */
static void draw_row_cells(int px, int py, cell_t* r) {
    static char rowbuf[COLS+1];
    int sx = 0;
    while (sx < COLS) {
        int ex = sx;
        while (ex < COLS && r[ex].fg == r[sx].fg && r[ex].bg == r[sx].bg) ex++;
        int len = ex - sx;
        int k; for (k = 0; k < len; k++) rowbuf[k] = r[sx+k].ch;
        rowbuf[k] = 0;
        while (k > 0 && rowbuf[k-1]==' ') rowbuf[--k]=0;
        if (k) eigen_draw_text(win_fb, cur_w, cur_h, px + sx*CHAR_W, py, rowbuf, r[sx].fg);
        sx = ex;
    }
}

static void render_screen(void) {
    eigen_draw_fillrect(win_fb, cur_w, cur_h, 0, 0, cur_w, cur_h, BG_DEF);

    int pad = 8;
    int view_rows = (cur_h - pad*2) / G_LINE;   /* rows that fit in the window */
    if (view_rows > ROWS) view_rows = ROWS;

    /* how many live grid rows are on-screen */
    int sb_view = sb_off;                       /* user-requested history depth */
    if (sb_view > sb_count) sb_view = sb_count;
    if (sb_view > view_rows) sb_view = view_rows;
    int live_vis = view_rows - sb_view;

    int py = pad;
    /* scrollback slice (oldest at top) */
    for (int i = sb_view; i > 0; i--, py += G_LINE) {
        int idx = (sb_head - i + SB_LINES*2) % SB_LINES;
        if (!sb[idx]) continue;
        draw_row_cells(pad, py, sb[idx]);
    }
    /* live grid: bottom `live_vis` rows */
    int g0 = ROWS - live_vis;
    for (int gr = g0; gr < ROWS; gr++, py += G_LINE)
        draw_row_cells(pad, py, grid[gr]);

    /* cursor */
    if (sb_off == 0) {
        static uint32_t blink_t = 0;
        uint32_t now = eigen_gettime_ms();
        if (now - blink_t > 500) { blink_vis ^= 1; blink_t = now; }
        if (blink_vis && cy >= g0)
            eigen_draw_fillrect(win_fb, cur_w, cur_h, pad + cx*CHAR_W,
                                pad + sb_view*G_LINE + (cy-g0)*G_LINE,
                                CHAR_W, G_LINE-1, 0x88C0D0);
    }
    eigen_win_flush(win_id);
}

/* ------------------------------------------------------------------ */
static void spawn_shell(void) {
    int fds[2];
    if (eigen_openpty(fds) != 0) return;
    mfd = fds[0]; sfd = fds[1];

    const char* argv[] = { "/user/sh.elf", NULL };
    int inherit[3] = { sfd, sfd, sfd };
    sh_pid = eigen_spawn_fds("/user/sh.elf", 1, (char* const*)argv, inherit);
    /* diag goes to the pre-pty console (fd1 at terminal start) */
    { char dbg[80]; int dn = snprintf(dbg,sizeof(dbg),
        "[TERM] spawn pid=%d m=%d s=%d\n", sh_pid, mfd, sfd);
      write(1, dbg, dn); }
    if (sh_pid > 0) {
        eigen_pty_setfg(mfd, sh_pid);
        const char* banner = "\x1b[36mEigenOS shell\x1b[0m\r\n";
        vt_feed(banner, (int)strlen(banner));
    } else {
        const char* err = "spawn /user/sh.elf failed\r\n";
        vt_feed(err, (int)strlen(err));
    }
    close(sfd);          /* keep only master in the emulator */
    sfd = -1;
    fcntl(mfd, 4, 0x800); /* O_NONBLOCK reads */
}

static void send_key(eigen_ev_t* ev) {
    if (mfd < 0) return;
    char seq[8];
    int code = ev->b, mods = ev->c;
    char ch = (char)ev->a;

    if ((mods & 2)) {                     /* ctrl */
        if (ch=='c'||ch=='C'){ write(mfd,"\x03",1); return; }
        if (ch=='d'||ch=='D'){ write(mfd,"\x04",1); return; }
        if (ch=='u'||ch=='U'){ write(mfd,"\x15",1); return; }
        if (ch=='l'||ch=='L'){ write(mfd,"\x0c",1); return; }
    }
    switch (code) {
    case 0x48: write(mfd, "\x1b[A", 3); return;  /* up */
    case 0x50: write(mfd, "\x1b[B", 3); return;  /* down */
    case 0x4B: write(mfd, "\x1b[D", 3); return;  /* left */
    case 0x4D: write(mfd, "\x1b[C", 3); return;  /* right */
    case 0x0E: write(mfd, "\x7f", 1);   return;  /* backspace */
    case 0x1C: write(mfd, "\r", 1);     return;  /* enter */
    case 0x39: write(mfd, " ", 1);      return;  /* space */
    case 0x49: if (sb_off < sb_count) sb_off += 10; return;
    case 0x51: sb_off -= 10; if (sb_off < 0) sb_off = 0; return;
    default: break;
    }
    if (ch >= 32 && ch < 127) { seq[0]=ch; write(mfd, seq, 1); }
}

int main(int argc, char* argv[]) {
    (void)argc;(void)argv;
    win_id = eigen_win_create(80, 60, WIN_W, WIN_H, "Terminal");
    if (win_id < 0) return 1;
    win_fb = (uint32_t*)eigen_win_map(win_id);
    eigen_win_getsize(win_id, &cur_w, &cur_h);

    for (int r=0;r<ROWS;r++) for (int c=0;c<COLS;c++){
        grid[r][c].ch=' ';grid[r][c].fg=FG_DEF;grid[r][c].bg=BG_DEF;}

    spawn_shell();

    char rbuf[256];
    eigen_ev_t evs[8];
    int running = 1;
    while (running) {
        int n = eigen_win_poll(win_id, evs, 8);
        win_fb = (uint32_t*)eigen_win_map(win_id);
        eigen_win_getsize(win_id, &cur_w, &cur_h);

        for (int i = 0; i < n; i++) {
            if (evs[i].type == EIGEN_EV_CLOSE) running = 0;
            else if (evs[i].type == EIGEN_EV_KEY) send_key(&evs[i]);
        }

        if (mfd >= 0) {
            int got = read(mfd, rbuf, sizeof(rbuf));
            if (got > 0) vt_feed(rbuf, got);
        }

        render_screen();
        eigen_sleep_ms(20);
    }
    if (sh_pid > 0) eigen_kill(sh_pid, SIGKILL);
    if (mfd >= 0) close(mfd);
    eigen_win_close(win_id);
    return 0;
}
