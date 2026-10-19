/* Quality-of-life options and free-mode levers (docs/PLAN.md, "Quality of
 * life"): host-side hooks at game functions, in hook slots 12..23 (view.c has
 * 0..11). Settings come from the environment, which the app fills from its
 * preferences before the game starts (tp_qol_init):
 *   TP_EW_GAP=s         the grossed-out sound ("ew", SICK.RAW): 0 as the game
 *                       plays it (default), s > 0 at most once per s seconds,
 *                       -1 never
 *   TP_RIDE_MUSIC=0     no ride music (the game's jingles still play)
 *   TP_RIDE_TUNES=t:n,..  ride type t (0..28, the order of the type table at
 *                       0x1cced0) plays tune n (1..22) instead, or 0: none
 *   TP_PARK_TUNE=n      a tune (1..22) where no ride's music plays
 *   TP_FREE_VISITORS=n  free mode (sandbox parks, sim level [0x1d66f1] = 0):
 *                       at most n visitors in the park (the game's 800; the
 *                       things pool, 1200 records shared with rides, staff
 *                       and cars, takes visitors to about 1,100, so at most
 *                       1000 in a 64x64 park). A 128x128 park has 12,000
 *                       records (bigmap.c): up to n (10,000 when n is 0), a
 *                       bus there bringing four times as many (TP_FREE_ARRIVALS)
 *   TP_FREE_BUS=n       free mode: a bus brings up to n visitors (1..255; the
 *                       game's 20..150 by park level, table at 0x1b78cc)
 *   TP_SOUND_LOG=1      development: every sound effect the game asks for
 *
 * The visitor cap is the immediate at 0x167ff6 in 0x167cd4 (how many leave an
 * arriving bus: none if the park would pass 800); tools/build_game.py makes it
 * the variable tp_tune_max_people. Both levers are set as a bus arrives.
 *
 * Sound effects (SNDS0/SNDS1 banks: 32-byte TAB entries, name and offset) are
 * started by these, cdecl (bank, sample, ...), returning nothing callers use:
 *   0x180fe0  one-shot effect (46 callers: the interface, building, events)
 *   0x180c80  effect in the park (71 callers: people, rides); the only
 *             grossed-out call is at 0x122fc5 (bank 0, sample 13, SICK.RAW),
 *             when a visitor reacts to a mess in its cell (0x122e22)
 *   0x180dc0, 0x1805a4, 0x181ad0  looping and channel variants
 * A call is skipped by returning to its caller at entry.
 *
 * Music (the MUSIC0 bank: tunes 1..17 RIDE1..17.HMP, 18 HRTBEAT, 19 FANFARE,
 * 20 TOVER, 21 WIN2, 22 FUNERAL, through HMI's MIDI driver):
 *   0x180a20  play_tune(n) (cdecl; nothing unless the driver [0x1ba714] and
 *             music [0x1ba715] are on, n <= [0x1ba71d] and n != the current
 *             tune, word [0x1ba71a])
 *   0x180990  stop_music(), current tune 0
 * The park renderer (0x11b837) picks the music ride as it draws: the first
 * running ride with a tune ([0x1d3ae8] = the thing, [0x1d6632] = 1), or the
 * thing under the pointer. At its end (block 0x11c166) it plays that ride's
 * type tune (byte +0xb7 of the type record; play_tune returns to 0x11c1c7),
 * or stops the music (returning to 0x11c183 / 0x11c193) when [0x1d6632] = -1.
 * Game passes of the zoomed view (view.c) run the renderer with other
 * cameras: their sound and music calls are dropped here (nested). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

uint32_t tp_get(unsigned i);
void tp_set(unsigned i, uint32_t v);
uint8_t *tp_host_memory(void);
int64_t tp_host_guest_call(uint32_t fn, const uint32_t *args, int nargs, uint32_t max_blocks);
extern uint32_t tp_hook_pcs[32];
extern int tp_hook_which;
void tp_hooks_changed(void);

#define QOL0 12
enum { H_SFX, H_SFX_PARK, H_SFX3, H_SFX4, H_SFX5, H_PLAY_TUNE, H_STOP_MUSIC, H_MUSIC_END, H_BUS_ARRIVES, H_NEW_PARK, H_TICK, H_BUS_COUNT, H_OPTIONS, NHOOK };
static const uint32_t hook_pc[NHOOK] = {0x180fe0, 0x180c80, 0x180dc0, 0x1805a4, 0x181ad0, 0x180a20, 0x180990, 0x11c198, 0x167cd4, 0x119eee, 0x1139ad, 0x1283dc, 0x150ccd};   /* (H_OPTIONS: the menus' options function, called with its item's word) */   /* (H_TICK: in the park's tick, the return from its call at 0x1139a8) */
#define BIG_VISITORS 10000                /* a 128x128 park's most (its 12,000 things, bigmap.c) */
extern uint32_t tp_things_n;              /* the things pool (bigmap.c): 1200, more in a 128x128 park */
extern uint32_t tp_tune_max_people;       /* (dispatch.c, generated: the game's 800) */
#define SIM_LEVEL    0x1d66f1u            /* 0 sandbox, 1 sim, 2 full */
#define PLAYER_PARK  0x1d3818u            /* -> the player's park record (0x250 bytes): +0x1f visitors, +0x48 bus size, +0x49 level */
#define BUS_SIZES    0x1b78ccu
#define PLAY_TUNE    0x180a20u
#define STOP_MUSIC   0x180990u
#define SET_MUSIC_VOL 0x180e40u
#define RIDE_TUNE_RET 0x11c1c7u
#define CUR_TUNE     0x1ba71au
#define MUSIC_RIDE   0x1d3ae8u
#define MUSIC_STATE  0x1d6632u
#define RIDE_TYPES   0x1cced0u
#define NTYPES       29
#define NTUNES       22

static int log_sounds, ride_music = 1, park_tune, free_visitors, free_bus, arrive_x = 4;
static double ew_gap, ew_last = -1e9;
static double music_hold = 3.0;             /* seconds a ride tune outlasts its ride leaving the game's view (TP_MUSIC_HOLD) */
#define MUSIC_HOLD music_hold
static double tune_seen[64];               /* when each tune's ride was last in the game's view */
static int8_t type_tune[NTYPES];          /* -1: the game's */

static uint32_t r32(const uint8_t *m, uint32_t a) { uint32_t v; memcpy(&v, m + a, 4); return v; }
static uint16_t r16(const uint8_t *m, uint32_t a) { uint16_t v; memcpy(&v, m + a, 2); return v; }
static void w16(uint8_t *m, uint32_t a, uint16_t v) { memcpy(m + a, &v, 2); }
static void w32(uint8_t *m, uint32_t a, uint32_t v) { memcpy(m + a, &v, 4); }
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

void tp_qol_init(void) {
    const char *e;
    log_sounds = getenv("TP_SOUND_LOG") != NULL;
    ew_gap = (e = getenv("TP_EW_GAP")) ? atof(e) : 0;
    if ((e = getenv("TP_MUSIC_HOLD"))) music_hold = atof(e);
    ride_music = !((e = getenv("TP_RIDE_MUSIC")) && atoi(e) == 0);
    park_tune = (e = getenv("TP_PARK_TUNE")) ? atoi(e) : 0;
    free_visitors = (e = getenv("TP_FREE_VISITORS")) ? atoi(e) : 0;
    if (free_visitors > BIG_VISITORS) free_visitors = BIG_VISITORS;
    if (free_visitors < 0) free_visitors = 0;
    arrive_x = (e = getenv("TP_FREE_ARRIVALS")) ? atoi(e) : 4;   /* (development: other multiples) */
    if (arrive_x < 1) arrive_x = 1; if (arrive_x > 100) arrive_x = 100;
    free_bus = (e = getenv("TP_FREE_BUS")) ? atoi(e) : 0;
    if (free_bus > 255) free_bus = 255;
    if (free_bus < 0) free_bus = 0;
    tp_tune_max_people = 0x320;
    { void tp_bigmap_init(void); tp_bigmap_init(); }   /* larger maps (bigmap.c) */
    { void tp_free_init(void); tp_free_init(); }       /* Free Mode rules (freemode.c) */
    if (park_tune < 0 || park_tune > NTUNES) park_tune = 0;
    memset(type_tune, -1, sizeof type_tune);
    for (e = getenv("TP_RIDE_TUNES"); e && *e; ) {
        char *p; long t = strtol(e, &p, 10); if (*p != ':') break;
        long n = strtol(p + 1, &p, 10);
        if (t >= 0 && t < NTYPES && n >= 0 && n <= NTUNES) type_tune[t] = (int8_t)n;
        e = *p == ',' ? p + 1 : p; if (*p != ',') break;
    }
    for (int i = 0; i < NHOOK; i++) tp_hook_pcs[QOL0 + i] = hook_pc[i];
    { void tp_view_pacing_init(void); tp_view_pacing_init(); }   /* steady pacing of park frames (view.c) */
    tp_hooks_changed();
    if (ew_gap || !ride_music || park_tune || getenv("TP_RIDE_TUNES") || free_visitors || free_bus)
        fprintf(stderr, "qol: ew gap %g, ride music %d, park tune %d, tunes %s, free mode visitors %d bus %d\n", ew_gap, ride_music, park_tune,
                getenv("TP_RIDE_TUNES") ? getenv("TP_RIDE_TUNES") : "-", free_visitors, free_bus);
}

static void skip_call(void) {                     /* return to the caller at once (it pops the arguments) */
    uint8_t *m = tp_host_memory(); uint32_t sp = tp_get(4);
    tp_set(9, r32(m, sp)); tp_set(4, sp + 4);
}
static void play(uint32_t tune) {
    uint32_t a = tune; tp_host_guest_call(PLAY_TUNE, &a, 1, 2000000);
    uint32_t v = 100; tp_host_guest_call(SET_MUSIC_VOL, &v, 1, 2000000);
}

/* ---- EXTRAS: this port's options in the game's own top bar (submenus: GAME SPEED, PANEL SIZE) ----
 * The bar is data: a list of {drop-down, text} words ([tp_menu_bar], the game's at 0x1b53b6, ended by text -1; a
 * build site, config/mapgeo.json), each drop-down 0x354 bytes at 0x1b53c6 + n * 0x354 (its rectangle, +0xa the item
 * under the pointer, from +0xc items of 0x15 bytes: submenu or -1, text, kind (bit 0 a tick from the byte at +5,
 * bit 1 a line under it), +5 the tick's byte, +9 another, +0xd a function and +0x11 its word, called by the main
 * loop after the click (0x1122b2), or +0x13/+0x14 a key pressed instead). Texts are words into the string table
 * [0x1d7bcc]. The game's list is copied with an EXTRAS entry into a hole of memory the game is never given
 * (host.c), with the drop-down there (n = 32666) and a copy of the string table with these texts at its end
 * (re-made if the game loads another). Each item calls the options function 0x150ccd with a word of 0x7700 up
 * (its switch has 0..13, nothing else: unhooked, they do nothing); H_OPTIONS takes them. Only what takes effect
 * at once: the ride and park music, the grossed-out sounds, frame pacing, and the app's pixels and pointer. */
#define X_AREA   0x1c40000u
#define X_LIST   (X_AREA + 0x0)
#define X_DROPS  3                                  /* EXTRAS and its two submenus */
#define X_DROP_N 32666                              /* (0x1b53c6 + 32666 * 0x354 = 0x1c4004e: inside the area) */
#define X_DROP(i) (0x1b53c6u + (uint32_t)(X_DROP_N + (i)) * 0x354u)
#define X_FLAGS  (X_AREA + 0xb00)
#define X_TEXT   (X_AREA + 0xc00)
#define X_TAB    (X_AREA + 0x2000)
#define X_TAB_MAX ((0x10000 - 0x2000) / 4)
#define X_ARG    0x7700
long (*tp_extras_get)(const char *key);              /* (main.m: the app's settings) */
void (*tp_extras_set)(const char *key, long value);
double (*tp_extras_getf)(const char *key);
void (*tp_extras_setf)(const char *key, double value);
extern uint32_t tp_menu_bar;
void tp_view_set_pacing(int on); int tp_view_pacing(void); void tp_view_set_ui_scale(double k);
void tp_host_set_cycles(double per_ms); double tp_host_cycles(void);
static const char *x_title = "EXTRAS";
/* menu: 0 EXTRAS, 1 GAME SPEED, 2 PANEL SIZE; sub: the submenu an item opens */
static const struct { int menu; const char *text; int line, sub; } x_items[] = {
    {0, "RIDE MUSIC", 0, 0}, {0, "PARK MUSIC", 0, 0}, {0, "FEWER EW SOUNDS", 1, 0},
    {0, "GAME SPEED", 0, 1}, {0, "PANEL SIZE", 0, 2}, {0, "STEADY FRAME PACING", 1, 0},
    {0, "SMOOTH PIXELS", 0, 0}, {0, "CRT LINES", 0, 0}, {0, "MAC POINTER", 0, 0},
    {1, "ORIGINAL", 0, 0}, {1, "1.25X", 0, 0}, {1, "1.5X", 0, 0}, {1, "2X", 0, 0}, {1, "4X", 0, 0},
    {2, "AS DRAWN", 0, 0}, {2, "1.25X", 0, 0}, {2, "1.5X", 0, 0}, {2, "1.75X", 0, 0}, {2, "2X", 0, 0}};
#define X_N (int)(sizeof x_items / sizeof x_items[0])
#define X_SPEED0 9                                   /* (first items of the submenus: the launcher's choices) */
#define X_PANEL0 14
static const double x_speeds[5] = {10000, 12500, 15000, 20000, 40000}, x_panels[5] = {1.0, 1.25, 1.5, 1.75, 2.0};
static uint32_t x_game_tab; static int x_on = -1, x_tick;
static long x_get(const char *k, long d) { return tp_extras_get ? tp_extras_get(k) : d; }
static void x_set(const char *k, long v) { if (tp_extras_set) tp_extras_set(k, v); }
static double x_getf(const char *k, double d) { return tp_extras_getf ? tp_extras_getf(k) : d; }
static void x_flags(uint8_t *m) {
    long px = x_get("SmoothPixels", 0); double cyc = tp_host_cycles(), ps = x_getf("PanelScale", 1.0);
    uint8_t f[X_N]; memset(f, 0, sizeof f);
    f[0] = ride_music != 0; f[1] = park_tune != 0; f[2] = ew_gap != 0; f[5] = tp_view_pacing() != 0;
    f[6] = px == 1; f[7] = px == 2; f[8] = x_get("ShowMacCursor", 0) != 0;
    for (int i = 0; i < 5; i++) { f[X_SPEED0 + i] = fabs(cyc - x_speeds[i]) < 1; f[X_PANEL0 + i] = fabs(ps - x_panels[i]) < 0.01; }
    memcpy(m + X_FLAGS, f, X_N);
}
static void x_build(uint8_t *m) {                    /* the menus, the bar's list, the texts (once the game's table exists) */
    uint32_t tab = r32(m, 0x1d7bcc);
    if (!tab || tab == X_TAB || tab >= 0x4000000u) return;
    int n = 0; uint32_t lo = 0xffffffffu, hi = 0;
    for (; n < 8000; n++) { uint32_t p = r32(m, tab + (uint32_t)n * 4); if (p < 0x100000u || p >= 0x4000000u) break; if (p < lo) lo = p; if (p > hi) hi = p; if (hi - lo > 0x40000) break; }
    if (n < 64 || n + X_N + 1 > X_TAB_MAX) { fprintf(stderr, "extras: the string table at %#x was not understood (%d)\n", tab, n); x_on = 0; return; }
    memcpy(m + X_TAB, m + tab, (size_t)n * 4);
    uint32_t at = X_TEXT; int id0 = n;
    for (int k = 0; k <= X_N; k++) { const char *t = k ? x_items[k - 1].text : x_title; w32(m, X_TAB + (uint32_t)(n + k) * 4, at); memcpy(m + at, t, strlen(t) + 1); at += (uint32_t)strlen(t) + 1; }
    for (int d = 0; d < X_DROPS; d++) {
        uint32_t dd = X_DROP(d); memset(m + dd, 0, 0x354); w16(m, dd + 0xa, 0xffff); int c = 0;
        for (int k = 0; k < X_N; k++) { if (x_items[k].menu != d) continue;
            uint32_t it = dd + 0xc + (uint32_t)c++ * 0x15, sub = x_items[k].sub;
            w16(m, it, sub ? (uint16_t)(X_DROP_N + sub) : 0xffff); w16(m, it + 2, (uint16_t)(id0 + 1 + k));
            m[it + 4] = (uint8_t)((sub ? 0 : 1) | (x_items[k].line ? 2 : 0));
            w32(m, it + 5, sub ? 0 : X_FLAGS + (uint32_t)k); w32(m, it + 9, 0); w32(m, it + 0xd, sub ? 0 : 0x150ccd); w16(m, it + 0x11, sub ? 0 : (uint16_t)(X_ARG + k));
            m[it + 0x13] = m[it + 0x14] = 0; }
        w16(m, dd + 0xc + (uint32_t)c * 0x15 + 2, 0xffff);   /* (the end: text -1) */
    }
    int e = 0; for (uint32_t a = 0x1b53b6; e < 12 && (int16_t)r16(m, a + 2) != -1; a += 4, e++) w32(m, X_LIST + (uint32_t)e * 4, r32(m, a));
    w16(m, X_LIST + (uint32_t)e * 4, X_DROP_N); w16(m, X_LIST + (uint32_t)e * 4 + 2, (uint16_t)id0); e++;
    w16(m, X_LIST + (uint32_t)e * 4, 0); w16(m, X_LIST + (uint32_t)e * 4 + 2, 0xffff);
    x_flags(m);
    x_game_tab = tab; w32(m, 0x1d7bcc, X_TAB); tp_menu_bar = X_LIST;
    fprintf(stderr, "extras: in the top bar (%d game texts, the menu's from %d)\n", n, id0);
}
static void x_frame(uint8_t *m) {
    if (x_on < 0) x_on = !getenv("TP_NO_EXTRAS");
    if (!x_on) return;
    if (r32(m, 0x1d7bcc) != X_TAB) x_build(m);
    if (++x_tick % 8 == 0) x_flags(m);
}
static void x_choose(uint8_t *m, int k) {
    switch (k) {
    case 0: ride_music = !ride_music; x_set("RideMusic", ride_music); break;
    case 1: { static int last = 1; if (park_tune) { last = park_tune; park_tune = 0; } else park_tune = last > 0 ? last : 1; x_set("ParkTune", park_tune); break; }
    case 2: { static double last = 30; if (ew_gap) { last = ew_gap; ew_gap = 0; } else ew_gap = last > 0 ? last : 30; x_set("GrossOutGap", (long)ew_gap); break; }
    case 5: { int on = !tp_view_pacing(); tp_view_set_pacing(on); x_set("SteadyPacing", on); break; }
    case 6: x_set("SmoothPixels", x_get("SmoothPixels", 0) == 1 ? 0 : 1); break;
    case 7: x_set("SmoothPixels", x_get("SmoothPixels", 0) == 2 ? 0 : 2); break;
    case 8: x_set("ShowMacCursor", !x_get("ShowMacCursor", 0)); break;
    default:
        if (k >= X_SPEED0 && k < X_SPEED0 + 5) { tp_host_set_cycles(x_speeds[k - X_SPEED0]); x_set("Cycles", (long)x_speeds[k - X_SPEED0]); }
        else if (k >= X_PANEL0 && k < X_PANEL0 + 5) { tp_view_set_ui_scale(x_panels[k - X_PANEL0]); if (tp_extras_setf) tp_extras_setf("PanelScale", x_panels[k - X_PANEL0]); }
    }
    x_flags(m);
    if (log_sounds || getenv("TP_EXTRAS_LOG")) fprintf(stderr, "extras: %s%s chosen\n", x_items[k].menu == 1 ? "game speed " : x_items[k].menu == 2 ? "panel size " : "", x_items[k].text);
}

void tp_qol_hook(int nested) {
    uint8_t *m = tp_host_memory();
    uint32_t pc = tp_get(9), sp = tp_get(4);
    int k = tp_hook_which - QOL0;
    if (k < 0 || k >= NHOOK || pc != hook_pc[k]) return;
    if (!nested && k != H_NEW_PARK) { void tp_bigmap_poll(void); tp_bigmap_poll(); }   /* (a park being set up: its size) */
    if (k == H_OPTIONS) {                             /* (an EXTRAS item: its word) */
        uint32_t a = r32(m, sp + 4) & 0xffff;
        if (!nested && a >= X_ARG && a < X_ARG + (uint32_t)X_N) { x_choose(m, (int)(a - X_ARG)); skip_call(); }
        return;
    }
    if (k == H_TICK) {                                /* (the park ticks: Free Mode rules, freemode.c) */
        if (!nested) x_frame(m);
        if (!nested) { void tp_free_frame(void); tp_free_frame(); }
        if (!nested && getenv("TP_STALL_PROFILE")) {      /* development: the blocks of the first park tick over 200 ms, 15 s in */
            extern uint32_t *tp_trace_buf; extern uint32_t tp_trace_cap, tp_trace_n; static double l, t0; static int done; double t = now();
            if (!t0) t0 = t;
            if (!tp_trace_buf && !done) { tp_trace_cap = 30000000; tp_trace_buf = malloc(4ul * tp_trace_cap); }
            if (tp_trace_buf && l && t - l > 0.2 && t - t0 > 15) {
                FILE *f = fopen(getenv("TP_STALL_PROFILE"), "wb"); if (f) { fwrite(tp_trace_buf, 4, tp_trace_n, f); fclose(f); }
                fprintf(stderr, "stall profile: %u blocks in %.1f ms\n", tp_trace_n, (t - l) * 1e3); tp_trace_buf = NULL; done = 1;
            }
            tp_trace_n = 0; l = t;
        }
        if (!nested && log_sounds) { static int lc = -1; int c = (int16_t)r16(m, CUR_TUNE); if (c != lc) { fprintf(stderr, "music: now playing %d (%.3f)\n", c, now()); lc = c; } }
        if (!nested && log_sounds) { static double l, worst, lp; double t = now(); if (l && t - l > worst) worst = t - l;   /* (development: the park's tick, slowed?) */
            extern double tp_dbg_lockwait, tp_dbg_passes, tp_dbg_pace, tp_dbg_run, tp_dbg_gov; static double lw0, ps0, pc0, rn0, gv0;
            if (l && t - l > 0.04) fprintf(stderr, "tick gap %.3f: %.1f ms (game code %.1f, governor %.1f, pacing %.1f, view lock %.1f, passes %.1f)\n", t, (t - l) * 1e3,
                                           (tp_dbg_run - rn0) * 1e3, (tp_dbg_gov - gv0) * 1e3, (tp_dbg_pace - pc0) * 1e3, (tp_dbg_lockwait - lw0) * 1e3, (tp_dbg_passes - ps0) * 1e3);
            lw0 = tp_dbg_lockwait; ps0 = tp_dbg_passes; pc0 = tp_dbg_pace; rn0 = tp_dbg_run; gv0 = tp_dbg_gov;
            if (t - lp > 2) { fprintf(stderr, "ticks: worst gap %.1f ms\n", worst * 1e3); worst = 0; lp = t; } l = t; }
        return;
    }
    if (k == H_BUS_COUNT) {                           /* (0x167cd4 returned: how many get off the bus) */
        uint32_t park = r32(m, PLAYER_PARK);
        if (!nested && m[SIM_LEVEL] == 0 && tp_things_n > 1200 && park) {   /* a 128x128 park: four times the visitors a bus brings */
            int n = (int16_t)tp_get(0), in = (int16_t)r16(m, park + 0x1f), room = (int)tp_tune_max_people - in, more = n * arrive_x;
            if (more > room) more = room;
            if (more > n) tp_set(0, (tp_get(0) & 0xffff0000u) | (uint16_t)more);
            if (log_sounds) { static int c; if (c++ < 10) fprintf(stderr, "free: bus brings %d, now %d (in park %d)\n", n, more > n ? more : n, in); }
        }
        return;
    }
    uint32_t ret = r32(m, sp), a1 = r32(m, sp + 4), a2 = r32(m, sp + 8);
    switch (k) {
    case H_SFX: case H_SFX_PARK: case H_SFX3: case H_SFX4: case H_SFX5:
        if (log_sounds) fprintf(stderr, "sound %.3f fn %06x bank %d sample %d from %06x%s\n", now(), pc, (int16_t)a1, (int16_t)a2, ret, nested ? " (pass)" : "");
        if (nested) { skip_call(); return; }          /* a pass drawing another part of the park */
        if (k == H_SFX_PARK && (int16_t)a1 == 0 && (int16_t)a2 == 13 && ew_gap) {   /* grossed out */
            double t = now();
            if (ew_gap < 0 || t - ew_last < ew_gap) { if (log_sounds) fprintf(stderr, "sound: ew skipped\n"); skip_call(); return; }
            ew_last = t; if (log_sounds) fprintf(stderr, "sound: ew played\n");
        }
        return;
    case H_PLAY_TUNE:
        if (log_sounds) { static int n; if (n++ < 20) fprintf(stderr, "music: play_tune(%d) from %06x%s\n", (int16_t)a1, ret, nested ? " (pass)" : ""); }
        if (ret != RIDE_TUNE_RET) return;             /* jingles and the like: as the game plays them */
        if (nested) { skip_call(); return; }
        {   int tune = (int16_t)a1;
            uint32_t thing = r32(m, MUSIC_RIDE);
            int type = thing ? r16(m, thing + 0x16) / 0xc8 : -1;
            if (type >= 0 && type < NTYPES && type_tune[type] >= 0) tune = type_tune[type];
            if (!ride_music) tune = 0;
            if (!tune && park_tune) tune = park_tune;
            if (log_sounds) { static int last = -99; if (tune != last) fprintf(stderr, "music: ride type %d tune %d -> %d\n", type, (int16_t)a1, tune); last = tune; }
            /* The game plays the tune of a music ride in its view; in the wide view that view follows the pointer, so
             * moving it over the park stopped, restarted and swapped tunes many times a second (tunes stepping on each
             * other). A playing ride tune now stays until its ride has been out of the game's view for MUSIC_HOLD. */
            {   double t = now(); int cur = (int16_t)r16(m, CUR_TUNE);
                if (tune > 0 && tune < 64) tune_seen[tune] = t;
                if (tune && cur > 0 && cur < 64 && cur != tune && cur != park_tune && t - tune_seen[cur] < MUSIC_HOLD) {
                    if (log_sounds) { static double l; if (t - l > 2) { l = t; fprintf(stderr, "music: keeping tune %d (asked for %d)\n", cur, tune); } }
                    skip_call(); return;
                }
            }
            if (!tune) {                                /* none: stop what plays (stop returns where play would) */
                if (r16(m, CUR_TUNE)) tp_set(9, STOP_MUSIC); else skip_call();
                return;
            }
            w16(m, sp + 4, (uint16_t)tune);
        }
        return;
    case H_STOP_MUSIC:
        if (ret != 0x11c183u && ret != 0x11c193u) return;   /* the renderer: no music ride on screen */
        if (nested) { skip_call(); return; }
        {   int cur = (int16_t)r16(m, CUR_TUNE);              /* (its ride was in view moments ago: keep it, MUSIC_HOLD) */
            if (cur > 0 && cur < 64 && cur != park_tune && now() - tune_seen[cur] < MUSIC_HOLD) { skip_call(); return; } }
        if (park_tune) { play((uint32_t)park_tune); skip_call(); }
        if (log_sounds) { static double l; if (now() - l > 2) { l = now(); fprintf(stderr, "music: no ride on screen, %s\n", park_tune ? "park tune" : "stop"); } }
        return;
    case H_NEW_PARK:                                  /* larger maps (bigmap.c): the new park's size */
        if (!nested) { void tp_bigmap_new_park(void); tp_bigmap_new_park(); void tp_free_new_park(void); tp_free_new_park(); }
        return;
    case H_BUS_ARRIVES: {                             /* free mode, sandbox parks only */
        int sandbox = m[SIM_LEVEL] == 0;
        int cap = tp_things_n > 1200 ? (free_visitors ? free_visitors : BIG_VISITORS) : free_visitors < 1000 ? free_visitors : 1000;
        tp_tune_max_people = sandbox && cap > 0x320 ? (uint32_t)cap : 0x320;
        uint32_t park = r32(m, PLAYER_PARK);
        if (sandbox && free_bus && park) {
            if (m[park + 0x48] < free_bus) m[park + 0x48] = (uint8_t)free_bus;   /* (the game's size by level, BUS_SIZES, if larger) */
        }
        if (log_sounds) { static int n; if (n++ < 10) fprintf(stderr, "free: bus arrives, sandbox %d, cap %u, bus size %d, in park %d\n", sandbox,
                                                                 tp_tune_max_people, park ? m[park + 0x48] : -1, park ? (int16_t)r16(m, park + 0x1f) : -1); }
        return;
    }
    case H_MUSIC_END:                                 /* the renderer's music step (not stopping): no music ride seen at all? */
        if (log_sounds) { static int n; if (n++ < 5) fprintf(stderr, "music: step state %d current %d%s\n", (int16_t)r16(m, MUSIC_STATE), r16(m, CUR_TUNE), nested ? " (pass)" : ""); }
        if (!nested && park_tune && (int16_t)r16(m, MUSIC_STATE) == 0 && r16(m, CUR_TUNE) != park_tune) play((uint32_t)park_tune);
        return;
    }
}
