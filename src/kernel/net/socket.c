/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#include "kernel/net/mbuf.h"
#include "kernel/net/tcp.h"
#include "kernel/net/ip.h"
#include "kernel/net/in.h"
#include "kernel/net/if.h"
#include "kernel/net/socket.h"
#include "kernel/net/icmp.h"
#include "kernel/mem/kheap.h"
#include "kernel/lib/stdio.h"
#include "kernel/task/task.h"
#include "kernel/time/timer.h"
#include <string.h>
extern void serial_puts(const char* s);

#define MAX_SOCKETS 32
#define SOCK_RCVBUF (256 * 1024)
/* Largest TCP payload that fits one TX mbuf: 2048-byte cluster, 256 bytes
   of headroom, minus the TCP + IP headers (40). sys_send must never hand
   tcp_send_packet() more than this — an unbounded memcpy overruns the
   cluster and corrupts the kheap free list (ISR 13 #GP in kmalloc). */
#define MAX_TCP_PAYLOAD 1752
static struct socket* sockets[MAX_SOCKETS];

/* Ephemeral source port allocator. Ports must NOT be tied to the socket
   slot index: the peer keeps the 4-tuple in TIME_WAIT/FIN_WAIT after a
   close, and a new SYN reusing the same source port gets ignored (the
   connection would fail on the second fetch to the same host). */
static uint16_t next_ephemeral = 20000;

#include "kernel/net/net_log.h"

static int tcp_send_packet(struct socket* so, uint8_t flags, const void* data, int len) {
    struct mbuf* m = m_getcl(MT_DATA);
    if (m == NULL) return -1;
    if (m->m_ext.ext_buf == NULL) { m_free(m); return -1; }
    if (len < 0) len = 0;
    if (len > MAX_TCP_PAYLOAD) len = MAX_TCP_PAYLOAD;   /* keep cluster bounds */

    m->m_data = (char*)m->m_ext.ext_buf + 256;
    if (len > 0 && data) {
        memcpy(m->m_data, data, len);
        m->m_len = len;
    }

    m->m_data -= sizeof(struct tcphdr);
    m->m_len += sizeof(struct tcphdr);
    struct tcphdr* th = (struct tcphdr*)m->m_data;
    memset(th, 0, sizeof(struct tcphdr));
    th->th_sport = so->so_local.sin_port;
    th->th_dport = so->so_remote.sin_port;
    th->th_off = sizeof(struct tcphdr) >> 2;
    th->th_flags = flags;
    th->th_seq = htonl(so->so_seq);
    th->th_ack = htonl(so->so_ack);
    
    uint32_t free_win = SOCK_RCVBUF - (so->so_rcv_len);
    if (free_win > 65535) free_win = 65535;
    th->th_win = htons((uint16_t)free_win);

    m->m_data -= sizeof(struct ip);
    m->m_len += sizeof(struct ip);
    struct ip* ip = (struct ip*)m->m_data;
    memset(ip, 0, sizeof(struct ip));
    ip->ip_v = 4;
    ip->ip_hl = 5;
    ip->ip_ttl = 64;
    static uint16_t tcp_ip_id = 0;
    ip->ip_id = htons(tcp_ip_id++);
    ip->ip_p = IPPROTO_TCP;
    ip->ip_len = htons(m->m_len);
    
    struct ifnet* ifp = if_find_primary();
    ip->ip_src.s_addr = (ifp && ifp->if_ip != 0) ? ifp->if_ip : htonl(0x0A00020F);
    ip->ip_dst = so->so_remote.sin_addr;
    
    /* Force ACK field directly in mbuf to prevent corruption */
    uint32_t ack_val = htonl(so->so_ack);
    uint8_t* tcp_hdr = (uint8_t*)m->m_data + sizeof(struct ip);
    tcp_hdr[8] = (ack_val >> 24) & 0xFF;
    tcp_hdr[9] = (ack_val >> 16) & 0xFF;
    tcp_hdr[10] = (ack_val >> 8) & 0xFF;
    tcp_hdr[11] = ack_val & 0xFF;
    th->th_ack = ack_val;
    
    th->th_sum = 0;
    th->th_sum = tcp_checksum(ip, th, m, sizeof(struct ip));
    
    int _ret = ip_output(m, ifp);
    return _ret;
}

void tcp_input(struct mbuf* m, int off) {
    struct ip* ip = (struct ip*)(m->m_data - off);
    struct tcphdr* th = (struct tcphdr*)m->m_data;

    /* Find matching socket */
    struct socket* so = NULL;
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (sockets[i] && ntohs(sockets[i]->so_local.sin_port) == ntohs(th->th_dport)) {
            so = sockets[i];
            break;
        }
    }
    
    if (so == NULL) {
        m_freem(m);
        return;
    }
    
    if (th->th_flags & TH_RST) {
        net_log_addf("TCP", 0xF85149, "RST received for port %u", ntohs(th->th_dport));
        so->so_closed = 1;
        m_freem(m);
        return;
    }

    /* Handshake handling */
    if ((th->th_flags & TH_SYN) && (th->th_flags & TH_ACK)) {
        if (so->so_state == TCPS_SYN_SENT) {
            so->so_ack = ntohl(th->th_seq) + 1;
            so->so_seq = ntohl(th->th_ack);
            so->so_una = so->so_seq;
            so->so_state = TCPS_ESTABLISHED;
            tcp_send_packet(so, TH_ACK, NULL, 0);
            net_log_addf("TCP", 0x3FB950, "Established connection (SYN-ACK rcvd, ACK sent)");
        } else if (so->so_state == TCPS_ESTABLISHED) {
            tcp_send_packet(so, TH_ACK, NULL, 0);
        }
    }
    
    /* Track remote ACK of our data */
    uint32_t rcv_ack = ntohl(th->th_ack);
    if (rcv_ack > so->so_una) {
        so->so_una = rcv_ack;
    }

    /* Handle data payload */
    int data_len = ntohs(ip->ip_len) - (ip->ip_hl << 2) - (th->th_off << 2);
    if (data_len > 0 && so->so_state == TCPS_ESTABLISHED) {
        /* Update expected ACK */
        so->so_ack = ntohl(th->th_seq) + data_len;
        
        /* Copy payload to socket receive buffer */
        void* data = (char*)m->m_data + (th->th_off << 2);
        if (so->so_rcv_buf == NULL) {
            so->so_rcv_buf = kmalloc(SOCK_RCVBUF);
            so->so_rcv_len = 0;
        }
        
        so->so_rcv_total += data_len;
        if (so->so_rcv_len + data_len <= SOCK_RCVBUF) {
            memcpy((char*)so->so_rcv_buf + so->so_rcv_len, data, data_len);
            so->so_rcv_len += data_len;
        } else {
            /* Drop oldest bytes to keep newest (ring-buffer style). Speed
               accounting uses so_rcv_total, so throughput stays accurate. */
            uint32_t over = so->so_rcv_len + data_len - SOCK_RCVBUF;
            if (over >= so->so_rcv_len) {
                so->so_rcv_len = 0;
            } else {
                memmove(so->so_rcv_buf, (char*)so->so_rcv_buf + over, so->so_rcv_len - over);
                so->so_rcv_len -= over;
            }
            if (so->so_rcv_len + data_len > SOCK_RCVBUF) {
                memcpy((char*)so->so_rcv_buf + so->so_rcv_len, data, SOCK_RCVBUF - so->so_rcv_len);
                so->so_rcv_len = SOCK_RCVBUF;
            } else {
                memcpy((char*)so->so_rcv_buf + so->so_rcv_len, data, data_len);
                so->so_rcv_len += data_len;
            }
            net_log_addf("TCP", 0xD29922, "RX buffer overflow (kept newest, total %llu)", so->so_rcv_total);
        }

        /* Send ACK for data */
        tcp_send_packet(so, TH_ACK, NULL, 0);
    }
    
    /* Handle FIN - mark socket as closed after buffering data */
    if (th->th_flags & TH_FIN) {
        so->so_ack++;
        tcp_send_packet(so, TH_ACK, NULL, 0);
        so->so_closed = 1;
        net_log_addf("TCP", 0xD29922, "FIN received (Remote closed connection)");
    }

    m_freem(m);
}

/* Socket API implementations */

int sys_socket(int domain, int type, int protocol) {
    (void)domain; (void)protocol;
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (sockets[i] == NULL) {
            struct socket* so = kmalloc(sizeof(struct socket));
            memset(so, 0, sizeof(struct socket));
            so->so_type = type;
            so->so_rcv_buf = NULL; /* lazy-allocated on first RX */
            so->so_rcv_len = 0;
            so->so_rcv_total = 0;
            so->so_closed = 0;
            so->so_una = 0;
            sockets[i] = so;
            net_log_addf("SOCK", 0x8B949E, "Socket #%d created", i);
            return i;
        }
    }
    net_log_add("SOCK", "Failed to allocate socket: table full", 0xF85149);
    return -1;
}

int sys_connect(int s, const struct sockaddr* name, int namelen) {
    (void)namelen;
    if (s < 0 || s >= MAX_SOCKETS || sockets[s] == NULL) {
        return -1;
    }
    struct socket* so = sockets[s];
    so->so_remote = *(const struct sockaddr_in*)name;
    so->so_state = TCPS_SYN_SENT;
    so->so_seq = 1000; /* Initial sequence number */
    so->so_ack = 0;
    so->so_local.sin_port = htons(__atomic_fetch_add(&next_ephemeral, 1, __ATOMIC_RELAXED));
    if (next_ephemeral >= 40000) next_ephemeral = 20000;

    uint32_t ip_h = ntohl(so->so_remote.sin_addr.s_addr);
    uint16_t port_h = ntohs(so->so_remote.sin_port);
    net_log_addf("TCP", 0x58A6FF, "Connect #%d -> %u.%u.%u.%u:%u",
        s, (ip_h>>24)&0xFF, (ip_h>>16)&0xFF, (ip_h>>8)&0xFF, ip_h&0xFF, port_h);

    /* Send SYN packet */
    if (tcp_send_packet(so, TH_SYN, NULL, 0) == 0) {
        so->so_seq++; /* SYN occupies 1 sequence number */
    }

    /* Wait for SYN-ACK with retransmit every 3s */
    int timeout = 500;
    int syn_retry = 8;
    uint32_t orig_seq = so->so_seq;
    while (so->so_state != TCPS_ESTABLISHED && timeout > 0 && !so->so_closed) {
        extern void net_poll();
        net_poll();
        sleep_task(50);
        timeout--;
        if (timeout % 60 == 0 && syn_retry > 0) {
            syn_retry--;
            so->so_seq = orig_seq;
            tcp_send_packet(so, TH_SYN, NULL, 0);
        }
    }

    if (so->so_state == TCPS_ESTABLISHED) {
        return 0;
    }
    net_log_addf("TCP", 0xF85149, "Connect #%d failed (timeout / refused)", s);
    return -1;
}

int sys_send(int s, const void* msg, int len, int flags) {
    (void)flags;
    if (s < 0 || s >= MAX_SOCKETS || sockets[s] == NULL) {
        return -1;
    }
    struct socket* so = sockets[s];
    if (so->so_state != TCPS_ESTABLISHED) return -1;

    if (len < 0) return -1;
    if (len > MAX_TCP_PAYLOAD) len = MAX_TCP_PAYLOAD;

    uint32_t target_seq = so->so_seq + len;

    net_log_addf("TCP", 0x58A6FF, "TX #%d: %d bytes (seq %u)", s, len, so->so_seq);

    /* Send the packet immediately — don't wait for retransmit window */
    tcp_send_packet(so, TH_ACK | TH_PUSH, msg, len);

    int timeout = 400; /* ~20s max */
    int retransmit_ticks = 120; /* retransmit every ~6s */
    int ticks_since_tx = 0;

    while (timeout > 0) {
        extern void net_poll();
        net_poll();
        sleep_task(50);
        timeout--;
        ticks_since_tx++;

        if (so->so_una >= target_seq) {
            so->so_seq = target_seq;
            return len;
        }

        if (so->so_closed) {
            net_log_addf("TCP", 0xF85149, "Send #%d failed (connection closed)", s);
            return -1;
        }

        /* Retransmit if no ACK received within retransmit window */
        if (ticks_since_tx >= retransmit_ticks) {
            ticks_since_tx = 0;
            tcp_send_packet(so, TH_ACK | TH_PUSH, msg, len);
        }
    }

    net_log_addf("TCP", 0xF85149, "Send #%d failed (ACK timeout)", s);
    return -1;
}

int sys_recv(int s, void* buf, int len, int flags) {
    (void)flags;
    if (s < 0 || s >= MAX_SOCKETS || sockets[s] == NULL) return -1;
    struct socket* so = sockets[s];
    
    /* Wait for data up to 5 seconds (100 * 50ms) */
    int timeout = 100;
    while (so->so_rcv_len == 0 && timeout > 0 && !so->so_closed) {
        extern void net_poll();
        net_poll();
        sleep_task(50);
        timeout--;
    }
    
    if (so->so_rcv_len == 0) {
        return 0;
    }
    
    int to_copy = (len < so->so_rcv_len) ? len : so->so_rcv_len;
    /* Drain under the e1000 lock: tcp_input appends to so_rcv_buf under
       the same lock, so the copy/compact/len update must be atomic with
       respect to it (otherwise the recv and a concurrent append can tear
       each other's buffer/length state). */
    extern void e1000_mutex_lock(void);
    extern void e1000_mutex_unlock(void);
    e1000_mutex_lock();
    memcpy(buf, so->so_rcv_buf, to_copy);
    if (to_copy < so->so_rcv_len) {
        memmove(so->so_rcv_buf, (char*)so->so_rcv_buf + to_copy, so->so_rcv_len - to_copy);
    }
    so->so_rcv_len -= to_copy;
    e1000_mutex_unlock();

    /* Re-open the receive window so a full buffer doesn't stall the sender. */
    if (to_copy > 0 && so->so_state == TCPS_ESTABLISHED && !so->so_closed) {
        uint32_t free_win = SOCK_RCVBUF - so->so_rcv_len;
        if (free_win > 0 && free_win <= 65535)
            tcp_send_packet(so, TH_ACK, NULL, 0);
    }
    
    return to_copy;
}

int sys_socket_rx_total(int s) {
    if (s < 0 || s >= MAX_SOCKETS || sockets[s] == NULL) return -1;
    return (int)sockets[s]->so_rcv_total;
}

int sys_socket_close(int s) {
    extern void serial_puts(const char* s);
    if (s < 0 || s >= MAX_SOCKETS || sockets[s] == NULL) return -1;
    
    struct socket* so = sockets[s];
    net_log_addf("SOCK", 0x8B949E, "Socket #%d closed", s);
    
    /* Graceful close: send FIN so the peer releases the 4-tuple. Without
       this the server sits in FIN_WAIT_2 forever, and the next connect
       reusing the same local port is ignored (SYN on a half-open
       connection) and times out after 25s. */
    if (so->so_state == TCPS_ESTABLISHED) {
        uint32_t fin_seq = so->so_seq;
        tcp_send_packet(so, TH_FIN | TH_ACK, NULL, 0);
        /* Give the peer a moment to ACK the FIN before freeing the socket */
        int waits = 40; /* ~2s max */
        while (waits-- > 0 && so->so_una <= fin_seq) {
            extern void net_poll(void);
            net_poll();
            sleep_task(50);
        }
    }
    
    /* Free under the e1000 lock: tcp_input runs under the same lock, so a
       concurrent RX path can never be mid-use of `so` when it is freed
       (it would otherwise read freed memory -> heap corruption). */
    extern void e1000_mutex_lock(void);
    extern void e1000_mutex_unlock(void);
    e1000_mutex_lock();
    if (so->so_rcv_buf) kfree(so->so_rcv_buf);
    kfree(so);
    sockets[s] = NULL;
    e1000_mutex_unlock();
    return 0;
}

int sys_socket_closed(int s) {
    if (s < 0 || s >= MAX_SOCKETS || sockets[s] == NULL) return 1;
    return sockets[s]->so_closed;
}

int sys_ping(uint32_t ip_addr) {
    struct ifnet* ifp = if_list_head();
    while (ifp) {
        if ((ifp->if_flags & IFF_RUNNING) && ifp->if_ip != 0) break;
        ifp = ifp->if_next;
    }
    if (!ifp) return -1;

    struct mbuf* m = m_getcl(MT_DATA);
    if (!m) return -1;

    m->m_data += 100;

    struct icmphdr* icp = (struct icmphdr*)m->m_data;
    memset(icp, 0, sizeof(struct icmphdr));
    icp->icmp_type = ICMP_ECHO;
    icp->icmp_code = 0;
    icp->icmp_id = htons(0x1234);
    icp->icmp_seq = htons(1);

    m->m_len = sizeof(struct icmphdr);
    extern uint16_t icmp_checksum(void* vdata, size_t length);
    icp->icmp_cksum = icmp_checksum(icp, m->m_len);

    m->m_data -= sizeof(struct ip);
    m->m_len += sizeof(struct ip);
    struct ip* ip = (struct ip*)m->m_data;
    memset(ip, 0, sizeof(struct ip));
    ip->ip_v = 4;
    ip->ip_hl = 5;
    ip->ip_p = IPPROTO_ICMP;
    ip->ip_len = htons(m->m_len);
    ip->ip_ttl = 64;
    ip->ip_src.s_addr = ifp->if_ip;
    ip->ip_dst.s_addr = ip_addr;

    uint32_t ip_h = ntohl(ip_addr);
    net_log_addf("ICMP", 0x58A6FF, "Ping Request -> %u.%u.%u.%u",
        (ip_h>>24)&0xFF, (ip_h>>16)&0xFF, (ip_h>>8)&0xFF, ip_h&0xFF);

    if (ip_output(m, ifp) != 0) return -1;
    uint32_t t0 = timer_get_ms();
    uint32_t ping_id = 0x1234;
    uint16_t ping_seq = 1;
    while (timer_get_ms() - t0 < 2000) {
        extern void net_poll(void);
        net_poll();
        uint32_t reply_src;
        uint16_t reply_id, reply_seq;
        if (icmp_get_echo_reply(&reply_src, &reply_id, &reply_seq) == 0) {
            if (reply_src == ip_addr && reply_id == htons(ping_id) && reply_seq == htons(ping_seq)) {
                uint32_t ms = timer_get_ms() - t0;
                net_log_addf("ICMP", 0x3FB950, "Ping Reply from %u.%u.%u.%u (%u ms)",
                    (ip_h>>24)&0xFF, (ip_h>>16)&0xFF, (ip_h>>8)&0xFF, ip_h&0xFF, ms);
                return 0;
            }
        }
        sleep_task(10);
    }
    net_log_addf("ICMP", 0xF85149, "Ping Request -> %u.%u.%u.%u TIMEOUT",
        (ip_h>>24)&0xFF, (ip_h>>16)&0xFF, (ip_h>>8)&0xFF, ip_h&0xFF);
    return -1;
}
