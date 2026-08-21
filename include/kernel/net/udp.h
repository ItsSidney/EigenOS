/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#ifndef _KERNEL_NET_UDP_H
#define _KERNEL_NET_UDP_H

#include <stdint.h>
#include "in.h"
#include "mbuf.h"

struct udphdr {
    uint16_t uh_sport;
    uint16_t uh_dport;
    uint16_t uh_ulen;
    uint16_t uh_sum;
} __attribute__((packed));

void udp_input(struct mbuf* m, int off);
int  udp_output(uint32_t dst_ip, uint16_t dport, uint16_t sport,
                const void* data, uint16_t datalen);
int  udp_recv(uint16_t port, uint32_t* src_ip, uint16_t* src_port,
              void* buf, uint16_t* len);

#endif
