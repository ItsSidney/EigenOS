/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#include "kernel/net/dns.h"
#include "kernel/net/udp.h"
#include "kernel/net/in.h"
#include <string.h>
#include <stdint.h>
#include "drivers/video/framebuffer.h"
#include "kernel/time/timer.h"
#include "kernel/net/net_log.h"

/*
 * 10.0.2.3 in network byte order on little-endian x86: htonl(0x0A000203) = 0x0302000A
  * x86 is little-endian, so 0x0A000203 in host order stored as bytes: 03 02 00 0A
  * which matches the wire representation for 10.0.2.3 */
static uint32_t dns_server = 0x0302000A; /* 10.0.2.3 (QEMU user-net DNS proxy, network byte order) */

void dns_set_server(uint32_t ip)
{
    dns_server = ip;
}

/* ---- dns cache ---- */
#define DNS_CACHE_SIZE 16
struct dns_cache_entry {
    char        hostname[64];
    uint32_t    ip;
    uint8_t     valid;
} dns_cache[DNS_CACHE_SIZE];

static int dns_cache_lookup(const char* hostname, uint32_t* ip_addr) {
    for (int i = 0; i < DNS_CACHE_SIZE; i++) {
        if (dns_cache[i].valid && strcmp(dns_cache[i].hostname, hostname) == 0) {
            *ip_addr = dns_cache[i].ip;
            return 0;
        }
    }
    return -1;
}

static void dns_cache_insert(const char* hostname, uint32_t ip) {
    int idx = 0;
    uint32_t best = 0xFFFFFFFFu;
    for (int i = 0; i < DNS_CACHE_SIZE; i++) {
        if (!dns_cache[i].valid) { idx = i; break; }
        if (dns_cache[i].ip < best) { best = dns_cache[i].ip; idx = i; }
    }
    strncpy(dns_cache[idx].hostname, hostname, 63);
    dns_cache[idx].hostname[63] = 0;
    dns_cache[idx].ip = ip;
    dns_cache[idx].valid = 1;
}

static int parse_dns_name(const uint8_t* msg, int msg_len, int off, char* out, int out_len)
{
    int out_pos = 0;
    int labels = 0;
    while (off < msg_len) {
        uint8_t b = msg[off++];
        if (b == 0) {
            if (out_pos == 0) { out[0] = 0; return off; }   /* root / empty name */
            out[out_pos] = 0;
            return off;
        }
        if ((b & 0xC0) == 0xC0) {           /* compression pointer */
            if (off >= msg_len) return -1;
            int ptr = ((b & 0x3F) << 8) | msg[off++];
            char tmp[256];
            int r = parse_dns_name(msg, msg_len, ptr, tmp, sizeof(tmp));
            if (r < 0) return -1;
            int len = strlen(tmp);
            if (out_pos + len + 1 >= out_len) return -1;
            if (len) { memcpy(out + out_pos, tmp, len); out_pos += len; }
            out[out_pos] = 0;
            return off;
        }
        if ((b & 0xC0) != 0) return -1;     /* unknown label type */
        if (off + b > msg_len) return -1;
        if (out_pos + b + 1 >= out_len) return -1;
        memcpy(out + out_pos, msg + off, b);
        out_pos += b;
        out[out_pos++] = '.';
        off += b;
        labels++;
        if (labels > 16) return -1;
    }
    out[out_pos > 0 ? out_pos - 1 : 0] = 0;  /* strip trailing dot */
    return off;
}

static int encode_dns_name(uint8_t* dst, const char* hostname)
{
    int written = 0;
    while (*hostname) {
        const char* dot = hostname;
        while (*dot && *dot != '.') dot++;
        int label_len = dot - hostname;
        if (label_len > 63) return -1;
        dst[written++] = (uint8_t)label_len;
        memcpy(dst + written, hostname, label_len);
        written += label_len;
        hostname = *dot ? dot + 1 : dot;
    }
    dst[written++] = 0;
    return written;
}

/*
 * Send a single A-record query to one DNS server and collect the reply.
 * Returns 0 + ip_addr on success, -1 otherwise.
 */
static int dns_query_once(uint32_t server, const char* hostname, uint32_t* ip_addr)
{
    uint8_t query[512];
    memset(query, 0, sizeof(query));

    struct dns_header* hdr = (struct dns_header*)query;
    static uint16_t query_id = 1;
    hdr->id      = htons(query_id++);
    hdr->flags   = htons(DNS_FLAG_RD);
    hdr->qdcount = htons(1);

    int off = (int)sizeof(struct dns_header);
    int enc_len = encode_dns_name(query + off, hostname);
    if (enc_len < 0) {
        net_log_addf("DNS", 0xF85149, "Encode failed for '%s'", hostname);
        return -1;
    }
    off += enc_len;
    query[off++] = (DNS_TYPE_A >> 8) & 0xFF;
    query[off++] = DNS_TYPE_A & 0xFF;
    query[off++] = (DNS_CLASS_IN >> 8) & 0xFF;
    query[off++] = DNS_CLASS_IN & 0xFF;

    int query_len = off;
    uint16_t sport = 30000 + (query_id % 1000);

    uint32_t s_h = ntohl(server);
    net_log_addf("DNS", 0x58A6FF, "Query '%s' via %u.%u.%u.%u (port %u)",
        hostname, (s_h>>24)&0xFF, (s_h>>16)&0xFF, (s_h>>8)&0xFF, s_h&0xFF, sport);

    if (udp_output(server, DNS_PORT, sport, query, query_len) < 0) {
        net_log_addf("DNS", 0xF85149, "UDP send failed for '%s'", hostname);
        return -1;
    }

    /* Flush any stale RX entries for our port from previous calls */
    {
        uint8_t dummy[64];
        uint16_t dummy_len = sizeof(dummy);
        uint32_t dummy_ip;
        uint16_t dummy_port;
        while (udp_recv(sport, &dummy_ip, &dummy_port, dummy, &dummy_len) == 0) {
            dummy_len = sizeof(dummy);
        }
    }

    int timeout = 100;
    int retry = 20;
    while (timeout > 0) {
        extern void net_poll(void);
        net_poll();

        uint8_t reply[512];
        uint16_t reply_len = sizeof(reply);
        uint32_t src_ip;
        uint16_t src_port;
        if (udp_recv(sport, &src_ip, &src_port, reply, &reply_len) == 0) {
            if (reply_len < (uint16_t)sizeof(struct dns_header)) {
                net_log_addf("DNS", 0xF85149, "Reply too short (%u bytes)", reply_len);
                return -1;
            }

            struct dns_header* rh = (struct dns_header*)reply;
            uint16_t rflags = ntohs(rh->flags);
            uint16_t rcode  = rflags & DNS_FLAG_RCODE_MASK;

            if (rcode != 0) {
                net_log_addf("DNS", 0xF85149, "Server error rcode=%u for '%s'", rcode, hostname);
                return -1;
            }

            uint16_t ancount = ntohs(rh->ancount);
            if (ancount == 0) {
                net_log_addf("DNS", 0xD29922, "No A records for '%s'", hostname);
                return -1;
            }

            off = (int)sizeof(struct dns_header);
            int qcount = ntohs(rh->qdcount);
            for (int i = 0; i < qcount; i++) {
                char tmp[256];
                off = parse_dns_name(reply, reply_len, off, tmp, sizeof(tmp));
                if (off < 0) return -1;
                off += 4;  /* type(2) + class(2) */
            }

            for (int i = 0; i < ancount; i++) {
                char tmp[256];
                off = parse_dns_name(reply, reply_len, off, tmp, sizeof(tmp));
                if (off < 0) return -1;

                if (off + 10 > (int)reply_len) return -1;

                uint16_t type = (reply[off] << 8) | reply[off + 1];
                off += 2;  /* type */
                off += 2;  /* class */
                off += 4;  /* ttl */
                uint16_t rdlength = (reply[off] << 8) | reply[off + 1];
                off += 2;  /* rdlength */

                if (type == DNS_TYPE_A && rdlength == 4 && off + 4 <= (int)reply_len) {
                    uint32_t addr = 0;
                    addr |= (uint32_t)reply[off] << 24;
                    addr |= (uint32_t)reply[off + 1] << 16;
                    addr |= (uint32_t)reply[off + 2] << 8;
                    addr |= (uint32_t)reply[off + 3];
                    *ip_addr = htonl(addr);

                    dns_cache_insert(hostname, *ip_addr);

                    uint32_t h = addr;
                    net_log_addf("DNS", 0x3FB950, "Resolved '%s' -> %u.%u.%u.%u (via %.1f.%.1f.%.1f.%.1f)",
                        hostname, (h>>24)&0xFF, (h>>16)&0xFF, (h>>8)&0xFF, h&0xFF,
                        (s_h>>24)&0xFF, (s_h>>16)&0xFF, (s_h>>8)&0xFF, s_h&0xFF);
                    return 0;
                }

                off += rdlength;
            }

            return -1;  /* no A record in this answer */
        }

        extern void sleep_task(uint32_t ms);
        sleep_task(50);
        timeout--;

        retry--;
        if (retry <= 0) {
            retry = 20;
            udp_output(server, DNS_PORT, sport, query, query_len);
        }
    }

    net_log_addf("DNS", 0xF85149, "Timeout resolving '%s' via %.1f.%.1f.%.1f.%.1f",
        hostname, (s_h>>24)&0xFF, (s_h>>16)&0xFF, (s_h>>8)&0xFF, s_h&0xFF);
    return -1;
}

/* Last-resort whitelist so common demo URLs resolve even when no DNS
 * server is reachable (e.g. bare-metal LAN without DHCP yet). */
static int dns_fallback_whitelist(const char* hostname, uint32_t* ip_addr) {
    static const struct { const char* name; uint32_t ip; } fb[] = {
        {"google.com",       htonl(0xD83AD6CE)},  /* 216.58.214.206 */
        {"example.com",      htonl(0x5DB8D822)},  /* 93.184.216.34 */
        {"github.com",       htonl(0xC01EFF71)},  /* 192.30.255.113 */
        {"open-meteo.com",   htonl(0x681A0E6F)},  /* 104.26.14.111 */
        {"api.open-meteo.com", htonl(0x681A0E6F)},
        {0, 0}
    };
    for (int i = 0; fb[i].name; i++) {
        if (strcmp(hostname, fb[i].name) == 0) {
            *ip_addr = fb[i].ip;
            dns_cache_insert(hostname, *ip_addr);
            uint32_t h = ntohl(fb[i].ip);
            net_log_addf("DNS", 0xD29922, "Fallback IP for '%s' -> %u.%u.%u.%u",
                hostname, (h>>24)&0xFF, (h>>16)&0xFF, (h>>8)&0xFF, h&0xFF);
            return 0;
        }
    }
    return -1;
}

int dns_resolve(const char* hostname, uint32_t* ip_addr)
{
    if (!hostname || !ip_addr) return -1;

    /* Check if hostname is already an IPv4 address (e.g. "1.1.1.1") */
    int is_ip = 1;
    uint32_t ip_val = 0;
    int octets = 0;
    int cur_val = 0;
    for (int i = 0; hostname[i]; i++) {
        if (hostname[i] >= '0' && hostname[i] <= '9') {
            cur_val = cur_val * 10 + (hostname[i] - '0');
        } else if (hostname[i] == '.') {
            ip_val = (ip_val << 8) | (cur_val & 0xFF);
            cur_val = 0;
            octets++;
        } else {
            is_ip = 0; break;
        }
    }
    if (is_ip && octets == 3) {
        ip_val = (ip_val << 8) | (cur_val & 0xFF);
        *ip_addr = htonl(ip_val);
        return 0;
    }

    /* Check DNS cache first */
    if (dns_cache_lookup(hostname, ip_addr) == 0) {
        uint32_t h = ntohl(*ip_addr);
        net_log_addf("DNS", 0x3FB950, "Cache Hit: %s -> %u.%u.%u.%u",
            hostname, (h>>24)&0xFF, (h>>16)&0xFF, (h>>8)&0xFF, h&0xFF);
        return 0;
    }

    /* Try a list of resolvers: the configured server first, then
     * well-known public resolvers (1.1.1.1, 8.8.8.8) as a fallback so
     * that HTTPS still works on real LANs when the DHCP-learned DNS
     * server is unreachable. */
    static const uint32_t resolvers[] = {
        0,                       /* filled below: dns_server */
        htonl(0x01010101),       /* 1.1.1.1 */
        htonl(0x08080808),       /* 8.8.8.8 */
    };
    for (int s = 0; s < (int)(sizeof(resolvers)/sizeof(resolvers[0])); s++) {
        uint32_t server = (s == 0) ? dns_server : resolvers[s];
        if (server == 0) continue;
        if (dns_query_once(server, hostname, ip_addr) == 0) {
            return 0;
        }
        /* try next resolver */
    }

    /* All resolvers failed — try a small hardcoded whitelist as a last
     * resort so common demo URLs still resolve. */
    if (dns_fallback_whitelist(hostname, ip_addr) == 0) {
        return 0;
    }

    net_log_addf("DNS", 0xF85149, "All resolvers failed for '%s'", hostname);
    return -1;
}

/* Expose the DNS cache for `netstat`. */
void dns_foreach(void (*cb)(int idx, const char* hostname, uint32_t ip)) {
    for (int i = 0; i < DNS_CACHE_SIZE; i++) {
        if (dns_cache[i].valid)
            cb(i, dns_cache[i].hostname, dns_cache[i].ip);
    }
}
