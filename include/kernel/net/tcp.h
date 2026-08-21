/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#ifndef _KERNEL_NET_TCP_H
#define _KERNEL_NET_TCP_H

#include <stdint.h>
#include "mbuf.h"

/*
 * TCP header definitions following FreeBSD's <netinet/tcp.h>
 */

typedef uint32_t tcp_seq;

/* TCP states */
#define TCPS_CLOSED        0
#define TCPS_SYN_SENT      1
#define TCPS_ESTABLISHED   2
#define TCPS_FIN_WAIT_1    3

struct tcphdr {
    uint16_t th_sport;       /* source port */
    uint16_t th_dport;       /* destination port */
    tcp_seq  th_seq;         /* sequence number */
    tcp_seq  th_ack;         /* acknowledgement number */
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint8_t  th_x2:4,        /* (unused) */
             th_off:4;       /* data offset */
#else
    uint8_t  th_off:4,       /* data offset */
             th_x2:4;        /* (unused) */
#endif
    uint8_t  th_flags;
#define TH_FIN  0x01
#define TH_SYN  0x02
#define TH_RST  0x04
#define TH_PUSH 0x08
#define TH_ACK  0x10
#define TH_URG  0x20
#define TH_ECE  0x40
#define TH_CWR  0x80
    uint16_t th_win;         /* window */
    uint16_t th_sum;         /* checksum */
    uint16_t th_urp;         /* urgent pointer */
} __attribute__((packed));

void tcp_input(struct mbuf* m, int off);

#endif /* _KERNEL_NET_TCP_H */
