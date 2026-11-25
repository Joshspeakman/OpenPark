/* runtime/platform.h on Windows (MinGW-w64 with winpthreads; linked with runtime/platform_pool.c). Windows wakes
 * sleepers on its timer interrupt (15.6 ms unless asked): 1 ms for the process, and a high-resolution waitable timer
 * where there is one (Windows 10 1803 and later). Files open in binary mode by default, as on POSIX (the DOS host
 * reads and writes the game's files byte for byte). From the 2026-10-19 windows branch (edc8b9c, runtime/platform.c),
 * moved to the platform layer nightly has had since. Not yet run on Windows itself (docs/PC-HANDOFF.md). */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include "platform.h"

__attribute__((constructor)) static void windows_init(void) {
    timeBeginPeriod(1);
    _fmode = _O_BINARY;
}

static double now_s(void) {
    static LARGE_INTEGER f; LARGE_INTEGER c;
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c); return (double)c.QuadPart / (double)f.QuadPart;
}

/* THREAD_PRIORITY_TIME_CRITICAL would starve the desktop the way SCHED_FIFO does on Linux (the game thread computes
 * most of the time): HIGHEST, plus MMCSS's "Games" class where avrt is present (loaded at run time). */
int tp_plat_realtime_thread(double period) {
    (void)period;
    int ok = SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST) != 0;
    HMODULE avrt = LoadLibraryA("avrt.dll");
    if (avrt) {
        typedef HANDLE (WINAPI *Set)(LPCSTR, LPDWORD);
        Set set = (Set)(void *)GetProcAddress(avrt, "AvSetMmThreadCharacteristicsA");
        DWORD idx = 0; if (set && set("Games", &idx)) ok = 1;
    }
    return ok;
}

void tp_plat_game_thread(void) { SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL); }

void tp_plat_sleep(double seconds) {
    static __thread HANDLE timer; static __thread int tried;
    if (seconds <= 0) return;
    double until = now_s() + seconds;
    if (!tried) { tried = 1; timer = CreateWaitableTimerExW(NULL, NULL, 0x00000002 /* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION */, TIMER_ALL_ACCESS); }
    if (timer) {
        LARGE_INTEGER due; due.QuadPart = -(LONGLONG)(seconds * 1e7);   /* (100 ns units, relative) */
        if (SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE)) { WaitForSingleObject(timer, INFINITE); return; }
    }
    DWORD ms = (DWORD)(seconds * 1e3); Sleep(ms ? ms : (seconds > 0.0002 ? 1 : 0));
    while (now_s() < until - 0.002) Sleep(1);           /* (never much longer: a coarse Sleep can come back early) */
}

void tp_pool_parallel_for(size_t n, void *ctx, void (*fn)(void *, size_t));
void tp_plat_parallel_for(size_t n, void *ctx, void (*fn)(void *, size_t)) { tp_pool_parallel_for(n, ctx, fn); }

/* pread: the file position stays where it was */
long long tp_pread(int fd, void *buf, size_t n, long long offset) {
    long long at = _lseeki64(fd, 0, SEEK_CUR);
    if (at < 0 || _lseeki64(fd, offset, SEEK_SET) < 0) return -1;
    int r = _read(fd, buf, (unsigned)n);
    _lseeki64(fd, at, SEEK_SET);
    return r;
}
