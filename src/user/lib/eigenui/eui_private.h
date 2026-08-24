/***************************************************************/
/*  EigenUI — private shared declarations (not part of the API)  */
/***************************************************************/
#ifndef EUI_PRIVATE_H
#define EUI_PRIVATE_H

#include "eui_widget.h"

/* Per-kind handler signatures. The base dispatch in eui_widget.c switches on
   kind and forwards to these. Defined in eui_layout.c / eui_widgets.c. */
void eui_box_layout(eui_widget* w, int x, int y, int aw, int ah);
void eui_box_draw(eui_widget* w, eui_canvas* c, eui_theme* t);
int  eui_box_event(eui_widget* w, const eui_event* e, struct eui_window* win);

void eui_panel_layout(eui_widget* w, int x, int y, int aw, int ah);
void eui_panel_draw(eui_widget* w, eui_canvas* c, eui_theme* t);
int  eui_panel_event(eui_widget* w, const eui_event* e, struct eui_window* win);

void eui_label_layout(eui_widget* w, int x, int y, int aw, int ah);
void eui_label_draw(eui_widget* w, eui_canvas* c, eui_theme* t);
int  eui_label_event(eui_widget* w, const eui_event* e, struct eui_window* win);

void eui_button_layout(eui_widget* w, int x, int y, int aw, int ah);
void eui_button_draw(eui_widget* w, eui_canvas* c, eui_theme* t);
int  eui_button_event(eui_widget* w, const eui_event* e, struct eui_window* win);

void eui_toggle_layout(eui_widget* w, int x, int y, int aw, int ah);
void eui_toggle_draw(eui_widget* w, eui_canvas* c, eui_theme* t);
int  eui_toggle_event(eui_widget* w, const eui_event* e, struct eui_window* win);

void eui_slider_layout(eui_widget* w, int x, int y, int aw, int ah);
void eui_slider_draw(eui_widget* w, eui_canvas* c, eui_theme* t);
int  eui_slider_event(eui_widget* w, const eui_event* e, struct eui_window* win);

void eui_entry_layout(eui_widget* w, int x, int y, int aw, int ah);
void eui_entry_draw(eui_widget* w, eui_canvas* c, eui_theme* t);
int  eui_entry_event(eui_widget* w, const eui_event* e, struct eui_window* win);

void eui_list_layout(eui_widget* w, int x, int y, int aw, int ah);
void eui_list_draw(eui_widget* w, eui_canvas* c, eui_theme* t);
int  eui_list_event(eui_widget* w, const eui_event* e, struct eui_window* win);

void eui_separator_layout(eui_widget* w, int x, int y, int aw, int ah);
void eui_separator_draw(eui_widget* w, eui_canvas* c, eui_theme* t);
int  eui_separator_event(eui_widget* w, const eui_event* e, struct eui_window* win);

void eui_icon_layout(eui_widget* w, int x, int y, int aw, int ah);
void eui_icon_wdraw(eui_widget* w, eui_canvas* c, eui_theme* t);
int  eui_icon_event(eui_widget* w, const eui_event* e, struct eui_window* win);

void eui_progress_layout(eui_widget* w, int x, int y, int aw, int ah);
void eui_progress_draw(eui_widget* w, eui_canvas* c, eui_theme* t);
int  eui_progress_event(eui_widget* w, const eui_event* e, struct eui_window* win);

void eui_scroll_layout(eui_widget* w, int x, int y, int aw, int ah);
void eui_scroll_draw(eui_widget* w, eui_canvas* c, eui_theme* t);
int  eui_scroll_event(eui_widget* w, const eui_event* e, struct eui_window* win);

void eui_spacer_layout(eui_widget* w, int x, int y, int aw, int ah);
void eui_spacer_draw(eui_widget* w, eui_canvas* c, eui_theme* t);
int  eui_spacer_event(eui_widget* w, const eui_event* e, struct eui_window* win);

#endif /* EUI_PRIVATE_H */
