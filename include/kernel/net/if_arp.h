/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#ifndef _KERNEL_NET_IF_ARP_H
#define _KERNEL_NET_IF_ARP_H

#include <stdint.h>
#include "in.h"
#include "mbuf.h"
#include "if.h"

/*
 * ARP definitions following FreeBSD's <net/if_arp.h>
 */

struct arphdr {
    uint16_t ar_hrd;    /* format of hardware address */
    uint16_t ar_pro;    /* format of protocol address */
    uint8_t  ar_hln;    /* length of hardware address */
    uint8_t  ar_pln;    /* length of protocol address */
    uint16_t ar_op;     /* ARP opcode (command) */
#define ARPHRD_ETHER   1
#define ARPOP_REQUEST  1  /* ARP request */
#define ARPOP_REPLY    2  /* ARP reply */
} __attribute__((packed));

#ifndef ETHERTYPE_IP
#define ETHERTYPE_IP 0x0800
#endif
#ifndef ETHERTYPE_ARP
#define ETHERTYPE_ARP 0x0806
#endif

struct ether_arp {
    struct arphdr ea_hdr;               /* fixed-size header */
    uint8_t  arp_sha[6];                /* sender hardware address */
    uint8_t  arp_spa[4];                /* sender protocol address */
    uint8_t  arp_tha[6];                /* target hardware address */
    uint8_t  arp_tpa[4];                /* target protocol address */
} __attribute__((packed));

void arp_input(struct mbuf* m);
int arp_resolve(struct ifnet* ifp, struct mbuf* m, const struct in_addr* dst, uint8_t* dest_enaddr);

#endif /* _KERNEL_NET_IF_ARP_H */
