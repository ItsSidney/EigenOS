/***************************************************************/
/*  EigenUI App — one-line application bootstrap                  */
/***************************************************************/
#ifndef EUI_APP_H
#define EUI_APP_H

#include "eui_window.h"

/* Build a window around `content` and run the event loop until closed. */
void eui_app_run(eui_widget* content, const char* title, int w, int h);

#endif /* EUI_APP_H */
