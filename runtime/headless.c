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

int main(int argc, char **argv) {
    const char *game = 0, *overlay = 0, *out = "work/headless", *keys = "", *clicks = "", *block_clicks = "";
    uint32_t quantum = 1000, limit = 8000000; int realtime = 0; double seconds = 0;
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
        else { fprintf(stderr, "unknown argument %s\n", a); return 2; }
    }
    if (!game || !overlay) { fprintf(stderr, "usage: headless --game GAME --overlay DIR [--image MAIN.TPIMG] ...\n"); return 2; }
    TpScript s = {0};
    uint32_t tmp[64 * 3];
    s.nkeys = parse(keys, tmp, 2, 64);
    for (int i = 0; i < s.nkeys; i++) s.keys[i].at = tmp[i * 2], s.keys[i].key = tmp[i * 2 + 1];
    s.nclicks = parse(clicks, tmp, 3, 64);
    for (int i = 0; i < s.nclicks; i++) s.clicks[i].at = tmp[i * 3], s.clicks[i].x = tmp[i * 3 + 1], s.clicks[i].y = tmp[i * 3 + 2];
    s.nblock_clicks = parse(block_clicks, tmp, 3, 64);
    for (int i = 0; i < s.nblock_clicks; i++)
        s.block_clicks[i].at = tmp[i * 3], s.block_clicks[i].x = tmp[i * 3 + 1], s.block_clicks[i].y = tmp[i * 3 + 2];
    mkdir(out, 0755);
    struct timespec t0, t1; clock_gettime(CLOCK_MONOTONIC, &t0);
    if (tp_host_init(game, overlay, !realtime, &s)) { fprintf(stderr, "init: %s\n", tp_host_status()); return 1; }
    int r;
    if (realtime) {
        do r = tp_host_run(2000, 0, 50); while (!r && ({ clock_gettime(CLOCK_MONOTONIC, &t1); (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) * 1e-9 < seconds; }));
    } else {
        r = tp_host_run(quantum, limit, 0);
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
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
