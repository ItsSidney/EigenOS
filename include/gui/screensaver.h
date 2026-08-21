/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#ifndef SCREENSAVER_H
#define SCREENSAVER_H

#include "engine/anim/anim_engine.h"

#define SAVER_EFFECTS 5
/* effect ids */
#define SAVER_STARFIELD 0
#define SAVER_MATRIX    1
#define SAVER_PLASMA    2
#define SAVER_ORBIT     3
#define SAVER_AURORA    4

/* Settings (writable by the Screen Saver app). */
extern int saver_enabled;   /* idle seconds before activation; 0 = disabled */
extern int saver_effect;    /* 0..SAVER_EFFECTS-1 */
extern int saver_cycle;     /* auto-cycle seconds between effects; 0 = off */
extern int saver_active;    /* currently covering the screen */
extern uint32_t saver_armed_at;  /* ms timestamp of last activation (wake grace) */

/* Start/stop the fullscreen screensaver. */
void screensaver_activate(void);
void screensaver_deactivate(void);

/* Render one fullscreen frame (call from the GUI loop while active). */
void screensaver_frame(void);

/* Effect controls (used by the settings app for the live preview). */
void saver_effect_init(void);
void saver_effect_update(float dt);
anim_canvas_t* saver_canvas(void);

#endif /* SCREENSAVER_H */
