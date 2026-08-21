/* Freestanding semaphore.h for ring-3 user apps (futex-based counting
 * semaphores, see src/user/lib/pthread/pthread.c). */
#ifndef EIGEN_SHIM_SEMAPHORE_H
#define EIGEN_SHIM_SEMAPHORE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sem_t {
    int count;
} sem_t;

int sem_init(sem_t* sem, int pshared, unsigned int value);
int sem_destroy(sem_t* sem);
int sem_wait(sem_t* sem);
int sem_trywait(sem_t* sem);
int sem_post(sem_t* sem);
int sem_getvalue(sem_t* sem, int* sval);

#ifdef __cplusplus
}
#endif

#endif