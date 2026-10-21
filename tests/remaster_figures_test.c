/* Source-free kind-8 decisions: native selection, terminal suppression and
 * actual person identity must survive both normal and Visitor capture. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
static uint8_t *memory;
uint8_t *tp_host_memory(void){return memory;}
void tp_host_palette_rgba(uint32_t*p){memset(p,0,256*4);}
uint32_t tp_hook_pcs[32],tp_map_w=64,tp_map_ext=64;
void tp_hooks_changed(void){}
#include "../runtime/walk.c"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"figures line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static void put16(unsigned p,unsigned v){memory[p]=v;memory[p+1]=v>>8;}
static void put32(unsigned p,unsigned v){put16(p,v);put16(p+2,v>>16);}
typedef struct {int kind,a,b,c,e;uint32_t d;} Event;
typedef struct {
    Event events[64];int n,depth,bad,figures;
    TPRMCapture capture;TPRMSceneObject object;NDTarget target;
} Pass;
static void normal_log(int kind,int a,int b,int c,uint32_t d,int e,void*ctx){
    Pass*p=ctx;
    if(kind==6){if(p->depth++)p->bad=1;tp_remaster_capture_begin(&p->capture,b);return;}
    if(kind==12){
        TPRMProjection g={0,48,32,16,1,8,12,0,0,0};
        if(--p->depth)p->bad=1;
        tp_remaster_scene_resolve(memory,64u<<20,64,a,&p->capture,&g,&p->object);return;
    }
    if(kind>=6){
        if(!p->depth)p->bad=1;
        if(kind==13)p->figures++;
        if(!tp_remaster_capture_event(&p->capture,memory,64u<<20,kind,a,b,c,d,e))p->bad=1;
        return;
    }
    if(p->n>=64){p->bad=1;return;}
    p->events[p->n++]=(Event){kind,a,b,c,e,d};
    if(kind==2)tp_native_draw_world_sprite(&p->target,memory,d,c&255,c>>8,a,b,e,0,NULL);
}
static void reset_pass(Pass*p,uint8_t*pixels,int lo){
    memset(p,0,sizeof *p);memset(pixels,17,640*480);
    p->target=(NDTarget){pixels,640*480,0,640,640,480,639,lo,NULL,0};
}
static int visitor(Snap*s){
    s->ncards=s->npix=0;memset(sa,0,sizeof sa);memset(sb,255,sizeof sb);
    Build b={.s=s,.g=&s->g,.cx=12,.cy=14,.rider_slot=-1};
    b.a=(NDTarget){sa,sizeof sa,0,SW_,SW_,SH_,SW_-1,s->g.lo,NULL,0};b.b=b.a;b.b.buf=sb;
    int missing=tp_native_cell_things(memory,12,14,OXS<<s->g.lo,OYS<<s->g.lo,card_log,&b);
    finish(&b);return !missing && s->ncards==1;
}
int main(void){
    memory=calloc(64u<<20,1);uint8_t*before=malloc(64u<<20),*pa=malloc(640*480),*pb=malloc(640*480);
    Snap*s=calloc(1,sizeof *s);CHECK(memory&&before&&pa&&pb&&s);M=memory;
    unsigned things=0x400000,t=things+200,rec=0x1cced0+800;
    put32(MAP_PTR,0x300000);put32(THINGS,things);put32(0x1d3b78,things+1200*200);
    put32(0x1d3b80,0x500000);put32(0x1d3b7c,0x510000);put32(0x1d3bb8,0x520000);put32(0x1d3b38,0x540000);
    put16(0x300000+(14*64+12)*4,1);memory[t+4]=4;memory[t+12]=2;
    memory[t+6]=memory[t+8]=128;memory[t+7]=12;memory[t+9]=14;put16(t+10,1486);put16(t+0x16,800);
    put16(rec+0xa6,16);memory[rec+0xb3]=8;memory[rec+0xa8]=80;
    put16(0x500000+1486*8,1);
    for(int k=1;k<=2;k++){
        unsigned e=0x510000+k*10;put16(e,k*6);put16(e+2,-8);put16(e+4,k==1?-16:-32);
        put32(0x520000+k*6,0x530000+k*64);memory[0x520000+k*6+4]=8;memory[0x520000+k*6+5]=8;
        for(int y=0;y<4;y++){unsigned at=0x530000+k*64+y*6;memory[at]=4;memset(memory+at+1,40+k,4);}
    }
    for(int family=0;family<17;family++){
        int base=family<8?100+family*5:200+(family-8)*5;
        for(int step=0;step<5;step++){
            unsigned f=0x500000+(base+step)*8;put16(f,2);put16(f+6,base+(step+1)%5);memory[f+5]=step==4;
        }
        if(family<8){put16(0x1b4a0e + family*0x24,20+family);put16(0x540000+(20+family)*2,base);}
        else{
            int alt=family-8;put16(0x1b4e5a+alt*6,40+alt);put16(0x540000+(40+alt)*2,base);
            put16(0x1b4e5c+alt*6,alt*13-42);put16(0x1b4e5e + alt*6,27-alt*7);
        }
    }
    Pass a,b;int cases=0,terminal=0,random_cases=0,alternates=0,guest_choices=0;
    for(int mode=0;mode<2;mode++)for(int stable=0;stable<2;stable++){
        put16(SCREEN_MODE,mode?8:1);put16(TILE_W,48);put16(TILE_H,32);CHECK(geom(&s->g));tp_native_stable_rng=stable;
        for(int variant=0;variant<8;variant++)for(int choice=0;choice<10;choice++)
          for(int step=0;step<4;step++)for(int tick=0;tick<2;tick++){
            int n=step%3==0?16:step%3,person=n+1,selector=choice?-person:person;
            memset(memory+things+400,0,16*200);put16(t+0x47,2);
            for(int i=0;i<n;i++){unsigned r=things+(i+2)*200;memory[r+4]=2;memory[r+0x59]=variant;put16(r+0x47,i+1<n?i+3:0);}
            int base=choice?200+(choice-1)*5:100+variant*5;
            put16(things+person*200+10,base+step);put16(t+0x4d,selector);put16(0x1d7be8,choice?choice-1:0);memory[0x1d3afc]=tick;
            memcpy(before,memory,64u<<20);reset_pass(&a,pa,!mode);reset_pass(&b,pb,!mode);
            tp_native_things(memory,8,12,8,normal_log,&a);tp_native_things_metadata(memory,8,12,8,normal_log,&b);
            CHECK(!a.bad&&!b.bad&&!a.depth&&!b.depth&&b.figures==1);
            CHECK(a.n==b.n&&!memcmp(a.events,b.events,a.n*sizeof(Event))&&!memcmp(pa,pb,640*480));
            CHECK(visitor(s));Card*c=&s->cards[0];const TPRMFigure*f=&c->figure.draw;
            CHECK(!c->rig_ok&&!b.object.rig_ok&&c->figure_count==1&&c->figure.valid);
            CHECK(c->figure.person==person&&c->figure.variant==variant);
            CHECK(c->figure.occupants==n&&!memcmp(c->seats,b.object.seats,sizeof c->seats)&&!memcmp(c->rider_ids,b.object.rider_ids,sizeof c->rider_ids));
            for(int i=0;i<40;i++)CHECK(c->seats[i]==(i<n?variant:255)&&c->rider_ids[i]==(i<n?i+2:0));
            CHECK(!memcmp(&c->figure,&b.object.figure,sizeof c->figure));
            CHECK(f->selector==selector&&f->frame==base+step+(tick==0)&&f->alternative==choice-1);
            CHECK(f->x==(choice?(choice-1)*13-42:0)&&f->y==(choice?27-(choice-1)*7:0));
            CHECK(f->visible==!(step==3&&tick==0));terminal+=!f->visible;
            CHECK(!memcmp(before,memory,64u<<20));cases++;
        }
        /* Initial native random selection must be observed, never rerolled by
         * the second view. Both passes begin from the same simulation seed. */
        for(int seed=0;seed<64;seed++){
            memset(memory+things+400,0,16*200);put16(t+0x47,2);put16(t+0x4d,0);
            memory[things+404]=2;memory[things+400+0x59]=seed%8;put32(0x1d3af4,seed);memory[0x1d3afc]=seed&1;
            memcpy(before,memory,64u<<20);reset_pass(&a,pa,!mode);reset_pass(&b,pb,!mode);
            tp_native_things(memory,8,12,8,normal_log,&a);tp_native_things_metadata(memory,8,12,8,normal_log,&b);
            CHECK(!a.bad&&!b.bad&&a.n==b.n&&!memcmp(a.events,b.events,a.n*sizeof(Event))&&!memcmp(pa,pb,640*480));
            CHECK(visitor(s));Card*c=&s->cards[0];CHECK(c->figure.valid&&c->figure.person==2&&c->figure.variant==seed%8);
            CHECK(c->figure.occupants==1&&c->rider_ids[0]==2&&c->seats[0]==seed%8);
            CHECK(!memcmp(&c->figure,&b.object.figure,sizeof c->figure));
            if(c->figure.draw.selector>0)guest_choices++;else alternates|=1<<c->figure.draw.alternative;
            CHECK(!memcmp(before,memory,64u<<20));random_cases++;
        }
        put16(t+0x47,0);reset_pass(&b,pb,!mode);tp_native_things_metadata(memory,8,12,8,normal_log,&b);
        CHECK(!b.bad&&!b.figures&&visitor(s)&&!s->cards[0].figure_count&&!s->cards[0].figure.valid);
    }
    CHECK(guest_choices&&alternates==511&&terminal==320);
    /* A preceding Maze may choose a new alternate. Stable capture must not
     * leak that choice into a different Maze's remembered negative selector. */
    int multiple_cases=0;unsigned other=things+18*200;
    memset(memory+things+400,0,20*200);put16(t+0x47,2);put16(t+0x4d,-2);
    memory[things+404]=2;memory[things+400+0x59]=3;
    put16(0x300000+(14*64+13)*4,18);memory[other+4]=4;memory[other+12]=2;
    memory[other+6]=memory[other+8]=128;memory[other+7]=13;memory[other+9]=14;
    put16(other+10,1486);put16(other+0x16,800);put16(other+0x47,19);memory[things+19*200+4]=2;
    tp_native_stable_rng=1;memory[0x1d3afc]=1;
    for(int mode=0;mode<2;mode++)for(int alt=0;alt<9;alt++)for(int seed=1;seed<=64;seed++){
        put16(SCREEN_MODE,mode?8:1);CHECK(geom(&s->g));put16(0x1d7be8,alt);put32(0x1d3af4,seed);
        put16(things+400+10,200+alt*5+1);memcpy(before,memory,64u<<20);
        reset_pass(&a,pa,!mode);reset_pass(&b,pb,!mode);
        tp_native_things(memory,8,12,8,normal_log,&a);tp_native_things_metadata(memory,8,12,8,normal_log,&b);
        CHECK(!a.bad&&!b.bad&&b.figures==2&&b.object.idx==1);
        CHECK(a.n==b.n&&!memcmp(a.events,b.events,a.n*sizeof(Event))&&!memcmp(pa,pb,640*480));
        CHECK(visitor(s)&&s->cards[0].figure.valid&&s->cards[0].figure.person==2);
        CHECK(!memcmp(&s->cards[0].figure,&b.object.figure,sizeof b.object.figure));
        CHECK(b.object.figure.draw.alternative==alt&&b.object.figure.draw.x==alt*13-42&&b.object.figure.draw.y==27-alt*7);
        CHECK(!memcmp(before,memory,64u<<20));multiple_cases++;
    }
    put16(0x300000+(14*64+13)*4,0);
    /* Malformed capture and ownership never become eligible geometry. */
    TPRMCapture cap;TPRMSceneObject object;TPRMProjection g={0,48,32,16,1,8,12,0,0,0};
    put16(t+0x47,2);put16(t+0x4d,2);memory[things+404]=2;memory[things+400+0x59]=3;
    tp_remaster_capture_begin(&cap,4);cap.frame_count=1;cap.drawn_frame=1486;
    CHECK(tp_remaster_capture_event(&cap,memory,64u<<20,13,2,115,0,0,0));
    CHECK(!tp_remaster_scene_resolve(memory,64u<<20,64,1,&cap,&g,&object)&&object.figure.valid);
    put16(rec+0xa6,15);CHECK(!tp_remaster_scene_resolve(memory,64u<<20,64,1,&cap,&g,&object)&&!object.figure.valid);put16(rec+0xa6,16);
    put16(t+0x16,801);CHECK(!tp_remaster_scene_resolve(memory,64u<<20,64,1,&cap,&g,&object)&&!object.figure.valid);put16(t+0x16,800);
    put16(t+0x2e,80);CHECK(!tp_remaster_scene_resolve(memory,64u<<20,64,1,&cap,&g,&object)&&!object.figure.valid);put16(t+0x2e,0);
    cap.rig_riders[0]=(TPRMRider){65024,3,1,1};CHECK(!tp_remaster_scene_resolve(memory,64u<<20,64,1,&cap,&g,&object)&&!object.figure.valid);memset(cap.rig_riders,0,sizeof cap.rig_riders);
    put16(t+0x4d,3);CHECK(!tp_remaster_scene_resolve(memory,64u<<20,64,1,&cap,&g,&object)&&!object.figure.valid);put16(t+0x4d,2);
    put16(t+0x47,3);memory[things+604]=2;CHECK(!tp_remaster_scene_resolve(memory,64u<<20,64,1,&cap,&g,&object)&&!object.figure.valid);put16(t+0x47,2);
    memory[things+400+0x59]=8;CHECK(!tp_remaster_scene_resolve(memory,64u<<20,64,1,&cap,&g,&object)&&!object.figure.valid);memory[things+400+0x59]=3;
    put16(things+400+0x47,3);memory[things+600+0x59]=8;
    CHECK(!tp_remaster_scene_resolve(memory,64u<<20,64,1,&cap,&g,&object)&&!object.figure.valid&&!object.figure.occupants);
    put16(things+400+0x47,0);memory[things+600+0x59]=0;
    CHECK(tp_remaster_capture_event(&cap,memory,64u<<20,13,2,115,0,0,0)&&cap.figure_error);
    CHECK(!tp_remaster_scene_resolve(memory,64u<<20,64,1,&cap,&g,&object)&&!object.figure.valid);
    int malformed[][5]={{0,1,0,0,0},{16384,1,0,0,0},{-16384,1,0,0,1},{2,-1,0,0,0},{2,65536,0,0,0},
        {2,1,32768,0,0},{2,1,0,-32769,0},{2,1,0,0,1},{-2,1,0,0,0},{-2,1,0,0,10},{2,1,0,0,32},{2,1,0,0,-1}};
    for(unsigned i=0;i<sizeof malformed/sizeof *malformed;i++){
        int*v=malformed[i];tp_remaster_capture_begin(&cap,4);
        CHECK(tp_remaster_capture_event(&cap,NULL,0,13,v[0],v[1],v[2],(uint32_t)v[3],v[4])&&cap.figure_error);
    }
    tp_remaster_capture_begin(&cap,24);cap.frame_count=1;cap.drawn_frame=1486;
    CHECK(tp_remaster_capture_event(&cap,NULL,0,13,2,115,0,0,0)&&cap.figure_error);
    memory[t+4]=24;CHECK(!tp_remaster_scene_resolve(memory,64u<<20,64,1,&cap,&g,&object));
    printf("remaster figures: %d remembered cases, %d suppressed endings, %d random choices, all nine alternate placements; both views, source pixels, memory and malformed ownership passed\n",cases,terminal,random_cases);
    printf("remaster figures: %d multi-Maze stable selections preserve per-thing offsets in both views\n",multiple_cases);
    free(memory);free(before);free(pa);free(pb);free(s);return 0;
}
