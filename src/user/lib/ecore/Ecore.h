/* Ecore.h — minimal freestanding port of the classic Ecore main-loop API.
 *
 * This is an adaptation of the EFL 1.26 core scheduling semantics (timers,
 * idlers, animators, pollers, jobs, idle enterers/exiters and the main loop)
 * implemented without the Efl interface layer, since that layer requires the
 * eolian code generator which is unavailable in this freestanding port.
 * The public symbols and callback contracts match upstream Ecore so EFL-style
 * applications can be built against it. */
#ifndef _ECORE_H
#define _ECORE_H

#include <stdint.h>

#define EAPI
#define ECORE_CALLBACK_RENEW   1
#define ECORE_CALLBACK_CANCEL  0
#define ECORE_CALLBACK_PASS_ON 1
#define ECORE_CALLBACK_DONE    0

typedef void (*Ecore_Cb)(void *data);
typedef int  (*Ecore_Task_Cb)(void *data);
typedef void (*Ecore_Data_Cb)(void *data);

#ifdef __cplusplus
extern "C" {
#endif

int  ecore_init(void);
int  ecore_shutdown(void);

void ecore_main_loop_begin(void);
void ecore_main_loop_quit(void);

/* Timers */
typedef struct _Ecore_Timer Ecore_Timer;
Ecore_Timer *ecore_timer_add(double interval, Ecore_Task_Cb func, const void *data);
void         ecore_timer_del(Ecore_Timer *timer);
double       ecore_timer_interval_get(const Ecore_Timer *timer);
void         ecore_timer_interval_set(Ecore_Timer *timer, double interval);
double       ecore_timer_pending_get(const Ecore_Timer *timer);
void         ecore_timer_reset(Ecore_Timer *timer);
void         ecore_timer_freeze(Ecore_Timer *timer);
void         ecore_timer_thaw(Ecore_Timer *timer);

/* Idlers */
typedef struct _Ecore_Idler Ecore_Idler;
Ecore_Idler *ecore_idler_add(Ecore_Task_Cb func, const void *data);
void         ecore_idler_del(Ecore_Idler *idler);

/* Animators */
typedef struct _Ecore_Animator Ecore_Animator;
Ecore_Animator *ecore_animator_add(Ecore_Task_Cb func, const void *data);
void            ecore_animator_del(Ecore_Animator *animator);
double          ecore_animator_frametime_get(void);
void            ecore_animator_frametime_set(double frametime);

/* Pollers */
typedef struct _Ecore_Poller Ecore_Poller;
Ecore_Poller *ecore_poller_add(int interval, Ecore_Task_Cb func, const void *data);
void          ecore_poller_del(Ecore_Poller *poller);
double        ecore_poller_poll_interval_get(void);
void          ecore_poller_poll_interval_set(double interval);

/* Jobs (run once on next loop iteration) */
typedef struct _Ecore_Job Ecore_Job;
Ecore_Job *ecore_job_add(Ecore_Cb func, const void *data);
void       ecore_job_del(Ecore_Job *job);

/* Idle enterers/exiters */
typedef struct _Ecore_Idle_Enterer Ecore_Idle_Enterer;
Ecore_Idle_Enterer *ecore_idle_enterer_add(Ecore_Task_Cb func, const void *data);
void                ecore_idle_enterer_del(Ecore_Idle_Enterer *idle_enterer);
typedef struct _Ecore_Idle_Exiter Ecore_Idle_Exiter;
Ecore_Idle_Exiter *ecore_idle_exiter_add(Ecore_Task_Cb func, const void *data);
void               ecore_idle_exiter_del(Ecore_Idle_Exiter *idle_exiter);

/* Time */
double ecore_time_get(void);
double ecore_loop_time_get(void);
double ecore_time_unix_get(void);

#ifdef __cplusplus
}
#endif
#endif /* _ECORE_H */
