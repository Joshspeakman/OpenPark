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
static int saved;

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
    int alloc = getenv("TP_MAP_ALLOC") ? atoi(getenv("TP_MAP_ALLOC")) : w;   /* (tests: the same memory layout at any stride) */
    if (alloc < w) alloc = w;
    tp_map_bytes = (uint32_t)(alloc * alloc * 4);   /* (the block, cleared whole for a new park) */
    wr32(m, MAP_SIZE, (uint32_t)(alloc * alloc * 4)); wr32(m, SAVE_SIZE, (uint32_t)(alloc * alloc * 4));
    for (int t = 0; t < NTABLES; t++) for (int k = 0; k < OFFSETS[t].n; k++) for (int i = 0; i < OFFSETS[t].nf; i++)
        wr16(m, OFFSETS[t].at + (uint32_t)(k * OFFSETS[t].size) + OFFSETS[t].f[i], recode(orig[t][k][i], w));
    fprintf(stderr, "bigmap: stride %d, playable %dx%d\n", w, ext, ext);
}

/* At start-up, before the game allocates its blocks: TP_MAP_W / TP_MAP_EXT (development and tests). */
void tp_bigmap_init(void) {
    const char *w = getenv("TP_MAP_W"), *e = getenv("TP_MAP_EXT");
    if (w || e) tp_bigmap_set(w ? atoi(w) : 64, e ? atoi(e) : (w ? atoi(w) : 64));
}
