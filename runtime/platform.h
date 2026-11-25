/* What the portable runtime needs from the operating system (docs/PORTING.md). One implementation per platform:
 * platform/macos/platform_macos.c (Mach timing, Grand Central Dispatch), runtime/platform_posix.c (Linux and other
 * POSIX systems), platform/windows/platform_windows.c (MinGW-w64: winpthreads, waitable timers, a thread pool).
 * Everything else in runtime/ is plain C11 with pthreads; the window, sound output and input belong to each platform's
 * app (platform/macos/ for Cocoa, platform/sdl/ for Linux and Windows). */
#ifndef TP_PLATFORM_H
#define TP_PLATFORM_H
#include <stddef.h>

/* Ask for the calling thread to be woken on time (a frame every `period` seconds, a few ms of work each);
 * best effort, quietly ignored where the system offers nothing. Returns 1 if granted. */
int tp_plat_realtime_thread(double period);

/* Mark the calling thread as the game thread (interactive priority where the system has classes). */
void tp_plat_game_thread(void);

/* Sleep for about `seconds` (sub-millisecond precision where the system has it; never much longer). */
void tp_plat_sleep(double seconds);

/* fn(ctx, i) for i in [0, n), spread over the cores where the platform can (in any order; returns when all are
 * done). */
void tp_plat_parallel_for(size_t n, void *ctx, void (*fn)(void *ctx, size_t i));

/* Files: the POSIX calls whose names or arguments differ on Windows (MinGW-w64 has the rest: clock_gettime,
 * nanosleep, usleep, dirent, pthreads). Files open in binary mode there (platform_windows.c sets _fmode). */
#ifdef _WIN32
#include <stdlib.h>
#include <io.h>
#include <direct.h>
static inline int tp_mkdir(const char *path) { return _mkdir(path); }
long long tp_pread(int fd, void *buf, size_t n, long long offset);   /* (platform_windows.c) */
static inline int setenv(const char *name, const char *value, int overwrite) {
    return !overwrite && getenv(name) ? 0 : _putenv_s(name, value);
}
static inline int unsetenv(const char *name) { return _putenv_s(name, ""); }
#include <time.h>
#ifndef _POSIX_THREAD_SAFE_FUNCTIONS
static inline struct tm *localtime_r(const time_t *t, struct tm *out) { return localtime_s(out, t) ? NULL : out; }
#endif
#else
#include <sys/stat.h>
#include <unistd.h>
static inline int tp_mkdir(const char *path) { return mkdir(path, 0755); }
#define tp_pread pread
#endif

#endif
