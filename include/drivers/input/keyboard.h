/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>

// Arrow key codes (using values above ASCII range to avoid conflicts)
#define KEY_UP    128
#define KEY_DOWN  129
#define KEY_LEFT  130
#define KEY_RIGHT 131
#define KEY_PAGE_UP   133
#define KEY_PAGE_DOWN 134
#define KEY_LSHIFT    135
#define KEY_RSHIFT    136
#define KEY_HOME      137
#define KEY_END       138
#define KEY_DELETE    139
#define KEY_ESC   27
#define KEY_SUPER 132

// Compare key codes safely (char callbacks receive 128+ as negative values)
#define KEY_MATCH(k, code) ((unsigned char)(k) == (unsigned char)(code))

char get_key();
int get_key_ex(void);
int keyboard_has_input(void);
void keyboard_drain(void);
int keyboard_is_key_down(uint8_t scancode);
void keyboard_handler();

unsigned char port_byte_in(unsigned short port);
void port_byte_out(unsigned short port, unsigned char data);
unsigned short port_word_in(unsigned short port);
void port_word_out(unsigned short port, unsigned short data);
unsigned int port_long_in(unsigned short port);
void port_long_out(unsigned short port, unsigned int data);


#endif
