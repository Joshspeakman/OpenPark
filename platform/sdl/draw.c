/* The launch window's software canvas (draw.h). Each shape is drawn from its signed distance d (negative inside): the
 * coverage of a pixel is clamp(0.5 - d), so edges are smooth at any scale, and a soft shadow is the same distance through
 * a Gaussian. Colours compose premultiplied ("over"). stb_truetype and stb_image (vendor/stb) are compiled here. */
#include "draw.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#endif
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "../../vendor/stb/stb_truetype.h"
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#include "../../vendor/stb/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include "../../vendor/stb/stb_image_write.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
/* (gcc reports the libraries' unused static declarations, such as stbi_load_gif_from_memory, at the end of the file) */
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
Col mix(Col a, Col b, float t) { Col r = {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t}; return r; }

/* ---- the canvas ---- */
typedef struct { int x0, y0, x1, y1; } Span;            /* pixels x0 <= x < x1, y0 <= y < y1 */
static Span clip_of(const Canvas *c);
/* Clips nest: each cv_clip adds one (a canvas's draws stay inside all of its clips at once) and cv_unclip takes away
 * the latest. (One clip a canvas, replaced and then cleared, lost the launcher's repaint clip at the poster's sash:
 * everything drawn after it went over the whole view, the tiles over their own labels.) */
static struct { const Canvas *c; Span s; unsigned order; } clips[16];
static unsigned clip_order;

Canvas *cv_new(int w, int h) {
    Canvas *c = calloc(1, sizeof *c);
    if (!c) return NULL;
    c->w = w > 0 ? w : 1; c->h = h > 0 ? h : 1;
    c->px = calloc((size_t)c->w * c->h, 4);
    if (!c->px) { free(c); return NULL; }
    return c;
}
void cv_free(Canvas *c) {
    if (!c) return;
    for (int i = 0; i < 16; i++) if (clips[i].c == c) clips[i].c = NULL;   /* (all of its clips) */
    free(c->px); free(c);
}
void cv_fill(Canvas *c, Col k) {
    uint8_t p[4] = {(uint8_t)lrintf(k.r * k.a * 255), (uint8_t)lrintf(k.g * k.a * 255), (uint8_t)lrintf(k.b * k.a * 255), (uint8_t)lrintf(k.a * 255)};
    for (size_t i = 0; i < (size_t)c->w * c->h; i++) memcpy(c->px + i * 4, p, 4);
}
void cv_clip(Canvas *c, Box b) {
    int slot = -1;
    for (int i = 0; slot < 0 && i < 16; i++) if (!clips[i].c) slot = i;
    if (slot < 0) { fprintf(stderr, "draw: more than 16 clips\n"); return; }
    clips[slot].c = c; clips[slot].order = ++clip_order;
    clips[slot].s = (Span){(int)floorf(b.x), (int)floorf(b.y), (int)ceilf(b.x + b.w), (int)ceilf(b.y + b.h)};
}
void cv_unclip(Canvas *c) {
    int last = -1;
    for (int i = 0; i < 16; i++) if (clips[i].c == c && (last < 0 || clips[i].order > clips[last].order)) last = i;
    if (last >= 0) clips[last].c = NULL;
}
static Span clip_of(const Canvas *c) {
    Span s = {0, 0, c->w, c->h};
    for (int i = 0; i < 16; i++) if (clips[i].c == c) {
        Span k = clips[i].s;
        if (k.x0 > s.x0) s.x0 = k.x0;
        if (k.y0 > s.y0) s.y0 = k.y0;
        if (k.x1 < s.x1) s.x1 = k.x1;
        if (k.y1 < s.y1) s.y1 = k.y1;
    }
    return s;
}
/* The pixels a shape spanning [x0, x1) x [y0, y1) (floats, with room for its soft edge) can touch. */
static Span span_of(const Canvas *c, float x0, float y0, float x1, float y1) {
    Span k = clip_of(c), s = {(int)floorf(x0), (int)floorf(y0), (int)ceilf(x1), (int)ceilf(y1)};
    if (s.x0 < k.x0) s.x0 = k.x0;
    if (s.y0 < k.y0) s.y0 = k.y0;
    if (s.x1 > k.x1) s.x1 = k.x1;
    if (s.y1 > k.y1) s.y1 = k.y1;
    return s;
}
/* k at coverage `cov` over the pixel p. */
static inline void blend(uint8_t *p, Col k, float cov) {
    float a = k.a * cov;
    if (a <= 0.0005f) return;
    float keep = 1 - a;
    float r = k.r * a * 255 + p[0] * keep, g = k.g * a * 255 + p[1] * keep, b = k.b * a * 255 + p[2] * keep, al = a * 255 + p[3] * keep;
    p[0] = (uint8_t)(r > 255 ? 255 : r + 0.5f); p[1] = (uint8_t)(g > 255 ? 255 : g + 0.5f);
    p[2] = (uint8_t)(b > 255 ? 255 : b + 0.5f); p[3] = (uint8_t)(al > 255 ? 255 : al + 0.5f);
}
void cv_over(Canvas *dst, const Canvas *src, float x, float y, float alpha) {
    int ox = (int)lrintf(x), oy = (int)lrintf(y);
    Span s = span_of(dst, (float)ox, (float)oy, (float)(ox + src->w), (float)(oy + src->h));
    for (int j = s.y0; j < s.y1; j++) {
        const uint8_t *q = src->px + ((size_t)(j - oy) * src->w + (s.x0 - ox)) * 4;
        uint8_t *p = dst->px + ((size_t)j * dst->w + s.x0) * 4;
        for (int i = s.x0; i < s.x1; i++, p += 4, q += 4) {
            float a = q[3] / 255.f * alpha;
            if (a <= 0) continue;
            float keep = 1 - a;
            for (int ch = 0; ch < 3; ch++) { float v = q[ch] * alpha + p[ch] * keep; p[ch] = (uint8_t)(v > 255 ? 255 : v + 0.5f); }
            float v = a * 255 + p[3] * keep; p[3] = (uint8_t)(v > 255 ? 255 : v + 0.5f);
        }
    }
}

/* ---- distances ---- */
static inline float rrect_d(float px, float py, Box b, float r) {
    float cx = b.x + b.w * 0.5f, cy = b.y + b.h * 0.5f;
    float qx = fabsf(px - cx) - (b.w * 0.5f - r), qy = fabsf(py - cy) - (b.h * 0.5f - r);
    float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
    float in = qx > qy ? qx : qy;
    return sqrtf(ox * ox + oy * oy) + (in < 0 ? in : 0) - r;
}
static inline float radius_of(Box b, float r) {
    float m = (b.w < b.h ? b.w : b.h) * 0.5f;
    return r < 0 ? 0 : r > m ? m : r;
}

void cv_rrect(Canvas *c, Box b, float r, Col k) {
    if (b.w <= 0 || b.h <= 0) return;
    r = radius_of(b, r);
    Span s = span_of(c, b.x - 1, b.y - 1, b.x + b.w + 1, b.y + b.h + 1);
    for (int j = s.y0; j < s.y1; j++) {
        uint8_t *p = c->px + ((size_t)j * c->w + s.x0) * 4;
        for (int i = s.x0; i < s.x1; i++, p += 4) blend(p, k, clampf(0.5f - rrect_d(i + 0.5f, j + 0.5f, b, r), 0, 1));
    }
}
void cv_rrect_v(Canvas *c, Box b, float r, Col top, Col bottom) {
    if (b.w <= 0 || b.h <= 0) return;
    r = radius_of(b, r);
    Span s = span_of(c, b.x - 1, b.y - 1, b.x + b.w + 1, b.y + b.h + 1);
    for (int j = s.y0; j < s.y1; j++) {
        Col k = mix(top, bottom, clampf((j + 0.5f - b.y) / b.h, 0, 1));
        uint8_t *p = c->px + ((size_t)j * c->w + s.x0) * 4;
        for (int i = s.x0; i < s.x1; i++, p += 4) blend(p, k, clampf(0.5f - rrect_d(i + 0.5f, j + 0.5f, b, r), 0, 1));
    }
}
void cv_vgrad(Canvas *c, Box b, Col top, Col bottom) { cv_rrect_v(c, b, 0, top, bottom); }
void cv_rrect_line(Canvas *c, Box b, float r, float width, Col k) {
    if (b.w <= 0 || b.h <= 0 || width <= 0) return;
    r = radius_of(b, r);
    Box in = inset(b, width);
    float ri = r - width > 0 ? r - width : 0;
    Span s = span_of(c, b.x - 1, b.y - 1, b.x + b.w + 1, b.y + b.h + 1);
    for (int j = s.y0; j < s.y1; j++) {
        uint8_t *p = c->px + ((size_t)j * c->w + s.x0) * 4;
        for (int i = s.x0; i < s.x1; i++, p += 4) {
            float x = i + 0.5f, y = j + 0.5f;
            float outer = clampf(0.5f - rrect_d(x, y, b, r), 0, 1);
            float inner = in.w > 0 && in.h > 0 ? clampf(0.5f - rrect_d(x, y, in, ri), 0, 1) : 0;
            blend(p, k, outer - inner);
        }
    }
}
void cv_shadow(Canvas *c, Box b, float r, float blur, Col k) {
    if (b.w <= 0 || b.h <= 0) return;
    if (blur < 0.5f) { cv_rrect(c, b, r, k); return; }
    r = radius_of(b, r);
    float sigma = blur * 0.5f, reach = sigma * 3;
    Span s = span_of(c, b.x - reach, b.y - reach, b.x + b.w + reach, b.y + b.h + reach);
    for (int j = s.y0; j < s.y1; j++) {
        uint8_t *p = c->px + ((size_t)j * c->w + s.x0) * 4;
        for (int i = s.x0; i < s.x1; i++, p += 4) {
            float d = rrect_d(i + 0.5f, j + 0.5f, b, r);
            if (d >= reach) continue;                     /* (beyond 3 sigma: nothing; well inside: all of it) */
            blend(p, k, d <= -reach ? 1 : 0.5f * erfcf(d / (sigma * 1.41421356f)));
        }
    }
}
void cv_stripes(Canvas *c, Box b, float r, float period, float width, float angle_deg, Col k) {
    if (b.w <= 0 || b.h <= 0 || period <= 0) return;
    r = radius_of(b, r);
    float a = angle_deg * 3.14159265f / 180, ca = cosf(a), sa = sinf(a);
    Span s = span_of(c, b.x - 1, b.y - 1, b.x + b.w + 1, b.y + b.h + 1);
    for (int j = s.y0; j < s.y1; j++) {
        uint8_t *p = c->px + ((size_t)j * c->w + s.x0) * 4;
        for (int i = s.x0; i < s.x1; i++, p += 4) {
            float x = i + 0.5f, y = j + 0.5f;
            float inside = clampf(0.5f - rrect_d(x, y, b, r), 0, 1);
            if (inside <= 0) continue;
            float t = fmodf((x - b.x) * ca + (y - b.y) * sa, period);
            if (t < 0) t += period;
            float f = t <= width ? fminf(t, width - t) : -fminf(t - width, period - t);
            blend(p, k, inside * clampf(f + 0.5f, 0, 1));
        }
    }
}
void cv_circle(Canvas *c, float cx, float cy, float rad, Col k) {
    if (rad <= 0) return;
    Span s = span_of(c, cx - rad - 1, cy - rad - 1, cx + rad + 1, cy + rad + 1);
    for (int j = s.y0; j < s.y1; j++) {
        uint8_t *p = c->px + ((size_t)j * c->w + s.x0) * 4;
        for (int i = s.x0; i < s.x1; i++, p += 4) {
            float dx = i + 0.5f - cx, dy = j + 0.5f - cy;
            blend(p, k, clampf(0.5f - (sqrtf(dx * dx + dy * dy) - rad), 0, 1));
        }
    }
}
void cv_ring(Canvas *c, float cx, float cy, float rad, float width, Col k) {
    if (rad <= 0 || width <= 0) return;
    float o = rad + width * 0.5f;
    Span s = span_of(c, cx - o - 1, cy - o - 1, cx + o + 1, cy + o + 1);
    for (int j = s.y0; j < s.y1; j++) {
        uint8_t *p = c->px + ((size_t)j * c->w + s.x0) * 4;
        for (int i = s.x0; i < s.x1; i++, p += 4) {
            float dx = i + 0.5f - cx, dy = j + 0.5f - cy;
            blend(p, k, clampf(0.5f - (fabsf(sqrtf(dx * dx + dy * dy) - rad) - width * 0.5f), 0, 1));
        }
    }
}
void cv_ellipse_glow(Canvas *c, float cx, float cy, float rx, float ry, Col k) {
    if (rx <= 0 || ry <= 0) return;
    Span s = span_of(c, cx - rx, cy - ry, cx + rx, cy + ry);
    for (int j = s.y0; j < s.y1; j++) {
        uint8_t *p = c->px + ((size_t)j * c->w + s.x0) * 4;
        for (int i = s.x0; i < s.x1; i++, p += 4) {
            float dx = (i + 0.5f - cx) / rx, dy = (j + 0.5f - cy) / ry;
            float g = 1 - sqrtf(dx * dx + dy * dy);
            if (g > 0) blend(p, k, g * g);
        }
    }
}
void cv_glow(Canvas *c, float cx, float cy, float rad, Col k) { cv_ellipse_glow(c, cx, cy, rad, rad, k); }
static inline float dot2(float x, float y) { return x * x + y * y; }
void cv_tri(Canvas *c, float x0, float y0, float x1, float y1, float x2, float y2, Col k) {
    float minx = fminf(x0, fminf(x1, x2)), maxx = fmaxf(x0, fmaxf(x1, x2)), miny = fminf(y0, fminf(y1, y2)), maxy = fmaxf(y0, fmaxf(y1, y2));
    Span s = span_of(c, minx - 1, miny - 1, maxx + 1, maxy + 1);
    float e0x = x1 - x0, e0y = y1 - y0, e1x = x2 - x1, e1y = y2 - y1, e2x = x0 - x2, e2y = y0 - y2;
    float sg = e0x * e2y - e0y * e2x > 0 ? 1.f : -1.f;
    for (int j = s.y0; j < s.y1; j++) {
        uint8_t *p = c->px + ((size_t)j * c->w + s.x0) * 4;
        for (int i = s.x0; i < s.x1; i++, p += 4) {
            float px = i + 0.5f, py = j + 0.5f;
            float v0x = px - x0, v0y = py - y0, v1x = px - x1, v1y = py - y1, v2x = px - x2, v2y = py - y2;
            float h0 = clampf((v0x * e0x + v0y * e0y) / dot2(e0x, e0y), 0, 1), h1 = clampf((v1x * e1x + v1y * e1y) / dot2(e1x, e1y), 0, 1),
                  h2 = clampf((v2x * e2x + v2y * e2y) / dot2(e2x, e2y), 0, 1);
            float d0 = dot2(v0x - e0x * h0, v0y - e0y * h0), d1 = dot2(v1x - e1x * h1, v1y - e1y * h1), d2 = dot2(v2x - e2x * h2, v2y - e2y * h2);
            float s0 = sg * (v0x * e0y - v0y * e0x), s1 = sg * (v1x * e1y - v1y * e1x), s2 = sg * (v2x * e2y - v2y * e2x);
            float d = fminf(d0, fminf(d1, d2)), side = fminf(s0, fminf(s1, s2));
            float dist = side > 0 ? -sqrtf(d) : sqrtf(d);      /* (inside: every edge has the point on its inner side) */
            blend(p, k, clampf(0.5f - dist, 0, 1));
        }
    }
}
void cv_line(Canvas *c, float x0, float y0, float x1, float y1, float width, Col k) {
    float h = width * 0.5f;
    Span s = span_of(c, fminf(x0, x1) - h - 1, fminf(y0, y1) - h - 1, fmaxf(x0, x1) + h + 1, fmaxf(y0, y1) + h + 1);
    float bx = x1 - x0, by = y1 - y0, bb = dot2(bx, by);
    for (int j = s.y0; j < s.y1; j++) {
        uint8_t *p = c->px + ((size_t)j * c->w + s.x0) * 4;
        for (int i = s.x0; i < s.x1; i++, p += 4) {
            float ax = i + 0.5f - x0, ay = j + 0.5f - y0;
            float t = bb > 0 ? clampf((ax * bx + ay * by) / bb, 0, 1) : 0;
            blend(p, k, clampf(0.5f - (sqrtf(dot2(ax - bx * t, ay - by * t)) - h), 0, 1));
        }
    }
}

/* ---- gradients, polygons, cut-outs, blur, turned layers ---- */
static Col grad_at(const Col *stops, const float *pos, int n, float t) {
    if (t <= pos[0]) return stops[0];
    for (int i = 1; i < n; i++)
        if (t <= pos[i]) return mix(stops[i - 1], stops[i], pos[i] > pos[i - 1] ? (t - pos[i - 1]) / (pos[i] - pos[i - 1]) : 1);
    return stops[n - 1];
}
void cv_grad_rrect(Canvas *c, Box b, float r, const Col *stops, const float *pos, int n, float angle_deg) {
    if (b.w <= 0 || b.h <= 0 || n <= 0) return;
    r = radius_of(b, r);
    float a = angle_deg * 3.14159265f / 180, dx = cosf(a), dy = sinf(a);
    if (fabsf(dx) < 1e-6f) dx = 0;
    if (fabsf(dy) < 1e-6f) dy = 0;
    /* (the box's extent along the direction: its corners' projections, from b's top-left) */
    float p0 = fminf(0, b.w * dx) + fminf(0, b.h * dy), p1 = fmaxf(0, b.w * dx) + fmaxf(0, b.h * dy), span = p1 - p0 > 1e-6f ? p1 - p0 : 1;
    Span s = span_of(c, b.x - 1, b.y - 1, b.x + b.w + 1, b.y + b.h + 1);
    for (int j = s.y0; j < s.y1; j++) {
        uint8_t *p = c->px + ((size_t)j * c->w + s.x0) * 4;
        for (int i = s.x0; i < s.x1; i++, p += 4) {
            float x = i + 0.5f, y = j + 0.5f, cov = clampf(0.5f - rrect_d(x, y, b, r), 0, 1);
            if (cov <= 0) continue;
            blend(p, grad_at(stops, pos, n, ((x - b.x) * dx + (y - b.y) * dy - p0) / span), cov);
        }
    }
}
/* The signed distance to a simple polygon (negative inside), after Inigo Quilez's sdPolygon. */
static float poly_d(const float *v, int n, float px, float py) {
    float d = dot2(px - v[0], py - v[1]), s = 1;
    for (int i = 0, j = n - 1; i < n; j = i, i++) {
        float ex = v[2 * j] - v[2 * i], ey = v[2 * j + 1] - v[2 * i + 1], wx = px - v[2 * i], wy = py - v[2 * i + 1];
        float t = clampf((wx * ex + wy * ey) / fmaxf(dot2(ex, ey), 1e-12f), 0, 1);
        d = fminf(d, dot2(wx - ex * t, wy - ey * t));
        int c1 = py >= v[2 * i + 1], c2 = py < v[2 * j + 1], c3 = ex * wy > ey * wx;
        if ((c1 && c2 && c3) || (!c1 && !c2 && !c3)) s = -s;
    }
    return s * sqrtf(d);
}
void cv_poly(Canvas *c, const float *xy, int n, Col k) {
    if (n < 3) return;
    float x0 = xy[0], x1 = xy[0], y0 = xy[1], y1 = xy[1];
    for (int i = 1; i < n; i++) { x0 = fminf(x0, xy[2 * i]); x1 = fmaxf(x1, xy[2 * i]); y0 = fminf(y0, xy[2 * i + 1]); y1 = fmaxf(y1, xy[2 * i + 1]); }
    Span s = span_of(c, x0 - 1, y0 - 1, x1 + 1, y1 + 1);
    for (int j = s.y0; j < s.y1; j++) {
        uint8_t *p = c->px + ((size_t)j * c->w + s.x0) * 4;
        for (int i = s.x0; i < s.x1; i++, p += 4) blend(p, k, clampf(0.5f - poly_d(xy, n, i + 0.5f, j + 0.5f), 0, 1));
    }
}
void cv_polyline(Canvas *c, const float *xy, int n, float width, int closed, Col k) {
    /* One pass over the union of the segments' capsules, so a see-through stroke isn't darker where they meet. */
    if (n < 2) return;
    float h = width * 0.5f, x0 = xy[0], x1 = xy[0], y0 = xy[1], y1 = xy[1];
    for (int i = 1; i < n; i++) { x0 = fminf(x0, xy[2 * i]); x1 = fmaxf(x1, xy[2 * i]); y0 = fminf(y0, xy[2 * i + 1]); y1 = fmaxf(y1, xy[2 * i + 1]); }
    Span s = span_of(c, x0 - h - 1, y0 - h - 1, x1 + h + 1, y1 + h + 1);
    int segs = closed ? n : n - 1;
    for (int j = s.y0; j < s.y1; j++) {
        uint8_t *p = c->px + ((size_t)j * c->w + s.x0) * 4;
        for (int i = s.x0; i < s.x1; i++, p += 4) {
            float px = i + 0.5f, py = j + 0.5f, d = 1e9f;
            for (int g = 0; g < segs; g++) {
                const float *a = xy + 2 * g, *b = xy + 2 * ((g + 1) % n);
                float bx = b[0] - a[0], by = b[1] - a[1], ax = px - a[0], ay = py - a[1], bb = dot2(bx, by);
                float t = bb > 0 ? clampf((ax * bx + ay * by) / bb, 0, 1) : 0;
                d = fminf(d, dot2(ax - bx * t, ay - by * t));
            }
            blend(p, k, clampf(0.5f - (sqrtf(d) - h), 0, 1));
        }
    }
}
void cv_erase_circle(Canvas *c, float cx, float cy, float rad) {
    Span s = span_of(c, cx - rad - 1, cy - rad - 1, cx + rad + 1, cy + rad + 1);
    for (int j = s.y0; j < s.y1; j++) {
        uint8_t *p = c->px + ((size_t)j * c->w + s.x0) * 4;
        for (int i = s.x0; i < s.x1; i++, p += 4) {
            float dx = i + 0.5f - cx, dy = j + 0.5f - cy, keep = 1 - clampf(0.5f - (sqrtf(dx * dx + dy * dy) - rad), 0, 1);
            if (keep < 1) for (int ch = 0; ch < 4; ch++) p[ch] = (uint8_t)(p[ch] * keep + 0.5f);
        }
    }
}
/* One box pass of radius r along rows (step 4, count w, lines h, line stride w*4) or columns. */
static void box_pass(uint8_t *px, int count, int lines, size_t step, size_t stride, int r, uint8_t *tmp) {
    int win = 2 * r + 1;
    for (int l = 0; l < lines; l++) {
        uint8_t *line = px + l * stride;
        for (int ch = 0; ch < 4; ch++) {
            int sum = 0;
            for (int i = -r; i <= r; i++) { int k = i < 0 ? 0 : i >= count ? count - 1 : i; sum += line[k * step + ch]; }
            for (int i = 0; i < count; i++) {
                tmp[i] = (uint8_t)((sum + win / 2) / win);
                int out = i - r, in = i + r + 1;
                out = out < 0 ? 0 : out; in = in >= count ? count - 1 : in;
                sum += line[in * step + ch] - line[out * step + ch];
            }
            for (int i = 0; i < count; i++) line[i * step + ch] = tmp[i];
        }
    }
}
void cv_blur(Canvas *c, float radius) {
    int r = (int)lrintf(radius);
    if (r < 1) return;
    uint8_t *tmp = malloc((size_t)(c->w > c->h ? c->w : c->h));
    if (!tmp) return;
    for (int pass = 0; pass < 3; pass++) {
        box_pass(c->px, c->w, c->h, 4, (size_t)c->w * 4, r, tmp);
        box_pass(c->px, c->h, c->w, (size_t)c->w * 4, 4, r, tmp);
    }
    free(tmp);
}
void cv_shadow_of(Canvas *dst, const Canvas *src, float x, float y, float dx, float dy, float blur, Col k) {
    float sigma = blur * 0.5f;
    int m = (int)ceilf(sigma * 3) + 1;
    Canvas *t = cv_new(src->w + 2 * m, src->h + 2 * m);
    if (!t) return;
    for (int j = 0; j < src->h; j++)
        for (int i = 0; i < src->w; i++) {
            float a = src->px[((size_t)j * src->w + i) * 4 + 3] / 255.f * k.a;
            uint8_t *p = t->px + ((size_t)(j + m) * t->w + i + m) * 4;
            p[0] = (uint8_t)(k.r * a * 255 + 0.5f); p[1] = (uint8_t)(k.g * a * 255 + 0.5f); p[2] = (uint8_t)(k.b * a * 255 + 0.5f); p[3] = (uint8_t)(a * 255 + 0.5f);
        }
    cv_blur(t, sigma);
    cv_over(dst, t, x + dx - m, y + dy - m, 1);
    cv_free(t);
}
void cv_over_turned(Canvas *dst, const Canvas *src, float ax, float ay, float x, float y, float angle, float alpha) {
    float ca = cosf(angle), sa = sinf(angle);
    /* the turned box: src's corners about (ax, ay), moved to (x, y) */
    float cx[4] = {0, (float)src->w, 0, (float)src->w}, cy[4] = {0, 0, (float)src->h, (float)src->h}, x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
    for (int i = 0; i < 4; i++) {
        float u = cx[i] - ax, v = cy[i] - ay, X = x + u * ca - v * sa, Y = y + u * sa + v * ca;
        x0 = fminf(x0, X); x1 = fmaxf(x1, X); y0 = fminf(y0, Y); y1 = fmaxf(y1, Y);
    }
    Span s = span_of(dst, x0 - 1, y0 - 1, x1 + 1, y1 + 1);
    for (int j = s.y0; j < s.y1; j++) {
        uint8_t *p = dst->px + ((size_t)j * dst->w + s.x0) * 4;
        for (int i = s.x0; i < s.x1; i++, p += 4) {
            float u = i + 0.5f - x, v = j + 0.5f - y;
            float sx = u * ca + v * sa + ax - 0.5f, sy = -u * sa + v * ca + ay - 0.5f;   /* (the inverse turn, to pixel centres) */
            int ix = (int)floorf(sx), iy = (int)floorf(sy);
            if (ix < -1 || iy < -1 || ix >= src->w || iy >= src->h) continue;
            float fx = sx - ix, fy = sy - iy, acc[4] = {0, 0, 0, 0};
            for (int q = 0; q < 4; q++) {
                int qx = ix + (q & 1), qy = iy + (q >> 1);
                if (qx < 0 || qy < 0 || qx >= src->w || qy >= src->h) continue;
                float wgt = ((q & 1) ? fx : 1 - fx) * ((q >> 1) ? fy : 1 - fy);
                const uint8_t *sp = src->px + ((size_t)qy * src->w + qx) * 4;
                for (int ch = 0; ch < 4; ch++) acc[ch] += sp[ch] * wgt;
            }
            float a = acc[3] / 255.f * alpha;
            if (a <= 0.0005f) continue;
            float keep = 1 - a;
            for (int ch = 0; ch < 3; ch++) { float val = acc[ch] * alpha + p[ch] * keep; p[ch] = (uint8_t)(val > 255 ? 255 : val + 0.5f); }
            float val = a * 255 + p[3] * keep; p[3] = (uint8_t)(val > 255 ? 255 : val + 0.5f);
        }
    }
}

/* ---- images ---- */
Image *img_from_rgba(const uint8_t *rgba, int w, int h) {
    Image *m = calloc(1, sizeof *m);
    if (!m) return NULL;
    m->w = w; m->h = h; m->px = malloc((size_t)w * h * 4);
    if (!m->px) { free(m); return NULL; }
    for (size_t i = 0; i < (size_t)w * h; i++) {
        unsigned a = rgba[i * 4 + 3];
        for (int ch = 0; ch < 3; ch++) m->px[i * 4 + ch] = (uint8_t)((rgba[i * 4 + ch] * a + 127) / 255);
        m->px[i * 4 + 3] = (uint8_t)a;
    }
    return m;
}
Image *img_load(const char *path) {
    int w, h, n;
    unsigned char *d = stbi_load(path, &w, &h, &n, 4);
    if (!d) return NULL;
    Image *m = img_from_rgba(d, w, h);
    stbi_image_free(d);
    return m;
}
void img_free(Image *m) { if (m) { free(m->px); free(m); } }
/* One axis of a resample: n_out samples of a line of n_in, from `from` to from+len in the input's units. Down: each
 * output averages the inputs it covers (fractions at its ends); up: linear between neighbours. */
static void resample_line(const float *in, int n_in, int stride_in, float from, float len, float *out, int n_out, int stride_out) {
    float step = len / n_out;
    for (int o = 0; o < n_out; o++) {
        float acc[4] = {0, 0, 0, 0};
        if (step > 1) {
            float a = from + o * step, b = a + step, wsum = 0;
            for (int i = (int)floorf(a); i < (int)ceilf(b); i++) {
                float wgt = fminf(b, i + 1.f) - fmaxf(a, (float)i);
                if (wgt <= 0) continue;
                int k = i < 0 ? 0 : i >= n_in ? n_in - 1 : i;
                for (int ch = 0; ch < 4; ch++) acc[ch] += in[(size_t)k * stride_in + ch] * wgt;
                wsum += wgt;
            }
            for (int ch = 0; ch < 4; ch++) acc[ch] = wsum > 0 ? acc[ch] / wsum : 0;
        } else {
            float x = from + (o + 0.5f) * step - 0.5f;
            int i = (int)floorf(x); float t = x - i;
            int a = i < 0 ? 0 : i >= n_in ? n_in - 1 : i, b = i + 1 < 0 ? 0 : i + 1 >= n_in ? n_in - 1 : i + 1;
            for (int ch = 0; ch < 4; ch++) acc[ch] = in[(size_t)a * stride_in + ch] * (1 - t) + in[(size_t)b * stride_in + ch] * t;
        }
        for (int ch = 0; ch < 4; ch++) out[(size_t)o * stride_out + ch] = acc[ch];
    }
}
Image *img_scaled(const Image *src, Box from, int w, int h) {
    if (!src || w <= 0 || h <= 0) return NULL;
    int x0 = (int)floorf(from.x), x1 = (int)ceilf(from.x + from.w), y0 = (int)floorf(from.y), y1 = (int)ceilf(from.y + from.h);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > src->w) x1 = src->w;
    if (y1 > src->h) y1 = src->h;
    int iw = x1 - x0, ih = y1 - y0;
    if (iw <= 0 || ih <= 0) return NULL;
    float *in = malloc((size_t)iw * ih * 4 * sizeof(float)), *mid = malloc((size_t)w * ih * 4 * sizeof(float)), *out = malloc((size_t)w * h * 4 * sizeof(float));
    Image *m = calloc(1, sizeof *m);
    if (!in || !mid || !out || !m) { free(in); free(mid); free(out); free(m); return NULL; }
    for (int j = 0; j < ih; j++)
        for (int i = 0; i < iw; i++)
            for (int ch = 0; ch < 4; ch++) in[((size_t)j * iw + i) * 4 + ch] = src->px[((size_t)(y0 + j) * src->w + x0 + i) * 4 + ch];
    for (int j = 0; j < ih; j++) resample_line(in + (size_t)j * iw * 4, iw, 4, from.x - x0, from.w, mid + (size_t)j * w * 4, w, 4);
    for (int i = 0; i < w; i++) resample_line(mid + (size_t)i * 4, ih, w * 4, from.y - y0, from.h, out + (size_t)i * 4, h, w * 4);
    m->w = w; m->h = h; m->px = malloc((size_t)w * h * 4);
    if (!m->px) { free(in); free(mid); free(out); free(m); return NULL; }
    for (size_t i = 0; i < (size_t)w * h * 4; i++) { float v = out[i]; m->px[i] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v + 0.5f); }
    free(in); free(mid); free(out);
    return m;
}
void cv_image(Canvas *c, const Image *m, float x, float y, float alpha) {
    if (!m) return;
    Canvas view = {m->w, m->h, m->px};
    cv_over(c, &view, x, y, alpha);
}

/* ---- type ---- */
struct Font { stbtt_fontinfo info; unsigned char *data; Font *fallback; };
Font *font_load(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    Font *font = calloc(1, sizeof *font);
    if (font) font->data = malloc(n > 0 ? (size_t)n : 1);
    if (!font || !font->data || n <= 0 || fread(font->data, 1, (size_t)n, f) != (size_t)n
        || !stbtt_InitFont(&font->info, font->data, stbtt_GetFontOffsetForIndex(font->data, 0))) {
        fclose(f);
        if (font) free(font->data);
        free(font);
        return NULL;
    }
    fclose(f);
    return font;
}
static float scale_of(const Font *f, float size) { return stbtt_ScaleForMappingEmToPixels(&f->info, size); }
void font_set_fallback(Font *f, Font *fallback) { if (f && fallback != f) f->fallback = fallback; }
float font_ascent(const Font *f, float size) { int a, d, g; stbtt_GetFontVMetrics(&f->info, &a, &d, &g); return a * scale_of(f, size); }
float font_descent(const Font *f, float size) { int a, d, g; stbtt_GetFontVMetrics(&f->info, &a, &d, &g); return -d * scale_of(f, size); }

/* The next code point of a UTF-8 string (advancing *s); bad bytes come out as U+FFFD. */
static int next_cp(const char **s) {
    const unsigned char *p = (const unsigned char *)*s;
    int c = *p, n = c < 0x80 ? 0 : c < 0xe0 ? 1 : c < 0xf0 ? 2 : 3, cp = n == 0 ? c : n == 1 ? c & 0x1f : n == 2 ? c & 0x0f : c & 0x07;
    p++;
    for (int i = 0; i < n; i++, p++) {
        if ((*p & 0xc0) != 0x80) { *s = (const char *)p; return 0xfffd; }
        cp = cp << 6 | (*p & 0x3f);
    }
    *s = (const char *)p;
    return cp;
}

/* Rendered glyphs, kept by font, size, glyph and the quarter pixel they start on. */
typedef struct { const Font *f; int size64, glyph, sub; int w, h, x0, y0; uint8_t *bm; } Glyph;
enum { GLYPHS = 4096 };
static Glyph glyphs[GLYPHS];
static Glyph *glyph_of(const Font *f, float size, int g, int sub) {
    int size64 = (int)lrintf(size * 64);
    unsigned hsh = ((unsigned)(uintptr_t)f * 2654435761u) ^ (unsigned)size64 * 40503u ^ (unsigned)g * 97u ^ (unsigned)sub;
    for (int probe = 0; probe < 16; probe++) {
        Glyph *e = &glyphs[(hsh + probe) % GLYPHS];
        if (e->f == f && e->size64 == size64 && e->glyph == g && e->sub == sub) return e;
        if (!e->f || probe == 15) {
            free(e->bm);
            float sc = scale_of(f, size), sx = sub * 0.25f;
            int x0, y0, x1, y1;
            stbtt_GetGlyphBitmapBoxSubpixel(&f->info, g, sc, sc, sx, 0, &x0, &y0, &x1, &y1);
            *e = (Glyph){f, size64, g, sub, x1 - x0, y1 - y0, x0, y0, NULL};
            if (e->w > 0 && e->h > 0 && (e->bm = malloc((size_t)e->w * e->h)))
                stbtt_MakeGlyphBitmapSubpixel(&f->info, e->bm, e->w, e->h, e->w, sc, sc, sx, 0, g);
            return e;
        }
    }
    return NULL;
}
/* Type a little heavier than the rasteriser's exact coverage, as macOS draws it. */
static float ink(uint8_t v) { static float lut[256]; static int made; if (!made) { for (int i = 0; i < 256; i++) lut[i] = powf(i / 255.f, 0.8f); made = 1; } return lut[v]; }

static float run_text(Canvas *c, Font *f0, float size, float x, float y, float track, Col k, const char *utf8) {
    float pen = x;
    int prev = 0; const Font *prev_f = NULL;
    for (const char *s = utf8; *s;) {
        int cp = next_cp(&s);
        Font *f = f0;
        int g = stbtt_FindGlyphIndex(&f->info, cp);
        if (!g && cp == 0xa0) g = stbtt_FindGlyphIndex(&f->info, ' ');         /* (a no-break space as a space) */
        if (!g && f->fallback && (g = stbtt_FindGlyphIndex(&f->fallback->info, cp))) f = f->fallback;   /* (arrows and the like) */
        float sc = scale_of(f, size);
        if (prev && prev_f == f) pen += stbtt_GetGlyphKernAdvance(&f->info, prev, g) * sc;
        if (c) {
            float fl = floorf(pen);
            int sub = (int)((pen - fl) * 4) & 3;
            Glyph *e = glyph_of(f, size, g, sub);
            if (e && e->bm) {
                int gx = (int)fl + e->x0, gy = (int)lrintf(y) + e->y0;
                Span sp = span_of(c, (float)gx, (float)gy, (float)(gx + e->w), (float)(gy + e->h));
                for (int j = sp.y0; j < sp.y1; j++) {
                    uint8_t *p = c->px + ((size_t)j * c->w + sp.x0) * 4;
                    const uint8_t *q = e->bm + (size_t)(j - gy) * e->w + (sp.x0 - gx);
                    for (int i = sp.x0; i < sp.x1; i++, p += 4, q++) if (*q) blend(p, k, ink(*q));
                }
            }
        }
        int adv, lsb;
        stbtt_GetGlyphHMetrics(&f->info, g, &adv, &lsb);
        pen += adv * sc + track * size;
        prev = g; prev_f = f;
    }
    return pen - x - (utf8 && *utf8 ? track * size : 0);
}
float text_width(Font *f, float size, float track, const char *utf8) { return f && utf8 ? run_text(NULL, f, size, 0, 0, track, (Col){0, 0, 0, 0}, utf8) : 0; }
float cv_text(Canvas *c, Font *f, float size, float x, float y, float track, Col k, const char *utf8) {
    return f && utf8 ? run_text(c, f, size, x, y, track, k, utf8) : 0;
}
void text_fit(Font *f, float size, float track, const char *utf8, float width, char *out, int n) {
    if (n <= 0) return;
    snprintf(out, (size_t)n, "%s", utf8 ? utf8 : "");
    if (!f || text_width(f, size, track, out) <= width) return;
    const char *ell = "\xe2\x80\xa6";
    int best = 0;
    for (const char *s = utf8; *s;) {
        next_cp(&s);
        int len = (int)(s - utf8);
        if (len + 4 > n) break;
        char tmp[1024];
        if (len + 4 > (int)sizeof tmp) break;
        memcpy(tmp, utf8, (size_t)len); strcpy(tmp + len, ell);
        if (text_width(f, size, track, tmp) > width) break;
        best = len;
    }
    while (best > 0 && utf8[best - 1] == ' ') best--;      /* no space before the ellipsis */
    memcpy(out, utf8, (size_t)best); strcpy(out + best, ell);
}

int cv_write_png(const Canvas *c, const char *path) {
    uint8_t *rgba = malloc((size_t)c->w * c->h * 4);
    if (!rgba) return -1;
    for (size_t i = 0; i < (size_t)c->w * c->h; i++) {
        unsigned a = c->px[i * 4 + 3];
        for (int ch = 0; ch < 3; ch++) rgba[i * 4 + ch] = a ? (uint8_t)(c->px[i * 4 + ch] * 255u / a > 255 ? 255 : c->px[i * 4 + ch] * 255u / a) : 0;
        rgba[i * 4 + 3] = (uint8_t)a;
    }
    int ok = stbi_write_png(path, c->w, c->h, 4, rgba, c->w * 4);
    free(rgba);
    return ok ? 0 : -1;
}
