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
 *                       1000 here)
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
#include <time.h>

uint32_t tp_get(unsigned i);
void tp_set(unsigned i, uint32_t v);
uint8_t *tp_host_memory(void);
int64_t tp_host_guest_call(uint32_t fn, const uint32_t *args, int nargs, uint32_t max_blocks);
extern uint32_t tp_hook_pcs[24];
extern int tp_hook_which;
void tp_hooks_changed(void);

#define QOL0 12
enum { H_SFX, H_SFX_PARK, H_SFX3, H_SFX4, H_SFX5, H_PLAY_TUNE, H_STOP_MUSIC, H_MUSIC_END, H_BUS_ARRIVES, NHOOK };
static const uint32_t hook_pc[NHOOK] = {0x180fe0, 0x180c80, 0x180dc0, 0x1805a4, 0x181ad0, 0x180a20, 0x180990, 0x11c198, 0x167cd4};
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

static int log_sounds, ride_music = 1, park_tune, free_visitors, free_bus;
static double ew_gap, ew_last = -1e9;
static int8_t type_tune[NTYPES];          /* -1: the game's */

static uint32_t r32(const uint8_t *m, uint32_t a) { uint32_t v; memcpy(&v, m + a, 4); return v; }
static uint16_t r16(const uint8_t *m, uint32_t a) { uint16_t v; memcpy(&v, m + a, 2); return v; }
static void w16(uint8_t *m, uint32_t a, uint16_t v) { memcpy(m + a, &v, 2); }
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

void tp_qol_init(void) {
    const char *e;
    log_sounds = getenv("TP_SOUND_LOG") != NULL;
    ew_gap = (e = getenv("TP_EW_GAP")) ? atof(e) : 0;
    ride_music = !((e = getenv("TP_RIDE_MUSIC")) && atoi(e) == 0);
    park_tune = (e = getenv("TP_PARK_TUNE")) ? atoi(e) : 0;
    free_visitors = (e = getenv("TP_FREE_VISITORS")) ? atoi(e) : 0;
    if (free_visitors > 1000) free_visitors = 1000;
    if (free_visitors < 0) free_visitors = 0;
    free_bus = (e = getenv("TP_FREE_BUS")) ? atoi(e) : 0;
    if (free_bus > 255) free_bus = 255;
    if (free_bus < 0) free_bus = 0;
    tp_tune_max_people = 0x320;
    { void tp_bigmap_init(void); tp_bigmap_init(); }   /* larger maps (bigmap.c) */
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
    if ((e = getenv("TP_BARPROBE"))) for (int k = 0; k < 3 && *e; k++) {   /* development: sentinel at the first, checks at the others */
        char *q; tp_hook_pcs[21 + k] = (uint32_t)strtoul(e, &q, 16); e = *q ? q + 1 : q; }
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

void tp_qol_hook(int nested) {
    uint8_t *m = tp_host_memory();
    uint32_t pc = tp_get(9), sp = tp_get(4);
    if (tp_hook_which >= 21 && getenv("TP_BARPROBE") && !nested) {
        uint8_t *bb = m + r32(m, 0x1de5bc); static int cnt;
        if (tp_hook_which == 21) memset(bb, 0xee, 640 * 4);
        else { int left = 0; for (int i = 0; i < 640 * 4; i++) left += bb[i] == 0xee;
               if (cnt++ < 40) fprintf(stderr, "barprobe at %06x: %d sentinel left, px %02x %02x\n", pc, left, bb[50], bb[200]); }
        return;
    }
    int k = tp_hook_which - QOL0;
    if (k < 0 || k >= NHOOK || pc != hook_pc[k]) return;
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
        if (park_tune) { play((uint32_t)park_tune); skip_call(); }
        if (log_sounds) { static double l; if (now() - l > 2) { l = now(); fprintf(stderr, "music: no ride on screen, %s\n", park_tune ? "park tune" : "stop"); } }
        return;
    case H_BUS_ARRIVES: {                             /* free mode, sandbox parks only */
        int sandbox = m[SIM_LEVEL] == 0;
        tp_tune_max_people = sandbox && free_visitors > 0x320 ? (uint32_t)free_visitors : 0x320;
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
