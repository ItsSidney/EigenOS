/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#ifndef TIMER_H
#define TIMER_H
#include <stdint.h>

void init_timer(uint32_t freq);
void timer_calibrate(void);
void sleep_ms(uint32_t ms);
uint32_t timer_get_ms(void);

extern uint32_t timer_hz;

#endif
