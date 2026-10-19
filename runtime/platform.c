/* Operating-system services for the runtime (platform.h): SHA-256, waiting on time, a soft real-time game thread,
 * a parallel loop for the park view's compositor, and the file calls Windows names differently. */
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#include <fcntl.h>
#include <io.h>
#else
#include <sched.h>
#include <unistd.h>
#endif
#include "platform.h"

/* ---- SHA-256 (FIPS 180-4) ---- */
static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
#define ROR(x, n) ((x) >> (n) | (x) << (32 - (n)))
static void sha_block(uint32_t h[8], const uint8_t *p) {
    uint32_t w[64], a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], k = h[7];
    for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = k + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
        uint32_t t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        k = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += k;
}
void tp_sha256_init(TpSha256 *s) {
    static const uint32_t h0[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    memcpy(s->h, h0, sizeof h0); s->bytes = 0; s->used = 0;
}
void tp_sha256_update(TpSha256 *s, const void *data, size_t n) {
    const uint8_t *p = data; s->bytes += n;
    if (s->used) {
        size_t k = 64 - s->used < n ? 64 - s->used : n;
        memcpy(s->buf + s->used, p, k); s->used += k; p += k; n -= k;
        if (s->used < 64) return;
        sha_block(s->h, s->buf); s->used = 0;
    }
    for (; n >= 64; p += 64, n -= 64) sha_block(s->h, p);
    memcpy(s->buf, p, n); s->used = n;
}
void tp_sha256_final(TpSha256 *s, uint8_t out[32]) {
    uint64_t bits = s->bytes * 8;
    uint8_t pad[72] = {0x80}; size_t k = (s->used < 56 ? 56 : 120) - s->used;
    for (int i = 0; i < 8; i++) pad[k + i] = (uint8_t)(bits >> (56 - 8 * i));
    tp_sha256_update(s, pad, k + 8);
    for (int i = 0; i < 8; i++) { out[4 * i] = (uint8_t)(s->h[i] >> 24); out[4 * i + 1] = (uint8_t)(s->h[i] >> 16); out[4 * i + 2] = (uint8_t)(s->h[i] >> 8); out[4 * i + 3] = (uint8_t)s->h[i]; }
}
void tp_sha256(const void *data, size_t n, uint8_t out[32]) { TpSha256 s; tp_sha256_init(&s); tp_sha256_update(&s, data, n); tp_sha256_final(&s, out); }

/* ---- time ---- */
double tp_platform_now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
#ifdef _WIN32
/* Windows wakes sleepers on its timer interrupt (15.6 ms unless asked): 1 ms for the process, and a
 * high-resolution waitable timer where there is one (Windows 10 1803 and later). Files open in binary mode by
 * default, as on POSIX (the DOS host reads and writes the game's files byte for byte). */
__attribute__((constructor)) static void windows_init(void) {
    timeBeginPeriod(1);
    _fmode = _O_BINARY;
}
void tp_platform_wait_until(double t) {
    static __thread HANDLE timer; static __thread int tried;
    double left = t - tp_platform_now();
    if (left <= 0) return;
    if (!tried) { tried = 1; timer = CreateWaitableTimerExW(NULL, NULL, 0x00000002 /* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION */, TIMER_ALL_ACCESS); }
    if (timer) {
        LARGE_INTEGER due; due.QuadPart = -(LONGLONG)(left * 1e7);   /* (100 ns units, relative) */
        if (SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE)) { WaitForSingleObject(timer, INFINITE); return; }
    }
    DWORD ms = (DWORD)(left * 1e3); Sleep(ms ? ms : 1);
}
int tp_platform_thread_realtime(double period, double computation) {
    (void)period; (void)computation;
    return SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST) != 0;
}
#elif defined(__APPLE__)
/* (macOS paces with Mach calls in view.c; these keep the interface whole) */
void tp_platform_wait_until(double t) {
    double left = t - tp_platform_now(); if (left <= 0) return;
    struct timespec ts; ts.tv_sec = (time_t)left; ts.tv_nsec = (long)((left - (double)ts.tv_sec) * 1e9);
    nanosleep(&ts, NULL);
}
int tp_platform_thread_realtime(double period, double computation) { (void)period; (void)computation; return 0; }
#else
void tp_platform_wait_until(double t) {
    struct timespec ts; ts.tv_sec = (time_t)t; ts.tv_nsec = (long)((t - (double)ts.tv_sec) * 1e9);
    if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL) == 4 /* EINTR */) ;
}
/* Linux: the game thread computes most of the time, and as a SCHED_RR thread it would starve the desktop's
 * threads on its core, so only when asked (TP_RT_SCHED=1; it also needs RLIMIT_RTPRIO or CAP_SYS_NICE). Without
 * it the pacer still works, woken a little later under load. */
int tp_platform_thread_realtime(double period, double computation) {
    (void)period; (void)computation;
    const char *e = getenv("TP_RT_SCHED"); if (!e || atoi(e) != 1) return 0;
    struct sched_param p = {0}; p.sched_priority = sched_get_priority_min(SCHED_RR);
    return pthread_setschedparam(pthread_self(), SCHED_RR, &p) == 0;
}
#endif

/* ---- a parallel loop: a few workers kept for the process, the caller working too ---- */
static pthread_mutex_t pool_lock = PTHREAD_MUTEX_INITIALIZER, call_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t pool_go = PTHREAD_COND_INITIALIZER, pool_done = PTHREAD_COND_INITIALIZER;
static int pool_n = -1, pool_busy; static unsigned pool_gen;
static struct { void *ctx; void (*fn)(void *, size_t); size_t n; atomic_size_t next; } job;
static void pool_work(void) { size_t i; while ((i = atomic_fetch_add(&job.next, 1)) < job.n) job.fn(job.ctx, i); }
static void *pool_main(void *arg) {
    (void)arg; unsigned seen = 0;
    pthread_mutex_lock(&pool_lock);
    for (;;) {
        while (pool_gen == seen) pthread_cond_wait(&pool_go, &pool_lock);
        seen = pool_gen;
        pthread_mutex_unlock(&pool_lock);
        pool_work();
        pthread_mutex_lock(&pool_lock);
        if (--pool_busy == 0) pthread_cond_signal(&pool_done);
    }
    return NULL;
}
static int cores(void) {
#ifdef _WIN32
    SYSTEM_INFO si; GetSystemInfo(&si); return (int)si.dwNumberOfProcessors;
#else
    long n = sysconf(_SC_NPROCESSORS_ONLN); return n > 0 ? (int)n : 1;
#endif
}
void tp_platform_parallel_for(size_t n, void *ctx, void (*fn)(void *ctx, size_t i)) {
    pthread_mutex_lock(&call_lock);
    if (pool_n < 0) {
        int want = cores() - 1; if (want > 15) want = 15;
        pool_n = 0;
        for (int i = 0; i < want; i++) { pthread_t t; if (pthread_create(&t, NULL, pool_main, NULL) == 0) { pthread_detach(t); pool_n++; } }
    }
    job.ctx = ctx; job.fn = fn; job.n = n; atomic_store(&job.next, 0);
    if (pool_n && n > 1) {
        pthread_mutex_lock(&pool_lock); pool_busy = pool_n; pool_gen++; pthread_cond_broadcast(&pool_go); pthread_mutex_unlock(&pool_lock);
        pool_work();
        pthread_mutex_lock(&pool_lock); while (pool_busy) pthread_cond_wait(&pool_done, &pool_lock); pthread_mutex_unlock(&pool_lock);
    } else pool_work();
    pthread_mutex_unlock(&call_lock);
}

/* ---- files ---- */
#ifdef _WIN32
int tp_mkdir(const char *path) { return _mkdir(path); }
/* (the file position stays where it was, as with pread) */
long long tp_pread(int fd, void *buf, size_t n, long long offset) {
    long long at = _lseeki64(fd, 0, SEEK_CUR);
    if (at < 0 || _lseeki64(fd, offset, SEEK_SET) < 0) return -1;
    int r = _read(fd, buf, (unsigned)n);
    _lseeki64(fd, at, SEEK_SET);
    return r;
}
#else
int tp_mkdir(const char *path) { return mkdir(path, 0755); }
#endif
