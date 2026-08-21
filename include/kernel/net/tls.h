/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#ifndef KERNEL_NET_TLS_H
#define KERNEL_NET_TLS_H
/*
 * BearSSL TLS-client glue (see src/kernel/net/tls.c).
 *
 * Provides a small, synchronous TLS client over a connected TCP
 * socket.  edrowser and any networking app can:
 *
 *   br_tls_begin(sock, host)     // initiate handshake, set SNI
 *   br_tls_write_all(req, n)     // send HTTP/TLS app data (also drives handshake)
 *   n = br_tls_read(buf, n)      // read decrypted app data
 *   n = br_tls_read_all(buf, n)  // block until n bytes (or error)
 *   br_tls_close()               // shutdown + close socket
 *   br_tls_error()               // reason on failure
 *
 * BearSSL itself is NOT modified; the vendored library under
 * src/libs/bearssl/ is consumed as-is.  This header only exposes
 * the kernel-side wrapper.
 */
#include <stddef.h>
#include <stdint.h>

int  br_tls_begin(int sock, const char *host);
int  br_tls_write(const void *data, size_t len);
int  br_tls_write_all(const void *data, size_t len);
int  br_tls_read(void *dst, size_t len);
int  br_tls_read_all(void *dst, size_t len);
void br_tls_close(void);
const char *br_tls_error(void);

/* Diagnostic (for self-tests / debugging).  Returns the BearSSL
 * engine's last error code (0 = none) and fills *state_out with the
 * current br_ssl_engine_current_state() bitmask (or 0 if never begun). */
uint32_t br_tls_engine_diag(unsigned *state_out);

/* Kernel boot self-test for the TLS subsystem (returns 1 on success).
 * Proves the crypto backend loads its CA bundle and RNG without a
 * network. Implemented in src/kernel/net/tls.c. */
int tls_selftest(void);

#endif /* KERNEL_NET_TLS_H */
