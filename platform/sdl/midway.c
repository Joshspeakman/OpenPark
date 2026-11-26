/* The launch window's moving fairground (midway.h): sprites drawn once with draw.c at the device scale, then moved,
 * turned, scaled and faded by the renderer every frame. Every formula is TPMidway's (platform/macos/launcher.m:138-300,
 * docs/parity/launcher.md section 9); the comments name the Mac's numbers where they come from.
 *
 * Colour: Core Animation's own colours (the back fill, the dusk, the bunting string) are Generic RGB, which the Mac
 * colour-matches to the screen; here they are converted to sRGB with Apple's Generic RGB profile (gamma 1.8, its
 * primaries), which reproduces the Mac's render of the dusk to one level in 255 (docs/launcher-reference/main.png).
 * Every other colour is sRGB as on the Mac. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L         /* (popen, for GNOME's setting) */
#endif
#include "midway.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#define WIN_W 1000.0f
#define WIN_H 700.0f
#define TAU 6.28318530717958647692

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static double frand(unsigned k) { k ^= k >> 13; k *= 0x5bd1e995u; k ^= k >> 15; return (k & 0xffffff) / (double)0x1000000; }

/* A Generic RGB colour (CGColorCreateGenericRGB) as sRGB. */
static float srgb_enc(float l) { l = clampf(l, 0, 1); return l <= 0.0031308f ? l * 12.92f : 1.055f * powf(l, 1 / 2.4f) - 0.055f; }
static Col generic(float r, float g, float b, float a) {
    float R = powf(r, 1.8f), G = powf(g, 1.8f), B = powf(b, 1.8f);
    Col c = {srgb_enc(1.02389f * R - 0.02715f * G + 0.00330f * B), srgb_enc(0.02064f * R + 0.94871f * G + 0.03065f * B),
             srgb_enc(-0.00187f * R - 0.00146f * G + 1.00319f * B), a};
    return c;
}

/* ---- sprites: drawn once ---- */
/* A sprite: its cell (x, y, w, h) on a sheet tx of tw x th pixels, and where its anchor is in the cell: the point it is
 * placed by, turned about and scaled about; k: device pixels per texture pixel (1/ss, or more if the sprite is too big
 * for the renderer's textures). */
typedef struct { SDL_Texture *tx; float x, y, w, h, ax, ay, k, tw, th; } Sprite;

/* k (straight colour) at coverage cov over the pixel p, as draw.c composes. */
static inline void put(uint8_t *p, Col k, float cov) {
    float a = k.a * cov;
    if (a <= 0.0005f) return;
    float keep = 1 - a;
    float v[4] = {k.r * a * 255 + p[0] * keep, k.g * a * 255 + p[1] * keep, k.b * a * 255 + p[2] * keep, a * 255 + p[3] * keep};
    for (int ch = 0; ch < 4; ch++) p[ch] = (uint8_t)(v[ch] > 255 ? 255 : v[ch] + 0.5f);
}
/* Straight colour stops at positions 0..1, interpolated (premultiplied out). */
typedef struct { float at; Col c; } Stop;
static Col stops_at(const Stop *s, int n, float t) {
    if (t <= s[0].at) return s[0].c;
    for (int i = 1; i < n; i++)
        if (t <= s[i].at) return mix(s[i - 1].c, s[i].c, (t - s[i - 1].at) / (s[i].at - s[i - 1].at));
    return s[n - 1].c;
}
/* Signed distance from (px, py) to the triangle a, b, c (negative inside). */
static float tri_dist(float px, float py, const float *v) {
    float d = 1e30f, s = 1;
    float sg = (v[2] - v[0]) * (v[5] - v[1]) - (v[3] - v[1]) * (v[4] - v[0]) > 0 ? 1.f : -1.f;
    for (int e = 0; e < 3; e++) {
        float x0 = v[e * 2], y0 = v[e * 2 + 1], x1 = v[(e + 1) % 3 * 2], y1 = v[(e + 1) % 3 * 2 + 1];
        float ex = x1 - x0, ey = y1 - y0, wx = px - x0, wy = py - y0;
        float h = clampf((wx * ex + wy * ey) / (ex * ex + ey * ey), 0, 1), dx = wx - ex * h, dy = wy - ey * h;
        float dd = dx * dx + dy * dy;
        if (dd < d) d = dd;
        if (sg * (ex * wy - ey * wx) < 0) s = -1;    /* (outside this edge) */
    }
    return s > 0 ? -sqrtf(d) : sqrtf(d);
}
/* Each pixel's alpha blurred (a Gaussian of sigma pixels) into out (floats, the canvas's size). */
static void blur_alpha(const Canvas *c, float sigma, float *out) {
    int r = (int)ceilf(sigma * 3), n = 2 * r + 1;
    float *k = malloc(sizeof(float) * n), *tmp = malloc(sizeof(float) * c->w * c->h), sum = 0;
    if (!k || !tmp) { free(k); free(tmp); memset(out, 0, sizeof(float) * c->w * c->h); return; }
    for (int i = 0; i < n; i++) { k[i] = expf(-(float)((i - r) * (i - r)) / (2 * sigma * sigma)); sum += k[i]; }
    for (int i = 0; i < n; i++) k[i] /= sum;
    for (int y = 0; y < c->h; y++)
        for (int x = 0; x < c->w; x++) {
            float a = 0;
            for (int i = -r; i <= r; i++) if (x + i >= 0 && x + i < c->w) a += k[i + r] * c->px[((size_t)y * c->w + x + i) * 4 + 3];
            tmp[(size_t)y * c->w + x] = a / 255;
        }
    for (int y = 0; y < c->h; y++)
        for (int x = 0; x < c->w; x++) {
            float a = 0;
            for (int i = -r; i <= r; i++) if (y + i >= 0 && y + i < c->h) a += k[i + r] * tmp[(size_t)(y + i) * c->w + x];
            out[(size_t)y * c->w + x] = a;
        }
    free(k); free(tmp);
}

enum { QUADS = 256 };                                /* (the most sprites one call draws: the 212 bulbs) */
typedef struct { SDL_Texture *tx; int n; SDL_Vertex v[4 * QUADS]; } Batch;   /* sprites of one sheet, drawn in one call */

struct Midway {
    SDL_Renderer *r; float sc; int premul, maxtex; MidwayLayout lay;
    SDL_BlendMode blend;
    Col fill;
    SDL_Texture *park_tx, *dusk_tx, *sheet[8]; int nsheet;
    Sprite beam, spark[5], mote, string[2], pennant[6], bulb[2], glint;
    float park_s, park_w, park_h;                    /* the park: points per picture pixel; its size in device pixels */
    float pen_x[42], pen_y[42]; int pen_c[42];       /* the pennants: where each hangs (points) and its colour */
    float bulb_x[256], bulb_y[256]; int nbulb, ring_end[3];   /* the bulbs, ring by ring: the sign's, the poster's, Play's */
    Batch batch;
    int index[6 * QUADS];                            /* (two triangles a quad) */
};

/* A premultiplied canvas as a texture, in the renderer's blend convention (midway.h). */
static SDL_Texture *upload(Midway *m, const uint8_t *px, int w, int h, int nearest) {
    SDL_Texture *t = SDL_CreateTexture(m->r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
    if (!t) return NULL;
    if (m->premul) SDL_UpdateTexture(t, NULL, px, w * 4);
    else {                                           /* straight alpha for SDL_BLENDMODE_BLEND */
        uint8_t *s = malloc((size_t)w * h * 4);
        if (!s) { SDL_DestroyTexture(t); return NULL; }
        for (size_t i = 0; i < (size_t)w * h; i++) {
            unsigned a = px[i * 4 + 3];
            for (int ch = 0; ch < 3; ch++) s[i * 4 + ch] = a ? (uint8_t)(px[i * 4 + ch] * 255u / a > 255 ? 255 : px[i * 4 + ch] * 255u / a) : 0;
            s[i * 4 + 3] = (uint8_t)a;
        }
        SDL_UpdateTexture(t, NULL, s, w * 4);
        free(s);
    }
    SDL_SetTextureBlendMode(t, m->blend);
    SDL_SetTextureScaleMode(t, nearest ? SDL_ScaleModeNearest : SDL_ScaleModeLinear);
    return t;
}
SDL_Texture *midway_upload(Midway *m, const uint8_t *px, int w, int h, SDL_Texture *reuse) {
    if (!m || !px || w <= 0 || h <= 0) return NULL;
    int tw = 0, th = 0;
    if (reuse && SDL_QueryTexture(reuse, NULL, NULL, &tw, &th) == 0 && tw == w && th == h) {
        if (m->premul) SDL_UpdateTexture(reuse, NULL, px, w * 4);
        else {
            SDL_Texture *t = upload(m, px, w, h, 0);   /* (straight alpha: converted on a fresh texture) */
            if (!t) return reuse;
            SDL_DestroyTexture(reuse);
            return t;
        }
        return reuse;
    }
    if (reuse) SDL_DestroyTexture(reuse);
    return upload(m, px, w, h, 0);
}
int midway_premultiplied(const Midway *m) { return m && m->premul; }
/* Sprites drawn side by side on one texture (a sheet), each with a clear border, so a frame draws every sprite of a
 * kind (all 212 bulbs, all of a swag's pennants, all the sparks) in one call and in the Mac's order.
 * A spec: a sprite w x h points, its anchor (ax, ay) points from its top-left, drawn by `draw` at `ps` pixels a point
 * (the device's scale times ss) inside a border of `pad` pixels (so a turned or scaled copy's edges stay soft). ss 2 for
 * hard-edged sprites placed between pixels or turned (pennants, bulbs): the renderer's bilinear filter then softens
 * their edges by half a device pixel, not a whole one, near what Core Animation's vector layers show. */
typedef void (*Painter)(Canvas *c, float ps, float pad, const void *arg);
typedef struct { float w, h, ax, ay, ss; Painter draw; const void *arg; } Spec;
static int sheet(Midway *m, const Spec *sp, int n, Sprite *out) {
    Canvas *c[8]; float ps[8]; int W = 0, H = 0, x = 0, y = 0, row = 0, ok = 1;
    const float pad = 2;
    if (n > 8 || m->nsheet >= 8) return 0;
    for (int i = 0; i < n; i++) {
        float big = sp[i].w > sp[i].h ? sp[i].w : sp[i].h;
        ps[i] = m->sc * sp[i].ss;
        if (big * ps[i] + 2 * pad > m->maxtex) ps[i] = (m->maxtex - 2 * pad) / big;   /* (too big for a texture: coarser, stretched) */
        c[i] = cv_new((int)ceilf(sp[i].w * ps[i] + 2 * pad), (int)ceilf(sp[i].h * ps[i] + 2 * pad));
        if (!c[i]) { ok = 0; n = i; break; }
        sp[i].draw(c[i], ps[i], pad, sp[i].arg);
        if (x + c[i]->w > m->maxtex) { x = 0; y += row; row = 0; }   /* (side by side; a new row when a row is full) */
        out[i] = (Sprite){NULL, (float)x, (float)y, (float)c[i]->w, (float)c[i]->h, pad + sp[i].ax * ps[i], pad + sp[i].ay * ps[i], m->sc / ps[i], 0, 0};
        x += c[i]->w;
        if (c[i]->h > row) row = c[i]->h;
        if (x > W) W = x;
        if (y + row > H) H = y + row;
    }
    Canvas *all = ok && H <= m->maxtex ? cv_new(W, H) : NULL;
    if (all) for (int i = 0; i < n; i++) cv_over(all, c[i], out[i].x, out[i].y, 1);
    for (int i = 0; i < n; i++) cv_free(c[i]);
    SDL_Texture *t = all ? upload(m, all->px, W, H, 0) : NULL;
    cv_free(all);
    if (!t) return 0;
    m->sheet[m->nsheet++] = t;
    for (int i = 0; i < n; i++) { out[i].tx = t; out[i].tw = (float)W; out[i].th = (float)H; }
    return 1;
}

/* A soft point of light (glow_img, launcher.m:124): white at the centre, the colour at 0.9 by 22 % of the radius, clear
 * at the radius. arg: {colour, radius in points}. */
typedef struct { Col c; float rad; } GlowArg;
static void paint_glow(Canvas *c, float ps, float pad, const void *arg) {
    const GlowArg *g = arg;
    Stop st[3] = {{0, col(255, 255, 255)}, {0.22f, with_alpha(g->c, 0.9f)}, {1, with_alpha(g->c, 0)}};
    float cx = pad + g->rad * ps, cy = cx, R = g->rad * ps;
    for (int j = 0; j < c->h; j++)
        for (int i = 0; i < c->w; i++) {
            float d = hypotf(i + 0.5f - cx, j + 0.5f - cy) / R;
            if (d < 1) put(c->px + ((size_t)j * c->w + i) * 4, stops_at(st, 3, d), 1);
        }
}
/* A marquee bulb (bulb_img, launcher.m:114), 22 points square, centred: unlit, with the lit bulb over it at `lit`
 * (the chase shows the lit layer at 1, or at 0.1 for a bulb that's out). */
static void paint_bulb_layer(Canvas *c, float ps, float pad, int lit) {
    Col bulb = col(255, 236, 150);
    float cx = pad + 11 * ps, cy = cx;
    if (lit) {                                   /* the glow: the bulb's colour at 0.8, clear at 11 points */
        for (int j = 0; j < c->h; j++)
            for (int i = 0; i < c->w; i++) {
                float d = hypotf(i + 0.5f - cx, j + 0.5f - cy) / (11 * ps);
                if (d < 1) put(c->px + ((size_t)j * c->w + i) * 4, with_alpha(bulb, 0.8f * (1 - d)), 1);
            }
    }
    cv_circle(c, cx, cy, 4 * ps, col(96, 56, 16));   /* the socket, 8 across */
    /* the glass, 6 across: a radial gradient from a highlight up and left (relativeCenterPosition -0.3, 0.3 in the
     * sprite's y-up space) to a circle round the glass's square */
    Col g0 = lit ? col(255, 255, 255) : col(170, 130, 70), g1 = lit ? bulb : col(110, 76, 34);
    float sx = cx - 0.9f * ps, sy = cy - 0.9f * ps, R = 3 * 1.41421356f * ps, dx = cx - sx, dy = cy - sy;
    float a2 = R * R - (dx * dx + dy * dy);
    for (int j = (int)(cy - 4 * ps); j <= (int)(cy + 4 * ps) + 1; j++)
        for (int i = (int)(cx - 4 * ps); i <= (int)(cx + 4 * ps) + 1; i++) {
            if (i < 0 || j < 0 || i >= c->w || j >= c->h) continue;
            float px = i + 0.5f, py = j + 0.5f, cov = clampf(0.5f - (hypotf(px - cx, py - cy) - 3 * ps), 0, 1);
            if (cov <= 0) continue;
            float qx = px - sx, qy = py - sy, qd = qx * dx + qy * dy;       /* the circle through p of the cone from s to the end circle */
            float t = (-qd + sqrtf(qd * qd + a2 * (qx * qx + qy * qy))) / a2;
            put(c->px + ((size_t)j * c->w + i) * 4, mix(g0, g1, clampf(t, 0, 1)), cov);
        }
}
static void paint_bulb(Canvas *c, float ps, float pad, const void *arg) {
    float lit = *(const float *)arg;
    paint_bulb_layer(c, ps, pad, 0);
    Canvas *l = cv_new(c->w, c->h);
    if (!l) return;
    paint_bulb_layer(l, ps, pad, 1);
    cv_over(c, l, 0, 0, lit);
    cv_free(l);
}
/* A searchlight (launcher.m:192): a triangle 160 x 760 points, its apex at the bottom centre, filled from the apex's warm
 * white at 0.55 to a clear blue at the top. */
static void paint_beam(Canvas *c, float ps, float pad, const void *arg) {
    (void)arg;
    cv_tri(c, pad + 80 * ps, pad + 760 * ps, pad + 160 * ps, pad, pad, pad, col(255, 255, 255));
    Col apex = cola(255, 242, 204, 0.55f), top = cola(179, 191, 255, 0);
    for (int j = 0; j < c->h; j++) {
        Col k = mix(top, apex, clampf((j + 0.5f - pad) / (760 * ps), 0, 1));
        for (int i = 0; i < c->w; i++) {
            uint8_t *p = c->px + ((size_t)j * c->w + i) * 4;
            float a = p[3] / 255.f * k.a;
            p[0] = (uint8_t)lrintf(k.r * a * 255); p[1] = (uint8_t)lrintf(k.g * a * 255); p[2] = (uint8_t)lrintf(k.b * a * 255); p[3] = (uint8_t)lrintf(a * 255);
        }
    }
}
/* A pennant (launcher.m:221-225): the triangle (-7,0) (7,0) (0,17) hanging from its anchor, black at 0.3 round its edge
 * (0.6 points), its shadow black at 0.4, 2 points down, blurred by Core Animation's radius 2. */
static void paint_pennant(Canvas *c, float ps, float pad, const void *arg) {
    Col fill = *(const Col *)arg, edge = cola(0, 0, 0, 0.3f);
    float ax = pad + 14 * ps, ay = pad + 6 * ps, hw = 0.3f * ps;
    float v[6] = {ax - 7 * ps, ay, ax + 7 * ps, ay, ax, ay + 17 * ps};
    Canvas *f = cv_new(c->w, c->h);
    float *sh = malloc(sizeof(float) * c->w * c->h);
    if (!f || !sh) { cv_free(f); free(sh); return; }
    for (int j = 0; j < c->h; j++)
        for (int i = 0; i < c->w; i++) {
            float d = tri_dist(i + 0.5f, j + 0.5f, v);
            uint8_t *p = f->px + ((size_t)j * c->w + i) * 4;
            put(p, fill, clampf(0.5f - d, 0, 1));
            put(p, edge, clampf(0.5f - (fabsf(d) - hw), 0, 1));
        }
    blur_alpha(f, 2 * ps, sh);
    int dy = (int)lrintf(2 * ps);
    for (int j = 0; j < c->h; j++)
        for (int i = 0; i < c->w; i++)
            if (j - dy >= 0) put(c->px + ((size_t)j * c->w + i) * 4, cola(0, 0, 0, 0.4f), sh[(size_t)(j - dy) * c->w + i]);
    cv_over(c, f, 0, 0, 1);
    cv_free(f); free(sh);
}
/* A bunting string (launcher.m:215-217): the quadratic from (x0, ytop) through ((x0+x1)/2, ytop + 2 sag) to (x1, ytop),
 * 1.2 points, Generic RGB (0.95, 0.92, 0.85) at 0.7. Its sprite's top-left is (x0, ytop - 2) points. */
typedef struct { float x0, x1, ytop, sag; } StringArg;
static void paint_string(Canvas *c, float ps, float pad, const void *arg) {
    const StringArg *s = arg;
    Col k = generic(0.95f, 0.92f, 0.85f, 0.7f);
    float len = s->x1 - s->x0;
    for (int i = 0; i < c->w; i++) {
        float x = (i + 0.5f - pad) / ps, u = x / len;              /* (points from x0) */
        float ends = clampf(0.5f + x * ps, 0, 1) * clampf(0.5f + (len - x) * ps, 0, 1);   /* (butt ends) */
        if (ends <= 0) continue;
        u = clampf(u, 0, 1);
        float y = 2 + 4 * s->sag * u * (1 - u), slope = 4 * s->sag * (1 - 2 * u) / len, n = sqrtf(1 + slope * slope);
        for (int j = 0; j < c->h; j++) {
            float d = fabsf((j + 0.5f - pad) / ps - y) / n * ps;   /* (pixels from the curve) */
            float cov = clampf(0.6f * ps + 0.5f - d, 0, 1) * ends;
            if (cov > 0) put(c->px + ((size_t)j * c->w + i) * 4, k, cov);
        }
    }
}
/* The glint (launcher.m:241-244): a band 90 points wide, white clear at its sides and 0.32 at its middle. */
static void paint_glint(Canvas *c, float ps, float pad, const void *arg) {
    float w = *(const float *)arg * ps;
    for (int i = 0; i < c->w; i++) {
        float u = (i + 0.5f - pad) / w;
        if (u <= 0 || u >= 1) continue;
        Col k = cola(255, 255, 255, 0.32f * (1 - fabsf(2 * u - 1)));
        for (int j = (int)pad; j < c->h - (int)pad; j++) put(c->px + ((size_t)j * c->w + i) * 4, k, 1);
    }
}
/* The dusk (launcher.m:186-189): a CAGradientLayer over the whole window, its first stop at the bottom; Generic RGB,
 * interpolated as Generic RGB; see-through over a park, opaque without one. One column, stretched across. */
static void paint_dusk(Canvas *c, float ps, float pad, const void *arg) {
    int park = *(const int *)arg;
    (void)pad;
    Stop st[4] = {{0, {0.03f, 0.03f, 0.10f, park ? 0.80f : 1}}, {0.38f, {0.26f, 0.08f, 0.22f, park ? 0.66f : 1}},
                  {0.62f, {0.10f, 0.05f, 0.18f, park ? 0.74f : 1}}, {1, {0.03f, 0.03f, 0.09f, park ? 0.86f : 1}}};   /* (top-down) */
    for (int j = 0; j < c->h; j++) {
        Col g = stops_at(st, 4, (j + 0.5f) / (WIN_H * ps)), k = generic(g.r, g.g, g.b, g.a);
        for (int i = 0; i < c->w; i++) {
            uint8_t *p = c->px + ((size_t)j * c->w + i) * 4;
            p[0] = (uint8_t)lrintf(k.r * k.a * 255); p[1] = (uint8_t)lrintf(k.g * k.a * 255); p[2] = (uint8_t)lrintf(k.b * k.a * 255); p[3] = (uint8_t)lrintf(k.a * 255);
        }
    }
}

/* ---- the park ---- */
static void make_park(Midway *m, const Image *park, int soft) {
    if (m->park_tx) SDL_DestroyTexture(m->park_tx);
    if (m->dusk_tx) SDL_DestroyTexture(m->dusk_tx);
    m->park_tx = m->dusk_tx = NULL;
    int has = park && park->w > 0 && park->h > 0;
    {   /* the dusk: one column of the window's height, stretched across */
        Canvas *c = cv_new(2, (int)lrintf(WIN_H * m->sc));
        if (c) { paint_dusk(c, m->sc, 0, &has); m->dusk_tx = upload(m, c->px, c->w, c->h, 1); cv_free(c); }
    }
    if (!has) return;
    float s = 0.6f;                              /* GOG's pictures: shrunk a little */
    if (!soft) for (s = 1; s < 4 && (park->w * s < WIN_W * 1.15f || park->h * s < WIN_H * 1.15f); s++) {}   /* the player's park: the smallest whole size with room to pan */
    m->park_s = s; m->park_w = park->w * s * m->sc; m->park_h = park->h * s * m->sc;
    if (s * m->sc >= 1 && park->w <= m->maxtex && park->h <= m->maxtex)   /* enlarged: hard pixel edges (kCAFilterNearest) */
        m->park_tx = upload(m, park->px, park->w, park->h, 1);
    else {                                       /* made smaller: averaged here (Core Animation filters it trilinear) */
        int w = (int)lrintf(m->park_w), h = (int)lrintf(m->park_h);
        if (w > m->maxtex) { h = (int)((long)h * m->maxtex / w); w = m->maxtex; }
        if (h > m->maxtex) { w = (int)((long)w * m->maxtex / h); h = m->maxtex; }
        Image *k = img_scaled(park, box(0, 0, (float)park->w, (float)park->h), w, h);
        if (k) m->park_tx = upload(m, k->px, w, h, 1);
        img_free(k);
    }
}
void midway_set_backdrop(Midway *m, const Image *park, int soft) { if (m) make_park(m, park, soft); }
void midway_set_park(Midway *m, const Image *park) { midway_set_backdrop(m, park, 0); }

Image *midway_park_from_shots(const Image *const *shots, int n) {
    if (n < 1 || !shots[0]) return NULL;
    if (n > 4) n = 4;
    int sw = shots[0]->w, sh = shots[0]->h;
    Canvas *c = cv_new(sw * n, sh * 2);
    if (!c) return NULL;
    for (int k = 0; k < n; k++) {
        const Image *top = shots[k], *bot = shots[(k + 1) % n];
        if (top) {
            Image *t = top->w == sw && top->h == sh ? NULL : img_scaled(top, box(0, 0, (float)top->w, (float)top->h), sw, sh);
            cv_image(c, t ? t : top, (float)(sw * k), 0, 1); img_free(t);
        }
        if (bot) {
            Image *t = bot->w == sw && bot->h == sh ? NULL : img_scaled(bot, box(0, 0, (float)bot->w, (float)bot->h), sw, sh);
            cv_image(c, t ? t : bot, (float)(sw * k), (float)sh, 1); img_free(t);
        }
    }
    Image *out = malloc(sizeof *out);
    if (!out) { cv_free(c); return NULL; }
    out->w = c->w; out->h = c->h; out->px = c->px;
    free(c);                                     /* (the pixels move to the image) */
    return out;
}

/* ---- making it ---- */
/* Points round a rectangle, clockwise from its top-left corner (ring_points, launcher.m:140). */
static int ring(Midway *m, Box r, float step) {
    float per = 2 * (r.w + r.h);
    int n = (int)lround(per / step);
    for (int i = 0; i < n && m->nbulb < 256; i++) {
        float d = per * i / n, x, y;
        if (d < r.w) { x = r.x + d; y = r.y; }
        else if ((d -= r.w) < r.h) { x = r.x + r.w; y = r.y + d; }
        else if ((d -= r.h) < r.w) { x = r.x + r.w - d; y = r.y + r.h; }
        else { d -= r.w; x = r.x; y = r.y + r.h - d; }
        m->bulb_x[m->nbulb] = x; m->bulb_y[m->nbulb] = y; m->nbulb++;
    }
    return m->nbulb;
}

Midway *midway_new(SDL_Renderer *r, float scale, const Image *park, MidwayLayout lay) {
    if (!r || scale <= 0) return NULL;
    Midway *m = calloc(1, sizeof *m);
    if (!m) return NULL;
    m->r = r; m->sc = scale; m->lay = lay;
    SDL_RendererInfo info;
    m->maxtex = 4096;
    if (SDL_GetRendererInfo(r, &info) == 0 && info.max_texture_width > 0)
        m->maxtex = info.max_texture_height > 0 && info.max_texture_height < info.max_texture_width ? info.max_texture_height : info.max_texture_width;
    /* premultiplied alpha if the renderer takes a custom blend mode; else straight alpha and SDL's blend */
    m->blend = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD,
                                          SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
    {
        SDL_Texture *t = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, 1, 1);
        m->premul = t && SDL_SetTextureBlendMode(t, m->blend) == 0;
        if (t) SDL_DestroyTexture(t);
        if (!m->premul) m->blend = SDL_BLENDMODE_BLEND;
    }
    static const int corner[6] = {0, 1, 2, 0, 2, 3};
    for (int q = 0; q < QUADS; q++)
        for (int k = 0; k < 6; k++) m->index[q * 6 + k] = q * 4 + corner[k];
    m->fill = generic(0.04f, 0.04f, 0.10f, 1);   /* the back layer's colour */
    make_park(m, park, 0);
    int ok = 1;
    {   Spec sp = {160, 760, 80, 760, 1, paint_beam, NULL}; ok &= sheet(m, &sp, 1, &m->beam); }
    {   /* the sparks (five colours: coral, gold, sky, lilac, green) and the mote */
        static const int fc[5][3] = {{255, 120, 90}, {255, 214, 90}, {120, 200, 255}, {190, 130, 255}, {120, 240, 150}};
        GlowArg g[6]; Spec sp[6]; Sprite out[6];
        for (int i = 0; i < 6; i++) {
            g[i] = (GlowArg){i < 5 ? col(fc[i][0], fc[i][1], fc[i][2]) : col(255, 220, 150), i < 5 ? 12.f : 5.f};
            sp[i] = (Spec){2 * g[i].rad, 2 * g[i].rad, g[i].rad, g[i].rad, 1, paint_glow, &g[i]};
        }
        ok &= sheet(m, sp, 6, out);
        memcpy(m->spark, out, sizeof m->spark); m->mote = out[5];
    }
    {   /* the bunting: two swags, line 0 from -10 to 52 % across, line 1 from 48 % to 10 past the edge */
        static const int flagc[6][3] = {{214, 40, 40}, {250, 204, 44}, {40, 120, 220}, {60, 170, 80}, {245, 130, 30}, {150, 80, 210}};
        Col k[6]; Spec sp[6];
        for (int i = 0; i < 6; i++) { k[i] = col(flagc[i][0], flagc[i][1], flagc[i][2]); sp[i] = (Spec){28, 32, 14, 6, 2, paint_pennant, &k[i]}; }
        ok &= sheet(m, sp, 6, m->pennant);
        StringArg s[2]; Spec ss[2];
        for (int line = 0, p = 0; line < 2; line++) {
            s[line] = (StringArg){line ? WIN_W * 0.48f : -10, line ? WIN_W + 10 : WIN_W * 0.52f, 2.f + line * 6, 14};
            ss[line] = (Spec){s[line].x1 - s[line].x0, s[line].sag + 4, 0, 0, 1, paint_string, &s[line]};
            int n = (int)((s[line].x1 - s[line].x0) / 24);
            for (int i = 1; i < n && p < 42; i++, p++) {
                double u = (double)i / n;
                m->pen_x[p] = (float)(s[line].x0 + (s[line].x1 - s[line].x0) * u);
                m->pen_y[p] = (float)(s[line].ytop + 4 * s[line].sag * u * (1 - u));
                m->pen_c[p] = (i + line * 3) % 6;
            }
        }
        ok &= sheet(m, ss, 2, m->string);
    }
    {   /* the bulbs: round the sign (inset 7, every 15 points), the poster (inset 6, 18), Play (outset 8, 16); a bulb
         * that's out, and one that's lit */
        m->ring_end[0] = ring(m, inset(lay.sign, 7), 15);
        m->ring_end[1] = ring(m, inset(lay.poster, 6), 18);
        m->ring_end[2] = ring(m, inset(lay.play, -8), 16);
        float o[2] = {0.1f, 1};
        Spec sp[2] = {{22, 22, 11, 11, 2, paint_bulb, &o[0]}, {22, 22, 11, 11, 2, paint_bulb, &o[1]}};
        ok &= sheet(m, sp, 2, m->bulb);
    }
    if (lay.logo.w > 0 && lay.logo.h > 0) {
        float w = 90;
        Spec sp = {90, lay.logo.h * 2.2f, 45, lay.logo.h * 1.1f, 1, paint_glint, &w};
        ok &= sheet(m, &sp, 1, &m->glint);
    }
    if (!ok || !m->dusk_tx) { midway_free(m); return NULL; }
    return m;
}

void midway_free(Midway *m) {
    if (!m) return;
    if (m->park_tx) SDL_DestroyTexture(m->park_tx);
    if (m->dusk_tx) SDL_DestroyTexture(m->dusk_tx);
    for (int i = 0; i < m->nsheet; i++) SDL_DestroyTexture(m->sheet[i]);
    free(m);
}

/* ---- a frame ---- */
/* The queued sprites of one sheet, drawn in one call (SDL_RenderGeometry), in the order they came. */
static void flush(Midway *m) {
    Batch *b = &m->batch;
    if (b->n) SDL_RenderGeometry(m->r, b->tx, b->v, b->n * 4, m->index, b->n * 6);
    b->n = 0;
}
/* s with its anchor on the point (x, y), scaled by k about it and turned by a (radians, Core Animation's sense: + is
 * anticlockwise on screen) about it, at opacity o; queued. */
static void put_sprite(Midway *m, const Sprite *s, float x, float y, float k, double a, float o) {
    Batch *b = &m->batch;
    if (!s->tx || o <= 0) return;
    if (b->tx != s->tx || b->n == QUADS) { flush(m); b->tx = s->tx; }
    Uint8 al = (Uint8)lrintf(clampf(o, 0, 1) * 255);
    SDL_Color c = {255, 255, 255, al};
    if (m->premul) c.r = c.g = c.b = al;         /* (premultiplied: the colour fades with the alpha) */
    float kk = k * s->k, ca = (float)cos(a), sa = (float)sin(a), px = x * m->sc, py = y * m->sc;
    static const float cx[4] = {0, 1, 1, 0}, cy[4] = {0, 0, 1, 1};
    SDL_Vertex *v = b->v + b->n++ * 4;
    for (int i = 0; i < 4; i++) {
        float dx = (cx[i] * s->w - s->ax) * kk, dy = (cy[i] * s->h - s->ay) * kk;   /* (from the anchor, device pixels) */
        v[i].position.x = px + dx * ca + dy * sa; v[i].position.y = py - dx * sa + dy * ca;
        v[i].color = c;
        v[i].tex_coord.x = (s->x + cx[i] * s->w) / s->tw; v[i].tex_coord.y = (s->y + cy[i] * s->h) / s->th;
    }
}

void midway_draw_back(Midway *m, double t) {
    if (!m) return;
    SDL_Renderer *r = m->r;
    Uint8 cr, cg, cb, ca; SDL_BlendMode bm;
    SDL_GetRenderDrawColor(r, &cr, &cg, &cb, &ca); SDL_GetRenderDrawBlendMode(r, &bm);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);   /* the back layer's colour, opaque */
    SDL_SetRenderDrawColor(r, (Uint8)lrintf(m->fill.r * 255), (Uint8)lrintf(m->fill.g * 255), (Uint8)lrintf(m->fill.b * 255), 255);
    SDL_FRect all = {0, 0, WIN_W * m->sc, WIN_H * m->sc};
    SDL_RenderFillRectF(r, &all);
    SDL_SetRenderDrawColor(r, cr, cg, cb, ca); SDL_SetRenderDrawBlendMode(r, bm);
    if (m->park_tx) {                            /* a slow pan round the park, corner to corner and back (~2 min), in whole
                                                  * picture pixels (no shimmer), rounded to whole device pixels */
        float sc = m->sc, W = m->park_w, H = m->park_h;   /* (device pixels) */
        double mx = W > WIN_W * sc ? W - WIN_W * sc : 0, my = H > WIN_H * sc ? H - WIN_H * sc : 0;
        double u = 0.5 - 0.5 * cos(t * TAU / 130), v = 0.5 - 0.5 * cos(t * TAU / 97 + 1.1);
        double q = (m->park_s > 0.5f ? m->park_s : 0.5f) * sc;   /* (one picture pixel, in device pixels) */
        double x = -round(mx * u / q) * q, yup = -round(my * v / q) * q;
        if (W < WIN_W * sc) x = round((WIN_W * sc - W) / 2);
        if (H < WIN_H * sc) yup = round((WIN_H * sc - H) / 2);
        SDL_FRect d = {(float)round(x), (float)round(WIN_H * sc - yup - H), W, H};   /* (the layer's origin is its bottom-left) */
        SDL_RenderCopyF(r, m->park_tx, NULL, &d);
    }
    if (m->dusk_tx) SDL_RenderCopyF(r, m->dusk_tx, NULL, &all);
    for (int i = 0; i < 2; i++)                  /* searchlights from 30 points below the window: slow sweeps, out of step */
        put_sprite(m, &m->beam, i ? WIN_W * 0.78f : WIN_W * 0.18f, WIN_H + 30, 1,
                   (i ? 0.32 : -0.32) + 0.38 * sin(t * (i ? 0.23 : 0.19) + i * 2.1), 0.16f);
    flush(m);
    for (int b = 0; b < 3; b++) {                /* fireworks: each burst 2.4 s, then dark; three, out of step */
        double cyc = 7.5, d = fmod(t + b * 2.6, cyc);
        if (d > 2.4 || d < 0) continue;
        unsigned rnd = (unsigned)floor((t + b * 2.6) / cyc) * 3 + (unsigned)b;
        double cx = 110 + 460 * frand(rnd * 41 + 7), cy = 205 + 80 * frand(rnd * 43 + 11);   /* (the sky round the tagline) */
        float o = (float)(d < 0.15 ? d / 0.15 : 1 - (d - 0.15) / 2.25), k = (float)(1 - 0.5 * d / 2.4);
        for (int i = 0; i < 26; i++) {
            double a = i * TAU / 26 + frand(rnd * 7 + (unsigned)i) * 0.2, sp = 70 + 40 * frand(rnd * 13 + (unsigned)i);
            double rr = sp * (1 - exp(-d * 2.2));
            put_sprite(m, &m->spark[(b * 2 + i % 2) % 5], (float)(cx + rr * cos(a)), (float)(cy - rr * sin(a) + 22 * d * d), k, 0, o);
        }
    }
    flush(m);
}

void midway_draw_front(Midway *m, double t, int page_open) {
    if (!m) return;
    SDL_Renderer *r = m->r;
    for (int line = 0, p = 0; line < 2; line++) {   /* the bunting: the string, then each pennant swaying about its top */
        put_sprite(m, &m->string[line], line ? WIN_W * 0.48f : -10, line * 6.f, 1, 0, 1);
        flush(m);
        for (int e = p + 21; p < e; p++) put_sprite(m, &m->pennant[m->pen_c[p]], m->pen_x[p], m->pen_y[p], 1, 0.08 * sin(t * 2.1 + p * 0.7), 1);
        flush(m);
    }
    long step = (long)floor(t * 8);              /* the chase: every third bulb lit, moving on 8 times a second; the poster's
                                                  * the other way round */
    for (int rg = 0, i = 0; rg < 3; rg++) {
        int first = i;
        if (page_open && rg < 2) { i = m->ring_end[rg]; continue; }   /* (a page: the sign's and the poster's go out) */
        for (; i < m->ring_end[rg]; i++) {
            long k = (long)(i - first) - step * (rg == 1 ? -1 : 1);
            put_sprite(m, &m->bulb[((k % 3) + 3) % 3 == 0], m->bulb_x[i], m->bulb_y[i], 1, 0, 1);
        }
    }
    flush(m);
    if (page_open) return;
    {   double g = fmod(t, 7.0);                 /* the glint: across the logo in 1.1 s, every 7 s, clipped to it */
        if (g >= 0 && g < 1.1 && m->glint.tx) {
            Box L = m->lay.logo;
            int x0 = (int)lrintf(L.x * m->sc), y0 = (int)lrintf(L.y * m->sc);
            SDL_Rect old, clip = {x0, y0, (int)lrintf((L.x + L.w) * m->sc) - x0, (int)lrintf((L.y + L.h) * m->sc) - y0};
            SDL_bool had = SDL_RenderIsClipEnabled(r);
            SDL_RenderGetClipRect(r, &old);
            if (had) SDL_IntersectRect(&clip, &old, &clip);
            SDL_RenderSetClipRect(r, &clip);
            put_sprite(m, &m->glint, (float)(L.x - 90 + (L.w + 180) * g / 1.1), L.y + L.h / 2, 1, -0.42, 1);
            flush(m);
            SDL_RenderSetClipRect(r, had ? &old : NULL);
        }
    }
    for (int i = 0; i < 22; i++) {               /* motes rising, swaying, twinkling */
        double sp = 9 + 10 * frand((unsigned)i * 11 + 3), span = WIN_H + 40;
        double y = fmod(t * sp + span * frand((unsigned)i * 13 + 5), span) - 20;
        double x = WIN_W * frand((unsigned)i * 17 + 9) + 14 * sin(t * (0.4 + 0.3 * frand((unsigned)i * 19)) + i);
        put_sprite(m, &m->mote, (float)x, (float)(WIN_H - y), 1, 0, (float)(0.25 + 0.45 * (0.5 + 0.5 * sin(t * (1.3 + frand((unsigned)i * 23)) + i * 1.7))));
    }
    flush(m);
}

/* ---- reduce motion ---- */
#if !defined(_WIN32) && !defined(__APPLE__)
/* 1 if the file at $XDG_CONFIG_HOME (or ~/.config) / rel has a line `key=` one of the values in `off` (comma-separated). */
static int config_says(const char *rel, const char *key, const char *const *off) {
    char path[1024];
    const char *x = getenv("XDG_CONFIG_HOME"), *h = getenv("HOME");
    if (x && *x) snprintf(path, sizeof path, "%s/%s", x, rel);
    else if (h && *h) snprintf(path, sizeof path, "%s/.config/%s", h, rel);
    else return 0;
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char line[512]; int hit = 0; size_t kl = strlen(key);
    while (!hit && fgets(line, sizeof line, f)) {
        char *s = line;
        while (*s == ' ' || *s == '\t') s++;
        if (strncmp(s, key, kl)) continue;
        s += kl;
        while (*s == ' ' || *s == '\t') s++;
        if (*s++ != '=') continue;
        while (*s == ' ' || *s == '\t') s++;
        for (int i = 0; off[i]; i++) if (!strncmp(s, off[i], strlen(off[i])) && (s[strlen(off[i])] <= ' ')) hit = 1;
    }
    fclose(f);
    return hit;
}
#endif
int midway_reduce_motion(void) {
    const char *e = getenv("TP_REDUCE_MOTION");
    if (e && *e) return atoi(e) != 0;            /* (0: animate whatever the system says) */
#if defined(_WIN32)
#ifndef SPI_GETCLIENTAREAANIMATION
#define SPI_GETCLIENTAREAANIMATION 0x1042
#endif
    BOOL on = TRUE;                              /* Settings > Accessibility > Visual effects > Animation effects */
    return SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &on, 0) && !on;
#elif defined(__APPLE__)
    return 0;
#else
    static const char *const no[] = {"0", "false", "False", "FALSE", NULL}, *const zero[] = {"0", "0.0", NULL};
    const char *de = getenv("XDG_CURRENT_DESKTOP");
    if (de && (strstr(de, "GNOME") || strstr(de, "Unity") || strstr(de, "Budgie") || strstr(de, "Pantheon"))) {
        FILE *p = popen("gsettings get org.gnome.desktop.interface enable-animations 2>/dev/null", "r");   /* (dconf's database is binary) */
        if (p) {
            char buf[32] = "";
            char *got = fgets(buf, sizeof buf, p);
            pclose(p);
            if (got && !strncmp(buf, "false", 5)) return 1;
        }
    }
    return config_says("kdeglobals", "AnimationDurationFactor", zero) || config_says("gtk-3.0/settings.ini", "gtk-enable-animations", no)
        || config_says("gtk-4.0/settings.ini", "gtk-enable-animations", no);
#endif
}
