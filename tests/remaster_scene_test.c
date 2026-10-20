/* Source-free normal-pass metadata checks. In particular, Visitor capture's
 * entrance and sign exceptions must not leak into the normal draw stream. */
#include "../runtime/native_draw.c"
#include "../runtime/native_scene.h"
#include "../runtime/remaster_capture.h"
uint32_t tp_map_w=64,tp_map_ext=64;
extern const uint8_t *tp_native_text;
extern int tp_native_stable_rng;
#define CHECK(x) do{if(!(x)){fprintf(stderr,"scene line %d: %s\n",__LINE__,#x);return 1;}}while(0)
typedef struct {int kind,a,b,c,e;uint32_t d;uint8_t text[7];} Event;
typedef struct {
    Event events[32768];int n,overflow,metadata,begins,ends,frames,depth,maxdepth,bad;
    int stack[64],classes[64],colour,signs;
    TPRMCapture objects[64];int captured_frames;
    NDTarget target;
} Capture;
static uint8_t *memory;
static void put16(unsigned p,unsigned v){memory[p]=v;memory[p+1]=v>>8;}
static void put32(unsigned p,unsigned v){put16(p,v);put16(p+2,v>>16);}
static void log_call(int kind,int a,int b,int c,uint32_t d,int e,void *ctx){
    Capture*s=ctx;
    if(kind>=6){
        s->metadata++;
        if(kind==6){
            if(s->depth>=64){s->bad=1;return;}
            tp_remaster_capture_begin(&s->objects[s->depth],b);
            s->stack[s->depth]=a;s->classes[s->depth++]=b;s->begins++;
            if(s->depth>s->maxdepth)s->maxdepth=s->depth;
        }else if(kind==12){
            if(!s->depth || s->stack[s->depth-1]!=a || s->classes[s->depth-1]!=b)s->bad=1;
            else {s->captured_frames+=s->objects[s->depth-1].frame_count;s->depth--;}
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
        tp_native_things(memory,cx,cy,8,log_call,a);
        tp_native_things_metadata(memory,cx,cy,8,log_call,b);
        CHECK(!a->metadata&&!a->bad&&!b->bad&&!a->overflow&&!b->overflow);
        CHECK(b->begins&&b->begins==b->ends&&!b->depth&&b->frames&&b->captured_frames==b->frames);
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
    free(memory);free(before);free(a);free(b);free(pa);free(pb);
    printf("remaster scene: %d unchanged native passes, %d nested entrance passes, sign bounds, balanced objects, pixels and read-only memory passed\n",cases,nested);
    return 0;
}
