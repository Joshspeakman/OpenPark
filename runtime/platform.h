/* What the runtime needs from the operating system beyond standard C and POSIX threads, for macOS, Linux and
 * Windows (MinGW-w64). macOS keeps its own calls (Mach timing and thread policy, libdispatch) in view.c; the
 * others use these. Included after the system headers. */
#ifndef TP_PLATFORM_H
#define TP_PLATFORM_H
#include <stddef.h>
#include <stdint.h>

/* SHA-256 (frame and save hashes; the same digests as hashlib). */
typedef struct { uint32_t h[8]; uint64_t bytes; uint8_t buf[64]; size_t used; } TpSha256;
void tp_sha256_init(TpSha256 *s);
void tp_sha256_update(TpSha256 *s, const void *data, size_t n);
void tp_sha256_final(TpSha256 *s, uint8_t out[32]);
void tp_sha256(const void *data, size_t n, uint8_t out[32]);

/* Waiting on time for the frame pacer (view.c): a wait until a CLOCK_MONOTONIC time, woken close to it. */
double tp_platform_now(void);
void tp_platform_wait_until(double t);
/* The calling thread as a soft real-time one (pacing); 1 when the system agreed. */
int tp_platform_thread_realtime(double period, double computation);
/* fn(ctx, 0..n-1) across the cores; returns when all are done. One caller at a time. */
void tp_platform_parallel_for(size_t n, void *ctx, void (*fn)(void *ctx, size_t i));

/* Files: POSIX names that differ on Windows. */
int tp_mkdir(const char *path);
#ifdef _WIN32
#include <stdlib.h>
#include <sys/types.h>
long long tp_pread(int fd, void *buf, size_t n, long long offset);
static inline int setenv(const char *name, const char *value, int overwrite) {
    return !overwrite && getenv(name) ? 0 : _putenv_s(name, value);
}
#else
#include <unistd.h>
#define tp_pread pread
#endif
#endif
