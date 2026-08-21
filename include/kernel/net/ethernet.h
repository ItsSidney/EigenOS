/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#ifndef _KERNEL_NET_ETHERNET_H
#define _KERNEL_NET_ETHERNET_H

#include <stdint.h>
#include "if.h"
#include "mbuf.h"

/*
 * Ethernet definitions following FreeBSD's <net/ethernet.h>
 */

#define ETHER_ADDR_LEN  6
#define ETHER_TYPE_LEN  2
#define ETHER_CRC_LEN   4
#define ETHER_HDR_LEN   14
#define ETHER_MIN_LEN   64
#define ETHER_MAX_LEN   1518

struct ether_header {
    uint8_t  ether_dhost[ETHER_ADDR_LEN];
    uint8_t  ether_shost[ETHER_ADDR_LEN];
    uint16_t ether_type;
} __attribute__((packed));

#define ETHERTYPE_PUP      0x0200  /* PUP protocol */
#define ETHERTYPE_IP       0x0800  /* IP protocol */
#define ETHERTYPE_ARP      0x0806  /* address resolution protocol */
#define ETHERTYPE_REVARP   0x8035  /* reverse addr resolution protocol */
#define ETHERTYPE_VLAN     0x8100  /* IEEE 802.1Q VLAN tagging */
#define ETHERTYPE_IPV6     0x86dd  /* IPv6 */
#define ETHERTYPE_LOOPBACK 0x9000  /* used to test interfaces */

void ether_input(struct ifnet* ifp, struct mbuf* m);
int  ether_output(struct ifnet* ifp, struct mbuf* m, const uint8_t* dst, uint16_t type);

#endif /* _KERNEL_NET_ETHERNET_H */
