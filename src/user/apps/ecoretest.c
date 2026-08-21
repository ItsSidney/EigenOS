/* ecoretest.c — verify the Ecore main-loop port:
 * timers, animators, idlers, pollers and jobs all drive a loop that
 * terminates via ecore_main_loop_quit(). */
#include <stdio.h>
#include "Ecore.h"

static int timer_ticks = 0;
static int anim_ticks  = 0;
static int poller_ticks = 0;
static int idle_runs   = 0;
static int job_runs    = 0;

static int timer_cb(void *data) {
    (void)data;
    timer_ticks++;
    printf("[ECORETEST] timer tick %d\n", timer_ticks);
    if (timer_ticks >= 5) {
        printf("[ECORETEST] 5 timer ticks -> quitting main loop\n");
        ecore_main_loop_quit();
        return ECORE_CALLBACK_CANCEL;
    }
    return ECORE_CALLBACK_RENEW;
}

static int anim_cb(void *data) {
    (void)data;
    anim_ticks++;
    if (anim_ticks % 20 == 0)
        printf("[ECORETEST] animator frame %d\n", anim_ticks);
    return ECORE_CALLBACK_RENEW;
}

static int poller_cb(void *data) {
    (void)data;
    poller_ticks++;
    return ECORE_CALLBACK_RENEW;
}

static int idler_cb(void *data) {
    (void)data;
    idle_runs++;
    /* run only a few times, then stop so the loop can go idle */
    if (idle_runs >= 3) return ECORE_CALLBACK_CANCEL;
    printf("[ECORETEST] idler run %d\n", idle_runs);
    return ECORE_CALLBACK_RENEW;
}

static void job_cb(void *data) {
    (void)data;
    job_runs++;
    printf("[ECORETEST] job ran (%d)\n", job_runs);
}

static int idle_enterer_cb(void *data) {
    (void)data;
    printf("[ECORETEST] entered idle\n");
    return ECORE_CALLBACK_CANCEL;
}

int main(void) {
    if (ecore_init() != 1) { printf("[ECORETEST] FAIL: init count\n"); return 1; }

    ecore_animator_frametime_set(1.0 / 60.0);
    ecore_timer_add(0.1, timer_cb, NULL);
    ecore_animator_add(anim_cb, NULL);
    ecore_poller_add(3, poller_cb, NULL);
    ecore_idler_add(idler_cb, NULL);
    ecore_idle_enterer_add(idle_enterer_cb, NULL);
    ecore_job_add(job_cb, NULL);
    ecore_job_add(job_cb, NULL);

    printf("[ECORETEST] starting main loop\n");
    ecore_main_loop_begin();

    int ok = (timer_ticks == 5) && (anim_ticks > 0) &&
             (poller_ticks > 0) && (idle_runs == 3) && (job_runs == 2);

    printf("[ECORETEST] timer=%d anim=%d poller=%d idle=%d job=%d\n",
           timer_ticks, anim_ticks, poller_ticks, idle_runs, job_runs);

    ecore_shutdown();

    if (ok) { printf("[ECORETEST] ALL PASS\n"); return 0; }
    printf("[ECORETEST] FAIL\n");
    return 1;
}
