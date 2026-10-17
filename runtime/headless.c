/* Headless native runner: the deterministic scripted scenario through the C
 * host, for comparison with config/milestones.json. Bounded and muted. */
#include "host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static int parse(const char *s, uint32_t *out, int width, int max) {
    int n = 0;
    while (s && *s && n < max) {
        for (int k = 0; k < width; k++) {
            char *end;
            out[n * width + k] = (uint32_t)strtoul(s, &end, k == 1 && width == 2 ? 16 : 10);
            s = end;
            if (*s == ':' || *s == ',') s++;
        }
        n++;
    }
    return n;
}
static void saved(const char *rel, const char *hex, void *ctx) {
    int *first = ctx;
    printf("%s\n    \"%s\": \"%s\"", *first ? "" : ",", rel, hex);
    *first = 0;
}

#include <signal.h>
#include <unistd.h>
#include <stdint.h>
extern uint8_t *G_MEM;
uint32_t tp_get(unsigned i);
static void on_fault(int sig, siginfo_t *si, void *uc) {
    (void)uc;
    fprintf(stderr, "signal %d at host %p (guest offset %#llx), guest block %08x esp %08x\n", sig, si->si_addr,
            (unsigned long long)((uint8_t *)si->si_addr - G_MEM), tp_get(9), tp_get(4));
    _exit(3);
}

#ifdef WITH_INTRO
struct Ctx; int intro_dispatch(struct Ctx *); void intro_dispatch_init(void);
#else
#define intro_dispatch 0
#define intro_dispatch_init 0
#endif
void tp_select_code(int (*dispatch)(struct Ctx *), void (*init)(void));
int main(int argc, char **argv) {
    struct sigaction sa = {0}; sa.sa_sigaction = on_fault; sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, 0); sigaction(SIGBUS, &sa, 0);
    const char *game = 0, *overlay = 0, *out = "work/headless", *keys = "", *clicks = "", *block_clicks = "";
    uint32_t quantum = 1000, limit = 8000000; int realtime = 0; double seconds = 0, shots = 0;
    double fuzz = 0; int intro = 0;            /* after the deterministic part: seconds of random clicks in real time */
    const char *rt_keys = "";   /* real time: seconds:key(hex) or seconds:m:x:y:buttons */
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : "";
        if (!strcmp(a, "--game")) game = v, i++;
        else if (!strcmp(a, "--overlay")) overlay = v, i++;
        else if (!strcmp(a, "--image")) setenv("TP_IMAGE", v, 1), i++;
        else if (!strcmp(a, "--out")) out = v, i++;
        else if (!strcmp(a, "--keys")) keys = v, i++;
        else if (!strcmp(a, "--clicks")) clicks = v, i++;
        else if (!strcmp(a, "--block-clicks")) block_clicks = v, i++;
        else if (!strcmp(a, "--quantum")) quantum = (uint32_t)atoi(v), i++;
        else if (!strcmp(a, "--blocks")) limit = (uint32_t)atoi(v), i++;
        else if (!strcmp(a, "--realtime")) realtime = 1, seconds = atof(v), i++;
        else if (!strcmp(a, "--rt-keys")) rt_keys = v, i++;
        else if (!strcmp(a, "--shots")) shots = atof(v), i++;
        else if (!strcmp(a, "--fuzz")) fuzz = atof(v), i++;
        else if (!strcmp(a, "--intro")) intro = 1;
        else { fprintf(stderr, "unknown argument %s\n", a); return 2; }
    }
    if (overlay && (!*overlay || overlay[strlen(overlay) - 1] == '/')) { fprintf(stderr, "overlay must name a directory\n"); return 2; }
    if (!game || !overlay) { fprintf(stderr, "usage: headless --game GAME --overlay DIR [--image MAIN.TPIMG] ...\n"); return 2; }
    static TpScript s;
    static uint32_t tmp[4096 * 3];
    s.nkeys = parse(keys, tmp, 2, 64);
    for (int i = 0; i < s.nkeys; i++) s.keys[i].at = tmp[i * 2], s.keys[i].key = tmp[i * 2 + 1];
    s.nclicks = parse(clicks, tmp, 3, 64);
    for (int i = 0; i < s.nclicks; i++) s.clicks[i].at = tmp[i * 3], s.clicks[i].x = tmp[i * 3 + 1], s.clicks[i].y = tmp[i * 3 + 2];
    /* block:x:y[:buttons] */
    for (const char *q = block_clicks; q && *q && s.nblock_clicks < 4096; ) {
        char *e; int i = s.nblock_clicks++;
        s.block_clicks[i].at = (uint32_t)strtoul(q, &e, 10); s.block_clicks[i].x = (uint32_t)strtoul(e + 1, &e, 10);
        s.block_clicks[i].y = (uint32_t)strtoul(e + 1, &e, 10);
        if (*e == ':') s.block_clicks[i].buttons = (uint32_t)strtoul(e + 1, &e, 10);
        q = *e ? e + 1 : e;
    }
    mkdir(out, 0755);
    struct timespec t0, t1; clock_gettime(CLOCK_MONOTONIC, &t0);
    if (intro) { tp_select_code(intro_dispatch, intro_dispatch_init); }
    if (tp_host_init(game, overlay, !realtime, &s)) { fprintf(stderr, "init: %s\n", tp_host_status()); return 1; }
    int r;
    if (realtime) {
        double next_shot = shots; int shot_n = 0; const char *k = rt_keys;
        for (;;) {
            r = tp_host_run(2000, 0, 20);
            clock_gettime(CLOCK_MONOTONIC, &t1);
            double el = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) * 1e-9;
            if (r || el >= seconds) break;
            while (*k && strtod(k, NULL) <= el) {
                char *e; strtod(k, &e); e++;
                if (*e == 'm') {
                    double x = strtod(e + 2, &e); double y = strtod(e + 1, &e); unsigned b = (unsigned)strtoul(e + 1, &e, 10);
                    tp_host_mouse(x / 640, y / 400, b);
                } else { uint32_t key = (uint32_t)strtoul(e, &e, 16); tp_host_key(key); tp_host_key_raw(key >> 8, 1); tp_host_key_raw(key >> 8, 0); }
                k = *e ? e + 1 : e;
            }
            static double next_fz; static int held;
            if (!*k && getenv("TP_RT_FUZZ") && el >= next_fz) {     /* random clicks after the script */
                static double hx, hy;
                if (held) { tp_host_mouse(hx, hy, 0); held = 0; }
                else { hx = rand() / (double)RAND_MAX * 0.999; hy = rand() / (double)RAND_MAX * 0.999;
                       tp_host_mouse(hx, hy, 0); tp_host_mouse(hx, hy, rand() % 5 ? 1 : 2); held = 1; }
                next_fz = el + 0.15;
            }
            if (shots && el >= next_shot) {
                char p[1024], h[65]; snprintf(p, sizeof p, "%s/rt-%03d.png", out, shot_n++);
                tp_host_png(p, h); fprintf(stderr, "shot %s t=%.2f\n", p, el); next_shot += shots;
                if (getenv("TP_SHOT_CKPT")) { snprintf(p, sizeof p, "%s/rt-%03d.ckpt", out, shot_n - 1); tp_host_checkpoint(p); }   /* survey: memory at each shot */
            }
        }
    } else {
        if (shots) {                 /* deterministic: a frame every `shots` million blocks */
            uint32_t step = (uint32_t)(shots * 1e6), at = step; int n = 0;
            do {
                r = tp_host_run(quantum, at < limit ? at : limit, 0);
                char p[1024], h[65]; snprintf(p, sizeof p, "%s/d-%03d.png", out, n++);
                tp_host_png(p, h); fprintf(stderr, "shot %s blocks=%u %s\n", p, at, h);
                at += step;
            } while (!r && at <= limit);
        } else r = tp_host_run(quantum, limit, 0);
        if (!r && fuzz > 0) {
            tp_host_set_realtime();
            srand(getenv("TP_SEED") ? atoi(getenv("TP_SEED")) : 1234);
            struct timespec f0; clock_gettime(CLOCK_MONOTONIC, &f0);
            double next_click = 0, next_shot = 0; int n = 0, held = 0; double hx = 0, hy = 0;
            for (;;) {
                r = tp_host_run(2000, 0, 10);
                clock_gettime(CLOCK_MONOTONIC, &t1);
                double el = (t1.tv_sec - f0.tv_sec) + (t1.tv_nsec - f0.tv_nsec) * 1e-9;
                if (r || el >= fuzz) break;
                if (el >= next_click) {
                    if (held) { tp_host_mouse(hx, hy, 0); held = 0; }
                    else {
                        hx = rand() / (double)RAND_MAX * 0.999; hy = rand() / (double)RAND_MAX * 0.999;
                        tp_host_mouse(hx, hy, 0);
                        tp_host_mouse(hx, hy, rand() % 5 ? 1 : 2); held = 1;
                    }
                    next_click = el + 0.05;
                }
                if (shots && el >= next_shot) {
                    char p[1024], h[65]; snprintf(p, sizeof p, "%s/fz-%03d.png", out, n);
                    tp_host_png(p, h); next_shot += shots;
                    if (getenv("TP_CHECKPOINTS")) { snprintf(p, sizeof p, "%s/fz-%03d.ckpt", out, n); tp_host_checkpoint(p); }
                    n++;
                }
            }
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    if (r < 0 && getenv("TP_DUMP_ON_STOP")) tp_host_checkpoint(getenv("TP_DUMP_ON_STOP"));
    char png[1024], hex[65] = "";
    snprintf(png, sizeof png, "%s/frame.png", out);
    int have = tp_host_png(png, hex);
    printf("{\n  \"status\": \"%s\",\n  \"result\": %d,\n  \"executed_blocks\": %llu,\n  \"elapsed_seconds\": %.3f,\n"
           "  \"frame_sha256\": \"%s\",\n  \"saved_files\": {", r == 0 && !realtime ? "block bound" : tp_host_status(), r,
           (unsigned long long)tp_host_blocks(), (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) * 1e-9, have ? hex : "");
    int first = 1; tp_host_list_saved(saved, &first);
    printf("\n  }\n}\n");
    return r < 0 ? 1 : 0;
}
