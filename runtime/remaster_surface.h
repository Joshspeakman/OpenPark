/* Bounded visible-region storage. Game and presentation threads each own a
 * surface; publication swaps them under the view lock. No whole-park HD cache. */
#ifndef TP_REMASTER_SURFACE_H
#define TP_REMASTER_SURFACE_H
#include <math.h>
#include <stdlib.h>
#include "remaster_art.h"
typedef struct {
    uint32_t *pixels;
    size_t capacity;
    int x, y, w, h, density, valid;
} TPRemasterSurface;
#define TP_REMASTER_SURFACE_PIXELS (16u*1024u*1024u)
static int tp_remaster_surface_prepare(TPRemasterSurface *s,double x,double y,double w,double h,int cw,int ch) {
    s->valid=0;
    if(!isfinite(x)||!isfinite(y)||!isfinite(w)||!isfinite(h)||w<=0||h<=0||cw<1||ch<1||cw>16384||ch>16384)return 0;
    double left=fmax(0,floor(x)-8),top=fmax(0,floor(y)-8);
    double right=fmin(cw,ceil(x+w)+8),bottom=fmin(ch,ceil(y+h)+8);
    if(left>=right||top>=bottom)return 0;
    int iw=(int)(right-left),ih=(int)(bottom-top),d=4;
    while(d>1 && ((size_t)iw*ih*d*d>TP_REMASTER_SURFACE_PIXELS || iw*d>16384 || ih*d>16384))d--;
    size_t n=(size_t)iw*ih*d*d;
    if(n>TP_REMASTER_SURFACE_PIXELS)return 0;
    if(n>s->capacity){
        uint32_t *p=realloc(s->pixels,n*sizeof *p);
        if(!p)return 0;
        s->pixels=p;s->capacity=n;
    }
    s->x=(int)left;s->y=(int)top;s->w=iw;s->h=ih;s->density=d;s->valid=1;
    return 1;
}
static TPRemasterPaint tp_remaster_surface_paint(TPRemasterSurface *s,const uint32_t *pal,int x,int y) {
    int w=s->w*s->density,h=s->h*s->density;
    return (TPRemasterPaint){.target={s->pixels,(size_t)w,w,h,0,0,w,h},.palette=pal,
        .origin_x=(x-s->x)*s->density,.origin_y=(y-s->y)*s->density,.density=s->density};
}
static int tp_remaster_surface_sample(const TPRemasterSurface *s,double x,double y,uint32_t *out) {
    if(!s->valid || !isfinite(x) || !isfinite(y) || x<s->x || y<s->y || x>=s->x+s->w || y>=s->y+s->h)return 0;
    int ix=(int)((x-s->x)*s->density),iy=(int)((y-s->y)*s->density);
    *out=s->pixels[(size_t)iy*s->w*s->density+ix];return 1;
}
#endif
