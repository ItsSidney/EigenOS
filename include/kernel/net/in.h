/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#ifndef _KERNEL_NET_IN_H
#define _KERNEL_NET_IN_H

#include <stdint.h>

/*
 * Internet address definitions following FreeBSD's <netinet/in.h>
 */

typedef uint32_t in_addr_t;

struct in_addr {
    in_addr_t s_addr;
};

#define IPPROTO_IP      0
#define IPPROTO_ICMP    1
#define IPPROTO_TCP     6
#define IPPROTO_UDP     17

#define INADDR_ANY       0x00000000
#define INADDR_BROADCAST 0xffffffff
#define INADDR_NONE      0xffffffff
#define INADDR_LOOPBACK  0x7f000001

struct sockaddr_in {
    uint8_t        sin_len;
    uint8_t        sin_family;
    uint16_t       sin_port;
    struct in_addr sin_addr;
    char           sin_zero[8];
};

#define AF_INET 2

/* Byte swapping macros */
#define htons(n) ((((uint16_t)(n) & 0xFF00) >> 8) | (((uint16_t)(n) & 0x00FF) << 8))
#define ntohs(n) htons(n)
#define htonl(n) ((((uint32_t)(n) & 0xFF000000) >> 24) | \
                  (((uint32_t)(n) & 0x00FF0000) >> 8) | \
                  (((uint32_t)(n) & 0x0000FF00) << 8) | \
                  (((uint32_t)(n) & 0x000000FF) << 24))
#define ntohl(n) htonl(n)

#endif /* _KERNEL_NET_IN_H */
