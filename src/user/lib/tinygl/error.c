#include <stdarg.h>
#include "zgl.h"

/* BEDI kernel: no stderr / exit() available; fatal errors become no-ops */
void gl_fatal_error(char *format, ...)
{
}
