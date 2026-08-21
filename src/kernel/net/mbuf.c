/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#include "kernel/net/mbuf.h"
#include "kernel/mem/kheap.h"
#include "drivers/video/framebuffer.h" /* for print_string */
#include <string.h>

/*
 * Implementation of FreeBSD-style mbufs for Eigen.
 */

struct mbuf* m_get(int type) {
    struct mbuf* m = kmalloc(sizeof(struct mbuf));
    if (m == NULL) return NULL;
    
    m->m_next = NULL;
    m->m_nextpkt = NULL;
    m->m_len = 0;
    m->m_type = type;
    m->m_flags = 0;
    m->m_data = m->m_dat;
    
    return m;
}

struct mbuf* m_gethdr(int type) {
    struct mbuf* m = m_get(type);
    if (m == NULL) return NULL;
    
    m->m_flags |= M_PKTHDR;
    m->m_pkthdr.rcvif = NULL;
    m->m_pkthdr.len = 0;
    
    return m;
}

struct mbuf* m_getcl(int type) {
    struct mbuf* m = m_get(type);
    if (m == NULL) return NULL;
    
    void* cluster = kmalloc_aligned(MCLBYTES, 16);
    if (cluster == NULL) {
        kfree(m);
        return NULL;
    }
    memset(cluster, 0, MCLBYTES);
    
    m->m_flags |= M_EXT;
    m->m_ext.ext_buf = cluster;
    m->m_ext.ext_size = MCLBYTES;
    m->m_data = cluster;
    
    return m;
}

void m_free(struct mbuf* m) {
    if (m == NULL) return;
    
    if (m->m_flags & M_EXT) {
        kfree_aligned(m->m_ext.ext_buf);
    }
    
    kfree(m);
}

void m_freem(struct mbuf* m) {
    struct mbuf* n;
    
    while (m != NULL) {
        n = m->m_next;
        m_free(m);
        m = n;
    }
}

void m_copydata(struct mbuf* m, int off, int len, void* cp) {
    int count;
    char* cpd = (char*)cp;

    while (off > 0) {
        if (m == NULL) return;
        if (off < m->m_len)
            break;
        off -= m->m_len;
        m = m->m_next;
    }
    while (len > 0) {
        if (m == NULL) return;
        count = (m->m_len - off < len) ? m->m_len - off : len;
        memcpy(cpd, m->m_data + off, count);
        len -= count;
        cpd += count;
        off = 0;
        m = m->m_next;
    }
}
