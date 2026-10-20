/* Synthetic artwork only. Tests replacement bindings and native blitter edges. */
#include "../runtime/native_draw.c"
#include "../runtime/native_land.c"
uint32_t tp_map_w=64,tp_map_ext=64;
#define CHECK(x) do { if(!(x)){fprintf(stderr,"remaster line %d: %s\n",__LINE__,#x);return 1;} } while(0)
static void w16(FILE*f,unsigned x){fputc(x&255,f);fputc(x>>8&255,f);}
static void w32(FILE*f,unsigned x){w16(f,x);w16(f,x>>16);}
static FILE *fixture(uint64_t hash) {
    FILE*f=tmpfile();if(!f)return NULL;
    fwrite("TPRART01",1,8,f);w32(f,1);
    w16(f,0);w16(f,1);w16(f,4);w16(f,2);
    w32(f,(unsigned)hash);w32(f,(unsigned)(hash>>32));
    unsigned p[]={256,10,11,256,12,256,13,14};
    for(int i=0;i<8;i++)w16(f,p[i]);
    rewind(f);return f;
}
static FILE *expanded_fixture(uint64_t hash) {
    FILE*f=tmpfile();if(!f)return NULL;
    fwrite("TPRART02",1,8,f);w32(f,1);
    w16(f,0);w16(f,1);w16(f,4);w16(f,2);
    w32(f,(unsigned)hash);w32(f,(unsigned)(hash>>32));
    w16(f,65535);w16(f,65535);w16(f,6);w16(f,4);
    for(int i=0;i<24;i++)w16(f,i==7?256:10+i);
    rewind(f);return f;
}
int main(void) {
    uint8_t *m=calloc(64u<<20,1);CHECK(m);
    uint32_t table=512,source=1024;
    memcpy(m+0x1d3bb8,&table,4);memcpy(m+table+6,&source,4);
    m[table+10]=8;m[table+11]=4;
    const uint8_t original[]={4,1,2,3,4,0,4,5,6,7,8,0};
    memcpy(m+source,original,sizeof original);
    uint64_t hash;CHECK(ra_hash(m,source,4,2,&hash));
    CHECK(hash==0x67c564b0deac075dull); /* independently computed by Python */
    FILE*f=fixture(hash);CHECK(f);CHECK(ra_read(f));fclose(f);ra_loaded=1;
    setenv("TP_ART_MODE","original",1);
    CHECK(tp_remaster_source(m,source,8,4,1)==m+source);
    setenv("TP_ART_MODE","faithful3d",1);
    CHECK(tp_remaster_source(m,source,8,4,1)==m+source);
    setenv("TP_ART_MODE","reimagined",1);
    const uint8_t*art=tp_remaster_source(m,source,8,4,1);
    CHECK(art!=m+source);
    CHECK(tp_remaster_source(m,source,8,4,0)==m+source);
    CHECK(tp_remaster_source(m,source+1,8,4,1)==m+source+1);
    /* A changed source at the same address must not retain a cached binding. */
    m[source+1]=88;CHECK(tp_remaster_source(m,source,8,4,1)==m+source);
    m[source+1]=1;
    uint8_t pixels[8*6],expected[8*6],ghost[65536];
    for(int i=0;i<65536;i++)ghost[i]=(uint8_t)((i>>8)+(i&255));
    for(int flags=0;flags<4;flags++) {
        memset(pixels,99,sizeof pixels);memset(expected,99,sizeof expected);
        NDTarget t={pixels,sizeof pixels,0,8,8,6,7,0,ghost,0};
        tp_native_draw_sprite(&t,art,4,2,2,1,flags,0);
        unsigned src[]={256,10,11,256,12,256,13,14};
        for(int y=0;y<2;y++)for(int x=0;x<4;x++)
            if(src[y*4+x]<256)expected[(flags&2?3-y:1+y)*8+(flags&1?6-x:2+x)]=(uint8_t)src[y*4+x];
        CHECK(!memcmp(pixels,expected,sizeof pixels));
    }
    /* Left-clipped normal path consumes skip/run pairs differently. */
    memset(pixels,99,sizeof pixels);
    NDTarget t={pixels,sizeof pixels,0,8,8,6,7,0,ghost,0};
    tp_native_draw_sprite(&t,art,4,2,-1,1,0,0);
    CHECK(pixels[8]==10&&pixels[9]==11&&pixels[10]==99);
    CHECK(pixels[16]==99&&pixels[17]==13&&pixels[18]==14);
    /* Ghosting, colour overrides and top clipping retain native semantics. */
    memset(pixels,20,sizeof pixels);tp_native_draw_sprite(&t,art,4,2,1,1,4,0);
    CHECK(pixels[10]==30&&pixels[11]==31&&pixels[9]==20);
    memset(pixels,20,sizeof pixels);tp_native_draw_sprite(&t,art,4,2,1,1,64,7);
    CHECK(pixels[10]==7&&pixels[11]==7&&pixels[9]==20);
    memset(pixels,20,sizeof pixels);tp_native_draw_sprite(&t,art,4,2,1,-1,0,0);
    CHECK(pixels[1]==12&&pixels[2]==20&&pixels[3]==13&&pixels[4]==14);
    CHECK(!memcmp(m+source,original,sizeof original));
    ra_clear();
    f=fixture(hash);CHECK(f);fseek(f,28,SEEK_SET);w16(f,257);rewind(f);
    CHECK(!ra_read(f));CHECK(!ra_count);fclose(f);
    f=fixture(hash);CHECK(f);fseek(f,0,SEEK_END);fputc(1,f);rewind(f);
    CHECK(!ra_read(f));fclose(f);
    f=tmpfile();CHECK(f);fwrite("TPRART01",1,8,f);w32(f,9000);rewind(f);
    CHECK(!ra_read(f));fclose(f);
    f=expanded_fixture(hash);CHECK(f);CHECK(ra_read(f));fclose(f);
    TPRemasterImage image;
    CHECK(tp_remaster_resolve(m,source,8,4,1,&image));
    CHECK(image.pixels && !image.rle && image.dx==-1 && image.dy==-1 && image.width==6 && image.height==4);
    for(int flags=0;flags<4;flags++)for(int yy=-2;yy<=5;yy+=3)for(int xx=-3;xx<=7;xx+=5){
        memset(pixels,99,sizeof pixels);memset(expected,99,sizeof expected);
        NDTarget draw={pixels,sizeof pixels,0,8,8,6,7,1,ghost,0};
        int bounds[4];tp_native_draw_world_sprite(&draw,m,source,8,4,xx*2,yy*2,flags,0,bounds);
        for(int v=0;v<4;v++)for(int u=0;u<6;u++){
            int pixel=v*6+u;if(pixel==7)continue;
            int x=xx+((flags&1)?4-(-1+u):-1+u);
            int y=yy+((flags&2)?2-(-1+v):-1+v);
            if(x>=0 && x<8 && y>=0 && y<6)expected[y*8+x]=(uint8_t)(10+pixel);
        }
        CHECK(!memcmp(pixels,expected,sizeof pixels));
        CHECK(bounds[2]-bounds[0]==6 && bounds[3]-bounds[1]==4 && !draw.unsupported);
        memset(pixels,99,sizeof pixels);M=m;
        Target ground={pixels,8,8,6,1,0,0,7,5};
        blit(&ground,table+6,xx*2,yy*2,flags);
        CHECK(!memcmp(pixels,expected,sizeof pixels));
    }
    NDTarget draw={pixels,sizeof pixels,0,8,8,6,7,1,ghost,0};
    memset(pixels,20,sizeof pixels);
    tp_native_draw_world_sprite(&draw,m,source,8,4,0,0,4,0,NULL);
    CHECK(pixels[0]==20 && pixels[1]==38); /* transparent corner + clipped ghost */
    memset(pixels,20,sizeof pixels);
    tp_native_draw_world_sprite(&draw,m,source,8,4,0,0,64,7,NULL);
    CHECK(pixels[0]==20 && pixels[1]==7);
    setenv("TP_ART_MODE","original",1);
    CHECK(!tp_remaster_resolve(m,source,8,4,1,&image));
    memset(pixels,99,sizeof pixels);memset(expected,99,sizeof expected);
    tp_native_draw_world_sprite(&draw,m,source,8,4,2,2,0,0,NULL);
    draw.buf=expected;tp_native_draw_sprite(&draw,m+source,8,4,2,2,0,0);
    CHECK(!memcmp(pixels,expected,sizeof pixels));CHECK(!memcmp(m+source,original,sizeof original));
    setenv("TP_ART_MODE","reimagined",1);ra_clear();
    f=expanded_fixture(hash);CHECK(f);fseek(f,32,SEEK_SET);w16(f,0);rewind(f);
    CHECK(!ra_read(f));CHECK(!ra_count);fclose(f);
    f=expanded_fixture(hash);CHECK(f);fseek(f,28,SEEK_SET);w16(f,3000);rewind(f);
    CHECK(!ra_read(f));CHECK(!ra_count);fclose(f);
    f=expanded_fixture(hash);CHECK(f);fseek(f,36,SEEK_SET);w16(f,257);rewind(f);
    CHECK(!ra_read(f));CHECK(!ra_count);fclose(f);
    free(m);puts("remaster art: source binding, mode isolation, native clipping/mirroring/ghosting, malformed packs passed");
    return 0;
}
