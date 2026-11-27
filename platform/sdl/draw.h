/* A small software 2D canvas for the launch window (platform/sdl/launcher.c), the counterpart of what Core Graphics and
 * Core Animation draw for platform/macos/launcher.m: premultiplied RGBA; anti-aliased rounded rectangles, gradients,
 * soft shadows and glows, stripes, circles, triangles and lines (each from its signed distance, so every edge is smooth);
 * images (stb_image, scaled by area or bilinear); type (stb_truetype, kerned, tracked). Coordinates are floats in the
 * canvas's pixels; the launcher multiplies its points by the display's scale. Plain C, no SDL. */
#ifndef TP_DRAW_H
#define TP_DRAW_H
#include <stdint.h>

typedef struct { int w, h; uint8_t *px; } Canvas;              /* RGBA bytes, premultiplied, rows of w*4 */
typedef struct { float r, g, b, a; } Col;                       /* straight colour, 0..1 */
typedef struct { float x, y, w, h; } Box;

static inline Col col(int r, int g, int b) { Col c = {r / 255.f, g / 255.f, b / 255.f, 1}; return c; }
static inline Col cola(int r, int g, int b, float a) { Col c = {r / 255.f, g / 255.f, b / 255.f, a}; return c; }
static inline Col with_alpha(Col c, float a) { c.a *= a; return c; }
static inline Box box(float x, float y, float w, float h) { Box b = {x, y, w, h}; return b; }
static inline Box inset(Box b, float d) { Box r = {b.x + d, b.y + d, b.w - 2 * d, b.h - 2 * d}; return r; }
Col mix(Col a, Col b, float t);

Canvas *cv_new(int w, int h);
void cv_free(Canvas *c);
void cv_fill(Canvas *c, Col k);                                 /* the whole canvas, replaced */
void cv_clip(Canvas *c, Box b);                                 /* draws stay inside b (and any clip already set) until cv_unclip */
void cv_unclip(Canvas *c);                                      /* the latest clip taken away */
void cv_over(Canvas *dst, const Canvas *src, float x, float y, float alpha);   /* src over dst at whole pixels */

/* Shapes. A rounded rectangle's radius is clamped to half its shorter side. */
void cv_rrect(Canvas *c, Box b, float r, Col k);
void cv_rrect_v(Canvas *c, Box b, float r, Col top, Col bottom);               /* top-to-bottom gradient */
void cv_rrect_line(Canvas *c, Box b, float r, float width, Col k);              /* an edge inside b */
void cv_shadow(Canvas *c, Box b, float r, float blur, Col k);                   /* b blurred by about `blur` */
void cv_stripes(Canvas *c, Box b, float r, float period, float width, float angle_deg, Col k);   /* bands inside b */
void cv_circle(Canvas *c, float cx, float cy, float rad, Col k);
void cv_ring(Canvas *c, float cx, float cy, float rad, float width, Col k);
void cv_glow(Canvas *c, float cx, float cy, float rad, Col k);                  /* a soft light fading to nothing at rad */
void cv_ellipse_glow(Canvas *c, float cx, float cy, float rx, float ry, Col k);
void cv_tri(Canvas *c, float x0, float y0, float x1, float y1, float x2, float y2, Col k);
void cv_line(Canvas *c, float x0, float y0, float x1, float y1, float width, Col k);   /* round ends */
void cv_vgrad(Canvas *c, Box b, Col top, Col bottom);                            /* a plain rectangle, top to bottom */

/* A gradient of n stops (colour, position 0..1) across b, as AppKit's NSGradient drawInRect:angle: in a flipped
 * (y down) view: the direction is (cos a, sin a), the first stop where the box begins along it (so 90 runs top to bottom
 * and -90 bottom to top, as the Mac launcher's sign, tickets and Play are drawn). */
void cv_grad_rrect(Canvas *c, Box b, float r, const Col *stops, const float *pos, int n, float angle_deg);
/* Any simple polygon (n points, xy pairs), filled; and a polyline stroked with round ends and joins. */
void cv_poly(Canvas *c, const float *xy, int n, Col k);
void cv_polyline(Canvas *c, const float *xy, int n, float width, int closed, Col k);
/* Pieces cut out of what is drawn (each pixel kept by 1 - coverage): the ticket's notches. */
void cv_erase_circle(Canvas *c, float cx, float cy, float rad);
/* Everything in c blurred (three box passes, close to a Gaussian of sigma = radius). */
void cv_blur(Canvas *c, float radius);
/* src's alpha as a shadow of colour k, blurred by `blur` (AppKit's radius: sigma = blur / 2) and moved by (dx, dy), over
 * dst with src's top-left at (x, y). The shape itself is not drawn. */
void cv_shadow_of(Canvas *dst, const Canvas *src, float x, float y, float dx, float dy, float blur, Col k);
/* src over dst turned by `angle` (radians, clockwise on screen) about its point (ax, ay), which lands on (x, y);
 * bilinear. */
void cv_over_turned(Canvas *dst, const Canvas *src, float ax, float ay, float x, float y, float angle, float alpha);

/* Images: premultiplied RGBA like the canvas. */
typedef struct { int w, h; uint8_t *px; } Image;
Image *img_load(const char *path);                              /* PNG, JPEG, BMP (stb_image); NULL if unreadable */
Image *img_from_rgba(const uint8_t *rgba, int w, int h);        /* straight RGBA in, premultiplied copy out */
Image *img_scaled(const Image *src, Box from, int w, int h);    /* the part `from` of src at w x h: area average down, bilinear up */
void img_free(Image *m);
void cv_image(Canvas *c, const Image *m, float x, float y, float alpha);         /* at whole pixels, 1:1 */

/* Type. size: the em in pixels (as a Mac font's point size, times the scale); track: extra advance per character, in
 * ems (Cocoa's kern / size). Positions are the baseline's left end. */
typedef struct Font Font;
Font *font_load(const char *path);
void font_set_fallback(Font *f, Font *fallback);                /* glyphs f lacks (arrows) come from fallback */
float font_ascent(const Font *f, float size);
float font_descent(const Font *f, float size);                  /* below the baseline, positive */
float text_width(Font *f, float size, float track, const char *utf8);
float cv_text(Canvas *c, Font *f, float size, float x, float y, float track, Col k, const char *utf8);   /* its width */
/* The longest start of `utf8` that fits in `width` with an ellipsis ("…") if cut; out gets it. */
void text_fit(Font *f, float size, float track, const char *utf8, float width, char *out, int n);

int cv_write_png(const Canvas *c, const char *path);            /* (test renders) 0 on success */
#endif
