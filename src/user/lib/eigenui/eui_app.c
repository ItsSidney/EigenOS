/***************************************************************/
/*  EigenUI App — implementation                                  */
/***************************************************************/
#include "eui_app.h"

void eui_app_run(eui_widget* content, const char* title, int w, int h) {
    if (!content) return;
    eui_window* win = eui_window_new(title, w, h);
    eui_window_set_content(win, content);
    eui_window_run(win);
}
