/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

/* kilo.c — ring-3 port of kilo, the minimal text editor.
 *
 * kilo (https://github.com/antirez/kilo, public domain) is ~1000 lines
 * of C. This port keeps the editor core untouched (rows, cursor, undo of
 * nothing, search, syntax highlighting) but replaces the terminal layer:
 *   - termios raw mode        -> ring-3 window key events
 *   - ANSI escape sequences   -> direct framebuffer drawing (font8x16)
 *   - stdout writes           -> window buffer + eigen_win_flush
 *   - open()/read()/write()   -> eigen FS one-shot helpers
 *
 * Keys: kernel codes arrive as ASCII (ctrl+letter = 1-26, '\n', '\b',
 * ESC=27, TAB=9) plus nav keys (128-131 arrows, 133/134 pg, 137/138
 * home/end, 139 delete); releases carry the 0x100 marker and are ignored.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

#include "userlib.h"

#define KILO_VERSION "0.0.1"

#define CTRL_KEY(k) ((k) & 0x1f)
#define CTRL_Q 17
#define CTRL_S 19
#define CTRL_F 6
#define CTRL_G 7
#define CTRL_L 12
#define CTRL_U 21
#define CTRL_O 15
#define CTRL_H 8
#define CTRL_C 3
#define CTRL_D 4
#define CTRL_E 5
#define CTRL_K 11
#define CTRL_P 16
#define CTRL_R 18
#define CTRL_T 20
#define CTRL_V 22
#define CTRL_W 23
#define CTRL_X 24
#define CTRL_Y 25
#define CTRL_Z 26

enum editorKey {
    BACKSPACE = 8,
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN
};

enum editorHighlight {
    HL_NORMAL = 0,
    HL_COMMENT,
    HL_MLCOMMENT,
    HL_KEYWORD1,
    HL_KEYWORD2,
    HL_STRING,
    HL_NUMBER,
    HL_MATCH
};

#define HL_HIGHLIGHT_NUMBERS (1<<0)
#define HL_HIGHLIGHT_STRINGS (1<<1)

struct editorSyntax {
    char *filetype;
    char **filematch;
    char **keywords;
    char *singleline_comment_start;
    char *multiline_comment_start;
    char *multiline_comment_end;
    int flags;
};

typedef struct erow {
    int idx;
    int size;
    int rsize;
    char *chars;
    char *render;
    unsigned char *hl;
    int hl_oc;
} erow;

struct editorConfig {
    int cx, cy;
    int rx;
    int rowoff;
    int coloff;
    int screenrows;
    int screencols;
    int numrows;
    erow *row;
    int dirty;
    char *filename;
    char statusmsg[80];
    long statusmsg_time;
    struct editorSyntax *syntax;

    /* ── ring-3 window ─────────────────────────────────────────── */
    int win_id;
    uint32_t *winbuf;
    int w, h;
    uint32_t col_bg, col_fg, col_dim, col_accent, col_on_accent;
    uint32_t col_keyword, col_string, col_number, col_comment;

    /* editorPrompt state */
    int prompt_active;
    char prompt_text[128];
    int prompt_cursor;
};

static struct editorConfig E;

/* forward declarations */
void editorSetStatusMessage(const char *fmt, ...);
void editorRefreshScreen(void);
int editorNextKey(void);
void editorPrompt(char *prompt, void (*callback)(char *, int));
void editorOpen(char *filename);
void editorSave(void);
int is_separator(int c);
void editorUpdateSyntax(erow *row);
void editorScroll(void);
void editorOpenCallback(char *path, int key);
void editorSaveAsCallback(char *path, int key);
void editorGoToCallback(char *num, int key);

/* ── Filetypes ─────────────────────────────────────────────────────────── */

char *C_HL_extensions[] = {".c", ".h", ".cpp", ".hpp", ".cc", NULL};
char *C_HL_keywords[] = {
    "switch", "if", "while", "for", "break", "continue", "return", "else",
    "struct", "union", "typedef", "static", "enum", "class", "case",
    "int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|",
    "void|", NULL
};

struct editorSyntax HLDB[] = {
    {
        "c",
        C_HL_extensions,
        C_HL_keywords,
        "//", "/*", "*/",
        HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS
    },
};

#define HLDB_ENTRIES (sizeof(HLDB)/sizeof(HLDB[0]))

/* ── Syntax highlighting ───────────────────────────────────────────────── */

int editorSyntaxToColor(int hl) {
    switch (hl) {
        case HL_COMMENT: case HL_MLCOMMENT: return 1;
        case HL_KEYWORD1: return 2;
        case HL_KEYWORD2: return 3;
        case HL_STRING: return 4;
        case HL_NUMBER: return 5;
        case HL_MATCH: return 6;
        default: return 0;
    }
}

static uint32_t editorHlColor(int hl) {
    switch (hl) {
        case HL_COMMENT: case HL_MLCOMMENT: return E.col_comment;
        case HL_KEYWORD1: case HL_KEYWORD2: return E.col_keyword;
        case HL_STRING: return E.col_string;
        case HL_NUMBER: return E.col_number;
        case HL_MATCH: return E.col_accent;
        default: return E.col_fg;
    }
}

int editorSelectSyntaxHighlight(void) {
    E.syntax = NULL;
    if (E.filename == NULL) return 0;
    char *ext = strrchr(E.filename, '.');
    for (unsigned int j = 0; j < HLDB_ENTRIES; j++) {
        struct editorSyntax *s = &HLDB[j];
        unsigned int i = 0;
        while (s->filematch[i]) {
            int is_ext = (s->filematch[i][0] == '.');
            if ((is_ext && ext && strcmp(ext, s->filematch[i]) == 0) ||
                (!is_ext && strstr(E.filename, s->filematch[i]))) {
                E.syntax = s;
                return 1;
            }
            i++;
        }
    }
    return 0;
}

/* ── Row operations ────────────────────────────────────────────────────── */

int editorRowCxToRx(erow *row, int cx) {
    int rx = 0;
    for (int j = 0; j < cx; j++) {
        if (row->chars[j] == '\t') rx += (8 - 1) - (rx % 8);
        rx++;
    }
    return rx;
}

int editorRowRxToCx(erow *row, int rx) {
    int cur_rx = 0;
    int cx;
    for (cx = 0; cx < row->size; cx++) {
        if (row->chars[cx] == '\t') cur_rx += (8 - 1) - (cur_rx % 8);
        cur_rx++;
        if (cur_rx > rx) return cx;
    }
    return cx;
}

void editorUpdateRow(erow *row) {
    int tabs = 0;
    for (int j = 0; j < row->size; j++)
        if (row->chars[j] == '\t') tabs++;

    free(row->render);
    row->render = malloc(row->size + tabs * 7 + 1);

    int idx = 0;
    for (int j = 0; j < row->size; j++) {
        if (row->chars[j] == '\t') {
            row->render[idx++] = ' ';
            while (idx % 8 != 0) row->render[idx++] = ' ';
        } else {
            row->render[idx++] = row->chars[j];
        }
    }
    row->render[idx] = '\0';
    row->rsize = idx;

    editorUpdateSyntax(row);
}

void editorUpdateSyntax(erow *row) {
    row->hl = realloc(row->hl, row->rsize);
    memset(row->hl, HL_NORMAL, row->rsize);

    if (E.syntax == NULL) return;

    char **keywords = E.syntax->keywords;
    char *scs = E.syntax->singleline_comment_start;
    char *mcs = E.syntax->multiline_comment_start;
    char *mce = E.syntax->multiline_comment_end;

    int scs_len = scs ? strlen(scs) : 0;
    int mcs_len = mcs ? strlen(mcs) : 0;
    int mce_len = mce ? strlen(mce) : 0;

    int prev_sep = 1;
    int in_string = 0;
    int in_comment = (row->idx > 0 && E.row[row->idx - 1].hl_oc);

    int i = 0;
    while (i < row->rsize) {
        char c = row->render[i];
        unsigned char prev_hl = (i > 0) ? row->hl[i - 1] : HL_NORMAL;

        if (scs_len && !in_string && !in_comment) {
            if (!strncmp(&row->render[i], scs, scs_len)) {
                memset(&row->hl[i], HL_COMMENT, row->rsize - i);
                break;
            }
        }

        if (mcs_len && mce_len && !in_string) {
            if (in_comment) {
                row->hl[i] = HL_MLCOMMENT;
                if (!strncmp(&row->render[i], mce, mce_len)) {
                    memset(&row->hl[i], HL_MLCOMMENT, mce_len);
                    i += mce_len;
                    in_comment = 0;
                    prev_sep = 1;
                    continue;
                } else {
                    i++;
                    continue;
                }
            } else if (!strncmp(&row->render[i], mcs, mcs_len)) {
                memset(&row->hl[i], HL_MLCOMMENT, mcs_len);
                i += mcs_len;
                in_comment = 1;
                continue;
            }
        }

        if (E.syntax->flags & HL_HIGHLIGHT_STRINGS) {
            if (in_string) {
                row->hl[i] = HL_STRING;
                if (c == '\\' && i + 1 < row->rsize) {
                    row->hl[i + 1] = HL_STRING;
                    i += 2;
                    continue;
                }
                if (c == in_string) in_string = 0;
                i++;
                prev_sep = 1;
                continue;
            } else {
                if (c == '"' || c == '\'') {
                    in_string = c;
                    row->hl[i] = HL_STRING;
                    i++;
                    continue;
                }
            }
        }

        if (E.syntax->flags & HL_HIGHLIGHT_NUMBERS) {
            if ((isdigit(c) && (prev_sep || prev_hl == HL_NUMBER)) ||
                (c == '.' && prev_hl == HL_NUMBER)) {
                row->hl[i] = HL_NUMBER;
                i++;
                prev_sep = 0;
                continue;
            }
        }

        if (prev_sep) {
            int j;
            for (j = 0; keywords[j]; j++) {
                int klen = strlen(keywords[j]);
                int kw2 = keywords[j][klen - 1] == '|';
                if (kw2) klen--;

                if (!strncmp(&row->render[i], keywords[j], klen) &&
                    is_separator(row->render[i + klen])) {
                    memset(&row->hl[i], kw2 ? HL_KEYWORD2 : HL_KEYWORD1, klen);
                    i += klen;
                    break;
                }
            }
            if (keywords[j] != NULL) {
                prev_sep = 0;
                continue;
            }
        }

        prev_sep = is_separator(c);
        i++;
    }

    int changed = (in_comment != row->hl_oc);
    row->hl_oc = in_comment;
    if (changed && row->idx + 1 < E.numrows)
        editorUpdateSyntax(&E.row[row->idx + 1]);
}

int is_separator(int c) {
    return c == '\0' || isspace(c) || strchr(",.()+-/*=~%<>;[]", c) != NULL;
}

void editorInsertRow(int at, char *s, size_t len) {
    if (at < 0 || at > E.numrows) return;
    E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
    memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numrows - at));
    for (int j = at + 1; j <= E.numrows; j++) E.row[j].idx++;

    E.row[at].idx = at;
    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';
    E.row[at].rsize = 0;
    E.row[at].render = NULL;
    E.row[at].hl = NULL;
    E.row[at].hl_oc = 0;
    editorUpdateRow(&E.row[at]);
    E.numrows++;
    E.dirty++;
}

void editorFreeRow(erow *row) {
    free(row->render);
    free(row->chars);
    free(row->hl);
}

void editorDelRow(int at) {
    if (at < 0 || at >= E.numrows) return;
    editorFreeRow(&E.row[at]);
    memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numrows - at - 1));
    for (int j = at; j < E.numrows - 1; j++) E.row[j].idx--;
    E.numrows--;
    E.dirty++;
}

void editorRowInsertChar(erow *row, int at, int c) {
    if (at < 0 || at > row->size) at = row->size;
    row->chars = realloc(row->chars, row->size + 2);
    memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
    row->size++;
    row->chars[at] = c;
    editorUpdateRow(row);
    E.dirty++;
}

void editorRowAppendString(erow *row, char *s, size_t len) {
    row->chars = realloc(row->chars, row->size + len + 1);
    memcpy(&row->chars[row->size], s, len);
    row->size += len;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
    E.dirty++;
}

void editorRowDelChar(erow *row, int at) {
    if (at < 0 || at >= row->size) return;
    memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
    row->size--;
    editorUpdateRow(row);
    E.dirty++;
}

/* ── Editor operations ─────────────────────────────────────────────────── */

void editorInsertChar(int c) {
    if (E.cy == E.numrows) {
        editorInsertRow(E.numrows, "", 0);
    }
    editorRowInsertChar(&E.row[E.cy], E.cx, c);
    E.cx++;
}

void editorInsertNewline(void) {
    if (E.cx == 0) {
        editorInsertRow(E.cy, "", 0);
    } else {
        erow *row = &E.row[E.cy];
        editorInsertRow(E.cy + 1, &row->chars[E.cx], row->size - E.cx);
        row = &E.row[E.cy];
        row->size = E.cx;
        row->chars[row->size] = '\0';
        editorUpdateRow(row);
    }
    E.cy++;
    E.cx = 0;
}

void editorDelChar(void) {
    if (E.cy == E.numrows) return;
    if (E.cx == 0 && E.cy == 0) return;
    erow *row = &E.row[E.cy];
    if (E.cx > 0) {
        editorRowDelChar(row, E.cx - 1);
        E.cx--;
    } else {
        E.cx = E.row[E.cy - 1].size;
        editorRowAppendString(&E.row[E.cy - 1], row->chars, row->size);
        editorDelRow(E.cy);
        E.cy--;
    }
}

void editorDelForward(void) {
    if (E.cy == E.numrows) return;
    erow *row = &E.row[E.cy];
    if (E.cx < row->size) {
        editorRowDelChar(row, E.cx);
    } else if (E.cy + 1 < E.numrows) {
        editorRowAppendString(row, E.row[E.cy + 1].chars, E.row[E.cy + 1].size);
        editorDelRow(E.cy + 1);
    }
}

/* ── File I/O (eigen FS) ───────────────────────────────────────────────── */

char *editorRowsToString(int *buflen) {
    int totlen = 0;
    int j;
    for (j = 0; j < E.numrows; j++)
        totlen += E.row[j].size + 1;
    *buflen = totlen;

    char *buf = malloc(totlen);
    char *p = buf;
    for (j = 0; j < E.numrows; j++) {
        memcpy(p, E.row[j].chars, E.row[j].size);
        p += E.row[j].size;
        *p = '\n';
        p++;
    }
    return buf;
}

void editorOpen(char *filename) {
    free(E.filename);
    E.filename = strdup(filename);

    editorSelectSyntaxHighlight();

    long fsize = eigen_fs_size(filename);
    if (fsize <= 0) return;            /* new file */
    if (fsize > 8 * 1024 * 1024) {
        editorSetStatusMessage("File too large (%.1f MB)", (double)fsize / 1048576.0);
        return;
    }
    char *content = malloc((size_t)fsize + 1);
    int nread = eigen_fs_read_file(filename, content, (int)fsize + 1);
    if (nread < 0) {
        editorSetStatusMessage("Could not open \"%s\"", filename);
        free(content);
        return;
    }
    content[nread] = '\0';

    char *s = content;
    char *nl = strchr(s, '\n');
    while (nl != NULL) {
        *nl = '\0';
        editorInsertRow(E.numrows, s, strlen(s));
        s = nl + 1;
        nl = strchr(s, '\n');
    }
    editorInsertRow(E.numrows, s, strlen(s));
    free(content);
    E.dirty = 0;
    editorSetStatusMessage("Opened %s (%d lines)", filename, E.numrows);
}

void editorSave(void) {
    if (E.filename == NULL) {
        editorPrompt("Save as: ", editorSaveAsCallback);
        return;
    }

    int len;
    char *buf = editorRowsToString(&len);
    int r = eigen_fs_write_file(E.filename, buf, len);
    free(buf);
    if (r < 0) {
        editorSetStatusMessage("Can't save! I/O error");
        return;
    }
    E.dirty = 0;
    editorSetStatusMessage("Wrote %d bytes to %s", len, E.filename);
}

void editorOpenCallback(char *path, int key) {
    if (key != '\r' || path[0] == '\0') return;
    editorOpen(path);
    E.cy = 0;
    E.cx = 0;
}

void editorSaveAsCallback(char *path, int key) {
    if (key != '\r' || path[0] == '\0') return;
    free(E.filename);
    E.filename = strdup(path);
    editorSelectSyntaxHighlight();
    editorSave();
}

void editorGoToCallback(char *num, int key) {
    if (key != '\r' || num[0] == '\0') return;
    int line = atoi(num);
    if (line < 1) line = 1;
    if (line > E.numrows) line = E.numrows;
    E.cy = line - 1;
    E.cx = 0;
}

/* ── Find ──────────────────────────────────────────────────────────────── */

void editorFindCallback(char *query, int key) {
    static int last_match = -1;
    static int direction = 1;

    if (key == '\r' || key == 27) {
        last_match = -1;
        direction = 1;
        return;
    } else if (key == ARROW_RIGHT || key == ARROW_DOWN) {
        direction = 1;
    } else if (key == ARROW_LEFT || key == ARROW_UP) {
        direction = -1;
    } else {
        last_match = -1;
        direction = 1;
    }

    if (last_match == -1) direction = 1;
    int current = last_match;
    for (int i = 0; i < E.numrows; i++) {
        current += direction;
        if (current == -1) current = E.numrows - 1;
        else if (current == E.numrows) current = 0;
        erow *row = &E.row[current];
        char *match = strstr(row->render, query);
        if (match) {
            last_match = current;
            E.cy = current;
            E.cx = editorRowRxToCx(row, match - row->render);
            E.rowoff = E.numrows;
            break;
        }
    }
}

void editorSearch(void) {
    if (E.numrows == 0) return;
    editorPrompt("Search: %s (Use ESC/Arrows/Enter)", editorFindCallback);
}

/* ── Prompt ────────────────────────────────────────────────────────────── */

void editorPrompt(char *prompt, void (*callback)(char *, int)) {
    int bufsize = 128;
    char *buf = malloc(bufsize);
    buf[0] = '\0';
    int buflen = 0;

    E.prompt_active = 1;
    snprintf(E.prompt_text, sizeof(E.prompt_text), "%s", prompt);
    E.prompt_cursor = 0;

    while (1) {
        editorSetStatusMessage("%s%s", E.prompt_text, buf);
        editorRefreshScreen();

        int key = editorNextKey();
        if (key == -1) {                /* window closed */
            free(buf);
            E.prompt_active = 0;
            return;
        }
        if (key == 27) {                /* ESC: cancel */
            editorSetStatusMessage("");
            break;
        }
        if (key == '\r') {              /* Enter: confirm */
            if (callback != NULL) {
                editorSetStatusMessage("");
                callback(buf, key);
            }
            break;
        }
        if (key == 8 || key == 127) {   /* backspace */
            if (buflen > 0 && E.prompt_cursor > 0) {
                memmove(&buf[E.prompt_cursor - 1], &buf[E.prompt_cursor],
                        buflen - E.prompt_cursor + 1);
                buflen--;
                E.prompt_cursor--;
            }
            continue;
        }
        if (key == ARROW_LEFT) {
            if (E.prompt_cursor > 0) E.prompt_cursor--;
            continue;
        }
        if (key == ARROW_RIGHT) {
            if (E.prompt_cursor < buflen) E.prompt_cursor++;
            continue;
        }
        if (key >= 32 && key < 128) {   /* printable */
            if (buflen < bufsize - 1) {
                memmove(&buf[E.prompt_cursor + 1], &buf[E.prompt_cursor],
                        buflen - E.prompt_cursor + 1);
                buf[E.prompt_cursor++] = key;
                buflen++;
            }
            continue;
        }
    }

    free(buf);
    E.prompt_active = 0;
}

/* ── Key input (ring-3 window events) ──────────────────────────────────── */

/* Returns the next editor key, or -1 if the user closed the window.
   Kernel key codes: ASCII chars (ctrl+letter = 1-26, '\n'=10, '\b'=8,
   ESC=27, TAB=9), nav keys (128-131 arrows, 133/134 pgup/pgdn,
   137/138 home/end, 139 delete). Releases carry the 0x100 marker. */
int editorNextKey(void) {
    eigen_ev_t ev;
    while (1) {
        int n = eigen_win_poll(E.win_id, &ev, 1);
        if (n > 0) {
            if (ev.type == EIGEN_EV_CLOSE) return -1;
            if (ev.type != EIGEN_EV_KEY) continue;
            int k = ev.a;
            if (k & 0x100) continue;    /* key release */
            int c = k & 0xFF;
            switch (c) {
                case '\n': return '\r';
                case '\b': return BACKSPACE;
                case 127:  return BACKSPACE;
                case 27:   return 27;    /* ESC */
                case 9:    return 9;     /* TAB */
                case 128:  return ARROW_UP;
                case 129:  return ARROW_DOWN;
                case 130:  return ARROW_LEFT;
                case 131:  return ARROW_RIGHT;
                case 139:  return DEL_KEY;
                case 137:  return HOME_KEY;
                case 138:  return END_KEY;
                case 133:  return PAGE_UP;
                case 134:  return PAGE_DOWN;
                default:
                    if (c >= 1 && c <= 26) return c;   /* ctrl+letter */
                    return c;
            }
        }
        eigen_sleep_ms(2);
    }
}

/* ── Rendering (window framebuffer, font8x16) ──────────────────────────── */

#define FONT_W 8
#define FONT_H 16

void editorDrawRowText(int y, int coloff, erow *row) {
    int rlen = row->rsize - coloff;
    if (rlen < 0) rlen = 0;
    if (rlen > E.screencols) rlen = E.screencols;
    int x = 0;
    while (x < rlen) {
        unsigned char hl = (row->hl != NULL) ? row->hl[x + coloff] : HL_NORMAL;
        uint32_t col = editorHlColor(hl);
        int run = 1;
        while (x + run < rlen &&
               (row->hl != NULL ? row->hl[x + coloff + run] : HL_NORMAL) == hl)
            run++;
        char tmp[129];
        int i;
        for (i = 0; i < run && i < 128; i++)
            tmp[i] = row->render[x + coloff + i];
        tmp[i] = '\0';
        eigen_draw_text(E.winbuf, E.w, E.h, x * FONT_W, y * FONT_H, tmp, col);
        x += run;
    }
}

void editorDrawRows(void) {
    for (int y = 0; y < E.screenrows; y++) {
        int filerow = y + E.rowoff;
        if (filerow >= E.numrows) {
            char hint[128];
            if (E.numrows == 0 && filerow == E.screenrows / 3) {
                snprintf(hint, sizeof(hint),
                         "Kilo v%s - Ctrl-O open | Ctrl-S save | Ctrl-Q quit | Ctrl-F find",
                         KILO_VERSION);
                int pad = (E.screencols - (int)strlen(hint)) / 2;
                if (pad < 0) pad = 0;
                char line[160];
                int hlen = (int)strlen(hint);
                int i;
                memset(line, ' ', (size_t)pad);
                for (i = 0; i < hlen; i++)
                    line[pad + i] = hint[i];
                line[pad + hlen] = '\0';
                eigen_draw_text(E.winbuf, E.w, E.h, 0, y * FONT_H, line, E.col_dim);
            } else {
                eigen_draw_text(E.winbuf, E.w, E.h, 0, y * FONT_H, "~", E.col_dim);
            }
        } else {
            editorDrawRowText(y, E.coloff, &E.row[filerow]);
        }
    }
}

void editorDrawStatusBar(void) {
    char status[160], rstatus[160];
    int len = snprintf(status, sizeof(status), " %.20s - %d lines %s",
                       E.filename ? E.filename : "[No Name]",
                       E.numrows,
                       E.dirty ? "(modified)" : "");
    int rlen = snprintf(rstatus, sizeof(rstatus), "%s | Ctrl-S save | Ctrl-Q quit ",
                        E.syntax ? E.syntax->filetype : "text");
    if (len > E.screencols) len = E.screencols;
    if (rlen > E.screencols) rlen = E.screencols;
    if (len + rlen > E.screencols) len = E.screencols - rlen;

    int y = E.screenrows * FONT_H;
    eigen_draw_fillrect(E.winbuf, E.w, E.h, 0, y, E.w, FONT_H, E.col_accent);
    eigen_draw_text(E.winbuf, E.w, E.h, 0, y, status, E.col_on_accent);
    eigen_draw_text(E.winbuf, E.w, E.h, (E.screencols - rlen) * FONT_W, y,
                    rstatus, E.col_on_accent);
}

void editorDrawMessageBar(void) {
    int y = (E.screenrows + 1) * FONT_H;
    if (E.prompt_active) {
        /* invert the prompt line */
        char line[160];
        snprintf(line, sizeof(line), " %s%s", E.prompt_text, E.statusmsg);
        int len = (int)strlen(line);
        if (len > E.screencols) len = E.screencols;
        char tmp[160];
        int i;
        for (i = 0; i < len; i++) tmp[i] = line[i];
        tmp[i] = '\0';
        eigen_draw_fillrect(E.winbuf, E.w, E.h, 0, y, E.w, FONT_H, E.col_fg);
        eigen_draw_text(E.winbuf, E.w, E.h, 0, y, tmp, E.col_bg);
        /* cursor inside prompt */
        int cx = (1 + (int)strlen(E.prompt_text) + E.prompt_cursor) * FONT_W;
        eigen_draw_fillrect(E.winbuf, E.w, E.h, cx, y, 1, FONT_H, E.col_accent);
        return;
    }
    if (E.statusmsg_time &&
        (long)eigen_gettime_ms() - E.statusmsg_time < 5000) {
        eigen_draw_text(E.winbuf, E.w, E.h, 0, y, E.statusmsg, E.col_dim);
    }
}

void editorRefreshScreen(void) {
    editorScroll();
    eigen_draw_fillrect(E.winbuf, E.w, E.h, 0, 0, E.w, E.h, E.col_bg);
    editorDrawRows();
    editorDrawStatusBar();
    editorDrawMessageBar();

    /* cursor: draw the cell inverted */
    int rx = 0;
    if (E.cy < E.numrows)
        rx = editorRowCxToRx(&E.row[E.cy], E.cx);
    else
        rx = 0;
    int cx = rx - E.coloff;
    int cy = E.cy - E.rowoff;
    if (cx >= 0 && cy >= 0 && cx < E.screencols && cy < E.screenrows) {
        int px = cx * FONT_W;
        int py = cy * FONT_H;
        uint32_t fg = E.col_fg;
        char ch = ' ';
        if (E.cy < E.numrows && cx + E.coloff < E.row[E.cy].rsize) {
            int hl = (E.row[E.cy].hl != NULL)
                         ? E.row[E.cy].hl[cx + E.coloff] : HL_NORMAL;
            ch = E.row[E.cy].render[cx + E.coloff];
            fg = editorHlColor(hl);
        }
        eigen_draw_fillrect(E.winbuf, E.w, E.h, px, py, FONT_W, FONT_H, fg);
        char tmp[2] = { ch, 0 };
        eigen_draw_text(E.winbuf, E.w, E.h, px, py, tmp, E.col_bg);
    }
    eigen_win_flush(E.win_id);
}

void editorScroll(void) {
    E.rx = 0;
    if (E.cy < E.numrows)
        E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);

    if (E.cy < E.rowoff) E.rowoff = E.cy;
    if (E.cy >= E.rowoff + E.screenrows) E.rowoff = E.cy - E.screenrows + 1;
    if (E.rx < E.coloff) E.coloff = E.rx;
    if (E.rx >= E.coloff + E.screencols) E.coloff = E.rx - E.screencols + 1;
}

/* ── Status message ────────────────────────────────────────────────────── */

void editorSetStatusMessage(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
    va_end(ap);
    E.statusmsg_time = (long)eigen_gettime_ms();
}

/* ── Key processing ────────────────────────────────────────────────────── */

void editorMoveCursor(int key) {
    erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
    switch (key) {
        case ARROW_LEFT:
            if (E.cx != 0) E.cx--;
            else if (E.cy > 0) {
                E.cy--;
                E.cx = E.row[E.cy].size;
            }
            break;
        case ARROW_RIGHT:
            if (row && E.cx < row->size) E.cx++;
            else if (row && E.cx == row->size) {
                E.cy++;
                E.cx = 0;
            }
            break;
        case ARROW_UP:
            if (E.cy != 0) E.cy--;
            break;
        case ARROW_DOWN:
            if (E.cy < E.numrows) E.cy++;
            break;
        case PAGE_UP:
        case PAGE_DOWN: {
            int times = E.screenrows;
            while (times--)
                editorMoveCursor(key == PAGE_UP ? ARROW_UP : ARROW_DOWN);
            break;
        }
        case HOME_KEY:
            E.cx = 0;
            break;
        case END_KEY:
            if (E.cy < E.numrows) E.cx = E.row[E.cy].size;
            break;
    }

    row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
    int rowlen = row ? row->size : 0;
    if (E.cx > rowlen) E.cx = rowlen;
}

void editorProcessKeypress(int key) {
    static int quit_times = 0;

    if (key == -1) {             /* window close button */
        if (E.dirty && quit_times < 1) {
            editorSetStatusMessage("WARNING! File has unsaved changes. "
                                   "Press Ctrl-Q again to quit");
            quit_times = 1;
            return;
        }
        eigen_win_close(E.win_id);
        exit(0);
    }

    switch (key) {
        case '\r':
            editorInsertNewline();
            break;
        case CTRL_Q: {
            if (E.dirty && quit_times < 1) {
                editorSetStatusMessage("WARNING! File has unsaved changes. "
                                       "Press Ctrl-Q again to quit");
                quit_times = 1;
                return;
            }
            eigen_win_close(E.win_id);
            exit(0);
        }
        case CTRL_S:
            editorSave();
            break;
        case CTRL_F:
            editorSearch();
            break;
        case CTRL_G:
            editorPrompt("Goto line: ", editorGoToCallback);
            break;
        case CTRL_L:
            /* refresh only */
            break;
        case CTRL_U:
            /* scroll up half a screen */
            E.rowoff -= E.screenrows / 2;
            break;
        case CTRL_O:
            editorPrompt("Open file: ", editorOpenCallback);
            break;
        case CTRL_H:
        case 127:
            editorDelChar();
            break;
        case DEL_KEY:
            editorDelForward();
            break;
        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
        case PAGE_UP:
        case PAGE_DOWN:
        case HOME_KEY:
        case END_KEY:
            editorMoveCursor(key);
            break;
        case 27:   /* ESC */
            break;
        case 9:    /* TAB */
            editorInsertChar('\t');
            break;
        case CTRL_C:   /* ignore stray ctrl combos */
        case CTRL_D:
        case CTRL_E:
        case CTRL_K:
        case CTRL_P:
        case CTRL_R:
        case CTRL_T:
        case CTRL_V:
        case CTRL_W:
        case CTRL_X:
        case CTRL_Y:
        case CTRL_Z:
            break;
        default:
            if (key >= 32 && key < 128)
                editorInsertChar(key);
            break;
    }
    quit_times = 0;
}

/* ── Init & main ───────────────────────────────────────────────────────── */

void editorInit(void) {
    E.cx = 0;
    E.cy = 0;
    E.rx = 0;
    E.rowoff = 0;
    E.coloff = 0;
    E.numrows = 0;
    E.row = NULL;
    E.dirty = 0;
    E.filename = NULL;
    E.statusmsg[0] = '\0';
    E.statusmsg_time = 0;
    E.syntax = NULL;
    E.prompt_active = 0;
    E.prompt_cursor = 0;
    E.prompt_text[0] = '\0';

    uint32_t theme[EIGEN_THEME_COUNT];
    int n = eigen_win_gettheme(theme, EIGEN_THEME_COUNT);
    if (n > 0) {
        E.col_bg        = theme[EIGEN_THEME_BG];
        E.col_fg        = theme[EIGEN_THEME_PRIMARY];
        E.col_dim       = theme[EIGEN_THEME_SECONDARY];
        E.col_accent    = theme[EIGEN_THEME_ACCENT];
        E.col_on_accent = theme[EIGEN_THEME_ON_PRIMARY];
    } else {
        E.col_bg        = 0x1A1B26;
        E.col_fg        = 0xC0CAF5;
        E.col_dim       = 0x565F89;
        E.col_accent    = 0x7AA2F7;
        E.col_on_accent = 0x1A1B26;
    }
    E.col_keyword = 0xFF9E64;    /* orange */
    E.col_string  = 0x9ECE6A;    /* green  */
    E.col_number  = 0xE0AF68;    /* yellow */
    E.col_comment = 0x565F89;    /* grey   */

    E.win_id = eigen_win_create(90, 50, 880, 640, "Kilo Text Editor");
    if (E.win_id < 0) {
        exit(1);
    }
    E.winbuf = (uint32_t *)eigen_win_map(E.win_id);
    uint32_t cw = 0, ch = 0;
    eigen_win_getsize(E.win_id, &cw, &ch);
    E.w = (int)cw;
    E.h = (int)ch;
    if (E.w < 80) E.w = 80;
    if (E.h < 48) E.h = 48;
    E.screencols = E.w / FONT_W;
    E.screenrows = E.h / FONT_H - 2;
    if (E.screenrows < 2) E.screenrows = 2;
}

int main(void) {
    editorInit();

    /* optional: open a file already present in the filesystem */
    if (eigen_fs_exists("/kilo_open.txt") > 0) {
        char req[512];
        int n = eigen_fs_read_file("/kilo_open.txt", req, 511);
        if (n > 0) {
            req[n] = 0;
            while (n > 0 && (req[n-1] == '\n' || req[n-1] == '\r')) req[--n] = 0;
            if (req[0]) editorOpen(req);
        }
        eigen_fs_delete("/kilo_open.txt");
    } else if (eigen_fs_exists("kilo.txt") > 0)
        editorOpen("kilo.txt");

    editorSetStatusMessage("HELP: Ctrl-S = save | Ctrl-Q = quit | "
                           "Ctrl-F = find | Ctrl-O = open | Ctrl-G = goto");

    while (1) {
        editorRefreshScreen();
        int key = editorNextKey();
        if (key == -1) break;      /* window closed: exit the editor */
        editorProcessKeypress(key);
    }
    return 0;
}