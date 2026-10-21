/* Source-free normal-pass metadata checks. In particular, Visitor capture's
 * entrance and sign exceptions must not leak into the normal draw stream. */
#include "../runtime/native_draw.c"
#include "../runtime/native_scene.h"
#include "../runtime/remaster_scene.h"
uint32_t tp_map_w=64,tp_map_ext=64;
extern const uint8_t *tp_native_text;
extern int tp_native_stable_rng;
#define CHECK(x) do{if(!(x)){fprintf(stderr,"scene line %d: %s\n",__LINE__,#x);return 1;}}while(0)
typedef struct {int kind,a,b,c,e;uint32_t d;uint8_t text[7];} Event;
typedef struct {
    Event events[32768];int n,overflow,metadata,begins,ends,frames,depth,maxdepth,bad;
    int stack[64],classes[64],colour,signs;
    TPRMCapture objects[64];int captured_frames;
    TPRMProjection projection;int cell_x[64],cell_y[64],placements;
    NDTarget target;
} Capture;
static uint8_t *memory;
static void put16(unsigned p,unsigned v){memory[p]=v;memory[p+1]=v>>8;}
static void put32(unsigned p,unsigned v){put16(p,v);put16(p+2,v>>16);}
/* Normal passes use one camera origin; Visitor capture places each cell in a
 * scratch image. Both must resolve the same physical model and attachments. */
static int placement_matches(Capture*s,int level){
    TPRMCapture local=s->objects[level];TPRMProjection normal=s->projection;
    normal.cell_y=s->cell_y[level];TPRMSceneObject a,b;
    int ok=tp_remaster_scene_resolve(memory,64u<<20,tp_map_ext,s->stack[level],
                                    &s->objects[level],&normal,&a);
    float cell_x,cell_y;tp_rm_scene_position(&normal,s->cell_x[level],s->cell_y[level],&cell_x,&cell_y);
    TPRMProjection shifted=normal;shifted.world_x=cell_x;shifted.world_y=cell_y;
    shifted.origin_x=200;shifted.origin_y=320;shifted.cell_y=320<<normal.lo;
    int dx=(200<<normal.lo)-(int16_t)s->cell_x[level],dy=(320<<normal.lo)-(int16_t)s->cell_y[level];
    local.frame_x=(int16_t)(local.frame_x+dx);local.frame_y=(int16_t)(local.frame_y+dy);
    local.car_origin.x=(int16_t)(local.car_origin.x+dx);local.car_origin.y=(int16_t)(local.car_origin.y+dy);
    for(int i=0;i<local.ncomponents;i++){
        local.components[i].x=(int16_t)(local.components[i].x+dx);
        local.components[i].y=(int16_t)(local.components[i].y+dy);
    }
    if(ok!=tp_remaster_scene_resolve(memory,64u<<20,tp_map_ext,s->stack[level],&local,&shifted,&b) ||
       a.cls!=b.cls || a.type!=b.type || a.frame!=b.frame || a.rig_ok!=b.rig_ok || a.ncomponents!=b.ncomponents ||
       memcmp(a.seats,b.seats,sizeof a.seats) || memcmp(a.riders,b.riders,sizeof a.riders) ||
       memcmp(a.rider_ids,b.rider_ids,sizeof a.rider_ids) || memcmp(&a.vehicle,&b.vehicle,sizeof a.vehicle) ||
       a.figure_count!=b.figure_count || memcmp(&a.figure,&b.figure,sizeof a.figure))return 0;
    if(a.rig_ok && (fabsf(a.x-b.x)>3e-5f || fabsf(a.y-b.y)>3e-5f || fabsf(a.z-b.z)>3e-5f))return 0;
    for(unsigned i=0;i<a.ncomponents;i++){
        const TPRMPlacedComponent*p=&a.components[i],*q=&b.components[i];
        if(p->frame!=q->frame || fabsf(p->x-q->x)>3e-5f || fabsf(p->y-q->y)>3e-5f || p->z!=q->z)return 0;
    }
    s->placements+=ok;return 1;
}
static void log_call(int kind,int a,int b,int c,uint32_t d,int e,void *ctx){
    Capture*s=ctx;
    if(kind>=6){
        s->metadata++;
        if(kind==6){
            if(s->depth>=64){s->bad=1;return;}
            tp_remaster_capture_begin(&s->objects[s->depth],b);
            s->cell_x[s->depth]=c;s->cell_y[s->depth]=(int32_t)d;
            s->stack[s->depth]=a;s->classes[s->depth++]=b;s->begins++;
            if(s->depth>s->maxdepth)s->maxdepth=s->depth;
        }else if(kind==12){
            if(!s->depth || s->stack[s->depth-1]!=a || s->classes[s->depth-1]!=b)s->bad=1;
            else {
                if(!placement_matches(s,s->depth-1))s->bad=1;
                s->captured_frames+=s->objects[s->depth-1].frame_count;s->depth--;
            }
            s->ends++;
        }else{
            if(kind==8)s->frames++;
            if(!s->depth)s->bad=1;
            else tp_remaster_capture_event(&s->objects[s->depth-1],memory,64u<<20,kind,a,b,c,d,e);
        }
        return;
    }
    if(s->n==32768){s->overflow=1;return;}
    Event*q=&s->events[s->n++];*q=(Event){.kind=kind,.a=a,.b=b,.c=c,.e=e,.d=d};
    if(kind==5){s->colour=c;return;}
    if(kind==2){
        tp_native_draw_world_sprite(&s->target,memory,d,c&255,c>>8,a,b,e,s->colour,NULL);
        s->colour=0;
    }else if(kind==4){
        s->signs++;if(!tp_native_text){s->bad=1;return;}
        memcpy(q->text,tp_native_text,7);
        tp_native_draw_small_text(&s->target,memory,a,b,tp_native_text,c);
    }
}
static void reset(Capture*s,uint8_t*p,int lo){
    memset(s,0,sizeof *s);memset(p,17,640*480);
    s->target=(NDTarget){p,640*480,0,640,640,480,639,lo,NULL,0};
    int tw=tp_rm_scene_u16(memory,0x1d663a),th=tp_rm_scene_u16(memory,0x1d6654);
    int off=(int16_t)tp_rm_scene_u16(memory,0x1d6624);
    s->projection=(TPRMProjection){lo,tw>>lo,th>>lo,(tw/3)>>lo,lo?1.2:1,
        0,0,(640-tw*((640-off)/tw))>>lo,0,0};
}
int main(void){
    memory=calloc(64u<<20,1);uint8_t*before=malloc(64u<<20);
    Capture*a=calloc(1,sizeof *a),*b=calloc(1,sizeof *b);
    uint8_t*pa=malloc(640*480),*pb=malloc(640*480);CHECK(memory&&before&&a&&b&&pa&&pb);
    put32(0x1d3b08,0x300000);put32(0x1d3b10,0x400000);
    put32(0x1d3b80,0x500000);put32(0x1d3b7c,0x510000);
    put32(0x1d3bb8,0x520000);put32(0x1d3b38,0x540000);
    put16(0x1b44c1+2,7);put16(0x540000+14,3);
    put16(0x500000+3*8,1);put16(0x500000+3*8+6,3);
    put16(0x510000+10,6);put16(0x510000+12,-4);put16(0x510000+14,-8);
    put32(0x520006,0x530000);memory[0x52000a]=8;memory[0x52000b]=8;
    for(int y=0;y<8;y++){memory[0x530000+y*10]=8;memset(memory+0x530001+y*10,35+y,8);}
    /* Every cell's scenery also exercises static begin/end and clipping. */
    for(int y=0;y<64;y++)for(int x=0;x<64;x++)put16(0x300000+(y*64+x)*4,65535);
    unsigned gate=0x400000+200,sign=0x400000+400;
    memory[gate+4]=20;memory[gate+12]=6;memory[gate+7]=22;memory[gate+9]=10;
    put16(gate+10,0x13f);put16(0x300000+(10*64+22)*4,1);
    put16(0x540000+0x27a,3);put16(0x540000+0x27c,3);
    put16(0x540000+0x27e,3);put16(0x540000+0x440,3);
    put32(0x1d3818,0x550000);memory[0x550018]=1;
    put32(0x1d3814,0x560000);memcpy(memory+0x560020,"SYNTHETIC PARK",15);
    put32(0x1d3b64,0x570000);put32(0x570006,0x530000);
    memory[0x57000a]=8;memory[0x57000b]=8;
    for(int i=1;i<256;i++)memory[0x1b51c1+i]=1;
    memory[0x1d7772]=42;
    memory[sign+4]=6;memory[sign+7]=22;memory[sign+9]=11;put16(sign+10,3);put16(sign+0x16,3);
    put16(0x300000+(11*64+22)*4,2);put16(0x400000+600+0x16,0);
    memcpy(memory+0x1cced0,"TEST RIDE",10);memory[0x1cced0+0xc6]=23;
    memory[0x1d7773]=53;memory[0x1d3afc]=8;
    int cases=0,nested=0,signs=0;
    for(int mode=0;mode<2;mode++)for(int stable=0;stable<2;stable++)
      for(int orientation=0;orientation<2;orientation++)for(int cy=9;cy<=11;cy++){
        put16(0x1de5f6,mode?8:1);put16(0x1d663a,48);put16(0x1d6654,32);
        put16(gate+10,orientation?0x140:0x13f);tp_native_stable_rng=stable;
        int cx=orientation?22:11;memcpy(before,memory,64u<<20);
        reset(a,pa,!mode);reset(b,pb,!mode);
        b->projection.world_x=cx;b->projection.world_y=cy;
        tp_native_things(memory,cx,cy,8,log_call,a);
        tp_native_things_metadata(memory,cx,cy,8,log_call,b);
        CHECK(!a->metadata&&!a->bad&&!b->bad&&!a->overflow&&!b->overflow);
        CHECK(b->begins&&b->begins==b->ends&&!b->depth&&b->frames&&b->captured_frames==b->frames&&b->placements);
        CHECK(a->n==b->n&&!memcmp(a->events,b->events,(size_t)a->n*sizeof(Event)));
        CHECK(!memcmp(pa,pb,640*480));CHECK(!memcmp(before,memory,64u<<20));
        nested+=b->maxdepth>1;signs+=b->signs;cases++;
        /* Metadata and Visitor entry points cannot change a later normal pass. */
        reset(b,pb,!mode);tp_native_cell_things(memory,22,11,-100,-100,log_call,b);
        CHECK(b->signs==1&&b->begins==1&&!b->ends); /* Visitor's intentional exception */
        reset(b,pb,!mode);tp_native_things(memory,cx,cy,8,log_call,b);
        CHECK(!b->metadata&&a->n==b->n&&!memcmp(a->events,b->events,(size_t)a->n*sizeof(Event)));
        CHECK(!memcmp(pa,pb,640*480));
    }
    CHECK(nested&&signs);
    /* A normal sign at negative coordinates remains clipped, despite metadata. */
    reset(b,pb,0);tp_native_things_metadata(memory,35,11,1,log_call,b);
    CHECK(!b->signs&&!b->bad&&!b->depth);
    /* Shared capture rejects truncated memory and malformed/duplicate
     * attachments without hiding their error when the next part arrives. */
    TPRMCapture capture;tp_remaster_capture_begin(&capture,14);
    CHECK(tp_remaster_capture_event(&capture,NULL,0,11,3,0,0,0,0));CHECK(capture.car_error);
    tp_remaster_capture_begin(&capture,14);put32(0x1d3b78,0x400000+600);memory[0x400000+600+4]=18;
    CHECK(tp_remaster_capture_event(&capture,memory,64u<<20,11,3,2,0,0,0));
    CHECK(!capture.car_error&&capture.car_track==3&&capture.car_bounce==2);
    CHECK(tp_remaster_capture_event(&capture,memory,64u<<20,11,3,2,0,0,0));CHECK(capture.car_error);
    tp_remaster_capture_begin(&capture,4);
    CHECK(tp_remaster_capture_event(&capture,NULL,0,7,0,3,65024,0,128));
    CHECK(!capture.rig_rider_error&&capture.rig_riders[0].present&&!capture.rig_riders[0].visible);
    CHECK(tp_remaster_capture_event(&capture,NULL,0,7,0,3,65024,0,128));CHECK(capture.rig_rider_error);
    tp_remaster_capture_begin(&capture,4);
    CHECK(tp_remaster_capture_event(&capture,NULL,0,7,0,3,65536,1,128));CHECK(capture.rig_rider_error);
    tp_remaster_capture_begin(&capture,4);CHECK(!capture.rig_rider_error&&!capture.rig_riders[0].present);
    TPRMSceneObject placed;
    TPRMProjection projection={0,48,32,16,1,12,14,200,320,320};
    tp_remaster_capture_begin(&capture,-1);capture.frame_count=1;capture.drawn_frame=3;
    capture.frame_x=228;capture.frame_y=328;
    CHECK(tp_remaster_scene_resolve(memory,64u<<20,64,-1,&capture,&projection,&placed));
    CHECK(placed.cls==255&&placed.type==7&&placed.frame==3&&placed.x==12.5f&&placed.y==14.25f);
    CHECK(!tp_remaster_scene_resolve(memory,128,64,-1,&capture,&projection,&placed));CHECK(!placed.rig_ok&&!placed.ncomponents);
    projection.tws=0;CHECK(!tp_remaster_scene_resolve(memory,64u<<20,64,-1,&capture,&projection,&placed));
    projection.tws=48;projection.world_x=NAN;
    CHECK(!tp_remaster_scene_resolve(memory,64u<<20,64,-1,&capture,&projection,&placed));projection.world_x=12;
    capture.drawn_frame=65536;
    CHECK(!tp_remaster_scene_resolve(memory,64u<<20,64,-1,&capture,&projection,&placed));capture.drawn_frame=3;
    capture.ncomponents=TP_RM_ASSEMBLY_MAX+1;
    CHECK(!tp_remaster_scene_resolve(memory,64u<<20,64,-1,&capture,&projection,&placed));capture.ncomponents=0;
    CHECK(!tp_remaster_scene_resolve(memory,64u<<20,64,0,&capture,&projection,&placed));
    /* Opaque Flight Simulator frames have no source passenger markers. Both
     * projections must keep real identities inside the moving cabin, using
     * an explicit authored sitter rather than dropping occupied rides. */
    const unsigned enclosed_frames[]={1350,1351,175,1353,176,177};
    unsigned rec=0x1cced0,base=0x400000;int enclosed_cases=0;
    memset(memory+base+200,0,8*200);memory[base+204]=4;memory[base+212]=2;
    put16(rec+0xa6,5);memory[rec+0xb3]=5;memory[rec+0xa8]=80;
    put32(0x1d3b78,base+8*200);
    for(int mode=0;mode<2;mode++)for(int fi=0;fi<6;fi++)
      for(int count=0;count<=5;count++)for(int appearance=0;appearance<9;appearance++){
        tp_remaster_capture_begin(&capture,4);capture.frame_count=1;
        capture.drawn_frame=enclosed_frames[fi];capture.frame_x=228;capture.frame_y=328;
        put16(base+210,enclosed_frames[fi]);put16(base+200+0x47,count?2:0);
        for(int i=0;i<5;i++){
            unsigned person=base+(i+2)*200;memory[person+4]=2;
            memory[person+0x59]=appearance==8?i:appearance;
            put16(person+0x47,i+1<count?i+3:0);
        }
        projection.lo=mode;projection.aspect=mode?1.2:1;
        memcpy(before,memory,64u<<20);
        CHECK(tp_remaster_scene_resolve(memory,64u<<20,64,1,&capture,&projection,&placed));
        CHECK(placed.frame==enclosed_frames[fi]&&placed.type==0&&placed.rig_ok);
        for(int slot=0;slot<40;slot++){
            CHECK(!capture.rig_riders[slot].present); /* source metadata unchanged */
            if(slot<count){
                CHECK(placed.rider_ids[slot]==count+1-slot);
                CHECK(placed.seats[slot]==(appearance==8?count-1-slot:appearance));
                CHECK(placed.riders[slot].present&&!placed.riders[slot].visible);
                CHECK(placed.riders[slot].pose==65452);
            }else CHECK(placed.seats[slot]==255&&!placed.riders[slot].present);
        }
        CHECK(tp_remaster_riders_valid(5,placed.seats,placed.riders));
        CHECK(!memcmp(memory,before,64u<<20));enclosed_cases++;
    }
    CHECK(enclosed_cases==648);
    CHECK(!tp_remaster_enclosed_ride_key(1,1350));
    CHECK(!tp_remaster_enclosed_ride_key(0,1352));
    CHECK(!tp_remaster_car_pose_valid(65452,0));
    placed.riders[0].visible=1;CHECK(!tp_remaster_riders_valid(5,placed.seats,placed.riders));
    placed.riders[0].visible=0;CHECK(!tp_remaster_riders_valid(4,placed.seats,placed.riders));
    capture.rig_riders[0]=(TPRMRider){65024,0,1,1};
    CHECK(!tp_remaster_scene_resolve(memory,64u<<20,64,1,&capture,&projection,&placed));
    memset(capture.rig_riders,0,sizeof capture.rig_riders);put16(rec+0xa6,4);
    CHECK(!tp_remaster_scene_resolve(memory,64u<<20,64,1,&capture,&projection,&placed));
    put16(rec+0xa6,5);put16(base+200+0x16,1);
    CHECK(!tp_remaster_scene_resolve(memory,64u<<20,64,1,&capture,&projection,&placed));
    put16(base+200+0x16,0);memory[rec+0xb3]=7;
    CHECK(!tp_remaster_scene_resolve(memory,64u<<20,64,1,&capture,&projection,&placed));
    memory[rec+0xb3]=5;memory[base+400+0x59]=8;
    CHECK(!tp_remaster_scene_resolve(memory,64u<<20,64,1,&capture,&projection,&placed));
    memory[base+400+0x59]=0;put16(base+1200+0x47,7);memory[base+1404]=2;
    CHECK(!tp_remaster_scene_resolve(memory,64u<<20,64,1,&capture,&projection,&placed));
    /* Tree House: only its 23 unmarked interior keys synthesize the hidden
     * occupant attachment. Its lift/walking/lookout markers remain native. */
    int tree_cases=0;unsigned tree_rec=rec+200;
    put16(base+200+0x16,200);put16(tree_rec+0xa6,1);
    memory[tree_rec+0xb3]=5;memory[tree_rec+0xa8]=80;put16(base+400+0x47,0);
    for(int mode=0;mode<2;mode++)for(int frame=5618;frame<=5645;frame++){
        if(frame>5618 && frame<5624)continue;
        for(int occupied=0;occupied<2;occupied++)for(int variant=0;variant<8;variant++){
            tp_remaster_capture_begin(&capture,4);capture.frame_count=1;
            capture.drawn_frame=frame;capture.frame_x=228;capture.frame_y=328;
            put16(base+210,frame);put16(base+200+0x47,occupied?2:0);
            memory[base+400+0x59]=variant;projection.lo=mode;
            CHECK(tp_remaster_scene_resolve(memory,64u<<20,64,1,&capture,&projection,&placed));
            CHECK(placed.type==1&&placed.frame==frame);
            CHECK(placed.riders[0].present==occupied&&!placed.riders[0].visible);
            if(occupied)CHECK(placed.riders[0].pose==65453&&placed.seats[0]==variant&&placed.rider_ids[0]==2);
            else CHECK(placed.seats[0]==255&&!placed.rider_ids[0]);
            CHECK(tp_remaster_riders_valid(1,placed.seats,placed.riders));tree_cases++;
        }
    }
    CHECK(tree_cases==736&&!tp_remaster_car_pose_valid(65453,0));
    CHECK(!tp_remaster_tree_hidden_key(5619)&&!tp_remaster_tree_hidden_key(5623));
    CHECK(!tp_remaster_tree_hidden_key(294)&&!tp_remaster_tree_hidden_key(5646));
    put16(base+210,5619);capture.drawn_frame=5619;
    CHECK(tp_remaster_scene_resolve(memory,64u<<20,64,1,&capture,&projection,&placed));
    CHECK(!placed.riders[0].present&&!tp_remaster_riders_valid(1,placed.seats,placed.riders));
    put16(base+210,5618);capture.drawn_frame=5618;put16(tree_rec+0xa6,2);
    CHECK(!tp_remaster_scene_resolve(memory,64u<<20,64,1,&capture,&projection,&placed));
    put16(tree_rec+0xa6,1);put16(base+200+0x16,201);
    CHECK(!tp_remaster_scene_resolve(memory,64u<<20,64,1,&capture,&projection,&placed));
    free(memory);free(before);free(a);free(b);free(pa);free(pb);
    printf("remaster scene: %d hidden Tree House identity cases and visible-frame guards passed\n",tree_cases);
    printf("remaster scene: %d enclosed-cabin identity/pose cases and malformed cabin states passed\n",enclosed_cases);
    printf("remaster scene: %d unchanged native passes, %d nested entrance passes, shared world placement, malformed descriptors, sign bounds, scopes and read-only memory passed\n",cases,nested);
    return 0;
}
