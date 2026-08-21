/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#ifndef _KERNEL_NET_IF_H
#define _KERNEL_NET_IF_H

#include <stdint.h>
#include "mbuf.h"

/*
 * FreeBSD-style interface management for Eigen.
 */

#define IF_NAMESIZE 16

struct ifnet {
    char        if_xname[IF_NAMESIZE];  /* interface name (e.g. "em0") */
    void*       if_softc;               /* driver private data */
    uint32_t    if_flags;               /* up/down, broadcast, etc. */
    uint32_t    if_mtu;                 /* maximum transmission unit */
    uint32_t    if_ip;                  /* IP address */
    uint32_t    if_netmask;             /* subnet mask (network byte order) */
    uint32_t    if_gateway;             /* default gateway (network byte order) */
    uint32_t    if_dns;                 /* DNS server (network byte order) */
    int         if_dhcp;                /* 1 = address came from DHCP, 0 = static */

    /* Hardware address */
    uint8_t     if_hwaddr[6];

    /* Statistics */
    uint64_t    if_ipackets;
    uint64_t    if_opackets;
    uint64_t    if_ierrors;
    uint64_t    if_oerrors;

    /* Driver methods */
    int (*if_init)(struct ifnet*);
    int (*if_output)(struct ifnet*, struct mbuf*);
    int (*if_ioctl)(struct ifnet*, uint32_t, void*);
    void (*if_start)(struct ifnet*);

    struct ifnet* if_next;
};

/* Interface flags */
#define IFF_UP          0x01    /* interface is up */
#define IFF_BROADCAST   0x02    /* broadcast address valid */
#define IFF_DEBUG       0x04    /* turn on debugging */
#define IFF_LOOPBACK    0x08    /* is a loopback net */
#define IFF_POINTOPOINT 0x10    /* is a point-to-point link */
#define IFF_RUNNING     0x40    /* resources allocated */
#define IFF_NOARP       0x80    /* no address resolution protocol */
#define IFF_PROMISC     0x100   /* receive all packets */

/* IOCTLs */
#define SIOCSIFADDR     1
#define SIOCGIFADDR     2
#define SIOCSIFFLAGS    3
#define SIOCGIFFLAGS    4

void if_attach(struct ifnet* ifp);
void if_detach(struct ifnet* ifp);
struct ifnet* if_find(const char* name);
struct ifnet* if_list_head(void);
struct ifnet* if_find_primary(void);

/* Walk the ARP cache (one callback per valid entry). */
void arp_foreach(void (*cb)(int idx, uint32_t ip, const uint8_t* mac, int is_static));

#endif /* _KERNEL_NET_IF_H */
