/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#include "kernel/net/if.h"
#include "kernel/net/mbuf.h"
#include "kernel/net/if_arp.h"
#include "kernel/net/ethernet.h"
#include "kernel/net/in.h"
#include <string.h>
#include "drivers/video/framebuffer.h"
#include "kernel/lib/stdio.h"

/*
 * ARP logic for Eigen.
 */

struct arp_entry {
    struct in_addr ip;
    uint8_t mac[6];
    int valid;
    int is_static;
};

#define ARP_CACHE_SIZE 16
static struct arp_entry arp_cache[ARP_CACHE_SIZE];

#define ARP_PENDING_MAX 4
static struct {
    struct in_addr dst;
    struct mbuf *m;
} arp_pending[ARP_PENDING_MAX];
static int arp_pending_count = 0;

static int arp_find_or_alloc(uint32_t ip) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip.s_addr == ip) return i;
    }
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!arp_cache[i].valid) return i;
    }
    return -1;
}

void arp_input(struct mbuf* m) {
    struct ether_arp* ea;
    struct ifnet* ifp = m->m_pkthdr.rcvif;
    
    if (m->m_len < sizeof(struct ether_arp)) {
//        print_string("  ARP: Too short\n");
        m_freem(m);
        return;
    }
    
    ea = (struct ether_arp*)m->m_data;
    uint16_t op = ntohs(ea->ea_hdr.ar_op);

    #include "kernel/net/net_log.h"

    if (op == ARPOP_REPLY) {
        uint32_t reply_ip = *(uint32_t*)ea->arp_spa;
        uint32_t tgt_ip = *(uint32_t*)ea->arp_tpa;
        uint8_t reply_mac[6];
        memcpy(reply_mac, ea->arp_sha, 6);
        
        uint32_t r_h = ntohl(reply_ip);
        net_log_addf("ARP", 0x3FB950, "Reply: %u.%u.%u.%u is at %02X:%02X:%02X:%02X:%02X:%02X",
            (r_h>>24)&0xFF, (r_h>>16)&0xFF, (r_h>>8)&0xFF, r_h&0xFF,
            reply_mac[0], reply_mac[1], reply_mac[2], reply_mac[3], reply_mac[4], reply_mac[5]);
        
        int idx = arp_find_or_alloc(reply_ip);
        if (idx >= 0) {
            if (!arp_cache[idx].is_static) {
                memcpy(&arp_cache[idx].ip, &reply_ip, 4);
                memcpy(arp_cache[idx].mac, reply_mac, 6);
                arp_cache[idx].valid = 1;
            }
        }
        
        for (int i = 0; i < arp_pending_count; i++) {
            if (arp_pending[i].m && arp_pending[i].dst.s_addr == reply_ip) {
                ether_output(ifp, arp_pending[i].m, reply_mac, ETHERTYPE_IP);
                arp_pending[i].m = NULL;
            }
        }
        /* Compact the pending queue to remove NULL entries */
        int new_count = 0;
        for (int i = 0; i < arp_pending_count; i++) {
            if (arp_pending[i].m != NULL) {
                if (new_count != i) {
                    arp_pending[new_count] = arp_pending[i];
                }
                new_count++;
            }
        }
        arp_pending_count = new_count;
    } else if (op == ARPOP_REQUEST) {
        uint32_t req_ip = *(uint32_t*)ea->arp_spa;
        uint32_t tgt_ip = *(uint32_t*)ea->arp_tpa;
        uint32_t t_h = ntohl(tgt_ip);
        
        net_log_addf("ARP", 0x58A6FF, "Request for %u.%u.%u.%u",
            (t_h>>24)&0xFF, (t_h>>16)&0xFF, (t_h>>8)&0xFF, t_h&0xFF);
        
        int idx = arp_find_or_alloc(req_ip);
        if (idx >= 0 && !arp_cache[idx].is_static) {
            memcpy(&arp_cache[idx].ip, &req_ip, 4);
            memcpy(arp_cache[idx].mac, ea->arp_sha, 6);
            arp_cache[idx].valid = 1;
        }
        
        if (memcmp(ea->arp_tpa, &ifp->if_ip, 4) == 0) {
            struct mbuf* am = m_gethdr(MT_DATA);
            if (!am) { m_freem(m); return; }
            
            am->m_data += sizeof(struct ether_header);
            struct ether_arp* reply = (struct ether_arp*)am->m_data;
            
            reply->ea_hdr.ar_hrd = htons(ARPHRD_ETHER);
            reply->ea_hdr.ar_pro = htons(ETHERTYPE_IP);
            reply->ea_hdr.ar_hln = 6;
            reply->ea_hdr.ar_pln = 4;
            reply->ea_hdr.ar_op = htons(ARPOP_REPLY);
            
            memcpy(reply->arp_sha, ifp->if_hwaddr, 6);
            memcpy(reply->arp_spa, &ifp->if_ip, 4);
            memcpy(reply->arp_tha, ea->arp_sha, 6);
            memcpy(reply->arp_tpa, ea->arp_spa, 4);
            
            am->m_len = sizeof(struct ether_arp);
            ether_output(ifp, am, ea->arp_sha, ETHERTYPE_ARP);
        }
    }
    
    m_freem(m);
}

/* Expose the ARP cache for `netstat`. `cb` is called per valid entry. */
void arp_foreach(void (*cb)(int idx, uint32_t ip, const uint8_t* mac, int is_static)) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid)
            cb(i, arp_cache[i].ip.s_addr, arp_cache[i].mac, arp_cache[i].is_static);
    }
}

int arp_resolve(struct ifnet* ifp, struct mbuf* m, const struct in_addr* dst, uint8_t* dest_enaddr) {
    (void)dest_enaddr;

    /* Broadcast destinations must be sent straight out on the wire via the
     * broadcast MAC -- they must NOT be pushed through ARP resolution.
     * Previously these were queued in the (tiny, 4-slot) pending table, which
     * saturated it with retries and then starved real unicast resolutions
     * (ping/DNS to the gateway) -- causing every connection to "fail in 0ms"
     * because arp_resolve started returning -1 the moment the queue filled. */
    if (dst->s_addr == htonl(INADDR_BROADCAST)) {
        uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        ether_output(ifp, m, bcast, ETHERTYPE_IP);
        return 0;
    }
    /* Subnet-directed broadcast for this interface (255.255.255.255 style
     * covers the wildcard; the per-subnet bcast below covers e.g. 10.0.2.255). */
    if (ifp->if_netmask != 0) {
        uint32_t bcast_ip = (ifp->if_ip & ifp->if_netmask) | ~ifp->if_netmask;
        if (dst->s_addr == bcast_ip) {
            uint8_t bcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
            ether_output(ifp, m, bcast_mac, ETHERTYPE_IP);
            return 0;
        }
    }

    /* If this destination is already waiting for an ARP reply, just queue the
     * new packet behind the existing outstanding request instead of spawning a
     * duplicate (which would waste scarce pending slots and flood the wire). */
    for (int i = 0; i < arp_pending_count; i++) {
        if (arp_pending[i].dst.s_addr == dst->s_addr) {
            /* An ARP request for this peer is already in flight. Drop this
             * duplicate packet rather than re-flooding (the original will be
             * retransmitted/re-handled by the caller on timeout). */
            m_freem(m);
            return -1;
        }
    }

    int hit_idx = -1;
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip.s_addr == dst->s_addr) {
            hit_idx = i;
            break;
        }
    }

    if (hit_idx >= 0) {
        ether_output(ifp, m, arp_cache[hit_idx].mac, ETHERTYPE_IP);
        return 0;
    }

    if (arp_pending_count >= ARP_PENDING_MAX) {
        /* All pending slots busy; we have no room to start a new resolution.
         * Drop this one so the caller can retry later. */
        m_freem(m);
        return -1;
    }

    arp_pending[arp_pending_count].dst = *dst;
    arp_pending[arp_pending_count].m = m;
    arp_pending_count++;

    struct mbuf* am = m_gethdr(MT_DATA);
    if (!am) return -1;

    am->m_data += sizeof(struct ether_header);
    struct ether_arp* ea = (struct ether_arp*)am->m_data;
    memset(ea, 0, sizeof(struct ether_arp));
    ea->ea_hdr.ar_hrd = htons(ARPHRD_ETHER);
    ea->ea_hdr.ar_pro = htons(ETHERTYPE_IP);
    ea->ea_hdr.ar_hln = 6;
    ea->ea_hdr.ar_pln = 4;
    ea->ea_hdr.ar_op = htons(ARPOP_REQUEST);
    memcpy(ea->arp_sha, ifp->if_hwaddr, 6);
    memcpy(ea->arp_spa, &ifp->if_ip, 4);
    memcpy(ea->arp_tpa, dst, 4);

    am->m_len = sizeof(struct ether_arp);
    uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    ether_output(ifp, am, bcast, ETHERTYPE_ARP);
    return 0;
}
