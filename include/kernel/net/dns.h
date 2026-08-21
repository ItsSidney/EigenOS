/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#ifndef _KERNEL_NET_DNS_H
#define _KERNEL_NET_DNS_H

#include <stdint.h>

#define DNS_PORT 53

struct dns_header {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} __attribute__((packed));

#define DNS_FLAG_QR    0x8000
#define DNS_FLAG_OPCODE_STD 0x0000
#define DNS_FLAG_RD    0x0100
#define DNS_FLAG_RCODE_MASK 0x000F

#define DNS_TYPE_A     1
#define DNS_CLASS_IN   1

int dns_resolve(const char* hostname, uint32_t* ip_addr);
void dns_set_server(uint32_t ip);

/* Walk the DNS cache (one callback per valid entry). */
void dns_foreach(void (*cb)(int idx, const char* hostname, uint32_t ip));

#endif /* _KERNEL_NET_DNS_H */
