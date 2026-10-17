/* Native landscape: the first piece of Theme Park's renderer rebuilt in C
 * (docs/PLAN.md, "Engine rebuild", stage 3). It draws what the game's
 * render_park (0x11b837) draws into its landscape cache when the camera moves:
 * the ground block of every map cell, row by row, from the game's own data in
 * memory (map, block sprites, camera geometry), with a native sprite blitter.
 * It is checked byte for byte against the game's cache (TP_NATIVE_CHECK, view.c).
 *
 * Transcribed from MAIN.EXE (work/survey/render2.txt):
 *   0x11b8fe..0x11bd47  the landscape loop: for each of `rows` map rows from
 *       the camera, cells right to left, 48-pixel steps (word [0x1d663a]),
 *       staggered a third of a step per row; a cell's ground block is entry
 *       (byte 2 + 1) of the 6-byte block table [0x1d3b90], drawn so its foot
 *       is on the row line; cells off the 64x64 map get block 0 (grass);
 *       flag bits of byte 3 add decorations (not yet: they are reported)
 *   0x15ec9c / 0x15ed21  the sprite blitter: {pointer, width, height} entry;
 *       in 320x200 (word [0x1de5f6] bit 0) positions and the table's doubled
 *       sizes are halved; Bullfrog RLE (n > 0 copy n, n < 0 skip -n, 0 = end
 *       of row), clipped to the display window ([0x1de5c8] x [0x1de5f0]).
 * Coordinates are in the game's 640-wide space. */
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#define MAP_PTR     0x1d3b08u   /* 64x64 cells of 4 bytes */
#define BLOCK_TABLE 0x1d3b90u   /* MBLK: 6-byte entries {u32 data, u8 width, u8 height}, doubled sizes */
#define TILE_W      0x1d663au   /* word: 48 */
#define TILE_H      0x1d6654u   /* word: row height */
#define VIEW_OFF    0x1d6624u   /* int16: the view's x offset */
#define SCREEN_MODE 0x1de5f6u
#define BLIT_FLAGS  0x1becbcu   /* word: mirror/ghost/scale variants of the blitter */
#define FRAMES      0x1d3b80u   /* composite sprites: 8-byte frames, word 0 = first element */
#define ELEMENTS    0x1d3b7cu   /* 10-byte elements {sprite offset, dx, dy, flags, next} */
#define SPRITES     0x1d3bb8u   /* MSPR: 6-byte sprite entries (as the block table) */
#define DECO_TABLE  0x1d3b38u   /* word tables: frames for decorations (+0x252 by bit, +0x4ae by kind) */
#define FRAME_MODE  0x1d667cu   /* word: special frame drawing modes (0 = plain) */

static const uint8_t *M;
static uint32_t r32(uint32_t a) { uint32_t v; memcpy(&v, M + a, 4); return v; }
static int16_t r16(uint32_t a) { int16_t v; memcpy(&v, M + a, 2); return v; }

typedef struct { uint8_t *buf; int pitch, w, h; int lowres; } Target;

static int unsupported;                             /* blitter variants met that are not native yet */

/* One sprite (a table entry) at (x, y) in the game's coordinates, with the
 * blitter's flags: bit 0 mirrors (rows written leftwards from x + width), bit 1
 * flips (rows upwards from y + height), as 0x15ed21 does, one past the edge. */
static void blit(const Target *t, uint32_t entry, int x, int y, int flags) {
    uint32_t data = r32(entry); int w = M[entry + 4], h = M[entry + 5];
    if (!h) return;
    if (flags & ~3) unsupported++;
    if (t->lowres) { w >>= 1; h >>= 1; x >>= 1; y >>= 1; }
    int dx = flags & 1 ? -1 : 1, x0 = flags & 1 ? x + w : x;
    int dy = flags & 2 ? -1 : 1, y0 = flags & 2 ? y + h : y;
    const uint8_t *s = M + data;
    for (int row = 0; row < h; row++) {
        int px = x0, py = y0 + dy * row;
        for (;;) {
            int8_t n = (int8_t)*s++;
            if (n == 0) break;
            if (n < 0) { px -= n * dx; continue; }
            if (py >= 0 && py < t->h)
                for (int k = 0; k < n; k++) { int xx = px + dx * k; if (xx >= 0 && xx < t->w) t->buf[py * t->pitch + xx] = s[k]; }
            s += n; px += n * dx;
        }
    }
}

/* A composite sprite (0x1209b5 in its plain mode): the frame's elements, each a
 * sprite at an offset with its own blitter flags. */
static void frame(const Target *t, int f, int x, int y) {
    if ((uint16_t)r16(FRAME_MODE)) { unsupported++; return; }
    uint32_t frames = r32(FRAMES), elems = r32(ELEMENTS), sprites = r32(SPRITES);
    uint32_t e = elems + (uint32_t)(uint16_t)r16(frames + (uint32_t)f * 8) * 10;
    for (int guard = 0; e > elems && guard < 256; guard++) {
        uint32_t spr = sprites + (uint16_t)r16(e);
        if (spr > sprites) blit(t, spr, (int16_t)(x + r16(e + 2)), (int16_t)(y + r16(e + 4)), (uint16_t)r16(e + 6));
        e = elems + (uint32_t)(uint16_t)r16(e + 8) * 10;
    }
}

/* What render_park draws into the landscape cache for camera (cx0, cy0), into buf.
 * Returns the number of cells whose decorations were left out (byte 3 bits). */
int tp_native_landscape(const uint8_t *mem, int cx0, int cy0, int rows, uint8_t *buf, int pitch, int w, int h) {
    M = mem;
    Target t = {buf, pitch, w, h, r16(SCREEN_MODE) & 1};
    int tw = (uint16_t)r16(TILE_W), th = (uint16_t)r16(TILE_H), off = r16(VIEW_OFF);
    uint32_t map = r32(MAP_PTR), table = r32(BLOCK_TABLE);
    if (!tw || !th) return -1;
    uint32_t base = map + (uint32_t)(((cy0 << 6) + cx0) * 4);
    int skipped = 0; unsupported = 0;
    for (int r = 0, y = cy0; r < rows; r++, y++, base += 0x100) {
        int left = off + (tw / 3) * r - (r / 3 + 1) * tw;
        int x = (tw / 3) * r + 0x280 - (r / 3 + 1) * tw;
        int cx = (cx0 - (r / 3 + 1)) + (0x280 - off) / tw;
        uint32_t p = base - (uint32_t)((r / 3) * 4 + 4) + (uint32_t)(((0x280 - off) / tw) * 4);
        for (; (int16_t)x >= (int16_t)left; x -= tw, p -= 4, cx--) {
            if (x < off - tw) continue;
            if ((int16_t)cx >= 0 && (int16_t)cx < 0x40 && (int16_t)y >= 0 && (int16_t)y < 0x40) {
                uint32_t e = table + (uint32_t)(M[p + 2] + 1) * 6;
                int ys = (int16_t)(r * th + th) - M[e + 5];
                blit(&t, e, (int16_t)x, (int16_t)ys, 0);
                /* decorations (0x11bb21..0x11bc9e): bits 0-2 at a pseudo-random place in the tile,
                 * bit 5 a kind (top two bits) half a tile across and 16 down from the block's top */
                int cell = (int)((p - map) >> 2), bits = M[p + 3] & 0x3f;
                for (int i = 0; bits; i++, bits >>= 1) {
                    if (!(bits & 1)) continue;
                    if (i <= 2) {
                        int fx = (int16_t)(x + (cell + M[p + 2] + i * 16) % tw);
                        int fy = (int16_t)(r * th + (cell + i * 16) % th);
                        frame(&t, (uint16_t)r16(r32(DECO_TABLE) + 0x252 + i * 2), fx, fy);
                    } else if (i == 5) {
                        int hires = r16(SCREEN_MODE) == 8;
                        int fy = (int16_t)(ys + (hires ? 8 : 0x10));
                        int fx = (int16_t)(x + ((tw << 7) >> 8) + (hires ? 4 : 8));
                        frame(&t, (uint16_t)r16(r32(DECO_TABLE) + 0x4ae + (M[p + 3] >> 6) * 2), fx, fy);
                    }
                }
            } else {
                uint32_t e = table + 6;                      /* block 0 off the map */
                int ys = (int16_t)(r * th + th) - M[table + 0xb];
                blit(&t, e, (int16_t)x, (int16_t)ys, 0);
            }
        }
    }
    return skipped + unsupported;
}

int tp_native_blit_flags(const uint8_t *mem) { M = mem; return (uint16_t)r16(BLIT_FLAGS); }

/* ---- things (stage 3b, in progress) ----
 * The second loop of render_park (0x11be5a..0x11c015) walks the cells as the
 * landscape loop does and calls 0x11c22a(first thing, x, row * height) for each
 * cell with things. 0x11c22a follows the cell's list (word 2 = next) and draws
 * each thing by its class (byte 4 of the 200-byte records at [0x1d3b10]).
 * Native so far: static scenery (negative index: one frame from word table
 * 0x1b44c1) and class 24 (an animated frame, word 10). Draw calls go to a log
 * (development) so they can be compared with the game's (view.c). */
#define THINGS      0x1d3b10u
#define STATIC_TAB  0x1b44c1u
typedef void (*DrawLog)(int kind, int a, int b, int c, uint32_t d, int e, void *ctx);
static DrawLog dlog; static void *dctx;
static void log_blit(uint32_t entry, int x, int y, int flags) {
    if (dlog) dlog(2, x, y, M[entry + 4] | M[entry + 5] << 8, r32(entry), flags, dctx);
}
static void frame_logged(int f, int x, int y) {
    uint32_t frames = r32(FRAMES), elems = r32(ELEMENTS), sprites = r32(SPRITES);
    uint32_t e = elems + (uint32_t)(uint16_t)r16(frames + (uint32_t)f * 8) * 10;
    for (int guard = 0; e > elems && guard < 256; guard++) {
        uint32_t spr = sprites + (uint16_t)r16(e);
        if (spr > sprites) log_blit(spr, (int16_t)(x + r16(e + 2)), (int16_t)(y + r16(e + 4)), (uint16_t)r16(e + 6));
        e = elems + (uint32_t)(uint16_t)r16(e + 8) * 10;
    }
}
/* The renderer advances animations as it draws (frame word 6 = the next frame). The native
 * renderer leaves the game's state alone and computes what the game would draw instead. */
static int next_frame(int f) { return (uint16_t)r16(r32(FRAMES) + (uint32_t)f * 8 + 6); }
static int tick_is(int mask) { return (M[0x1d3afc] & mask) == 0; }
static int anim_77a4;                                   /* 0x1d77a4: one animation shared by class 8 sub-kind 0x3f */

/* 0x11feb5: the cars of a ride on top of its stack (class 0x12 sub-kinds 0x13, 0x15): the frame's
 * first element h above (x, y), then from there each car in the chain (word 0x7a) as the
 * frame's other elements, the car frame (word 0x90 when the first car's byte 0x12 > 1) stepped
 * on by the car's word 0x7c, each seat showing the car's rider (word 0x47) as class 0x0e does;
 * kind 1 adds a part (deco 0x2dc). Unchecked sprite offsets are drawn as the game does. */
static int train_draw(uint32_t t, int x, int y, int h) {
    uint32_t frames = r32(FRAMES), elems = r32(ELEMENTS), sprites = r32(SPRITES), things = r32(THINGS), deco = r32(DECO_TABLE);
    uint32_t fr = frames + (uint32_t)(uint16_t)r16(t + 0xa) * 8, e0 = elems + (uint32_t)(uint16_t)r16(fr) * 10;
    if (M[FRAME_MODE] & 8) return 0;                                  /* (the game draws nothing in scaled mode) */
    log_blit(sprites + (uint16_t)r16(e0), (int16_t)(x + r16(e0 + 2)), (int16_t)(y + h + r16(e0 + 4)), (uint16_t)r16(e0 + 6));
    y += h;
    if (M[things + (uint32_t)(uint16_t)r16(t + 0x7a) * 0xc8 + 0x12] > 1) fr = frames + (uint32_t)(uint16_t)r16(t + 0x90) * 8;
    int guard = 0;
    for (uint32_t car = things + (uint32_t)(uint16_t)r16(t + 0x7a) * 0xc8; car > things; car = things + (uint32_t)(uint16_t)r16(car + 0x7a) * 0xc8) {
        if (++guard > 256) return 0x413;
        uint32_t f2 = fr;
        for (int k = (uint16_t)r16(car + 0x7c); (uint16_t)k && r16(f2 + 6); k--) f2 = frames + (uint32_t)(uint16_t)r16(f2 + 6) * 8;
        uint32_t e = elems + (uint32_t)(uint16_t)r16(elems + (uint32_t)(uint16_t)r16(f2) * 10 + 8) * 10;
        for (int g = 0; e > elems && g < 256; e = elems + (uint32_t)(uint16_t)r16(e + 8) * 10, g++) {
            int fl = (uint16_t)r16(e + 6);
            if (fl & 0x80) {
                uint32_t rider = things + (uint32_t)(uint16_t)r16(car + 0x47) * 0xc8;
                if (rider <= things) continue;
                if (M[rider + 0x59]) {
                    for (int i = 0; i < 0x2f; i++) if (r16(0x1b9a78 + (uint32_t)i * 2) == r16(e)) {
                        uint32_t ns = sprites + (uint16_t)r16(0x1b9a78 + (uint32_t)M[rider + 0x59] * 0x5e + (uint32_t)i * 2), os = sprites + (uint16_t)r16(e);
                        int bx = (int16_t)x - (M[ns + 4] >> 1) + (M[os + 4] >> 1), by = y - M[ns + 5] + M[os + 5];
                        log_blit(ns, (int16_t)(bx + r16(e + 2)), (int16_t)(by + r16(e + 4)), fl);
                        break;
                    }
                } else {
                    uint32_t s = sprites + (uint16_t)r16(e);
                    if (s > sprites) log_blit(s, (int16_t)(x + r16(e + 2)), (int16_t)(y + r16(e + 4)), fl);
                }
            } else log_blit(sprites + (uint16_t)r16(e), (int16_t)(x + r16(e + 2)), (int16_t)(y + r16(e + 4)), fl);
        }
    }
    if (r16(t + 0x16) == 1) {
        uint32_t e = elems + (uint32_t)(uint16_t)r16(frames + (uint32_t)(uint16_t)r16(deco + 0x2dc) * 8) * 10;
        log_blit(sprites + (uint16_t)r16(e), (int16_t)(x + r16(e + 2)), (int16_t)(y + r16(e + 4)), (uint16_t)r16(e + 6));
    }
    return 0;
}
/* Class 0x12 (0x11f205): a base frame, a stack of repeated frames (count byte 0x79,
 * a step per level), then the animated top frame (word 10); kind word 0x16, sub-kind
 * byte 0x44, from the record table at 0x1b4399. Returns nonzero if a part is not native. */
static int class12(uint32_t t, int x, int y) {
    int tw = (uint16_t)r16(TILE_W), th = (uint16_t)r16(TILE_H);
    int xx = (int16_t)(tw / 2 + (int16_t)x), yy = (int16_t)(th / 2 + (int16_t)y);
    int step = (M[SCREEN_MODE] & 8) ? 0xc : 0x18;
    int div = (M[FRAME_MODE] & 8) ? (uint16_t)r16(0x1d6642) : 1;
    uint32_t deco = r32(DECO_TABLE), map = r32(MAP_PTR);
    int cellb = M[map + (uint32_t)((M[t + 9] * 64 + M[t + 7]) * 4) + 2];
    int q = (uint16_t)r16(0x1b44d9 + (uint32_t)cellb * 4);
    int typ = (uint16_t)r16(t + 0x16), sub = M[t + 0x44];
    if (typ == 1 || typ == 2) {
        if (!sub) { frame_logged((uint16_t)r16(t + 0xa), xx, yy); return 0; }
        uint32_t rec = r32(0x1b4399 + (uint32_t)typ * 10) + (uint32_t)sub * 0x19;
        int f8 = (uint16_t)r16(deco + (uint32_t)(r16(rec + 2) * 2));
        if (q == 0 || (q & 0x400)) frame_logged((uint16_t)r16(deco + (uint32_t)(r16(rec) * 2)), xx, yy);
        else frame_logged(f8, xx, (int16_t)(yy + 0xc));
        int n = (uint16_t)((sub == 0xc || sub == 0xf) ? M[t + 0x79] - 1 : M[t + 0x79]);
        int i = 0;
        for (; (uint16_t)i < (uint16_t)n && i < 4096; i++) frame_logged(f8, xx, (int16_t)(yy - (i * step) / div));
        if (M[t + 0xc] == 0x13 || M[t + 0xc] == 0x15) return train_draw(t, xx, yy, (int16_t)-(((uint16_t)i + 1) * step / div));
        if (r16(t + 0xa)) {
            int f = (uint16_t)r16(t + 0xa); if (tick_is(3)) f = next_frame(f);
            frame_logged(f, xx, (int16_t)(yy - ((M[t + 0x79] + 1) * step) / div));
        }
        return 0;
    }
    if (typ == 3) {
        if (sub) {
            if (q == 0 || (q & 0x400)) frame_logged((uint16_t)r16(deco + 0x3aa), xx, yy);
            if (r16(t + 0xa)) frame_logged((uint16_t)r16(t + 0xa), xx, (int16_t)(yy - (step * 2) / div));
        } else frame_logged((uint16_t)r16(deco + 0x562), xx, yy);
    }
    return 0;
}
/* 0x1204b9: the cell's first thing of class `cls`, or 0. */
static uint32_t find_in_cell(uint32_t cell, int cls) {
    if (cell < r32(MAP_PTR) || cell >= r32(0x1d3b04)) return 0;
    uint32_t things = r32(THINGS);
    for (uint32_t t = things + (uint32_t)(r16(cell) * 0xc8); t > things; t = things + (uint32_t)(r16(t + 2) * 0xc8))
        if (M[t + 4] == cls) return t;
    return 0;
}
/* Class 2, people (0x11c9e2): by sub-kind (byte 12, a key table at 0x11c9ee); most use the
 * common path 0x11cf61: their place in the tile (bytes 6, 8), the selection ring, the
 * figure (word 10), carried items (bits of word 0x52, advanced by word 0x8c), a thought
 * (byte 0xb9), the arrow over the selected one. Returns nonzero if a part is not native. */
static int class2(uint32_t t, int x, int y) {
    int sub = M[t + 0xc], tw = (uint16_t)r16(TILE_W), hires = r16(SCREEN_MODE) == 8;
    uint32_t deco = r32(DECO_TABLE), frames = r32(FRAMES), map = r32(MAP_PTR), things = r32(THINGS);
    static const uint8_t common[] = {0x02, 0x04, 0x06, 0x08, 0x0a, 0x0c, 0x14, 0x18, 0x1a, 0x1e, 0x20, 0x22, 0x28, 0x2a, 0x2c, 0x2e,
                                     0x34, 0x38, 0x3a, 0x3c, 0x3e, 0x40, 0x42, 0x4a, 0x4c};   /* (0x08, 0x18, 0x1a play sounds first) */
    int is_common = 0;
    for (unsigned k = 0; k < sizeof common; k++) if (common[k] == sub) is_common = 1;
    if (sub == 0x10) {                                           /* 0x11cab7: an offset from a direction table, then common */
        int d = (int)((int16_t)r16(t + 0x14) * M[t + 0x13]) / 64;
        int k = (M[t + 0x12] * 4 + (int16_t)d) * 2;
        x = (int16_t)(x + (int8_t)M[0x1b16d8 + k]); y = (int16_t)(y + (int8_t)M[0x1b16d9 + k]);
        is_common = 1;
    } else if (sub == 0x12) {                                    /* 0x11cb13: only on a cell of kind 0x40 */
        uint32_t cell = map + (uint32_t)((M[t + 9] * 64 + M[t + 7]) * 4);
        if ((uint16_t)r16(0x1b44d9 + (uint32_t)M[cell + 2] * 4) != 0x40) return 0;
        is_common = 1;
    }
    int px = (int16_t)(x + ((tw * M[t + 6]) >> 8) + (M[t + 8] >> (hires ? 5 : 4)));
    int py = (int16_t)(y + (hires ? (M[t + 8] >> 3) >> 1 : M[t + 8] >> 3));
    if (!is_common) {
        if (sub == 0x36) {                                       /* 0x11cb51: the figure with its own sprites in the seats */
            uint32_t elems = r32(ELEMENTS), sprites = r32(SPRITES);
            int scaled = r16(FRAME_MODE) == 8;                   /* (then the game draws nothing here) */
            for (uint32_t e = elems + (uint32_t)(uint16_t)r16(frames + (uint32_t)(uint16_t)r16(t + 0xa) * 8) * 10, g = 0; e > elems && g < 256;
                 e = elems + (uint32_t)(uint16_t)r16(e + 8) * 10, g++) {
                int fl = (uint16_t)r16(e + 6);
                if (fl & 0x80) {
                    for (int i = 0; i < 0x2f; i++) if (r16(0x1b9a78 + (uint32_t)i * 2) == r16(e)) {
                        if (!scaled) {
                            uint32_t ns = sprites + (uint16_t)r16(0x1b9a78 + (uint32_t)M[t + 0x59] * 0x5e + (uint32_t)i * 2), os = sprites + (uint16_t)r16(e);
                            int bx = (int16_t)px - (M[ns + 4] >> 1) + (M[os + 4] >> 1), by = py - M[ns + 5] + M[os + 5];
                            log_blit(ns, (int16_t)(bx + r16(e + 2)), (int16_t)(by + r16(e + 4)), fl);
                        }
                        break;
                    }
                } else if (!scaled) log_blit(sprites + (uint16_t)r16(e), (int16_t)(px + r16(e + 2)), (int16_t)(py + r16(e + 4)), fl);
            }
            return 0;
        }
        if (sub == 0x32) {                                       /* 0x11d4c6: the figure raised by word 0x5f */
            int h = (uint16_t)r16(t + 0x5f);
            frame_logged((uint16_t)r16(t + 0xa), px, (int16_t)(py - ((M[SCREEN_MODE] & 8) ? h >> 1 : h)));
            return 0;
        }
        if (sub == 0x44 || sub == 0x46 || sub == 0x48)           /* 0x11d5c5, 0x11d764: a scaled frame (0x167182): not native */
            return r16(FRAME_MODE) == 8 ? 0 : 0x302;
        return 0;                                                /* (0x4e plays a sound; 0x75 and unknown keys draw nothing) */
    }
    if (sub == 0xc && find_in_cell(map + (uint32_t)((M[t + 9] * 64 + M[t + 7]) * 4), 0x1a)) py = (int16_t)(py - 8);
    if ((M[0x1b14cc] & 7) && (M[t + 5] & 0x80)) frame_logged((uint16_t)r16(deco + 0x46c), px, py);
    frame_logged((uint16_t)r16(t + 0xa), px, py);
    int items = (uint16_t)r16(t + 0x52);
    if (items) {
        int counter = (uint16_t)r16(t + 0x8c);                  /* (stepped after drawing, every 8th tick) */
        for (int i = 0; items; i++, items >>= 1) {
            if (!(items & 1) || !r16(0x1b53a2 + (uint32_t)i * 2)) continue;
            uint32_t f = frames + (uint32_t)(uint16_t)r16(deco + (uint32_t)(uint16_t)r16(0x1b53a2 + (uint32_t)i * 2) * 2) * 8;
            for (int k = (uint16_t)counter % 4; k && r16(f + 6); k--) f = frames + (uint32_t)(uint16_t)r16(f + 6) * 8;
            frame_logged((int)((f - frames) >> 3) & 0xffff, px, py);
        }
    }
    if (M[t + 0xb9]) frame_logged((uint16_t)r16(deco + (uint32_t)(uint16_t)r16(t + 0xb7) * 2), px, py);
    int missing = 0;
    uint32_t sel = things + (uint32_t)(uint16_t)r16(0x1d6678) * 0xc8;
    if (sel == t) {
        uint32_t fr = frames + (uint32_t)(uint16_t)r16(t + 0xa) * 8;
        frame_logged((uint16_t)r16(deco + 0x284), px, (int16_t)(py - M[fr + 3]));
        if (r16(0x1d6678) && !r16(0x1d6652) && r16(0x1d6684)) missing = 0x2fe;   /* debug numbers: not native */
    }
    return missing;
}
/* Class 8, staff (0x11e9a7): placed like people; sub-kind (byte 12, key table 0x11e9b3)
 * 0x3f has a shared extra animation, 0x0c..0x10 also play a sound (not drawing), the rest
 * a marker (deco 0x500) for some jobs, the selection arrow, a mark for sub-kind 0x3c. */
static int class8(uint32_t t, int x, int y) {
    int sub = M[t + 0xc], tw = (uint16_t)r16(TILE_W), hires = r16(SCREEN_MODE) == 8;
    uint32_t deco = r32(DECO_TABLE), frames = r32(FRAMES), things = r32(THINGS);
    int px = (int16_t)(x + ((tw * M[t + 6]) >> 8) + (M[t + 8] >> (hires ? 5 : 4)));
    int py = (int16_t)(y + (hires ? (M[t + 8] >> 3) >> 1 : M[t + 8] >> 3));
    if (sub == 0x3f) {
        frame_logged((uint16_t)r16(t + 0xa), px, py);
        if ((int16_t)r16(t + 0x14) > 0x708) {
            if (!anim_77a4) anim_77a4 = (uint16_t)r16(deco + 0x52a);
            else if (tick_is(3)) anim_77a4 = next_frame(anim_77a4);
            frame_logged(anim_77a4, px, py);
        }
        return 0;
    }
    if (!(sub >= 2 && sub <= 0x3e && !(sub & 1) && sub != 0x20 && sub != 0x28 && sub != 0x2a)) return 0;
    frame_logged((uint16_t)r16(t + 0xa), px, py);
    if (r16(0x1d35ec + (uint32_t)M[t + 0x59] * 0x36) == 0x16 && (int16_t)r16(t + 0x9c) != -1)
        frame_logged((uint16_t)r16(deco + 0x500), px, py);
    if (things + (uint32_t)(uint16_t)r16(0x1d6678) * 0xc8 == t)
        frame_logged((uint16_t)r16(deco + 0x284), px, (int16_t)(py - M[frames + (uint32_t)(uint16_t)r16(t + 0xa) * 8 + 3]));
    else if (sub == 0x3c) frame_logged((uint16_t)r16(deco + 0x4a0), px, py);
    return 0;
}
/* 0x120beb: elements [skip, skip + count) of a frame. */
static void frame_part(int f, int skip, int count, int x, int y) {
    uint32_t frames = r32(FRAMES), elems = r32(ELEMENTS), sprites = r32(SPRITES);
    uint32_t e = elems + (uint32_t)(uint16_t)r16(frames + (uint32_t)f * 8) * 10;
    for (; e > elems && (uint16_t)skip; skip--) e = elems + (uint32_t)(uint16_t)r16(e + 8) * 10;
    if (r16(FRAME_MODE)) return;                                     /* (other modes: not native) */
    for (; e > elems && (uint16_t)count; count--) {
        uint32_t spr = sprites + (uint16_t)r16(e);
        if (spr > sprites) log_blit(spr, (int16_t)(x + r16(e + 2)), (int16_t)(y + r16(e + 4)), (uint16_t)r16(e + 6));
        e = elems + (uint32_t)(uint16_t)r16(e + 8) * 10;
    }
}
/* 0x13f49e with flag 0: text in the sprite font ([0x1d3b64], character map 0x1b51c1), each
 * glyph in one colour (0x111bd5); punctuation sits lower. */
static void text_solid(int x, int y, const uint8_t *str, int colour) {
    uint32_t font = r32(0x1d3b64); int lo = M[SCREEN_MODE] & 1;
    for (; *str; str++) {
        if (*str == ' ') { x = (int16_t)(x + (lo ? 8 : 4)); continue; }
        int g = M[0x1b51c1 + *str];
        if (g) {
            int dy = 0;
            if (*str == ',' || *str == '.') dy = M[0x1d7765] - M[font + (uint32_t)g * 6 + 5];
            else if (*str == '-') dy = M[0x1d7765] / 2;
            else if (*str == '+' || *str == ':') dy = 2;
            if (dlog) dlog(5, (uint16_t)x, (uint16_t)(y + dy), colour, font + (uint32_t)g * 6, 0, dctx);
            log_blit(font + (uint32_t)g * 6, (int16_t)x, (int16_t)(y + dy), 0x40);   /* (0x111bd5 blits it, flags 0x40: one colour) */
        }
        x = (int16_t)(x + M[font + (uint32_t)g * 6 + 4] + (lo ? 2 : 1));
    }
}
static int cell_things(int idx, int x, int y);
static uint32_t rng;                                    /* a copy of the game's generator (0x118b92, seed 0x1d3af4) */
static int rand15(void) { rng *= 0xbb40e62du; return (int)((rng >> 8) & 0x7fff); }
static int idiv_trunc(int a, int b) { return a / b; }   /* (C division truncates, as idiv) */
/* Class 0x0e, ride cars (0x11dd27). Sub-kinds 8, 0x0a, 0x22: the car frame placed by the ride
 * (class 0x12 in its cell) with each rider element swapped for the rider's sprite (table
 * 0x1b9a78, 47 sprites per kind of visitor); 0x14..0x18: a train of frames; 0x20: one frame
 * without riders. Sounds and click tests along the way are not drawing. */
static int classE(uint32_t t, int x, int y) {
    int sub = M[t + 0xc], tw = (uint16_t)r16(TILE_W), hires = r16(SCREEN_MODE) == 8, missing = 0;
    uint32_t deco = r32(DECO_TABLE), frames = r32(FRAMES), elems = r32(ELEMENTS), sprites = r32(SPRITES);
    uint32_t map = r32(MAP_PTR), things = r32(THINGS);
    int place_x = (tw * M[t + 6] >> 8) + (M[t + 8] >> (hires ? 5 : 4));
    int place_y = hires ? (M[t + 8] >> 3) >> 1 : M[t + 8] >> 3;
    if (sub == 8 || sub == 0xa || sub == 0x22) {
        int16_t off = 0;
        if (sub != 8) {
            uint32_t parent = things + (uint32_t)(uint16_t)r16(t + 0x16) * 0xc8;
            if ((uint16_t)r16(parent + 0x2e) > M[0x1ccf78 + (uint16_t)r16(parent + 0x16)]) off = (int16_t)(off - (rand15() & 3));
        }
        uint32_t ride = find_in_cell(map + (uint32_t)((M[t + 9] * 64 + M[t + 7]) * 4), 0x12);
        if (!ride) ride = find_in_cell(map + (uint32_t)((int16_t)r16(t + 0x10) * 4), 0x12);
        uint32_t rider = things + (uint32_t)(uint16_t)r16(t + 0x47) * 0xc8;
        if (!ride) {                                             /* no ride: the rider alone */
            if (rider <= things) return 0;
            int a = M[rider + 6] - M[rider + 8], b = M[rider + 6] + M[rider + 8];
            int px = (int16_t)(x + 2 * idiv_trunc(a, 16) + 0x20), py = (int16_t)(y + 2 * idiv_trunc(b, 32));
            frame_logged((uint16_t)r16(deco + (uint32_t)((uint16_t)r16(rider + 0xa) + M[t + 0x12]) * 2), px, py);
            return 0;
        }
        uint32_t rec = r32(0x1b4399 + (uint32_t)(uint16_t)r16(ride + 0x16) * 10) + (uint32_t)M[ride + 0x44] * 0x19;
        int f = (uint16_t)r16(deco + (uint32_t)(uint16_t)r16(rec + (uint32_t)M[t + 0x12] * 2 + 5) * 2);
        x = (int16_t)(x + place_x); y = (int16_t)(y + place_y);
        off = (int16_t)(off - idiv_trunc((int8_t)M[rec + 0x12] * M[t + 6], 256));
        off = (int16_t)(off - idiv_trunc((int8_t)M[rec + 0x13] * M[t + 8], 256));
        off = (int16_t)(off + (int8_t)M[rec + 0x14]);
        off = (int16_t)(off - (r16(ride + 0x16) == 3 ? 0x18 : M[ride + 0x79] * 0x18));
        off = (int16_t)idiv_trunc(off, (M[FRAME_MODE] & 8) ? (uint16_t)r16(0x1d6642) : 1);
        if (hires) off = (int16_t)(off / 2);
        y = (int16_t)(y + off);
        for (uint32_t e = elems + (uint32_t)(uint16_t)r16(frames + (uint32_t)f * 8) * 10, guard = 0; e > elems && guard < 256;
             e = elems + (uint32_t)(uint16_t)r16(e + 8) * 10, guard++) {
            int fl = (uint16_t)r16(e + 6);
            if (fl & 0x80) {                                     /* a seat: the next rider's sprite */
                if (rider <= things) continue;
                if (r16(ride + 0x16) == 3 && sub != 0x22) { rider = things + (uint32_t)(uint16_t)r16(rider + 0x47) * 0xc8; continue; }
                for (int i = 0; i < 0x2f; i++) if (r16(0x1b9a78 + (uint32_t)i * 2) == r16(e)) {
                    if (r16(FRAME_MODE) != 8) {
                        uint32_t ns = sprites + (uint16_t)r16(0x1b9a78 + (uint32_t)M[rider + 0x59] * 0x5e + (uint32_t)i * 2);
                        uint32_t os = sprites + (uint16_t)r16(e);
                        int bx = (M[os + 4] >> 1) + ((int16_t)x - (M[ns + 4] >> 1)), by = y - M[ns + 5] + M[os + 5];
                        log_blit(ns, (int16_t)(bx + r16(e + 2)), (int16_t)(by + r16(e + 4)), fl);
                    }
                    break;
                }
                rider = things + (uint32_t)(uint16_t)r16(rider + 0x47) * 0xc8;
            } else {
                if ((uint16_t)r16(e) == 0xba) continue;
                if (r16(FRAME_MODE) == 8) { missing = 0x30e; continue; }   /* scaled (0x166dfa): not native */
                log_blit(sprites + (uint16_t)r16(e), (int16_t)(x + r16(e + 2)), (int16_t)(y + r16(e + 4)), fl);
            }
        }
        return missing;
    }
    if (sub == 0x14 || sub == 0x16 || sub == 0x18) {
        x = (int16_t)(x + place_x); y = (int16_t)(y + place_y);
        uint32_t tab = 0x1b7870 + (uint32_t)(uint16_t)r16(t + 0xa) * 6;
        int f0 = (uint16_t)r16(deco + (uint32_t)(uint16_t)r16(tab) * 2), f1 = (uint16_t)r16(deco + (uint32_t)(uint16_t)r16(tab + 2) * 2);
        int f2 = (uint16_t)r16(deco + (uint32_t)(uint16_t)r16(tab + 4) * 2), xx = (int16_t)x;
        frame_logged(f0, xx, y);
        for (int i = 0; i < (uint16_t)r16(t + 0x26); i++) {
            frame_logged(f1, xx, y);
            int w = M[frames + (uint32_t)f1 * 8 + 2];
            xx = (int16_t)(xx + ((M[FRAME_MODE] & 8) ? w / (uint16_t)r16(0x1d6640) : w));
        }
        frame_logged(f2, xx, y);
        return 0;
    }
    if (sub == 0x20) {
        x = (int16_t)(x + place_x); y = (int16_t)(y + place_y - r16(t + 0x98));
        uint32_t parent = things + (uint32_t)(uint16_t)r16(t + 0x16) * 0xc8;
        int f = (uint16_t)r16(deco + (r16(0x1cced0 + (uint32_t)(uint16_t)r16(parent + 0x16) + 0x22) == 0xdb ? 0x3c2 : 0x146));
        for (uint32_t e = elems + (uint32_t)(uint16_t)r16(frames + (uint32_t)f * 8) * 10, guard = 0; e > elems && guard < 256;
             e = elems + (uint32_t)(uint16_t)r16(e + 8) * 10, guard++)
            if (!(r16(e + 6) & 0x80)) log_blit(sprites + (uint16_t)r16(e), (int16_t)(x + r16(e + 2)), (int16_t)(y + r16(e + 4)), (uint16_t)r16(e + 6));
    }
    return 0;
}
/* Class 0x14, the entrance (0x11f8f8): sub-kind 2 walks like people; 6 is the gate with the
 * park's name scrolling over it (and the cells four to either side drawn again over it);
 * 8 a frame with up to three parts; the rest one frame from the deco table. */
static int class14(uint32_t t, int x, int y) {
    int sub = M[t + 0xc], tw = (uint16_t)r16(TILE_W), hires = r16(SCREEN_MODE) == 8;
    uint32_t deco = r32(DECO_TABLE), map = r32(MAP_PTR);
    int f = (uint16_t)r16(t + 0xa), missing = 0;
    if (sub == 2) {
        int px = (int16_t)(x + ((tw * M[t + 6]) >> 8) + (M[t + 8] >> (hires ? 5 : 4)));
        int py = (int16_t)(y + (hires ? (M[t + 8] >> 3) >> 1 : M[t + 8] >> 3));
        frame_logged(f, px, py);
    } else if (sub == 6) {
        int px = (int16_t)(x + ((tw << 7) >> 8) + (hires ? 4 : 8)), py = (int16_t)(y + (hires ? 8 : 0x10));
        int lit = M[r32(0x1d3818) + 0x18] != 0;
        frame_logged((uint16_t)r16(deco + (lit ? (f == 0x13f ? 0x27a : 0x27c) : (f == 0x13f ? 0x27e : 0x440))), px, py);
        if (!r16(0x1d6652)) {
            uint32_t cell = map + (uint32_t)((M[t + 9] * 64 + M[t + 7]) * 4);
            if (f == 0x13f) {
                uint32_t str = lit ? r32(0x1d3814) + 0x20 : r32(r32(0x1d7bd0) + 0x19c);
                int len = 0; while (len < 4096 && M[str + len]) len++;
                if (len) {
                    uint8_t win[13]; int k = (uint16_t)r16(0x1d6656) % len;
                    for (int i = 0; i < 12; i++, k = (k + 1) % len) win[i] = M[str + k];
                    win[12] = 0;
                    int dx = 0x1c, dy = -100; if (M[SCREEN_MODE] & 8) { dx /= 2; dy /= 2; }
                    text_solid((int16_t)(px + dx), (int16_t)(py + dy), win, lit ? M[0x1d7772] : M[0x1d7783]);
                }
                if ((int16_t)x + tw * 4 >= 0x280) {
                    int16_t n = r16(cell + 0x10); uint16_t nx = (uint16_t)((int16_t)x + tw * 4);
                    if (dlog) dlog(1, n, nx, (uint16_t)y, 0, 0, dctx);
                    int m = cell_things(n, nx, (uint16_t)y); if (m) missing = m;
                }
            } else if ((int16_t)x - tw * 4 < 0) {
                int16_t n = r16(cell - 0x10); uint16_t nx = (uint16_t)((int16_t)x - tw * 4);
                if (dlog) dlog(1, n, nx, (uint16_t)y, 0, 0, dctx);
                int m = cell_things(n, nx, (uint16_t)y); if (m) missing = m;
            }
        }
    } else {
        int px = (int16_t)(x + ((tw << 7) >> 8) + (hires ? 7 : 0xf)), py = (int16_t)(y + (hires ? 0xf : 0x1f));
        if (sub == 8) {
            frame_logged((uint16_t)r16(deco + 0x158), px, py);
            for (int i = 0; i < 3; i++)
                if (r16(t + 0x69 + (uint32_t)i * 2)) frame_part((uint16_t)r16(deco + 0x15a), (uint16_t)(i * 4 + M[t + 0x66 + i]), 1, px, py);
        } else frame_logged((uint16_t)r16(deco + (uint32_t)f * 2), px, py);
    }
    return missing;
}
/* Class 0x0a (0x11ecd6), sub-kinds 2 and 4 (others draw nothing): placed like people, the frame
 * (word 10) with its seats showing the rider (word 0x47) from table 0x1b9d68 (23 sprites per
 * kind of visitor), word 0x5f a frame over it, and a mark (deco 0x49c) while the record's
 * entry (byte 0x67 of 0x1d3c62 + word 0x16, into 0x1d3c04) has word 0 clear. */
static int classA(uint32_t t, int x, int y) {
    int sub = M[t + 0xc], tw = (uint16_t)r16(TILE_W), hires = r16(SCREEN_MODE) == 8, missing = 0;
    if (sub != 2 && sub != 4) return 0;
    uint32_t frames = r32(FRAMES), elems = r32(ELEMENTS), sprites = r32(SPRITES), things = r32(THINGS), deco = r32(DECO_TABLE);
    int px = (int16_t)((int16_t)x + ((tw * M[t + 6]) >> 8) + (M[t + 8] >> (hires ? 5 : 4)));
    int py = (int16_t)((int16_t)y + (hires ? (M[t + 8] >> 3) >> 1 : M[t + 8] >> 3));
    uint32_t rec = 0x1d3c62 + (uint32_t)(uint16_t)r16(t + 0x16);
    uint32_t rider = things + (uint32_t)(uint16_t)r16(t + 0x47) * 0xc8;
    for (uint32_t e = elems + (uint32_t)(uint16_t)r16(frames + (uint32_t)(uint16_t)r16(t + 0xa) * 8) * 10, g = 0; e > elems && g < 256;
         e = elems + (uint32_t)(uint16_t)r16(e + 8) * 10, g++) {
        int fl = (uint16_t)r16(e + 6);
        uint32_t os = sprites + (uint16_t)r16(e);
        if (fl & 0x80) {
            if (rider <= things) continue;
            if (M[rider + 0x59]) {
                for (int i = 0; i < 0x17; i++) if (r16(0x1b9d68 + (uint32_t)i * 2) == r16(e)) {
                    if (r16(FRAME_MODE) != 8) {
                        uint32_t ns = sprites + (uint16_t)r16(0x1b9d68 + (uint32_t)M[rider + 0x59] * 0x2e + (uint32_t)i * 2);
                        int bx = (int16_t)px - (M[ns + 4] >> 1) + (M[os + 4] >> 1), by = py - M[ns + 5] + M[os + 5];
                        log_blit(ns, (int16_t)(bx + r16(e + 2)), (int16_t)(by + r16(e + 4)), fl);
                    }
                    break;
                }
            } else if (os > sprites) log_blit(os, (int16_t)(px + r16(e + 2)), (int16_t)(py + r16(e + 4)), fl);
        } else {
            if (r16(FRAME_MODE) == 8) { missing = 0x30a; continue; }   /* scaled (0x166dfa): not native */
            log_blit(os, (int16_t)(px + r16(e + 2)), (int16_t)(py + r16(e + 4)), fl);
        }
    }
    if (r16(t + 0x5f)) frame_logged((uint16_t)r16(t + 0x5f), px, py);
    int c = (int8_t)M[rec + 0x67];
    if (c != -1 && !r16((uint32_t)(0x1d3c04 + c * 0xe))) frame_logged((uint16_t)r16(deco + 0x49c), px, py);
    return missing;
}
static int rem_7be8;                                    /* 0x1d7be8: the last bystander pose picked by a kind-8 ride */
/* The ride renderer (0x169840): the ride's frame (word 10) with its seats filled. The rider
 * chain (word 0x47 links, at most 36) goes into 40 slots by the ride's kind (byte 0xb3 of the
 * ride record 0x1cced0 + word 0x16, capacity word 0xa6): kinds 0/1 rotate the riders by dword
 * 0x20, 5/7 (and 9 while running) seat them in order, filling up with empty seats; seats are
 * frame elements with flag 0x80, taken from the slots downwards, the rider's sprite in place of
 * the seat's (table 0x1b9a58 by pose when word 0x92 is negative, else 0x1b9a78 as class 0x0e).
 * Kind 8 draws the frame's first element and one animated figure: a rider (or a bystander pose
 * with an offset, from table 0x1b4e5a) picked at random, which the game remembers in word 0x4d;
 * other kinds draw the frame without seats. Scaled mode (0x167182) is not native. */
static int ride_draw(uint32_t t, int x, int y) {
    uint32_t frames = r32(FRAMES), elems = r32(ELEMENTS), sprites = r32(SPRITES), things = r32(THINGS), deco = r32(DECO_TABLE);
    uint32_t fr = frames + (uint32_t)(uint16_t)r16(t + 0xa) * 8;
    if (r16(FRAME_MODE)) return (M[FRAME_MODE] & 8) ? 0x304 : 0;
    uint32_t rec = 0x1cced0 + (uint32_t)(uint16_t)r16(t + 0x16);
    uint32_t slot[0x28]; int n = 0, start = 0, mode = 2;
    if (r16(t + 0x47)) {
        int cap = r16(rec + 0xa6), kind = M[rec + 0xb3];
        if (kind == 9 && !r16(t + 0x32)) kind = 2;
        if (kind == 0 || kind == 1 || kind == 5 || kind == 7) {
            if (cap <= 0 || cap > 0x28) return 0x440 | kind;          /* (the game would divide by 0 / overrun its slots) */
            uint32_t r = things + (uint32_t)(uint16_t)r16(t + 0x47) * 0xc8;
            for (int cnt = 0;; ) {
                slot[n++] = r;
                if (!r16(r + 0x47)) break;
                r = things + (uint32_t)(uint16_t)r16(r + 0x47) * 0xc8;
                if (cnt++ >= 0x23) break;
            }
            int rot = kind <= 1 ? (int)(r32(t + 0x20) % (uint32_t)cap) : 0;
            start = kind <= 1 ? n - cap + rot : n;
            if (n > cap) for (int i = n - cap - 1; i >= 0; i--) slot[i] = 0;
            else if (n < cap) {
                if (kind <= 1) start = (n + rot) % cap;
                for (; n < cap; n++) slot[n] = 0xffffffffu;
            }
            for (int i = n; i < 0x28; i++) slot[i] = 0;
            mode = 0;
        } else if (kind == 8) {
            if (!r16(t + 0x4d)) {
                int idx = r16(t + 0x47);
                uint32_t r = things + (uint32_t)(uint16_t)idx * 0xc8;
                for (int cnt = 0;; ) {
                    slot[n++] = (uint32_t)(int16_t)idx;
                    if (!r16(r + 0x47)) break;
                    idx = r16(r + 0x47);
                    r = things + (uint32_t)(uint16_t)idx * 0xc8;
                    if (cnt++ >= 0x23) break;
                }
                for (int i = 0; i < 3; i++) slot[n++] = (uint32_t)-(int)(int16_t)idx;
                for (int i = n; i < 0x28; i++) slot[i] = 0;
            }
            mode = 4;
        }
    }
    if (mode == 2 || mode == 0) {
        for (uint32_t e = elems + (uint32_t)(uint16_t)r16(fr) * 10, guard = 0; e > elems && guard < 256;
             e = elems + (uint32_t)(uint16_t)r16(e + 8) * 10, guard++) {
            int fl = (uint16_t)r16(e + 6);
            if (fl & 0x80) {
                if (mode == 2 || !r16(t + 0x47)) continue;
                uint32_t p = 0; int probes = 0;
                for (; probes < 0x29; probes++) {
                    if ((uint16_t)start == 0) start = 0x28;
                    start--;
                    p = slot[(uint16_t)start];
                    if (p) break;
                }
                if (!p) return 0x450;                                 /* (the game would spin here) */
                slot[(uint16_t)start] = 0;
                if (p == 0xffffffffu) continue;
                uint32_t os = sprites + (uint16_t)r16(e), ns = 0;
                if ((int16_t)r16(t + 0x92) < 0)
                    ns = sprites + (uint16_t)r16(0x1b9a58 + (uint32_t)M[p + 0x59] * 4 + (uint32_t)(r16(t + 0x92) & 1) * 2);
                else if (M[p + 0x59]) {
                    for (int i = 0; i < 0x2f; i++) if (r16(0x1b9a78 + (uint32_t)i * 2) == r16(e)) {
                        ns = sprites + (uint16_t)r16(0x1b9a78 + (uint32_t)M[p + 0x59] * 0x5e + (uint32_t)i * 2);
                        break;
                    }
                    if (!ns) continue;
                } else {
                    if (os > sprites) log_blit(os, (int16_t)(x + r16(e + 2)), (int16_t)(y + r16(e + 4)), fl);
                    continue;
                }
                if (ns <= sprites) continue;
                int bx = (int16_t)x - (M[ns + 4] >> 1) + (M[os + 4] >> 1), by = y - M[ns + 5] + M[os + 5];
                log_blit(ns, (int16_t)(bx + r16(e + 2)), (int16_t)(by + r16(e + 4)), fl);
            } else {
                uint32_t s = sprites + (uint16_t)r16(e);
                if (s > sprites) log_blit(s, (int16_t)(x + r16(e + 2)), (int16_t)(y + r16(e + 4)), fl);
            }
        }
        return 0;
    }
    /* mode 4 */
    uint32_t e0 = elems + (uint32_t)(uint16_t)r16(fr) * 10, s0 = sprites + (uint16_t)r16(e0);
    if (s0 > sprites) log_blit(s0, (int16_t)(x + r16(e0 + 2)), (int16_t)(y + r16(e0 + 4)), (uint16_t)r16(e0 + 6));
    int ox = 0, oy = 0, cur = r16(t + 0x4d), f;
    uint32_t r;
    if (!cur) {
        for (int k = 0; (uint16_t)k < (uint16_t)n; k++) {
            cur = (int16_t)slot[(int16_t)(rand15() % (uint16_t)n)];
            if ((uint16_t)r16(t + 0x85) != cur) break;
        }
        if ((int16_t)cur > 0) {
            r = things + (uint32_t)(int16_t)cur * 0xc8;
            f = (uint16_t)r16(deco + (uint32_t)(uint16_t)r16(0x1b4a0e + (uint32_t)M[r + 0x59] * 0x24) * 2);
        } else {
            r = things + (uint32_t)(-(int16_t)cur) * 0xc8;
            int rem = rand15() % 9;
            f = (uint16_t)r16(deco + (uint32_t)(uint16_t)r16(0x1b4e5a + (uint32_t)rem * 6) * 2);
            rem_7be8 = rem;
            ox = r16(0x1b4e5c + (uint32_t)rem * 6); oy = r16(0x1b4e5e + (uint32_t)rem * 6);
        }
    } else if ((int16_t)cur < 0) {
        r = things + (uint32_t)(-(int16_t)cur) * 0xc8;
        f = (uint16_t)r16(r + 0xa);
        ox = r16(0x1b4e5c + (uint32_t)(uint16_t)rem_7be8 * 6); oy = r16(0x1b4e5e + (uint32_t)(uint16_t)rem_7be8 * 6);
    } else {
        r = things + (uint32_t)(int16_t)cur * 0xc8;
        f = (uint16_t)r16(r + 0xa);
    }
    if (tick_is(3)) {                                                 /* advanced, then drawn (the end of a pose stops it) */
        f = next_frame(f);
        if (M[frames + (uint32_t)f * 8 + 5] & 1) return 0;
    }
    for (uint32_t e = elems + (uint32_t)(uint16_t)r16(frames + (uint32_t)f * 8) * 10, guard = 0; e > elems && guard < 256;
         e = elems + (uint32_t)(uint16_t)r16(e + 8) * 10, guard++) {
        uint32_t s = sprites + (uint16_t)r16(e);
        if (s > sprites) log_blit(s, (int16_t)(x + r16(e + 2) + ox), (int16_t)(y + r16(e + 4) + oy), (uint16_t)r16(e + 6));
    }
    return 0;
}
/* Class 4, rides (0x11d87a): sub-kinds 2, 4, 6, 0x1e, 0x26 (raised by byte 0xb1) are placed like
 * people; while running (word 0x2e at least byte 0xa8 of the record) the ride shakes a pixel
 * and has an overlay animation (word 0x8e, stepped by speed before drawing); 0x1e adds up to
 * two frames (words 0x87, 0x89). Sub-kind 0x24 is a scaled sprite (0x166dfa): not native. */
static int class4(uint32_t t, int x, int y) {
    int sub = M[t + 0xc], tw = (uint16_t)r16(TILE_W), hires = r16(SCREEN_MODE) == 8, missing = 0;
    if (sub == 0x24) {
        uint32_t fr = r32(FRAMES) + (uint32_t)(uint16_t)r16(t + 0xa) * 8;
        uint32_t e = r32(ELEMENTS) + (uint32_t)(uint16_t)r16(fr) * 10;
        return r32(SPRITES) + (uint16_t)r16(e) > r32(SPRITES) ? 0x424 : 0;
    }
    if (sub == 0x26) y = (int16_t)(y - M[t + 0xb1]);
    else if (sub != 2 && sub != 4 && sub != 6 && sub != 0x1e) return 0;
    uint32_t rec = 0x1cced0 + (uint32_t)(uint16_t)r16(t + 0x16);
    int px = (int16_t)((int16_t)x + ((tw * M[t + 6]) >> 8) + (M[t + 8] >> (hires ? 5 : 4)));
    int py = (int16_t)((int16_t)y + (hires ? (M[t + 8] >> 3) >> 1 : M[t + 8] >> 3));
    if ((uint16_t)r16(t + 0x2e) >= M[rec + 0xa8]) {
        py = (int16_t)(py + (M[0x1d3afc] & 3) - 1);
        missing = ride_draw(t, px, py);
        int c = (int16_t)(2 * M[rec + 0xa8] - r16(t + 0x2e)), f = (uint16_t)r16(t + 0x8e);
        if (c <= 1 || r32(0x1d3afc) % (uint32_t)c != 0) f = next_frame(f);
        frame_logged(f, px, py);
    } else if (r16(t + 0xa)) missing = ride_draw(t, px, py);
    if (sub == 0x1e) {
        if (r16(t + 0x87)) frame_logged((uint16_t)r16(t + 0x87), px, py);
        if (r16(t + 0x32) == 2) frame_logged((uint16_t)r16(t + 0x89), px, py);
    }
    return missing;
}
/* Class 6 and 0x1c, ride signs (0x11c5e9): a frame, then the ride's name and price ("%s %d.%d  ",
 * 0x1b00f0: the name at 0x1cced0 + word 0x16 of the ride, byte 0xc6 in tenths) scrolling
 * through six characters of the small font. The ride's scroll counter (byte 0xbb) steps every
 * 8th tick as each of its signs is drawn, before the text is cut (a per-pass count here). */
extern const uint8_t *tp_native_text;
static struct { uint32_t ride; int n; } bumps[64]; static int nbumps;
static int class6(uint32_t t, int x, int y) {
    int tw = (uint16_t)r16(TILE_W), hires = r16(SCREEN_MODE) == 8, lo = M[SCREEN_MODE] & 1;
    if (!r16(t + 0xa)) return 0;
    int px = x + ((tw << 7) >> 8) + (hires ? 7 : 0xf), py = y + (hires ? 0xf : 0x1f);
    frame_logged((uint16_t)r16(t + 0xa), (int16_t)px, (int16_t)py);
    uint32_t ride = r32(THINGS) + (uint32_t)(uint16_t)r16(t + 0x16) * 0xc8;
    uint32_t rec = 0x1cced0 + (uint16_t)r16(ride + 0x16);
    char name[201]; int k = 0; for (; k < 200 && M[rec + k]; k++) name[k] = (char)M[rec + k]; name[k] = 0;
    char buf[256]; snprintf(buf, sizeof buf, "%s %d.%d  ", name, M[rec + 0xc6] / 10, M[rec + 0xc6] % 10);
    int len = (int)strlen(buf);
    int bump = 0;
    if (tick_is(7)) {
        int i = 0; while (i < nbumps && bumps[i].ride != ride) i++;
        if (i == nbumps && nbumps < 64) bumps[nbumps++] = (typeof(bumps[0])){ride, 0};
        if (i < 64) bump = ++bumps[i].n;
    }
    static uint8_t win[7];
    int off = (uint8_t)(M[ride + 0xbb] + bump) % len;
    for (int i = 0; i < 6; i++, off = (off + 1) % len) win[i] = (uint8_t)buf[off];
    win[6] = 0;
    uint32_t tab = r16(rec + 0x2e) == 0x26a ? 0x1b16d0 : 0x1b16c8;
    int dx = (int8_t)M[tab + (uint32_t)M[t + 0x12] * 2], dy = (int8_t)M[tab + (uint32_t)M[t + 0x12] * 2 + 1];
    if (M[SCREEN_MODE] & 8) { dx /= 2; dy /= 2; }
    int tx = (int16_t)(dx + px), ty = (int16_t)(dy + py);
    int lim = (M[SCREEN_MODE] & 8) ? 0x1e0 : 0x190;
    if (tx > 0 && tx < 0x280 - (lo ? 8 : 4) * 6 && ty > 0 && ty < lim - (lo ? 0xc : 6)) {
        uint32_t h = 2166136261u; for (int i = 0; i < 6 && win[i]; i++) h = (h ^ win[i]) * 16777619u;
        tp_native_text = win;
        if (dlog) dlog(4, tx, ty, M[0x1d7773], h, lo ? 1 : 0, dctx);
    }
    return 0;
}
/* (The renderer's hover test, 0x120ff5, is not drawing: passes turn it off, view.c.)
 * One cell's things (0x11c22a). Returns 0 if every thing in it is native, else the first class that is not. */
static int cell_things(int idx, int x, int y) {
    int tw = (uint16_t)r16(TILE_W), hires = r16(SCREEN_MODE) == 8, missing = 0;
    while (idx) {
        if (idx < 0) {                                           /* static scenery: one frame, then the list ends */
            int f = (uint16_t)r16(r32(DECO_TABLE) + (uint16_t)r16(STATIC_TAB + (uint32_t)(-idx) * 2) * 2);
            frame_logged(f, (int16_t)((uint16_t)x + ((tw << 7) >> 8) + (hires ? 4 : 8)), (int16_t)((uint16_t)y + (hires ? 8 : 0x10)));
            return missing;
        }
        uint32_t t = r32(THINGS) + (uint32_t)idx * 0xc8;
        int cls = M[t + 4];
        if (cls == 0x18) {
            int f = (uint16_t)r16(t + 0xa); if (tick_is(0xf)) f = next_frame(f);   /* advanced, then drawn */
            frame_logged(f, (int16_t)((uint16_t)x + ((tw << 7) >> 8) + (hires ? 7 : 0xf)),
                         (int16_t)((uint16_t)y + (hires ? 0xf : 0x1f)));
        } else if (cls == 6 || cls == 0x1c) {
            int m = class6(t, x, y);
            if (m && !missing) missing = m;
        } else if (cls == 0xe) {
            int m = classE(t, x, y);
            if (m && !missing) missing = m;
        } else if (cls == 0x14) {
            int m = class14(t, x, y);
            if (m && !missing) missing = m;
        } else if (cls == 8) {
            int m = class8(t, x, y);
            if (m && !missing) missing = m;
        } else if (cls == 2) {
            int m = class2(t, x, y);
            if (m && !missing) missing = m;
        } else if (cls == 0x12) {
            int m = class12(t, x, y);
            if (m && !missing) missing = m;
        } else if (cls == 0xa) {
            int m = classA(t, x, y);
            if (m && !missing) missing = m;
        } else if (cls == 4) {
            int m = class4(t, x, y);
            if (m && !missing) missing = m;
        } else if (!missing) missing = cls ? cls : 0x100;
        idx = r16(t + 2);
    }
    return missing;
}
/* The things loop for camera (cx0, cy0): cell markers and draw calls to the log. */
void tp_native_things(const uint8_t *mem, int cx0, int cy0, int rows, DrawLog lg, void *ctx) {
    M = mem; dlog = lg; dctx = ctx; anim_77a4 = (uint16_t)r16(0x1d77a4); nbumps = 0; rem_7be8 = (uint16_t)r16(0x1d7be8); rng = r32(0x1d3af4);
    int tw = (uint16_t)r16(TILE_W), th = (uint16_t)r16(TILE_H), off = r16(VIEW_OFF);
    uint32_t map = r32(MAP_PTR);
    uint32_t base = map + (uint32_t)(((cy0 << 6) + cx0) * 4);
    for (int r = 0, y = cy0; r < rows; r++, y++, base += 0x100) {
        int left = off + (tw / 3) * r - (r / 3 + 1) * tw;
        int x = (tw / 3) * r + 0x280 - (r / 3 + 1) * tw;
        int cx = (cx0 - (r / 3 + 1)) + (0x280 - off) / tw;
        uint32_t p = base - (uint32_t)((r / 3) * 4 + 4) + (uint32_t)(((0x280 - off) / tw) * 4);
        for (; (int16_t)x >= (int16_t)left; x -= tw, p -= 4, cx--) {
            if (x < off - tw) continue;
            if ((int16_t)cx < 0 || (int16_t)cx >= 0x40 || (int16_t)y < 0 || (int16_t)y >= 0x40) continue;
            int16_t first = r16(p);
            if (!first) continue;
            int cy = (uint16_t)(r * th);
            lg(1, first, (uint16_t)x, cy, 0, 0, ctx);
            int miss = cell_things(first, (uint16_t)x, cy);
            lg(3, miss, 0, 0, 0, 0, ctx);
        }
    }
    dlog = NULL;
}
