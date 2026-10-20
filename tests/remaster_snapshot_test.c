/* Source-free scene fixtures: frame selection and model anchors must come from
 * the native draw call, not an animation guess or a cropped picture's bottom. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
static uint8_t *memory;
uint8_t *tp_host_memory(void){return memory;}
void tp_host_palette_rgba(uint32_t*p){memset(p,0,256*4);}
uint32_t tp_hook_pcs[32],tp_map_w=64,tp_map_ext=64;
void tp_hooks_changed(void){}
#include "../runtime/walk.c"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"snapshot line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static void put16(unsigned p,unsigned v){memory[p]=v;memory[p+1]=v>>8;}
static void put32(unsigned p,unsigned v){put16(p,v);put16(p+2,v>>16);}
static int capture(Snap*s,int idx){
    s->ncards=0;s->npix=0;
    memset(sa,0,sizeof sa);memset(sb,255,sizeof sb);
    Build b={.s=s,.g=&s->g,.cx=12,.cy=14,.rider_slot=-1};
    b.a=(NDTarget){sa,sizeof sa,0,SW_,SW_,SH_,SW_-1,s->g.lo,NULL,0};
    b.b=b.a;b.b.buf=sb;
    put16(0x300000+(14*64+12)*4,idx);
    int missing=tp_native_cell_things(memory,12,14,OXS<<s->g.lo,OYS<<s->g.lo,card_log,&b);
    finish(&b);return !missing && s->ncards==1;
}
int main(void){
    memory=calloc(64u<<20,1);Snap*s=calloc(1,sizeof*s);CHECK(memory&&s);M=memory;
    put16(TILE_W,48);put16(TILE_H,32);put16(SCREEN_MODE,1);
    put32(MAP_PTR,0x300000);put32(THINGS,0x400000);
    put32(0x1d3b80,0x500000);put32(0x1d3b7c,0x510000);
    put32(0x1d3bb8,0x520000);put32(0x1d3b38,0x540000);
    put16(0x1b44c1+2,7);put16(0x540000+14,3);
    /* Two deliberately different sprite crops share one native origin. */
    put16(0x500000+3*8,1);put16(0x500000+3*8+6,4);
    put16(0x500000+4*8,2);put16(0x500000+4*8+6,3);
    for(int k=1;k<=2;k++){
        put16(0x510000+k*10,k*6);
        put16(0x510000+k*10+2,k==1?-8:-20);
        put16(0x510000+k*10+4,k==1?-24:-40);
        put32(0x520000+k*6,0x530000+k*64);
        memory[0x520000+k*6+4]=8;memory[0x520000+k*6+5]=8;
        for(int y=0;y<4;y++){
            unsigned p=0x530000+k*64+y*6;memory[p]=4;
            memset(memory+p+1,40+k,4);memory[p+5]=0;
        }
    }
    CHECK(geom(&s->g));CHECK(capture(s,-1));Card c=s->cards[0];
    CHECK(c.rig_ok && c.cls==255 && c.type==7 && c.frame==3);
    CHECK(fabsf(c.rig_x-12.5f)<1e-6f && fabsf(c.rig_y-14.5f)<1e-6f);
    put16(0x540000+14,4);CHECK(capture(s,-1));
    CHECK(s->cards[0].frame==4 && s->cards[0].rig_x==c.rig_x && s->cards[0].rig_y==c.rig_y);
    unsigned thing=0x400000+200;memory[thing+4]=24;put16(thing+10,3);
    memory[0x1d3afc]=1;CHECK(capture(s,1));c=s->cards[0];
    CHECK(c.rig_ok && c.frame==3);
    CHECK(fabsf(c.rig_x-(12+11.5f/24))<1e-6f && c.rig_y==14.9375f);
    memory[0x1d3afc]=16;CHECK(capture(s,1));
    CHECK(s->cards[0].frame==4); /* advance-before-draw, exactly like native */
    CHECK(s->cards[0].rig_x==c.rig_x && s->cards[0].rig_y==c.rig_y);
    CHECK(memory[thing+10]==3); /* snapshot never advances game state */
    free(s);free(memory);
    puts("remaster snapshot: static/animated native origins, crop independence and actual tick-selected frames passed");
    return 0;
}
