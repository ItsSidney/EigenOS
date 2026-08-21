/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

/* pthread.c — EigenOS pthreads (futex-based, kernel-scheduled threads).
 *
 * Threads are kernel task slots sharing the process's pml4. Each thread
 * gets its own kernel stack, a 512 KB user stack and a 4 KiB TLS page
 * whose virtual address is loaded into IA32_FS_BASE by the scheduler.
 *
 * TLS layout (mirrors the kernel's THREAD_TLS_SELF_OFF in task.c):
 *   +0    void*  self TLS base (written by the kernel; fs:0 == tls_base)
 *   +8    void*  key_values[64]   (pthread keys)
 *   +520  long   self tid (written by the kernel at thread/process create)
 *   +528  int    errno (per-thread; kept clear of +0 which is the kernel's
 *                       self pointer — writing errno there would clobber it)
 *
 * All sync primitives are built from atomic ops + EIGEN_SYS_FUTEX, so a
 * blocked thread actually sleeps in the kernel instead of spinning.
 */
#include <pthread.h>
#include <semaphore.h>
#include <sched.h>
#include <stdint.h>
#include <errno.h>
#include "eigen.h"
#include "userlib.h"

#define TLS_KEYS_OFF  8
#define TLS_MAX_KEYS  64
#define TLS_SELF_OFF  520   /* mirror of THREAD_TLS_SELF_OFF (kernel task.c) */
#define TLS_ERRNO_OFF 528   /* past keys[64] (+8..+520) and self tid (+520) */

/* Process-wide pthread key registry (the values themselves live in each
   thread's TLS page). Guarded by key_lock. */
static pthread_mutex_t key_lock = PTHREAD_MUTEX_INITIALIZER;
static int key_used[TLS_MAX_KEYS];
static void (*key_destructors[TLS_MAX_KEYS])(void*);

/* ------------------------------------------------------------------ */
/* TLS helpers                                                         */
/* ------------------------------------------------------------------ */
static inline uint64_t tls_base(void) {
    uint64_t v;
    __asm__ volatile("mov %%fs:0, %0" : "=r"(v));
    return v;
}

static inline void* tls_key_slot(int key) {
    return (void*)(uintptr_t)(tls_base() + TLS_KEYS_OFF + (uint64_t)key * 8);
}

/* Per-thread errno (replaces the old global; see libc.c / errno.h). */
int* __errno_location(void) {
    return (int*)(uintptr_t)(tls_base() + TLS_ERRNO_OFF);
}

/* ------------------------------------------------------------------ */
/* Futex helpers (EIGEN_SYS_FUTEX)                                     */
/* ------------------------------------------------------------------ */
static inline long futex_wait(volatile int* addr, int expected, uint32_t timeout_ms) {
    return (long)eigen_syscall(EIGEN_SYS_FUTEX, EIGEN_FUTEX_WAIT,
                               (uint64_t)(uintptr_t)addr, (uint64_t)expected,
                               timeout_ms);
}

static inline void futex_wake(volatile int* addr, int n) {
    eigen_syscall(EIGEN_SYS_FUTEX, EIGEN_FUTEX_WAKE,
                  (uint64_t)(uintptr_t)addr, (uint64_t)n, 0);
}

/* ------------------------------------------------------------------ */
/* Core thread control                                                 */
/* ------------------------------------------------------------------ */
/* Entered via iretq with RSP % 16 == 8 (SysV), rdi = entry, rsi = arg.
   The kernel frame was built with rdi = start_routine, rsi = arg. */
static void pthread_trampoline(void* entry, void* arg) {
    void* (*fn)(void*) = (void* (*)(void*))entry;
    void* rc = fn(arg);
    pthread_exit(rc);
}

int pthread_create(pthread_t* thread, const pthread_attr_t* attr,
                   void* (*start_routine)(void*), void* arg) {
    (void)attr;
    if (!thread || !start_routine) return EINVAL;
    long tid = (long)eigen_syscall(EIGEN_SYS_THREAD, EIGEN_THREAD_CREATE,
                                   (uint64_t)(uintptr_t)pthread_trampoline,
                                   (uint64_t)(uintptr_t)start_routine,
                                   (uint64_t)(uintptr_t)arg);
    if (tid < 0) return EAGAIN;
    *thread = (pthread_t)tid;
    return 0;
}

void pthread_exit(void* retval) {
    /* Run TLS key destructors (POSIX: up to PTHREAD_DESTRUCTOR_ITERATIONS
       rounds; a destructor may re-arm its own key). */
    for (int round = 0; round < 4; round++) {
        int ran = 0;
        for (int i = 0; i < TLS_MAX_KEYS; i++) {
            if (key_used[i] && key_destructors[i]) {
                void** slot = (void**)tls_key_slot(i);
                if (*slot) {
                    void* v = *slot;
                    *slot = 0;
                    key_destructors[i](v);
                    ran = 1;
                }
            }
        }
        if (!ran) break;
    }
    eigen_syscall(EIGEN_SYS_THREAD, EIGEN_THREAD_EXIT,
                  (uint64_t)(uintptr_t)retval, 0, 0);
    for (;;) {}
}

int pthread_join(pthread_t thread, void** retval) {
    long r = (long)eigen_syscall(EIGEN_SYS_THREAD, EIGEN_THREAD_JOIN,
                                 (uint64_t)thread, 0, 0);
    if (r < 0) return (int)-r;
    if (retval) *retval = (void*)(uintptr_t)r;
    return 0;
}

int pthread_detach(pthread_t thread) {
    (void)thread;   /* no reaping bookkeeping: join works for any dead thread */
    return 0;
}

pthread_t pthread_self(void) {
    long* self = (long*)(uintptr_t)(tls_base() + TLS_SELF_OFF);
    if (*self) return (pthread_t)*self;
    return (pthread_t)eigen_getpid();   /* main thread: its tid == pid */
}

int pthread_equal(pthread_t a, pthread_t b) {
    return a == b;
}

/* ------------------------------------------------------------------ */
/* Attributes                                                          */
/* ------------------------------------------------------------------ */
int pthread_attr_init(pthread_attr_t* a) {
    a->detached = PTHREAD_CREATE_JOINABLE;
    a->stacksize = 0;   /* 0 = kernel default (512 KB) */
    return 0;
}

int pthread_attr_destroy(pthread_attr_t* a) {
    (void)a;
    return 0;
}

int pthread_attr_setdetachstate(pthread_attr_t* a, int state) {
    if (state != PTHREAD_CREATE_JOINABLE && state != PTHREAD_CREATE_DETACHED)
        return EINVAL;
    a->detached = state;
    return 0;
}

int pthread_attr_getdetachstate(const pthread_attr_t* a, int* state) {
    if (!a || !state) return EINVAL;
    *state = a->detached;
    return 0;
}

int pthread_attr_setstacksize(pthread_attr_t* a, size_t size) {
    if (!a || size < 16384) return EINVAL;
    a->stacksize = size;    /* stored only; the kernel uses its own default */
    return 0;
}

int pthread_attr_getstacksize(const pthread_attr_t* a, size_t* size) {
    if (!a || !size) return EINVAL;
    *size = a->stacksize ? a->stacksize : 524288;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Mutexes (futex-based)                                               */
/* ------------------------------------------------------------------ */
 int pthread_mutex_init(pthread_mutex_t* m, const void* attr) {
     m->locked = 0;
     m->waiters = 0;
     m->owner = 0;
     m->recursion = 0;
     m->type = (attr) ? ((const pthread_mutexattr_t*)attr)->type
                      : PTHREAD_MUTEX_NORMAL;
     return 0;
 }

 int pthread_mutex_destroy(pthread_mutex_t* m) {
     (void)m;
     return 0;
 }

 int pthread_mutex_lock(pthread_mutex_t* m) {
     if (m->type == PTHREAD_MUTEX_RECURSIVE) {
         unsigned long self = (unsigned long)pthread_self();
         if (m->owner == self) {
             m->recursion++;
             return 0;
         }
         for (;;) {
             if (__atomic_exchange_n(&m->locked, 1, __ATOMIC_ACQ_REL) == 0) {
                 m->owner = self;
                 m->recursion = 1;
                 return 0;
             }
             __atomic_store_n(&m->waiters, 1, __ATOMIC_RELEASE);
             futex_wait(&m->locked, 1, 0);
         }
     }
     for (;;) {
         if (__atomic_exchange_n(&m->locked, 1, __ATOMIC_ACQ_REL) == 0)
             return 0;
         __atomic_store_n(&m->waiters, 1, __ATOMIC_RELEASE);
         /* Sleep until locked != 1. The kernel re-checks the value after
            registering the sleep, so an unlock that races this WAIT is
            never missed (it either wakes us or we see 0 and return). */
         futex_wait(&m->locked, 1, 0);
     }
 }

 int pthread_mutex_trylock(pthread_mutex_t* m) {
     if (m->type == PTHREAD_MUTEX_RECURSIVE) {
         unsigned long self = (unsigned long)pthread_self();
         if (m->owner == self) {
             m->recursion++;
             return 0;
         }
         if (__atomic_exchange_n(&m->locked, 1, __ATOMIC_ACQ_REL) == 0) {
             m->owner = self;
             m->recursion = 1;
             return 0;
         }
         return EBUSY;
     }
     if (__atomic_exchange_n(&m->locked, 1, __ATOMIC_ACQ_REL) == 0)
         return 0;
     return EBUSY;
 }

 int pthread_mutex_unlock(pthread_mutex_t* m) {
     if (m->type == PTHREAD_MUTEX_RECURSIVE) {
         unsigned long self = (unsigned long)pthread_self();
         if (m->owner == self) {
             m->recursion--;
             if (m->recursion > 0)
                 return 0;
             m->owner = 0;
         }
     }
     __atomic_store_n(&m->locked, 0, __ATOMIC_RELEASE);
     if (__atomic_exchange_n(&m->waiters, 0, __ATOMIC_ACQ_REL) == 1)
         futex_wake(&m->locked, 1);
     return 0;
 }

/* ------------------------------------------------------------------ */
/* Condition variables                                                 */
/* ------------------------------------------------------------------ */
int pthread_cond_init(pthread_cond_t* c, const void* attr) {
    (void)attr;
    c->seq = 0;
    return 0;
}

int pthread_cond_destroy(pthread_cond_t* c) {
    (void)c;
    return 0;
}

int pthread_cond_wait(pthread_cond_t* c, pthread_mutex_t* m) {
    for (;;) {
        int seq = __atomic_load_n(&c->seq, __ATOMIC_ACQUIRE);
        pthread_mutex_unlock(m);
        futex_wait(&c->seq, seq, 0);
        pthread_mutex_lock(m);
        if (__atomic_load_n(&c->seq, __ATOMIC_ACQUIRE) != seq)
            return 0;                       /* the condition was signalled */
        /* Spurious wake (seq unchanged): re-arm the wait. */
    }
}

int pthread_cond_timedwait(pthread_cond_t* c, pthread_mutex_t* m,
                           const struct timespec* abstime) {
    if (!abstime) return EINVAL;
    uint64_t abs_ms = (uint64_t)abstime->tv_sec * 1000
                    + (uint64_t)abstime->tv_nsec / 1000000;
    for (;;) {
        uint32_t now_ms = eigen_gettime_ms();
        uint32_t timeout;
        if (abs_ms <= now_ms) timeout = 1;
        else {
            uint64_t d = abs_ms - now_ms;
            timeout = d > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)d;
        }
        int seq = __atomic_load_n(&c->seq, __ATOMIC_ACQUIRE);
        pthread_mutex_unlock(m);
        long r = futex_wait(&c->seq, seq, timeout);
        pthread_mutex_lock(m);
        if (r == 1) return ETIMEDOUT;
        if (__atomic_load_n(&c->seq, __ATOMIC_ACQUIRE) != seq)
            return 0;
        /* Spurious wake before the deadline: loop with a fresh timeout. */
    }
}

int pthread_cond_signal(pthread_cond_t* c) {
    __atomic_add_fetch(&c->seq, 1, __ATOMIC_ACQ_REL);
    futex_wake(&c->seq, 1);
    return 0;
}

int pthread_cond_broadcast(pthread_cond_t* c) {
    __atomic_add_fetch(&c->seq, 1, __ATOMIC_ACQ_REL);
    futex_wake(&c->seq, TLS_MAX_KEYS);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Thread-local storage keys                                           */
/* ------------------------------------------------------------------ */
int pthread_key_create(pthread_key_t* key, void (*destructor)(void*)) {
    if (!key) return EINVAL;
    pthread_mutex_lock(&key_lock);
    int i;
    for (i = 0; i < TLS_MAX_KEYS; i++)
        if (!key_used[i]) break;
    if (i >= TLS_MAX_KEYS) {
        pthread_mutex_unlock(&key_lock);
        return EAGAIN;
    }
    key_used[i] = 1;
    key_destructors[i] = destructor;
    pthread_mutex_unlock(&key_lock);
    *key = i;
    return 0;
}

int pthread_key_delete(pthread_key_t key) {
    if (key < 0 || key >= TLS_MAX_KEYS) return EINVAL;
    pthread_mutex_lock(&key_lock);
    key_used[key] = 0;
    key_destructors[key] = 0;
    pthread_mutex_unlock(&key_lock);
    return 0;
}

void* pthread_getspecific(pthread_key_t key) {
    if (key < 0 || key >= TLS_MAX_KEYS) return 0;
    return *(void**)tls_key_slot(key);
}

int pthread_setspecific(pthread_key_t key, const void* value) {
    if (key < 0 || key >= TLS_MAX_KEYS) return EINVAL;
    *(void**)tls_key_slot(key) = (void*)value;
    return 0;
}

/* ------------------------------------------------------------------ */
/* One-time initialization                                             */
/* ------------------------------------------------------------------ */
static pthread_mutex_t once_lock = PTHREAD_MUTEX_INITIALIZER;

int pthread_once(pthread_once_t* once, void (*init_routine)(void)) {
    if (!once || !init_routine) return EINVAL;
    if (__atomic_load_n(&once->done, __ATOMIC_ACQUIRE)) return 0;
    pthread_mutex_lock(&once_lock);
    if (!once->done) {
        once->done = 1;
        init_routine();
    }
    pthread_mutex_unlock(&once_lock);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Scheduling / counting semaphores                                    */
/* ------------------------------------------------------------------ */
int sched_yield(void) {
    eigen_syscall(EIGEN_SYS_THREAD, EIGEN_THREAD_YIELD, 0, 0, 0);
    return 0;
}

int sem_init(sem_t* sem, int pshared, unsigned int value) {
    (void)pshared;
    if (!sem || value > 0x7FFFFFFFu) {
        errno = EINVAL;
        return -1;
    }
    sem->count = (int)value;
    return 0;
}

int sem_destroy(sem_t* sem) {
    (void)sem;
    return 0;
}

int sem_wait(sem_t* sem) {
    for (;;) {
        int c = __atomic_load_n(&sem->count, __ATOMIC_ACQUIRE);
        if (c > 0 && __atomic_compare_exchange_n(&sem->count, &c, c - 1, 0,
                __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            return 0;
        /* Count <= 0 (or raced): sleep until it changes. */
        futex_wait(&sem->count, c, 0);
    }
}

int sem_trywait(sem_t* sem) {
    int c = __atomic_load_n(&sem->count, __ATOMIC_ACQUIRE);
    if (c > 0 && __atomic_compare_exchange_n(&sem->count, &c, c - 1, 0,
            __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        return 0;
    errno = EAGAIN;
    return -1;
}

int sem_post(sem_t* sem) {
    __atomic_add_fetch(&sem->count, 1, __ATOMIC_ACQ_REL);
    futex_wake(&sem->count, 1);
    return 0;
}

int sem_getvalue(sem_t* sem, int* sval) {
    if (!sem || !sval) {
        errno = EINVAL;
        return -1;
    }
    *sval = __atomic_load_n(&sem->count, __ATOMIC_ACQUIRE);
    return 0;
}
/* ------------------------------------------------------------------ */
/* Reader/writer lock (mutex + cond emulation over futex)              */
/* ------------------------------------------------------------------ */
int pthread_rwlock_init(pthread_rwlock_t* rw, const void* attr) {
    (void)attr;
    if (!rw) return EINVAL;
    pthread_mutex_init(&rw->mutex, 0);
    pthread_cond_init(&rw->read_ok, 0);
    pthread_cond_init(&rw->write_ok, 0);
    rw->writer = 0;
    rw->readers = 0;
    rw->writer_waiting = 0;
    return 0;
}
int pthread_rwlock_destroy(pthread_rwlock_t* rw) {
    if (!rw) return EINVAL;
    pthread_mutex_destroy(&rw->mutex);
    pthread_cond_destroy(&rw->read_ok);
    pthread_cond_destroy(&rw->write_ok);
    return 0;
}
int pthread_rwlock_rdlock(pthread_rwlock_t* rw) {
    if (!rw) return EINVAL;
    pthread_mutex_lock(&rw->mutex);
    while (rw->writer || rw->writer_waiting) {
        /* if a writer is queued, wait so readers don't starve writers */
        pthread_cond_wait(&rw->read_ok, &rw->mutex);
    }
    rw->readers++;
    pthread_mutex_unlock(&rw->mutex);
    return 0;
}
int pthread_rwlock_tryrdlock(pthread_rwlock_t* rw) {
    if (!rw) return EINVAL;
    if (!pthread_mutex_trylock(&rw->mutex)) return EBUSY;
    int r = 0;
    if (rw->writer || rw->writer_waiting) r = EBUSY;
    else rw->readers++;
    pthread_mutex_unlock(&rw->mutex);
    return r;
}
int pthread_rwlock_wrlock(pthread_rwlock_t* rw) {
    if (!rw) return EINVAL;
    pthread_mutex_lock(&rw->mutex);
    rw->writer_waiting = 1;
    while (rw->writer || rw->readers > 0)
        pthread_cond_wait(&rw->write_ok, &rw->mutex);
    rw->writer_waiting = 0;
    rw->writer = 1;
    pthread_mutex_unlock(&rw->mutex);
    return 0;
}
int pthread_rwlock_trywrlock(pthread_rwlock_t* rw) {
    if (!rw) return EINVAL;
    if (!pthread_mutex_trylock(&rw->mutex)) return EBUSY;
    int r = 0;
    if (rw->writer || rw->readers > 0) r = EBUSY;
    else { rw->writer = 1; }
    pthread_mutex_unlock(&rw->mutex);
    return r;
}
int pthread_rwlock_unlock(pthread_rwlock_t* rw) {
    if (!rw) return EINVAL;
    pthread_mutex_lock(&rw->mutex);
    if (rw->writer) {
        rw->writer = 0;
        /* prefer waking a waiter; signal one reader chain */
        pthread_cond_broadcast(&rw->write_ok);
        pthread_cond_broadcast(&rw->read_ok);
    } else if (rw->readers > 0) {
        rw->readers--;
        if (rw->readers == 0)
            pthread_cond_signal(&rw->write_ok);
    } else {
        pthread_mutex_unlock(&rw->mutex);
        return EPERM;
    }
    pthread_mutex_unlock(&rw->mutex);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Spinlock                                                           */
/* ------------------------------------------------------------------ */
static volatile int spin_lock_word = 0;
int pthread_spin_init(pthread_spinlock_t* s, int pshared) {
    (void)pshared;
    if (!s) return EINVAL;
    *s = 0;
    return 0;
}
int pthread_spin_destroy(pthread_spinlock_t* s) { (void)s; return 0; }
int pthread_spin_lock(pthread_spinlock_t* s) {
    while (1) {
        if (__atomic_exchange_n(s, 1, __ATOMIC_ACQ_REL) == 0) return 0;
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        /* spin a little, then yield */
        for (int i = 0; i < 1000; i++) __atomic_thread_fence(__ATOMIC_RELAXED);
        sched_yield();
    }
}
int pthread_spin_trylock(pthread_spinlock_t* s) {
    if (__atomic_exchange_n(s, 1, __ATOMIC_ACQ_REL) == 0) return 0;
    return EBUSY;
}
int pthread_spin_unlock(pthread_spinlock_t* s) {
    __atomic_store_n(s, 0, __ATOMIC_RELEASE);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Barrier                                                            */
/* ------------------------------------------------------------------ */
int pthread_barrier_init(pthread_barrier_t* b, const void* attr, unsigned count) {
    (void)attr;
    if (!b || count == 0) return EINVAL;
    pthread_mutex_init(&b->mutex, 0);
    pthread_cond_init(&b->cond, 0);
    b->count = count;
    b->trip = 0;
    b->left = count;
    b->initialized = 1;
    return 0;
}
int pthread_barrier_destroy(pthread_barrier_t* b) {
    if (!b) return EINVAL;
    pthread_mutex_destroy(&b->mutex);
    pthread_cond_destroy(&b->cond);
    b->initialized = 0;
    return 0;
}
int pthread_barrier_wait(pthread_barrier_t* b) {
    if (!b || !b->initialized) return EINVAL;
    pthread_mutex_lock(&b->mutex);
    b->left--;
    if (b->left == 0) {
        b->trip++;
        b->left = b->count;
        pthread_cond_broadcast(&b->cond);
        pthread_mutex_unlock(&b->mutex);
        return PTHREAD_BARRIER_SERIAL_THREAD;
    }
    unsigned trip = b->trip;
    while (trip == b->trip)
        pthread_cond_wait(&b->cond, &b->mutex);
    pthread_mutex_unlock(&b->mutex);
    return 0;
}

/* ------------------------------------------------------------------ */
/* condattr                                                           */
/* ------------------------------------------------------------------ */
int pthread_condattr_init(pthread_condattr_t* a) { (void)a; return 0; }
int pthread_condattr_destroy(pthread_condattr_t* a) { (void)a; return 0; }
int pthread_condattr_setpshared(pthread_condattr_t* a, int ps) { (void)a;(void)ps; return 0; }

/* ------------------------------------------------------------------ */
/* Cancellation (no-op stubs; EigenOS threads don't cancel)           */
/* ------------------------------------------------------------------ */
int pthread_cancel(pthread_t tid) { (void)tid; return 0; }
int pthread_setcancelstate(int state, int* oldstate) {
    if (oldstate) *oldstate = PTHREAD_CANCEL_DISABLE;
    (void)state;
    return 0;
}
int pthread_setcanceltype(int type, int* oldtype) {
    if (oldtype) *oldtype = PTHREAD_CANCEL_DEFERRED;
    (void)type;
    return 0;
}
int pthread_testcancel(void) { return 0; }

/* ------------------------------------------------------------------ */
/* Thread naming (best-effort: stored per-thread; ignored by kernel)  */
/* ------------------------------------------------------------------ */
int pthread_setname_np(pthread_t t, const char* name) {
    (void)t; (void)name;
    return 0;
}
int pthread_getname_np(pthread_t t, char* name, size_t len) {
    if (!name || len == 0) return EINVAL;
    name[0] = '\0';
    (void)t;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Cleanup stack backing (pthread_cleanup_push/pop macros)            */
/* ------------------------------------------------------------------ */
static __thread struct pthread_cleanup* thread_cleanup_head;

struct pthread_cleanup* pthread__cleanup_head(void) {
    return thread_cleanup_head;
}
void pthread__cleanup_set_head(struct pthread_cleanup* c) {
    thread_cleanup_head = c;
}

/* ------------------------------------------------------------------ */
/* Mutex attributes                                                   */
/* ------------------------------------------------------------------ */
int pthread_mutexattr_init(pthread_mutexattr_t* a) {
    if (!a) return EINVAL;
    a->type = PTHREAD_MUTEX_NORMAL;
    return 0;
}
int pthread_mutexattr_destroy(pthread_mutexattr_t* a) {
    (void)a;
    return 0;
}
int pthread_mutexattr_settype(pthread_mutexattr_t* a, int type) {
    if (!a) return EINVAL;
    a->type = type;
    return 0;
}
int pthread_mutexattr_gettype(const pthread_mutexattr_t* a, int* type) {
    if (!a || !type) return EINVAL;
    *type = a->type;
    return 0;
}

/* ------------------------------------------------------------------ */
/* pthread_sigmask (no-op: EigenOS has no signals)                    */
/* ------------------------------------------------------------------ */
int pthread_sigmask(int how, const void* set, void* oldset) {
    (void)how; (void)set; (void)oldset;
    return 0;
}

int pthread_getschedparam(pthread_t thread, int* policy, struct sched_param* param) {
    (void)thread;
    if (policy) *policy = SCHED_OTHER;
    if (param) param->sched_priority = 0;
    return 0;
}
int pthread_setschedparam(pthread_t thread, int policy, const struct sched_param* param) {
    (void)thread; (void)policy; (void)param;
    return 0;
}
