/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#include "drivers/time/rtc.h"
#include "drivers/input/keyboard.h"

static int offset_h = 0;
static int offset_m = 0;

unsigned char read_cmos(unsigned char reg) {
    port_byte_out(0x70, reg);
    return port_byte_in(0x71);
}

int is_updating() {
    port_byte_out(0x70, 0x0A);
    return (port_byte_in(0x71) & 0x80);
}

void get_time(time_t* t) {
    int rtc_timeout = 10000;
    while (is_updating() && --rtc_timeout > 0);
    
    unsigned char second = read_cmos(0x00);
    unsigned char minute = read_cmos(0x02);
    unsigned char hour = read_cmos(0x04);
    unsigned char day = read_cmos(0x07);
    unsigned char month = read_cmos(0x08);
    unsigned char year = read_cmos(0x09);
    unsigned char registerB = read_cmos(0x0B);

    // Convert BCD to binary if necessary
    if (!(registerB & 0x04)) {
        second = (second & 0x0F) + ((second / 16) * 10);
        minute = (minute & 0x0F) + ((minute / 16) * 10);
        hour = ((hour & 0x0F) + (((hour & 0x70) / 16) * 10)) | (hour & 0x80);
        day = (day & 0x0F) + ((day / 16) * 10);
        month = (month & 0x0F) + ((month / 16) * 10);
        year = (year & 0x0F) + ((year / 16) * 10);
    }

    t->second = (int)second;
    t->minute = (int)minute + offset_m;
    t->hour = (int)hour + offset_h;
    t->day = (int)day;
    t->month = (int)month;
    t->year = (int)year + 2000; // Assuming 21st century
    
    // Normalize
    while (t->minute >= 60) { t->minute -= 60; t->hour++; }
    while (t->minute < 0) { t->minute += 60; t->hour--; }
    while (t->hour >= 24) { t->hour -= 24; }
    while (t->hour < 0) { t->hour += 24; }
}

void set_time_offset(int h, int m) {
    offset_h = h;
    offset_m = m;
}
