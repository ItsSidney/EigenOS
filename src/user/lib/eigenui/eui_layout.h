/***************************************************************/
/*  EigenUI Layout — Box / Panel / ScrollView (non-overlapping)   */
/*                                                             */
/*  These containers assign every child an absolute, non-        */
/*  overlapping rect. Box = vertical/horizontal flow with weights */
/*  and alignment. ScrollView = clipped viewport with AUTOMATIC   */
/*  scrollbars when content exceeds the view.                    */
/***************************************************************/
#ifndef EUI_LAYOUT_H
#define EUI_LAYOUT_H

#include "eui_widget.h"

/* ── Box ─────────────────────────────────────────────────────── */
/* dir: 0 = vertical, 1 = horizontal */
eui_widget* eui_box_new(int dir);
void  eui_box_add(eui_widget* box, eui_widget* child);
void  eui_box_set_spacing(eui_widget* box, int s);
void  eui_box_set_padding(eui_widget* box, int p);

/* ── Panel (framed group with optional title, contains a flow) ─ */
eui_widget* eui_panel_new(const char* title);
void  eui_panel_add(eui_widget* panel, eui_widget* child);
void  eui_panel_set_padding(eui_widget* panel, int p);

/* ── ScrollView (auto scrollbars) ────────────────────────────── */
eui_widget* eui_scroll_new(int vertical);
void  eui_scroll_set_content(eui_widget* scroll, eui_widget* content);

/* A flexible spacer (absorbs free space, weight 1 on its axis). */
eui_widget* eui_spacer_new(void);

#endif /* EUI_LAYOUT_H */
