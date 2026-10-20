/* runtime/platform.h on macOS: Mach timing and Grand Central Dispatch (moved here from view.c unchanged). */
#include <stdio.h>
#include <pthread.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/thread_policy.h>
#include <dispatch/dispatch.h>
#include "platform.h"

static mach_timebase_info_data_t tb;
static uint64_t ticks(double s) { if (!tb.denom) mach_timebase_info(&tb); return (uint64_t)(s * 1e9 * tb.denom / tb.numer); }

/* The game thread as a real-time thread: timers not coalesced, woken on time. */
int tp_plat_realtime_thread(double period) {
    thread_time_constraint_policy_data_t pol;
    pol.period = (uint32_t)ticks(period);
    pol.computation = (uint32_t)ticks(period * 0.45 < 0.004 ? period * 0.45 : 0.004);   /* (at most the constraint: past 37,500 cycles 4 ms was more, and the policy was refused) */
    pol.constraint = (uint32_t)ticks(period * 0.9);
    pol.preemptible = 1;
    kern_return_t kr = thread_policy_set(pthread_mach_thread_np(pthread_self()), THREAD_TIME_CONSTRAINT_POLICY, (thread_policy_t)&pol, THREAD_TIME_CONSTRAINT_POLICY_COUNT);
    return kr == KERN_SUCCESS;
}

void tp_plat_game_thread(void) { pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0); }

/* mach_wait_until: woken on time, unlike nanosleep under load. */
void tp_plat_sleep(double seconds) { if (seconds > 0) mach_wait_until(mach_absolute_time() + ticks(seconds)); }

void tp_plat_parallel_for(size_t n, void *ctx, void (*fn)(void *, size_t)) { dispatch_apply_f(n, DISPATCH_APPLY_AUTO, ctx, fn); }
