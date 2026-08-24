/* Freestanding-gap declarations for the Lua 5.1.5 port.
 * Our ring-3 libc lacks EXIT_FAILURE (it has exit()), strcspn, strcoll,
 * localeconv and the strto* family; it DOES provide fmod/fputs/fflush/fwrite
 * and the rest of stdio. Provided in eigen_lua_shim.c.
 */
#ifndef EIGEN_LUA_SHIM_H
#define EIGEN_LUA_SHIM_H
#include <string.h>
#include <stdlib.h>
#ifndef EXIT_FAILURE
# define EXIT_FAILURE 1
#endif
size_t        strcspn(const char *s, const char *reject);
int           strcoll(const char *s1, const char *s2);
struct lconv { const char *decimal_point; };
struct lconv *localeconv(void);
unsigned long strtoul(const char *nptr, char **endptr, int base);
#endif
