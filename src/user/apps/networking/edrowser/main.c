/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

/*********************************************************************
 * Eigen OS — Edrowser 2.0 (Ring 3 Userland Browser)
 *
 * Modern web browser for EigenOS running in Ring 3 with full memory
 * isolation, HTML/CSS layout, HTTP/1.1, chunked transfer decoding,
 * redirect handling, and JPEG/PNG/BMP image rendering.
 *********************************************************************/

#include "userlib.h"
#include "userui.h"
#include "tiny_jpeg.h"
#include "tiny_png.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define WIN_W        960
#define WIN_H        640
#define MAX_EVS      32

#define RAW_MAX      1048576  /* 1 MB raw HTTP buffer */
#define BODY_TXT_MAX 524288   /* 512 KB text buffer */
#define MAX_HEADERS  64
#define MAX_HIST     32
#define FONT_W       8
#define FONT_H       16
#define LINE_H       16
#define MAX_LINES    8192
#define MAX_RUNS     8192
#define MAX_LINKS    2048
#define MAX_IMGS     32
#define MAX_STACK    32
#define MAX_CSS      128

#define TOP_BAR_H    42
#define FOOTER_H     24

/* ---- Sentinels ---- */
#define BR_CH        2
#define HR_CH        1
#define BULLET_CH    7

typedef struct {
    uint32_t fg, bg;
    int scale;
    int alpha;
    int underline;
    int bold;
} cstyle_t;

typedef struct {
    int start, len;
    int link;
    cstyle_t st;
} run_t;

typedef struct {
    char href[512];
    int start, len;
    int rx, ry, rw, rh;
    int visible;
} link_t;

typedef struct {
    char     src[512];
    int      state;     /* 0=none, 1=fetching, 2=decoded, 3=failed */
    int      after;
    uint8_t* raw;
    int      raw_len;
    uint8_t* pixels;
    int      width, height;
    int      placed;
    int      doc_y;
    int      disp_w, disp_h;
} img_t;

typedef struct {
    char sel[48];
    uint32_t fg, bg;
    int fg_set, bg_set;
    int scale, scale_set;
    int alpha, alpha_set;
    int underline, underline_set;
} css_rule_t;

/* ---- State ---- */
static int win_id = -1;
static uint32_t* win_fb = NULL;
static uint32_t cur_w = WIN_W, cur_h = WIN_H;

static char* raw_buf = NULL;
static int   raw_len = 0;
static int   body_offset = 0;

static char* body_txt = NULL;
static int   body_txt_len = 0;

static int*  line_start = NULL;
static int*  line_len_arr = NULL;
static int*  line_y_doc = NULL;
static int*  line_scale = NULL;
static int   num_lines = 0;

static run_t*      runs = NULL;
static int         run_count = 0;
static link_t*     links = NULL;
static int         link_count = 0;
static img_t       imgs[MAX_IMGS];
static int         img_count = 0;
static css_rule_t* css_rules = NULL;
static int         css_rule_count = 0;

static cstyle_t style_stack[MAX_STACK];
static int      style_depth = 0;
static cstyle_t cur_style;

static int scroll_y = 0;
static int content_h = 0;
static int bw_loading = 0;
static int zoom_level = 1;
static int reader_mode = 0;

static char url_buf[512] = "http://frogfind.com";
static int  editing_url = 0;
static char url_edit_text[512] = "";
static int  url_edit_len = 0;

static char search_buf[128] = "";
static int  search_len = 0;
static int  match_count = 0;
static int  match_cursor = 0;
static int* match_pos = NULL;

static int      status_code = 0;
static char     status_line[128] = "";
static char     content_type[128] = "";
static uint32_t fetch_time_ms = 0;
static uint32_t ttfb_ms = 0;
static char     fetch_error[200] = "";

static char header_keys[MAX_HEADERS][64];
static char header_vals[MAX_HEADERS][256];
static int  header_count = 0;

static char history[MAX_HIST][512];
static int  history_count = 0;
static int  history_idx = -1;

static uint32_t page_bg = 0xFFFFFF;
static int      last_cpl = -1;

/* Forward declarations */
static void bw_fetch_url(const char* url);
static void flatten_body_text(int cpl);
static void recompute_lines(int chars_per_line);
static void render_all(void);
static void resolve_href(const char* href, const char* base, char* out, int olen);

/* ---- URL Parsing ---- */
static void parse_url(const char* url, char* host, int hmax, char* path, int pmax, int* port_out) {
    int i = 0, default_port = 80;
    if (strncmp(url, "https://", 8) == 0) { i = 8; default_port = 443; }
    else if (strncmp(url, "http://", 7) == 0) i = 7;

    int hi = 0;
    while (url[i] && url[i] != '/' && url[i] != ':' && hi < hmax - 1)
        host[hi++] = url[i++];
    host[hi] = 0;
    *port_out = default_port;

    if (url[i] == ':') {
        i++;
        int p = 0;
        while (url[i] >= '0' && url[i] <= '9') p = p * 10 + (url[i++] - '0');
        if (p > 0 && p < 65536) *port_out = p;
    }
    int pi = 0;
    if (url[i] == '/') {
        while (url[i] && pi < pmax - 1) path[pi++] = url[i++];
    } else {
        path[0] = '/'; pi = 1;
    }
    path[pi] = 0;
}

static void resolve_href(const char* href, const char* base, char* out, int olen) {
    if (strncmp(href, "http://", 7) == 0 || strncmp(href, "https://", 8) == 0) {
        strncpy(out, href, olen - 1); out[olen - 1] = 0;
        return;
    }
    if (strncmp(href, "//", 2) == 0) {
        snprintf(out, olen, "http:%s", href);
        return;
    }
    char bhost[128], bpath[256];
    int bport = 80;
    parse_url(base, bhost, sizeof(bhost), bpath, sizeof(bpath), &bport);
    int is_https = (strncmp(base, "https://", 8) == 0 || bport == 443);
    const char* proto = is_https ? "https://" : "http://";
    if (href[0] == '/') {
        if (bport == 80 || bport == 443)
            snprintf(out, olen, "%s%s%s", proto, bhost, href);
        else
            snprintf(out, olen, "%s%s:%d%s", proto, bhost, bport, href);
        return;
    }
    char dir[256];
    strncpy(dir, bpath, 255); dir[255] = 0;
    int slash = -1;
    for (int i = 0; dir[i]; i++) if (dir[i] == '/') slash = i;
    if (slash >= 0) dir[slash + 1] = 0; else dir[0] = 0;
    if (bport == 80 || bport == 443)
        snprintf(out, olen, "%s%s%s%s", proto, bhost, dir, href);
    else
        snprintf(out, olen, "%s%s:%d%s%s", proto, bhost, bport, dir, href);
}

/* ---- HTTP Header Parser ---- */
static void parse_http_response(void) {
    status_code = 0;
    status_line[0] = 0;
    content_type[0] = 0;
    header_count = 0;
    body_offset = 0;

    if (raw_len == 0) return;

    int i = 0;
    while (i < raw_len && raw_buf[i] != '\r' && raw_buf[i] != '\n') i++;
    int slen = (i < 127) ? i : 127;
    memcpy(status_line, raw_buf, slen);
    status_line[slen] = 0;

    const char* p = status_line;
    while (*p && *p != ' ') p++;
    if (*p == ' ') {
        p++;
        status_code = 0;
        while (*p >= '0' && *p <= '9') { status_code = status_code * 10 + (*p - '0'); p++; }
    }

    if (i < raw_len && raw_buf[i] == '\r') i++;
    if (i < raw_len && raw_buf[i] == '\n') i++;

    while (i < raw_len && header_count < MAX_HEADERS) {
        if ((i < raw_len && raw_buf[i] == '\n') ||
            (i + 1 < raw_len && raw_buf[i] == '\r' && raw_buf[i+1] == '\n')) {
            if (raw_buf[i] == '\r') i++;
            if (i < raw_len && raw_buf[i] == '\n') i++;
            break;
        }
        int start = i;
        while (i < raw_len && raw_buf[i] != '\r' && raw_buf[i] != '\n') i++;
        int line_len = i - start;
        if (i < raw_len && raw_buf[i] == '\r') i++;
        if (i < raw_len && raw_buf[i] == '\n') i++;

        int colon = -1;
        for (int j = 0; j < line_len; j++) {
            if (raw_buf[start + j] == ':') { colon = j; break; }
        }
        if (colon > 0 && colon < 63) {
            memcpy(header_keys[header_count], raw_buf + start, colon);
            header_keys[header_count][colon] = 0;
            int vstart = colon + 1;
            while (vstart < line_len && raw_buf[start + vstart] == ' ') vstart++;
            int vlen = line_len - vstart;
            if (vlen > 255) vlen = 255;
            memcpy(header_vals[header_count], raw_buf + start + vstart, vlen);
            header_vals[header_count][vlen] = 0;

            if (strcmp(header_keys[header_count], "Content-Type") == 0 ||
                strcmp(header_keys[header_count], "content-type") == 0) {
                strncpy(content_type, header_vals[header_count], 127);
                content_type[127] = 0;
            }
            header_count++;
        }
    }
    body_offset = i;
}

static void reset_page_content(void) {
    for (int i = 0; i < img_count; i++) {
        if (imgs[i].raw)    { free(imgs[i].raw);    imgs[i].raw = NULL; }
        if (imgs[i].pixels) { free(imgs[i].pixels); imgs[i].pixels = NULL; }
        memset(&imgs[i], 0, sizeof(img_t));
    }
    img_count = 0;
    run_count = 0;
    link_count = 0;
    css_rule_count = 0;
    style_depth = 0;
    page_bg = reader_mode ? 0xFAF8F5 : 0xFFFFFF;
    cur_style.fg = reader_mode ? 0x1A1A1A : 0x1F2328;
    cur_style.bg = page_bg;
    cur_style.scale = reader_mode ? 2 : zoom_level;
    cur_style.alpha = 255;
    cur_style.underline = 0;
    cur_style.bold = 0;
}

/* ---- Fetch Engine ---- */
static void bw_fetch_url(const char* url) {
    if (!url || !url[0]) return;
    bw_loading = 1;
    render_all();

    char host[128], path[256];
    int port = 80;
    int is_https = (strncmp(url, "https://", 8) == 0);
    parse_url(url, host, sizeof(host), path, sizeof(path), &port);
    if (is_https && port == 80) port = 443;
    if (port == 443) is_https = 1;

    uint32_t t_start = eigen_gettime_ms();
    fetch_error[0] = 0;

    uint32_t ip = 0;
    if (eigen_dns_resolve(host, &ip) < 0) {
        bw_loading = 0;
        status_code = 502;
        snprintf(fetch_error, sizeof(fetch_error), "DNS resolve failed for '%s'", host);
        reset_page_content();
        render_all();
        return;
    }

    int sock = eigen_socket(2, 1, 0);
    if (sock < 0) {
        bw_loading = 0;
        status_code = 500;
        snprintf(fetch_error, sizeof(fetch_error), "Could not allocate network socket");
        render_all();
        return;
    }

    if (eigen_connect(sock, ip, (uint16_t)port) < 0) {
        bw_loading = 0;
        status_code = 504;
        snprintf(fetch_error, sizeof(fetch_error), "Could not connect to %s:%d", host, port);
        eigen_socket_close(sock);
        render_all();
        return;
    }

    char req[768];
    int ri = snprintf(req, sizeof(req),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: Mozilla/5.0 (compatible; EigenOS/1.0; x86_64)\r\n"
        "Accept: text/html,application/xhtml+xml,image/*;q=0.9,*/*;q=0.8\r\n"
        "Accept-Encoding: identity\r\n"
        "Connection: close\r\n\r\n", path, host);

    eigen_send(sock, req, ri, 0);

    raw_len = 0;
    uint32_t t_first = 0;
    uint32_t t0 = eigen_gettime_ms();

    while (raw_len < RAW_MAX - 1) {
        eigen_net_poll();
        int n = eigen_recv(sock, raw_buf + raw_len, RAW_MAX - 1 - raw_len, 0);
        if (n > 0) {
            raw_len += n;
            if (t_first == 0) t_first = eigen_gettime_ms();
        } else {
            break;
        }
        if (eigen_gettime_ms() - t0 > 15000) break;
        eigen_sleep_ms(5);
    }
    raw_buf[raw_len] = 0;
    eigen_socket_close(sock);

    fetch_time_ms = eigen_gettime_ms() - t_start;
    ttfb_ms = (t_first > 0) ? (t_first - t_start) : fetch_time_ms;

    parse_http_response();

    /* Chunked decoding */
    {
        int is_chunked = 0;
        for (int hi = 0; hi < header_count; hi++) {
            if ((strcmp(header_keys[hi], "Transfer-Encoding") == 0 ||
                 strcmp(header_keys[hi], "transfer-encoding") == 0) &&
                strstr(header_vals[hi], "chunked") != NULL) {
                is_chunked = 1; break;
            }
        }
        if (is_chunked && body_offset < raw_len) {
            char* buf = raw_buf + body_offset;
            int   len = raw_len - body_offset;
            int rp = 0, wp = 0;
            while (rp < len) {
                int sz = 0;
                while (rp < len && buf[rp] != '\r' && buf[rp] != '\n') {
                    char c = buf[rp++];
                    if      (c >= '0' && c <= '9') sz = sz*16 + (c-'0');
                    else if (c >= 'a' && c <= 'f') sz = sz*16 + (c-'a'+10);
                    else if (c >= 'A' && c <= 'F') sz = sz*16 + (c-'A'+10);
                    else break;
                }
                if (rp < len && buf[rp] == '\r') rp++;
                if (rp < len && buf[rp] == '\n') rp++;
                if (sz == 0) break;
                if (rp + sz > len) sz = len - rp;
                memmove(buf + wp, buf + rp, sz);
                wp += sz; rp += sz;
                if (rp < len && buf[rp] == '\r') rp++;
                if (rp < len && buf[rp] == '\n') rp++;
            }
            raw_len = body_offset + wp;
            raw_buf[raw_len] = 0;
        }
    }

    /* Redirects (301/302/303/307) */
    if (status_code == 301 || status_code == 302 ||
        status_code == 303 || status_code == 307) {
        static int red_depth = 0;
        if (red_depth < 5) {
            char loc[512] = "";
            for (int hi = 0; hi < header_count; hi++) {
                if (strcmp(header_keys[hi], "Location") == 0 ||
                    strcmp(header_keys[hi], "location") == 0) {
                    strncpy(loc, header_vals[hi], 511);
                    break;
                }
            }
            if (loc[0]) {
                char res[512];
                resolve_href(loc, url_buf, res, sizeof(res));
                strncpy(url_buf, res, 511);
                red_depth++;
                bw_fetch_url(url_buf);
                red_depth--;
                return;
            }
        }
    }

    /* Update history */
    if (history_count == 0 || strcmp(history[history_count - 1], url_buf) != 0) {
        if (history_count < MAX_HIST) {
            strncpy(history[history_count], url_buf, 511);
            history_idx = history_count++;
        }
    }

    int cpl = (cur_w - 24) / (FONT_W * zoom_level);
    if (cpl < 1) cpl = 1;
    flatten_body_text(cpl);

    bw_loading = 0;
    scroll_y = 0;
    render_all();
}

/* ---- HTML Flattening & Layout ---- */
static void recompute_lines(int chars_per_line) {
    num_lines = 0;
    int pos = 0;
    int avail = chars_per_line * FONT_W * zoom_level;

    while (pos < body_txt_len && num_lines < MAX_LINES) {
        int st = pos;
        int x = 0;
        int max_scale = zoom_level;
        int last_space = -1;

        while (pos < body_txt_len) {
            char c = body_txt[pos];
            if (c == BR_CH) { pos++; break; }
            int sc = zoom_level;
            if (sc > max_scale) max_scale = sc;
            int w = FONT_W * sc;
            if (x + w > avail && x > 0) {
                if (last_space >= st) pos = last_space + 1;
                break;
            }
            if (c == ' ') last_space = pos;
            x += w;
            pos++;
        }
        while (pos > st && (body_txt[pos - 1] == ' ' || body_txt[pos - 1] == BR_CH)) pos--;
        int len = pos - st;
        if (len <= 0) { pos = st + 1; len = 1; }

        line_start[num_lines] = st;
        line_len_arr[num_lines] = len;
        line_scale[num_lines] = max_scale;
        num_lines++;
    }
    if (num_lines == 0) {
        num_lines = 1; line_start[0] = 0; line_len_arr[0] = 0; line_scale[0] = zoom_level;
    }
    int doc_y = 0;
    for (int ln = 0; ln < num_lines; ln++) {
        line_y_doc[ln] = doc_y;
        doc_y += line_scale[ln] * FONT_H + 6;
    }
    content_h = doc_y + 40;
}

static void flatten_body_text(int chars_per_line) {
    const char* body = raw_buf + body_offset;
    int body_len = raw_len - body_offset;
    body_txt_len = 0;
    body_txt[0] = 0;
    if (body_len <= 0) { num_lines = 0; last_cpl = chars_per_line; return; }

    reset_page_content();
    last_cpl = chars_per_line;

    /* Direct image URL handling */
    const uint8_t* ubody = (const uint8_t*)body;
    int is_png = (body_len >= 8 && ubody[0] == 0x89 && ubody[1] == 'P' && ubody[2] == 'N' && ubody[3] == 'G');
    int is_jpg = (body_len >= 2 && ubody[0] == 0xFF && ubody[1] == 0xD8);
    int is_bmp = (body_len >= 2 && ubody[0] == 'B'  && ubody[1] == 'M');

    if (is_png || is_jpg || is_bmp || strstr(content_type, "image/") != NULL) {
        uint8_t* rgb_pixels = NULL;
        int img_w = 0, img_h = 0;
        int decoded = 0;
        if (is_jpg || (body_len >= 2 && ubody[0] == 0xFF && ubody[1] == 0xD8))
            decoded = jpeg_decode(ubody, body_len, &rgb_pixels, &img_w, &img_h);
        else if (is_png || (body_len >= 8 && ubody[0] == 0x89 && ubody[1] == 'P' && ubody[2] == 'N' && ubody[3] == 'G'))
            decoded = png_decode(ubody, body_len, &rgb_pixels, &img_w, &img_h);

        if (decoded && img_w > 0 && img_h > 0 && rgb_pixels) {
            imgs[0].pixels = rgb_pixels;
            imgs[0].width  = img_w;
            imgs[0].height = img_h;
            imgs[0].state  = 2;
            imgs[0].placed = 0;
            imgs[0].after  = 0;
            img_count = 1;
            body_txt[0] = ' '; body_txt[1] = 0; body_txt_len = 1;
            recompute_lines(chars_per_line);
            return;
        }
    }

    /* Standard HTML text parser */
    int oi = 0, prev_space = 1, i = 0;
    while (i < body_len && oi < BODY_TXT_MAX - 4) {
        if (body[i] == '<') {
            int close_tag = (i + 1 < body_len && body[i+1] == '/');
            int tag_start = close_tag ? i + 2 : i + 1;
            int q = tag_start;
            while (q < body_len && body[q] != '>' && body[q] != ' ' && body[q] != '\t' && body[q] != '\r' && body[q] != '\n') q++;
            int tag_len = q - tag_start;
            char tag[32] = "";
            for (int k = 0; k < tag_len && k < 31; k++) {
                char c = body[tag_start + k];
                if (c >= 'A' && c <= 'Z') c += ('a' - 'A');
                tag[k] = c;
            }
            while (q < body_len && body[q] != '>') q++;
            if (q < body_len) q++;
            i = q;

            /* Line breaking tags */
            if (strcmp(tag, "p") == 0 || strcmp(tag, "br") == 0 || strcmp(tag, "div") == 0 ||
                strcmp(tag, "h1") == 0 || strcmp(tag, "h2") == 0 || strcmp(tag, "h3") == 0 ||
                strcmp(tag, "h4") == 0 || strcmp(tag, "li") == 0 || strcmp(tag, "tr") == 0) {
                if (oi > 0 && body_txt[oi - 1] != BR_CH) {
                    body_txt[oi++] = BR_CH;
                    prev_space = 1;
                }
            }
            continue;
        }

        /* Entities */
        if (body[i] == '&') {
            int e = i + 1;
            while (e < body_len && e < i + 10 && body[e] != ';') e++;
            if (e < body_len && body[e] == ';') {
                char ent[12] = "";
                int elen = e - (i + 1);
                if (elen < 11) {
                    memcpy(ent, body + i + 1, elen);
                    ent[elen] = 0;
                    if (strcmp(ent, "amp") == 0) { body_txt[oi++] = '&'; i = e + 1; prev_space = 0; continue; }
                    if (strcmp(ent, "lt") == 0)  { body_txt[oi++] = '<'; i = e + 1; prev_space = 0; continue; }
                    if (strcmp(ent, "gt") == 0)  { body_txt[oi++] = '>'; i = e + 1; prev_space = 0; continue; }
                    if (strcmp(ent, "quot") == 0){ body_txt[oi++] = '"'; i = e + 1; prev_space = 0; continue; }
                    if (strcmp(ent, "nbsp") == 0){ body_txt[oi++] = ' '; i = e + 1; prev_space = 1; continue; }
                }
            }
        }

        char c = body[i++];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            if (!prev_space && oi > 0 && body_txt[oi - 1] != BR_CH) {
                body_txt[oi++] = ' ';
                prev_space = 1;
            }
        } else {
            body_txt[oi++] = c;
            prev_space = 0;
        }
    }
    body_txt[oi] = 0;
    body_txt_len = oi;
    recompute_lines(chars_per_line);
}

/* ---- Drawing & GUI ---- */
static void render_all(void) {
    if (!win_fb) return;

    uint32_t bg_main = reader_mode ? 0xFAF8F5 : 0x0D1117;
    uint32_t chrome_bg = 0x161B22;
    uint32_t border_clr = 0x30363D;
    uint32_t text_clr = 0xE6EDF3;
    uint32_t dim_clr = 0x8B949E;
    uint32_t accent = 0x007AFF;

    /* 1. Clear background */
    eigen_draw_fillrect(win_fb, cur_w, cur_h, 0, 0, cur_w, cur_h, bg_main);

    /* 2. Top Navigation Bar */
    eigen_draw_fillrect(win_fb, cur_w, cur_h, 0, 0, cur_w, TOP_BAR_H, chrome_bg);
    eigen_draw_fillrect(win_fb, cur_w, cur_h, 0, TOP_BAR_H - 1, cur_w, 1, border_clr);

    /* Nav Buttons */
    eigen_draw_fillrect(win_fb, cur_w, cur_h, 8, 7, 28, 28, 0x21262D);
    eigen_draw_rect(win_fb, cur_w, cur_h, 8, 7, 28, 28, border_clr);
    eigen_draw_text(win_fb, cur_w, cur_h, 16, 13, "<-", text_clr);

    eigen_draw_fillrect(win_fb, cur_w, cur_h, 40, 7, 28, 28, 0x21262D);
    eigen_draw_rect(win_fb, cur_w, cur_h, 40, 7, 28, 28, border_clr);
    eigen_draw_text(win_fb, cur_w, cur_h, 48, 13, "->", text_clr);

    eigen_draw_fillrect(win_fb, cur_w, cur_h, 72, 7, 28, 28, 0x21262D);
    eigen_draw_rect(win_fb, cur_w, cur_h, 72, 7, 28, 28, border_clr);
    eigen_draw_text(win_fb, cur_w, cur_h, 82, 13, "R", text_clr);

    /* Address Bar */
    int bar_x = 108;
    int bar_w = cur_w - 260;
    uint32_t bar_bg = editing_url ? 0x0D1117 : 0x21262D;
    eigen_draw_fillrect(win_fb, cur_w, cur_h, bar_x, 7, bar_w, 28, bar_bg);
    eigen_draw_rect(win_fb, cur_w, cur_h, bar_x, 7, bar_w, 28, editing_url ? accent : border_clr);

    /* Security Icon */
    int is_sec = (strncmp(url_buf, "https://", 8) == 0);
    eigen_draw_text(win_fb, cur_w, cur_h, bar_x + 8, 13, is_sec ? "[S]" : "[!]", is_sec ? 0x3FB950 : dim_clr);

    /* URL Text */
    if (editing_url) {
        char disp[512];
        int l = url_edit_len;
        memcpy(disp, url_edit_text, l);
        if ((eigen_gettime_ms() / 500) % 2) disp[l++] = '_';
        disp[l] = 0;
        eigen_draw_text(win_fb, cur_w, cur_h, bar_x + 36, 13, disp[0] ? disp : "Enter URL...", text_clr);
    } else {
        eigen_draw_text(win_fb, cur_w, cur_h, bar_x + 36, 13, url_buf[0] ? url_buf : "Enter URL...", url_buf[0] ? text_clr : dim_clr);
    }

    /* Reader & Zoom Controls */
    eigen_draw_fillrect(win_fb, cur_w, cur_h, cur_w - 144, 7, 28, 28, reader_mode ? accent : 0x21262D);
    eigen_draw_rect(win_fb, cur_w, cur_h, cur_w - 144, 7, 28, 28, border_clr);
    eigen_draw_text(win_fb, cur_w, cur_h, cur_w - 138, 13, "Aa", text_clr);

    eigen_draw_fillrect(win_fb, cur_w, cur_h, cur_w - 112, 7, 28, 28, 0x21262D);
    eigen_draw_rect(win_fb, cur_w, cur_h, cur_w - 112, 7, 28, 28, border_clr);
    eigen_draw_text(win_fb, cur_w, cur_h, cur_w - 104, 13, "-", text_clr);

    eigen_draw_fillrect(win_fb, cur_w, cur_h, cur_w - 80, 7, 28, 28, 0x21262D);
    eigen_draw_rect(win_fb, cur_w, cur_h, cur_w - 80, 7, 28, 28, border_clr);
    eigen_draw_text(win_fb, cur_w, cur_h, cur_w - 72, 13, "+", text_clr);

    /* Loading Progress Bar */
    if (bw_loading) {
        int pw = (int)((eigen_gettime_ms() / 8) % cur_w);
        eigen_draw_fillrect(win_fb, cur_w, cur_h, 0, TOP_BAR_H - 3, pw, 3, accent);
    }

    /* 3. Page Content Area */
    int content_y = TOP_BAR_H + 4;
    int content_h_avail = cur_h - content_y - FOOTER_H;

    /* Render Images */
    for (int i = 0; i < img_count; i++) {
        img_t* im = &imgs[i];
        if (im->state == 2 && im->pixels && im->width > 0 && im->height > 0) {
            int iy = content_y + 10 - scroll_y;
            int dw = im->width, dh = im->height;
            if (dw > (int)cur_w - 32) {
                dh = dh * (cur_w - 32) / dw;
                dw = cur_w - 32;
            }
            if (iy + dh >= content_y && iy < content_y + content_h_avail) {
                /* Render scaled RGB24 image */
                for (int dy = 0; dy < dh; dy++) {
                    int sy = dy * im->height / dh;
                    int py = iy + dy;
                    if (py < content_y || py >= content_y + content_h_avail) continue;
                    for (int dx = 0; dx < dw; dx++) {
                        int sx = dx * im->width / dw;
                        int px = 16 + dx;
                        if (px < 0 || px >= (int)cur_w) continue;
                        int sidx = (sy * im->width + sx) * 3;
                        uint32_t col = ((uint32_t)im->pixels[sidx] << 16) |
                                       ((uint32_t)im->pixels[sidx + 1] << 8) |
                                       im->pixels[sidx + 2];
                        win_fb[py * cur_w + px] = col;
                    }
                }
            }
        }
    }

    /* Render Lines of Text */
    for (int ln = 0; ln < num_lines; ln++) {
        int ly = content_y + line_y_doc[ln] - scroll_y;
        if (ly + FONT_H * line_scale[ln] < content_y) continue;
        if (ly > content_y + content_h_avail) break;

        int s = line_start[ln];
        int len = line_len_arr[ln];
        char line_buf[256];
        if (len > 255) len = 255;
        memcpy(line_buf, body_txt + s, len);
        line_buf[len] = 0;

        uint32_t fg = reader_mode ? 0x24292F : 0xE6EDF3;
        eigen_draw_text(win_fb, cur_w, cur_h, 16, ly, line_buf, fg);
    }

    /* 4. Bottom Status Bar */
    int foot_y = cur_h - FOOTER_H;
    eigen_draw_fillrect(win_fb, cur_w, cur_h, 0, foot_y, cur_w, FOOTER_H, chrome_bg);
    eigen_draw_fillrect(win_fb, cur_w, cur_h, 0, foot_y, cur_w, 1, border_clr);

    char stat[160];
    if (bw_loading) {
        snprintf(stat, sizeof(stat), "Connecting...");
    } else if (fetch_error[0]) {
        snprintf(stat, sizeof(stat), "Error: %s", fetch_error);
    } else if (status_code > 0) {
        snprintf(stat, sizeof(stat), "HTTP %d | %d bytes | %ums (TTFB %ums)",
                 status_code, raw_len, fetch_time_ms, ttfb_ms);
    } else {
        snprintf(stat, sizeof(stat), "Ready — type a URL and press Enter");
    }
    eigen_draw_text(win_fb, cur_w, cur_h, 12, foot_y + 4, stat, dim_clr);

    eigen_win_flush(win_id);
}

/* ---- Main Event Loop ---- */
int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

    raw_buf = (char*)malloc(RAW_MAX);
    body_txt = (char*)malloc(BODY_TXT_MAX);
    line_start = (int*)malloc(MAX_LINES * sizeof(int));
    line_len_arr = (int*)malloc(MAX_LINES * sizeof(int));
    line_y_doc = (int*)malloc(MAX_LINES * sizeof(int));
    line_scale = (int*)malloc(MAX_LINES * sizeof(int));
    match_pos = (int*)malloc(MAX_LINES * 8 * sizeof(int));
    runs = (run_t*)malloc(MAX_RUNS * sizeof(run_t));
    links = (link_t*)malloc(MAX_LINKS * sizeof(link_t));
    css_rules = (css_rule_t*)malloc(MAX_CSS * sizeof(css_rule_t));

    if (!raw_buf || !body_txt || !line_start || !runs) {
        eigen_puts("Edrowser: Memory allocation failure\n");
        return 1;
    }

    win_id = eigen_win_create(60, 40, WIN_W, WIN_H, "Edrowser");
    if (win_id < 0) return 1;

    win_fb = (uint32_t*)eigen_win_map(win_id);
    eigen_win_getsize(win_id, &cur_w, &cur_h);

    /* Initial fetch */
    bw_fetch_url(url_buf);

    eigen_ev_t evs[MAX_EVS];
    int running = 1;

    while (running) {
        int n = eigen_win_poll(win_id, evs, MAX_EVS);
        int redraw = 0;

        for (int i = 0; i < n; i++) {
            eigen_ev_t* ev = &evs[i];

            if (ev->type == EIGEN_EV_CLOSE) {
                running = 0;
                break;
            }

            if (ev->type == EIGEN_EV_MDOWN) {
                int mx = ev->a, my = ev->b;

                /* Address bar click */
                if (my >= 7 && my <= 35 && mx >= 108 && mx <= (int)cur_w - 152) {
                    editing_url = 1;
                    strncpy(url_edit_text, url_buf, 511);
                    url_edit_len = strlen(url_edit_text);
                    redraw = 1;
                } else {
                    if (editing_url) { editing_url = 0; redraw = 1; }
                }

                /* Back Button */
                if (mx >= 8 && mx <= 36 && my >= 7 && my <= 35) {
                    if (history_idx > 0) {
                        history_idx--;
                        strncpy(url_buf, history[history_idx], 511);
                        bw_fetch_url(url_buf);
                    }
                }
                /* Forward Button */
                if (mx >= 40 && mx <= 68 && my >= 7 && my <= 35) {
                    if (history_idx < history_count - 1) {
                        history_idx++;
                        strncpy(url_buf, history[history_idx], 511);
                        bw_fetch_url(url_buf);
                    }
                }
                /* Refresh Button */
                if (mx >= 72 && mx <= 100 && my >= 7 && my <= 35) {
                    bw_fetch_url(url_buf);
                }
                /* Reader Mode Toggle */
                if (mx >= (int)cur_w - 144 && mx <= (int)cur_w - 116 && my >= 7 && my <= 35) {
                    reader_mode = !reader_mode;
                    int cpl = (cur_w - 24) / (FONT_W * (reader_mode ? 2 : zoom_level));
                    if (cpl < 1) cpl = 1;
                    recompute_lines(cpl);
                    redraw = 1;
                }
                /* Zoom - */
                if (mx >= (int)cur_w - 112 && mx <= (int)cur_w - 84 && my >= 7 && my <= 35) {
                    if (zoom_level > 1) { zoom_level--; redraw = 1; }
                }
                /* Zoom + */
                if (mx >= (int)cur_w - 80 && mx <= (int)cur_w - 52 && my >= 7 && my <= 35) {
                    if (zoom_level < 3) { zoom_level++; redraw = 1; }
                }
            }

            if (ev->type == EIGEN_EV_KEY) {
                if (ev->a >= 0x100 || (ev->a & 0x100)) continue;
                char k = (char)ev->a;
                int code = ev->b;

                if (editing_url) {
                    if (k == '\n' || k == '\r' || code == 0x1C) {
                        editing_url = 0;
                        if (url_edit_len > 0) {
                            strncpy(url_buf, url_edit_text, 511);
                            bw_fetch_url(url_buf);
                        }
                    } else if (k == 8 || code == 0x0E) { /* Backspace */
                        if (url_edit_len > 0) url_edit_text[--url_edit_len] = 0;
                        redraw = 1;
                    } else if (k >= 32 && k < 127 && url_edit_len < 500) {
                        url_edit_text[url_edit_len++] = k;
                        url_edit_text[url_edit_len] = 0;
                        redraw = 1;
                    }
                } else {
                    /* Scrolling */
                    if (code == 0x48) { /* Up */
                        scroll_y -= 32; if (scroll_y < 0) scroll_y = 0;
                        redraw = 1;
                    } else if (code == 0x50) { /* Down */
                        scroll_y += 32; if (scroll_y > content_h - 100) scroll_y = content_h - 100;
                        redraw = 1;
                    } else if (code == 0x49) { /* PgUp */
                        scroll_y -= 200; if (scroll_y < 0) scroll_y = 0;
                        redraw = 1;
                    } else if (code == 0x51) { /* PgDn */
                        scroll_y += 200; if (scroll_y > content_h - 100) scroll_y = content_h - 100;
                        redraw = 1;
                    }
                }
            }
        }

        if (redraw || bw_loading || ((eigen_gettime_ms() / 500) % 2)) {
            render_all();
        }

        eigen_sleep_ms(16);
    }

    eigen_win_close(win_id);
    return 0;
}
