/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

/* ============================================================
 *  Eigen — WolfSSL TLS-client glue (ring-0, freestanding).
 *
 *  WolfSSL is vendored under libs/wolfssl/.  It is freestanding:
 *  NO_FILESYSTEM, WOLFSSL_USER_IO (custom socket callbacks),
 *  SINGLE_THREADED, custom RNG (CUSTOM_RAND_GENERATE_SEED,
 *  RDRAND + fallback entropy).
 *
 *  The public kernel API is preserved (br_tls_* names) so edrowser
 *  / networking callers need no changes:
 *
 *      br_tls_begin(sock, host)      // 1 ok / 0 fail
 *      br_tls_write(data, len)       // -> bytes, or -1
 *      br_tls_write_all(data, len)   // -> 0 ok / -1 err
 *      br_tls_read(dst, len)         // -> bytes / 0(closed) / -1(err)
 *      br_tls_read_all(dst, len)     // -> 0 ok / -1 err
 *      br_tls_close()
 *      br_tls_error()
 *      br_tls_engine_diag()
 *
 *  Drive model: br_tls_begin() builds a WOLFSSL_CTX (CA bundle from
 *  ca_roots.h), a WOLFSSL object, sets SNI + custom I/O callbacks and
 *  calls wolfSSL_connect().  Subsequent br_tls_write/read() go through
 *  wolfSSL_write/wolfSSL_read().  Every entry is defensive: a TLS
 *  failure becomes a clean error return (never a kernel fault).
 * ============================================================ */
#include "kernel/net/tls.h"
#include "kernel/net/ca_roots.h"
#include "kernel/net/socket.h"
#include "kernel/net/net_log.h"

#include <wolfssl/ssl.h>
#include <wolfssl/wolfcrypt/random.h>
#include <wolfssl/wolfcrypt/memory.h>
#include <wolfssl/wolfio.h>
#include <wolfssl/error-ssl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern void serial_puts(const char* s);
extern void net_poll(void);
extern void sleep_task(uint32_t ms);
extern void* kmalloc(unsigned long size);
extern void  kfree(void* ptr);

/* Kernel RTC access — declared locally (NOT via drivers/time/rtc.h) because
 * that header's `typedef struct {...} time_t` clashes with WolfSSL's own
 * `typedef long time_t` (USER_TIME). Same layout, same symbol. */
struct eigen_rtc_time { int hour, minute, second, day, month, year; };
extern void get_time(struct eigen_rtc_time* t);
extern uint32_t timer_get_ms(void);

/* ── Kernel heap bridge ──────────────────────────────────────
 * WolfSSL routes every allocation through wolfSSL_Malloc/Free/Realloc,
 * which dispatch to the callbacks registered below.  krealloc keeps a
 * size header so it can copy the old contents (kmalloc has no realloc). */
#define KALLOC_HDR sizeof(unsigned long)

/* Single-threaded kernel: mutexes are no-ops (the engine calls these as
 * real functions even under SINGLE_THREADED). */
int wc_InitMutex(int *m)  { if (m) *m = 0; return 0; }
int wc_FreeMutex(int *m)  { (void)m; return 0; }
int wc_LockMutex(int *m)  { (void)m; return 0; }
int wc_UnLockMutex(int *m){ (void)m; return 0; }

/* RwLocks same story (typedef int under SINGLE_THREADED). */
int wc_InitRwLock(int *m)    { if (m) *m = 0; return 0; }
int wc_FreeRwLock(int *m)    { (void)m; return 0; }
int wc_LockRwLock_Rd(int *m) { (void)m; return 0; }
int wc_LockRwLock_Wr(int *m) { (void)m; return 0; }
int wc_UnLockRwLock(int *m)  { (void)m; return 0; }

/* wolfCrypt_Init/Cleanup live in wc_port.c (not compiled); no-ops here. */
int wolfCrypt_Init(void)    { return 0; }
int wolfCrypt_Cleanup(void) { return 0; }

/* wolfSSL_strnstr lives in wc_port.c, which we cannot compile (it drags
 * host <sys/socket.h>); provide the trivial implementation here. */
char *wolfSSL_strnstr(const char *s, const char *needle, size_t slen) {
    size_t n;
    if (!s || !needle) return NULL;
    n = strlen(needle);
    if (n == 0) return (char *)s;
    for (; *s && slen >= n; s++, slen--) {
        if (*s == *needle && strncmp(s, needle, n) == 0) return (char *)s;
    }
    return NULL;
}

static void *tls_malloc_cb(size_t size) {
    unsigned long *h;
    if (size == 0) size = 1;
    h = (unsigned long *)kmalloc((unsigned long)size + KALLOC_HDR);
    if (!h) return NULL;
    h[0] = (unsigned long)size;
    return (void *)(h + 1);
}
static void tls_free_cb(void *ptr) {
    if (!ptr) return;
    kfree((void *)((unsigned long *)ptr - 1));
}
static void *tls_realloc_cb(void *ptr, size_t size) {
    unsigned long *h, old;
    void *n;
    if (!ptr) return tls_malloc_cb(size);
    if (size == 0) { tls_free_cb(ptr); return NULL; }
    h = (unsigned long *)ptr - 1;
    old = h[0];
    n = tls_malloc_cb(size);
    if (!n) return NULL;
    memcpy(n, ptr, old < (unsigned long)size ? old : (unsigned long)size);
    tls_free_cb(ptr);
    return n;
}

/* ── Active TLS state (one socket at a time) ───────────────── */
static WOLFSSL_CTX *tls_ctx = NULL;
static WOLFSSL    *tls_ssl = NULL;
static int         tls_sock = -1;
static int         tls_handshake_ok = 0;
static char        tls_err[128];

/* ── Entropy: RDRAND-backed seed, CPUID-gated ────────────────
 * WolfSSL calls wc_GenerateSeed -> our eigen_wc_GenerateSeed (see
 * CUSTOM_RAND_GENERATE_SEED in libs/wolfssl/user_settings.h).  RDRAND is
 * CPUID-detected; if absent we fall back to a weak RTC+timer+BSS-hash
 * seed rather than executing `rdrand` (#UD -> triple fault). */
static int rdrand_supported(void) {
    uint32_t eax, ebx, ecx, edx;
    eax = 1;
    __asm__ volatile("cpuid"
                     : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                     : "a"(eax));
    return (ecx & (1u << 30)) != 0;
}

static int rdrand_bytes(unsigned char* out, size_t len) {
    size_t i = 0;
    while (i + 8 <= len) {
        unsigned long long val; int ok = 0;
        for (int attempt = 0; attempt < 10; attempt++) {
            __asm__ volatile("rdrand %0; setc %1"
                              : "=r"(val), "=m"(ok) :: "cc");
            if (ok) break;
        }
        if (!ok) return 0;
        memcpy(out + i, &val, 8);
        i += 8;
    }
    if (i < len) {
        unsigned long long val; int ok = 0;
        for (int attempt = 0; attempt < 10; attempt++) {
            __asm__ volatile("rdrand %0; setc %1"
                              : "=r"(val), "=m"(ok) :: "cc");
            if (ok) break;
        }
        if (!ok) return 0;
        memcpy(out + i, &val, len - i);
    }
    return 1;
}

static int fallback_entropy(unsigned char* out, size_t len) {
    struct eigen_rtc_time rtc;
    get_time(&rtc);
    uint32_t t = timer_get_ms();
    uint64_t seed = ((uint64_t)rtc.year  << 48)
                  | ((uint64_t)rtc.month << 40)
                  | ((uint64_t)rtc.day   << 32)
                  | ((uint64_t)rtc.hour  << 24)
                  | ((uint64_t)rtc.minute<< 16)
                  | ((uint64_t)rtc.second<< 8)
                  | ((uint64_t)(t & 0xFF));
    for (size_t i = 0; i < len; i++) {
        seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
        out[i] = (unsigned char)((seed >> 33) & 0xFF);
    }
    return 1;
}

/* ── Wall-clock time for X.509 validation (USER_TIME hooks) ──
 * WolfSSL needs seconds-since-epoch (XTIME) plus gmtime() to check
 * certificate validity windows. Both derive from the kernel RTC via
 * Howard Hinnant's days_from_civil algorithm. */
static long civil_days(long y, unsigned m, unsigned d) {
    y -= (m <= 2);
    const long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);            /* [0,399] */
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097L + (long)doe - 719468L;
}

/* WolfSSL USER_TIME hook: seconds since Unix epoch. */
time_t XTIME(time_t *timer) {
    struct eigen_rtc_time rtc;
    get_time(&rtc);
    long days = civil_days((long)rtc.year, (unsigned)rtc.month, (unsigned)rtc.day);
    time_t now = (time_t)days * 86400L
               + (time_t)rtc.hour * 3600L
               + (time_t)rtc.minute * 60L
               + (time_t)rtc.second;
    if (timer) *timer = now;
    return now;
}

/* WolfSSL USER_TIME hook: epoch seconds -> UTC broken-down time. */
static struct tm tmbuf;

struct tm *gmtime_r(const time_t *timer, struct tm *out);

struct tm *gmtime(const time_t *timer) {
    return gmtime_r(timer, &tmbuf);
}

struct tm *gmtime_r(const time_t *timer, struct tm *out) {
    struct tm *t = out ? out : &tmbuf;
    time_t tt = timer ? *timer : XTIME(NULL);
    long days = (long)(tt / 86400L);
    long rem  = (long)(tt % 86400L);
    if (rem < 0) { rem += 86400L; days -= 1; }

    t->tm_hour = (int)(rem / 3600L); rem %= 3600L;
    t->tm_min  = (int)(rem / 60L);
    t->tm_sec  = (int)(rem % 60L);

    /* civil_from_days (Hinnant) */
    days += 719468L;
    long era = (days >= 0 ? days : days - 146096L) / 146097L;
    unsigned doe = (unsigned)(days - era * 146097L);
    unsigned yoe = (doe - doe / 1460L + doe / 36524L - doe / 146096L) / 365L;
    long y = (long)yoe + era * 400L;
    unsigned doy = doe - (365L * yoe + yoe / 4L - yoe / 100L);
    unsigned mp = (5L * doy + 2L) / 153L;
    unsigned d = doy - (153L * mp + 2L) / 5L + 1L;
    unsigned m = mp + (mp < 10L ? 3L : -9L);
    if (m <= 2) y += 1;

    t->tm_year = (int)(y - 1900);
    t->tm_mon  = (int)m - 1;
    t->tm_mday = (int)d;
    t->tm_isdst = 0;
    return t;
}

/* WolfSSL custom RNG seed callback. Returns 0 on success. */
int eigen_wc_GenerateSeed(unsigned char *output, unsigned long sz) {
    if (rdrand_supported()) {
        if (!rdrand_bytes(output, (size_t)sz)) return 1;
    } else {
        fallback_entropy(output, (size_t)sz);
    }
    return 0;
}

/* ── Custom socket I/O for WolfSSL ───────────────────────────
 * Bounded poll/sleep (like BearSSL's low_read/low_write) so the
 * input task keeps running. */
#define TLS_LOW_MAX_POLLS 5000

static int tls_io_recv(WOLFSSL *ssl, char *buf, int sz, void *ctx) {
    (void)ssl;
    int sock = (int)(long)ctx;
    int polls = 0;
    for (;;) {
        int n = sys_recv(sock, (unsigned char*)buf, sz, 0);
        if (n > 0) return n;
        if (n < 0) {
            char e[96];
            snprintf(e, sizeof e, "[TLS] io_recv: sys_recv=%d\n", n);
            serial_puts(e);
            return WOLFSSL_CBIO_ERR_WANT_READ;
        }
        if (sys_socket_closed(sock)) {
            serial_puts("[TLS] io_recv: socket closed\n");
            return 0;
        }
        net_poll();
        if (++polls >= TLS_LOW_MAX_POLLS) {
            serial_puts("[TLS] io_recv: poll timeout\n");
            return WOLFSSL_CBIO_ERR_WANT_READ;
        }
        sleep_task(1);
    }
}

static int tls_io_send(WOLFSSL *ssl, char *buf, int sz, void *ctx) {
    (void)ssl;
    int sock = (int)(long)ctx;
    int polls = 0;
    int off = 0;
    while (off < sz) {
        int n = sys_send(sock, (const unsigned char*)buf + off, sz - off, 0);
        if (n < 0) {
            if (!sys_socket_closed(sock)) {
                char e[96];
                snprintf(e, sizeof e, "[TLS] io_send: sys_send=%d\n", n);
                serial_puts(e);
            }
            return WOLFSSL_CBIO_ERR_WANT_WRITE;
        }
        if (n == 0) {
            net_poll();
            if (++polls >= TLS_LOW_MAX_POLLS) return WOLFSSL_CBIO_ERR_WANT_WRITE;
            sleep_task(1);
            continue;
        }
        off += n;
    }
    return off;
}

/* ── CA store ──────────────────────────────────────────────── */
static int tls_load_cas(WOLFSSL_CTX *ctx) {
    if (wolfSSL_CTX_load_verify_buffer(ctx, eigen_ca_pem, (long)EIGEN_CA_PEM_LEN,
                                       SSL_FILETYPE_PEM) != WOLFSSL_SUCCESS) {
        return -1;
    }
    wolfSSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
    wolfSSL_CTX_set_verify_depth(ctx, 4);
    return 0;
}

/* ── Public API ────────────────────────────────────────────── */

int br_tls_begin(int sock, const char *host) {
    if (!host || !host[0]) { tls_err[0] = 0; return 0; }
    tls_err[0] = 0;
    tls_handshake_ok = 0;
    tls_sock = sock;

    net_log_addf("TLS", 0xFF6B6B, "begin(%s)", host);

    wolfSSL_SetAllocators(tls_malloc_cb, tls_free_cb, tls_realloc_cb);
    wolfSSL_Init();
    if (!tls_ctx) tls_ctx = wolfSSL_CTX_new(wolfTLS_client_method());
    if (!tls_ctx) { snprintf(tls_err,sizeof tls_err,"TLS: SSL_CTX_new failed"); return 0; }
    if (!tls_ssl) tls_ssl = wolfSSL_new(tls_ctx);
    if (!tls_ssl) { snprintf(tls_err,sizeof tls_err,"TLS: SSL_new failed"); return 0; }

    static int cas_loaded = 0;
    if (!cas_loaded) {
        if (tls_load_cas(tls_ctx) != 0) {
            snprintf(tls_err, sizeof tls_err, "TLS: CA bundle load failed");
            return 0;
        }
        cas_loaded = 1;
    }

    wolfSSL_UseSNI(tls_ssl, WOLFSSL_SNI_HOST_NAME, host,
                   (unsigned short)strlen(host));   /* SNI */
    wolfSSL_SetIOReadCtx(tls_ssl, (void*)(long)sock);
    wolfSSL_SetIOWriteCtx(tls_ssl, (void*)(long)sock);
    wolfSSL_CTX_SetIORecv(tls_ctx, tls_io_recv);
    wolfSSL_CTX_SetIOSend(tls_ctx, tls_io_send);

    int r = wolfSSL_connect(tls_ssl);
    if (r != WOLFSSL_SUCCESS) {
        int e = wolfSSL_get_error(tls_ssl, r);
        snprintf(tls_err, sizeof tls_err, "TLS: handshake failed (err %d)", e);
        net_log_addf("TLS", 0xFF6B6B, "handshake FAIL err=%d", e);
        return 0;
    }
    tls_handshake_ok = 1;
    net_log_addf("TLS", 0x52C536, "handshake ok, SNI=%s", host);
    return 1;
}

int br_tls_write(const void *data, size_t len) {
    if (!tls_handshake_ok) { snprintf(tls_err,sizeof tls_err,"TLS: not connected"); return -1; }
    int r = wolfSSL_write(tls_ssl, data, (int)len);
    if (r <= 0) {
        snprintf(tls_err, sizeof tls_err, "TLS: write failed (err %d)",
                 wolfSSL_get_error(tls_ssl, r));
        return -1;
    }
    return r;
}

int br_tls_write_all(const void *data, size_t len) {
    if (!tls_handshake_ok) { snprintf(tls_err,sizeof tls_err,"TLS: not connected"); return -1; }
    size_t off = 0;
    while (off < len) {
        int r = wolfSSL_write(tls_ssl, (const char*)data + off, (int)(len - off));
        if (r <= 0) {
            int e = wolfSSL_get_error(tls_ssl, r);
            if (e == WOLFSSL_CBIO_ERR_WANT_WRITE) { sleep_task(1); net_poll(); continue; }
            snprintf(tls_err, sizeof tls_err, "TLS: write_all failed (err %d)", e);
            return -1;
        }
        off += (size_t)r;
    }
    return 0;
}

int br_tls_read(void *dst, size_t len) {
    if (!tls_handshake_ok) { snprintf(tls_err,sizeof tls_err,"TLS: not connected"); return -1; }
    int r = wolfSSL_read(tls_ssl, dst, (int)len);
    if (r <= 0) {
        int e = wolfSSL_get_error(tls_ssl, r);
        if (e == WOLFSSL_CBIO_ERR_WANT_READ) return 0;
        if (e == WOLFSSL_CBIO_ERR_WANT_WRITE) return 0;
        snprintf(tls_err, sizeof tls_err, "TLS: read failed (err %d)", e);
        return -1;
    }
    return r;
}

int br_tls_read_all(void *dst, size_t len) {
    if (!tls_handshake_ok) return -1;
    size_t off = 0;
    while (off < len) {
        int r = wolfSSL_read(tls_ssl, (char*)dst + off, (int)(len - off));
        if (r <= 0) {
            int e = wolfSSL_get_error(tls_ssl, r);
            if (e == WOLFSSL_CBIO_ERR_WANT_READ || e == WOLFSSL_CBIO_ERR_WANT_WRITE) {
                sleep_task(1); net_poll(); continue;
            }
            return -1;
        }
        off += (size_t)r;
    }
    return 0;
}

void br_tls_close(void) {
    if (tls_ssl) {
        wolfSSL_shutdown(tls_ssl);
        wolfSSL_free(tls_ssl);
        tls_ssl = NULL;
    }
    if (tls_ctx) {
        wolfSSL_CTX_free(tls_ctx);
        tls_ctx = NULL;
    }
    if (tls_sock >= 0) {
        sys_socket_close(tls_sock);
        tls_sock = -1;
        tls_handshake_ok = 0;
    }
}

const char *br_tls_error(void) {
    return tls_err[0] ? tls_err : "TLS: no error";
}

uint32_t br_tls_engine_diag(unsigned *state_out) {
    if (state_out) *state_out = tls_handshake_ok ? 1 : 0;
    return tls_err[0] ? 1 : 0;
}

/* ── Boot self-test: proves the freestanding WolfSSL port works
 * without network — init, load CA bundle (PEM), RNG. ────────── */
int tls_selftest(void) {
    int ok = 0;
    WOLFSSL_CTX *ctx = NULL;
    WOLFSSL    *ssl = NULL;
    WC_RNG      rng;
    byte seed[32];

    wolfSSL_SetAllocators(tls_malloc_cb, tls_free_cb, tls_realloc_cb);
    wolfSSL_Init();
    ctx = wolfSSL_CTX_new(wolfTLS_client_method());
    if (!ctx) { serial_puts("[WOLFSSL] self-test FAIL: SSL_CTX_new"); goto done; }

    if (wolfSSL_CTX_load_verify_buffer(ctx, eigen_ca_pem, (long)EIGEN_CA_PEM_LEN,
                                       SSL_FILETYPE_PEM) != WOLFSSL_SUCCESS) {
        serial_puts("[WOLFSSL] self-test FAIL: CA bundle load");
        goto done;
    }
    ssl = wolfSSL_new(ctx);
    if (!ssl) { serial_puts("[WOLFSSL] self-test FAIL: SSL_new"); goto done; }

    if (wc_InitRng(&rng) != 0) {
        serial_puts("[WOLFSSL] self-test FAIL: wc_InitRng");
        goto done;
    }
    if (wc_RNG_GenerateBlock(&rng, seed, sizeof seed) != 0) {
        serial_puts("[WOLFSSL] self-test FAIL: wc_RNG_GenerateBlock");
        goto done;
    }
    wc_FreeRng(&rng);
    ok = 1;
    serial_puts("[WOLFSSL] self-test ok (CAs loaded, RNG ok)");

done:
    if (ssl) wolfSSL_free(ssl);
    if (ctx) wolfSSL_CTX_free(ctx);
    wolfSSL_Cleanup();
    if (ok) serial_puts("[WOLFSSL] self-test ok");
    return ok;
}
