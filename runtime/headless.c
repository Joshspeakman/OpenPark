#include <pthread.h>
/* Headless native runner: the deterministic scripted scenario through the C
 * host, for comparison with config/milestones.json. Bounded and muted. */
#include "host.h"
#include "platform.h"
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
#include <math.h>
extern uint8_t *G_MEM;
uint32_t tp_get(unsigned i);
#ifndef _WIN32
static void on_fault(int sig, siginfo_t *si, void *uc) {
    (void)uc;
    fprintf(stderr, "signal %d at host %p (guest offset %#llx), guest block %08x esp %08x\n", sig, si->si_addr,
            (unsigned long long)((uint8_t *)si->si_addr - G_MEM), tp_get(9), tp_get(4));
    _exit(3);
}
#endif

#ifdef WITH_INTRO
struct Ctx; int intro_dispatch(struct Ctx *); void intro_dispatch_init(void);
#else
#define intro_dispatch 0
#define intro_dispatch_init 0
#endif
void tp_select_code(int (*dispatch)(struct Ctx *), void (*init)(void));
static double hl_wide = 1;   /* the z step's widescreen factor (s steps compose at it) */
int main(int argc, char **argv) {
#ifndef _WIN32
    struct sigaction sa = {0}; sa.sa_sigaction = on_fault; sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, 0); sigaction(SIGBUS, &sa, 0);
#endif
    const char *game = 0, *overlay = 0, *out = "work/headless", *keys = "", *clicks = "", *block_clicks = "";
    uint32_t quantum = 1000, limit = 8000000; int realtime = 0; double seconds = 0, shots = 0;
    double fuzz = 0; int intro = 0;            /* after the deterministic part: seconds of random clicks in real time */
    const char *rt_keys = "";   /* real time: seconds:key(hex) or seconds:m:x:y:buttons (and the letters below) */
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
    tp_mkdir(out);
    struct timespec t0, t1; clock_gettime(CLOCK_MONOTONIC, &t0);
    if (intro) { tp_select_code(intro_dispatch, intro_dispatch_init); }
    if (getenv("TP_ARGS")) tp_host_set_args(getenv("TP_ARGS"));
    if (tp_host_init(game, overlay, !realtime || getenv("TP_PREFIX_BLOCKS"), &s)) { fprintf(stderr, "init: %s\n", tp_host_status()); return 1; }
#ifdef __APPLE__
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);    /* (as the app's game thread) */
#endif
    if (!intro) { void tp_qol_init(void); tp_qol_init(); }            /* quality-of-life hooks (qol.c) */
    if (!intro && getenv("TP_COUNT_FRAMES")) { void tp_view_count_game_frames(int); tp_view_count_game_frames(1); }   /* (development: frames counted with the view off) */
    if (getenv("TP_START_HIRES")) tp_view_request_hires(1);          /* as the app's Start In 640x480 */
    int r;
    if (realtime) {
        if (getenv("TP_PREFIX_BLOCKS")) {   /* development: the scripted part (--keys, --clicks, --block-clicks) first, e.g. a new park */
            r = tp_host_run(quantum, (uint32_t)atoi(getenv("TP_PREFIX_BLOCKS")), 0);
            tp_host_set_realtime(); clock_gettime(CLOCK_MONOTONIC, &t0);
        }
        double next_shot = shots; int shot_n = 0; const char *k = rt_keys;
        double walk_in[3] = {0}, walk_until = 0, walk_last = 0;   /* Visitor View input held (i:...) */
        for (;;) {
            r = tp_host_run(2000, 0, 20);
            clock_gettime(CLOCK_MONOTONIC, &t1);
            double el = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) * 1e-9;
            if (r || el >= seconds) break;
            if (tp_walk_active()) {                     /* the walker moves on the app's side: here, every step */
                int held = el < walk_until;
                tp_walk_input(held ? walk_in[0] : 0, held ? walk_in[1] : 0, held ? walk_in[2] : 0, 0, 0, 0, walk_last ? el - walk_last : 0.02);
                walk_last = el;
            }
            while (*k && strtod(k, NULL) <= el) {
                char *e; strtod(k, &e); e++;
                if (*e == 'm' || *e == 'v') {           /* m: the game's mouse; v: through the zoomed view (view.c) */
                    int via = *e == 'v';
                    double x = strtod(e + 2, &e); double y = strtod(e + 1, &e); unsigned b = (unsigned)strtoul(e + 1, &e, 10);
                    if (via) tp_view_mouse(x / 640, y / 400, b); else tp_host_mouse(x / 640, y / 400, b);
                } else if (*e == 'z') {                  /* z:zoom:cx:cy[:wide] (canvas; cx < 0: the game's view centre;
                                                            wide: layout width over screen width, e.g. 1.33 for 16:9) */
                    double z = strtod(e + 2, &e), cx = strtod(e + 1, &e), cy = strtod(e + 1, &e), wide = 1;
                    if (*e == ':' && e[1] != ',' && e[1]) wide = strtod(e + 1, &e);
                    int sw, sh, wh; tp_view_geometry(&sw, &sh, &wh);
                    if (cx < 0) tp_view_camera_center(&cx, &cy);
                    tp_view_enable(1); tp_view_set(z != 1 || wide > 1, cx, cy, z, sw * wide); hl_wide = wide;
                } else if (*e == 's') {                  /* s:name: the composited zoomed picture, 1280x800 */
                    char name[256]; int n = 0; e += 2; while (*e && *e != ',' && n < 255) name[n++] = *e++; name[n] = 0;
                    static uint32_t frame[640 * 480]; int fw = 320, fh = 200;
                    tp_host_frame_size(&fw, &fh); tp_host_frame_rgba_any(frame, 640 * 480);
                    int oh = fw == 640 ? 960 : 800, ow = (int)lround(oh * 4.0 / 3.0 * hl_wide * (fw == 640 ? 1 : 1.0)) ;
                    if (fw == 320) ow = (int)lround(1280 * hl_wide);
                    uint32_t *big = malloc(4 * ow * oh); tp_view_compose(big, ow, oh, frame, fw, fh, 0);
                    char p[1024]; snprintf(p, sizeof p, "%s/%s.png", out, name);
                    int tp_host_png_rgba(const char *, const uint32_t *, int, int); tp_host_png_rgba(p, big, ow, oh);
                    if (getenv("TP_SHOT_RAW")) { snprintf(p, sizeof p, "%s/%s.rgba", out, name); FILE *f = fopen(p, "wb"); if (f) { fwrite(big, 4, (size_t)ow * oh, f); fclose(f); } }
                    free(big);
                    fprintf(stderr, "view shot %s live=%d\n", p, tp_view_live());
                    if (getenv("TP_VIEW_BENCH")) {        /* development: the compositor's cost at full-screen sizes */
                        struct timespec a0, a1; uint32_t *o2 = malloc(4 * 2048 * 1536);
                        for (int sm = 0; sm < 2; sm++) {
                            clock_gettime(CLOCK_MONOTONIC, &a0);
                            for (int r = 0; r < 10; r++) tp_view_compose(o2, fw * (fw == 320 ? 6 : 3), fh * (fw == 320 ? 6 : 3), frame, fw, fh, sm);
                            clock_gettime(CLOCK_MONOTONIC, &a1);
                            fprintf(stderr, "compose %dx%d smooth=%d: %.2f ms\n", fw * (fw == 320 ? 6 : 3), fh * (fw == 320 ? 6 : 3), sm,
                                    ((a1.tv_sec - a0.tv_sec) * 1e3 + (a1.tv_nsec - a0.tv_nsec) / 1e6) / 10);
                        }
                        free(o2);
                    }
                } else if (*e == 'w') {                  /* w:X:Y: the Visitor View at map point X,Y (X < 0: the middle of the game's view) */
                    double x = strtod(e + 2, &e), y = strtod(e + 1, &e);
                    if (x < 0 && !tp_view_point_to_world(0.5, 0.45, &x, &y)) x = y = 32;
                    fprintf(stderr, "walk: start at %.2f,%.2f: %d\n", x, y, tp_walk_start(x, y));
                } else if (*e == 'p' || *e == 'r' || *e == 'o' || *e == 'n') {   /* p follow (possess), r ride along, o view, n noclip */
                    char c = *e++;
                    tp_walk_command(c == 'p' ? TP_WALK_FOLLOW : c == 'r' ? TP_WALK_RIDE : c == 'o' ? TP_WALK_VIEW : TP_WALK_NOCLIP);
                    char st[256]; tp_walk_status(st, sizeof st); fprintf(stderr, "walk: %s\n", st);
                } else if (*e == 't') {                  /* t:X:Y:YAW: put the walker there, looking YAW degrees (0 east, -90 north) */
                    double x = strtod(e + 2, &e), y = strtod(e + 1, &e), yaw = strtod(e + 1, &e);
                    tp_walk_teleport(x, y, yaw * M_PI / 180);
                } else if (*e == 'i') {                  /* i:FWD:STRAFE:TURN:SECONDS: hold walking input */
                    walk_in[0] = strtod(e + 2, &e); walk_in[1] = strtod(e + 1, &e); walk_in[2] = strtod(e + 1, &e);
                    walk_until = el + strtod(e + 1, &e);
                } else if (*e == 'g') {                  /* g:name: the Visitor View, 960x600, to out/name.png */
                    char name[256]; int n = 0; e += 2; while (*e && *e != ',' && n < 255) name[n++] = *e++; name[n] = 0;
                    static uint32_t img[960 * 600];
                    tp_walk_input(0, 0, 0, 0, 0, 0, 0);
                    int ok = tp_walk_render(img, 960, 600);
                    char p[1024], st[256]; snprintf(p, sizeof p, "%s/%s.png", out, name);
                    int tp_host_png_rgba(const char *, const uint32_t *, int, int); if (ok) tp_host_png_rgba(p, img, 960, 600);
                    tp_walk_status(st, sizeof st); double wx, wy; tp_walk_position(&wx, &wy);
                    int type = -1; char ride[64] = ""; int on = tp_walk_ride(&type, ride, sizeof ride);
                    fprintf(stderr, "walk shot %s drawn=%d at %.2f,%.2f riding=%d type %d '%s' | %s\n", p, ok, wx, wy, on, type, ride, st);
                } else { uint32_t key = (uint32_t)strtoul(e, &e, 16); tp_host_key(key); tp_host_key_raw(key >> 8, 1); tp_host_key_raw(key >> 8, 0); }
                k = *e ? e + 1 : e;
            }
            {   /* development: TP_DUMP="t:path:addr:len" writes guest memory once at t seconds */
                static int dumped; static const char *d; if (!d) d = getenv("TP_DUMP");   /* (several: "t:path:addr:len;t:...") */
                if (d && *d && !dumped && strtod(d, NULL) <= el) {
                    char path[512]; const char *a = strchr(d, ':') + 1, *b = strchr(a, ':'); snprintf(path, sizeof path, "%.*s", (int)(b - a), a);
                    char *e; unsigned at = strtoul(b + 1, &e, 16); unsigned len = strtoul(e + 1, NULL, 16);
                    uint8_t *tp_host_memory(void); FILE *f = fopen(path, "wb"); if (f) { fwrite(tp_host_memory() + at, 1, len, f); fclose(f); }
                    fprintf(stderr, "dump %s %#x+%#x\n", path, at, len);
                    const char *nx = strchr(d, ';'); if (nx) d = nx + 1; else dumped = 1;
                }
            }
            static double next_fz; static int held;
            if (!*k && getenv("TP_RT_FUZZ") && el >= next_fz) {     /* random clicks after the script */
                static double hx, hy;
                if (held) { tp_host_mouse(hx, hy, 0); held = 0; }
                else { hx = rand() / (double)RAND_MAX * 0.999; hy = rand() / (double)RAND_MAX * 0.999;
                       tp_host_mouse(hx, hy, 0); tp_host_mouse(hx, hy, rand() % 5 ? 1 : 2); held = 1; }
                next_fz = el + 0.15;
            }
            {   /* survey: TP_VIEW_TEST=<second> runs the native view's pass test at the next park frame (view.c) */
                static int vt; const char *ve = getenv("TP_VIEW_TEST");
                if (ve && !vt && el >= atof(ve)) { void tp_view_test(const char *); tp_view_test(out); vt = 1; }
                if (vt == 1) { int tp_view_test_step(void); if (tp_view_test_step()) vt = 2; }
            }
            {   /* survey: TP_PROFILE=<second> records the next 20 million dispatched blocks, then writes counts per block */
                extern uint32_t *tp_trace_buf; extern uint32_t tp_trace_cap, tp_trace_n;
                static int prof; const char *pe = getenv("TP_PROFILE");
                if (pe && !prof && el >= atof(pe)) { tp_trace_cap = 20000000; tp_trace_buf = malloc(4ul * tp_trace_cap); tp_trace_n = 0; prof = 1; }
                if (prof == 1 && tp_trace_n >= tp_trace_cap) {
                    char p[1024]; snprintf(p, sizeof p, "%s/profile.bin", out);
                    FILE *f = fopen(p, "wb"); fwrite(tp_trace_buf, 4, tp_trace_n, f); fclose(f);
                    tp_trace_buf = NULL; prof = 2; fprintf(stderr, "profile %s\n", p);
                }
            }
            if (shots && el >= next_shot) {
                char p[1024], h[65]; snprintf(p, sizeof p, "%s/rt-%03d.png", out, shot_n++);
                if (!tp_host_png(p, h)) {             /* 640x480 (VESA): no hash, the plain picture */
                    int fw, fh; static uint32_t big[640 * 480];
                    if (tp_host_frame_size(&fw, &fh) && tp_host_frame_rgba_any(big, 640 * 480)) {
                        int tp_host_png_rgba(const char *, const uint32_t *, int, int); tp_host_png_rgba(p, big, fw, fh);
                    }
                }
                fprintf(stderr, "shot %s t=%.2f\n", p, el); next_shot += shots;
                if (getenv("TP_SHOT_CKPT")) { snprintf(p, sizeof p, "%s/rt-%03d.ckpt", out, shot_n - 1); tp_host_checkpoint(p); }   /* survey: memory at each shot */
            }
        }
    } else {
        if (getenv("TP_WATCH")) {    /* development: TP_WATCH=addr:step:limit, the first block at which a byte changes */
            uint8_t *tp_host_memory(void); uint32_t wa = 0, ws = 1000, wl = limit; sscanf(getenv("TP_WATCH"), "%x:%u:%u", &wa, &ws, &wl);
            uint8_t last = tp_host_memory()[wa]; r = 0;
            for (uint32_t at = ws; at <= wl && !r; at += ws) {
                r = tp_host_run(quantum, at, 0);
                uint8_t v = tp_host_memory()[wa];
                if (v != last) { fprintf(stderr, "watch %x: %02x -> %02x by block %u\n", wa, last, v, at); last = v; }
            }
            fprintf(stderr, "watch %x: %02x at the end; map at %x; r=%d blocks=%llu\n", wa, tp_host_memory()[wa], *(uint32_t *)(tp_host_memory() + 0x1d3b08), r, (unsigned long long)tp_host_blocks());
        } else
        if (shots) {                 /* deterministic: a frame every `shots` million blocks */
            uint32_t step = (uint32_t)(shots * 1e6), at = step; int n = 0;
            extern uint32_t *tp_trace_buf; extern uint32_t tp_trace_cap, tp_trace_n;
            uint32_t tr_from = getenv("TP_DET_TRACE") ? (uint32_t)atoi(getenv("TP_DET_TRACE")) : 0;
            do {
                if (tr_from && at > tr_from && at - step <= tr_from) { tp_trace_cap = 20000000; tp_trace_buf = malloc(4ul * tp_trace_cap); tp_trace_n = 0; }
                r = tp_host_run(quantum, at < limit ? at : limit, 0);
                if (tp_trace_buf && tr_from) {   /* development: the blocks of one shot interval (out/trace.bin) */
                    char tp[1024]; snprintf(tp, sizeof tp, "%s/trace.bin", out); FILE *tf = fopen(tp, "wb");
                    if (tf) { fwrite(tp_trace_buf, 4, tp_trace_n, tf); fclose(tf); } free(tp_trace_buf); tp_trace_buf = NULL; tr_from = 0; }
                char p[1024], h[65]; snprintf(p, sizeof p, "%s/d-%03d.png", out, n++);
                { extern uint64_t tp_insns, tp_rep; tp_host_png(p, h); fprintf(stderr, "shot %s blocks=%u %s rep=%llu insns=%llu\n", p, at, h, (unsigned long long)tp_rep, (unsigned long long)tp_insns); }
                if (getenv("TP_DET_CKPT")) { snprintf(p, sizeof p, "%s/d-%03d.ckpt", out, n - 1); tp_host_checkpoint(p); }   /* (development: memory at each shot) */
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
