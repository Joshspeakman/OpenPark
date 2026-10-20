/* Source-free modular-track fixtures. These shapes only test the renderer;
 * they are never installed as artwork. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
static uint8_t *memory;
uint8_t *tp_host_memory(void){return memory;}
void tp_host_palette_rgba(uint32_t *p){memset(p,0,256*4);}
uint32_t tp_hook_pcs[32],tp_map_w=64,tp_map_ext=64;
void tp_hooks_changed(void){}
#include "../runtime/walk.c"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"tracks line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static void put16(unsigned p,unsigned v){memory[p]=v;memory[p+1]=v>>8;}
static void put32(unsigned p,unsigned v){put16(p,v);put16(p+2,v>>16);}
static int capture(Snap *s,unsigned used){
    s->ncards=0;s->npix=0;s->ncomponents=used;
    memset(sa,0,sizeof sa);memset(sb,255,sizeof sb);
    Build b={.s=s,.g=&s->g,.cx=12,.cy=14,.rider_slot=-1};
    b.a=(NDTarget){sa,sizeof sa,0,SW_,SW_,SH_,SW_-1,s->g.lo,NULL,0};
    b.b=b.a;b.b.buf=sb;
    int missing=tp_native_cell_things(memory,12,14,OXS<<s->g.lo,OYS<<s->g.lo,card_log,&b);
    finish(&b);return !missing && s->ncards==1;
}
int main(void){
    memory=calloc(64u<<20,1);Snap *s=calloc(1,sizeof *s);CHECK(memory&&s);M=memory;
    const unsigned thing=0x400000+200,cell=0x300000+(14*64+12)*4,records=0x550000;
    put32(MAP_PTR,0x300000);put32(0x1d3b04,0x304000);put32(THINGS,0x400000);
    put32(THING_LAST,thing+200);put16(cell,1);memory[thing+4]=0x12;
    memory[thing+7]=12;memory[thing+9]=14;memory[thing+12]=2;
    put32(0x1d3b80,0x500000);put32(0x1d3b7c,0x510000);
    put32(0x1d3bb8,0x520000);put32(0x1d3b38,0x540000);
    put16(0x540000+14,3);put16(0x540000+16,4);
    put16(0x540000+0x3aa,3);put16(0x540000+0x562,3);
    for(int family=1;family<=2;family++)put32(0x1b4399+family*10,records);
    for(int shape=0;shape<24;shape++){put16(records+shape*25,7);put16(records+shape*25+2,8);}
    for(int frame=3;frame<=6;frame++){
        unsigned part=frame-2;
        put16(0x500000+frame*8,part);put16(0x500000+frame*8+6,frame==5?6:frame);
        put16(0x510000+part*10,part*6);
        put16(0x510000+part*10+2,-4*part);put16(0x510000+part*10+4,-8*part);
        put32(0x520000+part*6,0x530000+part*128);
        memory[0x520000+part*6+4]=8;memory[0x520000+part*6+5]=8;
        for(int y=0;y<8;y++){
            unsigned p=0x530000+part*128+y*10;memory[p]=8;
            memset(memory+p+1,40+frame,8);memory[p+9]=0;
        }
    }
    unsigned cases=0;const int levels[]={0,1,2,7,31,255};
    for(int mode=0;mode<2;mode++){
        put16(SCREEN_MODE,mode?8:1);put16(TILE_W,mode?24:48);put16(TILE_H,mode?16:32);
        CHECK(geom(&s->g));
        for(int family=1;family<=3;family++)for(int shape=0;shape<16;shape++)
        for(unsigned l=0;l<sizeof levels/sizeof *levels;l++)for(int terrain=0;terrain<3;terrain++)
        for(int tick=0;tick<2;tick++){
            int level=levels[l];put16(thing+0x16,family);memory[thing+0x44]=shape;
            memory[thing+0x79]=level;put16(thing+10,5);memory[0x1d3afc]=tick;
            put16(0x1b44d9,terrain==0?0:terrain==1?0x400:1);
            CHECK(capture(s,0));Card *c=&s->cards[0];
            if(family!=3 && (shape==12||shape==15) && !level){
                CHECK(!c->ncomponents && !s->ncomponents);continue;
            }
            unsigned stacks=level-((shape==12||shape==15)?1:0);
            unsigned count=shape?(family==3?(terrain==2?1:2):stacks+2):1;
            CHECK(c->ncomponents==count && s->ncomponents==count && !c->rig_ok);
            for(unsigned i=0;i<count;i++){
                TPRMPlacedComponent *p=&s->components[i];
                CHECK(fabsf(p->x-(12+1.0f/3))<1e-6f && p->y==14.5f);
                unsigned expected;float z;
                if(!shape){expected=family==3?3:5;z=0;}
                else if(family==3){expected=(terrain!=2&&i==0)?3:5;z=expected==3?0:1.2f;}
                else if(i==0){expected=terrain==2?4:3;z=terrain==2?(mode?-.6f:-.3f):0;}
                else if(i==count-1){expected=tick?5:6;z=(level+1)*.6f;}
                else{expected=4;z=(i-1)*.6f;}
                CHECK(p->frame==expected && fabsf(p->z-z)<.00002f);
            }
            CHECK((uint16_t)r16(thing+10)==5);cases++;
        }
    }
    /* Bounded pools reject whole assemblies; no truncated towers. */
    put16(thing+0x16,1);memory[thing+0x44]=1;memory[thing+0x79]=2;
    CHECK(capture(s,TP_RM_COMPONENT_POOL-4));CHECK(s->cards[0].ncomponents==4);
    CHECK(capture(s,TP_RM_COMPONENT_POOL-3));CHECK(!s->cards[0].ncomponents);
    CHECK(s->ncomponents==TP_RM_COMPONENT_POOL-3);
    for(int state=0x13;state<=0x15;state+=2){
        memory[thing+12]=state;CHECK(capture(s,0));CHECK(!s->cards[0].ncomponents);
    }
    memory[thing+12]=2;put16(thing+10,0);CHECK(capture(s,0));
    CHECK(s->cards[0].ncomponents==3); /* native intentionally omits the cap */
    put16(0x1b44d9,0);put16(thing+10,5);put16(0x540000+14,0);CHECK(capture(s,0));
    CHECK(!s->cards[0].ncomponents);put16(0x540000+14,3);
    put16(0x1d667cu,8);put16(0x1d6642,2);CHECK(capture(s,0));
    CHECK(!s->cards[0].ncomponents);put16(0x1d667cu,0);
    /* A new thing clears overflow and component state, including after a
     * fully clipped prior thing whose finish() has nothing to save. */
    Build b={.s=s,.g=&s->g};card_log(6,1,0x12,0,0,0,&b);
    for(int i=0;i<=TP_RM_ASSEMBLY_MAX;i++)card_log(9,3,0,0,0,1,&b);
    CHECK(b.capture.component_error && b.capture.ncomponents==TP_RM_ASSEMBLY_MAX);
    card_log(6,2,0x12,0,0,0,&b);CHECK(!b.capture.component_error && !b.capture.ncomponents);
    card_log(9,3,0,0,(uint32_t)-25,1,&b);CHECK(b.capture.component_error);
    /* Resolver tests use synthetic triangle fixtures directly in the private
     * cache. A missing LAST component must leave colour and depth untouched. */
    rm_bindings=calloc(2,sizeof *rm_bindings);CHECK(rm_bindings);
    rm_bindings[0]=(RMBinding){18,3,18,1};rm_bindings[1]=(RMBinding){18,4,18,1};
    rm_nbindings=2;rm_bindings_loaded=1;RMRig **slot=rm_cache_slot(18,1);CHECK(slot);
    RMRig *model=*slot=calloc(1,sizeof *model);CHECK(model);
    FILE *f=tmpfile();CHECK(f);
    fputs("TPRIG1 3 1 1 1 0\n-1 0 0 0\n1 0 0 0\n0 0 2 0\n0 1 2 ff0000\n"
          "3\n1 0 0 0 0 1 0 0 0 0 1 0\n",f);rewind(f);
    CHECK(rm_read(f,model));fclose(f);model->cls=18;model->type=1;
    uint32_t pixels[64*64];float depth[64*64];
    for(int i=0;i<64*64;i++){pixels[i]=0xff123456;depth[i]=1e9;}
    WMRender mr={.pixels=pixels,.depth=depth,.w=64,.h=64,.cy=-4,.cz=1,
        .fy=1,.rx=1,.uz=1,.focal=32};
    Card c={.cls=18,.ncomponents=2};s->ncomponents=2;
    s->components[0]=(TPRMPlacedComponent){3,0,0,0};
    s->components[1]=(TPRMPlacedComponent){4,0,0,.6f};
    CHECK(!draw_track_assembly(s,&c,&mr,1)); /* alias exists, pose missing */
    for(int i=0;i<64*64;i++)CHECK(pixels[i]==0xff123456 && depth[i]==1e9f);
    s->components[1].frame=5;CHECK(!draw_track_assembly(s,&c,&mr,1)); /* alias missing */
    for(int i=0;i<64*64;i++)CHECK(pixels[i]==0xff123456 && depth[i]==1e9f);
    s->components[1].frame=3;model->ns=1;
    CHECK(!draw_track_assembly(s,&c,&mr,1));model->ns=0;
    CHECK(draw_track_assembly(s,&c,&mr,1));int changed=0;
    for(int i=0;i<64*64;i++)if(pixels[i]!=0xff123456)changed++;
    CHECK(changed>0);c.component_first=2;CHECK(!draw_track_assembly(s,&c,&mr,1));
    rm_reset();free(s);free(memory);
    printf("remaster tracks: %u native assemblies, both modes, height/crop/tick mapping, limits, train guards and atomic drawing passed\n",cases);
    return 0;
}
