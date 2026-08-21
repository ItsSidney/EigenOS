/* emile_cipher_stub.c — minimal cipher backend for EigenOS.
 *
 * Emile's real cipher support requires OpenSSL or GnuTLS, which are not
 * available in this freestanding environment. We provide the symbols Eet
 * links against, but encryption/decryption/hmac are unsupported (they
 * return failure / NULL). Unencrypted Eet files work fully via zlib. */

#define EFL_BETA_API_SUPPORT
#include <Emile.h>

Eina_Bool
_emile_cipher_init(void)
{
   return EINA_TRUE;
}

EAPI Eina_Binbuf *
emile_binbuf_cipher(Emile_Cipher_Algorithm algo,
                    const Eina_Binbuf *in,
                    const char *key,
                    unsigned int length)
{
   (void)algo; (void)in; (void)key; (void)length;
   return NULL;
}

EAPI Eina_Binbuf *
emile_binbuf_decipher(Emile_Cipher_Algorithm algo,
                      const Eina_Binbuf *in,
                      const char *key,
                      unsigned int length)
{
   (void)algo; (void)in; (void)key; (void)length;
   return NULL;
}

EAPI Eina_Bool
emile_binbuf_hmac_sha1(const char *key,
                       unsigned int key_len,
                       const Eina_Binbuf *data,
                       unsigned char digest[20])
{
   (void)key; (void)key_len; (void)data; (void)digest;
   return EINA_FALSE;
}

EAPI Eina_Bool
emile_binbuf_sha1(const Eina_Binbuf *data,
                  unsigned char digest[20])
{
   (void)data; (void)digest;
   return EINA_FALSE;
}
