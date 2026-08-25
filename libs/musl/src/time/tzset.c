#include <time.h>

/* EigenOS: no tzdata, timezone is UTC. tzset() is provided so portable
   programs link; state refresh happens lazily inside localtime(). */
void tzset(void)
{
}

weak_alias(tzset, __tzset);
