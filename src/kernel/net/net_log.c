/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#include "kernel/net/net_log.h"
#include "kernel/time/timer.h"
#include "kernel/lib/stdio.h"
#include <string.h>
#include <stdarg.h>

static net_log_entry_t net_log_buf[NET_LOG_MAX_ENTRIES];
static int net_log_head = 0;
static int net_log_count = 0;

void net_log_init(void) {
    memset(net_log_buf, 0, sizeof(net_log_buf));
    net_log_head = 0;
    net_log_count = 0;
}

void net_log_add(const char* tag, const char* msg, uint32_t color) {
    int idx = (net_log_head + net_log_count) % NET_LOG_MAX_ENTRIES;
    if (net_log_count == NET_LOG_MAX_ENTRIES) {
        net_log_head = (net_log_head + 1) % NET_LOG_MAX_ENTRIES;
    } else {
        net_log_count++;
    }

    net_log_entry_t* e = &net_log_buf[idx];
    e->timestamp_ms = timer_get_ms();
    e->color = color ? color : 0x58A6FF;

    int i = 0;
    if (tag) {
        while (tag[i] && i < 11) { e->tag[i] = tag[i]; i++; }
    }
    e->tag[i] = 0;

    i = 0;
    if (msg) {
        while (msg[i] && i < NET_LOG_MSG_MAX - 1) { e->msg[i] = msg[i]; i++; }
    }
    e->msg[i] = 0;
}

void net_log_addf(const char* tag, uint32_t color, const char* fmt, ...) {
    char msg_buf[NET_LOG_MSG_MAX];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg_buf, sizeof(msg_buf), fmt, ap);
    va_end(ap);
    net_log_add(tag, msg_buf, color);
}

int net_log_get_count(void) {
    return net_log_count;
}

int net_log_get_entry(int index, net_log_entry_t* out_entry) {
    if (index < 0 || index >= net_log_count || !out_entry) return -1;
    int idx = (net_log_head + index) % NET_LOG_MAX_ENTRIES;
    *out_entry = net_log_buf[idx];
    return 0;
}

void net_log_clear(void) {
    net_log_head = 0;
    net_log_count = 0;
}
