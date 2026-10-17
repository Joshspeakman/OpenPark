/* Native drawing of the things layer (engine rebuild stage 3b, docs/PLAN.md).
 *
 * A port of the game's sprite blitter core 0x15ed21, which the things renderer
 * reaches through 0x15ec9c (320x200), 0x155bd4 (640x480) and the one-colour
 * glyph blit 0x111bd5. Sprites are Bullfrog RLE rows (n > 0: n pixels follow;
 * n < 0: skip -n, and a run always follows; 0: end of row). The blitter flags
 * (word [0x1becbc]) select a variant: bit 0 mirrors (drawing leftwards from
 * x + w), bit 1 flips (rows upwards from y + h), bit 2 blends through the
 * ghost table [0x1ba2f8] (dst = table[dst * 256 + src]), bits 3 and 5 scale
 * (by stack arguments, not used by things: left out), bit 6 paints one colour.
 *
 * The game's clipping has quirks this keeps, so the result is the same pixels:
 * - a sprite partly off the left or right edge only honours bits 0 and 1; with
 *   any other flag it is not drawn at all (and bit 6 is not applied when the
 *   unclipped path would have taken it — the clipped paths have no such case);
 * - the column counters of the clipped paths are bytes (they wrap after 128
 *   pixels, which only 640x480 sprites are wide enough to reach);
 * - a sprite off the top is not also clipped at the bottom;
 * - flipped sprites cover rows y + 1 .. y + h.
 * Writes land where the game's would (base + y * pitch + x, even outside the
 * clip window); only writes outside the whole buffer are dropped. */
#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint8_t *buf; size_t size;                  /* the screen buffer the game draws into */
    long base; long pitch;                      /* the clip window's origin in it, and the row pitch */
    int clip_w, clip_h, clip_r, lowres;         /* [0x1de5c8], [0x1de5f0], [0x1de5ec], [0x1de5f6] & 1 */
    const uint8_t *ghost;                       /* 256x256 table ([0x1ba2f8]), or NULL */
    long unsupported;
} NDTarget;

static inline void put(NDTarget *t, long off, uint8_t v) { if (off >= 0 && (size_t)off < t->size) t->buf[off] = v; }
static inline uint8_t get(NDTarget *t, long off) { return off >= 0 && (size_t)off < t->size ? t->buf[off] : 0; }

/* Skip n rows of RLE data. */
static const uint8_t *skip_rows(const uint8_t *s, int n) {
    while (n-- > 0) for (;;) { int8_t c = (int8_t)*s++; if (!c) break; if (c > 0) s += c; }
    return s;
}

void tp_native_draw_sprite(NDTarget *t, const uint8_t *s, int w, int h, int x, int y, int flags, int colour) {
    if (!(h & 0xff)) return;                                  /* or dh, dh */
    w &= 0xff; h &= 0xff;
    if (t->lowres) { w >>= 1; h >>= 1; x >>= 1; y >>= 1; }
    if (!h) { t->unsupported++; return; }                     /* (the game would draw 256 rows of whatever follows) */
    long P = t->pitch;
    /* vertical clipping (0x15ed5c, 0x15f4aa, 0x15f4fe) */
    if (y < 0) {
        if (flags & 2) { int n = h + y + 1; if (n <= 0) return; y = -1; h = n & 0xff; }
        else { int n = h + y; if (n <= 0) return; s = skip_rows(s, -y); h = n & 0xff; y = 0; }
    } else if (y + h >= t->clip_h) {
        if (flags & 2) { int e = y + 1 - t->clip_h; if (e >= 0) return; e += h; h = (h - e) & 0xff; s = skip_rows(s, e); }
        else { int n = t->clip_h - y; if (n <= 0) return; h = n & 0xff; }
    }
    if (!h) return;
    int mirror = flags & 1, flip = flags & 2;
    if (x < 0) {                                              /* 0x15f2ea: left edge */
        if (flags == 0 || (!mirror && flip)) {                /* plain / flip: skip the first -x columns */
            if (w <= -x) return;
            int8_t skip0 = (int8_t)(-x);
            long row = t->base + (long)(flip ? y + h : y) * P;
            for (int r = 0; r < h; r++, row += flip ? -P : P) {
                long d = row; int8_t bl = skip0;
                for (;;) {
                    int8_t c = (int8_t)*s++;
                    if (!c) break;
                    if (c > 0) { for (int k = 0; k < c; k++) { uint8_t v = *s++; bl--; if (bl < 0) put(t, d++, v); } continue; }
                    int8_t a = c;
                    if (bl > 0) { int8_t nb = (int8_t)(bl + c); bl = nb; if (nb >= 0) continue; a = nb; }
                    d -= a;
                    int n = *s++;
                    for (int k = 0; k < n; k++) { uint8_t v = *s++; bl--; if (bl < 0) put(t, d++, v); }
                }
            }
            return;
        }
        if (mirror) {                                         /* 0x15f3d8 / 0x15f375: from x + w leftwards to column 0 */
            int lim = x + w + 1; if (lim < 0) return;
            int8_t dl0 = (int8_t)lim;
            long row = t->base + x + w + (long)(flip ? y + h : y) * P;
            for (int r = 0; r < h; r++, row += flip ? -P : P) {
                long d = row; int8_t dl = dl0;
                for (;;) {
                    int8_t c = (int8_t)*s++;
                    if (!c) break;
                    if (c > 0) { for (int k = 0; k < c; k++) { uint8_t v = *s++; dl--; if (dl >= 0) put(t, d, v); d--; } continue; }
                    d += c; dl = (int8_t)(dl + c);
                }
            }
            return;
        }
        return;                                               /* other flags: not drawn */
    }
    if (x + w >= t->clip_w) {                                 /* 0x15f106: right edge ([0x1de5ec]) */
        if (flags == 0 || (!mirror && flip)) {
            long rem = (long)t->clip_r - x; if (rem < 0) return;
            int8_t bl0 = (int8_t)rem;
            long row = t->base + x + (long)(flip ? y + h : y) * P;
            for (int r = 0; r < h; r++, row += flip ? -P : P) {
                long d = row; int8_t bl = bl0;
                for (;;) {
                    int8_t c = (int8_t)*s++;
                    if (!c) break;
                    if (c > 0) { for (int k = 0; k < c; k++) { uint8_t v = *s++; bl--; if (bl >= 0) put(t, d++, v); } continue; }
                    if (flags == 0) {                         /* plain: a skip past the edge ends the row's drawing */
                        int sum = bl + c; int8_t nb = (int8_t)sum; bl = nb;
                        if (sum <= 0) continue;               /* (jle on the 8-bit add: the true sum) */
                        d -= c;
                        int n = *s++;
                        for (int k = 0; k < n; k++) { uint8_t v = *s++; bl--; if (bl >= 0) put(t, d++, v); }
                    } else { d -= c; bl = (int8_t)(bl + c); } /* flip: just move */
                }
            }
            return;
        }
        if (mirror) {                                         /* 0x15f20a / 0x15f191 */
            if ((long)t->clip_r - x < 0) return;
            int8_t dl0 = (int8_t)((long)t->clip_r - (x + w) - 2);
            long row = t->base + x + w + (long)(flip ? y + h : y) * P;
            for (int r = 0; r < h; r++, row += flip ? -P : P) {
                long d = row; int8_t dl = dl0;
                for (;;) {
                    int8_t c = (int8_t)*s++;
                    if (!c) break;
                    if (c > 0) { for (int k = 0; k < c; k++) { uint8_t v = *s++; dl++; if (dl >= 0) put(t, d, v); d--; } continue; }
                    d += c; dl = (int8_t)(dl - c);
                }
            }
            return;
        }
        return;
    }
    /* unclipped */
    if (mirror) {                                             /* 0x15ee07 / 0x15edaa */
        long row = t->base + x + w + (long)(flip ? y + h : y) * P;
        for (int r = 0; r < h; r++, row += flip ? -P : P) {
            long d = row;
            for (;;) {
                int8_t c = (int8_t)*s++;
                if (!c) break;
                if (c < 0) { d += c; c = (int8_t)*s++; }
                for (int k = 0; k < c; k++) put(t, d--, *s++);
            }
        }
        return;
    }
    int ghost = 0, solid = 0;
    if (flags && !flip) {
        if (flags & 4) ghost = 1;
        else if (flags & 0x28) { t->unsupported++; return; }  /* scaled: not used by things */
        else if (flags & 0x40) solid = 1;
    }
    if (ghost && !t->ghost) { t->unsupported++; return; }
    long row = t->base + x + (long)(flip ? y + h : y) * P;
    for (int r = 0; r < h; r++, row += flip ? -P : P) {
        long d = row;
        for (;;) {
            int8_t c = (int8_t)*s++;
            if (!c) break;
            if (c < 0) {
                d -= c;
                if (ghost) continue;                          /* (the ghost path re-reads a control byte) */
                c = (int8_t)*s++;
            }
            for (int k = 0; k < c; k++, s++, d++)
                put(t, d, solid ? (uint8_t)colour : ghost ? t->ghost[get(t, d) << 8 | *s] : *s);
        }
    }
}
