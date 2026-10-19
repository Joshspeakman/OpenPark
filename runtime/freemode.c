/* Free Mode rules for sandbox parks (docs/PLAN.md "Endless and True Freeplay").
 *
 * The Launcher's Free Mode > Rules (TP_FREE_RULES) applies to new sandbox parks:
 *   0 Sandbox: the game as it is.
 *   1 Endless: the park starts at the game's easiest settings (Park Visitors Happy, Start Level Easy,
 *     Opponents Easy: the start money of Easy, 200,000) and month by month moves to its hardest
 *     (Fussy, Hard) over the ramp (TP_FREE_RAMP months, 60 by default). Research is the game's own.
 *   2 True Freeplay: every ride and shop researched, and the money never runs out.
 * What the settings do is the game's: the player-details screen keeps them in 0x1d66f2 (park visitors
 * 0..2), 0x1d66f5 (start level) and 0x1d66f6 (opponents level); a park being bought (0x16e8xx) turns
 * them into its record's thresholds (+0: 200 + 50 fussy, +4: 100 + 40 (2 - start), +6: 25 + 15 (2 -
 * start), +8: 104 + 20 (2 - fussy)), a new visitor's +0x54 is (2 - fussy) * 50 (0x128534), and the
 * start money is 0x1b7f7c[start level]. Endless writes those with a difficulty d going 0 -> 2: the
 * record's values in proportion; the fussiness byte dithered between its neighbours, so the visitors
 * arriving are on average as fussy as d.
 * The month: the player record's month (0..11, [0x1d3814] + 0x5ed); Endless counts its changes. A
 * park's rules and months are kept beside its save (<save>.free, host.c), as the 128x128 marker is.
 * Called from qol.c: tp_free_frame on every park tick (0x1139ad, the return from the tick's call at 0x1139a8). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

uint8_t *tp_host_memory(void);

#define SIM_LEVEL    0x1d66f1u     /* 0 sandbox */
#define SET_VISITORS 0x1d66f2u     /* 0 happy, 1 average, 2 fussy */
#define SET_START    0x1d66f5u     /* 0 easy .. 2 hard */
#define SET_OPPS     0x1d66f6u
#define START_MONEY  0x1b7f7cu     /* dword[3]: 200,000 150,000 100,000 */
#define MONEY_SHOWN  0x1d6718u
#define COMPUTER     0x1d3b00u     /* -> the player's block: +0x0a money */
#define PLAYER_PARK  0x1d3818u     /* -> the player's park record */
#define PLAYER_REC   0x1d3814u     /* -> the player's record: +0x5ed the month */
#define RIDES_RT     0x1cced0u     /* 29 ride definitions (0xc8): +0xc5 researched */
#define SHOPS_RT     (0x1d3c62u + 0x22u)   /* 17 shop definitions (0x6b): +0x44 researched */

static int rules_new, ramp_new = 60;   /* the Launcher's: for new parks */
static int rules, months, ramp = 60, last_month = -1, pending;   /* the park being played */
static char loaded_rules = -1; static int loaded_months, loaded_ramp;
static uint32_t rng = 0x9e3779b9u;
/* Endless moves the player-details screen's own bytes; the next game gets back what was there
 * (a loaded Endless park: the screen's defaults, Happy / Medium / Easy). */
static uint8_t keep_set[3]; static int kept;
static int redraw_ticks;                            /* (the landscape redrawn on the next ticks too: the first frame's own clears it) */
static void keep_settings(const uint8_t *m, int defaults) {
    if (defaults) { keep_set[0] = 0; keep_set[1] = 1; keep_set[2] = 0; }
    else { keep_set[0] = m[SET_VISITORS]; keep_set[1] = m[SET_START]; keep_set[2] = m[SET_OPPS]; }
    kept = 1;
}
static void give_back_settings(void) {
    if (!kept) return;
    uint8_t *m = tp_host_memory(); m[SET_VISITORS] = keep_set[0]; m[SET_START] = keep_set[1]; m[SET_OPPS] = keep_set[2]; kept = 0;
}

static uint32_t r32(const uint8_t *m, uint32_t a) { uint32_t v; memcpy(&v, m + a, 4); return v; }
static void w32(uint8_t *m, uint32_t a, uint32_t v) { memcpy(m + a, &v, 4); }
static void w16(uint8_t *m, uint32_t a, int v) { int16_t s = (int16_t)v; memcpy(m + a, &s, 2); }
static int ok_ptr(uint32_t p) { return p >= 0x1000 && p < 0x3f00000; }

void tp_free_init(void) {
    const char *e;
    rules = months = pending = 0; last_month = -1; loaded_rules = -1; kept = 0; redraw_ticks = 0;   /* (a new game: issue #6) */
    rules_new = (e = getenv("TP_FREE_RULES")) ? atoi(e) : 0;
    if (rules_new < 0 || rules_new > 2) rules_new = 0;
    ramp_new = (e = getenv("TP_FREE_RAMP")) ? atoi(e) : 60;
    if (ramp_new < 1) ramp_new = 1; if (ramp_new > 600) ramp_new = 600;
}
/* A new park's rules (or a loaded one's) are settled at its first frame (view.c's FRAME_RENDER, kept
 * on meanwhile), when the setting-up screens are done and the sim level is known. */
extern uint32_t tp_hook_pcs[32]; void tp_hooks_changed(void);
static void want_frame(void) { tp_hook_pcs[0] = 0x113b9au; tp_hooks_changed(); }
int tp_free_waiting(void) { return pending != 0; }
void tp_free_new_park(void) { give_back_settings(); rules = 0; pending = 1; loaded_rules = -1; want_frame(); }
/* A saved park being read (host.c, its map): its rules from the save's trailer (r >= 0, issue #5), else from
 * beside the save (older saves). */
void tp_free_loaded_rules(const char *save, int r, int mo, int ra) {
    if (r < 0) {
        char p[1100]; snprintf(p, sizeof p, "%s.free", save);
        FILE *f = fopen(p, "r"); r = 0; mo = 0; ra = ramp_new;   /* "rules months ramp" */
        if (f) { if (fscanf(f, "%d %d %d", &r, &mo, &ra) < 2) r = mo = 0; fclose(f); }
    }
    loaded_rules = (char)(r >= 0 && r <= 2 ? r : 0); loaded_months = mo < 0 ? 0 : mo;
    loaded_ramp = ra < 1 ? 1 : ra > 600 ? 600 : ra; pending = 2; want_frame();
    if (rules == 1) give_back_settings();              /* (leaving an Endless park; the save's own follow) */
    rules = 0;
}
void tp_free_loaded(const char *save) { tp_free_loaded_rules(save, -1, 0, 0); }
/* The rules of the park being played, for a save's trailer (host.c). */
int tp_free_state(int *mo, int *ra) { *mo = months; *ra = ramp; return rules; }
/* A park saved (host.c): its rules beside it (none: the marker goes). */
void tp_free_saved(const char *save) {
    char p[1100]; snprintf(p, sizeof p, "%s.free", save);
    if (!rules) { unlink(p); return; }
    FILE *f = fopen(p, "w"); if (f) { fprintf(f, "%d %d %d\n", rules, months, ramp); fclose(f); }
}
int tp_free_rules(void) { return rules; }

static double difficulty(void) { double d = 2.0 * months / ramp; return d > 2 ? 2 : d; }
static void apply_difficulty(uint8_t *m, int fresh) {
    double d = difficulty();
    uint32_t park = r32(m, PLAYER_PARK);
    if (ok_ptr(park)) {
        w16(m, park + 0, (int)(200 + 50 * d + 0.5));
        w16(m, park + 4, (int)(100 + 40 * (2 - d) + 0.5));
        w16(m, park + 6, (int)(25 + 15 * (2 - d) + 0.5));
        w16(m, park + 8, (int)(104 + 20 * (2 - d) + 0.5));
    }
    int whole = d >= 1.5 ? 2 : d >= 0.5 ? 1 : 0;
    m[SET_START] = m[SET_OPPS] = (uint8_t)whole;
    if (fresh) {                                       /* a new Endless park: Easy's start money */
        uint32_t money = r32(m, START_MONEY), comp = r32(m, COMPUTER);
        w32(m, MONEY_SHOWN, money); if (ok_ptr(comp)) w32(m, comp + 0x0a, money);
    }
    if (getenv("TP_FREE_LOG")) fprintf(stderr, "free mode: endless month %d of %d, difficulty %.2f\n", months, ramp, d);
}

/* A new park opens with its entrance in the middle of the view (the player's park record +0x63f: the entrance
 * cell, on the bottom edge). Camera 0x1d667a x, 0x1d6666 y: entrance x - 6, y - 7 puts it in the middle
 * (measured in a 128x128 park, the same steps in a 64x64 one). Below where scrolling stops (the game's limit:
 * the map less a screen) is fine: the road shows, and scrolling up works as usual. */
extern uint32_t tp_map_w;
static void center_on_entrance(uint8_t *m) {
    uint32_t park = r32(m, PLAYER_REC);
    if (!ok_ptr(park) || !tp_map_w || getenv("TP_GAME_CAMERA")) return;   /* (TP_GAME_CAMERA: the game's own, for the golden checks) */
    uint16_t cell; memcpy(&cell, m + park + 0x63f, 2);
    int ex = cell % tp_map_w, ey = cell / tp_map_w, cx = ex - 6, cy = ey - 7;
    if (cx < 4) cx = 4; if (cy < 0) cy = 0; if (cx > (int)tp_map_w || cy > (int)tp_map_w) return;
    w16(m, 0x1d667a, cx); w16(m, 0x1d6666, cy); w16(m, 0x1d666e, 1); redraw_ticks = 8;
    if (getenv("TP_FREE_LOG")) fprintf(stderr, "free mode: entrance %d,%d, camera %d,%d\n", ex, ey, cx, cy);
}

/* The park's first frame: its rules. */
void tp_free_park_shown(void) {
    uint8_t *m = tp_host_memory();
    uint32_t rec = r32(m, PLAYER_REC);
    if (!pending || !ok_ptr(rec)) return;
    int month = m[rec + 0x5ed];
    {
        int sandbox = m[SIM_LEVEL] == 0;
        if (pending == 2) { rules = sandbox ? loaded_rules : 0; months = loaded_months; ramp = loaded_ramp; }
        else { rules = sandbox ? rules_new : 0; months = 0; ramp = ramp_new; }
        last_month = month;
        if (rules == 1 && !kept) keep_settings(m, pending == 2);
        if (rules == 1) apply_difficulty(m, pending == 1);
        if (pending == 1) center_on_entrance(m);
        if (rules && getenv("TP_FREE_LOG")) fprintf(stderr, "free mode: %s park, rules %d, month %d\n", pending == 2 ? "loaded" : "new", rules, months);
        pending = 0;
    }
}
/* Every park tick: keep the rules. */
void tp_free_frame(void) {
    uint8_t *m = tp_host_memory();
    uint32_t rec = r32(m, PLAYER_REC);
    if (redraw_ticks > 0) { redraw_ticks--; w16(m, 0x1d666e, 1); }
    if (pending || !rules || !ok_ptr(rec)) return;
    int month = m[rec + 0x5ed];
    if (rules == 1) {
        if (month != last_month && month < 12) { last_month = month; months++; apply_difficulty(m, 0); }
        double d = difficulty(); int lo = (int)d; double frac = d - lo;   /* arriving visitors: as fussy as d, on average */
        rng = rng * 1664525u + 1013904223u;
        m[SET_VISITORS] = (uint8_t)(lo + (lo < 2 && (rng >> 8) * (1.0 / 16777216.0) < frac));
    } else if (rules == 2) {
        for (int k = 0; k < 29; k++) m[RIDES_RT + k * 0xc8 + 0xc5] = 1;
        for (int k = 0; k < 17; k++) m[SHOPS_RT + k * 0x6b + 0x44] = 1;
        uint32_t comp = r32(m, COMPUTER);
        if (ok_ptr(comp) && (int32_t)r32(m, comp + 0x0a) < 1000000) w32(m, comp + 0x0a, 5000000);
    }
}
