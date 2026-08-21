/* Minimal fnmatch.h — bundled for the Eina freestanding port.
 * Mirrors the subset of POSIX fnmatch Eina's eina_fnmatch uses. */
#ifndef EINA_FNMATCH_H_BUNDLED
#define EINA_FNMATCH_H_BUNDLED

#define FNM_PATHNAME 0x1
#define FNM_NOESCAPE 0x2
#define FNM_PERIOD   0x4
#define FNM_NOMATCH  1
#define FNM_NOSYS    2

int __fnmatch(const char* pattern, const char* string, int flags);
int fnmatch(const char* pattern, const char* string, int flags);

#endif
