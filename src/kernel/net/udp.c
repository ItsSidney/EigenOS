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
#include "kernel/net/udp.h"
#include "kernel/net/ethernet.h"
#include "kernel/net/if_arp.h"
#include <string.h>
#include "drivers/video/framebuffer.h"

#define UDP_RX_SLOTS 32
static struct {
    uint16_t port;
    uint32_t src_ip;
    uint16_t src_port;
    uint16_t len;
    uint8_t  data[1500];
    int      used;
} udp_rx[UDP_RX_SLOTS];
static int udp_next_rx = 0;

extern void itoa(uint64_t n, char* s);

static uint16_t udp_checksum(uint32_t src_ip, uint32_t dst_ip,
                             uint16_t sport, uint16_t dport,
                             const void* data, uint16_t datalen)
{
    uint32_t sum = 0;
    uint16_t udp_len = datalen + sizeof(struct udphdr);

    uint8_t ph[12];
    memcpy(&ph[0], &src_ip, 4);
    memcpy(&ph[4], &dst_ip, 4);
    ph[8] = 0;
    ph[9] = IPPROTO_UDP;
    ph[10] = (udp_len >> 8) & 0xFF;
    ph[11] = udp_len & 0xFF;

    for (int i = 0; i < 12; i += 2)
        sum += (uint16_t)ph[i] << 8 | ph[i+1];

    sum += sport;
    sum += dport;
    sum += udp_len;
    sum += 0;

    uint8_t* ptr = (uint8_t*)data;
    int odd = 0;
    uint16_t last_byte = 0;
    for (uint16_t i = 0; i < datalen; i++) {
        if (!odd) { last_byte = ptr[i]; odd = 1; }
        else { sum += (uint16_t)last_byte << 8 | ptr[i]; odd = 0; }
    }
    if (odd) sum += (uint16_t)last_byte << 8;

    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    uint16_t result = ~((uint16_t)sum);
    return result == 0 ? 0xFFFF : result;
}

void udp_input(struct mbuf* m, int off)
{
    struct udphdr* uh = (struct udphdr*)m->m_data;
    uint16_t dport = ntohs(uh->uh_dport);
    uint16_t sport = ntohs(uh->uh_sport);
    uint16_t udp_len = ntohs(uh->uh_ulen);

    if (udp_len < sizeof(struct udphdr)) {
        m_freem(m);
        return;
    }

    uint16_t payload_len = udp_len - sizeof(struct udphdr);

    struct ip* ip = (struct ip*)(m->m_data - off);

    int i = udp_next_rx;
    udp_next_rx = (udp_next_rx + 1) % UDP_RX_SLOTS;

    udp_rx[i].port = dport;
    udp_rx[i].src_ip = ip->ip_src.s_addr;
    udp_rx[i].src_port = sport;
    udp_rx[i].len = (payload_len < 1500) ? payload_len : 1500;
    memcpy(udp_rx[i].data, (uint8_t*)uh + sizeof(struct udphdr), udp_rx[i].len);
    udp_rx[i].used = 1;

    m_freem(m);
}

int udp_output(uint32_t dst_ip, uint16_t dport, uint16_t sport,
               const void* data, uint16_t datalen)
{
    struct ifnet* ifp = if_find_primary();
    if (!ifp) return -1;

    /* Keep the payload inside the 2048-byte cluster (256 headroom + 28 hdrs) */
    if (datalen > 1472) datalen = 1472;

    struct mbuf* m = m_getcl(MT_DATA);
    if (!m || !m->m_ext.ext_buf) { if (m) m_free(m); return -1; }

    m->m_data = (char*)m->m_ext.ext_buf + 256;

    if (datalen > 0 && data) {
        memcpy(m->m_data, data, datalen);
        m->m_len = datalen;
    }

    m->m_data -= sizeof(struct udphdr);
    m->m_len += sizeof(struct udphdr);
    struct udphdr* uh = (struct udphdr*)m->m_data;
    memset(uh, 0, sizeof(struct udphdr));
    uh->uh_sport = htons(sport);
    uh->uh_dport = htons(dport);
    uh->uh_ulen  = htons(datalen + sizeof(struct udphdr));

    m->m_data -= sizeof(struct ip);
    m->m_len += sizeof(struct ip);
    struct ip* ip = (struct ip*)m->m_data;
    memset(ip, 0, sizeof(struct ip));
    ip->ip_v  = 4;
    ip->ip_hl = 5;
    ip->ip_p  = IPPROTO_UDP;
    ip->ip_ttl = 64;
    static uint16_t udp_ip_id = 0;
    ip->ip_id = htons(udp_ip_id++);
    ip->ip_len = htons(m->m_len);
    ip->ip_src.s_addr = ifp->if_ip;
    ip->ip_dst.s_addr = dst_ip;

    uh->uh_sum = 0;

    return ip_output(m, ifp);
}

int udp_recv(uint16_t port, uint32_t* src_ip, uint16_t* src_port,
             void* buf, uint16_t* len)
{
    for (int i = 0; i < UDP_RX_SLOTS; i++) {
        if (udp_rx[i].used && udp_rx[i].port == port) {
            if (src_ip)  *src_ip = udp_rx[i].src_ip;
            if (src_port) *src_port = udp_rx[i].src_port;
            uint16_t copy = (udp_rx[i].len < *len) ? udp_rx[i].len : *len;
            memcpy(buf, udp_rx[i].data, copy);
            *len = copy;
            udp_rx[i].used = 0;
            udp_rx[i].port = 0;
            return 0;
        }
    }
    return -1;
}
