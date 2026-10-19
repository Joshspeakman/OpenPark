#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../runtime/walk_mesh.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"mesh test failed: %s:%d: %s\n",__FILE__,__LINE__,#x); return 1; } } while(0)
static uint32_t pixels[128*96],palette[256];
static float depth[128*96]; static uint16_t texture[16*16];
static void clear(void) { memset(pixels,0,sizeof pixels); for(int i=0;i<128*96;i++) depth[i]=FLT_MAX; }
static unsigned count(uint32_t colour) { unsigned n=0;for(int i=0;i<128*96;i++) n+=pixels[i]==colour;return n; }
int main(void) {
    for(int i=0;i<256;i++) palette[i]=0xff000000u|((uint32_t)i*65793u);
    for(int i=0;i<256;i++) texture[i]=7;
    WMRender r={.pixels=pixels,.depth=depth,.w=128,.h=96,.cx=0,.cy=-4,.cz=1,
        .fx=0,.fy=1,.fz=0,.rx=1,.ry=0,.ux=0,.uy=0,.uz=1,.focal=90,
        .palette=palette,.texture=texture,.tw=16,.th=16};
    WMShell s={.x=0,.y=0,.kx=.1f,.ky=.1f,.depth=.5f,.base=0,
        .ax=8,.ay=16,.w=16,.h=16,.shape=WM_ROUND,.rx=1,.ry=0,.dx=0,.dy=1};
    clear();wm_shell(&r,&s,4);CHECK(count(palette[7])>500);
    for(int i=0;i<128*96;i++) CHECK(!pixels[i] || pixels[i]==palette[7]);
    /* A second surface in front must win regardless of draw order. */
    for(int i=0;i<256;i++)texture[i]=13;
    s.y=-1;wm_shell(&r,&s,3);CHECK(pixels[48*128+64]==palette[13]);
    for(int i=0;i<256;i++)texture[i]=7;
    s.y=0;wm_shell(&r,&s,4);CHECK(pixels[48*128+64]==palette[13]);
    /* Alpha holes do not occlude; every animated source frame is sampled live. */
    for(int i=0;i<256;i++)texture[i]=0xffff;
    clear();wm_shell(&r,&s,4);CHECK(count(0)==128*96);
    for(int i=0;i<256;i++)texture[i]=21;
    clear();wm_shell(&r,&s,4);CHECK(count(palette[21])>500);
    /* The same fixed shell still has thickness when observed from the side. */
    r.cx=-4;r.cy=0;r.fx=1;r.fy=0;r.rx=0;r.ry=-1;
    clear();wm_shell(&r,&s,4);CHECK(count(palette[21])>300);
    /* A partially near-clipped triangle survives with finite positive depths. */
    clear();wm_triangle(&r,(WMVertex){-.1f,-.1f,.01f,0,0},
        (WMVertex){.4f,-.1f,1,15,0},(WMVertex){0,.4f,1,8,15});
    CHECK(count(palette[21])>0);
    for(int i=0;i<128*96;i++)if(pixels[i])CHECK(isfinite(depth[i]) && depth[i]>=.079f);
    puts("pixel mesh: palette, depth order, transparency, animation, side volume, near clipping passed");
    return 0;
}
