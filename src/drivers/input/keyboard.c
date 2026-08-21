/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#include "drivers/input/keyboard.h"

static int shift_pressed = 0;
static int caps_lock = 0;
static int ctrl_pressed = 0;
static int extended_key = 0;

#define KEY_BUF_SIZE 256
static int key_buffer[KEY_BUF_SIZE];
static int key_head = 0;
static int key_tail = 0;
static uint8_t key_states[256];

int keyboard_is_key_down(uint8_t scancode) {
    return key_states[scancode & 0x7F];
}

unsigned char port_byte_in(unsigned short port) {
    unsigned char result;
    __asm__("in %%dx, %%al" : "=a" (result) : "d" (port));
    return result;
}

void port_byte_out(unsigned short port, unsigned char data) {
    __asm__("out %%al, %%dx" : : "a" (data), "d" (port));
}

void port_word_out(unsigned short port, unsigned short data) {
    __asm__("out %%ax, %%dx" : : "a" (data), "d" (port));
}

unsigned short port_word_in(unsigned short port) {
    unsigned short result;
    __asm__("in %%dx, %%ax" : "=a" (result) : "d" (port));
    return result;
}

unsigned int port_long_in(unsigned short port) {
    unsigned int result;
    __asm__("in %%dx, %%eax" : "=a" (result) : "d" (port));
    return result;
}

void port_long_out(unsigned short port, unsigned int data) {
    __asm__("out %%eax, %%dx" : : "a" (data), "d" (port));
}

static void push_key(int c) {
    if (c == 0) return;
    int next = (key_head + 1) % KEY_BUF_SIZE;
    if (next != key_tail) {
        key_buffer[key_head] = c;
        key_head = next;
    }
}

/* Raw key events: presses are the plain code, releases are code|0x100.
   Only the WM (get_key_ex) consumes releases; char readers (get_key)
   see them as "no key", exactly like before releases existed. */
int get_key_ex(void) {
    if (key_head == key_tail) return 0;
    int c = key_buffer[key_tail];
    key_tail = (key_tail + 1) % KEY_BUF_SIZE;
    return c;
}

char get_key(void) {
    int c = get_key_ex();
    if (c & 0x100) return 0;   /* key release: hidden from char readers */
    return (char)c;
}

/* Map a normal (non-extended) scancode to its character code using the
   current shift/caps/ctrl state. Returns 0 for unmapped keys. Shared by
   the press and release paths so releases mirror presses exactly. */
static int map_code(uint8_t sc) {
    static const char normal_map[] = {
        0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
        '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
        0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\',
        'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, 0, 0, ' '
    };
    static const char shift_map[] = {
        0, 0, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
        '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
        0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '\"', '~', 0, '|',
        'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, 0, 0, ' '
    };
    if (sc == 0x01) return KEY_ESC;
    if (sc >= 58) return 0;
    char c = shift_pressed ? shift_map[sc] : normal_map[sc];
    /* Map scancode 50 ('m') if it was missed */
    if (c == 0 && sc == 50) c = 'm';
    /* Apply Shift/CapsLock */
    if (shift_pressed || caps_lock) {
        if (c >= 'a' && c <= 'z') c -= 32;
        else if (c >= 'A' && c <= 'Z') c += 32; /* in case already uppercase */
    }
    /* Ctrl handling */
    if (ctrl_pressed) {
        if (c >= 'a' && c <= 'z') c = c - 'a' + 1;
        else if (c >= 'A' && c <= 'Z') c = c - 'A' + 1;
    }
    return (int)(unsigned char)c;
}

int keyboard_has_input(void) {
    return key_head != key_tail;
}

void keyboard_drain(void) {
    key_tail = key_head;
}

void keyboard_handler() {
    uint8_t status = port_byte_in(0x64);
    if (!(status & 1)) return;

    uint8_t scancode = port_byte_in(0x60);

    if (scancode == 0xE0) { extended_key = 1; return; }
    
    static int super_active = 0;
    static int super_used_combo = 0;

    if (extended_key) {
        extended_key = 0;
        int is_release = (scancode & 0x80) != 0;
        uint8_t sc = scancode & 0x7F;
        key_states[sc] = !is_release;
        
        if (sc == 0x5B || sc == 0x5C) { // Super key
            if (!is_release) {
                super_active = 1;
                super_used_combo = 0;
            } else {
                if (super_active && !super_used_combo) {
                    push_key(KEY_SUPER);
                }
                super_active = 0;
            }
            return;
        }

        if (super_active) super_used_combo = 1;

        int code = 0;
        if (sc == 0x48) code = KEY_UP;
        else if (sc == 0x50) code = KEY_DOWN;
        else if (sc == 0x4B) code = KEY_LEFT;
        else if (sc == 0x4D) code = KEY_RIGHT;
        else if (sc == 0x47) code = KEY_HOME;
        else if (sc == 0x4F) code = KEY_END;
        else if (sc == 0x49) code = KEY_PAGE_UP;
        else if (sc == 0x51) code = KEY_PAGE_DOWN;
        else if (sc == 0x53) code = KEY_DELETE;
        if (code) push_key(is_release ? (code | 0x100) : code);
        return;
    }

    if (scancode & 0x80) {
        uint8_t sc = scancode & 0x7F;
        key_states[sc] = 0;
        if (sc == 0x1D) { ctrl_pressed = 0; return; }
        if (sc == 0x2A || sc == 0x36) {
            shift_pressed = 0;
            key_states[sc == 0x2A ? KEY_LSHIFT : KEY_RSHIFT] = 0;
            return;
        }
        /* Key release: mirror the press mapping (0x100 = release marker)
           so ring-3 apps (DOOM) can clear their key-down state. */
        int c = map_code(sc);
        if (c) push_key(c | 0x100);
        return;
    }

    key_states[scancode & 0x7F] = 1;
    if (super_active) super_used_combo = 1;

    if (scancode == 0x1D) { ctrl_pressed = 1; return; } 
    if (scancode == 0x3A) { caps_lock = !caps_lock; return; }
    if (scancode == 0x2A || scancode == 0x36) {
        shift_pressed = 1;
        key_states[scancode == 0x2A ? KEY_LSHIFT : KEY_RSHIFT] = 1;
        return;
    }
    int c = map_code(scancode);
    if (c) push_key(c);
}
