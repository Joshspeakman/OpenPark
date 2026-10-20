/* Source-free full-colour pack and ordered output-pixel blit checks. */
#include <unistd.h>
#include "../runtime/native_draw.c"
#include "../runtime/remaster_surface.h"
uint32_t tp_map_w=64,tp_map_ext=64;
int tp_native_landscape_paint(const uint8_t *,int,int,int,uint8_t *,int,int,int,int,int,int,int,TPRemasterPaint *);
int tp_native_landscape_part(const uint8_t *,int,int,int,uint8_t *,int,int,int,int,int,int,int);
#define CHECK(x) do {if(!(x)){fprintf(stderr,"RGBA line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static void w16(FILE*f,unsigned x){fputc(x&255,f);fputc(x>>8&255,f);}
static void w32(FILE*f,unsigned x){w16(f,x);w16(f,x>>16);}
static uint32_t texel(int i){return (i==0?0x7f000000u:i==1?0x80000000u:0xff000000u)|(unsigned)(13+i)|((unsigned)(17+i*2)<<8)|((unsigned)(31+i)<<16);}
static FILE *fixture(uint64_t hash){
    FILE*f=tmpfile();if(!f)return NULL;
    fwrite("TPRART03",1,8,f);w32(f,1);
    w16(f,0);w16(f,1);w16(f,4);w16(f,2);w32(f,(unsigned)hash);w32(f,(unsigned)(hash>>32));
    w16(f,65535);w16(f,65535);w16(f,6);w16(f,4);w16(f,2);w16(f,0);
    for(int i=0;i<24;i++)w16(f,i+1);
    for(int i=0;i<96;i++)w32(f,texel(i));
    rewind(f);return f;
}
int main(void){
    uint8_t*m=calloc(64u<<20,1);CHECK(m);
    uint32_t table=512,source=1024;memcpy(m+0x1d3bb8,&table,4);memcpy(m+table+6,&source,4);
    const uint8_t original[]={4,1,2,3,4,0,4,5,6,7,8,0};memcpy(m+source,original,sizeof original);
    uint64_t hash;CHECK(ra_hash(m,source,4,2,&hash));FILE*f=fixture(hash);CHECK(f&&ra_read(f));fclose(f);ra_loaded=1;
    setenv("TP_ART_MODE","reimagined",1);TPRemasterImage im;
    CHECK(tp_remaster_resolve(m,source,8,4,1,&im));CHECK(im.rgba&&im.pixels&&!im.rle);
    CHECK(im.texture_width==12&&im.texture_height==8&&im.width==6&&im.height==4&&im.dx==-1&&im.dy==-1);
    for(int i=0;i<96;i++)CHECK(im.rgba[i]==texel(i));
    /* High-resolution texels retain distinct colours, including those within
     * one logical pixel. Padded output rows and clipped pixels stay untouched. */
    uint32_t out[20*12],want[20*12];
    TPRemasterTarget t={out,20,16,12,3,3,15,11};
    for(int flags=0;flags<4;flags++){
        for(int i=0;i<240;i++)out[i]=want[i]=0xff654321u;
        CHECK(tp_remaster_blit_rgba(&im,&t,4,4,2,2,flags,0));
        for(int v=0;v<8;v++)for(int u=0;u<12;u++){
            int x=(flags&1)?15-u:2+u,y=(flags&2)?11-v:2+v;
            uint32_t c=texel(v*12+u);
            if(x>=3&&x<15&&y>=3&&y<11&&(c>>24)>=128)want[y*20+x]=c|0xff000000u;
        }
        CHECK(!memcmp(out,want,sizeof out));
    }
    /* Fractional placement and non-square output pixels, checked by inverse
     * source coordinates rather than the implementation's texture rectangle. */
    for(int flags=0;flags<4;flags++){
        for(int i=0;i<240;i++)out[i]=want[i]=0xff654321u;
        CHECK(tp_remaster_blit_rgba(&im,&t,4.25,3.75,1.5,.75,flags,0));
        for(int y=3;y<11;y++)for(int x=3;x<15;x++){
            double a=(x+.5-4.25)/1.5,b=(y+.5-3.75)/.75;
            if(flags&1)a=5-a;if(flags&2)b=3-b;
            /* Reversing a half-open texel interval reverses which exact edge
             * it owns: mirrored source intervals are (left,right]. */
            int u=(flags&1)?(int)ceil((a+1)*2)-1:(int)floor((a+1)*2);
            int v=(flags&2)?(int)ceil((b+1)*2)-1:(int)floor((b+1)*2);
            if(u>=0&&u<12&&v>=0&&v<8&&(texel(v*12+u)>>24)>=128)want[y*20+x]=texel(v*12+u)|0xff000000u;
        }
        CHECK(!memcmp(out,want,sizeof out));
    }
    uint32_t colour=0xff14283cu;
    TPRemasterImage one={.width=1,.height=1,.source_width=1,.source_height=1,.rgba=&colour,.texture_width=1,.texture_height=1};
    TPRemasterTarget small={out,20,16,12,0,0,16,12};
    out[0]=0xff204060u;CHECK(tp_remaster_blit_rgba(&one,&small,0,0,1,1,4,0));CHECK(out[0]==0xff1a344eu);
    CHECK(tp_remaster_blit_rgba(&one,&small,0,0,1,1,64,0xff123456u));CHECK(out[0]==0xff123456u);
    colour=0x7f112233u;CHECK(tp_remaster_blit_rgba(&one,&small,0,0,1,1,0,0));CHECK(out[0]==0xff123456u);
    colour=0x80112233u;CHECK(tp_remaster_blit_rgba(&one,&small,0,0,1,1,0,0));CHECK(out[0]==0xff112233u);
    /* Unsupported requests must not partially write a frame. */
    memcpy(want,out,sizeof out);
    CHECK(!tp_remaster_blit_rgba(&one,&small,0,0,1,1,8,0));
    CHECK(!tp_remaster_blit_rgba(&one,&small,NAN,0,1,1,0,0));
    CHECK(!tp_remaster_blit_rgba(&one,&small,0,0,0,1,0,0));
    CHECK(tp_remaster_blit_rgba(&one,&small,1e100,-1e100,1,1,0,0));
    CHECK(!memcmp(out,want,sizeof out));
    small.stride=SIZE_MAX;CHECK(!tp_remaster_blit_rgba(&one,&small,0,0,1,1,0,0));
    /* Native ordered rendering: fine texels survive their own indexed preview,
     * while a later legacy sprite and text cover them normally. */
    uint32_t pal[256];for(int i=0;i<256;i++)pal[i]=0xff000000u|(unsigned)i*0x010101u;
    uint8_t indexed[16*12]={0},reference[16*12]={0};
    TPRemasterPaint paint={.target={out,20,16,12,0,0,16,12},.palette=pal,.density=2};
    NDTarget nd={indexed,sizeof indexed,0,16,16,12,15,1,NULL,0,&paint},plain=nd;
    plain.buf=reference;plain.paint=NULL;
    for(int i=0;i<240;i++)out[i]=0xff654321u;
    tp_native_draw_world_sprite(&nd,m,source,8,4,4,4,0,0,NULL);
    tp_native_draw_world_sprite(&plain,m,source,8,4,4,4,0,0,NULL);
    CHECK(!memcmp(indexed,reference,sizeof indexed));CHECK(!paint.failed);
    CHECK(out[4*20+4]==texel(2*12+2));CHECK(out[4*20+5]==texel(2*12+3));
    const uint8_t foreground[]={1,99,0};
    tp_native_draw_sprite(&nd,foreground,2,2,4,4,0,0);
    CHECK(out[4*20+4]==pal[99]&&out[5*20+5]==pal[99]);
    CHECK(out[4*20+6]==texel(2*12+4));
    /* Clip-window base is included in the companion's placement. */
    memset(indexed,0,sizeof indexed);nd.base=16+1;
    tp_native_draw_world_sprite(&nd,m,source,8,4,4,4,0,0,NULL);
    CHECK(out[6*20+6]==texel(2*12+2));CHECK(nd.paint==&paint);
    nd.base=0;
    memset(m+0x1ba2fc+6*('A'-0x20),0,6);
    tp_native_draw_small_text(&nd,m,4,0,(const uint8_t*)"A",42);
    CHECK(out[8]==pal[42]&&out[20+9]==pal[42]);
    /* Terrain uses the same high-resolution companion and still yields the
     * exact same indexed output used by game/UI bookkeeping. */
    uint32_t map=0x300000;memcpy(m+0x1d3b08,&map,4);memcpy(m+0x1d3b90,&table,4);
    m[table+10]=8;m[table+11]=4;m[0x1d663a]=48;m[0x1d6654]=32;m[0x1de5f6]=1;
    uint8_t land[320*64]={0},land_ref[320*64]={0};uint32_t *land_rgba=calloc(640*128,sizeof(uint32_t));CHECK(land_rgba);
    paint=(TPRemasterPaint){.target={land_rgba,640,640,128,0,0,640,128},.palette=pal,.density=2};
    CHECK(!tp_native_landscape_paint(m,10,10,4,land,320,320,64,0,0,320,64,&paint));
    CHECK(!tp_native_landscape_part(m,10,10,4,land_ref,320,320,64,0,0,320,64));
    CHECK(!paint.failed&&!memcmp(land,land_ref,sizeof land));
    /* First ground block at logical (296,14), expanded bounds (295,13). */
    CHECK(land_rgba[28*640+592]==texel(2*12+2));
    CHECK(land_rgba[28*640+593]==texel(2*12+3));free(land_rgba);
    /* Visible-region storage: bounded allocation, panning, density reduction,
     * exclusive sampling boundaries and invalid inputs cannot retain old art. */
    TPRemasterSurface surface={0};
    CHECK(tp_remaster_surface_prepare(&surface,10.5,20.25,20,30,100,100));
    CHECK(surface.x==2&&surface.y==12&&surface.w==37&&surface.h==47&&surface.density==4);
    surface.pixels[0]=0xff987654u;uint32_t sampled=0;
    CHECK(tp_remaster_surface_sample(&surface,2,12,&sampled)&&sampled==0xff987654u);
    CHECK(!tp_remaster_surface_sample(&surface,39,12,&sampled));
    CHECK(!tp_remaster_surface_sample(&surface,2,59,&sampled));
    CHECK(!tp_remaster_surface_sample(&surface,NAN,12,&sampled));
    CHECK(tp_remaster_surface_prepare(&surface,0,0,4400,2400,4400,2400));
    CHECK(surface.density==1&&surface.capacity<=TP_REMASTER_SURFACE_PIXELS);
    CHECK(!tp_remaster_surface_prepare(&surface,0,0,16384,16384,16384,16384)&&!surface.valid);
    CHECK(!tp_remaster_surface_prepare(&surface,NAN,0,10,10,100,100)&&!surface.valid);
    free(surface.pixels);
    setenv("TP_ART_MODE","original",1);CHECK(!tp_remaster_resolve(m,source,8,4,1,&im));CHECK(!im.rgba);
    setenv("TP_ART_MODE","reimagined",1);m[source+1]=88;CHECK(!tp_remaster_resolve(m,source,8,4,1,&im));m[source+1]=1;
    CHECK(!memcmp(m+source,original,sizeof original));ra_clear();
    const int offsets[]={36,36,38,32};const unsigned values[]={0,9,1,2048};
    for(int i=0;i<4;i++){
        f=fixture(hash);CHECK(f);fseek(f,offsets[i],SEEK_SET);w16(f,values[i]);rewind(f);
        CHECK(!ra_read(f)&&!ra_count);fclose(f);
    }
    f=fixture(hash);CHECK(f);fseek(f,0,SEEK_END);long size=ftell(f);CHECK(!fflush(f));CHECK(!ftruncate(fileno(f),size-1));rewind(f);
    CHECK(!ra_read(f)&&!ra_count);fclose(f);
    f=fixture(hash);CHECK(f);fseek(f,0,SEEK_END);fputc(0,f);rewind(f);CHECK(!ra_read(f)&&!ra_count);fclose(f);
    free(m);puts("remaster RGBA: density, source pivots, output clipping/stride, cutouts, ghosting, isolation and malformed packs passed");return 0;
}
