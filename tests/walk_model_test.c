/* Authored mesh loader/sampling without any game-derived data. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../runtime/walk_mesh.h"
#include "../runtime/walk_model.h"
#define CHECK(x) do {if(!(x)){fprintf(stderr,"model test failed: %s:%d: %s\n",__FILE__,__LINE__,#x);return 1;}}while(0)
static int read_text(const char *text,WMModel *m) {
    FILE*f=tmpfile();if(!f)return 0;fputs(text,f);rewind(f);
    int ok=wm_model_read(f,m);fclose(f);return ok;
}
int main(void) {
    WMModel m={0};
    CHECK(!read_text("TPMESH1 99999 1 16 16",&m));
    CHECK(!read_text("TPMESH1 3 1 0 16",&m));
    CHECK(!read_text("TPMESH1 3 1 16 16\n0 0 0 0 0\n1 0 0 1 0\n0 0 1 0 1\n0 1 3",&m));
    CHECK(!read_text("TPMESH1 3 1 16 16\nnan 0 0 0 0",&m));
    CHECK(read_text("TPMESH1 4 2 16 16\n-1 0 0 0 16\n1 0 0 16 16\n1 0 2 16 0\n-1 0 2 0 0\n0 1 2\n0 2 3",&m));
    uint32_t pixels[64*64]={0},pal[256];float depth[64*64];uint16_t tex[16*16];
    for(int i=0;i<256;i++){tex[i]=i%16<8 ? 17:91;pal[i]=0xff000000u|(uint32_t)i*65793;}
    for(int i=0;i<64*64;i++)depth[i]=FLT_MAX;
    WMRender r={.pixels=pixels,.depth=depth,.w=64,.h=64,.cy=-4,.cz=1,
        .fy=1,.rx=1,.uz=1,.focal=64,.palette=pal,.texture=tex,.tw=16,.th=16};
    CHECK(wm_model_draw(&r,&m,0,0,1));
    CHECK(pixels[32*64+20]==pal[17] && pixels[32*64+44]==pal[91]);
    for(int i=0;i<64*64;i++)CHECK(!pixels[i] || pixels[i]==pal[17] || pixels[i]==pal[91]);
    /* Existing geometry samples a changed simulation frame, without caching art. */
    for(int i=0;i<256;i++)tex[i]=29;
    for(int i=0;i<64*64;i++)depth[i]=FLT_MAX;
    CHECK(wm_model_draw(&r,&m,0,0,1));CHECK(pixels[32*64+32]==pal[29]);
    /* A differently cropped frame must use the original sprite, not shifted UVs. */
    r.tw=15;CHECK(!wm_model_draw(&r,&m,0,0,1));
    free(m.vertices);free(m.indices);
    puts("authored model: bounded loading, invalid data, exact texels, frame changes and extent fallback passed");
    return 0;
}
