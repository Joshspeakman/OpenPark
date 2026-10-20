/* Source-aware placement capture. The game's framebuffer is read-only here.
 * Predict its original sprite writes in private storage so their later pixel
 * differences do not overwrite the corresponding full-colour draw. Uncaptured
 * operations are reconciled before the next sprite, preserving their order. */
#ifndef TP_REMASTER_PREVIEW_H
#define TP_REMASTER_PREVIEW_H
#include <string.h>
#include "native_draw.h"
typedef struct {
    uint8_t expected[640*480];
    const uint8_t *ignore;
    TPRemasterPaint *paint;
    int pitch, height, world_height, active, sprites;
    size_t size, reconciled;
} TPRemasterPreview;
static int tp_remaster_preview_begin(TPRemasterPreview *p,const uint8_t *frame,int pitch,int height,
                                     int world_height,TPRemasterPaint *paint,const uint8_t *ignore) {
    p->active=0;p->sprites=0;p->reconciled=0;
    if(!frame || !paint || pitch<1 || pitch>640 || height<1 || height>480 ||
       world_height<1 || world_height>height)return 0;
    p->size=(size_t)pitch*height;p->pitch=pitch;p->height=height;p->world_height=world_height;
    p->paint=paint;p->ignore=ignore;memcpy(p->expected,frame,p->size);p->active=1;return 1;
}
static void tp_remaster_preview_sync(TPRemasterPreview *p,const uint8_t *frame) {
    if(!p->active)return;
    for(int y=0;y<p->world_height;y++)for(int x=0;x<p->pitch;x++){
        size_t i=(size_t)y*p->pitch+x;
        if(frame[i]!=p->expected[i] && (!p->ignore || !p->ignore[i])){
            tp_remaster_paint_index(p->paint,x,y,frame[i]);p->reconciled++;
        }
    }
    memcpy(p->expected,frame,p->size);
}
static int tp_remaster_preview_sprite(TPRemasterPreview *p,const uint8_t *frame,const NDTarget *geometry,
                                      const uint8_t *memory,uint32_t source,int w,int h,int x,int y,int flags,int colour) {
    if(!p->active)return 0;
    tp_remaster_preview_sync(p,frame);
    NDTarget t=*geometry;t.buf=p->expected;t.size=p->size;t.paint=p->paint;t.unsupported=0;
    if(t.pitch!=p->pitch){p->paint->failed=1;return 0;}
    if(!tp_native_paint_world_sprite(&t,memory,source,w,h,x,y,flags,colour))return 0;
    /* Resolve verified the source content and dimensions. Prediction uses the
     * original blitter, never the replacement's bookkeeping preview. */
    t.paint=NULL;tp_native_draw_sprite(&t,memory+source,w,h,x,y,flags,colour);
    if(t.unsupported)p->paint->failed=1;
    p->sprites++;return 1;
}
static void tp_remaster_preview_end(TPRemasterPreview *p,const uint8_t *frame) {
    tp_remaster_preview_sync(p,frame);p->active=0;
}
#endif
