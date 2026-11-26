/* The launch window's moving fairground (platform/sdl/midway.c) rendered once to a PNG, as the Mac's TP_LAUNCHER_SHOT
 * renders the window, for comparing with docs/launcher-reference/ (main.png: 2x at t 0.9; main-fireworks.png: 1x at
 * t 1.4; page-*.png: 1x at t 0.9). A hidden window's renderer draws into a target the window's size at the scale: the
 * back layers, a stand-in for the static view (platform/sdl/launcher.c's: here plain dark boxes where its pieces are,
 * drawn on a draw.c canvas and uploaded with midway_upload), then the front layers; the pixels are read back and written
 * by draw.c. With --bench it also times frames.
 *
 *   build (MSYS2 CLANG64, or Linux with gcc/clang; draw.c needs vendor/stb):
 *     mkdir -p build && cc -std=gnu11 -O2 -Wall -Wextra -I platform/sdl $(pkg-config --cflags sdl2) tests/midway_shot.c \
 *        platform/sdl/midway.c platform/sdl/draw.c $(pkg-config --libs sdl2) -lm -o build/midway_shot
 *   run: build/midway_shot out.png [--t 0.9] [--scale 2] [--page] [--view none] [--park WxH] [--soft] [--shots n] [--bench 600]
 *     --park WxH: a made-up picture (a checkerboard of that many pixels, nothing from the game) as the player's park
 *     (--soft: as GOG's screenshots); --shots n: n made-up 640x480 "screenshots" composed by midway_park_from_shots;
 *     SDL_RENDER_DRIVER=software tries the straight-alpha path. */
#include "midway.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The static view's stand-in: its pieces as dark boxes (launcher.md 1.3; a page: 1.4), clear between them. */
static Canvas *view_of(float sc, int page) {
    static const float pg[][4] = {{44, 30, 912, 536}, {556, 592, 162, 56}, {734, 588, 222, 64}};   /* (the board, Back, Play) */
    static const float b[][4] = {{44, 30, 584, 166}, {664, 30, 292, 412}, {44, 310, 107.2f, 132}, {163.2f, 310, 107.2f, 132},
        {282.4f, 310, 107.2f, 132}, {401.6f, 310, 107.2f, 132}, {520.8f, 310, 107.2f, 132}, {44, 484, 219, 82}, {275, 484, 219, 82},
        {506, 484, 219, 82}, {737, 484, 219, 82}, {44, 592, 176, 56}, {232, 592, 236, 56}, {556, 592, 162, 56}, {734, 588, 222, 64}};
    Canvas *c = cv_new((int)(1000 * sc + 0.5f), (int)(700 * sc + 0.5f));
    if (!c) return NULL;
    size_t n = page ? sizeof pg / sizeof *pg : sizeof b / sizeof *b;
    for (size_t i = 0; i < n; i++) {
        const float *k = page ? pg[i] : b[i];
        cv_rrect(c, box(k[0] * sc, k[1] * sc, k[2] * sc, k[3] * sc), 0, col(14, 16, 42));
    }
    return c;
}
static Image *checker(int w, int h, int tint) {   /* a stand-in picture: squares of 8 pixels, shaded across */
    Image *m = malloc(sizeof *m);
    if (!m || !(m->px = malloc((size_t)w * h * 4))) { free(m); return NULL; }
    m->w = w; m->h = h;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint8_t *p = m->px + ((size_t)y * w + x) * 4;
            int on = ((x >> 3) ^ (y >> 3)) & 1;
            p[0] = (uint8_t)(on ? 60 + 160 * x / w : 30 + 50 * tint); p[1] = (uint8_t)(on ? 140 : 60 + 120 * y / h); p[2] = (uint8_t)(on ? 70 : 40 + 60 * tint); p[3] = 255;
        }
    return m;
}
static void frame(SDL_Renderer *r, Midway *m, SDL_Texture *view, double t, int page) {
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);
    midway_draw_back(m, t);
    if (view) SDL_RenderCopy(r, view, NULL, NULL);
    midway_draw_front(m, t, page);
}

int main(int argc, char **argv) {
    const char *out = NULL;
    double t = 0.9; float sc = 2; int page = 0, view_on = 1, soft = 0, bench = 0, pw = 0, ph = 0, shots = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : "";
        if (!strcmp(a, "--t")) { t = atof(v); i++; }
        else if (!strcmp(a, "--scale")) { sc = (float)atof(v); i++; }
        else if (!strcmp(a, "--page")) page = 1;
        else if (!strcmp(a, "--view")) { view_on = strcmp(v, "none") != 0; i++; }
        else if (!strcmp(a, "--park")) { if (sscanf(v, "%dx%d", &pw, &ph) != 2) pw = ph = 0; i++; }
        else if (!strcmp(a, "--soft")) soft = 1;
        else if (!strcmp(a, "--shots")) { shots = atoi(v); i++; }
        else if (!strcmp(a, "--bench")) { bench = atoi(v); i++; }
        else out = a;
    }
    if (!out || sc <= 0) {
        fprintf(stderr, "usage: midway_shot out.png [--t s] [--scale k] [--page] [--view none] [--park WxH] [--soft] [--shots n] [--bench n]\n");
        return 2;
    }
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    int W = (int)(1000 * sc + 0.5f), H = (int)(700 * sc + 0.5f);
    SDL_Window *win = SDL_CreateWindow("midway", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, W, H, SDL_WINDOW_HIDDEN);
    SDL_Renderer *r = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_TARGETTEXTURE) : NULL;
    if (!r) { fprintf(stderr, "renderer: %s\n", SDL_GetError()); return 1; }
    SDL_RendererInfo info; SDL_GetRendererInfo(r, &info);
    SDL_Texture *target = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, W, H);
    if (!target) { fprintf(stderr, "target: %s\n", SDL_GetError()); return 1; }
    SDL_SetRenderTarget(r, target);

    Image *park = NULL;
    if (shots > 0) {
        Image *s[4] = {0};
        for (int i = 0; i < shots && i < 4; i++) s[i] = checker(640, 480, i % 2);
        park = midway_park_from_shots((const Image *const *)s, shots < 4 ? shots : 4);
        for (int i = 0; i < 4; i++) img_free(s[i]);
        soft = 1;
    } else if (pw > 0 && ph > 0) park = checker(pw, ph, 0);
    MidwayLayout lay = {box(44, 30, 584, 166), box(664, 30, 292, 412), box(734, 588, 222, 64), box(88, 80, 496, 94)};
    Uint64 f = SDL_GetPerformanceFrequency(), c0 = SDL_GetPerformanceCounter();
    Midway *m = midway_new(r, sc, park, soft, lay);
    Uint64 c1 = SDL_GetPerformanceCounter();
    img_free(park);
    if (!m) { fprintf(stderr, "midway_new failed: %s\n", SDL_GetError()); return 1; }
    SDL_Texture *view = NULL;
    if (view_on) {
        Canvas *vc = view_of(sc, page);
        if (vc) view = midway_upload(m, vc->px, vc->w, vc->h, NULL);
        cv_free(vc);
    }
    printf("renderer %s (%s), %dx%d, sprites made in %.1f ms; reduce motion %d\n", info.name,
           midway_premultiplied(m) ? "premultiplied" : "straight alpha", W, H, 1000.0 * (c1 - c0) / f, midway_reduce_motion());

    frame(r, m, view, t, page);
    Canvas *cv = cv_new(W, H);
    if (!cv || SDL_RenderReadPixels(r, NULL, SDL_PIXELFORMAT_RGBA32, cv->px, W * 4) != 0) { fprintf(stderr, "read: %s\n", SDL_GetError()); return 1; }
    if (cv_write_png(cv, out) != 0) { fprintf(stderr, "can't write %s\n", out); return 1; }
    printf("wrote %s (t %.2f, scale %.2f%s)\n", out, t, sc, page ? ", a page open" : "");

    if (bench > 0) {                              /* a frame's cost: the midway's calls (submit), the whole frame with the
                                                   * view, and each frame waited for (one pixel read back) */
        double sub = 0, worst = 0;
        uint8_t px[4];
        SDL_Rect one = {0, 0, 1, 1};
        Uint64 a0 = SDL_GetPerformanceCounter();
        for (int i = 0; i < bench; i++) {
            double ti = t + i / 60.0;
            SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
            SDL_RenderClear(r);
            Uint64 s0 = SDL_GetPerformanceCounter();
            midway_draw_back(m, ti);
            if (view) SDL_RenderCopy(r, view, NULL, NULL);
            midway_draw_front(m, ti, page);
            SDL_RenderFlush(r);
            double d = (double)(SDL_GetPerformanceCounter() - s0) / f;
            sub += d; if (d > worst) worst = d;
        }
        SDL_RenderReadPixels(r, &one, SDL_PIXELFORMAT_RGBA32, px, 4);
        double all = (double)(SDL_GetPerformanceCounter() - a0) / f;
        Uint64 b0 = SDL_GetPerformanceCounter();
        for (int i = 0; i < bench; i++) {
            frame(r, m, view, t + i / 60.0, page);
            SDL_RenderReadPixels(r, &one, SDL_PIXELFORMAT_RGBA32, px, 4);
        }
        double synced = (double)(SDL_GetPerformanceCounter() - b0) / f;
        printf("bench %d frames at %dx%d: submit %.3f ms each (longest %.3f), %.3f ms each unsynced overall, %.3f ms each waited for\n",
               bench, W, H, 1000 * sub / bench, 1000 * worst, 1000 * all / bench, 1000 * synced / bench);
    }
    cv_free(cv);
    if (view) SDL_DestroyTexture(view);
    midway_free(m);
    SDL_DestroyTexture(target);
    SDL_DestroyRenderer(r);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
