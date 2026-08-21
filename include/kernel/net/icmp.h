/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#ifndef _KERNEL_NET_ICMP_H
#define _KERNEL_NET_ICMP_H

#include <stdint.h>
#include "mbuf.h"
#include "ip.h"

/*
 * ICMP definitions following RFC 792
 */

struct icmphdr {
    uint8_t  icmp_type;
    uint8_t  icmp_code;
    uint16_t icmp_cksum;
    union {
        struct {
            uint16_t id;
            uint16_t sequence;
        } echo;
        uint32_t gateway;
        struct {
            uint16_t unused;
            uint16_t mtu;
        } frag;
    } icmp_hun;
} __attribute__((packed));

#define icmp_id       icmp_hun.echo.id
#define icmp_seq      icmp_hun.echo.sequence
#define icmp_nextmtu  icmp_hun.frag.mtu

/* ICMP types */
#define ICMP_ECHOREPLY      0
#define ICMP_UNREACH        3
#define ICMP_SOURCEQUENCH   4
#define ICMP_REDIRECT       5
#define ICMP_ECHO           8
#define ICMP_TIMXCEED       11
#define ICMP_PARAMPROB      12
#define ICMP_TSTAMP         13
#define ICMP_TSTAMPREPLY    14
#define ICMP_IREQ           15
#define ICMP_IREQREPLY      16
#define ICMP_MASKREQ        17
#define ICMP_MASKREPLY      18

void icmp_input(struct mbuf* m, int off);
int  icmp_get_echo_reply(uint32_t* src, uint16_t* id, uint16_t* seq);

#endif /* _KERNEL_NET_ICMP_H */
