/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#include "kernel/net/if.h"
#include "kernel/net/mbuf.h"
#include "kernel/net/ip.h"
#include "kernel/net/in.h"
#include "kernel/net/ethernet.h"
#include "kernel/net/if_arp.h"
#include "kernel/net/tcp.h"
#include "kernel/net/icmp.h"
#include "kernel/net/udp.h"
#include <string.h>
#include "drivers/video/framebuffer.h"

/*
 * IP input/output logic for Eigen.
 */

extern void itoa(uint64_t n, char* s);

uint16_t ip_checksum(void* vdata, size_t length) {
    uint8_t* data = (uint8_t*)vdata;
    uint32_t sum = 0;

    for (; length > 1; length -= 2) {
        sum += (data[0] << 8) | data[1];
        data += 2;
    }
    if (length == 1)
        sum += (data[0] << 8);

    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);

    return htons(~sum);
}

uint16_t tcp_checksum(struct ip* ip, struct tcphdr* th, struct mbuf* m_data, int off) {
    uint16_t tcp_len = ntohs(ip->ip_len) - (ip->ip_hl << 2);
    uint8_t buf[12 + 2048];
    uint8_t* ptr = buf;

    memcpy(ptr, &ip->ip_src.s_addr, 4); ptr += 4;
    memcpy(ptr, &ip->ip_dst.s_addr, 4); ptr += 4;
    *ptr++ = 0;
    *ptr++ = IPPROTO_TCP;
    *ptr++ = (tcp_len >> 8) & 0xFF;
    *ptr++ = tcp_len & 0xFF;

    m_copydata(m_data, off, tcp_len, ptr);
    ptr += tcp_len;

    int total = (int)(ptr - buf);
    uint32_t sum = 0;
    for (int i = 0; i < total - 1; i += 2) {
        sum += (uint16_t)(buf[i]) << 8 | buf[i+1];
    }
    if (total & 1) sum += (uint16_t)(buf[total - 1]) << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return htons(~((uint16_t)sum));
}


void ip_input(struct mbuf* m) {
    struct ip* ip;
    int hlen;
    
    if (m->m_len < sizeof(struct ip)) {
//        print_string("  IP: Packet too short for IP header\n");
        m_freem(m);
        return;
    }
    
    ip = (struct ip*)m->m_data;
    
    if (ip->ip_v != 4) {
//        print_string("  IP: Not IPv4\n");
        m_freem(m);
        return;
    }
    
    hlen = ip->ip_hl << 2;
    if (hlen < sizeof(struct ip)) {
//        print_string("  IP: Invalid header length\n");
        m_freem(m);
        return;
    }
    
    /* TODO: Validate checksum */
    
    switch (ip->ip_p) {
        case IPPROTO_TCP:
            m->m_data += hlen;
            m->m_len -= hlen;
            tcp_input(m, hlen);
            break;
        case IPPROTO_ICMP:
            m->m_data += hlen;
            m->m_len -= hlen;
            icmp_input(m, hlen);
            break;
        case IPPROTO_UDP:
            m->m_data += hlen;
            m->m_len -= hlen;
            udp_input(m, hlen);
            break;
        default:
//            print_string("  IP: Unknown proto\n");
            m_freem(m);
            break;
    }
}

int ip_output(struct mbuf* m, struct ifnet* ifp) {
    struct ip* ip = (struct ip*)m->m_data;
    static uint16_t ip_id_counter = 0;
    uint8_t dest_enaddr[6];
    
    if (ifp == NULL) {
        ifp = if_find_primary();
    }
    
    if (ifp == NULL) {
//        print_string("  IP: No interface found, dropping packet\n");
        m_freem(m);
        return -1;
    }

    
    ip->ip_v = 4;
    ip->ip_hl = sizeof(struct ip) >> 2;
    ip->ip_id = htons(ip_id_counter++);
    ip->ip_off = 0;
    if (ip->ip_ttl == 0) ip->ip_ttl = 64;
    ip->ip_sum = 0;
    ip->ip_sum = ip_checksum(ip, sizeof(struct ip));

    /* Resolve hardware address via ARP */
    /* Routing: if the destination is not on the local subnet, send to the
     * interface's default gateway. Falls back to a directly-reachable host
     * (same subnet) via ARP. */
    struct in_addr dst = ip->ip_dst;
    struct in_addr next = dst;
    if (ifp->if_netmask != 0 &&
        (dst.s_addr & ifp->if_netmask) != (ifp->if_ip & ifp->if_netmask)) {
        if (ifp->if_gateway != 0) next.s_addr = ifp->if_gateway;
    }

    return arp_resolve(ifp, m, &next, dest_enaddr);
}
