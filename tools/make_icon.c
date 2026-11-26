/* make_icon ART [--box] [--nightly] [--font FONT.ttf] [--png FILE] [--ico FILE] [--hicolor DIR] [--bmp FILE]
 *
 * The app icon for Windows and Linux, composed as platform/macos/tp_icon.m composes the Mac's: the same geometry,
 * colours and order, AppKit's y-up coordinates turned top-down. The park planet, cut with a soft circular mask from GOG's
 * box art (--box: box.jpg as tools/store_art.py fetches it) or from GOG's 256-pixel icon in the player's package (a PNG:
 * tools/ico_image.py ICO --png), on an 824 sky tile in 1024 with a drop shadow, a little light from above, the Nightly
 * band (--nightly), the outline and the launcher's yellow keyline. Drawn with platform/sdl/draw.c, the launch window's
 * canvas (stb_image reads the art, stb_truetype the band's type). Made at install time from the player's own art, into
 * build/ or the install; nothing it makes goes into this project.
 *
 *   --png FILE      the 1024-pixel picture
 *   --ico FILE      a Windows icon: 16, 24, 32, 48, 64 and 128 as 32-bit bitmaps (with their masks), 256 as PNG
 *   --hicolor DIR   DIR/<n>x<n>/apps/theme-park-native.png for n = 16, 32, 48, 64, 128, 256, 512 (the icon theme)
 *   --bmp FILE      256 pixels as a 32-bit BMP with alpha (SDL_SetWindowIcon: platform/sdl/main.c reads icon.bmp)
 *   --font FILE     the band's type: the Mac draws NIGHTLY in its system font, Heavy; this is a TrueType stand-in
 *                   (platform/sdl/fonts/Poppins-Bold.ttf, OFL), sized and placed to the same capitals (SF Pro's
 *                   metrics); without one the band goes without the word
 * The smaller pictures are the 1024 one averaged down (as sips -z makes the Mac's iconset). Exit 1 when the art can't be
 * read or is not the picture it should be: the caller then falls back, as tools/build_app.sh does (box art, else GOG's
 * icon through this, else GOG's icon as it is).
 *
 * Build (from the repository; MSYS2 CLANG64 or Linux): clang -O2 -I platform/sdl -I vendor/stb tools/make_icon.c
 * platform/sdl/draw.c -lm -o make_icon */
#include "draw.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define make_dir(p) _mkdir(p)
#else
#define make_dir(p) mkdir(p, 0755)
#endif

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC                       /* (draw.c has its own copy, also static) */
#include "stb_image_write.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

enum { S = 1024 };

/* The planet: everything outside a soft circle (image pixels, top-down) cleared; premultiplied, as tp_icon.m's bitmap. */
static void cut_planet(Image *m, float cx, float cy, float r0, float r1) {
    for (int y = 0; y < m->h; y++)
        for (int x = 0; x < m->w; x++) {
            float d = hypotf(x + 0.5f - cx, y + 0.5f - cy), a = d <= r0 ? 1 : d >= r1 ? 0 : (r1 - d) / (r1 - r0);
            uint8_t *p = m->px + ((size_t)y * m->w + x) * 4;
            for (int i = 0; i < 4; i++) p[i] = (uint8_t)lrintf(p[i] * a);
        }
}

/* A layer cut to the rounded tile (AppKit's [shape addClip]): each pixel times the shape's coverage. */
static float rrect_d(float px, float py, Box b, float r) {
    float qx = fabsf(px - (b.x + b.w * 0.5f)) - (b.w * 0.5f - r), qy = fabsf(py - (b.y + b.h * 0.5f)) - (b.h * 0.5f - r);
    float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0, in = qx > qy ? qx : qy;
    return sqrtf(ox * ox + oy * oy) + (in < 0 ? in : 0) - r;
}
static void clip_to(Canvas *c, Box b, float r) {
    for (int y = 0; y < c->h; y++)
        for (int x = 0; x < c->w; x++) {
            float k = 0.5f - rrect_d(x + 0.5f, y + 0.5f, b, r);
            if (k >= 1) continue;
            uint8_t *p = c->px + ((size_t)y * c->w + x) * 4;
            if (k <= 0) memset(p, 0, 4);
            else for (int i = 0; i < 4; i++) p[i] = (uint8_t)lrintf(p[i] * k);
        }
}

/* The height of the font's capitals at `size`: the top of a drawn H above its baseline. */
static float cap_height(Font *f, float size) {
    int n = (int)(size * 2) + 16, base = n - 8;
    Canvas *t = cv_new(n, n);
    if (!t) return 0;
    cv_text(t, f, size, 4, (float)base, 0, col(255, 255, 255), "H");
    float top = -1;
    for (int y = 0; y < base && top < 0; y++) {
        int most = 0;
        for (int x = 0; x < n; x++) if (t->px[((size_t)y * n + x) * 4 + 3] > most) most = t->px[((size_t)y * n + x) * 4 + 3];
        if (most) top = y + 1 - most / 255.f;                   /* (the stem's flat top, from its edge row's cover) */
    }
    cv_free(t);
    return top < 0 ? 0 : base - top;
}

/* NIGHTLY on the band. The Mac's: system font Heavy 104 pt, white, kern 8, shadow black 60 % 3 down blur 6, centred
 * across the tile in a line box of the font's ascent + descent centred 12 above the band's middle (y up). For SF Pro
 * (ascent 0.952, descent 0.241, capitals 0.7046 em) that puts the capitals' middle at y 819.8 (top-down), 73.3 tall:
 * another font is sized to capitals that tall and placed at the same middle. */
static void nightly_word(Canvas *layer, Font *f) {
    const float cap_want = 104 * 0.7046f, middle = 819.8f;
    float cap = cap_height(f, 104), size = cap > 1 ? 104 * cap_want / cap : 104, track = 8 / size;
    float w = text_width(f, size, track, "NIGHTLY");
    Canvas *t = cv_new(S, S);
    if (!t) return;
    cv_text(t, f, size, 100 + (824 - w) / 2, middle + cap_want / 2, track, col(255, 255, 255), "NIGHTLY");
    cv_shadow_of(layer, t, 0, 0, 0, 3, 6, cola(0, 0, 0, 0.6f));
    cv_over(layer, t, 0, 0, 1);
    cv_free(t);
}

/* tp_icon.m's picture (its line numbers in brackets). globe: the cut planet; (cx, cy, r1): its circle in globe pixels. */
static Canvas *compose(const Image *globe, float cx, float cy, float r1, int nightly, Font *font) {
    Canvas *c = cv_new(S, S), *layer = cv_new(S, S), *planet = cv_new(S, S);
    if (!c || !layer || !planet) exit(1);
    Box tile = box(100, 100, 824, 824);
    /* the tile's drop shadow (black 45 %, 10 down, blur 24) under its black fill [:52-53] */
    cv_shadow(c, box(100, 110, 824, 824), 185, 24, cola(0, 0, 0, 0.45f));
    cv_rrect(c, tile, 185, col(0, 0, 0));
    /* inside the tile: the sky, from (30,86,178) at the bottom through (66,146,228) at 0.55 to (150,206,250) at the top [:56-57] */
    Col sky[3] = {col(150, 206, 250), col(66, 146, 228), col(30, 86, 178)};
    float sky_at[3] = {0, 0.45f, 1};
    cv_grad_rrect(layer, tile, 0, sky, sky_at, 3, 90);
    /* the planet above the band (6 lower with it), 0.96 of the room up to 680, with its shadow (black 35 %, 12 down,
       blur 28) [:58-65]; the picture is resampled straight to whole pixels from the matching part of the art */
    float floor_ = nightly ? 293 : 100, room = 924 - floor_, d = fminf(room * 0.96f, 680);
    float tx = 512 - d / 2, ty = S - (floor_ + (room - d) / 2 - (nightly ? 6 : 0) + d);
    int x0 = (int)floorf(tx), y0 = (int)floorf(ty), x1 = (int)ceilf(tx + d), y1 = (int)ceilf(ty + d);
    float k = 2 * r1 / d;
    Image *g = img_scaled(globe, box(cx - r1 + (x0 - tx) * k, cy - r1 + (y0 - ty) * k, (x1 - x0) * k, (y1 - y0) * k), x1 - x0, y1 - y0);
    if (!g) exit(1);
    cv_image(planet, g, (float)x0, (float)y0, 1);
    img_free(g);
    cv_shadow_of(layer, planet, 0, 0, 0, 12, 28, cola(0, 0, 0, 0.35f));
    cv_over(layer, planet, 0, 0, 1);
    /* a little light from above: clear to 70 % of the way up, white 10 % at the top [:67-69] (NSGradient mixes its
       colours unpremultiplied, so the stop below the top is black at 0) */
    Col shade[3] = {cola(255, 255, 255, 0.10f), cola(0, 0, 0, 0), cola(0, 0, 0, 0)};
    float shade_at[3] = {0, 0.3f, 1};
    cv_grad_rrect(layer, tile, 0, shade, shade_at, 3, 90);
    if (nightly) {   /* the band, as on the other ports' Nightly apps [:70-81]: (0.36,0.20,0.78) at its top to (0.20,0.10,0.52) */
        Col band[2] = {{0.36f, 0.20f, 0.78f, 1}, {0.20f, 0.10f, 0.52f, 1}};
        float band_at[2] = {0, 1};
        cv_grad_rrect(layer, box(100, 739, 824, 185), 0, band, band_at, 2, 90);
        cv_rrect(layer, box(100, 731, 824, 8), 0, col(8, 8, 14));                /* the rule above it */
        if (font) nightly_word(layer, font);
    }
    clip_to(layer, tile, 185);
    cv_over(c, layer, 0, 0, 1);
    /* the outline (14 on the tile's edge) and the launcher's yellow keyline (5, inset 14, radius 172, 90 %) [:83-85] */
    cv_rrect_line(c, box(93, 93, 838, 838), 192, 14, col(8, 8, 14));
    cv_rrect_line(c, box(111.5f, 111.5f, 801, 801), 174.5f, 5, cola(255, 210, 63, 0.9f));
    cv_free(layer); cv_free(planet);
    return c;
}

/* The picture at n x n, straight (not premultiplied) RGBA: the 1024 one averaged down. */
static uint8_t *sized(const Canvas *c, int n) {
    Image view = {c->w, c->h, c->px}, *m = n == c->w ? NULL : img_scaled(&view, box(0, 0, (float)c->w, (float)c->h), n, n);
    const uint8_t *pm = m ? m->px : c->px;
    uint8_t *out = malloc((size_t)n * n * 4);
    if (!out) exit(1);
    for (size_t i = 0; i < (size_t)n * n; i++) {
        unsigned a = pm[i * 4 + 3];
        for (int ch = 0; ch < 3; ch++) { unsigned v = a ? (pm[i * 4 + ch] * 255u + a / 2) / a : 0; out[i * 4 + ch] = (uint8_t)(v > 255 ? 255 : v); }
        out[i * 4 + 3] = (uint8_t)a;
    }
    img_free(m);
    return out;
}

static int write_png(const Canvas *c, int n, const char *path) {
    uint8_t *px = sized(c, n);
    int ok = stbi_write_png(path, n, n, 4, px, n * 4);
    free(px);
    if (!ok) fprintf(stderr, "make_icon: cannot write %s\n", path);
    return ok;
}

static void put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, unsigned v) { put16(p, v & 0xffff); put16(p + 2, v >> 16); }

/* A 32-bit bottom-up BGRA bitmap of straight RGBA rows (the .ico's DIB entries and the BMP). */
static void bgra_rows(uint8_t *dst, const uint8_t *rgba, int n) {
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++) {
            const uint8_t *s = rgba + ((size_t)(n - 1 - y) * n + x) * 4;
            uint8_t *t = dst + ((size_t)y * n + x) * 4;
            t[0] = s[2]; t[1] = s[1]; t[2] = s[0]; t[3] = s[3];
        }
}

static int write_ico(const Canvas *c, const char *path) {
    static const int sizes[] = {16, 24, 32, 48, 64, 128, 256};
    enum { N = sizeof sizes / sizeof *sizes };
    uint8_t *blob[N]; int len[N];
    for (int i = 0; i < N; i++) {
        int n = sizes[i];
        uint8_t *px = sized(c, n);
        if (n == 256) {
            blob[i] = stbi_write_png_to_mem(px, n * 4, n, n, 4, &len[i]);   /* (Vista's PNG entry) */
            if (!blob[i]) exit(1);
        } else {             /* BITMAPINFOHEADER (height doubled for the mask), BGRA bottom-up, the 1-bit AND mask */
            int mask_row = (n + 31) / 32 * 4;
            len[i] = 40 + n * n * 4 + mask_row * n;
            blob[i] = calloc(1, (size_t)len[i]);
            if (!blob[i]) exit(1);
            put32(blob[i], 40); put32(blob[i] + 4, (unsigned)n); put32(blob[i] + 8, (unsigned)n * 2);
            put16(blob[i] + 12, 1); put16(blob[i] + 14, 32); put32(blob[i] + 20, (unsigned)(n * n * 4 + mask_row * n));
            bgra_rows(blob[i] + 40, px, n);
            uint8_t *mask = blob[i] + 40 + n * n * 4;
            for (int y = 0; y < n; y++)
                for (int x = 0; x < n; x++)
                    if (!px[((size_t)(n - 1 - y) * n + x) * 4 + 3]) mask[y * mask_row + x / 8] |= (uint8_t)(0x80 >> (x % 8));
        }
        free(px);
    }
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "make_icon: cannot write %s\n", path); return 0; }
    uint8_t head[6] = {0}, entry[16];
    put16(head + 2, 1); put16(head + 4, N);
    fwrite(head, 1, 6, f);
    unsigned at = 6 + 16 * N;
    for (int i = 0; i < N; i++) {
        memset(entry, 0, sizeof entry);
        entry[0] = entry[1] = (uint8_t)(sizes[i] & 0xff);          /* (0: 256) */
        put16(entry + 4, 1); put16(entry + 6, 32); put32(entry + 8, (unsigned)len[i]); put32(entry + 12, at);
        fwrite(entry, 1, 16, f);
        at += (unsigned)len[i];
    }
    for (int i = 0; i < N; i++) {
        fwrite(blob[i], 1, (size_t)len[i], f);
        if (sizes[i] == 256) STBIW_FREE(blob[i]); else free(blob[i]);
    }
    return fclose(f) == 0;
}

/* BITMAPV4HEADER with an alpha mask, bottom-up BGRA, as tools/ico_image.py writes icon.bmp (SDL_LoadBMP keeps the alpha). */
static int write_bmp(const Canvas *c, int n, const char *path) {
    uint8_t *px = sized(c, n), *out = calloc(1, 14 + 108 + (size_t)n * n * 4);
    if (!out) exit(1);
    unsigned size = 14 + 108 + (unsigned)(n * n * 4);
    out[0] = 'B'; out[1] = 'M'; put32(out + 2, size); put32(out + 10, 14 + 108);
    uint8_t *h = out + 14;
    put32(h, 108); put32(h + 4, (unsigned)n); put32(h + 8, (unsigned)n); put16(h + 12, 1); put16(h + 14, 32); put32(h + 16, 3);
    put32(h + 20, (unsigned)(n * n * 4)); put32(h + 24, 2835); put32(h + 28, 2835);
    put32(h + 40, 0x00ff0000); put32(h + 44, 0x0000ff00); put32(h + 48, 0x000000ff); put32(h + 52, 0xff000000);
    memcpy(h + 56, "BGRs", 4);
    bgra_rows(out + 14 + 108, px, n);
    FILE *f = fopen(path, "wb");
    int ok = f && fwrite(out, 1, size, f) == size;
    if (f && fclose(f)) ok = 0;
    if (!ok) fprintf(stderr, "make_icon: cannot write %s\n", path);
    free(px); free(out);
    return ok;
}

static int write_hicolor(const Canvas *c, const char *dir) {
    static const int sizes[] = {16, 32, 48, 64, 128, 256, 512};
    char p[4096];
    make_dir(dir);
    for (size_t i = 0; i < sizeof sizes / sizeof *sizes; i++) {
        snprintf(p, sizeof p, "%s/%dx%d", dir, sizes[i], sizes[i]); make_dir(p);
        snprintf(p, sizeof p, "%s/%dx%d/apps", dir, sizes[i], sizes[i]); make_dir(p);
        snprintf(p, sizeof p, "%s/%dx%d/apps/theme-park-native.png", dir, sizes[i], sizes[i]);
        if (!write_png(c, sizes[i], p)) return 0;
    }
    return 1;
}

int main(int argc, char **argv) {
    const char *usage = "usage: make_icon ART [--box] [--nightly] [--font FONT.ttf] [--png FILE] [--ico FILE] [--hicolor DIR] [--bmp FILE]\n";
    if (argc < 2) { fputs(usage, stderr); return 2; }
    const char *art = argv[1], *font_path = NULL, *png = NULL, *ico = NULL, *hicolor = NULL, *bmp = NULL;
    int nightly = 0, boxart = 0;
    for (int k = 2; k < argc; k++) {
        const char **opt = !strcmp(argv[k], "--font") ? &font_path : !strcmp(argv[k], "--png") ? &png : !strcmp(argv[k], "--ico") ? &ico
                         : !strcmp(argv[k], "--hicolor") ? &hicolor : !strcmp(argv[k], "--bmp") ? &bmp : NULL;
        if (!strcmp(argv[k], "--nightly")) nightly = 1;
        else if (!strcmp(argv[k], "--box")) boxart = 1;
        else if (opt && k + 1 < argc) *opt = argv[++k];
        else { fputs(usage, stderr); return 2; }
    }
    Image *src = img_load(art);
    if (!src || src->w < 64) { fprintf(stderr, "make_icon: no picture in %s\n", art); return 1; }
    /* the planet's centre and radius: on the 342 x 482 box art, or on the 256-pixel icon [:38-42] */
    float u = boxart ? src->w / 342.f : src->w / 256.f;
    float cx = (boxart ? 171 : 128) * u, cy = (boxart ? 284 : 125) * u, r0 = (boxart ? 150 : 82) * u, r1 = (boxart ? 161 : 91) * u;
    if (boxart && src->h < 400 * u) { fprintf(stderr, "make_icon: %s is not GOG's box art (%dx%d)\n", art, src->w, src->h); return 1; }
    cut_planet(src, cx, cy, r0, r1);
    Font *font = NULL;
    if (nightly && font_path && !(font = font_load(font_path))) fprintf(stderr, "make_icon: no font in %s: the band goes without its word\n", font_path);
    else if (nightly && !font_path) fprintf(stderr, "make_icon: no --font: the band goes without its word\n");
    Canvas *c = compose(src, cx, cy, r1, nightly, font);
    img_free(src);
    int ok = 1;
    if (png) ok &= write_png(c, S, png);
    if (ico) ok &= write_ico(c, ico);
    if (hicolor) ok &= write_hicolor(c, hicolor);
    if (bmp) ok &= write_bmp(c, 256, bmp);
    cv_free(c);
    return ok ? 0 : 1;
}
