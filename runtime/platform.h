/* What the portable runtime needs from the operating system (docs/PORTING.md). One implementation per platform:
 * platform/macos/platform_macos.c (Mach timing, Grand Central Dispatch), runtime/platform_posix.c (Linux and other
 * POSIX systems). Windows: a platform/windows/platform_windows.c to come. Everything else in runtime/ is plain C11
 * with pthreads; the window, sound output and input belong to each platform's app (platform/<os>/). */
#ifndef TP_PLATFORM_H
#define TP_PLATFORM_H
#include <stddef.h>

/* Ask for the calling thread to be woken on time (a frame every `period` seconds, a few ms of work each);
 * best effort, quietly ignored where the system offers nothing. Returns 1 if granted. */
int tp_plat_realtime_thread(double period);

/* Sleep for about `seconds` (sub-millisecond precision where the system has it; never much longer). */
void tp_plat_sleep(double seconds);

/* fn(ctx, i) for i in [0, n), spread over the cores where the platform can (in any order; returns when all are
 * done). */
void tp_plat_parallel_for(size_t n, void *ctx, void (*fn)(void *ctx, size_t i));

#endif
