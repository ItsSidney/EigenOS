/* ecore.c — freestanding implementation of the classic Ecore main loop.
 * Adapted from EFL 1.26 scheduling semantics; no Efl interface dependency. */
#include "Ecore.h"
#include "userlib.h"   /* eigen_gettime_ms, eigen_sleep_ms */
#include <stdlib.h>
#include <string.h>

/* ── internal node structs ─────────────────────────────────────────────── */
struct _Ecore_Timer {
    struct _Ecore_Timer *next, *prev;
    double  interval;
    double  expires;
    int     frozen;
    int     delete_me;
    int     refs;
    Ecore_Task_Cb func;
    void   *data;
};
struct _Ecore_Idler {
    struct _Ecore_Idler *next, *prev;
    int     delete_me; int refs;
    Ecore_Task_Cb func; void *data;
};
struct _Ecore_Animator {
    struct _Ecore_Animator *next, *prev;
    int     delete_me; int refs;
    Ecore_Task_Cb func; void *data;
};
struct _Ecore_Poller {
    struct _Ecore_Poller *next, *prev;
    int     interval;            /* run every `interval` loop iterations */
    int     delete_me; int refs;
    Ecore_Task_Cb func; void *data;
};
struct _Ecore_Job {
    struct _Ecore_Job *next, *prev;
    int     delete_me; int refs;
    Ecore_Cb func; void *data;
};
struct _Ecore_Idle_Enterer {
    struct _Ecore_Idle_Enterer *next, *prev;
    int     delete_me; int refs;
    Ecore_Task_Cb func; void *data;
};
struct _Ecore_Idle_Exiter {
    struct _Ecore_Idle_Exiter *next, *prev;
    int     delete_me; int refs;
    Ecore_Task_Cb func; void *data;
};

/* ── global state ─────────────────────────────────────────────────────── */
static int   _init_count = 0;
static int   _quit = 0;
static double _loop_time = 0.0;
static double _anim_frametime = 1.0 / 30.0;
static double _last_anim = 0.0;
static unsigned int _iter = 0;
static int   _in_idle = 0;

static Ecore_Timer        *_timers = NULL;
static Ecore_Idler        *_idlers = NULL;
static Ecore_Animator     *_animators = NULL;
static Ecore_Poller       *_pollers = NULL;
static Ecore_Job          *_jobs = NULL;
static Ecore_Idle_Enterer *_idle_enterers = NULL;
static Ecore_Idle_Exiter  *_idle_exiters = NULL;

#define EPOCH_BASE 1700000000.0

/* ── list helpers ─────────────────────────────────────────────────────── */
#define LIST_INSERT(head, n) do { \
    (n)->next = (head); (n)->prev = NULL; \
    if (head) (head)->prev = (n); \
    (head) = (n); \
} while (0)
#define LIST_REMOVE(head, n) do { \
    if ((n)->prev) (n)->prev->next = (n)->next; else (head) = (n)->next; \
    if ((n)->next) (n)->next->prev = (n)->prev; \
} while (0)

/* ── time ─────────────────────────────────────────────────────────────── */
double ecore_time_get(void) {
    return (double)eigen_gettime_ms() / 1000.0;
}
double ecore_loop_time_get(void) { return _loop_time; }
double ecore_time_unix_get(void) { return EPOCH_BASE + ecore_time_get(); }

/* ── init / shutdown ───────────────────────────────────────────────────── */
int ecore_init(void) {
    if (_init_count == 0) {
        _quit = 0;
        _loop_time = ecore_time_get();
        _last_anim = _loop_time;
        _iter = 0;
        _in_idle = 0;
    }
    return ++_init_count;
}
int ecore_shutdown(void) {
    _init_count--;
    if (_init_count > 0) return _init_count;
    /* free everything */
    while (_timers)      { Ecore_Timer *n=_timers; LIST_REMOVE(_timers,n); free(n); }
    while (_idlers)      { Ecore_Idler *n=_idlers; LIST_REMOVE(_idlers,n); free(n); }
    while (_animators)   { Ecore_Animator *n=_animators; LIST_REMOVE(_animators,n); free(n); }
    while (_pollers)     { Ecore_Poller *n=_pollers; ; LIST_REMOVE(_pollers,n); if(!n)break; free(n); }
    while (_jobs)        { Ecore_Job *n=_jobs; LIST_REMOVE(_jobs,n); free(n); }
    while (_idle_enterers){ Ecore_Idle_Enterer *n=_idle_enterers; LIST_REMOVE(_idle_enterers,n); free(n); }
    while (_idle_exiters){ Ecore_Idle_Exiter *n=_idle_exiters; LIST_REMOVE(_idle_exiters,n); free(n); }
    return 0;
}
void ecore_main_loop_quit(void) { _quit = 1; }

/* ── timers ───────────────────────────────────────────────────────────── */
Ecore_Timer *ecore_timer_add(double interval, Ecore_Task_Cb func, const void *data) {
    if (interval <= 0.0) interval = 0.0;
    Ecore_Timer *t = calloc(1, sizeof(Ecore_Timer));
    if (!t) return NULL;
    t->interval = interval;
    t->func = func;
    t->data = (void*)data;
    t->expires = _loop_time + interval;
    t->frozen = 0; t->delete_me = 0; t->refs = 0;
    LIST_INSERT(_timers, t);
    return t;
}
void ecore_timer_del(Ecore_Timer *timer) {
    if (!timer || timer->delete_me) return;
    timer->delete_me = 1;
    if (timer->refs == 0) { LIST_REMOVE(_timers, timer); free(timer); }
}
double ecore_timer_interval_get(const Ecore_Timer *timer) { return timer ? timer->interval : 0.0; }
void   ecore_timer_interval_set(Ecore_Timer *timer, double interval) {
    if (!timer) return;
    timer->interval = interval > 0.0 ? interval : 0.0;
}
double ecore_timer_pending_get(const Ecore_Timer *timer) {
    if (!timer) return 0.0;
    double p = timer->expires - _loop_time;
    return p > 0.0 ? p : 0.0;
}
void ecore_timer_reset(Ecore_Timer *timer) {
    if (!timer) return;
    timer->expires = _loop_time + timer->interval;
}
void ecore_timer_freeze(Ecore_Timer *timer) { if (timer) timer->frozen = 1; }
void ecore_timer_thaw(Ecore_Timer *timer)   { if (timer) timer->frozen = 0; }

static double _next_timer_expiry(void) {
    double min = 1e300;
    for (Ecore_Timer *t = _timers; t; t = t->next)
        if (!t->frozen && !t->delete_me && t->expires < min) min = t->expires;
    return min;
}
static void _run_timers(void) {
    for (Ecore_Timer *t = _timers; t; ) {
        Ecore_Timer *nxt = t->next;
        if (t->delete_me) { if (t->refs == 0) { LIST_REMOVE(_timers, t); free(t); } t = nxt; continue; }
        if (t->frozen || t->expires > _loop_time) { t = nxt; continue; }
        t->refs++;
        int r = t->func ? t->func(t->data) : ECORE_CALLBACK_CANCEL;
        t->refs--;
        if (r == ECORE_CALLBACK_RENEW && !t->delete_me)
            t->expires = _loop_time + t->interval;
        else
            t->delete_me = 1;
        if (t->delete_me && t->refs == 0) { LIST_REMOVE(_timers, t); free(t); }
        t = nxt;
    }
}

/* ── idlers ────────────────────────────────────────────────────────────── */
Ecore_Idler *ecore_idler_add(Ecore_Task_Cb func, const void *data) {
    Ecore_Idler *n = calloc(1, sizeof(Ecore_Idler));
    if (!n) return NULL;
    n->func = func; n->data = (void*)data;
    LIST_INSERT(_idlers, n);
    return n;
}
void ecore_idler_del(Ecore_Idler *idler) {
    if (!idler || idler->delete_me) return;
    idler->delete_me = 1;
    if (idler->refs == 0) { LIST_REMOVE(_idlers, idler); free(idler); }
}
static int _run_idlers(void) {
    int ran = 0;
    for (Ecore_Idler *n = _idlers; n; ) {
        Ecore_Idler *nx = n->next;
        if (n->delete_me) { if (n->refs == 0) { LIST_REMOVE(_idlers, n); free(n); } n = nx; continue; }
        n->refs++;
        int r = n->func ? n->func(n->data) : ECORE_CALLBACK_CANCEL;
        n->refs--;
        ran = 1;
        if (r == ECORE_CALLBACK_RENEW && !n->delete_me) { /* keep */ }
        else n->delete_me = 1;
        if (n->delete_me && n->refs == 0) { LIST_REMOVE(_idlers, n); free(n); }
        n = nx;
    }
    return ran;
}

/* ── animators ─────────────────────────────────────────────────────────── */
Ecore_Animator *ecore_animator_add(Ecore_Task_Cb func, const void *data) {
    Ecore_Animator *n = calloc(1, sizeof(Ecore_Animator));
    if (!n) return NULL;
    n->func = func; n->data = (void*)data;
    LIST_INSERT(_animators, n);
    return n;
}
void ecore_animator_del(Ecore_Animator *animator) {
    if (!animator || animator->delete_me) return;
    animator->delete_me = 1;
    if (animator->refs == 0) { LIST_REMOVE(_animators, animator); free(animator); }
}
double ecore_animator_frametime_get(void) { return _anim_frametime; }
void   ecore_animator_frametime_set(double frametime) {
    if (frametime > 0.0) _anim_frametime = frametime;
}
static void _run_animators(void) {
    for (Ecore_Animator *n = _animators; n; ) {
        Ecore_Animator *nx = n->next;
        if (n->delete_me) { if (n->refs == 0) { LIST_REMOVE(_animators, n); free(n); } n = nx; continue; }
        n->refs++;
        int r = n->func ? n->func(n->data) : ECORE_CALLBACK_CANCEL;
        n->refs--;
        if (r == ECORE_CALLBACK_RENEW && !n->delete_me) { /* keep */ }
        else n->delete_me = 1;
        if (n->delete_me && n->refs == 0) { LIST_REMOVE(_animators, n); free(n); }
        n = nx;
    }
}

/* ── pollers ───────────────────────────────────────────────────────────── */
Ecore_Poller *ecore_poller_add(int interval, Ecore_Task_Cb func, const void *data) {
    Ecore_Poller *n = calloc(1, sizeof(Ecore_Poller));
    if (!n) return NULL;
    n->interval = interval > 0 ? interval : 1;
    n->func = func; n->data = (void*)data;
    LIST_INSERT(_pollers, n);
    return n;
}
void ecore_poller_del(Ecore_Poller *poller) {
    if (!poller || poller->delete_me) return;
    poller->delete_me = 1;
    if (poller->refs == 0) { LIST_REMOVE(_pollers, poller); free(poller); }
}
double ecore_poller_poll_interval_get(void) { return 0.0; }
void   ecore_poller_poll_interval_set(double interval) { (void)interval; }
static void _run_pollers(void) {
    for (Ecore_Poller *n = _pollers; n; ) {
        Ecore_Poller *nx = n->next;
        if (n->delete_me) { if (n->refs == 0) { LIST_REMOVE(_pollers, n); free(n); } n = nx; continue; }
        if (_iter % (unsigned int)n->interval == 0) {
            n->refs++;
            int r = n->func ? n->func(n->data) : ECORE_CALLBACK_CANCEL;
            n->refs--;
            if (r != ECORE_CALLBACK_RENEW) n->delete_me = 1;
            if (n->delete_me && n->refs == 0) { LIST_REMOVE(_pollers, n); free(n); }
        }
        n = nx;
    }
}

/* ── jobs ──────────────────────────────────────────────────────────────── */
Ecore_Job *ecore_job_add(Ecore_Cb func, const void *data) {
    Ecore_Job *n = calloc(1, sizeof(Ecore_Job));
    if (!n) return NULL;
    n->func = func; n->data = (void*)data;
    LIST_INSERT(_jobs, n);
    return n;
}
void ecore_job_del(Ecore_Job *job) {
    if (!job || job->delete_me) return;
    job->delete_me = 1;
    if (job->refs == 0) { LIST_REMOVE(_jobs, job); free(job); }
}
static void _run_jobs(void) {
    /* snapshot current jobs; new jobs added during run go to next iteration */
    Ecore_Job *head = _jobs;
    _jobs = NULL;
    for (Ecore_Job *n = head; n; ) {
        Ecore_Job *nx = n->next;
        if (n->delete_me) { if (n->refs == 0) free(n); n = nx; continue; }
        n->refs++;
        if (n->func) n->func(n->data);
        n->refs--;
        if (n->delete_me && n->refs == 0) free(n);
        else if (!n->delete_me) { /* already removed from list; drop it */ free(n); }
        n = nx;
    }
}

/* ── idle enterers / exiters ───────────────────────────────────────────── */
Ecore_Idle_Enterer *ecore_idle_enterer_add(Ecore_Task_Cb func, const void *data) {
    Ecore_Idle_Enterer *n = calloc(1, sizeof(Ecore_Idle_Enterer));
    if (!n) return NULL;
    n->func = func; n->data = (void*)data;
    LIST_INSERT(_idle_enterers, n);
    return n;
}
void ecore_idle_enterer_del(Ecore_Idle_Enterer *ie) {
    if (!ie || ie->delete_me) return;
    ie->delete_me = 1;
    if (ie->refs == 0) { LIST_REMOVE(_idle_enterers, ie); free(ie); }
}
Ecore_Idle_Exiter *ecore_idle_exiter_add(Ecore_Task_Cb func, const void *data) {
    Ecore_Idle_Exiter *n = calloc(1, sizeof(Ecore_Idle_Exiter));
    if (!n) return NULL;
    n->func = func; n->data = (void*)data;
    LIST_INSERT(_idle_exiters, n);
    return n;
}
void ecore_idle_exiter_del(Ecore_Idle_Exiter *ix) {
    if (!ix || ix->delete_me) return;
    ix->delete_me = 1;
    if (ix->refs == 0) { LIST_REMOVE(_idle_exiters, ix); free(ix); }
}
static void _run_idle_enterers(void) {
    for (Ecore_Idle_Enterer *n = _idle_enterers; n; ) {
        Ecore_Idle_Enterer *nx = n->next;
        if (n->delete_me) { if (n->refs == 0) { LIST_REMOVE(_idle_enterers, n); free(n); } n = nx; continue; }
        n->refs++; int r = n->func ? n->func(n->data) : ECORE_CALLBACK_CANCEL; n->refs--;
        if (r != ECORE_CALLBACK_RENEW) n->delete_me = 1;
        if (n->delete_me && n->refs == 0) { LIST_REMOVE(_idle_enterers, n); free(n); }
        n = nx;
    }
}
static void _run_idle_exiters(void) {
    for (Ecore_Idle_Exiter *n = _idle_exiters; n; ) {
        Ecore_Idle_Exiter *nx = n->next;
        if (n->delete_me) { if (n->refs == 0) { LIST_REMOVE(_idle_exiters, n); free(n); } n = nx; continue; }
        n->refs++; int r = n->func ? n->func(n->data) : ECORE_CALLBACK_CANCEL; n->refs--;
        if (r != ECORE_CALLBACK_RENEW) n->delete_me = 1;
        if (n->delete_me && n->refs == 0) { LIST_REMOVE(_idle_exiters, n); free(n); }
        n = nx;
    }
}

/* ── main loop ─────────────────────────────────────────────────────────── */
void ecore_main_loop_begin(void) {
    if (_init_count <= 0) ecore_init();
    _quit = 0;
    _loop_time = ecore_time_get();
    _last_anim = _loop_time;
    _iter = 0;
    _in_idle = 0;

    while (!_quit) {
        _iter++;
        double next = _next_timer_expiry();
        double now = ecore_time_get();
        if (next > now) {
            double d = next - now;
            if (d > 0.05) d = 0.05;          /* cap sleep for responsiveness */
            if (d > 0.0) eigen_sleep_ms((int)(d * 1000));
        }
        _loop_time = ecore_time_get();

        _run_timers();

        if (_loop_time - _last_anim >= _anim_frametime) {
            _last_anim = _loop_time;
            _run_animators();
        }

        _run_pollers();

        int had_idle = _run_idlers();
        if (had_idle && !_in_idle) { _in_idle = 1; _run_idle_enterers(); }
        else if (!had_idle && _in_idle) { _in_idle = 0; _run_idle_exiters(); }

        _run_jobs();
    }
}
