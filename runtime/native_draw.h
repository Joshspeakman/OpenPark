#ifndef TP_NATIVE_DRAW_H
#define TP_NATIVE_DRAW_H
#include "remaster_art.h"
typedef struct {
    uint8_t *buf; size_t size;
    long base, pitch;
    int clip_w, clip_h, clip_r, lowres;
    const uint8_t *ghost;
    long unsupported;
    TPRemasterPaint *paint;                     /* NULL for UI/Visitor snapshots */
} NDTarget;
void tp_native_draw_sprite(NDTarget *, const uint8_t *, int, int, int, int, int, int);
void tp_native_draw_world_sprite(NDTarget *, const uint8_t *, uint32_t, int, int, int, int, int, int, int[4]);
/* Paint only the source-bound full-colour companion. The game can continue
 * drawing its original pixels independently (construction-preview capture). */
int tp_native_paint_world_sprite(NDTarget *, const uint8_t *, uint32_t, int, int, int, int, int, int);
void tp_native_draw_small_text(NDTarget *, const uint8_t *, int, int, const uint8_t *, int);
#endif
