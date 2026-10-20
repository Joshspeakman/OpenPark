/* World-only baked artwork. UI blits never call this entry point. */
#ifndef TP_REMASTER_ART_H
#define TP_REMASTER_ART_H
#include <stdint.h>
#include <stddef.h>
typedef struct {
    const uint8_t *rle;
    const uint16_t *pixels;
    int width, height, dx, dy, source_width, source_height;
    const uint32_t *rgba;                       /* ABGR words; cutout alpha */
    int texture_width, texture_height;
} TPRemasterImage;
/* Coordinates/dimensions are in decoded source pixels. The original source
 * rectangle remains the mirror/flip pivot even when replacement bounds grow. */
int tp_remaster_resolve(const uint8_t *memory, uint32_t source,
                       int width, int height, int lowres, TPRemasterImage *out);
/* Legacy RLE-only access; expanded images require tp_remaster_resolve. */
const uint8_t *tp_remaster_source(const uint8_t *memory, uint32_t source,
                                  int width, int height, int lowres);
typedef struct {
    uint32_t *pixels;
    size_t stride;                              /* words, including row padding */
    int width, height;
    int x0, y0, x1, y1;                         /* exclusive output clip rectangle */
} TPRemasterTarget;
/* Ordered full-colour world drawing. x/y are the output position of the
 * unexpanded source origin; scales are output pixels per decoded source pixel.
 * Returns zero for an unsupported image/target/mode without writing anything.
 * Ghost previews blend RGB evenly; UI palettes and simulation memory are never
 * modified. The caller retains responsibility for world/UI ordering. */
int tp_remaster_blit_rgba(const TPRemasterImage *image, TPRemasterTarget *target,
                         double x, double y, double scale_x, double scale_y,
                         int flags, uint32_t solid_rgba);
/* An optional ordered companion to native indexed drawing. Integer density
 * and origin make each legacy pixel an exact block; replacement texels can
 * occupy individual pixels within that block. Owned by the game thread. */
typedef struct {
    TPRemasterTarget target;
    const uint32_t *palette;
    int origin_x, origin_y, density, failed;
} TPRemasterPaint;
void tp_remaster_paint_index(TPRemasterPaint *paint, int x, int y, unsigned pixel);
int tp_remaster_paint_image(TPRemasterPaint *paint, const TPRemasterImage *image,
                            int x, int y, int flags, unsigned colour);
int tp_remaster_has_rgba(void);                  /* game thread only */
#endif
