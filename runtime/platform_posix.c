/* runtime/platform.h on Linux and other POSIX systems (C11 + pthreads). Untested on Linux so far beyond compiling
 * (tests/check_portable.sh, the CI); the macOS build uses platform/macos/platform_macos.c instead. */
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <time.h>
#include "platform.h"

/* SCHED_FIFO needs privileges most desktops do not grant: then the default scheduler, which is fine at 60 fps. */
int tp_plat_realtime_thread(double period) {
    (void)period;
    struct sched_param sp = {.sched_priority = 10};
    return pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) == 0;
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

/* Serial for now: the compositor's bands are independent, so a thread pool can come in later. */
void tp_plat_parallel_for(size_t n, void *ctx, void (*fn)(void *, size_t)) { for (size_t i = 0; i < n; i++) fn(ctx, i); }
