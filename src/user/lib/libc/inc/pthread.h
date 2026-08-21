/* Freestanding pthread.h for ring-3 user apps (EigenOS pthreads).
 *
 * Threads are first-class kernel tasks sharing the process's address space
 * (see kernel SYS_THREAD / SYS_FUTEX in include/user/eigen.h). The kernel
 * gives each thread its own kernel stack, a 512 KB user stack and a 4 KiB
 * TLS page loaded into IA32_FS_BASE, so errno and pthread keys are truly
 * per-thread.
 *
 * Sync primitives (mutex/cond/semaphore) are futex-based: pure userland
 * atomics plus EIGEN_SYS_FUTEX WAIT/WAKE, so a blocked thread actually
 * sleeps instead of burning the CPU.
 */
#ifndef EIGEN_SHIM_PTHREAD_H
#define EIGEN_SHIM_PTHREAD_H

#include <stddef.h>
#include <sched.h>   /* struct sched_param for getschedparam */
#include <signal.h>  /* sigset_t for pthread_sigmask */

/* Minimal POSIX timespec (libc time.h has none) for pthread_cond_timedwait. */
#ifndef EIGEN_TIMESPEC_DEFINED
#define EIGEN_TIMESPEC_DEFINED
struct timespec {
    long tv_sec;
    long tv_nsec;
};
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef int pthread_t;
typedef int pthread_key_t;
typedef struct pthread_once_t {
    int done;
} pthread_once_t;

#define PTHREAD_ONCE_INIT {0}

/* --- Attributes --- */
typedef struct pthread_attr {
    int detached;
    size_t stacksize;   /* accepted for source compat; kernel uses its own 512 KB default */
} pthread_attr_t;

#define PTHREAD_CREATE_JOINABLE 0
#define PTHREAD_CREATE_DETACHED 1

/* --- Mutex --- */
typedef struct pthread_mutex {
    int locked;          /* 0 = free, 1 = held */
    int waiters;         /* sticky flag: somebody may be asleep in futex WAIT */
    unsigned long owner; /* tid of owning thread (recursive mutexes only); 0 = none */
    int recursion;       /* recursion depth for PTHREAD_MUTEX_RECURSIVE */
    int type;            /* PTHREAD_MUTEX_NORMAL or PTHREAD_MUTEX_RECURSIVE */
} pthread_mutex_t;

#define PTHREAD_MUTEX_INITIALIZER {0, 0, 0, 0, 0}

/* --- Condition variable --- */
typedef struct pthread_cond {
    int seq;       /* bumped on every signal/broadcast; waiters sleep on it */
} pthread_cond_t;

#define PTHREAD_COND_INITIALIZER {0}

/* --- Reader/writer lock (mutex+cond emulation) --- */
typedef struct pthread_rwlock {
    pthread_mutex_t mutex;   /* protects the state below */
    pthread_cond_t  read_ok; /* signaled when a writer releases */
    pthread_cond_t  write_ok; /* signaled when the lock is released (read or write) */
    int writer;      /* 1 if a writer holds the lock */
    int readers;     /* count of active readers */
    int writer_waiting;  /* 1 if a writer is queued, to avoid writer starvation */
} pthread_rwlock_t;

#define PTHREAD_RWLOCK_INITIALIZER { PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, PTHREAD_COND_INITIALIZER, 0, 0, 0 }

/* --- Spinlock --- */
typedef int pthread_spinlock_t;

/* --- Cleanup --- */
typedef void (*pthread_cleanup_routine)(void*);
typedef struct pthread_cleanup {
    pthread_cleanup_routine routine;
    void* arg;
    int execute;
    struct pthread_cleanup* next;
} pthread_cleanup_t;
/* pthread_cleanup_push/pop are macros that manipulate thread-local stack.
 * NOTE: parameter names (func/dat/exec) are deliberately different from the
 * struct member names (routine/arg/execute) so the preprocessor does not
 * substitute the parameter token into the member-access expression. */
#define pthread_cleanup_push(func, dat)                         \
    do {                                                        \
        pthread_cleanup_t _cleanup;                             \
        _cleanup.routine = (func);                             \
        _cleanup.arg = (dat);                                  \
        _cleanup.execute = 1;                                  \
        _cleanup.next = pthread__cleanup_head();               \
        pthread__cleanup_set_head(&_cleanup);                  \
        if (0)

#define pthread_cleanup_pop(exec)                               \
        pthread__cleanup_set_head(_cleanup.next);              \
        if ((exec) && _cleanup.execute)                        \
            (_cleanup.routine)(_cleanup.arg);                  \
    } while (0)
/* TLS helpers backing the push/pop macros (implemented in pthread.c) */
struct pthread_cleanup* pthread__cleanup_head(void);
void pthread__cleanup_set_head(struct pthread_cleanup* c);

/* Core thread control */
int pthread_create(pthread_t* thread, const pthread_attr_t* attr,
                   void* (*start_routine)(void*), void* arg);
void pthread_exit(void* retval);
int pthread_join(pthread_t thread, void** retval);
int pthread_detach(pthread_t thread);
pthread_t pthread_self(void);
int pthread_equal(pthread_t a, pthread_t b);

int pthread_attr_init(pthread_attr_t* attr);
int pthread_attr_destroy(pthread_attr_t* attr);
int pthread_attr_setdetachstate(pthread_attr_t* attr, int state);
int pthread_attr_getdetachstate(const pthread_attr_t* attr, int* state);
int pthread_attr_setstacksize(pthread_attr_t* attr, size_t size);
int pthread_attr_getstacksize(const pthread_attr_t* attr, size_t* size);

/* Mutexes */
int pthread_mutex_init(pthread_mutex_t* m, const void* attr);
int pthread_mutex_destroy(pthread_mutex_t* m);
int pthread_mutex_lock(pthread_mutex_t* m);
int pthread_mutex_trylock(pthread_mutex_t* m);
int pthread_mutex_unlock(pthread_mutex_t* m);

/* Mutex attributes */
typedef struct pthread_mutexattr {
    int type;
} pthread_mutexattr_t;
#define PTHREAD_MUTEX_NORMAL     0
#define PTHREAD_MUTEX_RECURSIVE  1
#define PTHREAD_MUTEX_ERRORCHECK 2
int pthread_mutexattr_init(pthread_mutexattr_t* a);
int pthread_mutexattr_destroy(pthread_mutexattr_t* a);
int pthread_mutexattr_settype(pthread_mutexattr_t* a, int type);
int pthread_mutexattr_gettype(const pthread_mutexattr_t* a, int* type);

/* Signal mask (stub: EigenOS has no signals, but Eina calls pthread_sigmask) */
int pthread_sigmask(int how, const void* set, void* oldset);

/* Condition variables */
int pthread_cond_init(pthread_cond_t* c, const void* attr);
int pthread_cond_destroy(pthread_cond_t* c);
int pthread_cond_wait(pthread_cond_t* c, pthread_mutex_t* m);
int pthread_cond_timedwait(pthread_cond_t* c, pthread_mutex_t* m,
                           const struct timespec* abstime);
int pthread_cond_signal(pthread_cond_t* c);
int pthread_cond_broadcast(pthread_cond_t* c);

/* Reader/writer locks */
int pthread_rwlock_init(pthread_rwlock_t* rw, const void* attr);
int pthread_rwlock_destroy(pthread_rwlock_t* rw);
int pthread_rwlock_rdlock(pthread_rwlock_t* rw);
int pthread_rwlock_wrlock(pthread_rwlock_t* rw);
int pthread_rwlock_unlock(pthread_rwlock_t* rw);
int pthread_rwlock_tryrdlock(pthread_rwlock_t* rw);
int pthread_rwlock_trywrlock(pthread_rwlock_t* rw);

/* Spinlocks */
int pthread_spin_init(pthread_spinlock_t* s, int pshared);
int pthread_spin_destroy(pthread_spinlock_t* s);
int pthread_spin_lock(pthread_spinlock_t* s);
int pthread_spin_trylock(pthread_spinlock_t* s);
int pthread_spin_unlock(pthread_spinlock_t* s);

/* Cancellation (stubs for source compat — EigenOS has no cancellation) */
#define PTHREAD_CANCEL_DISABLE 0
#define PTHREAD_CANCEL_ENABLE  1
#define PTHREAD_CANCEL_DEFERRED 0
#define PTHREAD_CANCEL_ASYNCHRONOUS 1
#define PTHREAD_CANCELED ((void*)1)
int pthread_cancel(pthread_t tid);
int pthread_setcancelstate(int state, int* oldstate);
int pthread_setcanceltype(int type, int* oldtype);
int pthread_testcancel(void);

/* Scheduling parameters (stubs — single-priority, single-CPU port) */
int pthread_getschedparam(pthread_t thread, int* policy, struct sched_param* param);
int pthread_setschedparam(pthread_t thread, int policy, const struct sched_param* param);

/* Barriers */
typedef struct pthread_barrier {
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
    unsigned count;
    unsigned trip;
    unsigned left;
    int initialized;
} pthread_barrier_t;
int pthread_barrier_init(pthread_barrier_t* b, const void* attr, unsigned count);
int pthread_barrier_destroy(pthread_barrier_t* b);
int pthread_barrier_wait(pthread_barrier_t* b);
#define PTHREAD_BARRIER_SERIAL_THREAD -1

/* condattr */

typedef struct pthread_condattr { int dummy; } pthread_condattr_t;
int pthread_condattr_init(pthread_condattr_t* a);
int pthread_condattr_destroy(pthread_condattr_t* a);
int pthread_condattr_setpshared(pthread_condattr_t* a, int ps);

/* Thread naming (best-effort; for debugger/owner visibility) */
int pthread_setname_np(pthread_t t, const char* name);
int pthread_getname_np(pthread_t t, char* name, size_t len);

/* Thread-local storage keys */
int pthread_key_create(pthread_key_t* key, void (*destructor)(void*));
int pthread_key_delete(pthread_key_t key);
void* pthread_getspecific(pthread_key_t key);
int pthread_setspecific(pthread_key_t key, const void* value);

/* One-time initialization */
int pthread_once(pthread_once_t* once, void (*init_routine)(void));

#ifdef __cplusplus
}
#endif

#endif