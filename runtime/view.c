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
#include <mach/mach_time.h>
#include <dispatch/dispatch.h>
#include <mach/mach.h>
#include <mach/thread_policy.h>

int64_t tp_host_guest_call(uint32_t fn, const uint32_t *args, int nargs, uint32_t max_blocks);
uint8_t *tp_host_memory(void);
void tp_host_palette_rgba(uint32_t pal[256]);
int tp_host_png_rgba(const char *path, const uint32_t *px, int w, int h);
void tp_host_mouse(double fx, double fy, uint32_t buttons);
void tp_host_mouse_pos(int *x, int *y);
extern uint32_t tp_hook_pcs[32];
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
/* Later in the frame, 0x1145fb (the interface) calls 0x1149a4, which draws the current tool's
 * preview at the pointer: the ghost of a ride or shop being placed, paths, scenery. That is park
 * too (it must zoom with it), so what 0x1149a4 draws goes into the park picture when it returns. */
#define TOOL_PREVIEW 0x1149a4u
/* The weather, 0x147606 (from the frame at 0x111f92, after the park): rain as random streaks
 * in colour 0xc0 over the screen (640x480 units), lightning, the thunder sound. Screen space
 * like the interface, so in widescreen it would cover only the 4:3 column: what it draws is
 * kept as a layer the compositor also lays over the side areas (rain is new each frame, so a
 * repeat of the column's does not show). */
#define WEATHER      0x147606u
#define RAIN_COLOUR  0xc0           /* .. 0xc5: a streak fades along its length */
#define WEATHER2     0x147a27u      /* the other weather (bit 1 of [0x1b14cc]): 360 drifting particles at 0x1d2bb0 */
#define WEATHER_FLAGS 0x1b14ccu     /* bit 0 rain, 1 particles, 2 storm (rain and lightning) */
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
typedef struct { int s, sw, sh, wh, ux, uyx, uy, tx0, tx1, ty1, stx, sty, ox, oy, cw, ch, ph, pt, pb, rt, rb; } Geo;
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
#define PH G.ph                     /* a native pass's height: taller than the screen, so things below are drawn */
#define PT G.pt                     /* its trusted rows: below what objects anchored above the pass would cover */
#define PB G.pb                     /* (they are not drawn), above where things of rows below would be cut */
#define RT G.rt                     /* the real frame's trusted rows (the game draws only its own rows) */
#define RB G.rb
#define OX G.ox
#define OY G.oy
#define CW G.cw
#define CH G.ch
#define STEP_X G.stx                /* passes, in camera steps */
#define STEP_Y G.sty
#define CXMIN 2
#define CXMAX ((int)tp_map_ext - 11)     /* (53 for the game's 64x64 park; bigmap.c) */
#define CYMIN (-2)
#define CYMAX ((int)tp_map_ext - 11)
#define GAME_CX0 4
#define GAME_CX1 ((int)tp_map_ext - 13)  /* (51 for the 64x64 park) */
#define GAME_CY0 0
#define GAME_CY1 ((int)tp_map_ext - 13)
#define MAX_SCREEN (640 * 480)
#define MAX_CANVAS (4400 * 2400)              /* (a 128x128 park in 640x480: 4352 x 2340) */
/* The art is the same size in both modes (measured: a camera step moves the
 * picture (24, 0) or (8, 16) pixels in either); 640x480 shows more of the park. */
extern uint32_t tp_map_w, tp_map_ext;
static uint32_t geo_ext;                  /* the park size G was made for */
static Geo geo(int s) {
    geo_ext = tp_map_ext;
    Geo g; if (s != 2) s = 1;
    g.s = s; g.sw = 320 * s; g.sh = s == 1 ? 200 : 480; g.wh = s == 1 ? 178 : 436;
    g.ux = 24; g.uyx = 8; g.uy = 16;
    /* The trusted part of a pass (TP_VIEW_TRUST measures it: passes one camera step apart disagree up
     * to ~45 px in from the sides, where objects of cells outside the view are cut, and from ~100 rows
     * down, where tall objects of cells below the drawn rows are): 64 px in at the sides; rows below. */
    g.stx = s == 1 ? 8 : 21;
    g.tx0 = 64; g.tx1 = g.tx0 + g.stx * g.ux;
    /* Vertically: the renderer draws an object from the cell at its back, so a ride whose cell is above a
     * view's top row is not drawn at all though it hangs down into the view (measured: up to ~64 rows), and
     * at the bottom things of rows beyond those drawn are missing. Native passes are drawn taller (more
     * rows into a taller buffer); the game's own frame only has its screen. */
    g.ph = g.wh + 192; g.pt = 80; g.pb = g.ph - 112;
    g.sty = (g.pb - g.pt) / g.uy; g.pb = g.pt + g.sty * g.uy;
    g.rt = 80; g.rb = g.wh - 72; if (g.rb < g.rt + 16) g.rb = g.rt + 16;
    g.ty1 = g.rb;
    g.ox = -(g.ux * CXMIN + g.uyx * CYMIN); g.oy = -(g.uy * CYMIN);
    g.cw = g.ux * CXMAX + g.uyx * CYMAX + g.sw + g.ox; g.ch = g.uy * CYMAX + g.wh + g.oy;
    return g;
}
/* The game's top bar: with the pointer at the screen's top edge it draws a bar across the top (with the date,
 * before the extras: so it is interface by its rows) and scrolls its camera up. In the park view the pointer reaches it only through the zone
 * below; the game's scrolling is undone while it is there (tp_view_poll, FRAME_RENDER). */
#define TOPBAR_H 8                  /* its height (rows, with its dark edge) */
#define TOPBAR_OPEN 2               /* the pointer this near the layout's top (in the screen column) opens it */
#define MENU_OPEN   0x1b14d2u       /* 4 bytes, one per top menu: 0xff closed (a drop-down open otherwise) */
static int menu_open(void);
static volatile int topbar;                       /* the pointer is on the top bar */
static int16_t topbar_cx, topbar_cy;              /* the camera to hold meanwhile */
static int hud_rect(int k, int *r) {               /* the furniture's rectangles in this mode */
    if (k == 0) { r[0] = 0; r[1] = 0; r[2] = 64; r[3] = 12; return 1; }
    if (k == 1) { r[0] = SW - 72; r[1] = 0; r[2] = SW; r[3] = 16; return 1; }
    return 0;
}

static uint8_t *M;
static int16_t r16(uint32_t a) { int16_t v; memcpy(&v, M + a, 2); return v; }
static uint32_t r32(uint32_t a) { uint32_t v; memcpy(&v, M + a, 4); return v; }
static int menu_open(void) { for (int k = 0; k < 4; k++) if (M[MENU_OPEN + k] != 0xff) return 1; return 0; }
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
static uint16_t pub_overlay[MAX_SCREEN], prev_overlay[MAX_SCREEN];         /* 0xffff: no interface there */
/* Panel size: see band_map. */
static volatile double ui_scale = 1;
static int pub_straddle;                                /* interface crosses the band's top (a game window): panel at its own size */
__attribute__((constructor)) static void ui_scale_env(void) { const char *e = getenv("TP_UI_SCALE"); if (e) ui_scale = atof(e); }
#define BAND_H 60
static uint32_t pub_pal[256];
static double pub_time;
#define CUR 64                                    /* the game's cursor, cut out of its last frame */
static uint16_t pub_cursor[CUR * CUR]; static int pub_cur_x, pub_cur_y, pub_cur_ok, pub_cur_w, pub_cur_h, pub_cur_serial;
static uint8_t pub_panel[MAX_SCREEN];             /* the panel rows as published (without the game's cursor) */
int tp_view_hw_cursor;                            /* the app shows the cursor itself (a Mac cursor): compose leaves it out */   /* offset from the hotspot */
static double ptr_fx = -1, ptr_fy = -1;           /* the app's pointer (screen 0..1) */
static int pub_cx, pub_cy, pub_s;                  /* the published picture's scale */

/* ---- emulation thread ---- */
static uint8_t canvas[MAX_CANVAS], world_only[MAX_SCREEN];
static int pre_ok;
static uint8_t pre_extras[MAX_SCREEN]; static int have_pre_extras;
static uint8_t pre_tool[MAX_SCREEN];
static uint8_t date_clean[MAX_SCREEN], date_cols[256];   /* the date as the extras drew it (clean), and its colours */
static int date_box[4]; static long date_fixed;
static uint8_t date_drawn[MAX_SCREEN];             /* the box's pixels the date drew (its corners may show the park) */
static int world_ok; static int16_t world_cx, world_cy;   /* world_only holds a park capture, for this camera */                          /* the date's box, as the extras last drew it (x1 = 0: not yet) */             /* the back buffer as the tool preview starts */
static uint8_t pre_weather[MAX_SCREEN], weather[MAX_SCREEN], weather_rep[MAX_SCREEN]; static int weather_n;   /* what the weather drew (0: nothing) */
static uint8_t pub_weather[MAX_SCREEN], pub_weather_rep[MAX_SCREEN]; static int pub_weather_n; static int weather_resentinel;
static volatile int tool_shown; static int tool_resentinel;                   /* the last frame had a tool preview */
static int16_t fr_cx, fr_cy;
/* Debug overlays (View menu): frame rates and the sections (passes) grid, drawn into the composed picture. */
int tp_view_debug_fps, tp_view_debug_grid;
static int fps_game_hook;                               /* (the game's frames counted with the view off too) */
/* Steady pacing (the default; Launcher: Pacing): park frames are released at a fixed rate, 60 a second at the
 * game's Original speed and in proportion for the faster ones, on the display's refresh when the app reports
 * it: every frame lasts the same, and the game, which advances once per frame, runs at an
 * even speed whatever is drawn. Elsewhere (menus without the park) the instruction governor paces as before. */
static int pacing;
static double pub_frame_t; static long pub_serial, shown_serial;   /* the published frame's start (paced), and which the app has shown */
static volatile double vs_t, vs_period;                 /* the display's last refresh and its period */
extern volatile double tp_frame_paced_at;
double tp_host_cycles(void);
void tp_view_vsync(double t, double period) { vs_t = t; vs_period = period; }
void tp_view_vsync_now(double period, double ago) { vs_t = now() - ago; vs_period = period; }   /* (the app's display refresh, `ago` seconds ago) */
/* Steady pacing for the app: a new frame is shown 0.7 of a game frame after it began, so every frame stays on the
 * display for the same number of refreshes however long its drawing took. 1: compose now; 0: keep showing
 * the last picture. Without steady pacing, every refresh. */
int tp_view_frame_due(void) {
    pthread_mutex_lock(&lock);
    int due = 1;
    if (pacing && pub_serial) {
        double period = 1.0 / (60.0 * tp_host_cycles() / 10000.0);
        double thr = pub_frame_t + period * 0.7;          /* (after any frame's drawing, before the next frame's) */
        double vp = vs_period, vt = vs_t;
        if (vp > 0.002 && vp < 0.05 && now() - vt < 1) {  /* its refresh: due from half a refresh before it, so callback jitter cannot move it */
            double r = vt + ceil((thr - vt) / vp) * vp; thr = r - vp * 0.5;
        }
        due = pub_serial != shown_serial && now() >= thr;
        if (due) { shown_serial = pub_serial;
            if (getenv("TP_PACE_DEBUG")) { static double last, lo = 1, hi, l2; double t = now(), d = t - last; last = t;
                if (d < 0.5) { if (d < lo) lo = d; if (d > hi) hi = d; }
                if (t - l2 > 1) { fprintf(stderr, "shown intervals: %.2f .. %.2f ms\n", lo * 1e3, hi * 1e3); lo = 1; hi = 0; l2 = t; } } }
    }
    pthread_mutex_unlock(&lock);
    return due;
}
int tp_view_pacing(void) { return pacing; }
void tp_view_set_pacing(int on) { pacing = on; if (on) { tp_hook_pcs[0] = FRAME_RENDER; tp_hooks_changed(); } }
void tp_view_pacing_init(void) { const char *e = getenv("TP_PACING"); tp_view_set_pacing(!(e && !strcmp(e, "dosbox"))); }   /* (qol.c, at the game's start) */
int tp_host_timer_behind(void);
static void pace_frame(void) {
    /* Timer ticks overdue: not now. The game's timer interrupts (240 a second) are delivered between its
     * instructions; a loop of short frames (one that waits on its timer counters while an effect starts) got
     * two or three a frame between the frame waits, and the park froze for up to a second, its timer at half
     * speed. A frame that finds them overdue runs on; the next is paced. */
    if (tp_host_timer_behind()) return;
    static double target;
    double fps = 60.0 * tp_host_cycles() / 10000.0; if (fps < 10) fps = 10;
    double period = 1.0 / fps, t = now();
    static double ideal;                                  /* the exact schedule; target: it on the refresh grid */
    if (!ideal || t - ideal > 4 * period) ideal = t;     /* (starting, or after a stall: no burst to catch up) */
    ideal += period; target = ideal;
    /* (not snapped to the display's refreshes per frame: the refresh times the app reports wobble, and a frame
     * schedule of exact periods already falls on the same refresh phase every frame; the app shows each frame
     * on a refresh, tp_view_frame_due) */
    double wait = target - now();
    if (getenv("TP_PACE_DEBUG")) { static double l3, late; if (-wait > late) late = -wait; if (t - l3 > 1) { fprintf(stderr, "pace late max %.2f ms\n", late * 1e3); late = 0; l3 = t; } }
    if (getenv("TP_PACE_DEBUG")) { static double l; static int n, sl; n++; if (wait > 0) sl++; if (t - l > 1) { fprintf(stderr, "pace: %d frames, %d waited, fps %.0f, last wait %.2f ms\n", n, sl, fps, wait * 1e3); n = sl = 0; l = t; } }
    static mach_timebase_info_data_t tb; if (!tb.denom) mach_timebase_info(&tb);
    { static int rt; if (!rt) {                           /* the game thread as a real-time thread: timers not coalesced, woken on time */
        rt = 1; thread_time_constraint_policy_data_t pol;
        pol.period = (uint32_t)(period * 1e9 * tb.denom / tb.numer);
        pol.computation = (uint32_t)(0.004 * 1e9 * tb.denom / tb.numer);
        pol.constraint = (uint32_t)(period * 0.9 * 1e9 * tb.denom / tb.numer);
        pol.preemptible = 1;
        kern_return_t kr = thread_policy_set(pthread_mach_thread_np(pthread_self()), THREAD_TIME_CONSTRAINT_POLICY, (thread_policy_t)&pol, THREAD_TIME_CONSTRAINT_POLICY_COUNT);
        fprintf(stderr, "pace: real-time thread policy %s\n", kr == KERN_SUCCESS ? "set" : "refused"); } }
    {   /* mach_wait_until: woken on time, unlike nanosleep under load; a millisecond at a time, the sound card
         * going on meanwhile (sb16.c: the HMI driver mixes just ahead of its DMA position) */
        void sb16_pump(double t); uint64_t ms = (uint64_t)(1e6 * tb.denom / tb.numer);
        while (target - now() > 0.0015) { mach_wait_until(mach_absolute_time() + ms); sb16_pump(now()); }
        double left = target - now();
        if (left > 0.0008) mach_wait_until(mach_absolute_time() + (uint64_t)((left - 0.0005) * 1e9 * tb.denom / tb.numer));
    }
    while (now() < target) ;                              /* (the last half millisecond, exactly) */
    tp_frame_paced_at = now();
    if (getenv("TP_PACE_TRACE")) { static int n; static double lt, la; if (n++ < 400 && n > 300) fprintf(stderr, "pt target+%.2f actual+%.2f wait %.2f over %.3f\n", (target - lt) * 1e3, (tp_frame_paced_at - la) * 1e3, wait * 1e3, (tp_frame_paced_at - target) * 1e3); lt = target; la = tp_frame_paced_at; }
    if (getenv("TP_PACE_DEBUG")) { static double last, lo = 1, hi, l2; double d = tp_frame_paced_at - last; last = tp_frame_paced_at;
        if (d < 0.5) { if (d < lo) lo = d; if (d > hi) hi = d; }
        if (tp_frame_paced_at - l2 > 1) { fprintf(stderr, "pace intervals: %.2f .. %.2f ms (refresh %.2f ms)\n", lo * 1e3, hi * 1e3, vs_period * 1e3); lo = 1; hi = 0; l2 = tp_frame_paced_at; } }
}
static int st_game, st_pub, st_comp; static long st_game_total; static double st_pass, st_t0;      /* counts this second */
static double sh_game, sh_pub, sh_comp, sh_pass;                        /* last second's */
static void stats_tick(void) {
    double t = now(); if (!st_t0) st_t0 = t;
    if (t - st_t0 >= 1) { double k = 1 / (t - st_t0); sh_game = st_game * k; sh_pub = st_pub * k; sh_comp = st_comp * k; sh_pass = st_pass * 1e3 * k;
        st_game = st_pub = st_comp = 0; st_pass = 0; st_t0 = t; }
}                     /* the camera as the frame began */
static long skipped_moved;                        /* frames left out: the camera moved while drawn */
static int active, have_world, in_frame, coherent;
static int frame_park, park_gap; static volatile int pub_park;   /* this frame drew the park; presents since one did; the game shows its park */   /* coherent: this frame had passes */
static int16_t seen_cx = -999, seen_cy = -999;   /* the camera as last seen or set by the view */
static volatile int drag_held;                    /* the pointer is dragging in the park (tp_view_poll) */
static volatile int camera_moves;                 /* the game moved it itself (or the park appeared): the app recentres */
static int16_t real_cx, real_cy;

/* Renders the park with the camera at (cx, cy) into the back buffer and puts
 * the camera back; the landscape cache is marked dirty for the game's frame. */
/* Development: the native landscape (native_land.c) against the game's cache after each pass. */
int tp_native_landscape(const uint8_t *mem, int cx0, int cy0, int rows, uint8_t *buf, int pitch, int w, int h);
int tp_native_landscape_part(const uint8_t *mem, int cx0, int cy0, int rows, uint8_t *buf, int pitch, int w, int h, int wx0, int wy0, int wx1, int wy1);
int tp_native_blit_flags(const uint8_t *mem);
static void native_check(int cx, int cy, int rows, const uint8_t *before) {
    static int on = -1; if (on < 0) on = getenv("TP_NATIVE_CHECK") != NULL;
    if (!on) return;
    static uint8_t mine[MAX_SCREEN]; static long passes, bad_passes, bad_px, deco;
    uint32_t pitch = r32(DISP_PITCH); int w = (int)r32(0x1de5c8), h = WORLD_H;   /* the landscape stops above the panel */
    memcpy(mine, before, (size_t)pitch * SH);
    int d = tp_native_landscape(M, cx, cy, rows, mine, (int)pitch, w, h);
    const uint8_t *game = M + r32(LAND_CACHE); long n = 0; int fx = -1, fy = -1;
    for (int i = 0; i < (int)pitch * h; i++) if (mine[i] != game[i]) { if (!n) { fx = i % pitch; fy = i / pitch; } n++; }
    passes++; if (n) { bad_passes++; bad_px += n; } if (d > 0) deco += d;
    if (n && bad_passes <= 5) fprintf(stderr, "native landscape: camera %d,%d differs in %ld pixels (first %d,%d), flags %d, %d decorated cells\n",
                                       cx, cy, n, fx, fy, tp_native_blit_flags(M), d);
    if (passes % 200 == 0 || passes == 20) fprintf(stderr, "native landscape: %ld passes, %ld differ (%ld pixels), %ld decorated cells left out\n", passes, bad_passes, bad_px, deco);
}
/* Development (TP_NATIVE_THINGS): the game's draw calls in a pass against the native renderer's, per cell. */
typedef struct { int kind, a, b, c; uint32_t d; int e; } LogE;
static LogE glog[20000], nlog[20000]; static int glog_n, nlog_n, logging;
static void addlog(LogE *l, int *n, int kind, int a, int b, int c, uint32_t d, int e) { if (*n < 20000) l[(*n)++] = (LogE){kind, a, b, c, d, e}; }
/* Pixel check of the native things layer: the screen as the things loop starts (0x11be5a)
 * and ends (0x11c01a), and the clip window it draws in. */
static uint8_t snap0[MAX_SCREEN + 65536], snap1[MAX_SCREEN + 65536]; static size_t snap_n; static int snapped;
typedef struct { uint8_t *buf; size_t size; long base, pitch; int clip_w, clip_h, clip_r, lowres; const uint8_t *ghost; long unsupported; } NDTarget;
void tp_native_draw_sprite(NDTarget *t, const uint8_t *s, int w, int h, int x, int y, int flags, int colour);
void tp_native_draw_small_text(NDTarget *t, const uint8_t *mem, int x, int y, const uint8_t *str, int colour);
extern const uint8_t *tp_native_text;
static NDTarget snap_t;
static struct { int ok, mode; long base, pitch; int clip_w, clip_h, clip_r; } clipinfo;   /* the things' clip window */
static int capture_clip;
void tp_view_nested_hook(void) {
    extern uint32_t tp_get(unsigned); uint32_t pc = tp_get(9), sp = tp_get(4);
    if (pc == 0x11be5au && capture_clip) {                /* as the things loop starts: 0x14de1a(1) has set it */
        clipinfo.base = (long)r32(0x1de5e0) * (long)r32(DISP_PITCH) + (long)r32(0x1de5d0); clipinfo.pitch = (long)r32(DISP_PITCH);
        clipinfo.clip_w = (int)r32(0x1de5c8); clipinfo.clip_h = (int)r32(0x1de5f0); clipinfo.clip_r = (int)r32(0x1de5ec);
        clipinfo.mode = G.s; clipinfo.ok = 1; capture_clip = 0;
    }
    if (!logging) return;
    if (pc == 0x11be5au || pc == 0x11c01au) {
        size_t n = (size_t)r32(DISP_PITCH) * (size_t)SH; if (n > sizeof snap0) n = sizeof snap0;
        memcpy(pc == 0x11be5au ? snap0 : snap1, M + r32(DISP_BUF), n); snap_n = n;
        if (pc == 0x11be5au) {
            snap_t = (NDTarget){NULL, n, (long)r32(0x1de5e0) * (long)r32(DISP_PITCH) + (long)r32(0x1de5d0), (long)r32(DISP_PITCH),
                                (int)r32(0x1de5c8), (int)r32(0x1de5f0), (int)r32(0x1de5ec), M[0x1de5f6] & 1, M + r32(0x1ba2f8), 0};
            snapped = 1;
        } else if (snapped == 1) snapped = 2;
        return;
    }
    if (pc == 0x11c22au) addlog(glog, &glog_n, 1, r16(sp + 4), (uint16_t)r16(sp + 8), (uint16_t)r16(sp + 12), 0, 0);
    else if (pc == 0x11c010u) addlog(glog, &glog_n, 3, 0, 0, 0, 0, 0);
    else if (pc == 0x180e88u || pc == 0x180f34u) {        /* the small font writes straight to the screen */
        uint32_t str = r32(sp + 12), h = 2166136261u;
        for (int k = 0; k < 64 && M[str + k]; k++) h = (h ^ M[str + k]) * 16777619u;
        addlog(glog, &glog_n, 4, (int16_t)r16(sp + 4), (int16_t)r16(sp + 8), (uint16_t)r16(sp + 16), h, pc == 0x180e88u);
    }
    else if (pc == 0x111bd5u)                             /* a font glyph in one colour, straight to the screen */
        addlog(glog, &glog_n, 5, (uint16_t)r16(sp + 4), (uint16_t)r16(sp + 8), M[sp + 16], r32(sp + 12), 0);
    else if (pc == 0x15ed21u) {
        uint32_t ebx = tp_get(3), ecx = tp_get(1), edx = tp_get(2), esi = tp_get(6);
        addlog(glog, &glog_n, 2, (int)ebx, (int)ecx, (int)(edx & 0xffff), esi, (uint16_t)r16(0x1becbc));
    }
}
static uint8_t nlog_text[20000][16];
static void native_logger(int kind, int a, int b, int c, uint32_t d, int e, void *ctx) {
    (void)ctx;
    if (kind == 4 && nlog_n < 20000 && tp_native_text) { int k = 0; for (; k < 15 && tp_native_text[k]; k++) nlog_text[nlog_n][k] = tp_native_text[k]; nlog_text[nlog_n][k] = 0; }
    addlog(nlog, &nlog_n, kind, a, b, c, d, e);
}
void tp_native_things(const uint8_t *mem, int cx0, int cy0, int rows, void (*lg)(int, int, int, int, uint32_t, int, void *), void *ctx);
static void things_check(int cx, int cy, int rows) {
    static long cells, compared, bad, missing_cls[0x400];
    /* walk both logs cell by cell */
    int gi = 0, ni = 0, all_native = 1;
    while (gi < glog_n && glog[gi].kind != 1) gi++;           /* the landscape's blits come first */
    while (gi < glog_n && ni < nlog_n) {
        if (glog[gi].kind != 1 || nlog[ni].kind != 1) { bad++; all_native = 0; break; }
        int ok_cell = glog[gi].a == nlog[ni].a && glog[gi].b == nlog[ni].b && glog[gi].c == nlog[ni].c;
        int g1 = ++gi, n1 = ++ni;
        while (gi < glog_n && glog[gi].kind != 3) gi++;
        while (ni < nlog_n && nlog[ni].kind != 3) ni++;
        int miss = ni < nlog_n ? nlog[ni].a : 0;
        cells++;
        if (!ok_cell) { bad++; all_native = 0; if (bad < 5) fprintf(stderr, "native things: cell order differs (game %d at %d,%d)\n", glog[g1 - 1].a, glog[g1 - 1].b, glog[g1 - 1].c); }
        else if (miss) { missing_cls[miss & 0x3ff]++; all_native = 0; }
        else {
            compared++;
            int same = gi - g1 == ni - n1;
            for (int k = 0; same && k < gi - g1; k++) {
                LogE *a = &glog[g1 + k], *b = &nlog[n1 + k];
                same = a->kind == b->kind && a->a == b->a && a->b == b->b && a->c == b->c && a->d == b->d && (a->e & 0xff) == (b->e & 0xff);
            }
            if (!same) {
                bad++; all_native = 0; if (bad < 8) fprintf(stderr, "native things: cell (thing %d) draws differ: game %d calls, native %d\n", glog[g1 - 1].a, gi - g1, ni - n1);
                if (bad == 1 && getenv("TP_NATIVE_THINGS_DUMP"))
                    for (int k = 0; k < gi - g1 || k < ni - n1; k++) {
                        LogE z = {0}, *a = k < gi - g1 ? &glog[g1 + k] : &z, *b = k < ni - n1 ? &nlog[n1 + k] : &z;
                        fprintf(stderr, "  game %d %d %d %#x %#x %#x | native %d %d %d %#x %#x %#x\n", a->kind, a->a, a->b, a->c, a->d, a->e,
                                b->kind, b->a, b->b, b->c, b->d, b->e);
                    }
            }
        }
        gi++; ni++;
    }
    if (all_native && snapped == 2) {                     /* draw the native calls over the first snapshot */
        static uint8_t work[sizeof snap0]; static long ppasses, pbad, pdiff, punsup, pblits;
        memcpy(work, snap0, snap_n);
        NDTarget t = snap_t; t.buf = work; t.size = snap_n;
        int colour = 0;
        for (int k = 0; k < nlog_n; k++) {
            if (nlog[k].kind == 5) { colour = nlog[k].c; continue; }
            if (nlog[k].kind == 4) { tp_native_draw_small_text(&t, M, nlog[k].a, nlog[k].b, nlog_text[k], nlog[k].c); continue; }
            if (nlog[k].kind == 2) {
                static long nb; nb++; pblits++;
                if (getenv("TP_NATIVE_BREAK") && nb % 50 == 0) continue;   /* (a self-test of the check) */
                tp_native_draw_sprite(&t, M + nlog[k].d, nlog[k].c & 0xff, nlog[k].c >> 8, nlog[k].a, nlog[k].b, nlog[k].e, colour);
                colour = 0;
            }
        }
        long diff = 0, first = -1;
        for (size_t k = 0; k < snap_n; k++) if (work[k] != snap1[k]) { if (first < 0) first = (long)k; diff++; }
        ppasses++; punsup += t.unsupported;
        if (diff) { pbad++; pdiff += diff; if (pbad < 6) fprintf(stderr, "native pixels: pass differs in %ld bytes, first at %ld,%ld\n", diff, first % t.pitch, first / t.pitch); }
        if (ppasses % 200 == 0 || ppasses == 10)
            fprintf(stderr, "native pixels: %ld fully native passes drawn (%ld blits), %ld differ (%ld bytes), %ld unsupported blits\n", ppasses, pblits, pbad, pdiff, punsup);
    }
    for (; gi < glog_n; gi++) if (glog[gi].kind == 1) { bad++; if (bad < 8) fprintf(stderr, "native things: game has an extra cell (thing %d)\n", glog[gi].a); }
    for (; ni < nlog_n; ni++) if (nlog[ni].kind == 1) { bad++; if (bad < 8) fprintf(stderr, "native things: native has an extra cell (thing %d)\n", nlog[ni].a); }
    if (getenv("TP_NATIVE_FLAGS")) {                      /* development: which blitter flags the things use */
        static long hist[0x10000]; static long hp;
        int first = 0; for (int k = 0; k < glog_n; k++) { if (glog[k].kind == 1) first = 1; if (first && glog[k].kind == 2) hist[glog[k].e & 0xffff]++; }
        if (++hp % 500 == 0) { fprintf(stderr, "flags:"); for (int k = 0; k < 0x10000; k++) if (hist[k]) fprintf(stderr, " %#x x%ld", k, hist[k]); fprintf(stderr, "\n"); }
    }
    static long passes; if (++passes % 100 == 0 || passes == 10) {
        fprintf(stderr, "native things: %ld passes, %ld cells, %ld compared, %ld differ; not native yet:", passes, cells, compared, bad);
        for (int k = 0; k < 0x400; k++) if (missing_cls[k]) fprintf(stderr, " %s%#x x%ld", k < 0x200 ? "class " : "", k, missing_cls[k]);
        fprintf(stderr, "\n");
    }
}
/* Native passes (stage 3b): the landscape (native_land.c), copied to the screen, the things
 * drawn over it (native_land.c's call list through native_draw.c), in the clip window the
 * game's pass would use. Anything not native yet makes the pass the game's again. */
typedef struct { NDTarget t; int colour, fail; } NPState;
static void np_draw(int kind, int a, int b, int c, uint32_t d, int e, void *ctx) {
    NPState *st = ctx;
    if (kind == 5) { st->colour = c; return; }
    if (kind == 2) { tp_native_draw_sprite(&st->t, M + d, c & 0xff, c >> 8, a, b, e, st->colour); st->colour = 0; return; }
    if (kind == 4) { if (tp_native_text) tp_native_draw_small_text(&st->t, M, a, b, tp_native_text, c); else st->fail = 1; return; }
    if (kind == 3 && a) st->fail = 1;
}
static uint8_t np_cache[640 * 720], np_out[640 * 720];
static int native_pass(int cx, int cy, int rows) {
    static long why[4]; static int dbg = -1; if (dbg < 0) dbg = getenv("TP_VIEW_DEBUG") != NULL;
    if (dbg) { static double l; if (now() - l > 1) { l = now(); fprintf(stderr, "native pass bails: clip %ld frame_mode %ld sel %ld (sel %d kind %#x)\n", why[0], why[1], why[2],
        (uint16_t)r16(0x1d6678), r16(0x1d6678) ? M[r32(0x1d3b10) + (uint32_t)(uint16_t)r16(0x1d6678) * 0xc8 + 0xc] : 0); } }
    if (!clipinfo.ok || clipinfo.mode != G.s) { why[0]++; return -1; }
    if (r16(0x1d667c)) { why[1]++; return -1; }                                           /* (FRAME_MODE: scaled drawing) */
    uint16_t sel = (uint16_t)r16(0x1d6678);
    if (sel) { int k = M[r32(0x1d3b10) + (uint32_t)sel * 0xc8 + 0xc]; if (k == 0x2c || k == 0x2e) { why[2]++; return -1; } }   /* (0x164940) */
    size_t n = (size_t)clipinfo.pitch * (size_t)SH, nt = (size_t)clipinfo.pitch * (size_t)PH;
    if (nt > sizeof np_cache) return -1;
    memcpy(np_cache, M + r32(LAND_CACHE), n);             /* (the landscape leaves a few pixels at the map's edge as they were) */
    memset(np_cache + n, 0, nt - n);
    int th = (uint16_t)r16(TILE_H); if (th <= 0) th = 32;
    int rows_t = (PH * (G.s == 1 ? 2 : 1) + th - 1) / th + 3; if (rows_t > rows) rows = rows_t;   /* (rows in 640-space units) */
    double tq0 = now();
    if (tp_native_landscape_part(M, cx, cy, rows, np_cache, (int)clipinfo.pitch, clipinfo.clip_w, PH, TX0, PT, TX1, PB) != 0) return -1;
    /* (only TX0..TX1 x PT..PB of a pass is used: the rest of its landscape is left out, ~3x faster) */
    double tq1 = now();
    memcpy(np_out, np_cache, nt);
    { extern int tp_native_stable_rng; static int mode = -1; if (mode < 0) mode = getenv("TP_NATIVE_PASS") ? atoi(getenv("TP_NATIVE_PASS")) : 1;
      tp_native_stable_rng = mode == 1 && !getenv("TP_NATIVE_THINGS"); }   /* (checks compare with the game: its sequence) */
    NPState st = {{np_out, nt, clipinfo.base, clipinfo.pitch, clipinfo.clip_w, PH, clipinfo.clip_r,
                   M[0x1de5f6] & 1, M + r32(0x1ba2f8), 0}, 0, 0};
    tp_native_things(M, cx, cy, rows, np_draw, &st);
    if (getenv("TP_VIEW_PROF")) { static double tl, tt, last; static long np; double tq2 = now(); tl += tq1 - tq0; tt += tq2 - tq1; np++;
        if (tq2 - last > 2) { last = tq2; fprintf(stderr, "view prof: %ld passes, landscape %.3f ms, things %.3f ms a pass\n", np, tl / np * 1e3, tt / np * 1e3); tl = tt = 0; np = 0; } }
    return st.fail || st.t.unsupported ? -1 : 0;
}
static const uint8_t *pass_src;
static int lazy_save, state_saved;                        /* render_visible: save the game's state on demand */
static void find_regions(void); static void state_save(void);                           /* where the last pass's picture is (the screen or np_out) */
#define HOVER 0x1de5acu
#define CLICKED 0x1de5a0u
static int pass(int cx, int cy) {
    static int native_mode = -1; if (native_mode < 0) native_mode = getenv("TP_NATIVE_PASS") ? atoi(getenv("TP_NATIVE_PASS")) : 1;
    int th0 = (uint16_t)r16(TILE_H); if (th0 <= 0) th0 = 32;
    int rows0 = ((G.s == 1 ? 400 : 480) + th0 - 1) / th0;
    int native_ok = native_mode >= 1 && native_pass(cx, cy, rows0) == 0;
    static long np_count, gp_count; if (native_ok) np_count++; else gp_count++;
    if (getenv("TP_VIEW_DEBUG") && (np_count + gp_count) % 500 == 0) fprintf(stderr, "view: %ld native passes, %ld game passes\n", np_count, gp_count);
    if (native_ok && native_mode == 1) { pass_src = np_out; return 0; }
    pass_src = M + r32(DISP_BUF);
    if (lazy_save && !state_saved) { find_regions(); state_save(); state_saved = 1; }
    int16_t ox = r16(CAM_X), oy = r16(CAM_Y);
    if (!clipinfo.ok || clipinfo.mode != G.s) {           /* learn the clip window from this game pass */
        capture_clip = 1; if (tp_hook_pcs[7] == 0xffffffffu) { tp_hook_pcs[7] = 0x11be5au; tp_hooks_changed(); }
    }
    static int thing_check = -1; if (thing_check < 0) thing_check = getenv("TP_NATIVE_THINGS") != NULL;
    uint32_t keep_hooks[8]; int logged = 0;
    if (thing_check) {                                    /* log the game's calls during this pass */
        memcpy(keep_hooks, tp_hook_pcs, sizeof keep_hooks);
        tp_hook_pcs[5] = 0x11c22au; tp_hook_pcs[6] = 0x11c010u; tp_hook_pcs[7] = 0x15ed21u;
        tp_hook_pcs[3] = 0x180e88u; tp_hook_pcs[4] = 0x180f34u; tp_hook_pcs[2] = 0x111bd5u;
        tp_hook_pcs[1] = 0x11be5au; tp_hook_pcs[0] = 0x11c01au; snapped = 0;
        tp_hooks_changed();                               /* (slots 2..4 are not reached inside render_park) */
        glog_n = 0; logging = 1; logged = 1;
    }
    static uint8_t before[MAX_SCREEN]; int checking = getenv("TP_NATIVE_CHECK") != NULL;
    if (checking) memcpy(before, M + r32(LAND_CACHE), (size_t)r32(DISP_PITCH) * SH);
    w16(CAM_X, (int16_t)cx); w16(CAM_Y, (int16_t)cy); w16(LAND_DIRTY, 1);
    /* The renderer also picks the thing under the mouse (0x120ff5, when [0x1de5ac] is set):
     * it records the hit (0x1d6678...) and on a click can open its window (0x135f21). In a
     * pass the camera is not the one the mouse is over, so a pass must not pick: off here. */
    int16_t hover = r16(HOVER), clicked = r16(CLICKED); w16(HOVER, 0); w16(CLICKED, 0);   /* (and its click tests, 0x1de5a0) */
    int th = (uint16_t)r16(TILE_H); if (th <= 0) th = 32;
    uint32_t args[3] = {(uint32_t)cx, (uint32_t)cy, (uint32_t)(((G.s == 1 ? 400 : 480) + th - 1) / th)};   /* rows, as 0x113b9a */
    if (logged) { nlog_n = 0; tp_native_things(M, cx, cy, (int)args[2], native_logger, NULL); }   /* on the state the game draws from */
    int64_t r = tp_host_guest_call(RENDER_PARK, args, 3, 50000000);
    if (checking && r >= 0) native_check(cx, cy, (int)args[2], before);
    if (logged) {
        logging = 0; memcpy(tp_hook_pcs, keep_hooks, sizeof keep_hooks); tp_hooks_changed();
        if (r >= 0) things_check(cx, cy, (int)args[2]);
    }
    w16(CAM_X, ox); w16(CAM_Y, oy); w16(LAND_DIRTY, 1); w16(HOVER, hover); w16(CLICKED, clicked);
    if (tp_hook_pcs[7] == 0x11be5au && !capture_clip) { tp_hook_pcs[7] = 0xffffffffu; tp_hooks_changed(); }
    if (native_ok && native_mode == 2 && r >= 0) {        /* check: the native picture against the game's */
        static long cp, cbad, cpx; long diff = 0; int fx = -1, fy = -1; const uint8_t *g = M + r32(DISP_BUF);
        for (int y = PT; y < RB; y++) for (int x = TX0; x < TX1; x++) {   /* (where the game's pass is complete: it lacks things of rows below its own) */
            if (x < 0 || x >= SW) continue;
            if (np_out[y * SW + x] != g[y * SW + x]) { if (!diff) { fx = x; fy = y; } diff++; }
        }
        cp++; if (diff) { cbad++; cpx += diff; if (cbad < 6) fprintf(stderr, "native pass: camera %d,%d differs in %ld pixels (first %d,%d)\n", cx, cy, diff, fx, fy);
            if (cbad < 3) for (int y = PT; y < RB; y++) for (int x = TX0; x < TX1; x++) if (x >= 0 && x < SW && np_out[y * SW + x] != g[y * SW + x])
                fprintf(stderr, "   %d,%d native %d (landscape %d) game %d, game cache %d\n", x, y, np_out[y * SW + x], np_cache[y * SW + x], g[y * SW + x], M[r32(LAND_CACHE) + y * SW + x]); }
        if (cp % 200 == 0 || cp == 10) fprintf(stderr, "native pass: %ld checked, %ld differ (%ld pixels); %ld native, %ld game passes\n", cp, cbad, cpx, np_count, gp_count);
    }
    return r < 0 ? -1 : 0;
}
static void blit_to_canvas(int cx, int cy, int x0, int x1, int y0, int y1) {
    const uint8_t *src = pass_src ? pass_src : M + r32(DISP_BUF);
    int bx = cam_ox(cx, cy), by = cam_oy(cy);
    for (int y = y0; y < y1; y++) {
        int cy2 = by + y; if (cy2 < 0 || cy2 >= CH) continue;
        int a = x0, b = x1;
        if (bx + a < 0) a = -bx;
        if (bx + b > CW) b = CW - bx;
        if (a < b) memcpy(canvas + (size_t)cy2 * CW + bx + a, src + y * SW + a, (size_t)(b - a));
    }
}

/* The real frame's complete part into the canvas, less the screen's furniture (the date, the
 * advisor buttons: interface, though the game draws them with the park at times). */
/* The game's own frame is complete only in a thin band (RT..RB: rides anchored above its top row are not
 * drawn, nor things of rows below its bottom), and its edges move with the camera, which follows the
 * pointer: rides crossing them popped in and out. The native passes draw everything the frame has
 * (people's thought bubbles too), so the park picture is passes only; the frame is still used for what
 * the game draws over the park (interface, tool previews, weather). TP_VIEW_REAL=1: the band as before. */
static void blit_real(int cx, int cy) {
    static int on = -1; if (on < 0) on = getenv("TP_VIEW_REAL") != NULL;
    if (!on) return;
    blit_to_canvas(cx, cy, TX0, TX1, RT, RB);          /* (the furniture is above RT) */
}

/* The grid of passes covering the canvas. */
typedef struct { int16_t cx, cy; int x0, y0, x1, y1; } Pass;
static Pass grid[4096]; static uint8_t drawn[4096];   /* (512 for a 64x64 park; a 128x128 one needs ~2000) */ static int ngrid, rr, rr_done;
static void make_grid(void) {
    ngrid = 0; memset(drawn, 0, sizeof drawn);
    for (int cy = CYMIN - (PT + UY - 1) / UY; ; cy += STEP_Y) {
        if (cam_oy(cy) + PT >= CH) break;
        for (int cx = (int)floor((double)(-UYX * cy - OX - TX0) / UX); ; cx += STEP_X) {
            int x0 = cam_ox(cx, cy) + TX0;
            if (x0 >= CW || ngrid == 4096) break;
            grid[ngrid++] = (Pass){(int16_t)cx, (int16_t)cy, x0, cam_oy(cy) + PT, x0 + (TX1 - TX0), cam_oy(cy) + PB};
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
    { extern uint32_t tp_get(unsigned); sp = tp_get(4); }
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
/* Development (TP_VIEW_EDGE): where the real frame differs from the passes under it. */
static int edge_fresh;                                    /* passes drew under the real frame this frame */
static int edge_check(void) { static int on = -1; if (on < 0) on = getenv("TP_VIEW_EDGE") != NULL; return on; }
static void edge_compare(int cx, int cy) {
    if (!edge_fresh) return; edge_fresh = 0;
    static long rows[480], cols[640], frames; int bx = cam_ox(cx, cy), by = cam_oy(cy);
    long nd = 0;
    for (int y = RT; y < RB && y < WORLD_H; y++) for (int x = TX0; x < TX1 && x < SW; x++) {   /* the trusted part */
        int X = bx + x, Y = by + y; if (X < 0 || Y < 0 || X >= CW || Y >= CH) continue;
        if (canvas[(size_t)Y * CW + X] != world_only[y * SW + x]) { rows[y]++; cols[x]++; nd++; }
    }
    static int dumps; static long total, bad;
    total++; if (nd > 400) bad++;
    if (total % 300 == 0) fprintf(stderr, "view edge: %ld frames, %ld with more than 400 differing pixels\n", total, bad);
    if (nd > 400 && dumps < 4 && getenv("TP_VIEW_EDGE")[0]) {     /* the real frame, the passes under it, the differences */
        char path[600]; snprintf(path, sizeof path, "%s-%d.png", getenv("TP_VIEW_EDGE"), dumps++);
        uint32_t pal[256]; tp_host_palette_rgba(pal); uint32_t *px = malloc(4 * SW * WORLD_H * 3);
        for (int y = 0; y < WORLD_H; y++) for (int x = 0; x < SW; x++) {
            int X = bx + x, Y = by + y; uint8_t c = X >= 0 && Y >= 0 && X < CW && Y < CH ? canvas[(size_t)Y * CW + X] : 0, r = world_only[y * SW + x];
            px[(y * 3) * SW + x] = pal[r]; px[(y * 3 + 1) * SW + x] = pal[c]; px[(y * 3 + 2) * SW + x] = c != r ? pal[r] : 0xff000000u;
        }
        /* (three rows per park row interleaved is awkward to view: write them stacked instead) */
        uint32_t *st = malloc(4 * SW * WORLD_H * 3);
        for (int k = 0; k < 3; k++) for (int y = 0; y < WORLD_H; y++) memcpy(st + ((size_t)k * WORLD_H + y) * SW, px + (size_t)(y * 3 + k) * SW, 4 * SW);
        tp_host_png_rgba(path, st, SW, WORLD_H * 3); free(px); free(st);
        fprintf(stderr, "view edge: %ld differ, dumped %s at camera %d,%d\n", nd, path, cx, cy);
    }
    if (++frames % 100 == 0) {
        fprintf(stderr, "view edge (%ld frames) rows:", frames); for (int y = 0; y < WORLD_H; y++) if (rows[y]) fprintf(stderr, " %d:%ld", y, rows[y]);
        fprintf(stderr, "\n  cols:"); for (int x = 0; x < SW; x++) if (cols[x]) fprintf(stderr, " %d:%ld", x, cols[x]); fprintf(stderr, "\n");
    }
}
/* Does a pass's rectangle meet the screen furniture's rectangles of the real view (cx, cy)? The real
 * frame leaves those to the passes (blit_real), and PRESENT tells the date from the park by them. */
static int under_furniture(const Pass *p, int cx, int cy) {
    int hr[4], bx = cam_ox(cx, cy), by = cam_oy(cy);
    for (int r = 0; hud_rect(r, hr); r++)
        if (p->x0 < bx + hr[2] && p->x1 > bx + hr[0] && p->y0 < by + hr[3] && p->y1 > by + hr[1]) return 1;
    return 0;
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
    lazy_save = !nosave; state_saved = 0;                    /* saved before the first game pass (native passes change nothing) */
    double t0 = now(), budget = all || edge_check() ? 1.0 : 0.012;
    int16_t rcx = r16(CAM_X), rcy = r16(CAM_Y);                 /* the real view's rectangle in the canvas */
    /* The real frame's complete part: like a pass's, objects of cells outside it are cut off near
     * its sides and bottom (an entrance's arch whose cell is below), so passes draw those parts. */
    int rx0 = cam_ox(rcx, rcy) + TX0, ry0 = cam_oy(rcy), rx1 = cam_ox(rcx, rcy) + TX1, ry1 = ry0 + TY1;
    int start = rr;
    for (int k = 0; k < ngrid; k++) {                    /* newly visible passes never drawn: now, whatever the budget */
        Pass *p = &grid[k];
        if (drawn[k] || p->x1 < vx - 8 || p->x0 > vx + vw + 8 || p->y1 < vy - 8 || p->y0 > vy + vh + 8) continue;
        if (pass(p->cx, p->cy) == 0) blit_to_canvas(p->cx, p->cy, TX0, TX1, PT, pass_src == np_out ? PB : (PB < WORLD_H ? PB : WORLD_H));
        pass_src = NULL; drawn[k] = 1;
    }
    rr_done = 0;
    for (int k = 0; k < ngrid; k++) {
        Pass *p = &grid[(start + k) % ngrid];
        if (p->x1 < vx - 8 || p->x0 > vx + vw + 8 || p->y1 < vy - 8 || p->y0 > vy + vh + 8) continue;
        (void)rx0; (void)rx1; (void)ry0; (void)ry1;      /* (passes cover the real view too: its frame is trusted only in a band) */
        if (pass(p->cx, p->cy) == 0) blit_to_canvas(p->cx, p->cy, TX0, TX1, PT, pass_src == np_out ? PB : (PB < WORLD_H ? PB : WORLD_H));
        pass_src = NULL; drawn[(start + k) % ngrid] = 1; rr_done++;   /* (other copies are of the real frame) */
        rr = (start + k + 1) % ngrid;
        (void)budget;   /* every visible pass, every frame: a pass left for a later frame shows that area from another
                         * moment (people and rides jump when it catches up: flashing chunks); the frame rate gives instead */
    }
    if (getenv("TP_VIEW_DEBUG")) { static double l; static long fr, partial, vis_total, done_total;
        int vis = 0, done = 0;
        for (int k = 0; k < ngrid; k++) { Pass *p = &grid[k]; if (p->x1 < vx - 8 || p->x0 > vx + vw + 8 || p->y1 < vy - 8 || p->y0 > vy + vh + 8) continue; vis++; }
        done = rr_done; fr++; vis_total += vis; done_total += done; if (done < vis) partial++;
        if (now() - l > 1) { l = now(); fprintf(stderr, "view: %ld frames, %ld partial (visible passes %.1f, refreshed %.1f a frame)\n", fr, partial, (double)vis_total / fr, (double)done_total / fr); fr = partial = vis_total = done_total = 0; } }
    tp_insns = insns; tp_rep = reps; edge_fresh = 1; st_pass += now() - t0;
    if (state_saved) state_restore();                         /* (the dirty flag is among the globals) */
    lazy_save = 0;
    memcpy(M + cache, cache_keep, cache_n);
    /* (the governor lets the game catch up the time the passes took: it runs while behind) */
    if (getenv("TP_VIEW_DEBUG")) {
        static double acc, last; static int calls, npass; acc += now() - t0; calls++;
        if (now() - last > 1) { fprintf(stderr, "view: %d frames, %.1f ms in passes this second, grid %d\n", calls, acc * 1e3, ngrid); acc = 0; calls = 0; last = now(); }
        (void)npass;
    }
    if (snap) { diff_report(snap); free(snap); }
}

/* Development (TP_VIEW_VERIFY=prefix): each visible pass's rectangle of the canvas against a fresh native
 * pass (same game state: this runs at PRESENT, before the game moves on). Moving people make small
 * differences; a piece drawn for the wrong place makes thousands. */
static void verify_canvas(void) {
    static long frames, badf; static int dumps; frames++;
    int vx0 = (int)vis_x, vy0 = (int)vis_y, vx1 = vx0 + (int)((vis_lw > 0 ? vis_lw : SW) / vis_z), vy1 = vy0 + (int)(SH / vis_z);
    int rcx = real_cx, rcy = real_cy, rx0 = cam_ox(rcx, rcy) + TX0, rx1 = cam_ox(rcx, rcy) + TX1, ry0 = cam_oy(rcy) + RT, ry1 = cam_oy(rcy) + RB;
    int worst = 0, wk = -1; long sum = 0; static long total_sum;
    for (int k = 0; k < ngrid; k++) {
        Pass *p = &grid[k];
        if (p->x1 < vx0 || p->x0 > vx1 || p->y1 < vy0 || p->y0 > vy1) continue;
        int th0 = (uint16_t)r16(TILE_H); if (th0 <= 0) th0 = 32;
        if (native_pass(p->cx, p->cy, ((G.s == 1 ? 400 : 480) + th0 - 1) / th0) != 0) continue;
        int bx = cam_ox(p->cx, p->cy), by = cam_oy(p->cy), n = 0;
        for (int y = PT; y < PB; y++) for (int x = TX0; x < TX1; x++) {
            int X = bx + x, Y = by + y; if (X < 0 || Y < 0 || X >= CW || Y >= CH) continue;
            if (Y >= ry0 && Y < ry1 && X >= rx0 && X < rx1) continue;   /* the real frame's (hover, tools) */
            n += canvas[(size_t)Y * CW + X] != np_out[y * SW + x];
        }
        if (n > worst) { worst = n; wk = k; }
        sum += n;
    }
    if (worst > 3000) badf++;
    total_sum += sum;
    if (frames % 300 == 0) fprintf(stderr, "view verify: %ld frames, %ld with a piece off by more than 3000 px; %.0f stale pixels a frame on average; %ld frames skipped (camera moved)\n",
                                   frames, badf, (double)total_sum / frames, skipped_moved);
    if (worst > 3000 && dumps < 6) {
        Pass *p = &grid[wk];
        int th0 = (uint16_t)r16(TILE_H); if (th0 <= 0) th0 = 32;
        native_pass(p->cx, p->cy, ((G.s == 1 ? 400 : 480) + th0 - 1) / th0);
        int h = PB - PT, bx = cam_ox(p->cx, p->cy), by = cam_oy(p->cy) + PT;
        uint32_t *px = malloc(4 * SW * h * 2);
        for (int y = 0; y < h; y++) for (int x = 0; x < SW; x++) {
            int X = bx + x, Y = by + y; uint8_t c = X >= 0 && Y >= 0 && X < CW && Y < CH ? canvas[(size_t)Y * CW + X] : 0;
            px[y * SW + x] = pub_pal[c]; px[(h + y) * SW + x] = pub_pal[np_out[(PT + y) * SW + x]];
        }
        char path[600]; snprintf(path, sizeof path, "%s-%d.png", getenv("TP_VIEW_VERIFY"), dumps++);
        tp_host_png_rgba(path, px, SW, h * 2); free(px);
        fprintf(stderr, "view verify: pass %d,%d off by %d px (dumped %s); real camera %d,%d, seen %d,%d, coherent %d\n",
                p->cx, p->cy, worst, path, rcx, rcy, seen_cx, seen_cy, coherent);
    }
}
/* Development (TP_VIEW_TRUST): where are passes complete? Native passes one camera step apart (same
 * state) must agree where both are trusted; disagreements by position in the pass say how wide the
 * untrusted margins really are. */
static void trust_check(void) {
    static uint8_t a[640 * 720]; static long hx[640], hy[720], cmp;
    int th0 = (uint16_t)r16(TILE_H); if (th0 <= 0) th0 = 32; int rows = ((G.s == 1 ? 400 : 480) + th0 - 1) / th0;
    static const int st[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (int cy = GAME_CY0; cy <= GAME_CY1; cy += 3) for (int cx = GAME_CX0; cx <= GAME_CX1; cx += 3) {
        if (native_pass(cx, cy, rows) != 0) continue;
        memcpy(a, np_out, (size_t)SW * PH);
        for (int k = 0; k < 4; k++) {
            int dx = st[k][0], dy = st[k][1];
            if (native_pass(cx + dx, cy + dy, rows) != 0) continue;
            int ox = UX * dx + UYX * dy, oy = UY * dy;      /* b's screen point (x, y) is a's (x + ox, y + oy) */
            int mid = 0;
            for (int y = 0; y < PH; y++) for (int x = 0; x < SW; x++) {
                int ax = x + ox, ay = y + oy; if (ax < 0 || ay < 0 || ax >= SW || ay >= PH) continue;
                cmp++;
                if (np_out[y * SW + x] != a[ay * SW + ax]) { hx[x]++; hy[y]++; hx[ax]++; hy[ay]++;
                    if (x >= TX0 && x < TX1 && y >= PT && y < PB && ax >= TX0 && ax < TX1 && ay >= PT && ay < PB) mid++; }
            }
            static int dumps; if (mid > 200 && dumps < 3 && getenv("TP_VIEW_TRUST_DUMP")) {
                uint32_t pal[256]; tp_host_palette_rgba(pal); uint32_t *px = malloc(4 * SW * PH * 3);
                for (int y = 0; y < PH; y++) for (int x = 0; x < SW; x++) {
                    int ax = x + ox, ay = y + oy, in = ax >= 0 && ay >= 0 && ax < SW && ay < PH;
                    uint8_t bv = np_out[y * SW + x], av = in ? a[ay * SW + ax] : 0;
                    px[y * SW + x] = pal[av]; px[(PH + y) * SW + x] = pal[bv];
                    px[(2 * PH + y) * SW + x] = in && av != bv && x >= TX0 && x < TX1 && y >= PT && y < PB && ax >= TX0 && ax < TX1 && ay >= PT && ay < PB ? 0xffff00ffu : (pal[bv] & 0xff3f3f3fu);
                }
                char path[600]; snprintf(path, sizeof path, "%s-%d.png", getenv("TP_VIEW_TRUST_DUMP"), dumps++);
                tp_host_png_rgba(path, px, SW, PH * 3); free(px);
                fprintf(stderr, "view trust: %d differ in the trusted parts, cameras %d,%d and %d,%d: %s\n", mid, cx, cy, cx + dx, cy + dy, path);
            }
        }
    }
    fprintf(stderr, "view trust: %ld pixels compared; disagreements by x:", cmp);
    for (int x = 0; x < SW; x += 4) { long n = hx[x] + hx[x + 1] + hx[x + 2] + hx[x + 3]; if (n) fprintf(stderr, " %d:%ld", x, n); }
    fprintf(stderr, "\n  by y:"); for (int y = 0; y < PH; y += 4) { long n = 0; for (int k = 0; k < 4 && y + k < PH; k++) n += hy[y + k]; if (n) fprintf(stderr, " %d:%ld", y, n); }
    fprintf(stderr, "\n");
}
/* Development (TP_VIEW_SEAMS): a thing drawn by two passes must look the same in both. For neighbouring
 * visible passes (same state, this frame), compare the overlap of their complete parts. */
static void seam_check(void) {
    static uint8_t a[640 * 720]; static long frames, bad, px;
    int th0 = (uint16_t)r16(TILE_H); if (th0 <= 0) th0 = 32; int rows = ((G.s == 1 ? 400 : 480) + th0 - 1) / th0;
    int cx = r16(CAM_X), cy = r16(CAM_Y), worst = 0;
    static const int st[3][2] = {{1, 0}, {0, 1}, {1, 1}};
    for (int k = 0; k < 3; k++) {
        if (native_pass(cx, cy, rows) != 0) return;
        memcpy(a, np_out, (size_t)SW * PH);
        int dx = st[k][0], dy = st[k][1];
        if (native_pass(cx + dx, cy + dy, rows) != 0) return;
        int ox = UX * dx + UYX * dy, oy = UY * dy, n = 0;
        for (int y = PT; y < PB; y++) for (int x = TX0; x < TX1; x++) {
            int ax = x + ox, ay = y + oy; if (ax < TX0 || ax >= TX1 || ay < PT || ay >= PB) continue;
            n += np_out[y * SW + x] != a[ay * SW + ax];
        }
        if (n > worst) worst = n;
    }
    frames++; px += worst; if (worst > 0) bad++;
    if (frames % 120 == 0) fprintf(stderr, "view seams: %ld frames, %ld with a difference, %.1f px a frame\n", frames, bad, (double)px / frames);
}
/* Interface, exactly: once the park is captured (AFTER_RENDER), its pixels in the back buffer are set to a
 * colour the interface never uses (SENTINEL); what the game draws over them is interface, whatever its
 * colours (a portrait's edges, text over a road). At PRESENT the park is put back for the screen. Park
 * drawn later (tool previews, weather) is drawn over the real park and then marked again. */
#define SENTINEL 0xcc      /* unused by the interface, and its row of the blend table ([0x1ba2f8], dst * 256 + src) gives
                            * only colours the interface never uses either: a translucent pixel drawn over it is known */
static int sentinel_on;
static void sentinel_apply(void) {
    /* (not under a top menu: its edge and shadow darken what is under them through another table, so over
     * the marker they come out magenta; the camera is held meanwhile and the frame difference is exact) */
    if (menu_open() || topbar) { sentinel_on = 0; return; }   /* (the top bar's dark edge too: a magenta line under it) */
    uint8_t *bb = M + r32(DISP_BUF);
    for (int i = 0; i < SW * WORLD_H; i++) if (bb[i] == world_only[i]) bb[i] = SENTINEL;
    sentinel_on = 1;
}
static void sentinel_lift(void) {                       /* the park back where nothing was drawn over it */
    if (!sentinel_on) return;
    uint8_t *bb = M + r32(DISP_BUF);
    for (int i = 0; i < SW * WORLD_H; i++) if (bb[i] == SENTINEL) bb[i] = world_only[i];
    sentinel_on = 0;
}
static void hooks(int on) {
    tp_hook_pcs[0] = FRAME_RENDER;
    tp_hook_pcs[1] = on ? AFTER_RENDER : 0xffffffffu;
    tp_hook_pcs[2] = on ? (G.s == 2 ? PRESENT_HI : PRESENT) : 0xffffffffu;
    tp_hook_pcs[3] = on ? AFTER_SKIP : 0xffffffffu;
    tp_hook_pcs[4] = on ? BEFORE_EXTRAS : 0xffffffffu;
    tp_hook_pcs[5] = on ? TOOL_PREVIEW : 0xffffffffu; tp_hook_pcs[6] = 0xffffffffu;
    tp_hook_pcs[8] = on ? WEATHER : 0xffffffffu; tp_hook_pcs[9] = 0xffffffffu;
    tp_hook_pcs[10] = on ? WEATHER2 : 0xffffffffu; tp_hook_pcs[11] = 0xffffffffu;
    if (getenv("TP_VIEW_PROBE")) {   /* development: the back buffer after more steps of the frame */
        uint32_t probe[3] = {0x13e149, 0x14e75e, 0x14e6c7}; const char *e = getenv("TP_VIEW_PROBE");   /* "a,b,c" (hex) or 1 */
        for (int k = 0; k < 3 && strchr(e, ','); k++) { probe[k] = (uint32_t)strtoul(e, (char **)&e, 16); if (*e == ',') e++; }
        for (int k = 0; k < 3; k++) tp_hook_pcs[5 + k] = on ? probe[k] : 0xffffffffu;
    }
    tp_hooks_changed();
}

static void hires_step(int sc);
static void poster_step(void);
static volatile int hires_wanted, poster_wanted;   /* (below) */
void tp_walk_frame(void); int tp_walk_active(void);   /* the Visitor View (walk.c) */
/* Called by the host when the game reaches a hooked address. */
double tp_dbg_lockwait, tp_dbg_passes, tp_dbg_pace;                  /* development (qol.c's tick gaps): where the game thread's time went */
void tp_view_hook(void) {
    M = tp_host_memory();
    { double t0 = now(); pthread_mutex_lock(&lock); tp_dbg_lockwait += now() - t0; }
    int w = want; double vx = vis_x, vy = vis_y, vw = (vis_lw > 0 ? vis_lw : SW) / vis_z, vh = SH / vis_z;
    pthread_mutex_unlock(&lock);
    static int dbg = -1; if (dbg < 0) dbg = getenv("TP_VIEW_DEBUG_HOOKS") ? 400 : 0;
    int sc = mode_scale();
    if (dbg > 0) { dbg--; fprintf(stderr, "view hook %d want %d active %d scale %d\n", tp_hook_which, w, active, sc); }
    if (!sc) { if (active) { active = 0; hooks(0); } return; }
    if (sc != G.s || geo_ext != tp_map_ext) {             /* the game changed its screen mode ([R]) or the park size: start again */
        G = geo(sc); ngrid = 0; memset(canvas, 0, (size_t)CW * CH);
        have_world = have_pre_extras = pre_ok = 0; in_frame = 0;
        pthread_mutex_lock(&lock); pub_time = 0; pub_cur_ok = 0; pthread_mutex_unlock(&lock);
        if (active) { active = 0; hooks(0); }
        return;
    }
    if (getenv("TP_VIEW_BARPROBE") && active) {          /* development: where the game draws its top bar */
        uint8_t *bb = M + r32(DISP_BUF); static int n;
        if (tp_hook_which == 0) memset(bb, 0xee, (size_t)SW * 6);
        else if (tp_hook_which == 4 || tp_hook_which == 1 || tp_hook_which == 2) { int k = 0; for (int i = 0; i < SW * 6; i++) k += bb[i] == 0xee;
            if (n++ < 60) fprintf(stderr, "barprobe hook %d: %d of %d sentinel left, row0 px %02x\n", tp_hook_which, k, SW * 6, bb[100]); }
    }
    switch (tp_hook_which) {
    case 0:                                               /* FRAME_RENDER */
        { void tp_bigmap_setup_done(void); tp_bigmap_setup_done(); }   /* (the park is on: its size is settled; bigmap.c) */
        { void tp_free_park_shown(void); tp_free_park_shown(); }       /* (and its Free Mode rules; freemode.c) */
        tp_walk_frame();                                  /* the Visitor View's snapshot (walk.c), when walking */
        { extern uint64_t tp_insns; static uint64_t li; int fresh = tp_insns - li >= 20000; li = tp_insns;   /* (not an interrupt returning here) */
          if (fresh) { st_game++; st_game_total++; if (pacing) { double p0 = now(); pace_frame(); tp_dbg_pace += now() - p0; } } }
        if (getenv("TP_VIEW_DEBUG")) { static double l; static long c0; if (now() - l > 1) { fprintf(stderr, "view: %ld game frames/s\n", st_game_total - c0); c0 = st_game_total; l = now(); } }
        hires_step(sc);
        { static int tc = -1; if (tc < 0) tc = getenv("TP_VIEW_TRUST") ? atoi(getenv("TP_VIEW_TRUST")) : 0;
          static int nf; if (tc && active && ++nf == tc) trust_check(); }
        { static int force = -2; if (force == -2) force = getenv("TP_DEV_WEATHER") ? atoi(getenv("TP_DEV_WEATHER")) : -1;
          if (force >= 0) M[WEATHER_FLAGS] = (uint8_t)force; }   /* development: 1 rain, 2 particles, 4 storm */
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
        if (topbar && active) {                           /* the pointer is on the top bar: undo the game's scrolling */
            if (r16(CAM_X) != topbar_cx || r16(CAM_Y) != topbar_cy) { w16(CAM_X, topbar_cx); w16(CAM_Y, topbar_cy); w16(LAND_DIRTY, 1); }
        }
        if (active || w) {                                /* the game moved its camera (a jump to an event, its keys) */
            int16_t cx = r16(CAM_X), cy = r16(CAM_Y);
            if (cx != seen_cx || cy != seen_cy) {
                if (getenv("TP_VIEW_DEBUG")) fprintf(stderr, "view: the game moved its camera %d,%d -> %d,%d%s\n", seen_cx, seen_cy, cx, cy, drag_held ? " (drag)" : "");
                seen_cx = cx; seen_cy = cy; if (!drag_held) camera_moves++;   /* (a drag's own scrolling: the view stays) */
            }
        }
        coherent = 0; weather_n = 0; fr_cx = r16(CAM_X); fr_cy = r16(CAM_Y);
        if (getenv("TP_VIEW_OLD")) coherent = 1;          /* development: the behaviour before (every frame captured) */
        if (w && !active) { active = 1; hooks(1); render_visible(vx, vy, vw, vh, 1); coherent = 1; }
        else if (!w) {                                    /* the view is off: no hooks at all until it is wanted */
            active = 0; hooks(0);
            int tp_free_waiting(void);
            if (!hires_wanted && !poster_wanted && !fps_game_hook && !pacing && !tp_walk_active() && !tp_free_waiting()) { tp_hook_pcs[0] = 0xffffffffu; tp_hooks_changed(); }
        }
        else if (active) {                                /* passes at most 60 times a second (the game draws faster) */
            static double last; double t = now();
            static int old = -1; if (old < 0) old = getenv("TP_VIEW_OLD") != NULL;
            if (pacing || t - last >= (old ? 1.0 / 30 : 1.0 / 60)) { last = t; render_visible(vx, vy, vw, vh, 0); coherent = 1; tp_dbg_passes += now() - t; }   /* (paced: every frame) */
        }
        in_frame = active; frame_park = active;
        break;
    case 1: case 3:                                       /* AFTER_RENDER (or AFTER_SKIP): the real frame's park */
        if (!in_frame) break;                             /* already taken (after the extras; or an interrupt returned here) */
        in_frame = 0;
        if (!active) break;
        real_cx = r16(CAM_X); real_cy = r16(CAM_Y);
        /* The game draws its frame 100+ times a second, the passes run at most 60: only a frame that had
         * passes goes into the picture, so everything in it is from one moment (else things moving
         * across the real frame's edges tear: walkers, the bus, thought bubbles). */
        if (!coherent) { have_pre_extras = 0; break; }
        /* The camera must be the one the frame (and its passes) was drawn with: the game's own scrolling
         * (its mouse handler at the screen's edge, keys) can move it while the frame is drawn. */
        if (real_cx != fr_cx || real_cy != fr_cy) {
            if (getenv("TP_VIEW_DEBUG")) { int mx, my; tp_host_mouse_pos(&mx, &my);
                fprintf(stderr, "view: camera moved while drawn %d,%d -> %d,%d (mouse %d,%d, topbar %d)\n", fr_cx, fr_cy, real_cx, real_cy, mx, my, topbar); }
            have_pre_extras = 0; skipped_moved++; break;
        }
        real_cx = r16(CAM_X); real_cy = r16(CAM_Y);
        if (tp_hook_which == 1 && have_pre_extras) {      /* the furniture's rectangles from before the extras */
            uint8_t *bb = M + r32(DISP_BUF);
            static uint8_t keep[MAX_SCREEN]; size_t n = (size_t)SW * WORLD_H; memcpy(keep, bb, n);
            int hr[4];
            for (int r = 0; hud_rect(r, hr); r++)
                for (int y = hr[1]; y < hr[3]; y++)
                    memcpy(bb + y * SW + hr[0], pre_extras + y * SW + hr[0], (size_t)(hr[2] - hr[0]));
            if (getenv("TP_VIEW_EXTRAS")) { int c = 0, x0 = SW, y0 = SH, x1 = -1, y1 = -1;
                for (int i = 0; i < SW * WORLD_H; i++) if (keep[i] != pre_extras[i]) { c++; int x = i % SW, y = i / SW; if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; }
                static int k; if (k++ % 60 == 0) fprintf(stderr, "extras drew %d px in %d,%d..%d,%d\n", c, x0, y0, x1, y1); }
            memcpy(world_only, bb, n); world_ok = 1; world_cx = real_cx; world_cy = real_cy;
            {   /* the date as the game drew it (a solid box): its rectangle and pixels, all interface at PRESENT */
                int hr0[4]; hud_rect(0, hr0); int x0 = SW, y0 = SH, x1 = -1, y1 = -1;
                for (int y = hr0[1]; y < hr0[3]; y++) for (int x = hr0[0]; x < hr0[2]; x++) if (keep[y * SW + x] != pre_extras[y * SW + x]) {
                    if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; }
                if (x1 - x0 >= 16 && y1 - y0 >= 6) {     /* (kept: the box does not move) */
                    date_box[0] = x0; date_box[1] = y0; date_box[2] = x1 + 1; date_box[3] = y1 + 1;
                    memset(date_cols, 0, sizeof date_cols);
                    for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) { int i = y * SW + x;
                        date_drawn[i] = keep[i] != pre_extras[i]; if (!date_drawn[i]) continue;
                        date_clean[i] = keep[i]; date_cols[keep[i]] = 1; }
                }
            }
            if (edge_check()) edge_compare(real_cx, real_cy);
            blit_real(real_cx, real_cy);
            memcpy(bb, keep, n);                          /* the game's frame goes on as it drew it */
            have_world = 1; have_pre_extras = 0;
            if (!getenv("TP_VIEW_NOSENTINEL")) sentinel_apply();
            break;
        }
        { static int dumped; if (getenv("TP_VIEW_DUMP") && ++dumped == 30) {   /* development: the back buffer at capture */
            uint32_t pal[256]; tp_host_palette_rgba(pal); uint32_t *px = malloc(4 * SW * SH);
            const uint8_t *bb = M + r32(DISP_BUF); for (int i = 0; i < SW * SH; i++) px[i] = pal[bb[i]];
            tp_host_png_rgba(getenv("TP_VIEW_DUMP"), px, SW, SH); free(px);
            fprintf(stderr, "view dump at hook %d\n", tp_hook_which); } }
        if (getenv("TP_VIEW_DEBUG")) { int mx, my; tp_host_mouse_pos(&mx, &my); fprintf(stderr, "view frame: camera %d,%d mouse %d,%d\n", real_cx, real_cy, mx / 2, my / 2); }
        /* A frame where the game skipped drawing the park (AFTER_SKIP): the back buffer is the last frame's picture,
         * interface and all, with this frame's interface drawn over it. The park in it is the last capture's. */
        if (!getenv("TP_NOSKIPKEEP") && tp_hook_which == 3 && world_ok && real_cx == world_cx && real_cy == world_cy) { if (getenv("TP_HOLE_DEBUG2")) { static int k; if (k++ < 5) fprintf(stderr, "skip frame: park kept\n"); } }
        else memcpy(world_only, M + r32(DISP_BUF), (size_t)SW * WORLD_H);
        world_ok = 1; world_cx = real_cx; world_cy = real_cy;
        if (edge_check()) edge_compare(real_cx, real_cy);
        blit_real(real_cx, real_cy);
        have_world = 1;
        if (!getenv("TP_VIEW_NOSENTINEL")) sentinel_apply();
        break;
    case 4:                                               /* BEFORE_EXTRAS */
        if (active && in_frame) { memcpy(pre_extras, M + r32(DISP_BUF), (size_t)SW * WORLD_H); have_pre_extras = 1; }
        break;
    case 5:                                               /* TOOL_PREVIEW: entered */
        if (tp_hook_pcs[5] != TOOL_PREVIEW) goto probe;
        if (active && have_world) {
            extern uint32_t tp_get(unsigned);
            memcpy(pre_tool, M + r32(DISP_BUF), (size_t)SW * WORLD_H);
            tp_hook_pcs[6] = r32(tp_get(4)); tp_hooks_changed();   /* its return address */
        }
        break;
    case 6:                                               /* ... returned: its pixels are park */
        if (tp_hook_pcs[5] != TOOL_PREVIEW) goto probe;
        tp_hook_pcs[6] = 0xffffffffu; tp_hooks_changed();
        if (active && have_world) {
            const uint8_t *bb = M + r32(DISP_BUF); int bx = cam_ox(real_cx, real_cy), by = cam_oy(real_cy);
            int n = 0, hr[4];
            for (int y = 0; y < WORLD_H; y++) for (int x = 0; x < SW; x++) {
                int i = y * SW + x; if (bb[i] == pre_tool[i]) continue;
                int furniture = 0;                        /* (the date is sometimes redrawn in there) */
                for (int r = 0; hud_rect(r, hr); r++) if (x >= hr[0] && x < hr[2] && y >= hr[1] && y < hr[3]) furniture = 1;
                if (furniture) continue;
                world_only[i] = bb[i]; n++;
                int X = bx + x, Y = by + y; if (X >= 0 && Y >= 0 && X < CW && Y < CH) canvas[(size_t)Y * CW + X] = bb[i];
                if (sentinel_on) M[r32(DISP_BUF) + i] = SENTINEL;   /* (park: marked again; the marker is not lifted around it) */
            }
            tool_shown = n > 0;
        }
        break;
    case 8: case 10:                                      /* WEATHER, WEATHER2: entered */
        if (tp_hook_pcs[tp_hook_which] != (tp_hook_which == 8 ? WEATHER : WEATHER2)) break;
        if (active) {
            extern uint32_t tp_get(unsigned);
            memcpy(pre_weather, M + r32(DISP_BUF), (size_t)SW * WORLD_H);
            tp_hook_pcs[tp_hook_which + 1] = r32(tp_get(4)); tp_hooks_changed();
        }
        break;
    case 9: case 11:                                      /* ... returned: rain pixels (not the lightning), or the particles */
        tp_hook_pcs[tp_hook_which] = 0xffffffffu; tp_hooks_changed();
        if (active) {
            const uint8_t *bb = M + r32(DISP_BUF); int n = weather_n, rain_only = tp_hook_which == 9;
            if (!n) { memset(weather, 0, (size_t)SW * WORLD_H); memset(weather_rep, 0, (size_t)SW * WORLD_H); }
            for (int i = 0; i < SW * WORLD_H; i++)
                if (bb[i] != pre_weather[i]) {        /* rain and particles repeat beside the column; lightning does not */
                    weather[i] = bb[i]; weather_rep[i] = !rain_only || (bb[i] & 0xf8) == RAIN_COLOUR; n++; world_only[i] = bb[i];
                    if (sentinel_on) M[r32(DISP_BUF) + i] = SENTINEL;
                }
            /* (into world_only too: the weather is not interface; the compositor lays its own layer over the view) */
            weather_n = n;
            if (getenv("TP_VIEW_DEBUG")) { static int k; if (k++ < 6) { int h[256] = {0}, tot = 0;
                for (int i = 0; i < SW * WORLD_H; i++) if (bb[i] != pre_weather[i]) { h[bb[i]]++; tot++; }
                fprintf(stderr, "weather %d: %d changed, kept %d:", tp_hook_which, tot, n); for (int c = 0; c < 256; c++) if (h[c] > 5) fprintf(stderr, " %#x x%d", c, h[c]); fprintf(stderr, "\n"); } }
        }
        break;
    case 7:
    probe:                                               /* development probes */
        { static int n[8]; if (getenv("TP_VIEW_DUMP") && ++n[tp_hook_which] == 30) {
            char path[512]; snprintf(path, sizeof path, "%s.%d.png", getenv("TP_VIEW_DUMP"), tp_hook_which);
            uint32_t pal[256]; tp_host_palette_rgba(pal); uint32_t *px = malloc(4 * SW * SH);
            const uint8_t *bb = M + r32(DISP_BUF); for (int i = 0; i < SW * SH; i++) px[i] = pal[bb[i]];
            tp_host_png_rgba(path, px, SW, SH); free(px); } }
        if (active && have_world) {
            const uint8_t *bb = M + r32(DISP_BUF); int d = 0;
            for (int i = 0; i < SW * WORLD_H; i++) d += bb[i] != world_only[i];
            static int shown; if (shown++ < 4000) fprintf(stderr, "view probe %d (%#x): %d park pixels changed\n", tp_hook_which, tp_hook_pcs[tp_hook_which], d);
        }
        break;
    case 2:                                               /* PRESENT: interface overlay, publish */
        if (!active) { sentinel_lift(); break; }
        /* Frames presented without the park (a full-screen menu or window): the view is not live once that has
         * lasted 150 ms (not 0.5 s), so the game's screen shows and clicks go straight to it. */
        /* (by time: the game also presents a few frames without the park in play, after a camera move) */
        { static double since; static int maxgap; double t = now();
          if (frame_park) { if (park_gap > maxgap && getenv("TP_VIEW_DEBUG")) { maxgap = park_gap; fprintf(stderr, "view: longest run of presents without the park %d\n", maxgap); }
                            park_gap = 0; since = t; }
          else if (park_gap++ >= 0 && t - since > 0.15 && pub_park) { pthread_mutex_lock(&lock); pub_park = 0; pthread_mutex_unlock(&lock);
              if (getenv("TP_VIEW_DEBUG")) fprintf(stderr, "view: no park for %.0f ms (%d presents): the game's screen\n", (t - since) * 1e3, park_gap); } }
        frame_park = 0;
        pthread_mutex_lock(&lock);
        const uint8_t *bb = M + r32(DISP_BUF);
        {   /* The game's cursor, read where the game keeps it (0x17d2dd draws it in the copy to the screen):
             * [0x1becb4] shown, [0x1de58c]/[0x1de58e] width and height, [0x1de590] the image (rows
             * [0x1de594] apart, colour 0xfe transparent), drawn with its corner at the mouse position. */
            int cw = (uint16_t)r16(0x1de58c), chh = (uint16_t)r16(0x1de58e); uint32_t data = r32(0x1de590), stride = r32(0x1de594);
            if (r32(0x1becb4) && cw > 0 && chh > 0 && cw <= CUR && chh <= CUR && stride >= (uint32_t)cw && stride <= 640 && data > 0x10000 && data + stride * chh < (64u << 20)) {
                static uint16_t nc[CUR * CUR];
                for (int i = 0; i < CUR * CUR; i++) nc[i] = 0xffff;
                for (int y = 0; y < chh; y++) for (int x = 0; x < cw; x++) { uint8_t v = M[data + (uint32_t)y * stride + x]; if (v != 0xfe) nc[y * CUR + x] = v; }
                if (memcmp(nc, pub_cursor, sizeof nc) || !pub_cur_ok) { memcpy(pub_cursor, nc, sizeof nc); pub_cur_serial++; }
                pub_cur_w = cw; pub_cur_h = chh; pub_cur_x = 0; pub_cur_y = 0; pub_cur_ok = 1;
            } else if (pub_cur_ok) { pub_cur_ok = 0; pub_cur_serial++; }
        }
        /* Publish only frames whose park picture is coherent: the passes and the real frame drawn
         * from the same game moment (have_world, set when this frame had passes). */
        if (!have_world) { sentinel_lift(); pthread_mutex_unlock(&lock); break; }
        have_world = 0;
        if (getenv("TP_VIEW_CACHEDIFF")) {                 /* development: what the landscape cache has beyond the ground */
            static uint8_t fresh[MAX_SCREEN + 65536]; uint32_t cache = r32(LAND_CACHE);
            memset(fresh, 0, (size_t)SW * SH);
            int th = (uint16_t)r16(TILE_H); if (th <= 0) th = 32; int rows = ((G.s == 1 ? 400 : 480) + th - 1) / th + 3;
            tp_native_landscape(M, real_cx, real_cy, rows, fresh, SW, (int)r32(0x1de5c8), WORLD_H);
            int n = 0, nb = 0, y1 = 0; for (int y = 0; y < WORLD_H; y++) for (int x = 0; x < SW; x++) if (M[cache + y * SW + x] != fresh[y * SW + x]) { n++; if (y >= 150) nb++; y1 = y; }
            static int k; if (k++ < 30) fprintf(stderr, "cachediff: %d px (%d in rows 150+), last row %d\n", n, nb, y1);
        }
        if (sentinel_on && getenv("TP_VIEW_BUBBLE")) {     /* development: marker pixels left inside the advisor's bubble */
            int n = 0, m = 0; for (int y = 152; y < 172 && y < WORLD_H; y++) for (int x = 60; x < 300; x++) { n += bb[y * SW + x] == SENTINEL; m += bb[y * SW + x] == 0x6c || bb[y * SW + x] == 0x6e; }
            static int k; if (k++ < 400 && (n || m)) fprintf(stderr, "bubble: %d marker px, %d blend px\n", n, m);
            static int dumped; if (n > 40 && dumped < 2) { uint32_t *px = malloc(4 * SW * 60 * 3);
                for (int y = 0; y < 60; y++) for (int x = 0; x < SW; x++) { int i = (130 + y) * SW + x;
                    px[y * SW + x] = bb[i] == SENTINEL ? 0xffff00ffu : pub_pal[bb[i]];
                    px[(60 + y) * SW + x] = pub_pal[world_only[i]];
                    px[(120 + y) * SW + x] = pub_pal[pre_extras[i]]; }
                char path[300]; snprintf(path, sizeof path, "%s-%d.png", getenv("TP_VIEW_BUBBLE"), dumped++); tp_host_png_rgba(path, px, SW, 180); free(px); } }
        if (sentinel_on) {
            /* translucent interface over the marker: redo its blend over the park (a source giving that result) */
            const uint8_t *gt = M + r32(0x1ba2f8); int16_t src_of[256]; for (int v = 0; v < 256; v++) src_of[v] = -1;
            if (r32(0x1ba2f8) && !getenv("TP_VIEW_NOREMAP")) for (int s2 = 255; s2 >= 0; s2--) src_of[gt[SENTINEL * 256 + s2]] = (int16_t)s2;
            if (getenv("TP_TEXT_DEBUG")) { static int k; int h[256] = {0}, hw[256] = {0}, c = 0;
                for (int y = 150; y < 170; y++) for (int x = 60; x < 310; x++) { int i = y * SW + x; if (src_of[bb[i]] >= 0) { c++; h[src_of[bb[i]]]++; hw[world_only[i]]++; } }
                if (c && k++ < 20) { fprintf(stderr, "text: %d blended px; src:", c); for (int v = 0; v < 256; v++) if (h[v]) fprintf(stderr, " %02x x%d", v, h[v]);
                    fprintf(stderr, "; under:"); for (int v = 0; v < 256; v++) if (hw[v] > 2) fprintf(stderr, " %02x x%d", v, hw[v]); fprintf(stderr, "\n"); } }
            for (int i = 0; i < SW * WORLD_H; i++) {
                uint8_t v = bb[i];
                if (v == SENTINEL) pub_overlay[i] = 0xffff;
                else if (src_of[v] >= 0 && r32(0x1ba2f8)) pub_overlay[i] = 0xfffe;     /* (blended: settled below) */
                else pub_overlay[i] = v;
            }
            /* The two colours a blend over the marker gives (0xfffe above) are also ordinary interface colours: a
             * game window is full of them. Each patch of them is settled by what it touches: the bubble's white
             * (the advisor's text: darkened over the white, as in the game), other interface (the game's own
             * colour: kept), or only park (a blend over the park). */
            {
                static uint8_t cls[MAX_SCREEN]; static int bq[MAX_SCREEN]; static uint16_t bbase[MAX_SCREEN];
                int qh = 0, qt = 0;
                for (int i = 0; i < SW * WORLD_H; i++) { cls[i] = 0; if (pub_overlay[i] != 0xfffe) continue;
                    int x = i % SW, y = i / SW, base = -1, best = -1;   /* the brightest plain interface pixel near it */
                    for (int dy = -2; dy <= 2; dy++) for (int dx = -2; dx <= 2; dx++) {
                        int xx = x + dx, yy = y + dy; if (xx < 0 || xx >= SW || yy < 0 || yy >= WORLD_H) continue;
                        int j = yy * SW + xx; if (pub_overlay[j] >= 0xfffe) continue;
                        uint32_t c = pub_pal[pub_overlay[j]]; int lum = (int)(c & 255) * 3 + (int)((c >> 8) & 255) * 6 + (int)((c >> 16) & 255);
                        if (lum > best) { best = lum; base = pub_overlay[j]; } }
                    if (base < 0) continue;
                    uint32_t c = pub_pal[base]; int white = (c & 255) >= 200 && ((c >> 8) & 255) >= 200 && ((c >> 16) & 255) >= 200;
                    cls[i] = white ? 1 : 2; bbase[i] = (uint16_t)base; bq[qt++] = i;
                }
                /* (a patch touching other interface and also the park is a shadow or outline darkening the park, as a
                 * menu's edge: blended over the park; one enclosed by interface is the window's own colour) */
                static int root[MAX_SCREEN]; static uint8_t park_touch[MAX_SCREEN];
                for (int q = 0; q < qt; q++) { root[bq[q]] = bq[q]; park_touch[bq[q]] = 0; }
                while (qh < qt) { int i = bq[qh++], x = i % SW, y = i / SW;          /* (through the patch) */
                    static const int ddx[4] = {-1, 1, 0, 0}, ddy[4] = {0, 0, -1, 1};
                    for (int k = 0; k < 4; k++) { int xx = x + ddx[k], yy = y + ddy[k]; if (xx < 0 || xx >= SW || yy < 0 || yy >= WORLD_H) continue;
                        int j = yy * SW + xx;
                        if (pub_overlay[j] == 0xffff) {      /* (park: a real area, not a hole in the interface filled later) */
                            int np = 0; for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
                                int x2 = xx + dx, y2 = yy + dy; if (x2 >= 0 && x2 < SW && y2 >= 0 && y2 < WORLD_H && pub_overlay[y2 * SW + x2] == 0xffff) np++; }
                            if (np >= 6) park_touch[root[i]] = 1;
                            continue; }
                        if (pub_overlay[j] != 0xfffe || cls[j]) continue;
                        cls[j] = cls[i]; bbase[j] = bbase[i]; root[j] = root[i]; bq[qt++] = j; } }
                /* (and a thin patch, a line one or two pixels across, is an edge: the window's own colours are areas) */
                static int bx0[MAX_SCREEN], bx1[MAX_SCREEN], by0[MAX_SCREEN], by1[MAX_SCREEN];
                for (int q = 0; q < qt; q++) { int r = root[bq[q]]; bx0[r] = by0[r] = 1 << 30; bx1[r] = by1[r] = -1; }
                for (int q = 0; q < qt; q++) { int i = bq[q], r = root[i], x = i % SW, y = i / SW;
                    if (x < bx0[r]) bx0[r] = x; if (x > bx1[r]) bx1[r] = x; if (y < by0[r]) by0[r] = y; if (y > by1[r]) by1[r] = y; }
                for (int q = 0; q < qt; q++) { int i = bq[q], r = root[i];
                    if (cls[i] == 2 && (park_touch[r] || bx1[r] - bx0[r] < 2 || by1[r] - by0[r] < 2)) cls[i] = 0; }
                for (int i = 0; i < SW * WORLD_H; i++) if (pub_overlay[i] == 0xfffe) {
                    if (cls[i] == 2) { pub_overlay[i] = bb[i]; continue; }
                    if (cls[i] == 0) { pub_overlay[i] = gt[world_only[i] * 256 + src_of[bb[i]]]; continue; }
                    int under = bbase[i], res = gt[under * 256 + src_of[bb[i]]], dark = 1 << 30;
                    for (int s2 = 0; s2 < 256; s2++) if (gt[SENTINEL * 256 + s2] == bb[i]) {   /* (several sources give it:
                                                                                                * the darkest result, the text is dark) */
                        uint32_t c = pub_pal[gt[under * 256 + s2]]; int lum = (int)(c & 255) * 3 + (int)((c >> 8) & 255) * 6 + (int)((c >> 16) & 255);
                        if (lum < dark) { dark = lum; res = gt[under * 256 + s2]; } }
                    pub_overlay[i] = (uint16_t)res;
                }
            }
            /* Marker left inside interface: pixels the game did not redraw this frame because they still held its
             * last frame's interface (the advisor's bubble is redrawn only where it changed). What the game had
             * there (world_only) is interface when it is not the park as the passes drew it and the pixel is
             * mostly surrounded by interface. Twice, for small clumps. */
            if (getenv("TP_HOLE_DEBUG2")) { static int k; int c = 0, ex[6][5], ne = 0;
                for (int y = 150; y < 168; y++) for (int x = 60; x < 300; x++) { int i = y * SW + x;
                    if (pub_overlay[i] == 0xffff && pub_overlay[i - 1] != 0xffff && pub_overlay[i + 1] != 0xffff) { c++;
                        if (ne < 6) { ex[ne][0] = x; ex[ne][1] = y; ex[ne][2] = bb[i]; ex[ne][3] = world_only[i]; ex[ne][4] = prev_overlay[i]; ne++; } } }
                if (c && k++ < 60) { fprintf(stderr, "holes2: %d:", c); for (int e = 0; e < ne; e++) fprintf(stderr, " (%d,%d s%02x w%02x p%04x)", ex[e][0], ex[e][1], ex[e][2], ex[e][3], ex[e][4]); fprintf(stderr, "\n"); } }
            if (getenv("TP_HOLE_DEBUG")) { static int k; int c = 0;
                for (int y = 150; y < 168; y++) for (int x = 60; x < 300; x++) { int i = y * SW + x;
                    if (pub_overlay[i] == 0xffff && prev_overlay[i] != 0xffff) { if (c++ < 4 && k < 40) fprintf(stderr, "hole %d,%d: screen %02x world %02x prev %02x canvas %02x\n", x, y, bb[i], world_only[i], prev_overlay[i],
                        canvas[(size_t)(cam_oy(real_cy) + y) * CW + cam_ox(real_cx, real_cy) + x]); } }
                if (c && k++ < 40) fprintf(stderr, "holes: %d\n", c); }
            /* (only inside interface the game drew this frame: one it has taken away, the advisor's face when he
             * stops talking, must not be kept alive by its own leftovers) */
            static uint8_t fresh_near[MAX_SCREEN];
            for (int i = 0; i < SW * WORLD_H; i++) fresh_near[i] = 0;
            for (int y = 0; y < WORLD_H; y++) for (int x = 0; x < SW; x++) if (pub_overlay[y * SW + x] != 0xffff)
                for (int dy = -2; dy <= 2; dy++) for (int dx = -2; dx <= 2; dx++) {
                    int yy = y + dy, xx = x + dx; if (yy >= 0 && yy < WORLD_H && xx >= 0 && xx < SW) fresh_near[yy * SW + xx] = 1; }
            for (int pass2 = 0; pass2 < 3; pass2++) {
                int bx = cam_ox(real_cx, real_cy), by = cam_oy(real_cy);
                for (int y = 1; y < WORLD_H - 1; y++) for (int x = 1; x < SW - 1; x++) {
                    int i = y * SW + x; if (pub_overlay[i] != 0xffff || !fresh_near[i]) continue;
                    int X = bx + x, Y = by + y; if (X < 0 || Y < 0 || X >= CW || Y >= CH) continue;
                    int nb = 0; for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) if ((dx || dy) && pub_overlay[i + dy * SW + dx] != 0xffff) nb++;
                    if (nb < 3) continue;
                    if (canvas[(size_t)Y * CW + X] != world_only[i]) pub_overlay[i] = world_only[i];
                    else if (nb >= 5 && prev_overlay[i] != 0xffff) pub_overlay[i] = prev_overlay[i];   /* (what it showed last frame) */
                }
            }
            if (getenv("TP_DATE_DEBUG")) { static int k; if (k++ % 30 == 0) { int nc = 0; for (int c = 0; c < 256; c++) nc += date_cols[c];
                fprintf(stderr, "date: box %d,%d..%d,%d, %d colours, fixed %ld\n", date_box[0], date_box[1], date_box[2], date_box[3], nc, date_fixed); } }
            {   /* the date's box, whole, from the screen (the game draws it every frame, sometimes with the park,
                 * sometimes after it: either way the box's pixels on the screen are the date) */
                const uint8_t *sb = M + r32(DISP_BUF);
                for (int y = date_box[1]; y < date_box[3]; y++) for (int x = date_box[0]; x < date_box[2]; x++) { int i = y * SW + x; if (!date_drawn[i]) continue; uint8_t v = sb[i] == SENTINEL ? world_only[i] : sb[i];   /* (marker: the date as drawn with the park) */
                      pub_overlay[i] = date_cols[v] ? v : date_clean[i]; if (!date_cols[v]) date_fixed++; }   /* (drawn with the park, things can cover it: those pixels from the clean date) */
            }
            memcpy(prev_overlay, pub_overlay, sizeof prev_overlay);
            sentinel_lift();
            { uint8_t *wb = M + r32(DISP_BUF); for (int i = 0; i < SW * WORLD_H; i++) if (src_of[wb[i]] >= 0 && r32(0x1ba2f8)) wb[i] = (uint8_t)pub_overlay[i]; }   /* (the screen too) */
        }
        else for (int i = 0; i < SW * WORLD_H; i++) pub_overlay[i] = bb[i] != world_only[i] ? bb[i] : 0xffff;
        if (getenv("TP_DATE_DEBUG")) { static int k; if (k++ % 60 == 0) fprintf(stderr, "present: sentinel %d have_world %d box %d..%d\n", sentinel_on, 1, date_box[0], date_box[2]); }
        if (getenv("TP_VIEW_HIST")) { static long h[256]; static int fr; for (int i = 0; i < SW * SH; i++) if (i >= SW * WORLD_H || pub_overlay[i] != 0xffff) h[bb[i]]++;
            if (++fr % 300 == 0) { fprintf(stderr, "view hist: unused:"); for (int c = 0; c < 256; c++) if (!h[c]) fprintf(stderr, " %02x", c); fprintf(stderr, "\n"); } }
        if (topbar) for (int i = 0; i < SW * TOPBAR_H; i++) pub_overlay[i] = bb[i];   /* the top bar is drawn with the park: all interface */
        if (menu_open()) {                                /* the open drop-down's rectangle (0x1b571a: x0, y0, x1, y1, mouse units) */
            uint32_t dd = 0x1b53c6u + (uint32_t)(int16_t)r16(0x1b14d4) * 0x354u;   /* (the open one: [0x1b14d4]; qol.c's EXTRAS too) */
            if ((int16_t)r16(0x1b14d4) < 0) dd = 0x1b571a;
            int k = G.s == 1 ? 2 : 1, x0 = r16(dd) / k, y0 = r16(dd + 2) / k, x1 = r16(dd + 4) / k + 1, y1 = r16(dd + 6) / k + 1;
            if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 > SW) x1 = SW; if (y1 > WORLD_H) y1 = WORLD_H;
            for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) pub_overlay[y * SW + x] = bb[y * SW + x];
        }
        if (menu_open()) {                                /* (and its submenus): what differs from the park as the passes drew it */
            int bx = cam_ox(real_cx, real_cy), by = cam_oy(real_cy);
            for (int y = 0; y < WORLD_H; y++) for (int x = 0; x < SW; x++) {
                int X = bx + x, Y = by + y, i = y * SW + x;
                if (pub_overlay[i] == 0xffff && X >= 0 && Y >= 0 && X < CW && Y < CH && bb[i] != canvas[(size_t)Y * CW + X]) pub_overlay[i] = bb[i];
            }
        }
        {   /* The date is usually drawn by the extras (so world_only lacks it: interface), but at times with the
             * park: then nothing differs there. Only in that case, the date's rectangle is what differs from
             * the passes' park (not the whole time: the game's frame lacks rides anchored above its top). */
            int hr[4], n = 0; hud_rect(0, hr);
            for (int y = hr[1]; y < hr[3]; y++) for (int x = hr[0]; x < hr[2]; x++) n += pub_overlay[y * SW + x] != 0xffff;
            if (n < 30) {
                int bx = cam_ox(real_cx, real_cy), by = cam_oy(real_cy);
                for (int y = hr[1]; y < hr[3]; y++) for (int x = hr[0]; x < hr[2]; x++) {
                    int X = bx + x, Y = by + y, i = y * SW + x;
                    if (X >= 0 && Y >= 0 && X < CW && Y < CH && bb[i] != canvas[(size_t)Y * CW + X]) pub_overlay[i] = bb[i];
                }
            }
        }
        /* (no gap filling: with the sentinel, interface pixels equal to the park under them are found as well) */
        if (getenv("TP_VIEW_OVERLAY")) { static int k, dumped; k++;
            int n = 0; for (int i = 16 * SW; i < SW * (WORLD_H - 50); i++) n += pub_overlay[i] != 0xffff;   /* (below the date, above the advisor) */
            if (n > atoi(getenv("TP_VIEW_OVERLAY_MIN") ?: "800") && dumped < 6 && k % 7 == 0) {       /* development: frames with much "interface" over the park */
                uint32_t *px = malloc(4 * SW * WORLD_H * 2);
                for (int i = 0; i < SW * WORLD_H; i++) { px[i] = pub_pal[bb[i]]; px[SW * WORLD_H + i] = pub_overlay[i] != 0xffff ? pub_pal[pub_overlay[i]] : 0xff202020u; }
                char path[600]; snprintf(path, sizeof path, "%s-%d.png", getenv("TP_VIEW_OVERLAY"), dumped++);
                tp_host_png_rgba(path, px, SW, WORLD_H * 2); free(px); fprintf(stderr, "view overlay: %d px, dumped %s\n", n, path);
            } }
        if (getenv("TP_VIEW_DEBUG")) { static double l; if (now() - l > 0.5) { l = now(); int n = 0, x0 = SW, y0 = SH, x1 = -1, y1 = -1;
            for (int i = 0; i < SW * WORLD_H; i++) if (pub_overlay[i] != 0xffff) { n++; int x = i % SW, y = i / SW; if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; }
            fprintf(stderr, "view overlay: %d px, %d,%d..%d,%d\n", n, x0, y0, x1, y1); } }
        memcpy(pub_canvas, canvas, (size_t)CW * CH); pub_s = G.s;
        memcpy(pub_panel + (size_t)SW * WORLD_H, bb + (size_t)SW * WORLD_H, (size_t)SW * (SH - WORLD_H));
        pub_weather_n = weather_n; if (weather_n) { memcpy(pub_weather, weather, (size_t)SW * WORLD_H); memcpy(pub_weather_rep, weather_rep, (size_t)SW * WORLD_H); }
        weather_n = 0;                                    /* (a frame without weather clears it) */
        tp_host_palette_rgba(pub_pal);
        pub_cx = real_cx; pub_cy = real_cy; pub_time = now(); pub_park = 1; st_pub++;
        {   int bt = SH - BAND_H - (SH == 480 ? 44 : 22), n = 0;
            for (int x = 0; x < SW; x++) n += pub_overlay[(bt - 1) * SW + x] != 0xffff;
            pub_straddle = n > SW / 8; }                  /* (a few stray pixels are not a window) */
        pub_frame_t = pacing ? tp_frame_paced_at : pub_time; pub_serial++;
        if (getenv("TP_VIEW_VERIFY")) verify_canvas();
        if (getenv("TP_VIEW_SEAMS")) seam_check();    /* development: the picture against fresh native passes */
        pthread_mutex_unlock(&lock);
        break;
    }
}

/* Panel size (Launcher: Display > Panel Size): the bottom of the interface, the panel and the advisor's
 * band above it (BAND_H screen rows), drawn k times larger, anchored at the bottom middle of the view;
 * pointer events in it are mapped back. */
void tp_view_set_ui_scale(double k) { ui_scale = k < 0.5 ? 0.5 : k > 3 ? 3 : k; }
/* A layout point in the enlarged band: its point in the screen (sx, sy). 0 if outside it. */
static int band_map(double lx, double ly, double lw, int sw, int sh, double k, double *sx, double *sy) {
    if (fabs(k - 1) <= 0.001) return 0;                  /* (smaller as well as larger: the rest of its place shows park) */
    if (pub_straddle) return 0;                           /* (a game window over the band: the panel at its own size) */
    double bh = BAND_H + (sh == 480 ? 44 : 22);           /* (the advisor's rows and the panel's) */
    double x0 = lw / 2 - sw * k / 2, y0 = sh - bh * k;
    if (lx < x0 || lx >= x0 + sw * k || ly < y0 || ly >= sh) return 0;
    *sx = (lx - x0) / k; *sy = (sh - bh) + (ly - y0) / k;
    return 1;
}
/* Pointer events, on the emulation thread: park points move the camera so the
 * point is inside the real view (unless it already is), then go to the game
 * at their place in that view; panel points go straight through. */
void tp_view_poll(void) {
    if (!evn && !restore) return;
    /* Not inside a frame (between the passes and the real frame's capture): a camera move there would
     * draw the real frame elsewhere than the passes left room for, leaving stale strips. */
    if (in_frame && !getenv("TP_VIEW_OLD")) return;
    M = tp_host_memory();
    Ev q[256]; int n;
    pthread_mutex_lock(&lock);
    if (restore) { w16(CAM_X, (int16_t)restore_cx); w16(CAM_Y, (int16_t)restore_cy); w16(LAND_DIRTY, 1); restore = 0; }
    n = evn; memcpy(q, evq, sizeof(Ev) * n); evn = 0;
    double vx = vis_x, vy = vis_y, z = vis_z, lw = vis_lw > 0 ? vis_lw : SW; int on = want && active && vis_s == G.s;
    pthread_mutex_unlock(&lock);
    double col = (lw - SW) / 2;                           /* the interface column's left edge in the layout */
    static uint32_t held;                                 /* buttons down after the last event */
    for (int i = 0; i < n; i++) {
        int dragging = (q[i].buttons & 3) && (held & 3), change = (q[i].buttons & 3) != (held & 3);
        held = q[i].buttons; drag_held = dragging;
        double lx = q[i].fx * lw, ly = q[i].fy * SH;      /* layout point */
        double cxs = lx - col;                            /* ... in the screen column */
        double ly_ui = ly, bsx, bsy, kk = ui_scale; if (kk * SW > lw) kk = lw / SW;
        int in_band = on && band_map(lx, ly, lw, SW, SH, kk, &bsx, &bsy);
        if (in_band) { cxs = bsx; ly_ui = bsy; }          /* (the enlarged panel: its point on the screen) */
        int incol = cxs >= 0 && cxs < SW, ui = incol && ly_ui >= WORLD_H;   /* the panel, or the game's interface: through */
        if (on && !in_band && fabs(kk - 1) > 0.001 && !pub_straddle && incol && ly >= SH - (BAND_H + (SH == 480 ? 44 : 22))) incol = 0;   /* (its original place shows park) */
        int menus = menu_open();
        if (on && incol && (ly < TOPBAR_OPEN || (topbar && ly < TOPBAR_H) || (menus && ly < WORLD_H))) {   /* the top bar and its menus: straight to the game, camera held */
            if (!topbar) { topbar = 1; topbar_cx = r16(CAM_X); topbar_cy = r16(CAM_Y); }
            ui = 1;
        } else if (topbar && !menus && !(q[i].buttons & 3)) topbar = 0;
        if (on && incol && !ui) {
            int ox = (int)cxs, oy = (int)ly_ui;
            pthread_mutex_lock(&lock);
            for (int dy = -2; dy <= 2 && !ui; dy++) for (int dx = -2; dx <= 2 && !ui; dx++) {
                int x = ox + dx, y = oy + dy;
                if (x >= 0 && x < SW && y >= 0 && y < WORLD_H && pub_overlay[y * SW + x] != 0xffff) ui = 1;
            }
            pthread_mutex_unlock(&lock);
        }
        if (!on || ui) {
            double fx = incol ? cxs / SW : (cxs < 0 ? 0 : 0.9999), fy = in_band ? ly_ui / SH : q[i].fy;
            /* The game scrolls itself while the pointer is on the screen's edge (the panel's bottom row,
             * say): with the view on, the app pans instead, so the game's pointer stays off the edges. */
            if (on) { fx = fmin(fmax(fx, 2.0 / SW), (SW - 2.0) / SW); fy = fmin(fy, (SH - 2.0) / SH); }
            tp_host_mouse(fx, fy, q[i].buttons);
            continue;
        }
        {
            double px = vx + lx / z, py = vy + ly / z;                    /* canvas point */
            int cx = r16(CAM_X), cy = r16(CAM_Y);
            double sx = px - cam_ox(cx, cy), sy = py - cam_oy(cy);
            /* A tool preview (a ride's ghost) is drawn upwards from the pointer and only inside the
             * game's own view: then the pointer is kept low and central in it, else anywhere inside. */
            int tool = tool_shown, mx = tool ? SW / 4 : 32, my0 = tool ? WORLD_H * 3 / 5 : 24;
            double ty = tool ? WORLD_H * 0.75 : WORLD_H / 2.0;
            /* Not while a button is held or as it goes down or up: the game follows a drag (a path, say)
             * in screen terms, and a camera move under it bends the drag. Past the view's edge the game
             * scrolls itself, as at 1x. */
            if (!dragging && !change && (sx < mx || sx > SW - mx || sy < my0 || sy > WORLD_H - 20)) {
                int ny = (int)lround((py - OY - ty) / UY);
                ny = ny < GAME_CY0 ? GAME_CY0 : ny > GAME_CY1 ? GAME_CY1 : ny;
                int nx = (int)lround((px - OX - SW / 2.0 - UYX * ny) / UX);
                nx = nx < GAME_CX0 ? GAME_CX0 : nx > GAME_CX1 ? GAME_CX1 : nx;
                if (nx != cx || ny != cy) { w16(CAM_X, (int16_t)nx); w16(CAM_Y, (int16_t)ny); w16(LAND_DIRTY, 1); seen_cx = (int16_t)nx; seen_cy = (int16_t)ny;
                    if (getenv("TP_VIEW_DEBUG")) fprintf(stderr, "view poll: camera %d,%d -> %d,%d (in_frame %d)\n", cx, cy, nx, ny, in_frame); }
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
    if (!main_game) { for (int h = 0; h < 12; h++) tp_hook_pcs[h] = 0xffffffffu; tp_hooks_changed(); }
    active = 0; have_world = 0; in_frame = 0;
    /* (a new game, also THEME.BAT's in-process restart: nothing of the last one's frame survives, issue #6) */
    sentinel_on = 0; world_ok = 0; coherent = 0; ngrid = 0; rr = rr_done = 0; seen_cx = seen_cy = -999;
    memset(date_box, 0, sizeof date_box); date_fixed = 0; memset(prev_overlay, 0, sizeof prev_overlay);
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
/* The map point (cells; x across, y down the map) under a point of the window's picture (fx, fy in
 * 0..1 of what the app shows: the zoomed view when it is live, else the game's own screen). A cell's
 * corner is at tws X + tws/3 Y + C0 on the canvas (walk.c has the derivation). 0 over the panel. */
#define VIEW_OFF 0x1d6624u
int tp_view_point_to_world(double fx, double fy, double *wx, double *wy) {
    M = tp_host_memory();
    int tw = (uint16_t)r16(0x1d663au), lo = M[SCREEN_MODE] & 1, off = r16(VIEW_OFF);
    if (tw < 6) return 0;
    pthread_mutex_lock(&lock);
    Geo g = geo(vis_s); double px, py;
    int live = want && pub_time && pub_park && pub_s == vis_s && now() - pub_time < 0.5;
    if (live) {
        double lw = vis_lw > g.sw ? vis_lw : g.sw;
        px = vis_x + fx * lw / vis_z; py = vis_y + fy * g.sh / vis_z;
    } else {
        if (fy * g.sh >= g.wh) { pthread_mutex_unlock(&lock); return 0; }
        int cx = r16(CAM_X), cy = r16(CAM_Y);
        px = g.ux * cx + g.uyx * cy + g.ox + fx * g.sw; py = g.uy * cy + g.oy + fy * g.sh;
    }
    pthread_mutex_unlock(&lock);
    double c0 = (0x280 - tw * ((0x280 - off) / tw)) >> lo;
    *wy = (py - g.oy) / g.uy;
    *wx = (px - g.ox - c0 - g.uyx * *wy) / g.ux;
    return *wx >= 0 && *wy >= 0 && *wx < (int)tp_map_ext && *wy < (int)tp_map_ext;
}
void tp_view_canvas_size(double *w, double *h) { Geo g = geo(vis_s); *w = g.cw; *h = g.ch; }
/* The game's cursor as RGBA (CUR x CUR, w x h used), its corner's offset from the pointer in screen
 * pixels; returns a number that changes when it does (0: no cursor). */
int tp_view_cursor_rgba(uint32_t *out, int *w, int *h, int *ox, int *oy) {
    pthread_mutex_lock(&lock);
    int serial = pub_cur_ok ? pub_cur_serial * 2 + 1 : 0;
    if (pub_cur_ok) {
        for (int i = 0; i < CUR * CUR; i++) out[i] = pub_cursor[i] == 0xffff ? 0 : pub_pal[pub_cursor[i]];
        *w = pub_cur_w; *h = pub_cur_h; *ox = pub_cur_x; *oy = pub_cur_y;
    }
    pthread_mutex_unlock(&lock);
    return serial;
}
/* Is the zoomed view being drawn (the game is showing its park, at the app's scale)? */
int tp_view_live(void) {
    pthread_mutex_lock(&lock); int live = want && pub_time && pub_park && pub_s == vis_s && now() - pub_time < 0.5;
    if (getenv("TP_VIEW_DEBUG")) { static int last = -1; if (live != last) { fprintf(stderr, "view live %d: want %d published %d park %d scale %d/%d age %.3f\n",
        live, want, pub_time != 0, pub_park, pub_s, vis_s, pub_time ? now() - pub_time : -1); last = live; } }
    pthread_mutex_unlock(&lock);
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
/* A 3x5 font for the debug overlays: digits, a few letters and signs. */
static const char *font_chars = "0123456789ACDEFGKMNPRSTWXY/:,.- ";
static const uint16_t font_bits[] = {0x7b6f, 0x2c97, 0x73e7, 0x73cf, 0x5bc9, 0x79cf, 0x79ef, 0x7292, 0x7bef, 0x7bcf, 0x2bed, 0x7927, 0x6b6e, 0x79a7, 0x79a4, 0x796f, 0x5bad, 0x5fed, 0x7b6d, 0x7be4, 0x6bad, 0x79cf, 0x7492, 0x5bfd, 0x5aad, 0x5a92, 0x12a4, 0x0410, 0x0014, 0x0002, 0x01c0, 0x0000};
int tp_view_out_bgra;                                   /* compose BGRA (an IOSurface for the screen) rather than RGBA */
static size_t dbg_stride;                               /* the output's row stride for the overlays and cursor */
static uint32_t dbg_col(uint32_t c) { return tp_view_out_bgra ? (c & 0xff00ff00u) | ((c & 0xff) << 16) | ((c >> 16) & 0xff) : c; }
static void put_text(uint32_t *out, int ow, int oh, int x, int y, int k, const char *t, uint32_t col) {
    for (; *t; t++, x += 4 * k) {
        const char *f = strchr(font_chars, *t); if (!f) continue;
        uint16_t b = font_bits[f - font_chars];
        for (int r = 0; r < 5; r++) for (int c = 0; c < 3; c++) if (b & (1 << (14 - r * 3 - c)))
            for (int yy = 0; yy < k; yy++) for (int xx = 0; xx < k; xx++) {
                int X = x + c * k + xx, Y = y + r * k + yy; if (X >= 0 && Y >= 0 && X < ow && Y < oh) out[(size_t)Y * dbg_stride + X] = dbg_col(col);
            }
    }
}
static void put_box(uint32_t *out, int ow, int oh, int x0, int y0, int x1, int y1, uint32_t col) {
    for (int y = y0 < 0 ? 0 : y0; y < y1 && y < oh; y++) for (int x = x0 < 0 ? 0 : x0; x < x1 && x < ow; x++) out[(size_t)y * dbg_stride + x] = dbg_col(col);
}
static void debug_fps(uint32_t *out, int ow, int oh, int k, int live) {
    char lab[96];
    if (live) snprintf(lab, sizeof lab, "SCREEN %.0f  PARK %.0f  GAME %.0f FPS  DRAW %.1f MS/S", sh_comp, sh_pub, sh_game, sh_pass);
    else snprintf(lab, sizeof lab, "SCREEN %.0f  GAME %.0f FPS", sh_comp, sh_game);
    put_box(out, ow, oh, ow - 8 - 4 * k * (int)strlen(lab), 2, ow - 2, 4 + 6 * k, 0xc0000000u);
    put_text(out, ow, oh, ow - 6 - 4 * k * (int)strlen(lab), 4, k, lab, 0xff40ff40u);
}
/* The frame-rate line over the game's own picture (the park view off: 1x in a 4:3 window). */
void tp_view_debug_hud(uint32_t *out, int ow, int oh) {
    dbg_stride = (size_t)ow;
    st_comp++; stats_tick();
    if (tp_view_debug_fps) debug_fps(out, ow, oh, oh >= 700 ? 3 : 2, 0);
}
void tp_view_count_game_frames(int on) { fps_game_hook = on; if (on) { tp_hook_pcs[0] = FRAME_RENDER; tp_hooks_changed(); } }
typedef struct { uint32_t *out; int ow, oh, smooth; Geo g; double z, vx, vy, lw, col, lpx, lpy, sx, sy; int ss; const uint32_t *pal; size_t stride; double k; } ComposeJob;
static size_t compose_stride;                           /* pixels per output row (0: the width) */
void tp_view_compose_stride(size_t px) { compose_stride = px; }
static void compose_band(void *ctx, size_t band) {
    const ComposeJob *j = ctx;
    uint32_t *out = j->out; int ow = j->ow, oh = j->oh, smooth = j->smooth, ss = j->ss; Geo g = j->g;
    double z = j->z, vx = j->vx, vy = j->vy, col = j->col, lpx = j->lpx, lpy = j->lpy, sx = j->sx, sy = j->sy;
    const uint32_t *pal = j->pal; size_t stride = j->stride;
    int y0 = (int)band * 32, y1 = y0 + 32 < oh ? y0 + 32 : oh;
    for (int y = y0; y < y1; y++) {
        uint32_t *o = out + (size_t)y * stride;
        double ly = (y + 0.5) * lpy, py = vy + ly / z;
        int iy = (int)ly, panel_row = iy >= g.wh;
        for (int x = 0; x < ow; x++) {
            double lx = (x + 0.5) * lpx; int ix = (int)floor(lx - col);
            double bsx, bsy;
            if (band_map(lx, ly, j->lw, g.sw, g.sh, j->k, &bsx, &bsy)) {   /* the enlarged panel and advisor */
                int bx = (int)bsx, by = (int)bsy;
                if (bx >= 0 && bx < g.sw && by >= 0 && by < g.sh) {
                    if (by >= g.wh) { o[x] = pal[pub_panel[(size_t)by * g.sw + bx]]; continue; }
                    uint16_t u = pub_overlay[by * g.sw + bx];
                    if (u != 0xffff) { o[x] = pal[u]; continue; }
                }
                if (iy >= g.wh && j->k > 1) { o[x] = 0xff000000u; continue; }   /* (the panel's own rows beside it; a smaller one: park) */
                goto park;
            }
            if (j->k < 0.999 && !pub_straddle && ix >= 0 && ix < g.sw && iy >= g.sh - BAND_H - (g.sh == 480 ? 44 : 22)) goto park;   /* (a smaller panel: the rest of its place) */
            if (ix >= 0 && ix < g.sw) {                                /* the screen column: panel and interface */
                if (panel_row) { o[x] = pal[pub_panel[(size_t)iy * g.sw + ix]]; continue; }   /* (frame, fw, fh: unused now) */
                uint16_t u = pub_overlay[iy * g.sw + ix];
                if (u != 0xffff) { o[x] = pal[u]; continue; }
            }
            if (fabs(j->k - 1) > 0.001 && !pub_straddle && ix >= 0 && ix < g.sw && iy >= g.sh - BAND_H - (g.sh == 480 ? 44 : 22)) goto park;   /* (its original place: park) */
          park:
            /* The weather the game drew (screen space): over the whole view, repeated beside the 4:3 column. */
            if (pub_weather_n) {                                       /* (the panel and interface came first) */
                int wx = ((ix % g.sw) + g.sw) % g.sw, wy = iy % g.wh, in_col = ix >= 0 && ix < g.sw && iy < g.wh, k = wy * g.sw + wx;
                if (pub_weather[k] && (in_col || pub_weather_rep[k])) { o[x] = pal[pub_weather[k]]; continue; }
            }
            double px = vx + lx / z;
            if (ss == 1 && !smooth) {
                int cx = (int)floor(px), cy = (int)floor(py);
                o[x] = cx >= 0 && cy >= 0 && cx < g.cw && cy < g.ch ? pal[pub_canvas[(size_t)cy * g.cw + cx]] : 0xff000000u;
                continue;
            }
            /* 2x2 samples: an average when minifying, bilinear-ish when smooth */
            uint32_t r = 0, gg = 0, b = 0;
            for (int k = 0; k < 4; k++) {
                double qx = ss == 2 ? px + ((k & 1) - 0.5) * sx * 0.5 : px - 0.5 + (k & 1);
                double qy = ss == 2 ? py + ((k >> 1) - 0.5) * sy * 0.5 : py - 0.5 + (k >> 1);
                int cx = (int)floor(qx), cy = (int)floor(qy);
                uint32_t c = cx >= 0 && cy >= 0 && cx < g.cw && cy < g.ch ? pal[pub_canvas[(size_t)cy * g.cw + cx]] : 0xff000000u;
                r += c & 255; gg += (c >> 8) & 255; b += (c >> 16) & 255;
            }
            o[x] = 0xff000000u | ((b / 4) << 16) | ((gg / 4) << 8) | (r / 4);
        }
    }
}
void tp_view_compose(uint32_t *out, int ow, int oh, const uint32_t *frame, int fw, int fh, int smooth) {
    pthread_mutex_lock(&lock);
    Geo g = geo(pub_s);
    double z = vis_z, vx = vis_x, vy = vis_y, lw = vis_lw > g.sw ? vis_lw : g.sw, col = (lw - g.sw) / 2;
    double lpx = lw / ow, lpy = (double)g.sh / oh;                     /* layout pixels per output pixel */
    double sx = lpx / z, sy = lpy / z;                                 /* canvas pixels per output pixel */
    int ss = sx > 1.4 ? 2 : 1;                                         /* minifying: average 2x2 */
    static uint32_t pal_bgra[256]; const uint32_t *pal = pub_pal;
    if (tp_view_out_bgra) { for (int i = 0; i < 256; i++) { uint32_t c = pub_pal[i]; pal_bgra[i] = (c & 0xff00ff00u) | ((c & 0xff) << 16) | ((c >> 16) & 0xff); } pal = pal_bgra; }
    size_t stride = compose_stride ? compose_stride : (size_t)ow;
    double kk = ui_scale; if (kk * g.sw > lw) kk = lw / g.sw;   /* (no wider than the view) */
    ComposeJob job = {out, ow, oh, smooth, g, z, vx, vy, lw, col, lpx, lpy, sx, sy, ss, pal, stride, kk};
    dbg_stride = stride;
    dispatch_apply_f((size_t)((oh + 31) / 32), DISPATCH_APPLY_AUTO, &job, compose_band);   /* bands of 32 rows across the cores */
    st_comp++; stats_tick();
    if (getenv("TP_VIEW_FPS")) tp_view_debug_fps = 1;
    if (getenv("TP_VIEW_GRID")) tp_view_debug_grid = 1;
    if (tp_view_debug_grid || tp_view_debug_fps) {
        int k = oh >= 700 ? 3 : 2;                                     /* font scale */
        if (tp_view_debug_grid) {                                      /* every pass's complete part: the sections the park is drawn in */
            for (int i = 0; i < ngrid; i++) {
                Pass *p = &grid[i];
                double x0 = (p->x0 - vx) * z / lpx, x1 = (p->x1 - vx) * z / lpx, y0 = (p->y0 - vy) * z / lpy, y1 = (p->y1 - vy) * z / lpy;
                if (x1 < 0 || y1 < 0 || x0 >= ow || y0 >= g.wh / lpy) continue;
                uint32_t col = 0xffff40ffu;
                put_box(out, ow, oh, (int)x0, (int)y0, (int)x1, (int)y0 + 1, col); put_box(out, ow, oh, (int)x0, (int)y0, (int)x0 + 1, (int)y1, col);
                char lab[24]; snprintf(lab, sizeof lab, "%d,%d", p->cx, p->cy);
                put_box(out, ow, oh, (int)x0 + 2, (int)y0 + 2, (int)x0 + 4 + 4 * k * (int)strlen(lab), (int)y0 + 4 + 5 * k, 0xc0000000u);
                put_text(out, ow, oh, (int)x0 + 3, (int)y0 + 3, k, lab, 0xff80ffffu);
            }
            double plx = ptr_fx * lw, ply = ptr_fy * g.sh, pcx = vx + plx / z, pcy = vy + ply / z;   /* the pointer: canvas and camera */
            int ccy = (int)floor((pcy - g.oy - g.wh / 2.0) / g.uy), ccx = (int)floor((pcx - g.ox - g.sw / 2.0 - g.uyx * ccy) / g.ux);
            char lab[64]; snprintf(lab, sizeof lab, "XY %.0f,%.0f  CAM %d,%d", pcx, pcy, ccx, ccy);
            int y = tp_view_debug_fps ? 8 + 7 * k : 4;
            put_box(out, ow, oh, ow - 8 - 4 * k * (int)strlen(lab), y - 2, ow - 2, y + 6 * k, 0xc0000000u);
            put_text(out, ow, oh, ow - 6 - 4 * k * (int)strlen(lab), y, k, lab, 0xff40ffffu);
        }
        if (tp_view_debug_fps) debug_fps(out, ow, oh, k, 1);
    }
    /* The game's cursor at the pointer, at the screen's scale (over the panel the frame has it already). */
    double plx = ptr_fx * lw, ply = ptr_fy * g.sh;
    int over_panel = ply >= g.wh && plx - col >= 0 && plx - col < g.sw;
    (void)over_panel;
    if (pub_cur_ok && ptr_fy >= 0 && !tp_view_hw_cursor) {
        double kx = 1 / lpx, ky = 1 / lpy;
        int hx = (int)(ptr_fx * ow), hy = (int)(ptr_fy * oh);
        for (int y = 0; y < CUR; y++) for (int x = 0; x < CUR; x++) {
            uint16_t u = pub_cursor[y * CUR + x]; if (u == 0xffff) continue;
            int bx = hx + (int)((pub_cur_x + x) * kx), by = hy + (int)((pub_cur_y + y) * ky);
            for (int yy = by; yy < by + (int)ceil(ky); yy++) for (int xx = bx; xx < bx + (int)ceil(kx); xx++)
                if (xx >= 0 && yy >= 0 && xx < ow && yy < oh) out[(size_t)yy * dbg_stride + xx] = dbg_col(pub_pal[u]);
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
