/* Larger maps (sandbox parks; docs/PLAN.md "Larger maps").
 *
 * The park is a grid of 4-byte cells at [0x1d3b08], addressed by a cell index x + y * 64. The game's
 * instructions that encode that geometry (config/mapgeo.json, applied by tools/build_game.py) read three
 * variables instead: tp_map_w (the row stride, 64 or 128), tp_map_shift (its log2) and tp_map_ext (the
 * playable size: bounds, edges, random cells). This file sets them and re-encodes the game's data that
 * holds cell offsets in the 64-wide layout:
 *   - the map's memory block (*MAP, size at 0x1b127c) and its entry in the save list (0x1b1521);
 *   - tables of cell offsets (OFFSETS below): neighbour lists, direction steps, the footprints of the
 *     ride and building definitions and of the shops, and the park entrance's template (placed around
 *     the entrance cell by 0x118bc0).
 * Which parks: the Map Size setting (TP_BIG_MAPS=1) gives the map block room for 128x128 at start-up.
 * A new game builds its park (0x119eee, hooked: tp_bigmap_new_park) before the player chooses the sim
 * level, so it is built 64x64; while the park is being set up, a choice of Sandbox rebuilds it 128x128
 * (and back): the entrance and the border are the game's own (0x118bc0), the bus is carried over
 * (tp_bigmap_poll, from qol.c's hooks). The first park frame (view.c) or a load ends the setting up.
 * Other parks stay 64x64 and save exactly as the game does. A saved 128x128 park leaves a marker file beside
 * its save (<save>.big, host.c), and loading switches to the layout of the park being read.
 * The things pool (*THING: visitors, rides, staff, cars; 0xc8-byte records at [0x1d3b10]) grows with the map:
 * the game takes its size from four places (config/mapgeo.json): the allocation (0x1107fe, 0x110842: 1200
 * records, ten fewer at a time until the heap has room), the last record ([0x1d3b78], set at 0x112a34; every
 * loop over things runs to it), the new park's clearing (0x119f38) and the room kept free of visitors
 * (0x128473: the last 100 records, for rides and the like). Those read tp_things_alloc, tp_things_n,
 * tp_things_bytes and tp_things_spare. A 128x128 park uses BIG_THINGS records (saved whole: the save
 * list's entry at 0x1b1529, the count in the marker); other parks 1200, exactly as the game.
 * How long visitors stay grows with it too (twice the walk across): a visitor is ready to go home (and turns
 * for the gate when it next passes one) once 4000 ticks have gone since its clock was set (+0x75; 0x12287c,
 * 0x12308d, 0x1231bd, 0x12386b, 0x1240a4), or its tiredness (+0x82, rising as it walks) passes 8000
 * (0x1231a4, 0x12408b; its "tired" thought 0x1279eb, 0x167c3c) or 2000 per attraction of the park; or when
 * the park closes. A 128x128 park doubles the time and the tiredness (tp_stay_ticks, tp_stay_tired).
 * Test mode (TP_MAP_W=128 TP_MAP_EXT=64): the 64-wide park laid out with a 128 stride must play exactly
 * as the original; a missed or wrong site shows as a difference (docs/PLAN.md). */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

uint8_t *tp_host_memory(void);
extern uint32_t tp_map_w, tp_map_shift, tp_map_ext, tp_map_bytes;   /* (dispatch.c, generated: 64, 6, 64, 0x4000) */
extern uint32_t tp_things_alloc, tp_things_n, tp_things_bytes, tp_things_spare;   /* (1200, 1200, 0x3a980, 0x4e20) */
extern uint32_t tp_stay_ticks, tp_stay_tired;   /* (4000, 8000) */

#define MAP_SIZE    0x1b127cu   /* *MAP block: bytes */
#define SAVE_SIZE   0x1b1521u   /* save list: the map's bytes */
/* Cell offsets in the game's data: {first, records, record size, fields (byte offsets in a record)}. */
typedef struct { uint32_t at; int n, size; int nf; uint16_t f[26]; } Table;
static const Table OFFSETS[] = {
    {0x1b4920, 1, 0, 9, {0, 2, 4, 6, 8, 10, 12, 14, 16}},            /* neighbours: N E S W, then 0 N E S W */
    {0x1b4b2a, 1, 0, 10, {0, 2, 4, 6, 8, 10, 12, 14, 16, 18}},       /* by direction bit (1 N, 2 E, 4 S, 8 W) */
    {0x1b8c3e, 1, 0, 8, {0, 2, 4, 6, 8, 10, 12, 14}},                /* the eight neighbours */
    {0x1b8fbe, 1, 0, 6, {0, 2, 4, 6, 8, 10}},                        /* the row above, this row */
    {0x1b8f44, 8, 10, 2, {0, 4}},                                    /* steps: one and two cells per direction */
    {0x1b301a, 17, 0x6b, 8, {0x17, 0x19, 0x1b, 0x1d, 0x1f, 0x21, 0x23, 0x25}},   /* shops: footprints */
    {0x1b16f8, 29, 0xc8, 26, {0x2a, 0x2c, 0x30, 0x32, 0x34, 0x36, 0x38, 0x3a, 0x3c, 0x3e, 0x40, 0x42, 0x44,
                              0x46, 0x48, 0x4a, 0x4c, 0x4e, 0x50, 0x52, 0x54, 0x56, 0x58, 0x5a, 0x5c, 0x5e}},   /* rides: footprints */
    {0x1b4cca, 23, 6, 1, {0}},                                       /* the park entrance's template */
    {0x1b7df4, 1, 0, 4, {2, 4, 8, 16}},                              /* by direction bit, word-indexed (0x12c3de, 0x12c4aa: rails) */
    {0x1b48e4, 1, 0, 4, {0, 2, 4, 6}},                               /* N E S W, beside the reverse-direction table 0x1b48ec */
};
#define NTABLES (int)(sizeof OFFSETS / sizeof OFFSETS[0])
/* The game plays from copies of the ride and shop definitions, made when a park is started (0x119f73:
 * 0x1b16f8 -> 0x1cced0; 0x119f8c: 0x1b2ff8 -> 0x1d3c62): their offsets too, from the originals. */
static const struct { int table; uint32_t at; } MIRRORS[] = {{6, 0x1cced0}, {5, 0x1d3c62 + (0x1b301a - 0x1b2ff8)}};
static int16_t orig[NTABLES][29][26];
static int forced_things;                 /* TP_THINGS=n (development): every park n things (a 64x64 park's saves keep 1200, cut short: scratch copies only) */
static int saved, alloc_n = 64;           /* the map block: alloc_n square, fixed at start-up */

#define THING_BLOCK 0x1b1369u  /* *THING block: bytes (as allocated) */
#define THING_SAVE  0x1b1529u  /* save list: the things' bytes */
#define THING_PTR   0x1d3b10u
#define THING_LAST  0x1d3b78u  /* -> the last record */
#define GAME_THINGS 1200
#define BIG_THINGS  12000      /* (indices are words, some read signed) */
static int16_t rd16(const uint8_t *m, uint32_t a) { int16_t v; memcpy(&v, m + a, 2); return v; }
static void wr16(uint8_t *m, uint32_t a, int16_t v) { memcpy(m + a, &v, 2); }
static void wr32(uint8_t *m, uint32_t a, uint32_t v) { memcpy(m + a, &v, 4); }

/* A cell offset of the 64-wide layout (dx + dy * 64, small dx) in a w-wide one. */
static int16_t recode(int v, int w) {
    if (!v) return 0;
    int dy = (v + 32 + 64 * 64) / 64 - 64, dx = v - dy * 64;
    return (int16_t)(dx + dy * w);
}
int tp_bigmap_things(int n);
void tp_bigmap_set(int w, int ext) {
    uint8_t *m = tp_host_memory();
    if (w != 128) w = 64;
    if (ext < 64) ext = 64;
    if (ext > w) ext = w;
    if (!saved) {
        for (int t = 0; t < NTABLES; t++) for (int k = 0; k < OFFSETS[t].n; k++) for (int i = 0; i < OFFSETS[t].nf; i++)
            orig[t][k][i] = rd16(m, OFFSETS[t].at + (uint32_t)(k * OFFSETS[t].size) + OFFSETS[t].f[i]);
        saved = 1;
    }
    if (w > alloc_n) { fprintf(stderr, "bigmap: no room for %dx%d (the map block is %dx%d)\n", w, w, alloc_n, alloc_n); w = ext = 64; }
    tp_map_w = (uint32_t)w; tp_map_shift = w == 128 ? 7 : 6; tp_map_ext = (uint32_t)ext;   /* (after the room check: what the game addresses) */
    tp_map_bytes = (uint32_t)(alloc_n * alloc_n * 4);   /* (the block, cleared whole for a new park) */
    wr32(m, SAVE_SIZE, (uint32_t)(w * w * 4));           /* (a 64x64 park saves exactly as the game does) */
    for (int t = 0; t < NTABLES; t++) for (int k = 0; k < OFFSETS[t].n; k++) for (int i = 0; i < OFFSETS[t].nf; i++)
        wr16(m, OFFSETS[t].at + (uint32_t)(k * OFFSETS[t].size) + OFFSETS[t].f[i], recode(orig[t][k][i], w));
    for (int r = 0; r < (int)(sizeof MIRRORS / sizeof MIRRORS[0]); r++) {
        int t = MIRRORS[r].table;
        for (int k = 0; k < OFFSETS[t].n; k++) for (int i = 0; i < OFFSETS[t].nf; i++)
            wr16(m, MIRRORS[r].at + (uint32_t)(k * OFFSETS[t].size) + OFFSETS[t].f[i], recode(orig[t][k][i], w));
    }
    int nt = tp_bigmap_things(forced_things ? forced_things : w == 128 && ext == 128 ? BIG_THINGS : GAME_THINGS);
    fprintf(stderr, "bigmap: stride %d, playable %dx%d, %d things\n", w, ext, ext, nt);
}

int tp_bigmap_alloc(void) { return alloc_n; }

/* The things in play: n records (GAME_THINGS, or up to what the block holds). */
static uint32_t rd32(const uint8_t *m, uint32_t a) { uint32_t v; memcpy(&v, m + a, 4); return v; }
int tp_bigmap_things_room(void) {
    uint8_t *m = tp_host_memory();
    return rd32(m, THING_PTR) ? (int)(rd32(m, THING_BLOCK) / 0xc8) : (int)tp_things_alloc;
}
int tp_bigmap_things(int n) {
    uint8_t *m = tp_host_memory(); int room = tp_bigmap_things_room();
    if (forced_things) n = forced_things;
    if (n < GAME_THINGS) n = GAME_THINGS;
    if (n > room) n = room;
    tp_things_n = (uint32_t)n;
    tp_things_bytes = (uint32_t)room * 0xc8;                  /* (the block, cleared whole for a new park) */
    tp_things_spare = (uint32_t)(n > GAME_THINGS ? n / 25 : 100) * 0xc8;
    tp_stay_ticks = n > GAME_THINGS ? 8000 : 4000; tp_stay_tired = n > GAME_THINGS ? 16000 : 8000;
    wr32(m, THING_SAVE, (uint32_t)n * 0xc8);
    uint32_t things = rd32(m, THING_PTR);
    if (things) wr32(m, THING_LAST, things + (uint32_t)(n - 1) * 0xc8);
    return n;
}
static int big_maps, setup_open, armed;   /* armed: the game has set its default level (0x112b13: 2) */
int64_t tp_host_guest_call(uint32_t fn, const uint32_t *args, int nargs, uint32_t max_blocks);
#define ENTRANCE_FN 0x118bc0u   /* (cell): remove the entrance (0: only that), build the border, place it */
#define THINGS      0x1d3b10u
#define PARK        0x1d3814u   /* the park record: +0x63f the entrance cell */

/* Rebuild a park being set up at n x n: the game's entrance and border, the other things (the bus)
 * moved with the bottom middle of the park and linked into their new cells. */
static void rebuild(int n) {
    uint8_t *m = tp_host_memory();
    int old = (int)tp_map_ext, d = n - old;
    uint32_t zero = 0; tp_host_guest_call(ENTRANCE_FN, &zero, 1, 4000000);   /* (its gate things go too) */
    uint32_t things, map; memcpy(&things, m + THINGS, 4); memcpy(&map, m + 0x1d3b08, 4);
    if (!things || !map) return;
    uint32_t old_entr = (uint32_t)(old / 2 + old * (int)tp_map_w - 4 * (int)tp_map_w);
    memset(m + map, 0, (size_t)alloc_n * alloc_n * 4);
    tp_bigmap_set(n, n);
    uint32_t entr = (uint32_t)(n / 2 + n * n); tp_host_guest_call(ENTRANCE_FN, &entr, 1, 4000000);
    uint32_t new_entr = (uint32_t)(n / 2 + (n - 4) * n);
    for (int k = 1; k < tp_bigmap_things_room(); k++) {
        uint8_t *t = m + things + (uint32_t)k * 0xc8;
        if (!t[4] || t[4] == 20) continue;                     /* (empty; the gate's parts are the game's again) */
        int16_t x, y; memcpy(&x, t + 6, 2); memcpy(&y, t + 8, 2);
        if (x <= 0 || y <= 0 || x >= old * 256 || y >= old * 256) continue;   /* (not on the map yet) */
        x = (int16_t)(x + d / 2 * 256); y = (int16_t)(y + d * 256);
        memcpy(t + 6, &x, 2); memcpy(t + 8, &y, 2);
        uint16_t e; memcpy(&e, t + 0x49, 2); if (e == old_entr) memcpy(t + 0x49, &new_entr, 2);
        int cx = (uint16_t)x >> 8, cy = (uint16_t)y >> 8;
        if (cx >= 0 && cx < n && cy >= 0 && cy < n) {           /* (into the cell's list of things) */
            uint32_t c = map + (uint32_t)(cy * n + cx) * 4; uint16_t first; memcpy(&first, m + c, 2);
            memcpy(t + 2, &first, 2); uint16_t me = (uint16_t)k; memcpy(m + c, &me, 2);
        }
        fprintf(stderr, "bigmap: thing %d (kind %d) moved to %.2f, %.2f\n", k, t[4], x / 256.0, y / 256.0);
    }
}

/* While a park is set up: Sandbox with the setting on makes it 128x128, anything else 64x64. */
void tp_bigmap_poll(void) {
    if (!setup_open || !big_maps || getenv("TP_MAP_W")) return;
    if (!armed) { if (!tp_host_memory()[0x1d66f1]) return; armed = 1; }
    int n = tp_host_memory()[0x1d66f1] == 0 ? 128 : 64;
    if (n != (int)tp_map_ext) { fprintf(stderr, "bigmap: sim level %d: the park is now %dx%d\n", tp_host_memory()[0x1d66f1], n, n); rebuild(n); }
}
/* The border of a 128x128 park made before the fence fix (2026-10-18: the side loop 0x119221 and the bottom row's
 * entrance gap 0x11901a / 0x119021 were still 64x64's): the game's rules again, on its empty cells only (0x118bc0:
 * byte 2 and word 0 clear -> the fence's piece, byte 3 bit 3 set). Run at the first frame of a loaded park. */
static int repair_due;
static void repair_border(void) {
    uint8_t *m = tp_host_memory(); uint32_t map; memcpy(&map, m + 0x1d3b08, 4);
    int n = (int)tp_map_ext, w = (int)tp_map_w, fixed = 0;
    if (!map || n != 128 || w != 128) return;
    #define CELL(x, y) (m + map + (uint32_t)(((y) * w + (x)) * 4))
    #define EMPTY(c) (!(c)[2] && !(c)[0] && !(c)[1])
    for (int y = 1; y < n - 8; y++) {
        uint8_t *l = CELL(0, y), *r = CELL(n - 1, y);
        if (EMPTY(l)) { l[2] = 0xcc; l[3] |= 8; fixed++; }
        if (EMPTY(r)) { r[2] = 0xcc; r[3] |= 8; fixed++; }
    }
    for (int x = 0; x < n; x++) {
        uint8_t *b = CELL(x, n - 8);
        if (EMPTY(b) && (x < n / 2 - 2 || x > n / 2 + 2)) { b[2] = 0xcb; b[3] |= 8; fixed++; }
        if (x < n / 2 - 1 || x > n / 2 + 1) CELL(x, n - 9)[3] |= 8;
    }
    #undef CELL
    #undef EMPTY
    if (fixed) fprintf(stderr, "bigmap: the border's fence completed (%d pieces)\n", fixed);
}
void tp_bigmap_loaded(void) { setup_open = 0; repair_due = 1; }   /* (a saved park: its own size, from host.c) */
void tp_bigmap_setup_done(void) { if (repair_due) { repair_due = 0; repair_border(); } if (setup_open) { tp_bigmap_poll(); setup_open = 0; } }

/* At start-up, before the game allocates its blocks: the block's size; TP_MAP_W / TP_MAP_EXT force a layout
 * (development and tests: TP_MAP_ALLOC gives the block of another size, for the same memory layout). */
void tp_bigmap_init(void) {
    setup_open = armed = repair_due = 0;               /* (a new game, also the in-process restart: issue #6) */
    const char *w = getenv("TP_MAP_W"), *e = getenv("TP_MAP_EXT"), *a = getenv("TP_MAP_ALLOC");
    big_maps = getenv("TP_BIG_MAPS") && atoi(getenv("TP_BIG_MAPS")) > 0;
    int room = getenv("TP_BIG_ROOM") && atoi(getenv("TP_BIG_ROOM")) > 0;   /* (a saved 128x128 park exists: room to load it) */
    alloc_n = a ? atoi(a) : (big_maps || room || (w && atoi(w) == 128)) ? 128 : 64;
    if (alloc_n != 128) alloc_n = 64;
    wr32(tp_host_memory(), MAP_SIZE, (uint32_t)(alloc_n * alloc_n * 4));
    forced_things = getenv("TP_THINGS") ? atoi(getenv("TP_THINGS")) : 0;
    if (forced_things && (forced_things < GAME_THINGS || forced_things > 16000)) forced_things = 0;
    tp_things_alloc = forced_things ? forced_things : alloc_n == 128 ? BIG_THINGS : GAME_THINGS;   /* (the game takes ten fewer at a time if the heap is short) */
    tp_things_bytes = tp_things_alloc * 0xc8;
    if (forced_things) tp_things_n = (uint32_t)forced_things;
    tp_map_bytes = (uint32_t)(alloc_n * alloc_n * 4);
    if (w || e) tp_bigmap_set(w ? atoi(w) : 64, e ? atoi(e) : (w ? atoi(w) : 64));
    else if (big_maps) fprintf(stderr, "bigmap: room for 128x128 sandbox parks\n");
}

/* A new game (0x119eee, before the map is cleared and the entrance placed): 64x64 until the level is known. */
void tp_bigmap_new_park(void) {
    if (getenv("TP_MAP_W")) return;                           /* (a forced layout: tests) */
    setup_open = big_maps; armed = 0;
    int n = 64;                                               /* (the level is chosen later: tp_bigmap_poll) */
    { uint32_t tp_get(unsigned); uint32_t sp = tp_get(4); uint32_t ret; memcpy(&ret, tp_host_memory() + sp, 4);
      fprintf(stderr, "bigmap: new park (from %06x), level %d: %dx%d\n", ret, tp_host_memory()[0x1d66f1], n, n); }
    if ((int)tp_map_ext != n || (int)tp_map_w != n) tp_bigmap_set(n, n);
}
