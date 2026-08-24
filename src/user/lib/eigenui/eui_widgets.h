/***************************************************************/
/*  EigenUI Widgets — Label / Button / Toggle / Slider / Entry /  */
/*  List / Separator / Icon / Progress (Elementary-like)          */
/*  All draw with the canvas + theme + FreeType + icons.          */
/***************************************************************/
#ifndef EUI_WIDGETS_H
#define EUI_WIDGETS_H

#include "eui_widget.h"
#include "eui_icons.h"

/* ── Label ───────────────────────────────────────────────────── */
/* align: 0 left, 1 centre, 2 right */
eui_widget* eui_label_new(const char* text);
void  eui_label_set_text(eui_widget* w, const char* text);
void  eui_label_set_color(eui_widget* w, eui_color c);
void  eui_label_set_align(eui_widget* w, int align);

/* ── Button ──────────────────────────────────────────────────── */
eui_widget* eui_button_new(const char* label);
void  eui_button_set_icon(eui_widget* w, eui_icon_id id);
void  eui_button_set_label(eui_widget* w, const char* label);

/* ── Toggle (style: 0 = switch, 1 = check box) ──────────────── */
eui_widget* eui_toggle_new(int style, const char* text);
int   eui_toggle_get(eui_widget* w);
void  eui_toggle_set(eui_widget* w, int on);

/* ── Slider ──────────────────────────────────────────────────── */
eui_widget* eui_slider_new(int min, int max, int value);
int   eui_slider_get(eui_widget* w);
void  eui_slider_set(eui_widget* w, int value);

/* ── Entry (single-line text input) ──────────────────────────── */
eui_widget* eui_entry_new(void);
void  eui_entry_get(eui_widget* w, char* buf, int max);
void  eui_entry_set(eui_widget* w, const char* text);
void  eui_entry_set_secret(eui_widget* w, int on);   /* mask with '*' */

/* ── List (selectable rows; pairs well with ScrollView) ──────── */
eui_widget* eui_list_new(void);
void  eui_list_add(eui_widget* w, const char* text);
void  eui_list_clear(eui_widget* w);
int   eui_list_get_sel(eui_widget* w);               /* -1 = none */

/* ── Separator ───────────────────────────────────────────────── */
eui_widget* eui_separator_new(int vertical);

/* ── Icon ────────────────────────────────────────────────────── */
eui_widget* eui_icon_new(eui_icon_id id, eui_color col);

/* ── Progress bar ────────────────────────────────────────────── */
eui_widget* eui_progress_new(int max);
void  eui_progress_set(eui_widget* w, int value);

#endif /* EUI_WIDGETS_H */
