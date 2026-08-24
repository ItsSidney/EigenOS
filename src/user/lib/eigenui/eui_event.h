/***************************************************************/
/*  EigenUI Event — Ecore-like event types                      */
/*                                                             */
/*  The window translates raw eigen_ev_t into these and routes  */
/*  them to widgets. Key input carries BOTH the ASCII char (`key`,*/
/*  with 0x100 meaning "no ASCII") and the raw scancode (`code`), */
/*  so widgets can handle Enter/Arrows/etc. via the EUI_SCAN_*  */
/*  constants below.                                            */
/***************************************************************/
#ifndef EUI_EVENT_H
#define EUI_EVENT_H

#include "eui_core.h"

typedef enum {
    EUI_EV_MOUSEMOVE = 1,
    EUI_EV_MOUSEDOWN,
    EUI_EV_MOUSEUP,
    EUI_EV_MOUSEWHEEL,
    EUI_EV_KEYDOWN,
    EUI_EV_KEYUP,
    EUI_EV_TEXT,      /* printable char typed (utf8 in `text`) */
    EUI_EV_RESIZE,
    EUI_EV_CLOSE,
    EUI_EV_FOCUS,
    EUI_EV_BLUR
} eui_event_type;

typedef struct eui_event {
    eui_event_type type;
    int x, y;          /* mouse position, logical content coords        */
    int dx, dy;        /* wheel delta / drag delta                     */
    int button;        /* 0 left, 1 right, 2 middle                    */
    int key;           /* ASCII char, or (char|0x100) for non-printable */
    int code;          /* raw scancode (Set 1) for specials            */
    int mods;          /* bit0 shift, bit1 ctrl, bit2 alt              */
    char text[8];      /* utf8 for text events                         */
} eui_event;

/* Scancode (Set 1) constants for special keys. */
#define EUI_SCAN_ENTER  0x1C
#define EUI_SCAN_BACK   0x0E
#define EUI_SCAN_DEL    0x53
#define EUI_SCAN_TAB    0x0F
#define EUI_SCAN_ESC    0x01
#define EUI_SCAN_LEFT   0x4B
#define EUI_SCAN_RIGHT  0x4D
#define EUI_SCAN_UP     0x48
#define EUI_SCAN_DOWN   0x50
#define EUI_SCAN_HOME   0x47
#define EUI_SCAN_END    0x4F

/* True if the event's `key` field holds a printable ASCII char. */
static inline int eui_key_printable(const eui_event* e) {
    return (e->key & 0x100) == 0 && e->key >= 0x20 && e->key < 0x7F;
}

#endif /* EUI_EVENT_H */
