/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#include "kernel/net/if.h"
#include "kernel/net/in.h"
#include "kernel/net/udp.h"
#include "kernel/net/mbuf.h"
#include "kernel/net/dhcp.h"
#include "kernel/net/net_log.h"
#include "kernel/lib/stdio.h"
#include "kernel/time/timer.h"
#include "kernel/task/task.h"
#include "drivers/video/framebuffer.h"
#include <string.h>

/* DHCP message types */
#define DHCP_DISCOVER 1
#define DHCP_OFFER    2
#define DHCP_REQUEST  3
#define DHCP_ACK      5
#define DHCP_NAK      6

#define DHCP_SERVER_PORT 67
#define DHCP_CLIENT_PORT 68

#define DHCP_HDR_MAGIC 0x63825363u

/* DHCP option tags */
#define DHCP_OPT_SUBNET   1
#define DHCP_OPT_ROUTER   3
#define DHCP_OPT_DNS      6
#define DHCP_OPT_REQIP    50
#define DHCP_OPT_LEASET   51
#define DHCP_OPT_MSGTYPE  53
#define DHCP_OPT_SERVERID 54
#define DHCP_OPT_END      255

static uint32_t g_xid;          /* transaction id for the active exchange */
static uint8_t  g_mac[6];
static uint32_t g_offered_ip;
static uint32_t g_server_ip;

uint32_t dhcp_broadcast_addr(void) { return 0xFFFFFFFFu; }

/* minimal BOOTP/DHCP header (we only fill what we need) */
struct dhcp_pkt {
    uint8_t  op;      uint8_t htype; uint8_t hlen;  uint8_t hops;
    uint32_t xid;
    uint16_t secs;    uint16_t flags;
    uint32_t ciaddr;  uint32_t yiaddr; uint32_t siaddr; uint32_t giaddr;
    uint8_t  chaddr[16];
    uint8_t  sname[64];
    uint8_t  file[128];
    uint32_t magic;
    uint8_t  options[312];
};

static void dhcp_add_option(uint8_t* o, int* off, uint8_t tag, uint8_t len, const void* val) {
    o[(*off)++] = tag;
    o[(*off)++] = len;
    memcpy(o + *off, val, len);
    *off += len;
}

/* Parse the option block of a received DHCP message. */
static void dhcp_parse_options(const uint8_t* opt, int len,
                                uint32_t* mask, uint32_t* gw, uint32_t* dns) {
    int i = 0;
    while (i < len) {
        uint8_t tag = opt[i];
        if (tag == DHCP_OPT_END) break;
        if (tag == 0) { i++; continue; }
        uint8_t olen = opt[i + 1];
        const uint8_t* v = opt + i + 2;
        if (tag == DHCP_OPT_SUBNET && olen >= 4)      *mask = *(const uint32_t*)v;
        else if (tag == DHCP_OPT_ROUTER && olen >= 4) *gw   = *(const uint32_t*)v;
        else if (tag == DHCP_OPT_DNS && olen >= 4)    *dns  = *(const uint32_t*)v;
        i += 2 + olen;
    }
}

static int dhcp_send(int msgtype, uint32_t req_ip, uint32_t server_ip) {
    struct mbuf* m = m_getcl(MT_DATA);
    if (!m || !m->m_ext.ext_buf) { if (m) m_free(m); return -1; }
    m->m_data = (char*)m->m_ext.ext_buf + 256;
    memset(m->m_data, 0, sizeof(struct dhcp_pkt));
    struct dhcp_pkt* p = (struct dhcp_pkt*)m->m_data;
    m->m_len = sizeof(struct dhcp_pkt);

    p->op   = 1;            /* BOOTREQUEST */
    p->htype = 1;           /* ethernet */
    p->hlen = 6;
    p->xid  = htonl(g_xid);
    p->flags = htons(0x8000); /* broadcast flag */
    memcpy(p->chaddr, g_mac, 6);
    p->magic = htonl(DHCP_HDR_MAGIC);

    int off = 0;
    uint8_t mt = (uint8_t)msgtype;
    dhcp_add_option(p->options, &off, DHCP_OPT_MSGTYPE, 1, &mt);
    if (msgtype == DHCP_REQUEST && req_ip) {
        uint32_t rip = htonl(req_ip);
        dhcp_add_option(p->options, &off, DHCP_OPT_REQIP, 4, &rip);
    }
    if (msgtype == DHCP_REQUEST && server_ip) {
        uint32_t sip = htonl(server_ip);
        dhcp_add_option(p->options, &off, DHCP_OPT_SERVERID, 4, &sip);
    }
    /* client identifier (mac) so the server ties the lease to us */
    dhcp_add_option(p->options, &off, 61, 7, g_mac); /* type 1 + 6 bytes */
    p->options[off++] = DHCP_OPT_END;

    return udp_output(dhcp_broadcast_addr(), DHCP_SERVER_PORT, DHCP_CLIENT_PORT, p, m->m_len);
}

static int dhcp_recv(uint32_t want_xid, int* out_msgtype, uint32_t* out_yiaddr,
                     uint32_t* mask, uint32_t* gw, uint32_t* dns, uint32_t* server_ip) {
    uint8_t buf[sizeof(struct dhcp_pkt)];
    uint16_t len = sizeof(buf);
    uint32_t src_ip; uint16_t src_port;
    if (udp_recv(DHCP_CLIENT_PORT, &src_ip, &src_port, buf, &len) != 0) return -1;
    if (len < (int)sizeof(struct dhcp_pkt)) return -1;

    struct dhcp_pkt* p = (struct dhcp_pkt*)buf;
    if (ntohl(p->xid) != want_xid) return -1;
    if (ntohl(p->magic) != DHCP_HDR_MAGIC) return -1;
    /* server must reply from port 67 */
    if (src_port != DHCP_SERVER_PORT) return -1;

    *out_yiaddr = ntohl(p->yiaddr);
    *server_ip  = src_ip;

    /* find message type + options */
    int off = 0;
    *out_msgtype = 0;
    while (off < len - (int)sizeof(struct dhcp_pkt) + (int)sizeof(p->options)) {
        if (off < 0 || off + 1 >= (int)sizeof(p->options)) break;
        uint8_t tag = p->options[off];
        if (tag == DHCP_OPT_END) break;
        if (tag == 0) { off++; continue; }
        uint8_t olen = p->options[off + 1];
        if (off + 2 + olen > (int)sizeof(p->options)) break;
        const uint8_t* v = p->options + off + 2;
        if (tag == DHCP_OPT_MSGTYPE) *out_msgtype = v[0];
        else if (tag == DHCP_OPT_SUBNET && olen >= 4) *mask = ntohl(*(const uint32_t*)v);
        else if (tag == DHCP_OPT_ROUTER  && olen >= 4) *gw   = ntohl(*(const uint32_t*)v);
        else if (tag == DHCP_OPT_DNS     && olen >= 4) *dns  = ntohl(*(const uint32_t*)v);
        off += 2 + olen;
    }
    return 0;
}

int dhcp_request(struct ifnet* ifp) {
    if (!ifp) return -1;
    memcpy(g_mac, ifp->if_hwaddr, 6);
    g_xid = 0x12345678u + (ifp->if_hwaddr[5] << 8) + ifp->if_hwaddr[4];

    net_log_add("DHCP", "Sending DISCOVER (broadcast)...", 0x39D2C0);

    uint32_t mask = 0, gw = 0, dns = 0, yiaddr = 0, server = 0;
    int msgtype = 0;

    if (dhcp_send(DHCP_DISCOVER, 0, 0) < 0) {
        net_log_add("DHCP", "DISCOVER send failed", 0xF85149);
        return -1;
    }

    /* Wait for an OFFER (with retries) */
    int got_offer = 0;
    for (int tries = 0; tries < 8 && !got_offer; tries++) {
        uint32_t t0 = timer_get_ms();
        while (timer_get_ms() - t0 < 1500) {
            extern void net_poll(void);
            net_poll();
            if (dhcp_recv(g_xid, &msgtype, &yiaddr, &mask, &gw, &dns, &server) == 0) {
                if (msgtype == DHCP_OFFER && yiaddr) { got_offer = 1; break; }
            }
            sleep_task(10);
        }
        if (!got_offer) dhcp_send(DHCP_DISCOVER, 0, 0);
    }

    if (!got_offer) {
        net_log_add("DHCP", "No DHCPOFFER received (no server on link)", 0xF0883E);
        return -1;
    }

    net_log_addf("DHCP", 0x3FB950, "OFFER %u.%u.%u.%u from %u.%u.%u.%u",
        (yiaddr>>24)&0xFF, (yiaddr>>16)&0xFF, (yiaddr>>8)&0xFF, yiaddr&0xFF,
        (server>>24)&0xFF, (server>>16)&0xFF, (server>>8)&0xFF, server&0xFF);

    /* REQUEST the offered address */
    if (dhcp_send(DHCP_REQUEST, yiaddr, server) < 0) {
        net_log_add("DHCP", "REQUEST send failed", 0xF85149);
        return -1;
    }

    int got_ack = 0;
    for (int tries = 0; tries < 8 && !got_ack; tries++) {
        uint32_t t0 = timer_get_ms();
        while (timer_get_ms() - t0 < 1500) {
            extern void net_poll(void);
            net_poll();
            uint32_t m2 = 0, g2 = 0, d2 = 0, y2 = 0, s2 = 0;
            if (dhcp_recv(g_xid, &msgtype, &y2, &m2, &g2, &d2, &s2) == 0) {
                if (msgtype == DHCP_ACK && y2) {
                    mask = m2; gw = g2; dns = d2; yiaddr = y2; server = s2;
                    got_ack = 1; break;
                } else if (msgtype == DHCP_NAK) {
                    net_log_add("DHCP", "NAK from server", 0xF85149);
                    return -1;
                }
            }
            sleep_task(10);
        }
        if (!got_ack) dhcp_send(DHCP_REQUEST, yiaddr, server);
    }

    if (!got_ack) {
        net_log_add("DHCP", "No DHCPACK (lease not granted)", 0xF85149);
        return -1;
    }

    /* Apply configuration.
     * dhcp_recv() returns mask/gw/dns in host byte order (it ntohl()s the wire
     * values), so convert them back to network order here to match every other
     * consumer (and the static-fallback path), which stores if_* in network
     * order. if_ip is already stored via htonl(yiaddr) below. */
    ifp->if_ip      = htonl(yiaddr);
    ifp->if_netmask = mask ? htonl(mask) : htonl(0xFFFFFF00u); /* default /24 */
    ifp->if_gateway = gw   ? htonl(gw)   : 0;
    ifp->if_dns     = dns  ? htonl(dns)  : htonl(0x0A000203u);  /* fallback 10.0.2.3 */
    ifp->if_dhcp    = 1;

    net_log_addf("DHCP", 0x3FB950,
        "Leased %u.%u.%u.%u  mask %u.%u.%u.%u  gw %u.%u.%u.%u  dns %u.%u.%u.%u",
        (yiaddr>>24)&0xFF, (yiaddr>>16)&0xFF, (yiaddr>>8)&0xFF, yiaddr&0xFF,
        (ntohl(mask?mask:htonl(0xFFFFFF00u))>>24)&0xFF,
        (ntohl(mask?mask:htonl(0xFFFFFF00u))>>16)&0xFF,
        (ntohl(mask?mask:htonl(0xFFFFFF00u))>>8)&0xFF,
        (ntohl(mask?mask:htonl(0xFFFFFF00u)))&0xFF,
        (ntohl(gw)>>24)&0xFF, (ntohl(gw)>>16)&0xFF, (ntohl(gw)>>8)&0xFF, ntohl(gw)&0xFF,
        (ntohl(dns?dns:htonl(0x0A000203u))>>24)&0xFF,
        (ntohl(dns?dns:htonl(0x0A000203u))>>16)&0xFF,
        (ntohl(dns?dns:htonl(0x0A000203u))>>8)&0xFF,
        (ntohl(dns?dns:htonl(0x0A000203u)))&0xFF);

    /* Tell the DNS resolver to use the learned server */
    extern int dns_set_server(uint32_t);
    dns_set_server(ifp->if_dns);
    return 0;
}
