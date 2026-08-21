/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#include "kernel/net/icmp.h"
#include "kernel/net/ip.h"
#include "kernel/net/if.h"
#include "drivers/video/framebuffer.h"
#include <string.h>

/*
 * ICMP implementation for Eigen.
 */

static int gr_last_reply_valid;
static uint32_t gr_last_reply_src;
static uint16_t gr_last_reply_id;
static uint16_t gr_last_reply_seq;

int icmp_get_echo_reply(uint32_t* src, uint16_t* id, uint16_t* seq) {
    if (gr_last_reply_valid) {
        *src = gr_last_reply_src;
        *id = gr_last_reply_id;
        *seq = gr_last_reply_seq;
        gr_last_reply_valid = 0;
        return 0;
    }
    return -1;
}

uint16_t icmp_checksum(void* vdata, size_t length) {
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

void icmp_input(struct mbuf* m, int off) {
    struct ip* ip = (struct ip*)(m->m_data - off);
    struct icmphdr* icp = (struct icmphdr*)m->m_data;
    int icmplen = ntohs(ip->ip_len) - off;

    if (icp->icmp_type == ICMP_ECHO) {
        /* Convert Echo Request to Echo Reply */
        icp->icmp_type = ICMP_ECHOREPLY;
        icp->icmp_cksum = 0;
        icp->icmp_cksum = icmp_checksum(icp, icmplen);
        
        /* Swap IP addresses */
        struct in_addr tmp = ip->ip_src;
        ip->ip_src = ip->ip_dst;
        ip->ip_dst = tmp;
        
        /* Move m_data back to IP header for ip_output */
        m->m_data -= off;
        m->m_len += off;
        
        ip_output(m, NULL);
    } else if (icp->icmp_type == ICMP_ECHOREPLY) {
        gr_last_reply_src = ip->ip_src.s_addr;
        gr_last_reply_id = icp->icmp_id;
        gr_last_reply_seq = icp->icmp_seq;
        gr_last_reply_valid = 1;
        m_freem(m);
    } else {
        m_freem(m);
    }
}
