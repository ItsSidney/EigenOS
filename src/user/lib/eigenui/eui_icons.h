/***************************************************************/
/*  EigenUI Icons — professional vector icon set (crisp at any    */
/*  size). Drawn with the canvas so they rescale without blur.    */
/*  Apps use eui_icon_draw() or the Icon widget.                  */
/***************************************************************/
#ifndef EUI_ICONS_H
#define EUI_ICONS_H

#include "eui_canvas.h"

typedef enum {
    EUI_ICON_NONE = 0,
    EUI_ICON_CLOSE, EUI_ICON_MINIMIZE, EUI_ICON_MAXIMIZE,
    EUI_ICON_FILE, EUI_ICON_FOLDER, EUI_ICON_SETTINGS, EUI_ICON_SEARCH,
    EUI_ICON_PLUS, EUI_ICON_MINUS, EUI_ICON_TRASH, EUI_ICON_INFO,
    EUI_ICON_WARNING, EUI_ICON_ERROR, EUI_ICON_CHECK, EUI_ICON_CHECKBOX,
    EUI_ICON_ARROW_LEFT, EUI_ICON_ARROW_RIGHT, EUI_ICON_ARROW_UP, EUI_ICON_ARROW_DOWN,
    EUI_ICON_APP, EUI_ICON_POWER, EUI_ICON_WIFI, EUI_ICON_VOLUME,
    EUI_ICON_MENU, EUI_ICON_REFRESH, EUI_ICON_EDIT, EUI_ICON_SAVE,
    EUI_ICON_COPY, EUI_ICON_USER, EUI_ICON_LOCK, EUI_ICON_IMAGE,
    EUI_ICON_MUSIC, EUI_ICON_HELP, EUI_ICON_STAR, EUI_ICON_HOME,
    EUI_ICON_TRIANGLE, EUI_ICON_STARFILL, EUI_ICON_HEART,
    EUI_ICON_COUNT
} eui_icon_id;

/* Draw icon `id` centred in the box (x,y,size). */
void eui_icon_draw(eui_canvas* c, eui_icon_id id, int x, int y, int size, eui_color col);

#endif /* EUI_ICONS_H */
