/* Included once by native_draw.c. The game thread owns this immutable cache.
 *
 * TPRART01: magic[8], u32 count; sorted records {u16 bank,index,w,h;
 * u64 original decoded-pixel FNV1a; u16 replacement[w*h]} (little endian).
 * Bank 0 is world sprites, bank 1 terrain. 256 denotes transparency.
 * TPRART02 adds {i16 dx,dy; u16 draw_w,draw_h} after each record's source
 * metadata, followed by draw_w*draw_h indexed pixels. Expanded bounds never
 * change the source binding or its mirror/flip pivot.
 * Both formats retain the native palette. V1 keeps legacy clipping; V2 uses
 * bounded rectangle clipping for its expanded pixels. Original/UI drawing
 * remains unchanged. A source-content match is mandatory.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "art_mode.h"
#include "remaster_art.h"

typedef struct {
    uint16_t bank,index,w,h;
    uint64_t hash;
    uint8_t *rle;
    uint16_t *pixels;
    int dx,dy,dw,dh;
} RAEntry;
static RAEntry *ra_entries;
static unsigned ra_count;
static int ra_loaded;

static uint32_t ra_u32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static unsigned ra_u16(const uint8_t *p) { return p[0]|(unsigned)p[1]<<8; }
static uint64_t ra_u64(const uint8_t *p) { return ra_u32(p)|((uint64_t)ra_u32(p+4)<<32); }
static void ra_clear(void) {
    for (unsigned i=0;i<ra_count;i++) {free(ra_entries[i].rle);free(ra_entries[i].pixels);}
    free(ra_entries);ra_entries=NULL;ra_count=0;
}
static int ra_read(FILE *f) {
    uint8_t header[12];
    if (fread(header,1,12,f)!=12) return 0;
    int expanded=!memcmp(header,"TPRART02",8);
    if (!expanded && memcmp(header,"TPRART01",8)) return 0;
    unsigned count=ra_u32(header+8);
    if (!count || count>8192 || ra_entries) return 0;
    ra_entries=calloc(count,sizeof *ra_entries);
    if (!ra_entries) return 0;
    ra_count=count;
    size_t total=0;
    for (unsigned i=0;i<count;i++) {
        uint8_t meta[16];
        if (fread(meta,1,16,f)!=16) goto bad;
        RAEntry *e=&ra_entries[i];
        e->bank=ra_u16(meta);e->index=ra_u16(meta+2);
        e->w=ra_u16(meta+4);e->h=ra_u16(meta+6);e->hash=ra_u64(meta+8);
        unsigned key=e->bank*65536u+e->index;
        if (e->bank>1 || e->index>8191 || !e->w || !e->h || e->w>255 || e->h>255 ||
            (i && key<=ra_entries[i-1].bank*65536u+ra_entries[i-1].index)) goto bad;
        e->dw=e->w;e->dh=e->h;
        if(expanded) {
            uint8_t bounds[8];if(fread(bounds,1,8,f)!=8)goto bad;
            e->dx=(int16_t)ra_u16(bounds);e->dy=(int16_t)ra_u16(bounds+2);
            e->dw=ra_u16(bounds+4);e->dh=ra_u16(bounds+6);
            if(e->dx < -2048 || e->dx > 2048 || e->dy < -2048 || e->dy > 2048 ||
               e->dw<1 || e->dh<1 || e->dw>2048 || e->dh>2048)goto bad;
            size_t n=(size_t)e->dw*e->dh;
            total+=n*sizeof(uint16_t);if(total>64u*1024*1024)goto bad;
            e->pixels=malloc(n*sizeof(uint16_t));if(!e->pixels)goto bad;
            for(size_t j=0;j<n;j++){
                uint8_t p[2];if(fread(p,1,2,f)!=2 || (e->pixels[j]=(uint16_t)ra_u16(p))>256)goto bad;
            }
            continue;
        }
        size_t n=(size_t)e->w*e->h, cap=n*3+e->h;
        total+=cap;
        if (total>64u*1024*1024) goto bad;
        uint16_t pix[255*255];
        for (size_t j=0;j<n;j++) {
            uint8_t p[2];if(fread(p,1,2,f)!=2 || (pix[j]=(uint16_t)ra_u16(p))>256) goto bad;
        }
        e->rle=malloc(cap);if(!e->rle) goto bad;
        size_t at=0;
        for (int y=0;y<e->h;y++) {
            int x=0,last=e->w;
            while(last && pix[y*e->w+last-1]==256)last--;
            while(x<last) {
                int start=x, empty=pix[y*e->w+x]==256;
                while(x<last && (empty || x-start<127) && (pix[y*e->w+x]==256)==empty) x++;
                if(empty && x-start>128)goto bad;
                e->rle[at++]=(uint8_t)(empty?-(x-start):x-start);
                if(!empty)for(int k=start;k<x;k++)e->rle[at++]=(uint8_t)pix[y*e->w+k];
            }
            e->rle[at++]=0;
        }
    }
    if(fgetc(f)!=EOF) goto bad;
    return 1;
bad:ra_clear();return 0;
}
/* Decode only within the declared source image and memory allocation. */
static int ra_hash(const uint8_t *m,uint32_t p,int w,int h,uint64_t *result) {
    const uint32_t limit=64u<<20;
    uint64_t hash=14695981039346656037ull;
    for(int y=0;y<h;y++) {
        int x=0,guard=0;
        for(;;) {
            if(p>=limit || ++guard>2048) return 0;
            int c=(int8_t)m[p++];
            if(!c) break;
            int n=c<0?-c:c;if(n>w-x || (c>0 && p+(unsigned)n>limit))return 0;
            for(int k=0;k<n;k++) {
                unsigned v=c<0?256:m[p++];
                hash=(hash^(v&255))*1099511628211ull;
                hash=(hash^(v>>8))*1099511628211ull;
            }
            x+=n;
        }
        while(x++<w){hash=(hash^0)*1099511628211ull;hash=(hash^1)*1099511628211ull;}
    }
    *result=hash;return 1;
}
static void ra_load(void) {
    if(ra_loaded)return;ra_loaded=1;
    const char *root=getenv("TP_DATA");if(!root)return;
    char path[4096];int n=snprintf(path,sizeof path,"%s/reimagined/world.art",root);
    if(n<0 || (size_t)n>=sizeof path)return;
    FILE *f=fopen(path,"rb");if(!f)return;
    int ok=ra_read(f);fclose(f);
    fprintf(stderr,"Reimagined world pack: %s (%u baked parts)\n",ok?"loaded":"invalid",ra_count);
}
int tp_remaster_resolve(const uint8_t *m,uint32_t source,int width,int height,int lo,TPRemasterImage *out) {
    if(!out || lo<0 || lo>1)return 0;
    memset(out,0,sizeof *out);
    if(tp_art_mode()!=TP_ART_REIMAGINED)return 0;
    ra_load();
    if(!ra_count)return 0;
    /* Only entries in this pack are examined.
     * No simulation memory, sprite pointer, palette or animation link is edited. */
    int w=(width&255)>>lo,h=(height&255)>>lo;
    for(unsigned i=0;i<ra_count;i++) {
        RAEntry *e=&ra_entries[i];
        if(e->w!=w || e->h!=h)continue;
        uint32_t table=ra_u32(m+(e->bank?0x1d3b90:0x1d3bb8));
        uint64_t entry=(uint64_t)table+e->index*6;
        if(!table || entry+6>(64u<<20) || ra_u32(m+entry)!=source)continue;
        uint64_t hash=0;
        if(!ra_hash(m,source,w,h,&hash) || hash!=e->hash)return 0;
        *out=(TPRemasterImage){e->rle,e->pixels,e->dw,e->dh,e->dx,e->dy,e->w,e->h};
        return 1;
    }
    return 0;
}
const uint8_t *tp_remaster_source(const uint8_t *m,uint32_t source,int width,int height,int lo) {
    TPRemasterImage image;
    return tp_remaster_resolve(m,source,width,height,lo,&image) && image.rle?image.rle:m+source;
}
