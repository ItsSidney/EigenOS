/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

// ============================================================
//  Eigen — PC Speaker Driver (PIT Channel 2)
// ============================================================
#ifndef SPEAKER_H
#define SPEAKER_H

#include <stdint.h>

void speaker_play_freq(uint32_t freq);
void speaker_stop(void);
void play_beep(void);

#endif
