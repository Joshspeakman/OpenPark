/* runtime/platform.h on Linux and other POSIX systems (C11 + pthreads), linked with runtime/platform_pool.c. Built by
 * the SDL app (tools/build_linux.sh) and the CI; the macOS build uses platform/macos/platform_macos.c instead. */
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdlib.h>
#include <time.h>
#include "platform.h"

/* Opt-in (TP_RT_SCHED=1): the game thread computes most of the time (and spins the last half millisecond of a frame),
 * and as a real-time thread it would starve the desktop's threads on its core; it also needs RLIMIT_RTPRIO or
 * CAP_SYS_NICE. Without it the pacer still works, woken a little later under load. */
int tp_plat_realtime_thread(double period) {
    (void)period;
    const char *e = getenv("TP_RT_SCHED"); if (!e || atoi(e) != 1) return 0;
    struct sched_param sp = {.sched_priority = sched_get_priority_min(SCHED_RR)};
    return pthread_setschedparam(pthread_self(), SCHED_RR, &sp) == 0;
}

void tp_plat_game_thread(void) {}

void tp_plat_sleep(double seconds) {
    if (seconds <= 0) return;
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    long long ns = t.tv_nsec + (long long)(seconds * 1e9);
    t.tv_sec += ns / 1000000000; t.tv_nsec = ns % 1000000000;
#if defined(__linux__)
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &t, NULL) == EINTR) ;
#else
    struct timespec d = {(time_t)seconds, (long)((seconds - (time_t)seconds) * 1e9)};
    nanosleep(&d, NULL);
#endif
}

void tp_pool_parallel_for(size_t n, void *ctx, void (*fn)(void *, size_t));   /* (platform_pool.c) */
void tp_plat_parallel_for(size_t n, void *ctx, void (*fn)(void *, size_t)) { tp_pool_parallel_for(n, ctx, fn); }
