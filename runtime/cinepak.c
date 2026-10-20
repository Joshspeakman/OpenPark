/* 3DO DataStream video (FILM chunks of Cinepak), for the 3DO ride videos the owner imports (tools/extras.py).
 * Portable C, no platform calls: the app reads the frames as RGB and shows them its own way.
 *
 * A stream is chunks (type, size, time, channel) padded to 32 KB blocks; FILM/FHDR gives the size and frame
 * rate, FILM/FRME one Cinepak frame. Cinepak: per strip, V1 and V4 codebooks of 2x2 YUV blocks (Y0..Y3 U V);
 * a 4x4 block is one V1 entry (each Y doubled) or four V4 entries; key strips code every block, inter strips
 * flag the changed ones; codebooks carry over between frames and from one strip to the next. Written from the
 * format's public description; tools/cinepak.py is the same decoder in Python. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint8_t rgb[4][3]; } Entry;
typedef struct {
    uint8_t *data; size_t len;
    uint32_t *frame_off, *frame_len; int nframes, next;
    int w, h, fps;
    Entry book[8][2][256];          /* per strip: V4, V1 */
    uint8_t *px;                    /* w * h * 3 */
} TpVideo;

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static uint8_t clamp(int v) { return v < 0 ? 0 : v > 255 ? 255 : (uint8_t)v; }

TpVideo *tp_video_open(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    TpVideo *v = calloc(1, sizeof *v);
    v->data = malloc((size_t)n); v->len = (size_t)n;
    if (fread(v->data, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(v->data); free(v); return NULL; }
    fclose(f);
    v->w = 320; v->h = 200; v->fps = 15;
    int cap = 1024; v->frame_off = malloc(cap * sizeof(uint32_t)); v->frame_len = malloc(cap * sizeof(uint32_t));
    for (size_t i = 0; i + 8 <= v->len;) {
        uint32_t size = be32(v->data + i + 4);
        if (size < 8 || size > 0x100000) { i = (i / 32768 + 1) * 32768; continue; }
        if (!memcmp(v->data + i, "FILM", 4) && i + 40 <= v->len) {
            const uint8_t *s = v->data + i + 16;
            if (!memcmp(s, "FHDR", 4)) { v->h = (int)be32(s + 12); v->w = (int)be32(s + 16); v->fps = (int)be32(s + 20); }
            else if (!memcmp(s, "FRME", 4)) {
                if (v->nframes == cap) { cap *= 2; v->frame_off = realloc(v->frame_off, cap * sizeof(uint32_t)); v->frame_len = realloc(v->frame_len, cap * sizeof(uint32_t)); }
                v->frame_len[v->nframes] = be32(s + 8);
                v->frame_off[v->nframes++] = (uint32_t)(i + 28);
            }
        }
        i += size;
    }
    if (v->w <= 0 || v->w > 640 || v->h <= 0 || v->h > 480 || v->fps <= 0 || v->fps > 60 || !v->nframes) { free(v->frame_off); free(v->frame_len); free(v->data); free(v); return NULL; }
    v->px = calloc((size_t)v->w * v->h, 3);
    return v;
}
int tp_video_frames(TpVideo *v) { return v->nframes; }
int tp_video_fps(TpVideo *v) { return v->fps; }
int tp_video_width(TpVideo *v) { return v->w; }
int tp_video_height(TpVideo *v) { return v->h; }

static void entry(Entry *e, const uint8_t *b) {
    int u = (int8_t)b[4], vv = (int8_t)b[5];
    for (int k = 0; k < 4; k++) {
        int y = b[k];
        e->rgb[k][0] = clamp(y + 2 * vv); e->rgb[k][1] = clamp(y - (u >> 1) - vv); e->rgb[k][2] = clamp(y + 2 * u);
    }
}
static void codebook(Entry *book, const uint8_t *b, const uint8_t *end, int partial) {
    int k = 0;
    while (k < 256) {                                     /* (whole entries and flag words only: a chunk cut short reads no further) */
        if (partial) {
            if (end - b < 4) break;
            uint32_t flags = be32(b); b += 4;
            for (int bit = 0; bit < 32 && k < 256; bit++, k++)
                if (flags & (0x80000000u >> bit)) { if (end - b < 6) return; entry(&book[k], b); b += 6; }
        } else { if (end - b < 6) break; entry(&book[k++], b); b += 6; }
    }
}
static void put2x2(TpVideo *v, int x, int y, const uint8_t c[3]) {
    for (int dy = 0; dy < 2; dy++) for (int dx = 0; dx < 2; dx++)
        if (x + dx < v->w && y + dy < v->h) memcpy(v->px + ((y + dy) * v->w + x + dx) * 3, c, 3);
}
static void block_v1(TpVideo *v, int x, int y, const Entry *e) {
    for (int j = 0; j < 4; j++) put2x2(v, x + (j & 1) * 2, y + (j >> 1) * 2, e->rgb[j]);
}
static void block_v4(TpVideo *v, int x, int y, const Entry *e[4]) {
    for (int q = 0; q < 4; q++) for (int j = 0; j < 4; j++) {
        int px = x + (q & 1) * 2 + (j & 1), py = y + (q >> 1) * 2 + (j >> 1);
        if (px < v->w && py < v->h) memcpy(v->px + (py * v->w + px) * 3, e[q]->rgb[j], 3);
    }
}

/* The next frame into v->px (RGB); returns it, or NULL at the end. */
const uint8_t *tp_video_next(TpVideo *v) {
    if (v->next >= v->nframes) return NULL;
    const uint8_t *b = v->data + v->frame_off[v->next];
    const uint8_t *limit = v->data + v->len;
    uint32_t flen = v->frame_len[v->next++];                 /* (the 3DO chunk's size: the Cinepak header's can be short) */
    if (b + 10 > limit) return NULL;
    const uint8_t *fend = b + flen < limit ? b + flen : limit;
    int flags = b[0], nstrips = be16(b + 8), ytop = 0;
    if (nstrips > 8) nstrips = 8;
    const uint8_t *p = b + 10;
    for (int s = 0; s < nstrips; s++) {
        while (p + 4 <= fend && be16(p) != 0x1000 && be16(p) != 0x1100) { uint16_t n = be16(p + 2); if (!n) return v->px; p += n; }   /* (3DO's 0xfe00 chunk) */
        if (p + 12 > fend) break;
        uint16_t ssize = be16(p + 2), y1 = be16(p + 4), y2 = be16(p + 8);
        if (s && !(flags & 1)) memcpy(v->book[s], v->book[s - 1], sizeof v->book[s]);
        Entry *v4 = v->book[s][0], *v1 = v->book[s][1];
        int top = y1 ? y1 : ytop, height = y2 - y1;
        const uint8_t *end = p + ssize < fend ? p + ssize : fend, *c = p + 12;   /* (a chunk may run past its strip's size) */
        while (c + 4 <= end) {
            uint16_t cid = be16(c), csize = be16(c + 2);
            if (csize < 4) break;
            const uint8_t *body = c + 4, *cend = c + csize < fend ? c + csize : fend;
            if (cid == 0x2000 || cid == 0x2100) codebook(v4, body, cend, cid == 0x2100);
            else if (cid == 0x2200 || cid == 0x2300) codebook(v1, body, cend, cid == 0x2300);
            else if (cid == 0x3000 || cid == 0x3100 || cid == 0x3200) {
                const uint8_t *q = body; uint32_t bits = 0, mask = 0;
                #define BIT() (mask ? 0 : (q + 4 <= cend ? (bits = be32(q), q += 4, mask = 0x80000000u, 0) : (mask = 0x80000000u, bits = 0, 0)), \
                               ((bits & mask) ? (mask >>= 1, 1) : (mask >>= 1, 0)))
                for (int by = top; by < top + height; by += 4)
                    for (int bx = 0; bx < v->w; bx += 4) {
                        if (cid == 0x3100 && !BIT()) continue;    /* (unchanged: needs no data, even at the chunk's end) */
                        if (cid == 0x3200 || !BIT()) { if (q >= cend) goto done; block_v1(v, bx, by, &v1[*q++]); }
                        else {
                            if (q + 4 > cend) goto done;
                            const Entry *e[4] = {&v4[q[0]], &v4[q[1]], &v4[q[2]], &v4[q[3]]}; q += 4;
                            block_v4(v, bx, by, e);
                        }
                    }
                done:;
                #undef BIT
            }
            c += csize;
        }
        ytop = top + height;
        p = end;
    }
    return v->px;
}
void tp_video_close(TpVideo *v) { if (!v) return; free(v->px); free(v->frame_off); free(v->frame_len); free(v->data); free(v); }

#ifdef TP_VIDEO_TEST
/* cc -DTP_VIDEO_TEST runtime/cinepak.c: decode a stream, write frame N as a PPM (development check). */
int main(int argc, char **argv) {
    TpVideo *v = tp_video_open(argv[1]); if (!v) return 1;
    int want = argc > 3 ? atoi(argv[3]) : 0; const uint8_t *px = NULL;
    for (int k = 0; k <= want; k++) px = tp_video_next(v);
    FILE *f = fopen(argv[2], "wb"); fprintf(f, "P6 %d %d 255\n", v->w, v->h); fwrite(px, 3, (size_t)v->w * v->h, f); fclose(f);
    printf("%d frames, %d fps, %dx%d\n", v->nframes, v->fps, v->w, v->h);
    tp_video_close(v); return 0;
}
#endif
