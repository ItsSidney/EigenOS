/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#ifndef _KERNEL_NET_MBUF_H
#define _KERNEL_NET_MBUF_H

#include <stdint.h>
#include <stddef.h>

struct ifnet;

/*
 * FreeBSD-style mbuf implementation for Eigen.
 * mbufs are the basic unit of memory management in the network stack.
 */

#define MSIZE       256      /* size of an mbuf */
#define MCLBYTES    2048     /* size of an mbuf cluster */

/* mbuf types */
#define MT_FREE     0        /* should be on free list */
#define MT_DATA     1        /* dynamic (data) allocation */
#define MT_HEADER   2        /* packet header */
#define MT_SONAME   3        /* socket name */
#define MT_FTABLE   4        /* fragment reassembly table */

/* mbuf flags */
#define M_PKTHDR    0x0001   /* has a packet header */
#define M_EOR       0x0002   /* end of record */
#define M_EXT       0x0004   /* has external storage */

struct m_ext {
    void*     ext_buf;       /* start of buffer */
    uint32_t  ext_size;      /* size of buffer */
};

struct m_pkthdr {
    struct ifnet* rcvif;     /* rcv interface */
    int32_t       len;       /* total packet length */
};

struct mbuf {
    struct mbuf*    m_next;     /* next mbuf in chain */
    struct mbuf*    m_nextpkt;  /* next chain in queue */
    int32_t         m_len;      /* amount of data in this mbuf */
    uint32_t        m_type;     /* type of data in this mbuf */
    uint32_t        m_flags;    /* flags */
    char*           m_data;     /* location of data */
    
    struct m_pkthdr m_pkthdr;   /* packet header */
    struct m_ext    m_ext;      /* external storage */
    
    char m_dat[MSIZE - 72];     /* internal data storage (reduced) */
};

/* Function prototypes */
struct mbuf* m_get(int type);
struct mbuf* m_gethdr(int type);
struct mbuf* m_getcl(int type);
void         m_free(struct mbuf* m);
void         m_freem(struct mbuf* m);
void         m_copydata(struct mbuf* m, int off, int len, void* cp);

#endif /* _KERNEL_NET_MBUF_H */
