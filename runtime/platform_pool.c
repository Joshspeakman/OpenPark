/* A parallel loop for the compositor's row bands (runtime/platform.h tp_plat_parallel_for) on systems without Grand
 * Central Dispatch: a few workers kept for the process, the caller working too. pthreads only (POSIX, and MinGW-w64's
 * winpthreads), so platform_posix.c and platform/windows/platform_windows.c share it. From the 2026-10-19 linux and
 * windows branches (edc8b9c, runtime/platform.c). */
#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

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
void tp_pool_parallel_for(size_t n, void *ctx, void (*fn)(void *ctx, size_t i)) {
    pthread_mutex_lock(&call_lock);                     /* (one caller at a time: the job is shared) */
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
