/* Source-free large-park regression: misses must not starve later models,
 * and resolving hundreds of aliases must not move live assembly pointers. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include "../runtime/walk_mesh.h"
#include "../runtime/walk_rig.h"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"cache line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static const char *root="build/remaster-cache-fixture";
static int fixture(int type,int valid){
    char path[256];snprintf(path,sizeof path,"%s/reimagined/12-%d.rig",root,type);
    FILE*f=fopen(path,"w");if(!f)return 0;
    if(valid)fprintf(f,"TPRIG1 3 1 1 1 0\n-1 0 0 0\n1 0 0 0\n0 0 2 0\n"
        "0 1 2 %06x\n10\n1 0 0 0 0 1 0 0 0 0 1 0\n",0x120000u+(unsigned)type);
    else fputs("TPRIG1 3 1 1 1 0\n",f);
    int bad=ferror(f);if(fclose(f))bad=1;return !bad;
}
static void picture(const RMRig*r,const RMPose*p,uint32_t*pixels){
    float depth[64*64];memset(pixels,0,64*64*sizeof *pixels);
    for(int i=0;i<64*64;i++)depth[i]=FLT_MAX;
    WMRender wr={.pixels=pixels,.depth=depth,.w=64,.h=64,.cy=-4,.cz=1,
        .fy=1,.rx=1,.uz=1,.focal=64};
    rm_draw(&wr,r,p,0,0,1,NULL);
}
int main(void){
    CHECK(!mkdir(root,0700)||errno==EEXIST);
    CHECK(!mkdir("build/remaster-cache-fixture/reimagined",0700)||errno==EEXIST);
    CHECK(!setenv("TP_DATA",root,1));
    CHECK(!rm_find(-1,0)&&!rm_find(256,0)&&!rm_find(0,-1)&&!rm_find(0,32768));
    for(int c=0;c<256;c++)for(int p=0;p<128;p++)CHECK(!rm_pages[c][p]);
    /* Negative lookups used to consume every slot before these models loaded. */
    for(int type=20000;type<24096;type++)CHECK(!rm_find(18,type));
    CHECK(fixture(32767,1));const RMRig*held=rm_find(18,32767);CHECK(held);
    const RMPose*pose=rm_pose(held,10);CHECK(pose);
    uint32_t before[64*64],after[64*64];picture(held,pose,before);
    CHECK(before[32*64+32]);
    rm_bindings=calloc(512,sizeof *rm_bindings);CHECK(rm_bindings);
    rm_nbindings=512;rm_bindings_loaded=1;
    for(int i=0;i<512;i++)rm_bindings[i]=(RMBinding){18,(unsigned)i+1,18,(unsigned)i+1000};
    for(int i=0;i<512;i++){
        CHECK(fixture(1000+i,1));
        const RMRig*r=rm_find(18,1000+i);CHECK(r&&r->cls==18&&r->type==1000+i&&r->nv==3);
        CHECK(rm_find_frame(18,i+1)==r);CHECK(rm_find(18,32767)==held&&rm_pose(held,10)==pose);
    }
    for(int i=511;i>=0;i--){
        const RMRig*r=rm_find_frame(18,i+1);CHECK(r&&r->type==1000+i&&rm_pose(r,10));
        CHECK(r==rm_find(18,1000+i));
    }
    picture(held,pose,after);CHECK(!memcmp(before,after,sizeof before));
    CHECK(fixture(32766,0));CHECK(!rm_find(18,32766));
    CHECK(fixture(32766,1));CHECK(!rm_find(18,32766)); /* immutable until reset */
    CHECK(rm_find(18,32767)==held);
    rm_reset();CHECK(!rm_bindings&&!rm_nbindings&&!rm_bindings_loaded);
    for(int c=0;c<256;c++)for(int p=0;p<128;p++)CHECK(!rm_pages[c][p]);
    CHECK(rm_find(18,32766));CHECK(rm_find(18,32767));
    picture(rm_find(18,32767),rm_pose(rm_find(18,32767),10),after);
    CHECK(!memcmp(before,after,sizeof before));rm_reset();rm_reset();
    puts("remaster cache: 513 resident models after 4096 misses, alias identity, stable live poses/pixels, invalid keys and reset passed");
    return 0;
}
