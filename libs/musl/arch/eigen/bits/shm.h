/* stub SysV shm */
#ifndef _STUB_BITS_SHM_H
#define _STUB_BITS_SHM_H
#include <bits/ipc.h>
struct shminfo { unsigned long shmmax, shmmin, shmmni, shmseg, shmall; };
struct shm_info { int used_ids; unsigned long shm_tot, shm_rss, shm_swp, swap_attempts, swap_successes; };
#endif
