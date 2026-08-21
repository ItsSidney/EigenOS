/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#ifndef _KERNEL_NET_DHCP_H
#define _KERNEL_NET_DHCP_H

#include <stdint.h>

/* Attempt to configure the given interface via DHCP.
 * On success sets ifp->if_ip / if_netmask / if_gateway / if_dns and
 * ifp->if_dhcp = 1, and returns 0.
 * On failure (no server answered) returns -1 and leaves the interface
 * unconfigured. Caller is responsible for applying a static fallback. */
int dhcp_request(struct ifnet* ifp);

/* Broadcast address used for DHCP discovery/request (255.255.255.255). */
uint32_t dhcp_broadcast_addr(void);

#endif /* _KERNEL_NET_DHCP_H */
