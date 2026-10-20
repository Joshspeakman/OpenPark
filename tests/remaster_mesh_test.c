/* Synthetic engineering fixtures, never installed as game artwork. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../runtime/walk_mesh.h"
#include "../runtime/walk_rig.h"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"remaster mesh line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static int load(const char*s,RMRig*r){FILE*f=tmpfile();if(!f)return 0;fputs(s,f);rewind(f);int ok=rm_read(f,r);fclose(f);return ok;}
static int bindings(const char*s,RMBinding**b,unsigned*n){FILE*f=tmpfile();if(!f)return 0;fputs(s,f);rewind(f);int ok=rm_read_bindings(f,b,n);fclose(f);return ok;}
static uint32_t pixels[64*64];static float depth[64*64];
static void clear(void){memset(pixels,0,sizeof pixels);for(int i=0;i<64*64;i++)depth[i]=FLT_MAX;}
static void release(RMRig*r){free(r->v);free(r->t);free(r->p);free(r->matrices);free(r->texture);memset(r,0,sizeof*r);}
int main(void) {
    RMRig rig={0};
    const char*text="TPRIG2 4 2 1 1 0 2 1\n123456ff ff000000\n"
        "-1 0 0 0 0 1\n1 0 0 0 2 1\n1 0 2 0 2 0\n-1 0 2 0 0 0\n"
        "0 1 2 ffffff\n0 2 3 ffffff\n10\n1 0 0 0 0 1 0 0 0 0 1 0\n";
    CHECK(load(text,&rig));CHECK(rig.texture&&rig.tw==2&&rig.th==1);
    CHECK(rig.texture[0]==0xff563412);CHECK(rig.texture[1]==0xff);
    WMRender r={.pixels=pixels,.depth=depth,.w=64,.h=64,.cy=-4,.cz=1,.fy=1,.rx=1,.uz=1,.focal=64};
    clear();rm_draw(&r,&rig,rm_pose(&rig,10),0,0,1,NULL);
    CHECK(pixels[32*64+24]==0xff563412);CHECK(!pixels[32*64+40]);
    CHECK(depth[32*64+40]==FLT_MAX);CHECK(!r.rgba);
    /* Authored tint must affect both projections and restore caller state. */
    rig.t[0].colour=rig.t[1].colour=0xff4080ff;r.texture_tint=0xffabcdef;
    clear();rm_draw(&r,&rig,rm_pose(&rig,10),0,0,1,NULL);
    CHECK(pixels[32*64+24]==0xff151a12);CHECK(r.texture_tint==0xffabcdef);
    r.orthographic=1;r.focal=16;
    clear();rm_draw(&r,&rig,rm_pose(&rig,10),0,0,1,NULL);
    CHECK(pixels[32*64+24]==0xff151a12);CHECK(r.texture_tint==0xffabcdef);
    rig.t[0].colour=rig.t[1].colour=0xffffffff;r.texture_tint=0;
    r.orthographic=0;r.focal=64;
    rig.texture[0]=0x80563412;
    clear();rm_draw(&r,&rig,rm_pose(&rig,10),0,0,1,NULL);
    CHECK(pixels[32*64+24]==0xff563412); /* cutout threshold yields opaque pixels */
    rig.texture[0]=0xff563412;
    /* RGBA texture survives orthographic baking with precisely the same pose. */
    r.orthographic=1;r.focal=16;
    clear();rm_draw(&r,&rig,rm_pose(&rig,10),0,0,1,NULL);
    CHECK(pixels[32*64+24]==0xff563412);CHECK(!pixels[32*64+40]);
    uint32_t first[64*64];memcpy(first,pixels,sizeof first);
    r.cy=-8;clear();rm_draw(&r,&rig,rm_pose(&rig,10),0,0,1,NULL);
    CHECK(!memcmp(first,pixels,sizeof first)); /* distance does not change scale */
    /* A transparent near surface must leave a farther opaque surface visible. */
    uint32_t solid=0xff998877;r.rgba=&solid;r.tw=r.th=1;
    wm_quad(&r,(WMVertex){-1,-1,90,0,0},(WMVertex){1,-1,90,0,0},
               (WMVertex){1,1,90,0,0},(WMVertex){-1,1,90,0,0});
    CHECK(pixels[32*64+24]==0xff563412);CHECK(pixels[32*64+40]==solid);
    release(&rig);
    CHECK(!load("TPRIG2 3 1 1 1 0 9000 1",&rig));
    CHECK(!load("TPRIG2 3 1 1 1 0 1 1 ffffffff 0 0 0 0 nan 0",&rig));
    CHECK(!load("TPRIG2 3 1 1 1 0 1 1 ffffffff 0 0 0 0 2 0",&rig));
    RMBinding*b=NULL;unsigned n=0;
    CHECK(bindings("TPRMAP1 3\n24 10 255 4\n24 11 255 4\n25 2 255 7\n",&b,&n));
    CHECK(n==3 && b[1].type==4 && b[2].cls==25);free(b);
    CHECK(!bindings("TPRMAP1 2\n24 10 255 4\n24 10 255 7\n",&b,&n));
    CHECK(!bindings("TPRMAP1 2\n24 11 255 4\n24 10 255 4\n",&b,&n));
    CHECK(!bindings("TPRMAP1 1\n256 10 255 4\n",&b,&n));
    CHECK(!bindings("TPRMAP1 1\n24 10 255 4 extra\n",&b,&n));
    CHECK(!bindings("TPRMAP1 9000\n",&b,&n));
    CHECK(!bindings("TPRMAP1 1\n24 10\n",&b,&n));CHECK(!b && !n);
    puts("remaster mesh: RGBA/tint, alpha depth, shared pose, orthographic scale, bounded UV/atlas and frame bindings passed");
    return 0;
}
