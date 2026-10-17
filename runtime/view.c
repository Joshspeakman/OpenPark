/* Native park view: the first stage of rebuilding Theme Park's engine natively
 * (docs/PLAN.md, "Engine rebuild"). The host owns the camera and the zoom. The
 * park itself is still drawn by the game's own renderer, called once per area
 * ("pass") with the camera moved; the passes are assembled into one picture of
 * the whole park ("canvas") that the app shows at any zoom, with the game's
 * interface (windows, date, messages) and its panel on top. Later stages
 * replace the renderer behind this interface with native drawing.
 *
 * Per frame, at three points of the game's main loop (host hooks, verify.c):
 *   FRAME_RENDER  before the game renders the park: passes for the visible area
 *   AFTER_RENDER  after it and the park-anchored extras the game draws next
 *                 (0x14de1a: the tile preview of the tool under the cursor):
 *                 the real frame's park area into the canvas
 *   PRESENT       the back buffer goes to the screen: what differs from the
 *                 park area is interface, kept as an overlay; publish
 * Pointer events in the park are mapped back by moving the game's camera so the
 * point is inside its real view (tp_view_poll, on the emulation thread).
 *
 * Addresses are the game's (MAIN.EXE as loaded; work/survey and the plan say
 * how they were found):
 *   0x11b837  render_park(cam_x, cam_y, rows): landscape into its cache when
 *             dirty, the cache into the back buffer, then everything on it
 *   0x113b9a  the game's per-frame call of render_park, from 0x111e4e, which
 *             then calls 0x14de1a (park-anchored extras) and returns to
 *             0x111e90, or skips it and goes on at 0x111e98
 *   0x180210  the copy of the back buffer to the screen (320x200), 0x155507
 *             in 640x480; both draw the game's own cursor into it first
 *   0x1d667a  camera x, 0x1d6666 camera y (int16; the game keeps x 4..51,
 *             y 0..51). A step in x moves the picture 24 pixels, a step in y
 *             16 down and 8 across (measured, exact, the same in both modes)
 *   0x1d666e  landscape cache dirty flag
 *   0x1de5f6  screen mode: 1 = 320x200, 8 = 640x480 (only 320x200 so far)
 *   0x1de5b8  display pitch, 0x1de5bc back buffer, 0x1de5c0 display height
 * A pass changes nothing else (checked by diffing all memory: only the stack
 * below the caller's, the back buffer and the landscape cache). */
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int64_t tp_host_guest_call(uint32_t fn, const uint32_t *args, int nargs, uint32_t max_blocks);
uint8_t *tp_host_memory(void);
void tp_host_palette_rgba(uint32_t pal[256]);
int tp_host_png_rgba(const char *path, const uint32_t *px, int w, int h);
void tp_host_mouse(double fx, double fy, uint32_t buttons);
void tp_host_mouse_pos(int *x, int *y);
extern uint32_t tp_hook_pcs[8];
void tp_hooks_changed(void);
extern int tp_hook_which;
extern uint64_t tp_insns, tp_rep;

#define RENDER_PARK  0x11b837u
#define FRAME_RENDER 0x113b9au
#define BEFORE_EXTRAS 0x111e88u
#define AFTER_RENDER 0x111e90u
#define AFTER_SKIP   0x111e98u
/* The extras also draw the screen's furniture: the date (top left) and the
 * advisor buttons (top right). Those rectangles of the park are taken before
 * the extras, so they stay interface. */
#define PRESENT      0x180210u
#define PRESENT_HI   0x155507u      /* the same in 640x480 (banked VESA copy) */
#define CAM_X        0x1d667au
#define CAM_Y        0x1d6666u
#define LAND_DIRTY   0x1d666eu
#define SCREEN_MODE  0x1de5f6u
#define DISP_PITCH   0x1de5b8u
#define DISP_BUF     0x1de5bcu
#define DISP_H       0x1de5c0u
#define LAND_CACHE   0x1d3b98u
#define TILE_H       0x1d6654u      /* row height for the renderer's row count (0x113b9a) */

/* Geometry of the screen mode the game is in: 320x200 (s = 1) or its
 * 640x480 SVGA mode (s = 2, VESA 101h). Camera steps and the art scale with s. */
typedef struct { int s, sw, sh, wh, ux, uyx, uy, tx0, tx1, ty1, stx, sty, ox, oy, cw, ch; } Geo;
static Geo G;                       /* the emulation thread's (tp_view_hook) */
#define SW G.sw                     /* screen width */
#define SH G.sh                     /* screen height */
#define WORLD_H G.wh                /* park rows; the panel is below */
#define UX G.ux                     /* camera x step */
#define UYX G.uyx                   /* across, per camera y step */
#define UY G.uy                     /* down, per camera y step */
#define TX0 G.tx0                   /* the part of a pass trusted to be complete: */
#define TX1 G.tx1                   /* objects of cells outside the pass are cut */
#define TY1 G.ty1                   /* off near its edges, so passes overlap */
#define OX G.ox
#define OY G.oy
#define CW G.cw
#define CH G.ch
#define STEP_X G.stx                /* passes, in camera steps */
#define STEP_Y G.sty
#define CXMIN 2
#define CXMAX 53
#define CYMIN (-2)
#define CYMAX 53
#define GAME_CX0 4
#define GAME_CX1 51
#define GAME_CY0 0
#define GAME_CY1 51
#define MAX_SCREEN (640 * 480)
#define MAX_CANVAS (3968 * 2200)
/* The art is the same size in both modes (measured: a camera step moves the
 * picture (24, 0) or (8, 16) pixels in either); 640x480 shows more of the park. */
static Geo geo(int s) {
    Geo g; if (s != 2) s = 1;
    g.s = s; g.sw = 320 * s; g.sh = s == 1 ? 200 : 480; g.wh = s == 1 ? 178 : 436;
    g.ux = 24; g.uyx = 8; g.uy = 16;
    g.stx = s == 1 ? 12 : 24; g.sty = s == 1 ? 7 : 20;
    g.tx0 = 16; g.tx1 = g.tx0 + g.stx * g.ux; g.ty1 = g.sty * g.uy;
    g.ox = -(g.ux * CXMIN + g.uyx * CYMIN); g.oy = -(g.uy * CYMIN);
    g.cw = g.ux * CXMAX + g.uyx * CYMAX + g.sw + g.ox; g.ch = g.uy * CYMAX + g.wh + g.oy;
    return g;
}
static int hud_rect(int k, int *r) {               /* the furniture's rectangles in this mode */
    if (k == 0) { r[0] = 0; r[1] = 0; r[2] = 64; r[3] = 12; return 1; }
    if (k == 1) { r[0] = SW - 72; r[1] = 0; r[2] = SW; r[3] = 16; return 1; }
    return 0;
}

static uint8_t *M;
static int16_t r16(uint32_t a) { int16_t v; memcpy(&v, M + a, 2); return v; }
static uint32_t r32(uint32_t a) { uint32_t v; memcpy(&v, M + a, 4); return v; }
static void w16(uint32_t a, int16_t v) { memcpy(M + a, &v, 2); }
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
/* The game's screen mode as a scale: 1 (320x200), 2 (640x480) or 0 (neither, not drawable here). */
static int mode_scale(void) {
    uint32_t pitch = r32(DISP_PITCH), h = r32(DISP_H);
    if ((r16(SCREEN_MODE) & 1) && pitch == 320 && h == 200) return 1;
    if (r16(SCREEN_MODE) == 8 && pitch == 640 && h == 480) return 2;
    return 0;
}
static int cam_ox(int cx, int cy) { return UX * cx + UYX * cy + OX; }   /* the canvas position of a view's corner */
static int cam_oy(int cy) { return UY * cy + OY; }

/* ---- shared with the app (lock) ---- */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int want;                                  /* the app shows the zoomed view */
static double vis_x, vis_y, vis_z = 1;            /* visible canvas: corner and zoom */
static double vis_lw;                             /* the layout's width in screen pixels (wider than the
                                                     screen: widescreen; the interface stays in a centred column) */
static int vis_s = 1;                             /* the scale the app's canvas coordinates are in */
typedef struct { double fx, fy; uint32_t buttons; } Ev;
static Ev evq[256]; static int evn;
static int restore, restore_cx, restore_cy;       /* camera for the game's own view after zooming */
static uint8_t pub_canvas[MAX_CANVAS];
static uint16_t pub_overlay[MAX_SCREEN];         /* 0xffff: no interface there */
static uint32_t pub_pal[256];
static double pub_time;
#define CUR 64                                    /* the game's cursor, cut out of its last frame */
static uint16_t pub_cursor[CUR * CUR]; static int pub_cur_x, pub_cur_y, pub_cur_ok;   /* offset from the hotspot */
static double ptr_fx = -1, ptr_fy = -1;           /* the app's pointer (screen 0..1) */
static int pub_cx, pub_cy, pub_s;                  /* the published picture's scale */

/* ---- emulation thread ---- */
static uint8_t canvas[MAX_CANVAS], world_only[MAX_SCREEN];
static uint8_t pre_cursor[MAX_SCREEN]; static int pre_ok, pre_hx, pre_hy;   /* back buffer before the cursor */
static uint8_t pre_extras[MAX_SCREEN]; static int have_pre_extras;
static int active, have_world, in_frame;
static int16_t seen_cx = -999, seen_cy = -999;   /* the camera as last seen or set by the view */
static volatile int camera_moves;                 /* the game moved it itself (or the park appeared): the app recentres */
static int16_t real_cx, real_cy;

/* Renders the park with the camera at (cx, cy) into the back buffer and puts
 * the camera back; the landscape cache is marked dirty for the game's frame. */
static int pass(int cx, int cy) {
    int16_t ox = r16(CAM_X), oy = r16(CAM_Y);
    w16(CAM_X, (int16_t)cx); w16(CAM_Y, (int16_t)cy); w16(LAND_DIRTY, 1);
    int th = (uint16_t)r16(TILE_H); if (th <= 0) th = 32;
    uint32_t args[3] = {(uint32_t)cx, (uint32_t)cy, (uint32_t)(((G.s == 1 ? 400 : 480) + th - 1) / th)};   /* rows, as 0x113b9a */
    int64_t r = tp_host_guest_call(RENDER_PARK, args, 3, 50000000);
    w16(CAM_X, ox); w16(CAM_Y, oy); w16(LAND_DIRTY, 1);
    return r < 0 ? -1 : 0;
}
static void blit_to_canvas(int cx, int cy, int x0, int x1, int y0, int y1) {
    const uint8_t *src = M + r32(DISP_BUF);
    int bx = cam_ox(cx, cy), by = cam_oy(cy);
    for (int y = y0; y < y1; y++) {
        int cy2 = by + y; if (cy2 < 0 || cy2 >= CH) continue;
        int a = x0, b = x1;
        if (bx + a < 0) a = -bx;
        if (bx + b > CW) b = CW - bx;
        if (a < b) memcpy(canvas + (size_t)cy2 * CW + bx + a, src + y * SW + a, (size_t)(b - a));
    }
}

/* The grid of passes covering the canvas. */
typedef struct { int16_t cx, cy; int x0, y0, x1, y1; } Pass;
static Pass grid[512]; static int ngrid, rr;
static void make_grid(void) {
    ngrid = 0;
    for (int cy = CYMIN, last = 0; !last; cy += STEP_Y) {
        if (cy >= CYMAX) { cy = CYMAX; last = 1; }       /* the bottom row: the whole height */
        for (int cx = (int)floor((double)(-UYX * cy - OX - TX0) / UX); ; cx += STEP_X) {
            int x0 = cam_ox(cx, cy) + TX0;
            if (x0 >= CW || ngrid == 512) break;
            grid[ngrid++] = (Pass){(int16_t)cx, (int16_t)cy, x0, cam_oy(cy), x0 + (TX1 - TX0), cam_oy(cy) + (last ? WORLD_H : TY1)};
        }
    }
}

/* Passes must leave the game as it was, but drawing is not side-effect free:
 * the renderer advances things' animation frames (offset 10 of the 200-byte
 * thing records at [0x1d3b10]) every 16th tick. So the game's globals and its
 * data blocks (the pointer table at 0x1d3b00..0x1d3bbf, sizes up to the next
 * block) are saved before the passes and put back after, except the sprite
 * banks (read-only) and the screen buffers. */
#define GLOBALS_LO 0x1b0000u
#define GLOBALS_HI 0x1e1000u                       /* below the game's stack */
typedef struct { uint32_t at, n; } Region;
static Region saved[24]; static int nsaved; static uint8_t *save_buf; static size_t save_cap;
static void find_regions(void) {
    static const uint32_t skip[] = {0x1d3ba8, 0x1d3bb8, 0x1d3b80, 0x1d3b7c, 0x1d3ba0, 0x1d3b88, 0x1d3b74, 0x1d3b50,   /* sprites */
                                    0x1d3b04, 0x1d3b00, 0x1d3b94, 0x1d3b98};                                            /* screens */
    uint32_t ptr[48], slot[48]; int n = 0;
    for (uint32_t a = 0x1d3b00; a < 0x1d3bc0; a += 4) {
        uint32_t v = r32(a);
        if (v >= 0x200000 && v < (64u << 20)) { ptr[n] = v; slot[n] = a; n++; }
    }
    for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++)
        if (ptr[j] < ptr[i]) { uint32_t t = ptr[i]; ptr[i] = ptr[j]; ptr[j] = t; t = slot[i]; slot[i] = slot[j]; slot[j] = t; }
    nsaved = 0; saved[nsaved++] = (Region){GLOBALS_LO, GLOBALS_HI - GLOBALS_LO};
    for (int i = 0; i < n && nsaved < 24; i++) {
        int sk = 0; for (unsigned k = 0; k < sizeof skip / 4; k++) if (slot[i] == skip[k]) sk = 1;
        uint32_t end = i + 1 < n ? ptr[i + 1] : ptr[i] + 239800;         /* the last block: the things */
        if (!sk && end > ptr[i] && end - ptr[i] <= (4u << 20)) saved[nsaved++] = (Region){ptr[i], end - ptr[i]};
    }
    size_t total = 0; for (int i = 0; i < nsaved; i++) total += saved[i].n;
    if (total > save_cap) { free(save_buf); save_buf = malloc(total); save_cap = total; }
}
static void state_save(void) { size_t o = 0; for (int i = 0; i < nsaved; i++) { memcpy(save_buf + o, M + saved[i].at, saved[i].n); o += saved[i].n; } }
static void state_restore(void) { size_t o = 0; for (int i = 0; i < nsaved; i++) { memcpy(M + saved[i].at, save_buf + o, saved[i].n); o += saved[i].n; } }

/* Passes for the visible part of the canvas, within a time budget; the rest
 * of a large view is refreshed over the next frames (round robin). */
static void diff_report(const uint8_t *snap) {     /* development: what passes changed (TP_VIEW_DIFF) */
    uint32_t buf = r32(DISP_BUF), cache = r32(LAND_CACHE), bytes = SW * SH, sp = 0;
    { extern uint32_t tp_get(int); sp = tp_get(4); }
    int shown = 0; uint32_t run = 0, start = 0;
    for (uint32_t a = 0x10000; a < (64u << 20); a++) {
        int skip = (a >= buf && a < buf + bytes) || (a >= cache && a < cache + bytes) || a == LAND_DIRTY || a == LAND_DIRTY + 1
                   || (a < sp && a >= sp - 0x10000);
        int d = !skip && M[a] != snap[a];
        if (d && !run) start = a;
        if (d) run = a; else if (run && a > run + 16) { if (shown++ < 20) fprintf(stderr, "  pass changed %#x..%#x\n", start, run); run = 0; }
    }
    fprintf(stderr, "view diff: %d ranges changed by passes (things at %#x, 200-byte records)\n", shown, r32(0x1d3b10));
}
static void render_visible(double vx, double vy, double vw, double vh, int all) {
    static int diffs = -1; if (diffs < 0) diffs = getenv("TP_VIEW_DIFF") ? atoi(getenv("TP_VIEW_DIFF")) : 0;
    uint8_t *snap = NULL;
    if (diffs > 0) { diffs--; snap = malloc(64u << 20); memcpy(snap, M, 64u << 20); }
    uint64_t insns = tp_insns, reps = tp_rep;                 /* passes are not game time */
    int nosave = getenv("TP_VIEW_NOSAVE") != NULL, nopass = getenv("TP_VIEW_NOPASS") != NULL;
    if (getenv("TP_VIEW_DEBUG")) { static double l; static int c; c++; if (now() - l > 1) { fprintf(stderr, "view: %d frames/s\n", c); c = 0; l = now(); } }
    if (nopass) { if (getenv("TP_VIEW_SAVEONLY")) { find_regions(); state_save(); state_restore(); } tp_insns = insns; tp_rep = reps; return; }
    /* The landscape cache too (passes redraw it for their cameras): put back as it was, with
     * its dirty flag, so the game's frame does not redraw its landscape (that costs game time). */
    static uint8_t cache_keep[MAX_SCREEN]; uint32_t cache = r32(LAND_CACHE); size_t cache_n = (size_t)SW * SH;
    memcpy(cache_keep, M + cache, cache_n);
    if (!nosave) { find_regions(); state_save(); }
    double t0 = now(), budget = all ? 1.0 : 0.012;
    int16_t rcx = r16(CAM_X), rcy = r16(CAM_Y);                 /* the real view's rectangle in the canvas */
    int rx0 = cam_ox(rcx, rcy), ry0 = cam_oy(rcy), rx1 = rx0 + SW, ry1 = ry0 + WORLD_H;
    int start = rr;
    for (int k = 0; k < ngrid; k++) {
        Pass *p = &grid[(start + k) % ngrid];
        if (p->x1 < vx - 8 || p->x0 > vx + vw + 8 || p->y1 < vy - 8 || p->y0 > vy + vh + 8) continue;
        if (!all && p->x0 >= rx0 && p->x1 <= rx1 && p->y0 >= ry0 && p->y1 <= ry1) continue;   /* the real frame has it */
        if (pass(p->cx, p->cy) == 0) blit_to_canvas(p->cx, p->cy, TX0, TX1, 0, p->y1 - p->y0);
        rr = (start + k + 1) % ngrid;
        if (now() - t0 > budget) break;
    }
    tp_insns = insns; tp_rep = reps;
    if (!nosave) state_restore();                             /* (the dirty flag is among the globals) */
    memcpy(M + cache, cache_keep, cache_n);
    /* (the governor lets the game catch up the time the passes took: it runs while behind) */
    if (getenv("TP_VIEW_DEBUG")) {
        static double acc, last; static int calls, npass; acc += now() - t0; calls++;
        if (now() - last > 1) { fprintf(stderr, "view: %d frames, %.1f ms in passes this second, grid %d\n", calls, acc * 1e3, ngrid); acc = 0; calls = 0; last = now(); }
        (void)npass;
    }
    if (snap) { diff_report(snap); free(snap); }
}

static void hooks(int on) {
    tp_hook_pcs[0] = FRAME_RENDER;
    tp_hook_pcs[1] = on ? AFTER_RENDER : 0xffffffffu;
    tp_hook_pcs[2] = on ? (G.s == 2 ? PRESENT_HI : PRESENT) : 0xffffffffu;
    tp_hook_pcs[3] = on ? AFTER_SKIP : 0xffffffffu;
    tp_hook_pcs[4] = on ? BEFORE_EXTRAS : 0xffffffffu;
    if (getenv("TP_VIEW_PROBE")) {   /* development: the back buffer after more steps of the frame */
        static const uint32_t probe[] = {0x13e149, 0x14e75e, 0x14e6c7};
        for (int k = 0; k < 3; k++) tp_hook_pcs[5 + k] = on ? probe[k] : 0xffffffffu;
    }
    tp_hooks_changed();
}

static void hires_step(int sc);
static void poster_step(void);
static volatile int hires_wanted, poster_wanted;   /* (below) */
/* Called by the host when the game reaches a hooked address. */
void tp_view_hook(void) {
    M = tp_host_memory();
    pthread_mutex_lock(&lock);
    int w = want; double vx = vis_x, vy = vis_y, vw = (vis_lw > 0 ? vis_lw : SW) / vis_z, vh = SH / vis_z;
    pthread_mutex_unlock(&lock);
    static int dbg = -1; if (dbg < 0) dbg = getenv("TP_VIEW_DEBUG_HOOKS") ? 400 : 0;
    int sc = mode_scale();
    if (dbg > 0) { dbg--; fprintf(stderr, "view hook %d want %d active %d scale %d\n", tp_hook_which, w, active, sc); }
    if (!sc) { if (active) { active = 0; hooks(0); } return; }
    if (sc != G.s) {                                      /* the game changed its screen mode ([R]): start again */
        G = geo(sc); ngrid = 0; memset(canvas, 0, (size_t)CW * CH);
        have_world = have_pre_extras = pre_ok = 0; in_frame = 0;
        pthread_mutex_lock(&lock); pub_time = 0; pub_cur_ok = 0; pthread_mutex_unlock(&lock);
        if (active) { active = 0; hooks(0); }
        return;
    }
    switch (tp_hook_which) {
    case 0:                                               /* FRAME_RENDER */
        if (getenv("TP_VIEW_DEBUG")) { static double l; static int c; c++; if (now() - l > 1) { fprintf(stderr, "view: %d game frames/s\n", c); c = 0; l = now(); } }
        hires_step(sc);
        if (in_frame) break;                              /* an interrupt returned here: same frame */
        poster_step();
        if (!ngrid) make_grid();
        if (in_frame) break;                              /* an interrupt returned here: same frame */
        if (getenv("TP_VIEW_STEPS") && !in_frame) {        /* development: passes one camera step apart */
            static int done; if (!done++) {
                int cx = r16(CAM_X), cy = r16(CAM_Y); static const int st[5][2] = {{0, 0}, {1, 0}, {0, -1}, {0, -3}, {0, -6}};
                for (int k = 0; k < 5; k++) {
                    pass(cx + st[k][0], cy + st[k][1]);
                    uint32_t pal[256]; tp_host_palette_rgba(pal); uint32_t *px = malloc(4 * SW * SH);
                    const uint8_t *bb = M + r32(DISP_BUF); for (int i = 0; i < SW * SH; i++) px[i] = pal[bb[i]];
                    char path[512]; snprintf(path, sizeof path, "%s.%d.png", getenv("TP_VIEW_STEPS"), k);
                    tp_host_png_rgba(path, px, SW, SH); free(px);
                    snprintf(path, sizeof path, "%s.%d.raw", getenv("TP_VIEW_STEPS"), k);
                    FILE *f = fopen(path, "wb"); if (f) { fwrite(bb, 1, (size_t)SW * SH, f); fclose(f); }
                }
            }
        }
        if (active || w) {                                /* the game moved its camera (a jump to an event, its keys) */
            int16_t cx = r16(CAM_X), cy = r16(CAM_Y);
            if (cx != seen_cx || cy != seen_cy) { seen_cx = cx; seen_cy = cy; camera_moves++; }
        }
        if (w && !active) { active = 1; hooks(1); render_visible(vx, vy, vw, vh, 1); }
        else if (!w) {                                    /* the view is off: no hooks at all until it is wanted */
            active = 0; hooks(0);
            if (!hires_wanted && !poster_wanted) { tp_hook_pcs[0] = 0xffffffffu; tp_hooks_changed(); }
        }
        else if (active) {                                /* passes at most 30 times a second (the game draws faster) */
            static double last; double t = now();
            if (t - last >= 1.0 / 30) { last = t; render_visible(vx, vy, vw, vh, 0); }
        }
        in_frame = active;
        break;
    case 1: case 3:                                       /* AFTER_RENDER (or AFTER_SKIP): the real frame's park */
        if (tp_hook_which == 3 && !in_frame) break;       /* already taken after the extras */
        in_frame = 0;
        if (!active) break;
        real_cx = r16(CAM_X); real_cy = r16(CAM_Y);
        if (tp_hook_which == 1 && have_pre_extras) {      /* the furniture's rectangles from before the extras */
            uint8_t *bb = M + r32(DISP_BUF);
            static uint8_t keep[MAX_SCREEN]; size_t n = (size_t)SW * WORLD_H; memcpy(keep, bb, n);
            int hr[4];
            for (int r = 0; hud_rect(r, hr); r++)
                for (int y = hr[1]; y < hr[3]; y++)
                    memcpy(bb + y * SW + hr[0], pre_extras + y * SW + hr[0], (size_t)(hr[2] - hr[0]));
            memcpy(world_only, bb, n);
            blit_to_canvas(real_cx, real_cy, 0, SW, 0, WORLD_H);
            memcpy(bb, keep, n);                          /* the game's frame goes on as it drew it */
            have_world = 1; have_pre_extras = 0;
            break;
        }
        { static int dumped; if (getenv("TP_VIEW_DUMP") && ++dumped == 30) {   /* development: the back buffer at capture */
            uint32_t pal[256]; tp_host_palette_rgba(pal); uint32_t *px = malloc(4 * SW * SH);
            const uint8_t *bb = M + r32(DISP_BUF); for (int i = 0; i < SW * SH; i++) px[i] = pal[bb[i]];
            tp_host_png_rgba(getenv("TP_VIEW_DUMP"), px, SW, SH); free(px);
            fprintf(stderr, "view dump at hook %d\n", tp_hook_which); } }
        if (getenv("TP_VIEW_DEBUG")) { int mx, my; tp_host_mouse_pos(&mx, &my); fprintf(stderr, "view frame: camera %d,%d mouse %d,%d\n", real_cx, real_cy, mx / 2, my / 2); }
        memcpy(world_only, M + r32(DISP_BUF), (size_t)SW * WORLD_H);
        blit_to_canvas(real_cx, real_cy, 0, SW, 0, WORLD_H);
        have_world = 1;
        break;
    case 4:                                               /* BEFORE_EXTRAS */
        if (active && in_frame) { memcpy(pre_extras, M + r32(DISP_BUF), (size_t)SW * WORLD_H); have_pre_extras = 1; }
        break;
    case 5: case 6: case 7:                              /* development probes */
        { static int n[8]; if (getenv("TP_VIEW_DUMP") && ++n[tp_hook_which] == 30) {
            char path[512]; snprintf(path, sizeof path, "%s.%d.png", getenv("TP_VIEW_DUMP"), tp_hook_which);
            uint32_t pal[256]; tp_host_palette_rgba(pal); uint32_t *px = malloc(4 * SW * SH);
            const uint8_t *bb = M + r32(DISP_BUF); for (int i = 0; i < SW * SH; i++) px[i] = pal[bb[i]];
            tp_host_png_rgba(path, px, SW, SH); free(px); } }
        if (active && have_world) {
            const uint8_t *bb = M + r32(DISP_BUF); int d = 0;
            for (int i = 0; i < SW * WORLD_H; i++) d += bb[i] != world_only[i];
            static int shown; if (shown++ < 60) fprintf(stderr, "view probe %d (%#x): %d park pixels changed\n", tp_hook_which, tp_hook_pcs[tp_hook_which], d);
        }
        break;
    case 2:                                               /* PRESENT: interface overlay, publish */
        if (!active || !have_world) break;
        have_world = 0;
        pthread_mutex_lock(&lock);
        const uint8_t *bb = M + r32(DISP_BUF);
        for (int i = 0; i < SW * WORLD_H; i++) pub_overlay[i] = bb[i] != world_only[i] ? bb[i] : 0xffff;
        if (pre_ok) {   /* the screen now = the last back buffer + the cursor drawn at the last mouse position */
            const uint8_t *tp_host_screen(void); const uint8_t *vga = tp_host_screen();
            int x0 = SW, y0 = SH, x1 = -1, y1 = -1;
            for (int y = pre_hy - CUR / 2 < 0 ? 0 : pre_hy - CUR / 2; y < SH && y < pre_hy + CUR / 2; y++)
                for (int x = pre_hx - CUR / 2 < 0 ? 0 : pre_hx - CUR / 2; x < SW && x < pre_hx + CUR / 2; x++)
                    if (vga[y * SW + x] != pre_cursor[y * SW + x]) { if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; }
            if (x1 >= 0) {
                for (int i = 0; i < CUR * CUR; i++) pub_cursor[i] = 0xffff;
                for (int y = y0; y <= y1 && y - y0 < CUR; y++)
                    for (int x = x0; x <= x1 && x - x0 < CUR; x++)
                        if (vga[y * SW + x] != pre_cursor[y * SW + x]) pub_cursor[(y - y0) * CUR + x - x0] = vga[y * SW + x];
                pub_cur_x = x0 - pre_hx; pub_cur_y = y0 - pre_hy; pub_cur_ok = 1;
            }
        }
        memcpy(pre_cursor, bb, (size_t)SW * SH); pre_ok = 1;
        { int mx, my; tp_host_mouse_pos(&mx, &my); pre_hx = mx * SW / 640; pre_hy = G.s == 1 ? my / 2 : my; }   /* the driver's 640x400 / 640x480 */
        memcpy(pub_canvas, canvas, (size_t)CW * CH); pub_s = G.s;
        tp_host_palette_rgba(pub_pal);
        pub_cx = real_cx; pub_cy = real_cy; pub_time = now();
        pthread_mutex_unlock(&lock);
        break;
    }
}

/* Pointer events, on the emulation thread: park points move the camera so the
 * point is inside the real view (unless it already is), then go to the game
 * at their place in that view; panel points go straight through. */
void tp_view_poll(void) {
    if (!evn && !restore) return;
    M = tp_host_memory();
    Ev q[256]; int n;
    pthread_mutex_lock(&lock);
    if (restore) { w16(CAM_X, (int16_t)restore_cx); w16(CAM_Y, (int16_t)restore_cy); w16(LAND_DIRTY, 1); restore = 0; }
    n = evn; memcpy(q, evq, sizeof(Ev) * n); evn = 0;
    double vx = vis_x, vy = vis_y, z = vis_z, lw = vis_lw > 0 ? vis_lw : SW; int on = want && active && vis_s == G.s;
    pthread_mutex_unlock(&lock);
    double col = (lw - SW) / 2;                           /* the interface column's left edge in the layout */
    for (int i = 0; i < n; i++) {
        double lx = q[i].fx * lw, ly = q[i].fy * SH;      /* layout point */
        double cxs = lx - col;                            /* ... in the screen column */
        int incol = cxs >= 0 && cxs < SW, ui = incol && ly >= WORLD_H;   /* the panel, or the game's interface: through */
        if (on && incol && !ui) {
            int ox = (int)cxs, oy = (int)ly;
            pthread_mutex_lock(&lock);
            for (int dy = -2; dy <= 2 && !ui; dy++) for (int dx = -2; dx <= 2 && !ui; dx++) {
                int x = ox + dx, y = oy + dy;
                if (x >= 0 && x < SW && y >= 0 && y < WORLD_H && pub_overlay[y * SW + x] != 0xffff) ui = 1;
            }
            pthread_mutex_unlock(&lock);
        }
        if (!on || ui) {
            double fx = incol ? cxs / SW : (cxs < 0 ? 0 : 0.9999);
            tp_host_mouse(fx, q[i].fy, q[i].buttons);
            continue;
        }
        {
            double px = vx + lx / z, py = vy + ly / z;                    /* canvas point */
            int cx = r16(CAM_X), cy = r16(CAM_Y);
            double sx = px - cam_ox(cx, cy), sy = py - cam_oy(cy);
            if (sx < 32 || sx > SW - 32 || sy < 24 || sy > WORLD_H - 24) {
                int ny = (int)lround((py - OY - WORLD_H / 2.0) / UY);
                ny = ny < GAME_CY0 ? GAME_CY0 : ny > GAME_CY1 ? GAME_CY1 : ny;
                int nx = (int)lround((px - OX - SW / 2.0 - UYX * ny) / UX);
                nx = nx < GAME_CX0 ? GAME_CX0 : nx > GAME_CX1 ? GAME_CX1 : nx;
                if (nx != cx || ny != cy) { w16(CAM_X, (int16_t)nx); w16(CAM_Y, (int16_t)ny); w16(LAND_DIRTY, 1); seen_cx = (int16_t)nx; seen_cy = (int16_t)ny; }
                sx = px - cam_ox(nx, ny); sy = py - cam_oy(ny);
            }
            if (sx < 0) sx = 0; if (sx > SW - 1) sx = SW - 1;
            if (sy < 0) sy = 0; if (sy > WORLD_H - 1) sy = WORLD_H - 1;
            if (getenv("TP_VIEW_DEBUG")) fprintf(stderr, "view poll: canvas %.0f,%.0f camera %d,%d -> screen %.0f,%.0f\n", px, py, r16(CAM_X), r16(CAM_Y), sx, sy);
            tp_host_mouse(sx / SW, sy / SH, q[i].buttons);
        }
    }
}

/* ---- the app's side ---- */
/* The zoomed view on (zoom != 1) or off; (vx, vy) is the canvas point at the
 * centre of the park area. Turning it off moves the game's camera there. */
/* Start in 640x480: the game switches with its R key, which works once its
 * park is on screen, so the key is sent after the park has drawn a while. */
static volatile int hires_wanted; static int park_frames;
void tp_view_request_hires(int on) {
    hires_wanted = on; park_frames = 0;
    if (on) { tp_hook_pcs[0] = FRAME_RENDER; tp_hooks_changed(); }
}
static void hires_step(int sc) {
    if (!hires_wanted) return;
    if (sc == 2) { hires_wanted = 0; return; }           /* already there */
    if (++park_frames < 40) return;
    void tp_host_key(uint32_t); void tp_host_key_raw(uint8_t, int);
    tp_host_key(0x1372); tp_host_key_raw(0x13, 1); tp_host_key_raw(0x13, 0);
    hires_wanted = 0;
}
static void render_visible(double vx, double vy, double vw, double vh, int all);
static void make_grid(void);
/* "Save Park Picture": every pass, the whole canvas, once (on the emulation thread). */
static volatile int poster_wanted, poster_ready; static uint32_t *poster_px; static int poster_w, poster_h;
void tp_view_request_poster(void) {
    pthread_mutex_lock(&lock); free(poster_px); poster_px = NULL; poster_ready = 0; poster_wanted = 1; pthread_mutex_unlock(&lock);
    tp_hook_pcs[0] = FRAME_RENDER; tp_hooks_changed();
}
int tp_view_take_poster(uint32_t **px, int *w, int *h) {   /* 1: done, *px is the caller's (malloc) */
    pthread_mutex_lock(&lock);
    int ok = poster_ready; if (ok) { *px = poster_px; *w = poster_w; *h = poster_h; poster_px = NULL; poster_ready = 0; }
    pthread_mutex_unlock(&lock);
    return ok;
}
static void poster_step(void) {
    if (!poster_wanted) return;
    if (!ngrid) make_grid();
    render_visible(0, 0, CW, CH, 1);
    /* the park's bounding box: what the passes drew (the rest of the canvas stays 0) */
    int x0 = CW, y0 = CH, x1 = -1, y1 = -1;
    for (int y = 0; y < CH; y++) for (int x = 0; x < CW; x++) if (canvas[(size_t)y * CW + x]) {
        if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; }
    if (x1 < 0) { x0 = y0 = 0; x1 = CW - 1; y1 = CH - 1; }
    int w = x1 - x0 + 1, h = y1 - y0 + 1;
    uint32_t pal[256]; tp_host_palette_rgba(pal);
    uint32_t *px = malloc(sizeof *px * (size_t)w * h);
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) px[(size_t)y * w + x] = pal[canvas[(size_t)(y0 + y) * CW + x0 + x]];
    pthread_mutex_lock(&lock); free(poster_px); poster_px = px; poster_w = w; poster_h = h; poster_ready = 1; poster_wanted = 0; pthread_mutex_unlock(&lock);
}
static int enabled;
void tp_view_enable(int main_game) {
    pthread_mutex_lock(&lock); enabled = main_game; if (!enabled) want = 0; pthread_mutex_unlock(&lock);
    if (!main_game) { for (int h = 0; h < 8; h++) tp_hook_pcs[h] = 0xffffffffu; tp_hooks_changed(); }
    active = 0; have_world = 0; in_frame = 0;
}
void tp_view_geometry(int *sw, int *sh, int *wh);
int tp_view_set(int on, double cx, double cy, double zoom, double layout_w) {
    pthread_mutex_lock(&lock);
    if (!enabled && on) { pthread_mutex_unlock(&lock); return 0; }
    Geo g = geo(vis_s);
    if (!on && want) {                                   /* back to the game's own view, where the zoomed one was */
        int ny = (int)lround((cy - g.oy - g.wh / 2.0) / g.uy);
        restore_cy = ny < GAME_CY0 ? GAME_CY0 : ny > GAME_CY1 ? GAME_CY1 : ny;
        int nx = (int)lround((cx - g.ox - g.sw / 2.0 - g.uyx * restore_cy) / g.ux);
        restore_cx = nx < GAME_CX0 ? GAME_CX0 : nx > GAME_CX1 ? GAME_CX1 : nx;
        restore = 1;
    }
    if (on) { tp_hook_pcs[0] = FRAME_RENDER; tp_hooks_changed(); }
    want = on; vis_z = zoom > 0.05 ? zoom : 0.05; vis_lw = layout_w > g.sw ? layout_w : g.sw;
    vis_x = cx - vis_lw / 2.0 / vis_z; vis_y = cy - g.wh / 2.0 / vis_z;
    pthread_mutex_unlock(&lock);
    return 1;
}
/* The screen the view works in, as the game's mode is now: width, height and
 * park rows (320x200 with 178, or 640x480 with 436). Canvas coordinates are in
 * this screen's pixels, so when it changes the app starts its zoom again. */
void tp_view_geometry(int *sw, int *sh, int *wh) {
    M = tp_host_memory();
    int sc = mode_scale(); Geo g = geo(sc ? sc : 1);
    pthread_mutex_lock(&lock); vis_s = g.s; pthread_mutex_unlock(&lock);
    *sw = g.sw; *sh = g.sh; *wh = g.wh;
}
/* The canvas point at the centre of the game's own view (where zooming starts). */
void tp_view_camera_center(double *cx, double *cy) {
    M = tp_host_memory();
    Geo g = geo(vis_s); int x = r16(CAM_X), y = r16(CAM_Y);
    *cx = g.ux * x + g.uyx * y + g.ox + g.sw / 2.0; *cy = g.uy * y + g.oy + g.wh / 2.0;
}
int tp_view_camera_moves(void) { return camera_moves; }
void tp_view_canvas_size(double *w, double *h) { Geo g = geo(vis_s); *w = g.cw; *h = g.ch; }
/* Is the zoomed view being drawn (the game is showing its park, at the app's scale)? */
int tp_view_live(void) {
    pthread_mutex_lock(&lock); int live = want && pub_time && pub_s == vis_s && now() - pub_time < 0.5; pthread_mutex_unlock(&lock);
    return live;
}
/* A pointer event, in the game screen's 0..1 coordinates (as tp_host_mouse). */
void tp_view_mouse(double fx, double fy, uint32_t buttons) {
    pthread_mutex_lock(&lock);
    int on = want; ptr_fx = fx; ptr_fy = fy;
    if (on && evn < 256) evq[evn++] = (Ev){fx, fy, buttons};
    pthread_mutex_unlock(&lock);
    if (!on) tp_host_mouse(fx, fy, buttons);
}
/* The picture at ow x oh (the game screen's layout at any size): the visible
 * canvas in the park area with the interface overlay on top, and the panel from
 * the game's own frame (frame: fw x fh RGBA, tp_host_frame_rgba_any). */
void tp_view_compose(uint32_t *out, int ow, int oh, const uint32_t *frame, int fw, int fh, int smooth) {
    pthread_mutex_lock(&lock);
    Geo g = geo(pub_s);
    double z = vis_z, vx = vis_x, vy = vis_y, lw = vis_lw > g.sw ? vis_lw : g.sw, col = (lw - g.sw) / 2;
    double lpx = lw / ow, lpy = (double)g.sh / oh;                     /* layout pixels per output pixel */
    double sx = lpx / z, sy = lpy / z;                                 /* canvas pixels per output pixel */
    int ss = sx > 1.4 ? 2 : 1;                                         /* minifying: average 2x2 */
    for (int y = 0; y < oh; y++) {
        uint32_t *o = out + (size_t)y * ow;
        double ly = (y + 0.5) * lpy, py = vy + ly / z;
        int iy = (int)ly, panel_row = iy >= g.wh;
        for (int x = 0; x < ow; x++) {
            double lx = (x + 0.5) * lpx; int ix = (int)floor(lx - col);
            if (ix >= 0 && ix < g.sw) {                                /* the screen column: panel and interface */
                if (panel_row) { o[x] = frame[(size_t)(iy * fh / g.sh) * fw + ix * fw / g.sw]; continue; }
                uint16_t u = pub_overlay[iy * g.sw + ix];
                if (u != 0xffff) { o[x] = pub_pal[u]; continue; }
            }
            double px = vx + lx / z;
            if (ss == 1 && !smooth) {
                int cx = (int)floor(px), cy = (int)floor(py);
                o[x] = cx >= 0 && cy >= 0 && cx < g.cw && cy < g.ch ? pub_pal[pub_canvas[(size_t)cy * g.cw + cx]] : 0xff000000u;
                continue;
            }
            /* 2x2 samples: an average when minifying, bilinear-ish when smooth */
            uint32_t r = 0, gg = 0, b = 0;
            for (int k = 0; k < 4; k++) {
                double qx = ss == 2 ? px + ((k & 1) - 0.5) * sx * 0.5 : px - 0.5 + (k & 1);
                double qy = ss == 2 ? py + ((k >> 1) - 0.5) * sy * 0.5 : py - 0.5 + (k >> 1);
                int cx = (int)floor(qx), cy = (int)floor(qy);
                uint32_t c = cx >= 0 && cy >= 0 && cx < g.cw && cy < g.ch ? pub_pal[pub_canvas[(size_t)cy * g.cw + cx]] : 0xff000000u;
                r += c & 255; gg += (c >> 8) & 255; b += (c >> 16) & 255;
            }
            o[x] = 0xff000000u | ((b / 4) << 16) | ((gg / 4) << 8) | (r / 4);
        }
    }
    /* The game's cursor at the pointer, at the screen's scale (over the panel the frame has it already). */
    double plx = ptr_fx * lw, ply = ptr_fy * g.sh;
    int over_panel = ply >= g.wh && plx - col >= 0 && plx - col < g.sw;
    if (pub_cur_ok && ptr_fy >= 0 && !over_panel) {
        double kx = 1 / lpx, ky = 1 / lpy;
        int hx = (int)(ptr_fx * ow), hy = (int)(ptr_fy * oh);
        for (int y = 0; y < CUR; y++) for (int x = 0; x < CUR; x++) {
            uint16_t u = pub_cursor[y * CUR + x]; if (u == 0xffff) continue;
            int bx = hx + (int)((pub_cur_x + x) * kx), by = hy + (int)((pub_cur_y + y) * ky);
            for (int yy = by; yy < by + (int)ceil(ky); yy++) for (int xx = bx; xx < bx + (int)ceil(kx); xx++)
                if (xx >= 0 && yy >= 0 && xx < ow && yy < oh) out[(size_t)yy * ow + xx] = pub_pal[u];
        }
    }
    pthread_mutex_unlock(&lock);
}

/* ---- development (headless): TP_VIEW_TEST=<dir> renders the whole canvas once ---- */
static const char *test_dir;
static int test_state;
void tp_view_test(const char *dir) {
    test_dir = dir; test_state = 1;
    tp_view_enable(1);
    int sw, sh, wh; tp_view_geometry(&sw, &sh, &wh);
    double w, h; tp_view_canvas_size(&w, &h);
    tp_view_set(1, w / 2.0, h / 2.0, sw / w, 0);
}
int tp_view_test_step(void) {          /* once published: write the canvas and a composite */
    if (test_state != 1 || !tp_view_live()) return 0;
    uint32_t *px = malloc(sizeof *px * CW * CH);
    pthread_mutex_lock(&lock);
    for (int i = 0; i < CW * CH; i++) px[i] = pub_pal[pub_canvas[i]];
    pthread_mutex_unlock(&lock);
    char p[1024]; snprintf(p, sizeof p, "%s/canvas.png", test_dir); tp_host_png_rgba(p, px, CW, CH);
    free(px); test_state = 2;
    fprintf(stderr, "view test: canvas %dx%d, %d passes -> %s\n", CW, CH, ngrid, p);
    return 1;
}
