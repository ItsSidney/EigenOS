/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#ifndef EIGEN_NET_LOG_H
#define EIGEN_NET_LOG_H

#include <stdint.h>
#include <stddef.h>

#define NET_LOG_MAX_ENTRIES 256
#define NET_LOG_MSG_MAX     128

typedef struct {
    uint32_t timestamp_ms;
    char     tag[12];
    char     msg[NET_LOG_MSG_MAX];
    uint32_t color;
} net_log_entry_t;

void net_log_init(void);
void net_log_add(const char* tag, const char* msg, uint32_t color);
void net_log_addf(const char* tag, uint32_t color, const char* fmt, ...);
int  net_log_get_count(void);
int  net_log_get_entry(int index, net_log_entry_t* out_entry);
void net_log_clear(void);

#endif
