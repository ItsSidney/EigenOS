/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#include "kernel/net/if.h"
#include "kernel/net/mbuf.h"
#include <string.h>

/*
 * FreeBSD-style loopback interface (lo0) for Eigen.
 */

static struct ifnet lo0_ifp;

static int lo_init(struct ifnet* ifp) {
    ifp->if_flags |= IFF_RUNNING;
    return 0;
}

static int lo_output(struct ifnet* ifp, struct mbuf* m) {
    /* Loopback simply passes the packet back to the input side */
    /* TODO: Call ip_input(m) or similar */
    ifp->if_ipackets++;
    ifp->if_opackets++;
    m_freem(m);
    return 0;
}

void loopback_init() {
    memset(&lo0_ifp, 0, sizeof(struct ifnet));
    strcpy(lo0_ifp.if_xname, "lo0");
    lo0_ifp.if_mtu = 16384;
    lo0_ifp.if_flags = IFF_LOOPBACK | IFF_UP;
    lo0_ifp.if_init = lo_init;
    lo0_ifp.if_output = lo_output;
    
    if_attach(&lo0_ifp);
}
