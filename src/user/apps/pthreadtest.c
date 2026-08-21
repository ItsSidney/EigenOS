/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

/* pthreadtest.c — exercises the EigenOS pthreads:
 * create/join/self, mutex-protected counters, condvar ping-pong,
 * semaphores, per-thread TLS keys, pthread_once and per-thread errno. */

#include <pthread.h>
#include <semaphore.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <posix.h>

static int fails = 0;
#define CHECK(cond, msg) do { \
    if (cond) printf("  [PASS] %s\n", msg); \
    else { printf("  [FAIL] %s\n", msg); fails++; } \
} while (0)

/* ---- shared counter under a mutex ---- */
static pthread_mutex_t counter_mutex = PTHREAD_MUTEX_INITIALIZER;
static long shared_counter = 0;
#define WORKERS 4
#define ITERS   20000

static void* counter_worker(void* arg) {
    long my = (long)arg;
    for (int i = 0; i < ITERS; i++) {
        pthread_mutex_lock(&counter_mutex);
        shared_counter++;
        pthread_mutex_unlock(&counter_mutex);
    }
    printf("  [worker %ld] done, self=%d\n", my, (int)pthread_self());
    return (void*)(my * 10);
}

/* ---- condvar ping-pong ---- */
static pthread_mutex_t ping_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  ping_cond  = PTHREAD_COND_INITIALIZER;
static int ping_state = 0;          /* 0 = pinger's turn, 1 = ponger's turn */

static void* pinger(void* arg) {
    (void)arg;
    for (int i = 0; i < 50; i++) {
        pthread_mutex_lock(&ping_mutex);
        while (ping_state != 0)
            pthread_cond_wait(&ping_cond, &ping_mutex);
        ping_state = 1;
        pthread_cond_signal(&ping_cond);
        pthread_mutex_unlock(&ping_mutex);
    }
    return 0;
}

static void* ponger(void* arg) {
    (void)arg;
    for (int i = 0; i < 50; i++) {
        pthread_mutex_lock(&ping_mutex);
        while (ping_state != 1)
            pthread_cond_wait(&ping_cond, &ping_mutex);
        ping_state = 0;
        pthread_cond_signal(&ping_cond);
        pthread_mutex_unlock(&ping_mutex);
    }
    return 0;
}

/* ---- semaphore producer/consumer ---- */
static sem_t test_sem;

static void* sem_consumer(void* arg) {
    (void)arg;
    int got = 0;
    for (int i = 0; i < 5; i++) { sem_wait(&test_sem); got++; }
    return (void*)(long)got;
}

/* ---- per-thread keys + destructor ---- */
static pthread_key_t tls_key;
static int key_dtor_called = 0;

static void key_dtor(void* v) { key_dtor_called += (int)(long)v; }

static void* key_worker(void* arg) {
    if (pthread_setspecific(tls_key, arg) != 0) return (void*)1;
    return pthread_getspecific(tls_key);
}

/* ---- pthread_once ---- */
static pthread_once_t once_ctrl = PTHREAD_ONCE_INIT;
static int once_count = 0;

static void once_fn(void) { once_count++; }

static void* once_worker(void* arg) {
    (void)arg;
    pthread_once(&once_ctrl, once_fn);
    return 0;
}

/* ---- detached thread, completion via semaphore ---- */
static sem_t detach_sem;

static void* detach_worker(void* arg) {
    (void)arg;
    sem_post(&detach_sem);
    return 0;
}

int main(void) {
    printf("[pthreadtest] ring-3 pthread tests\n");

    /* pthread_self of the main thread == getpid() */
    CHECK((int)pthread_self() == (int)getpid(), "main pthread_self == getpid");

    /* per-thread errno: main's errno must not leak into workers */
    errno = ENOENT;
    CHECK(errno == ENOENT, "main errno settable");

    /* ---- mutex counter ---- */
    pthread_t workers[WORKERS];
    shared_counter = 0;
    for (int i = 0; i < WORKERS; i++) {
        int r = pthread_create(&workers[i], 0, counter_worker, (void*)(long)i);
        CHECK(r == 0, "pthread_create worker");
        if (r != 0) { fails++; break; }
    }
    int joinerr = 0, joinval = -1;
    for (int i = 0; i < WORKERS; i++) {
        void* rc = 0;
        if (pthread_join(workers[i], &rc) != 0) joinerr++;
        else joinval = (int)(long)rc;
        if (i == 0) CHECK(joinval == 0, "worker 0 exit code via join");
    }
    CHECK(joinerr == 0, "all workers joined");
    CHECK(shared_counter == WORKERS * ITERS,
          "mutex counter == WORKERS * ITERS (no lost updates)");

    /* ---- condvar ping-pong ---- */
    pthread_t t1, t2;
    ping_state = 0;
    CHECK(pthread_create(&t1, 0, pinger, 0) == 0, "create pinger");
    CHECK(pthread_create(&t2, 0, ponger, 0) == 0, "create ponger");
    pthread_join(t1, 0);
    pthread_join(t2, 0);
    CHECK(ping_state == 0, "condvar ping-pong completed (50 rounds)");

    /* ---- semaphore ---- */
    CHECK(sem_init(&test_sem, 0, 0) == 0, "sem_init(0)");
    pthread_t sc;
    CHECK(pthread_create(&sc, 0, sem_consumer, 0) == 0, "create sem consumer");
    sched_yield();                          /* let the consumer block first */
    for (int i = 0; i < 5; i++) CHECK(sem_post(&test_sem) == 0, "sem_post");
    void* got = 0;
    pthread_join(sc, &got);
    CHECK((int)(long)got == 5, "sem consumer received 5");

    /* ---- TLS keys ---- */
    CHECK(pthread_key_create(&tls_key, key_dtor) == 0, "pthread_key_create");
    pthread_t kw;
    pthread_create(&kw, 0, key_worker, (void*)42);
    void* krc = 0;
    pthread_join(kw, &krc);
    CHECK((int)(long)krc == 42, "key value round-trip through TLS");
    CHECK(pthread_getspecific(tls_key) == 0, "main thread key untouched");

    /* ---- pthread_once ---- */
    pthread_t ow[3];
    for (int i = 0; i < 3; i++) pthread_create(&ow[i], 0, once_worker, 0);
    for (int i = 0; i < 3; i++) pthread_join(ow[i], 0);
    CHECK(once_count == 1, "pthread_once ran exactly once");

    /* ---- detached thread ---- */
    sem_init(&detach_sem, 0, 0);
    pthread_t dt;
    pthread_attr_t da;
    pthread_attr_init(&da);
    pthread_attr_setdetachstate(&da, PTHREAD_CREATE_DETACHED);
    CHECK(pthread_create(&dt, &da, detach_worker, 0) == 0, "create detached");
    sem_wait(&detach_sem);
    CHECK(pthread_detach(dt) == 0, "pthread_detach");

    /* ---- trylock / timedwait ---- */
    pthread_mutex_t lm = PTHREAD_MUTEX_INITIALIZER;
    CHECK(pthread_mutex_trylock(&lm) == 0, "mutex trylock free");
    CHECK(pthread_mutex_trylock(&lm) == EBUSY, "mutex trylock busy");
    pthread_mutex_unlock(&lm);

    pthread_cond_t tc = PTHREAD_COND_INITIALIZER;
    struct timespec ts;
    ts.tv_sec = time(0) + 1;                /* 1 s in the future */
    ts.tv_nsec = 0;
    CHECK(pthread_cond_timedwait(&tc, &lm, &ts) == ETIMEDOUT,
          "cond timedwait times out");

    printf(fails ? "[pthreadtest] %d FAILURES\n" : "[pthreadtest] all passed\n",
           fails);
    return fails ? 1 : 0;
}