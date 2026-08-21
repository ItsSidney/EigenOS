/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#ifndef _KERNEL_NET_IP_H
#define _KERNEL_NET_IP_H

#include <stdint.h>
#include "in.h"
#include "mbuf.h"

/*
 * IP header definitions following FreeBSD's <netinet/ip.h>
 */

struct ip {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint8_t  ip_hl:4,        /* header length */
             ip_v:4;         /* version */
#else
    uint8_t  ip_v:4,         /* version */
             ip_hl:4;        /* header length */
#endif
    uint8_t  ip_tos;         /* type of service */
    uint16_t ip_len;         /* total length */
    uint16_t ip_id;          /* identification */
    uint16_t ip_off;         /* fragment offset field */
#define IP_RF 0x8000         /* reserved fragment flag */
#define IP_DF 0x4000         /* don't fragment flag */
#define IP_MF 0x2000         /* more fragments flag */
#define IP_OFFMASK 0x1fff    /* mask for fragmenting bits */
    uint8_t  ip_ttl;         /* time to live */
    uint8_t  ip_p;           /* protocol */
    uint16_t ip_sum;         /* checksum */
    struct in_addr ip_src, ip_dst; /* source and dest address */
} __attribute__((packed));

struct tcphdr;
void ip_input(struct mbuf* m);
int  ip_output(struct mbuf* m, struct ifnet* ifp);
uint16_t ip_checksum(void* vdata, size_t length);
uint16_t tcp_checksum(struct ip* ip, struct tcphdr* th, struct mbuf* m_data, int off);

#endif /* _KERNEL_NET_IP_H */
