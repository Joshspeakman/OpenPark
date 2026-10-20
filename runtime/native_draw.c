/* Native drawing of the things layer (engine rebuild stage 3b, docs/PLAN.md).
 *
 * A port of the game's sprite blitter core 0x15ed21, which the things renderer
 * reaches through 0x15ec9c (320x200), 0x155bd4 (640x480) and the one-colour
 * glyph blit 0x111bd5. Sprites are Bullfrog RLE rows (n > 0: n pixels follow;
 * n < 0: skip -n, and a run always follows; 0: end of row). The blitter flags
 * (word [0x1becbc]) select a variant: bit 0 mirrors (drawing leftwards from
 * x + w), bit 1 flips (rows upwards from y + h), bit 2 blends through the
 * ghost table [0x1ba2f8] (dst = table[dst * 256 + src]), bits 3 and 5 scale
 * (by stack arguments, not used by things: left out), bit 6 paints one colour.
 *
 * The game's clipping has quirks this keeps, so the result is the same pixels:
 * - a sprite partly off the left or right edge only honours bits 0 and 1; with
 *   any other flag it is not drawn at all (and bit 6 is not applied when the
 *   unclipped path would have taken it — the clipped paths have no such case);
 * - the column counters of the clipped paths are bytes (they wrap after 128
 *   pixels, which only 640x480 sprites are wide enough to reach);
 * - a sprite off the top is not also clipped at the bottom;
 * - flipped sprites cover rows y + 1 .. y + h.
 * Writes land where the game's would (base + y * pitch + x, even outside the
 * clip window); only writes outside the whole buffer are dropped. */
#include <stdint.h>
#include <stddef.h>
#include <math.h>
#include "remaster_art_impl.h"

#include "native_draw.h"

static inline void put(NDTarget *t, long off, uint8_t v) {
    if (off >= 0 && (size_t)off < t->size) {
        t->buf[off] = v;
        if(t->paint && t->pitch>0)
            tp_remaster_paint_index(t->paint,(int)(off%t->pitch),(int)(off/t->pitch),v);
    }
}
static inline uint8_t get(NDTarget *t, long off) { return off >= 0 && (size_t)off < t->size ? t->buf[off] : 0; }

/* Skip n rows of RLE data. */
static const uint8_t *skip_rows(const uint8_t *s, int n) {
    while (n-- > 0) for (;;) { int8_t c = (int8_t)*s++; if (!c) break; if (c > 0) s += c; }
    return s;
}

void tp_native_draw_sprite(NDTarget *t, const uint8_t *s, int w, int h, int x, int y, int flags, int colour) {
    if (!(h & 0xff)) return;                                  /* or dh, dh */
    w &= 0xff; h &= 0xff;
    if (t->lowres) { w >>= 1; h >>= 1; x >>= 1; y >>= 1; }
    if (!h) { t->unsupported++; return; }                     /* (the game would draw 256 rows of whatever follows) */
    long P = t->pitch;
    /* vertical clipping (0x15ed5c, 0x15f4aa, 0x15f4fe) */
    if (y < 0) {
        if (flags & 2) { int n = h + y + 1; if (n <= 0) return; y = -1; h = n & 0xff; }
        else { int n = h + y; if (n <= 0) return; s = skip_rows(s, -y); h = n & 0xff; y = 0; }
    } else if (y + h >= t->clip_h) {
        if (flags & 2) { int e = y + 1 - t->clip_h; if (e >= 0) return; e += h; h = (h - e) & 0xff; s = skip_rows(s, e); }
        else { int n = t->clip_h - y; if (n <= 0) return; h = n & 0xff; }
    }
    if (!h) return;
    int mirror = flags & 1, flip = flags & 2;
    if (x < 0) {                                              /* 0x15f2ea: left edge */
        if (flags == 0 || (!mirror && flip)) {                /* plain / flip: skip the first -x columns */
            if (w <= -x) return;
            int8_t skip0 = (int8_t)(-x);
            long row = t->base + (long)(flip ? y + h : y) * P;
            for (int r = 0; r < h; r++, row += flip ? -P : P) {
                long d = row; int8_t bl = skip0;
                for (;;) {
                    int8_t c = (int8_t)*s++;
                    if (!c) break;
                    if (c > 0) { for (int k = 0; k < c; k++) { uint8_t v = *s++; bl--; if (bl < 0) put(t, d++, v); } continue; }
                    int8_t a = c;
                    if (bl > 0) { int8_t nb = (int8_t)(bl + c); bl = nb; if (nb >= 0) continue; a = nb; }
                    d -= a;
                    int n = *s++;
                    for (int k = 0; k < n; k++) { uint8_t v = *s++; bl--; if (bl < 0) put(t, d++, v); }
                }
            }
            return;
        }
        if (mirror) {                                         /* 0x15f3d8 / 0x15f375: from x + w leftwards to column 0 */
            int lim = x + w + 1; if (lim < 0) return;
            int8_t dl0 = (int8_t)lim;
            long row = t->base + x + w + (long)(flip ? y + h : y) * P;
            for (int r = 0; r < h; r++, row += flip ? -P : P) {
                long d = row; int8_t dl = dl0;
                for (;;) {
                    int8_t c = (int8_t)*s++;
                    if (!c) break;
                    if (c > 0) { for (int k = 0; k < c; k++) { uint8_t v = *s++; dl--; if (dl >= 0) put(t, d, v); d--; } continue; }
                    d += c; dl = (int8_t)(dl + c);
                }
            }
            return;
        }
        return;                                               /* other flags: not drawn */
    }
    if (x + w >= t->clip_w) {                                 /* 0x15f106: right edge ([0x1de5ec]) */
        if (flags == 0 || (!mirror && flip)) {
            long rem = (long)t->clip_r - x; if (rem < 0) return;
            int8_t bl0 = (int8_t)rem;
            long row = t->base + x + (long)(flip ? y + h : y) * P;
            for (int r = 0; r < h; r++, row += flip ? -P : P) {
                long d = row; int8_t bl = bl0;
                for (;;) {
                    int8_t c = (int8_t)*s++;
                    if (!c) break;
                    if (c > 0) { for (int k = 0; k < c; k++) { uint8_t v = *s++; bl--; if (bl >= 0) put(t, d++, v); } continue; }
                    if (flags == 0) {                         /* plain: a skip past the edge ends the row's drawing */
                        int sum = bl + c; int8_t nb = (int8_t)sum; bl = nb;
                        if (sum <= 0) continue;               /* (jle on the 8-bit add: the true sum) */
                        d -= c;
                        int n = *s++;
                        for (int k = 0; k < n; k++) { uint8_t v = *s++; bl--; if (bl >= 0) put(t, d++, v); }
                    } else { d -= c; bl = (int8_t)(bl + c); } /* flip: just move */
                }
            }
            return;
        }
        if (mirror) {                                         /* 0x15f20a / 0x15f191 */
            if ((long)t->clip_r - x < 0) return;
            int8_t dl0 = (int8_t)((long)t->clip_r - (x + w) - 2);
            long row = t->base + x + w + (long)(flip ? y + h : y) * P;
            for (int r = 0; r < h; r++, row += flip ? -P : P) {
                long d = row; int8_t dl = dl0;
                for (;;) {
                    int8_t c = (int8_t)*s++;
                    if (!c) break;
                    if (c > 0) { for (int k = 0; k < c; k++) { uint8_t v = *s++; dl++; if (dl >= 0) put(t, d, v); d--; } continue; }
                    d += c; dl = (int8_t)(dl - c);
                }
            }
            return;
        }
        return;
    }
    /* unclipped */
    if (mirror) {                                             /* 0x15ee07 / 0x15edaa */
        long row = t->base + x + w + (long)(flip ? y + h : y) * P;
        for (int r = 0; r < h; r++, row += flip ? -P : P) {
            long d = row;
            for (;;) {
                int8_t c = (int8_t)*s++;
                if (!c) break;
                if (c < 0) { d += c; c = (int8_t)*s++; }
                for (int k = 0; k < c; k++) put(t, d--, *s++);
            }
        }
        return;
    }
    int ghost = 0, solid = 0;
    if (flags && !flip) {
        if (flags & 4) ghost = 1;
        else if (flags & 0x28) { t->unsupported++; return; }  /* scaled: not used by things */
        else if (flags & 0x40) solid = 1;
    }
    if (ghost && !t->ghost) { t->unsupported++; return; }
    long row = t->base + x + (long)(flip ? y + h : y) * P;
    for (int r = 0; r < h; r++, row += flip ? -P : P) {
        long d = row;
        for (;;) {
            int8_t c = (int8_t)*s++;
            if (!c) break;
            if (c < 0) {
                d -= c;
                if (ghost) continue;                          /* (the ghost path re-reads a control byte) */
                c = (int8_t)*s++;
            }
            for (int k = 0; k < c; k++, s++, d++)
                put(t, d, solid ? (uint8_t)colour : ghost ? t->ghost[get(t, d) << 8 | *s] : *s);
        }
    }
}

/* World-only replacement path. UI and Original mode retain the exact legacy
 * blitter above. Expanded artwork uses the original rectangle as its pivot,
 * so adding transparent margins cannot move a mirrored actor. */
static void draw_world_indexed(NDTarget *t,const uint8_t *memory,uint32_t source,
                                int w,int h,int x,int y,int flags,int colour,int bounds[4],const TPRemasterImage *replacement) {
    TPRemasterImage image=replacement?*replacement:(TPRemasterImage){0};
    int replaced=replacement!=NULL;
    int sx=x>>t->lowres,sy=y>>t->lowres;
    int sw=(w&255)>>t->lowres,sh=(h&255)>>t->lowres;
    if(!replaced || !image.pixels) {
        if(bounds){bounds[0]=sx;bounds[1]=sy;bounds[2]=sx+sw+1;bounds[3]=sy+sh+1;}
        tp_native_draw_sprite(t,replaced?image.rle:memory+source,w,h,x,y,flags,colour);
        return;
    }
    int mirror=flags&1,flip=flags&2;
    int left=sx+(mirror?sw-image.dx-image.width+1:image.dx);
    int top=sy+(flip?sh-image.dy-image.height+1:image.dy);
    if(bounds){bounds[0]=left;bounds[1]=top;bounds[2]=left+image.width;bounds[3]=top+image.height;}
    int ghost=0,solid=0;
    if(!mirror && !flip){
        if(flags&4)ghost=1;
        else if(flags&0x28){t->unsupported++;return;}
        else if(flags&0x40)solid=1;
    }
    if(ghost && !t->ghost){t->unsupported++;return;}
    int x0=left<0?0:left,y0=top<0?0:top;
    int x1=left+image.width,y1=top+image.height;
    if(x1>t->clip_w)x1=t->clip_w;
    if(x1>t->clip_r+1)x1=t->clip_r+1;
    if(y1>t->clip_h)y1=t->clip_h;
    for(int yy=y0;yy<y1;yy++)for(int xx=x0;xx<x1;xx++){
        int u=xx-left,v=yy-top;
        if(mirror)u=image.width-1-u;if(flip)v=image.height-1-v;
        unsigned pixel=image.pixels[(size_t)v*image.width+u];if(pixel==256)continue;
        long at=t->base+(long)yy*t->pitch+xx;
        put(t,at,solid?(uint8_t)colour:ghost?t->ghost[get(t,at)*256+pixel]:(uint8_t)pixel);
    }
}

/* Draw the full-colour part at the same point in the native command stream.
 * The indexed preview still drives hit tests and interface extraction. */
void tp_native_draw_world_sprite(NDTarget *t,const uint8_t *memory,uint32_t source,
                                int w,int h,int x,int y,int flags,int colour,int bounds[4]) {
    TPRemasterPaint *paint=t->paint;
    TPRemasterImage im;
    int replaced=tp_remaster_resolve(memory,source,w,h,t->lowres,&im);
    if(paint && replaced && im.rgba) {
        if(t->pitch<=0 || t->base<0)paint->failed=1;
        else {
            TPRemasterPaint clipped=*paint;
            int bx=(int)(t->base%t->pitch),by=(int)(t->base/t->pitch);
            int left=paint->origin_x+bx*paint->density,top=paint->origin_y+by*paint->density;
            int right=left+(t->clip_w<t->clip_r+1?t->clip_w:t->clip_r+1)*paint->density;
            int bottom=top+t->clip_h*paint->density;
            if(clipped.target.x0<left)clipped.target.x0=left;
            if(clipped.target.y0<top)clipped.target.y0=top;
            if(clipped.target.x1>right)clipped.target.x1=right;
            if(clipped.target.y1>bottom)clipped.target.y1=bottom;
            tp_remaster_paint_image(&clipped,&im,bx+(x>>t->lowres),by+(y>>t->lowres),flags,(uint8_t)colour);
            paint->failed|=clipped.failed;
        }
        t->paint=NULL;                         /* preview must not cover fine texels */
    }
    draw_world_indexed(t,memory,source,w,h,x,y,flags,colour,bounds,replaced?&im:NULL);
    t->paint=paint;
}

/* This operates in output pixels, independently of the indexed bookkeeping
 * image. The native source rectangle remains the flip pivot at every scale. */
int tp_remaster_blit_rgba(const TPRemasterImage *im, TPRemasterTarget *t,
                         double x,double y,double sx,double sy,int flags,uint32_t colour) {
    if(!im || !t || !im->rgba || !t->pixels || im->width<1 || im->height<1 ||
       im->width>2048 || im->height>2048 || im->source_width<1 || im->source_height<1 ||
       im->source_width>255 || im->source_height>255 ||
       im->dx < -2048 || im->dx > 2048 || im->dy < -2048 || im->dy > 2048 ||
       im->texture_width<1 || im->texture_height<1 || im->texture_width>2048 || im->texture_height>2048 ||
       t->width<1 || t->height<1 || t->width>16384 || t->height>16384 ||
       t->stride<(size_t)t->width || t->stride>SIZE_MAX/(sizeof(uint32_t)*(size_t)t->height) ||
       !isfinite(x) || !isfinite(y) || !isfinite(sx) || !isfinite(sy) || sx<=0 || sy<=0 || sx>256 || sy>256)return 0;
    int mirror=flags&1,flip=flags&2,ghost=0,solid=0;
    if(!mirror && !flip){
        if(flags&4)ghost=1;
        else if(flags&0x28)return 0;
        else if(flags&0x40)solid=1;
    }
    double left=x+(mirror?im->source_width-im->dx-im->width+1:im->dx)*sx;
    double top=y+(flip?im->source_height-im->dy-im->height+1:im->dy)*sy;
    double right=left+im->width*sx,bottom=top+im->height*sy;
    if(!isfinite(left)||!isfinite(top)||!isfinite(right)||!isfinite(bottom))return 0;
    int cx0=t->x0>0?t->x0:0,cy0=t->y0>0?t->y0:0;
    int cx1=t->x1<t->width?t->x1:t->width,cy1=t->y1<t->height?t->y1:t->height;
    if(cx0>=cx1 || cy0>=cy1 || right<=cx0+.5 || bottom<=cy0+.5 || left>cx1-.5 || top>cy1-.5)return 1;
    /* Clamp floating bounds before conversion, including far-offscreen draws. */
    int x0=(int)ceil(fmax(cx0,left-.5)),x1=(int)ceil(fmin(cx1,right-.5));
    int y0=(int)ceil(fmax(cy0,top-.5)),y1=(int)ceil(fmin(cy1,bottom-.5));
    for(int yy=y0;yy<y1;yy++){
        int v=(int)floor((yy+.5-top)/(bottom-top)*im->texture_height);
        if(v<0 || v>=im->texture_height)continue;
        if(flip)v=im->texture_height-1-v;
        for(int xx=x0;xx<x1;xx++){
        int u=(int)floor((xx+.5-left)/(right-left)*im->texture_width);
        if(u<0 || u>=im->texture_width)continue;
        if(mirror)u=im->texture_width-1-u;
        uint32_t pixel=im->rgba[(size_t)v*im->texture_width+u];if((pixel>>24)<128)continue;
        size_t at=(size_t)yy*t->stride+xx;
        if(solid)pixel=colour;
        else if(ghost){
            uint32_t old=t->pixels[at];
            unsigned r=((pixel&255)+(old&255))/2;
            unsigned g=(((pixel>>8)&255)+((old>>8)&255))/2;
            unsigned b=(((pixel>>16)&255)+((old>>16)&255))/2;
            pixel=r|(g<<8)|(b<<16);
        }
        t->pixels[at]=pixel|0xff000000u;
        }
    }
    return 1;
}

/* The small font (0x180e88 at 320x200, 0x180f34 at 640x480): glyphs of 6 rows of 3 pixels
 * from 6 bytes each at 0x1ba2fc + 6 * (c - 0x20), each row byte one of 8 patterns, written
 * in one colour straight into the screen (the screen's own origin, not the clip window's;
 * no clipping); 4 pixels further for each character. 320x200 halves the position. */
void tp_native_draw_small_text(NDTarget *t, const uint8_t *mem, int x, int y, const uint8_t *str, int colour) {
    static const uint8_t pat[8] = {4, 2, 6, 1, 5, 3, 7, 0};   /* pixel bits (1 = left, 2 = middle, 4 = right) */
    long d = t->lowres ? (long)((unsigned)(int16_t)y >> 1) * 0x140 + (long)((unsigned)(int16_t)x >> 1) : (long)(int16_t)y * 0x280 + (int16_t)x;
    for (; *str; str++) {
        const uint8_t *g = mem + 0x1ba2fc + 6 * (int)(*str - 0x20);
        for (int r = 0; r < 6; r++, d += t->pitch) {
            int b = g[r];
            if ((b & 1) || b > 14) { t->unsupported++; continue; }
            int p = pat[b >> 1];
            if (p & 1) put(t, d, (uint8_t)colour);
            if (p & 2) put(t, d + 1, (uint8_t)colour);
            if (p & 4) put(t, d + 2, (uint8_t)colour);
        }
        d += 4 - 6 * t->pitch;
    }
}
const uint8_t *tp_native_text;                  /* the string of the last small-font call (native_land.c) */

void tp_remaster_paint_index(TPRemasterPaint *p,int x,int y,unsigned pixel) {
    if(!p)return;
    TPRemasterTarget *t=&p->target;
    if(!t->pixels || !p->palette || p->density<1 || p->density>8 || pixel>255 ||
       t->width<1 || t->height<1 || t->stride<(size_t)t->width ||
       t->stride>SIZE_MAX/(sizeof(uint32_t)*(size_t)t->height)){p->failed=1;return;}
    int64_t left=(int64_t)p->origin_x+(int64_t)x*p->density;
    int64_t top=(int64_t)p->origin_y+(int64_t)y*p->density;
    int64_t right=left+p->density,bottom=top+p->density;
    int x0=t->x0>0?t->x0:0,y0=t->y0>0?t->y0:0;
    int x1=t->x1<t->width?t->x1:t->width,y1=t->y1<t->height?t->y1:t->height;
    if(left>x0){if(left>=x1)return;x0=(int)left;}
    if(top>y0){if(top>=y1)return;y0=(int)top;}
    if(right<x1){if(right<=x0)return;x1=(int)right;}
    if(bottom<y1){if(bottom<=y0)return;y1=(int)bottom;}
    uint32_t c=p->palette[pixel];
    for(int yy=y0;yy<y1;yy++)for(int xx=x0;xx<x1;xx++)t->pixels[(size_t)yy*t->stride+xx]=c;
}
int tp_remaster_paint_image(TPRemasterPaint *p,const TPRemasterImage *im,int x,int y,int flags,unsigned colour) {
    if(!p || !im || !im->rgba)return 0;
    if(!p->palette || p->density<1 || p->density>8 || colour>255 ||
       !tp_remaster_blit_rgba(im,&p->target,(double)p->origin_x+(double)x*p->density,
         (double)p->origin_y+(double)y*p->density,p->density,p->density,flags,p->palette[colour]))p->failed=1;
    return 1;
}
