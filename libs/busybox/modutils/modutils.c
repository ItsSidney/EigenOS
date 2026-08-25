/* EigenOS port: module syscalls unsupported; all callers config-disabled. */
#include "libbb.h"
int bb_init_module(const char *filename, const char *options)
{ (void)filename; (void)options; return -1; }
int bb_delete_module(const char *module, unsigned int flags)
{ (void)module; (void)flags; return -1; }
const char *moderror(int err) { (void)err; return "module support unavailable"; }
