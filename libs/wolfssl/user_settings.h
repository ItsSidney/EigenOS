/* user_settings.h — EigenOS freestanding (kernel, ring-0) WolfSSL config.
 *
 * Minimal TLS 1.2 *client*: NIST ECC (P-256/P-384), RSA (+RSA-PSS),
 * AES-128/256-GCM+CBC, SHA-1/256/384, HKDF/HMAC, custom I/O over kernel
 * sockets, custom RNG.  No OS, no filesystem, no threads, no asm.
 *
 * Compile every WolfSSL translation unit with -DWOLFSSL_USER_SETTINGS. */
#ifndef WOLFSSL_USER_SETTINGS_
#define WOLFSSL_USER_SETTINGS_

#include <string.h>
#define _DEFAULT_SOURCE
#include <strings.h>

/* --- host strcasecmp for the X* port layer --- */
#define XSTRCASECMP(s1, s2) strcasecmp((s1), (s2))

#define WOLFSSL_STATIC
#define NO_FILESYSTEM
#define SINGLE_THREADED
#define NO_WOLFSSL_SERVER
#define WOLFSSL_USER_IO        /* use wolfSSL_CTX_SetIORecv/SetIOSend, no BSD sockets */
#define WOLFSSL_NO_SOCK        /* never pull host <sys/socket.h>/<netinet/in.h> */
#define NO_WRITEV              /* never pull host <sys/uio.h> */
#define USER_TIME              /* we provide XTIME()/gmtime() from the kernel RTC
                                * (also makes WolfSSL use its own time_t/tm types) */
#define XGMTIME(c, t)          gmtime((c))  /* single-threaded: static buf is fine */
#define HAVE_SNI               /* Server Name Indication extension */
#define NO_OLD_TLS             /* no SSL3/TLS1.0/TLS1.1 (no MD5 handshake hash) */
#define NO_TLS13               /* match BearSSL's TLS 1.2 capability */
#define NO_ASM
#define NO_WOLFSSL_ASM

/* --- TLS/crypto features --- */
#define HAVE_TLS_EXTENSIONS
#define HAVE_SUPPORTED_CURVES
#define HAVE_ECC
#define HAVE_RSA
#define HAVE_AES
#define HAVE_AESGCM
#define HAVE_CHACHA
#define HAVE_POLY1305
#define HAVE_HKDF
#define HAVE_HMAC
#define HAVE_HASHDRBG
#define WOLFSSL_SHA256
#define WOLFSSL_SHA384
#define HAVE_SHA256
#define HAVE_SHA384
#define HAVE_SHA
#define HAVE_ECC_NIST_SP
#define HAVE_ECC_SECP256R1
#define HAVE_ECC_SECP384R1
#define HAVE_ECC_KEYPAIR
#define HAVE_ECDH
#define HAVE_ECDH_KEY
#define HAVE_SIGNER
#define HAVE_VERIFY
#define WOLFSSL_AES_128
#define WOLFSSL_AES_256
#define HAVE_SIG_WRAPPER
#define HAVE_RSA_PSS
#define WC_RSA_PSS

#define WOLFSSL_SMALL_STACK

/* --- kernel (ring-0) allocator ---
 * WolfSSL routes all heap use through wolfSSL_Malloc/Free/Realloc, which
 * dispatch to the callbacks registered via wolfSSL_SetAllocators() at
 * runtime (see tls.c: kmalloc/kfree + a size-header krealloc).  Do NOT
 * define XMALLOC/XFREE/XREALLOC here — types.h overrides them. */

/* --- custom RNG seed hook ---
 * With NO_DEV_RANDOM, wolfcrypt calls wc_GenerateSeed(output, sz) via the
 * function named by CUSTOM_RAND_GENERATE_SEED. */
#define CUSTOM_RAND_GENERATE_SEED  eigen_wc_GenerateSeed
extern int eigen_wc_GenerateSeed(unsigned char *output, unsigned long sz);

/* disables we don't need in a TLS client */
#define NO_RC4
#define NO_DES3
#define NO_MD5
#define NO_MD4
#define NO_DSA
#define NO_RIPEMD
#define NO_BLAKE2
#define NO_SHA3
#define NO_CURVE25519
#define NO_ED25519
#define NO_ED448
#define NO_ARIA
#define NO_CAMELLIA
#define NO_CAST
#define NO_IDEA
#define NO_BLOWFISH
#define NO_GOST
#define NO_SCRYPT
#define NO_PSK
#define NO_DH

#endif /* WOLFSSL_USER_SETTINGS_ */
