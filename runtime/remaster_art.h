/* World-only baked artwork. UI blits never call this entry point. */
#ifndef TP_REMASTER_ART_H
#define TP_REMASTER_ART_H
#include <stdint.h>
typedef struct {
    const uint8_t *rle;
    const uint16_t *pixels;
    int width, height, dx, dy, source_width, source_height;
} TPRemasterImage;
/* Coordinates/dimensions are in decoded source pixels. The original source
 * rectangle remains the mirror/flip pivot even when replacement bounds grow. */
int tp_remaster_resolve(const uint8_t *memory, uint32_t source,
                       int width, int height, int lowres, TPRemasterImage *out);
/* Legacy RLE-only access; expanded images require tp_remaster_resolve. */
const uint8_t *tp_remaster_source(const uint8_t *memory, uint32_t source,
                                  int width, int height, int lowres);
#endif
