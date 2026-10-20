/* Read-only native draw streams. Coordinates are the game's 640-space units;
 * consumers apply the screen mode's ordinary low-resolution shift. */
#ifndef TP_NATIVE_SCENE_H
#define TP_NATIVE_SCENE_H
#include <stdint.h>
typedef void (*TPNativeDrawLog)(int kind,int a,int b,int c,uint32_t d,int e,void *ctx);

/* Existing drawing events (1..5) retain their exact order and arguments.
 * Metadata events never replace a drawing event:
 *   6: begin thing (a=index, b=class, c=cell screen x, d=cell screen y)
 *   7: passenger (a=attachment, b=variant, c=pose, d=source visibility,
 *                 e=source flags); a=-1 ends a passenger's source parts
 *   8: native-selected frame (a=frame, b=screen x, c=screen y)
 *   9: track component (a=frame, b=ground x, c=ground y, d=height, e=family)
 *  10: car origin, same fields as 9
 *  11: car track (a=thing index, b=native bounce)
 *  12: end thing (a=index, b=class).
 * Entrance redraws can nest things: begin/end delimit a stack, not a flat
 * sequence. Static scenery has a negative index and class -1. Text pointers
 * referenced by event 4 are valid only during that callback; copy if deferred.
 * Neither stream is reentrant; call from the native game thread only. */
void tp_native_things(const uint8_t *,int cx,int cy,int rows,TPNativeDrawLog,void *);
void tp_native_things_metadata(const uint8_t *,int cx,int cy,int rows,TPNativeDrawLog,void *);

/* Visitor-only cell capture suppresses entrance neighbor redraws and keeps
 * sign text beyond normal pass bounds. It emits metadata 6..11 with the
 * historic zero cell origin in event 6 and no end event. */
int tp_native_cell_things(const uint8_t *,int cx,int cy,int gx,int gy,TPNativeDrawLog,void *);
#endif
