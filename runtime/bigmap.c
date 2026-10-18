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
 * Test mode (TP_MAP_W=128 TP_MAP_EXT=64): the 64-wide park laid out with a 128 stride must play exactly
 * as the original; a missed or wrong site shows as a difference (docs/PLAN.md). */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

uint8_t *tp_host_memory(void);
extern uint32_t tp_map_w, tp_map_shift, tp_map_ext, tp_map_bytes;   /* (dispatch.c, generated: 64, 6, 64, 0x4000) */

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
};
#define NTABLES (int)(sizeof OFFSETS / sizeof OFFSETS[0])
static int16_t orig[NTABLES][29][26];
static int saved, alloc_n = 64;           /* the map block: alloc_n square, fixed at start-up */

static int16_t rd16(const uint8_t *m, uint32_t a) { int16_t v; memcpy(&v, m + a, 2); return v; }
static void wr16(uint8_t *m, uint32_t a, int16_t v) { memcpy(m + a, &v, 2); }
static void wr32(uint8_t *m, uint32_t a, uint32_t v) { memcpy(m + a, &v, 4); }

/* A cell offset of the 64-wide layout (dx + dy * 64, small dx) in a w-wide one. */
static int16_t recode(int v, int w) {
    if (!v) return 0;
    int dy = (v + 32 + 64 * 64) / 64 - 64, dx = v - dy * 64;
    return (int16_t)(dx + dy * w);
}
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
    tp_map_w = (uint32_t)w; tp_map_shift = w == 128 ? 7 : 6; tp_map_ext = (uint32_t)ext;
    if (w > alloc_n) { fprintf(stderr, "bigmap: no room for %dx%d (the map block is %dx%d)\n", w, w, alloc_n, alloc_n); w = ext = 64; }
    tp_map_bytes = (uint32_t)(alloc_n * alloc_n * 4);   /* (the block, cleared whole for a new park) */
    wr32(m, SAVE_SIZE, (uint32_t)(w * w * 4));           /* (a 64x64 park saves exactly as the game does) */
    for (int t = 0; t < NTABLES; t++) for (int k = 0; k < OFFSETS[t].n; k++) for (int i = 0; i < OFFSETS[t].nf; i++)
        wr16(m, OFFSETS[t].at + (uint32_t)(k * OFFSETS[t].size) + OFFSETS[t].f[i], recode(orig[t][k][i], w));
    fprintf(stderr, "bigmap: stride %d, playable %dx%d\n", w, ext, ext);
}

int tp_bigmap_alloc(void) { return alloc_n; }
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
    for (int k = 1; k < 1200; k++) {
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
void tp_bigmap_loaded(void) { setup_open = 0; }   /* (a saved park: its own size, from host.c) */
void tp_bigmap_setup_done(void) { if (setup_open) { tp_bigmap_poll(); setup_open = 0; } }

/* At start-up, before the game allocates its blocks: the block's size; TP_MAP_W / TP_MAP_EXT force a layout
 * (development and tests: TP_MAP_ALLOC gives the block of another size, for the same memory layout). */
void tp_bigmap_init(void) {
    const char *w = getenv("TP_MAP_W"), *e = getenv("TP_MAP_EXT"), *a = getenv("TP_MAP_ALLOC");
    big_maps = getenv("TP_BIG_MAPS") && atoi(getenv("TP_BIG_MAPS")) > 0;
    alloc_n = a ? atoi(a) : (big_maps || (w && atoi(w) == 128)) ? 128 : 64;
    if (alloc_n != 128) alloc_n = 64;
    wr32(tp_host_memory(), MAP_SIZE, (uint32_t)(alloc_n * alloc_n * 4));
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
