/* Freestanding-gap implementations for the Lua 5.1.5 port.
 * Our ring-3 libc lacks strcspn, strcoll, localeconv and strtoul; it DOES
 * provide fmod/fputs/fflush/fwrite/strtol/strtod and the rest of stdio.
 */
#include "eigen_lua_shim.h"
#include <stdio.h>
#include <string.h>

size_t strcspn(const char *s, const char *reject)
{
   const char *p = s;
   while (*p && !strchr(reject, *p)) p++;
   return (size_t)(p - s);
}

int strcoll(const char *s1, const char *s2) { return strcmp(s1, s2); }

struct lconv _lc = { "." };
struct lconv *localeconv(void) { return &_lc; }

static int _isspace(int c)
{ return c==' '||c=='\t'||c=='\n'||c=='\r'||c=='\f'||c=='\v'; }

unsigned long strtoul(const char *nptr, char **endptr, int base)
{
   while (_isspace((unsigned char)*nptr)) nptr++;
   int neg = 0;
   if (*nptr == '+' || *nptr == '-') { neg = (*nptr == '-'); nptr++; }
   if (base == 0) {
      base = 10;
      if (*nptr == '0') {
         if (nptr[1]=='x'||nptr[1]=='X') { base = 16; nptr += 2; }
         else base = 8;
      }
   } else if (base == 16 && *nptr=='0' && (nptr[1]=='x'||nptr[1]=='X'))
      nptr += 2;
   unsigned long v = 0;
   if (base <= 10) {
      while (*nptr >= '0' && *nptr < '0' + base) { v = v*base + (*nptr-'0'); nptr++; }
   } else {
      for (;;) {
         int d;
         if (*nptr>='0'&&*nptr<='9') d=*nptr-'0';
         else if (*nptr>='a'&&*nptr<='f') d=*nptr-'a'+10;
         else if (*nptr>='A'&&*nptr<='F') d=*nptr-'A'+10;
         else break;
         if (d >= base) break;
         v = v*base + d; nptr++;
      }
   }
   if (endptr) *endptr = (char*)nptr;
   return neg ? (~v + 1UL) : v;
}
