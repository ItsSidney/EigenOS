/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#include "kernel/net/if.h"
#include "kernel/net/mbuf.h"
#include "kernel/net/ethernet.h"
#include "kernel/net/if_arp.h"
#include "kernel/net/ip.h"
#include "kernel/net/in.h"
#include "drivers/video/framebuffer.h"
#include <string.h>
#include "kernel/lib/stdio.h"

/*
 * Ethernet input/output logic for Eigen.
 */

void ether_input(struct ifnet* ifp, struct mbuf* m) {
    struct ether_header* eh;
    (void)ifp;
    if (m->m_len < ETHER_HDR_LEN) {
        m_freem(m);
        return;
    }
    eh = (struct ether_header*)m->m_data;
    uint16_t type = ntohs(eh->ether_type);
    m->m_data += ETHER_HDR_LEN;
    m->m_len -= ETHER_HDR_LEN;
    switch (type) {
        case ETHERTYPE_IP:
            ip_input(m);
            break;
        case ETHERTYPE_ARP:
            arp_input(m);
            break;
        default:
            m_freem(m);
            break;
    }
}

int ether_output(struct ifnet* ifp, struct mbuf* m, const uint8_t* dst, uint16_t type) {
    struct mbuf* m_hdr;
    struct ether_header* eh;
    
    m_hdr = m_get(MT_HEADER);
    if (m_hdr == NULL) {
//        print_string("  ETH: m_get failed\n");
        m_freem(m);
        return -1;
    }
    
    m_hdr->m_next = m;
    m_hdr->m_len = ETHER_HDR_LEN;
    m_hdr->m_data = m_hdr->m_dat;
    
    eh = (struct ether_header*)m_hdr->m_data;
    memcpy(eh->ether_dhost, dst, ETHER_ADDR_LEN);
    memcpy(eh->ether_shost, ifp->if_hwaddr, ETHER_ADDR_LEN);
    eh->ether_type = htons(type);

    /* Update total packet length if it's a packet header */
    if (m_hdr->m_flags & M_PKTHDR) {
        m_hdr->m_pkthdr.len = m->m_pkthdr.len + ETHER_HDR_LEN;
    }
    
    /* Pass to driver */
    return ifp->if_output(ifp, m_hdr);
}
