/* Source-free geometry and native event fixtures; never game artwork. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../runtime/remaster_world.h"
uint32_t tp_map_w=64,tp_map_ext=64;
#define CHECK(x) do{if(!(x)){fprintf(stderr,"remaster world line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static uint8_t memory[0x600000],indexed[64*64];
static uint32_t pixels[64*64],palette[256];
static void word(size_t a,uint16_t v){memcpy(memory+a,&v,2);}
static RMRig*fixture(int cls,int type,int seats,unsigned frame){
    RMRig**slot=rm_cache_slot(cls,type);CHECK(slot);*slot=calloc(1,sizeof **slot);CHECK(*slot);
    FILE*f=tmpfile();CHECK(f);
    fprintf(f,"TPRIG1 3 1 1 1 %d\n-.35 0 0 0\n.35 0 0 0\n0 0 .7 0\n0 1 2 12abef\n%u\n",seats,frame);
    for(int i=0;i<=seats;i++)fprintf(f,"1 0 0 %g 0 1 0 0 0 0 1 %g\n",i?(i==1?-.5:.5):0.,i?.5:0.);
    rewind(f);CHECK(rm_read(f,*slot));fclose(f);(*slot)->cls=cls;(*slot)->type=type;return *slot;
}
static void clear(WMRender*r){memset(r->pixels,0,sizeof pixels);for(int i=0;i<64*64;i++)r->depth[i]=FLT_MAX;r->fragments=0;}
static void atomic_assemblies(void){
    float depth[64*64];WMRender r={.pixels=pixels,.depth=depth,.w=64,.h=64,.cy=-4,.cz=.7,
        .fy=1,.rx=1,.uz=1,.focal=64};
    fixture(4,0,2,10);fixture(2,0,0,65024);
    TPRMSceneObject c={.cls=4,.type=0,.frame=10,.rig_ok=1};memset(c.seats,255,40);
    for(int i=0;i<2;i++){c.seats[i]=i;c.riders[i]=(TPRMRider){65024,(uint8_t)i,1,0};}
    clear(&r);CHECK(!rm_world_model(&r,&c));CHECK(!r.fragments);
    for(int i=0;i<64*64;i++)CHECK(!pixels[i]&&depth[i]==FLT_MAX);
    fixture(2,1,0,65024);CHECK(rm_world_model(&r,&c));CHECK(r.fragments>100);
    clear(&r);c.figure_count=1;CHECK(!rm_world_model(&r,&c));CHECK(!r.fragments);c.figure_count=0;
    clear(&r);c.riders[1].pose=65025;CHECK(!rm_world_model(&r,&c));CHECK(!r.fragments);
    c.riders[1].pose=65024;c.riders[1].variant=7;CHECK(!rm_world_model(&r,&c));CHECK(!r.fragments);
    rm_reset();fixture(255,0,0,10);
    rm_bindings=calloc(2,sizeof *rm_bindings);CHECK(rm_bindings);rm_nbindings=2;rm_bindings_loaded=1;
    rm_bindings[0]=(RMBinding){18,10,255,0};rm_bindings[1]=(RMBinding){18,11,255,1};
    c=(TPRMSceneObject){.cls=18,.ncomponents=2};
    c.components[0]=(TPRMPlacedComponent){.frame=10,.x=-.5};
    c.components[1]=(TPRMPlacedComponent){.frame=11,.x=.5};
    clear(&r);CHECK(!rm_world_model(&r,&c));CHECK(!r.fragments);
    fixture(255,1,0,11);CHECK(rm_world_model(&r,&c));CHECK(r.fragments>100);
    clear(&r);c.components[1].z=NAN;CHECK(!rm_world_model(&r,&c));CHECK(!r.fragments);rm_reset();
}
static void plane(WMRender*r,float z,uint32_t colour){
    r->rgba=&colour;r->tw=r->th=1;
    wm_quad(r,wm_camera(r,.3,1,z,0,0),wm_camera(r,1.3,1,z,0,0),
              wm_camera(r,1.3,2,z,0,0),wm_camera(r,.3,2,z,0,0));r->rgba=NULL;
}
int main(void){
    atomic_assemblies();word(0x1d663a,24);word(0x1d6654,16);word(0x1de5f6,8);
    for(int i=0;i<256;i++)palette[i]=0xff000000u|i;
    NDTarget source={.buf=indexed,.size=sizeof indexed,.pitch=64,.clip_w=64,.clip_h=64,.clip_r=63};
    TPRemasterPaint paint={.target={pixels,64,64,64,0,0,64,64},.palette=palette,.density=1};
    RMWorld w={0};CHECK(rm_world_begin(&w,memory,sizeof memory,64,0,0,&source,&paint));
    memset(pixels,0,sizeof pixels);plane(&w.renderer,-.1,0xff010203);CHECK(!w.renderer.fragments);
    plane(&w.renderer,0,0xff010203);CHECK(w.renderer.fragments>100);
    uint32_t first[64*64];
    CHECK(rm_world_begin(&w,memory,sizeof memory,64,0,0,&source,&paint));memset(pixels,0,sizeof pixels);
    plane(&w.renderer,.1,0xff0000ff);plane(&w.renderer,.4,0xffff0000);memcpy(first,pixels,sizeof first);
    CHECK(rm_world_begin(&w,memory,sizeof memory,64,0,0,&source,&paint));memset(pixels,0,sizeof pixels);
    plane(&w.renderer,.4,0xffff0000);plane(&w.renderer,.1,0xff0000ff);CHECK(!memcmp(first,pixels,sizeof first));
    /* Native nested source draws preserve order and private indexed state.
     * The text buffer belongs to the callback, so retain a private copy. */
    uint32_t table=0x300000,data=0x310000;memcpy(memory+0x1d3bb8,&table,4);memcpy(memory+table+6,&data,4);
    memory[table+10]=memory[table+11]=2;
    const uint8_t sprite[]={2,41,42,0,2,43,44,0};memcpy(memory+data,sprite,sizeof sprite);
    memset(indexed,0,sizeof indexed);memset(pixels,0,sizeof pixels);
    CHECK(rm_world_begin(&w,memory,sizeof memory,64,0,0,&source,&paint));
    rm_world_event(&w,6,0,0,0,0,0,NULL);rm_world_event(&w,2,10,12,0x202,data,0,NULL);
    rm_world_event(&w,6,1,0,0,0,0,NULL);rm_world_event(&w,2,11,12,0x202,data,0,NULL);
    uint8_t label[]="PARK";rm_world_event(&w,4,-100,-100,7,0,0,label);label[0]='X';
    CHECK(!strcmp((const char*)w.events[w.nevents-1].text,"PARK"));
    rm_world_event(&w,12,1,0,0,0,0,NULL);CHECK(!w.source_objects);
    rm_world_event(&w,12,0,0,0,0,0,NULL);CHECK(rm_world_end(&w));CHECK(w.source_objects==2&&!w.models);
    CHECK(w.indexed[12*64+10]==41&&w.indexed[12*64+11]==41&&w.indexed[12*64+12]==42);
    CHECK(pixels[12*64+11]==palette[41]);for(unsigned i=0;i<sizeof indexed;i++)CHECK(!indexed[i]);
    CHECK(rm_world_begin(&w,memory,sizeof memory,64,0,0,&source,&paint));
    rm_world_event(&w,6,1,4,0,0,0,NULL);rm_world_event(&w,12,2,4,0,0,0,NULL);CHECK(!rm_world_end(&w)&&paint.failed);
    paint.failed=0;CHECK(rm_world_begin(&w,memory,sizeof memory,64,0,0,&source,&paint));
    for(int i=0;i<65;i++)rm_world_event(&w,6,i,4,0,0,0,NULL);CHECK(w.failed&&paint.failed);
    paint.failed=0;CHECK(rm_world_begin(&w,memory,sizeof memory,64,0,0,&source,&paint));
    rm_world_event(&w,6,1,4,0,0,0,NULL);rm_world_event(&w,4,0,0,0,0,0,(const uint8_t*)"1234567890123456");CHECK(w.failed);
    rm_world_release(&w);puts("remaster world: atomic occupants/tracks, terrain depth, draw order, nested replay, text lifetime and malformed scopes passed");return 0;
}
