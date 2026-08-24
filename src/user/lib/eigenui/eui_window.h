/***************************************************************/
/*  EigenUI Window — bridges the kernel window/event syscalls to  */
/*  the widget tree (Ecore/Elementary-like event pump).          */
/***************************************************************/
#ifndef EUI_WINDOW_H
#define EUI_WINDOW_H

#include "eui_canvas.h"
#include "eui_theme.h"
#include "eui_widget.h"

typedef struct eui_window eui_window;

/* Create a window (maps its framebuffer, installs a default theme + font). */
eui_window* eui_window_new(const char* title, int w, int h);
void        eui_window_free(eui_window* w);
void        eui_window_set_title(eui_window* w, const char* title);
void        eui_window_set_theme(eui_window* w, const eui_theme* t);
void        eui_window_use_os_theme(eui_window* w);   /* match the live OS palette */
void        eui_window_set_scale(eui_window* w, float scale);

eui_widget* eui_window_root(eui_window* w);           /* the root container (a Box) */
void        eui_window_add(eui_window* w, eui_widget* child); /* add to root */
void        eui_window_set_content(eui_window* w, eui_widget* root); /* replace root */
void        eui_window_mark_dirty(eui_window* w);

/* Pump exactly one frame (drain events, tick animations, redraw if needed). */
void        eui_window_step(eui_window* w);
/* Blocking run loop until the window is closed. */
void        eui_window_run(eui_window* w);

#endif /* EUI_WINDOW_H */
