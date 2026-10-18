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
 * Called from qol.c: tp_free_frame on every park tick (its call to 0x118b92 from 0x1139a8). */
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

static uint32_t r32(const uint8_t *m, uint32_t a) { uint32_t v; memcpy(&v, m + a, 4); return v; }
static void w32(uint8_t *m, uint32_t a, uint32_t v) { memcpy(m + a, &v, 4); }
static void w16(uint8_t *m, uint32_t a, int v) { int16_t s = (int16_t)v; memcpy(m + a, &s, 2); }
static int ok_ptr(uint32_t p) { return p >= 0x1000 && p < 0x3f00000; }

void tp_free_init(void) {
    const char *e;
    rules_new = (e = getenv("TP_FREE_RULES")) ? atoi(e) : 0;
    if (rules_new < 0 || rules_new > 2) rules_new = 0;
    ramp_new = (e = getenv("TP_FREE_RAMP")) ? atoi(e) : 60;
    if (ramp_new < 1) ramp_new = 1; if (ramp_new > 600) ramp_new = 600;
}
/* A new park's rules (or a loaded one's) are settled at its first frame (view.c's FRAME_RENDER, kept
 * on meanwhile), when the setting-up screens are done and the sim level is known. */
extern uint32_t tp_hook_pcs[24]; void tp_hooks_changed(void);
static void want_frame(void) { tp_hook_pcs[0] = 0x113b9au; tp_hooks_changed(); }
int tp_free_waiting(void) { return pending != 0; }
void tp_free_new_park(void) { pending = 1; loaded_rules = -1; want_frame(); }
/* A saved park being read (host.c, its map): its rules from beside the save. */
void tp_free_loaded(const char *save) {
    char p[1100]; snprintf(p, sizeof p, "%s.free", save);
    FILE *f = fopen(p, "r"); int r = 0, mo = 0, ra = ramp_new;   /* "rules months ramp" */
    if (f) { if (fscanf(f, "%d %d %d", &r, &mo, &ra) < 2) r = mo = 0; fclose(f); }
    loaded_rules = (char)(r >= 0 && r <= 2 ? r : 0); loaded_months = mo < 0 ? 0 : mo;
    loaded_ramp = ra < 1 ? 1 : ra > 600 ? 600 : ra; pending = 2; want_frame();
}
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
        if (rules == 1) apply_difficulty(m, pending == 1);
        if (rules && getenv("TP_FREE_LOG")) fprintf(stderr, "free mode: %s park, rules %d, month %d\n", pending == 2 ? "loaded" : "new", rules, months);
        pending = 0;
    }
}
/* Every park tick: keep the rules. */
void tp_free_frame(void) {
    uint8_t *m = tp_host_memory();
    uint32_t rec = r32(m, PLAYER_REC);
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
