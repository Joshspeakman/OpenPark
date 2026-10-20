/* Source-free native vehicle selection, height and actual-rider fixtures. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
static uint8_t *memory;
uint8_t *tp_host_memory(void){return memory;}
void tp_host_palette_rgba(uint32_t *p){memset(p,0,256*4);}
uint32_t tp_hook_pcs[32],tp_map_w=64,tp_map_ext=64;
void tp_hooks_changed(void){}
#include "../runtime/walk.c"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"cars line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static void put16(unsigned p,unsigned v){memory[p]=v;memory[p+1]=v>>8;}
static void put32(unsigned p,unsigned v){put16(p,v);put16(p+2,v>>16);}
static int capture(Snap *s,int expected_missing){
    s->ncards=0;s->npix=0;s->ncomponents=0;
    memset(sa,0,sizeof sa);memset(sb,255,sizeof sb);
    Build b={.s=s,.g=&s->g,.cx=12,.cy=14,.rider_slot=-1};
    b.a=(NDTarget){sa,sizeof sa,0,SW_,SW_,SH_,SW_-1,s->g.lo,NULL,0};
    b.b=b.a;b.b.buf=sb;
    int missing=tp_native_cell_things(memory,12,14,OXS<<s->g.lo,OYS<<s->g.lo,card_log,&b);
    finish(&b);return missing==expected_missing && s->ncards==1;
}
int main(void){
    memory=calloc(64u<<20,1);Snap *s=calloc(1,sizeof *s);CHECK(memory&&s);M=memory;
    const unsigned car=0x400000+200,track=car+200,guest=car+400,records=0x550000;
    put32(MAP_PTR,0x300000);put32(0x1d3b04,0x304000);put32(THINGS,0x400000);
    put32(THING_LAST,guest+200);put16(0x300000+(14*64+12)*4,1);put16(car+2,2);
    memory[car+4]=14;memory[car+7]=12;memory[car+9]=14;memory[car+12]=8;
    put16(car+10,999);memory[car+6]=memory[car+8]=128;
    memory[track+4]=18;memory[track+7]=12;memory[track+9]=14;memory[track+12]=2;
    memory[track+0x44]=1;put16(track+22,2);memory[guest+4]=2;
    put32(0x1d3b80,0x500000);put32(0x1d3b7c,0x510000);
    put32(0x1d3bb8,0x520000);put32(0x1d3b38,0x540000);
    for(int family=1;family<=4;family++)put32(0x1b4399+family*10,records);
    for(int direction=0;direction<4;direction++){
        put16(records+25+5+direction*2,10+direction);
        put16(0x540000+(10+direction)*2,3+direction);
        put16(0x500000+(3+direction)*8,1);
    }
    for(int part=1;part<=4;part++){
        put16(0x510000+part*10,part*6);
        put16(0x510000+part*10+2,part*2-10);
        put16(0x510000+part*10+4,-part*8);
        put32(0x520000+part*6,0x530000+part*128);
        memory[0x520000+part*6+4]=8;memory[0x520000+part*6+5]=8;
        for(int y=0;y<8;y++){
            unsigned p=0x530000+part*128+y*10;memory[p]=8;
            memset(memory+p+1,40+part,8);memory[p+9]=0;
        }
    }
    put16(0x510000+10+8,2);put16(0x510000+20+6,0x80);
    unsigned movements=0,bindings=0;
    const int positions[][2]={{0,0},{128,128},{255,255},{31,127},{207,53}};
    const int slopes[][3]={{0,0,-16},{48,-24,-32},{-48,24,16},{16,32,-48}};
    const int levels[]={0,2,9};
    for(int mode=0;mode<2;mode++){
        put16(SCREEN_MODE,mode?8:1);put16(TILE_W,mode?24:48);put16(TILE_H,mode?16:32);CHECK(geom(&s->g));
        for(int family=1;family<=3;family++)for(int direction=0;direction<4;direction++)
        for(int p=0;p<5;p++)for(int slope=0;slope<4;slope++)for(int l=0;l<3;l++){
            int px=positions[p][0],py=positions[p][1],level=levels[l];
            put16(track+22,family);memory[track+0x79]=level;memory[car+0x12]=direction;
            memory[car+6]=px;memory[car+8]=py;
            for(int j=0;j<3;j++)memory[records+25+18+j]=(uint8_t)slopes[slope][j];
            CHECK(capture(s,0));Card *c=&s->cards[0];
            CHECK(c->rig_ok && c->frame==3+direction && c->cls==14 && c->type==family);
            int ox=((mode?24:48)*px>>8)+(py>>(mode?5:4));
            int oy=mode?(py>>3)>>1:py>>3;
            float vy=(oy>>s->g.lo)/s->g.ths,vx=((ox>>s->g.lo)-vy*s->g.shear)/s->g.tws;
            int off=-slopes[slope][0]*px/256-slopes[slope][1]*py/256+slopes[slope][2]-(family==3?24:level*24);
            if(mode)off=(off/2)*2;
            CHECK(fabsf(c->rig_x-(12+vx))<1e-6f && fabsf(c->rig_y-(14+vy))<1e-6f);
            CHECK(fabsf(c->rig_z-(-off/40.0f))<1e-6f && (uint16_t)r16(car+10)==999);
            CHECK(tp_remaster_riders_valid(1,c->seats,c->rig_riders));movements++;
        }
        put16(track+22,2);memory[track+0x79]=1;memory[car+6]=memory[car+8]=128;
        memory[car+0x12]=0;put16(car+0x47,3);
        for(int slot=0;slot<47;slot++){
            memset(memory+0x1b9a78,0,8*94);put16(0x1b9a78+slot*2,12);
            for(int variant=0;variant<8;variant++){
                memory[guest+0x59]=variant;
                if(variant)put16(0x1b9a78+variant*94+slot*2,18);
                for(int flags=0;flags<4;flags++){
                    put16(0x510000+20+6,0x80+flags);CHECK(capture(s,0));Card *c=&s->cards[0];
                    CHECK(c->rig_ok && c->seats[0]==variant && c->seats[1]==255);
                    CHECK(c->rig_riders[0].present && c->rig_riders[0].visible && c->rig_riders[0].pose==65024+flags*64+slot);
                    CHECK(tp_remaster_riders_valid(1,c->seats,c->rig_riders));
                    CHECK(!tp_remaster_riders_valid(0,c->seats,c->rig_riders));bindings++;
                }
            }
        }
        put16(car+0x47,0);
    }
    put16(SCREEN_MODE,1);put16(TILE_W,48);put16(TILE_H,32);CHECK(geom(&s->g));
    put16(car+0x47,3);memory[guest+0x59]=7;put16(0x510000+20+6,0x80);
    put16(0x1b9a78+7*94+46*2,0);CHECK(capture(s,0));Card c=s->cards[0];
    CHECK(c.rig_ok && c.rig_riders[0].present && !c.rig_riders[0].visible);
    CHECK(tp_remaster_riders_valid(1,c.seats,c.rig_riders));
    put16(0x510000+20+6,0x84);CHECK(capture(s,0));CHECK(!s->cards[0].rig_ok);
    put16(0x510000+20+6,0x80);put16(0x510000+20,31*6);
    put16(0x510000+20+8,4);put16(0x510000+40+6,0x80);
    put16(0x1b9a78+2*2,24);put16(0x1b9a78+3*94+2*2,18);
    put16(guest+0x47,4);memory[guest+200+4]=2;memory[guest+200+0x59]=3;
    put16(track+22,3);memory[car+12]=0x22;
    CHECK(capture(s,0));c=s->cards[0];
    CHECK(c.rig_ok && c.seats[0]==7 && c.seats[1]==3);
    CHECK(c.rig_riders[0].pose==65440 && !c.rig_riders[0].visible);
    CHECK(c.rig_riders[1].pose==65026 && c.rig_riders[1].visible);
    CHECK(tp_remaster_riders_valid(2,c.seats,c.rig_riders));
    c.rig_riders[0].visible=1;CHECK(!tp_remaster_riders_valid(2,c.seats,c.rig_riders));
    for(int sub=8;sub<=10;sub+=2){
        memory[car+12]=sub;CHECK(capture(s,0));c=s->cards[0];
        CHECK(c.rig_ok && c.seats[0]==255 && c.seats[1]==255);
        CHECK(!c.rig_riders[0].present && !c.rig_riders[1].present);
    }
    memory[car+12]=0x22;put16(0x510000+20,32*6);
    CHECK(capture(s,0));CHECK(!s->cards[0].rig_ok);put16(0x510000+20,31*6);
    memory[guest+200+4]=8;CHECK(capture(s,0));CHECK(!s->cards[0].rig_ok);memory[guest+200+4]=2;
    memory[guest+200+0x59]=8;CHECK(capture(s,0));CHECK(!s->cards[0].rig_ok);memory[guest+200+0x59]=3;
    put16(guest+200+0x47,3);CHECK(capture(s,0));CHECK(!s->cards[0].rig_ok);put16(guest+200+0x47,0);
    put16(0x1d667cu,8);put16(0x1d6642,2);CHECK(!capture(s,0x30e) && s->ncards==0);put16(0x1d667cu,0);
    put16(track+22,4);CHECK(capture(s,0));CHECK(!s->cards[0].rig_ok);put16(track+22,3);
    /* A car with no track can display a guest-only fallback. It must not
     * accidentally become a car rig merely because one frame was captured. */
    put16(car+2,0);put16(guest+10,10);CHECK(capture(s,0));CHECK(!s->cards[0].rig_ok);put16(car+2,2);
    float seat[12]={0,-1,0,.3f,1,0,0,.4f,0,0,1,.2f},lifted[12],q[3];
    rm_lift_attachment(lifted,seat,2.5f);rm_point(lifted,1,2,3,q);
    CHECK(fabsf(q[0]+1.7f)<1e-6f && fabsf(q[1]-1.4f)<1e-6f && fabsf(q[2]-5.7f)<1e-6f);
    CHECK(seat[11]==.2f); /* shared authored matrices remain immutable */
    free(s);free(memory);
    printf("remaster cars: %u movements, %u rider bindings, native omission, malformed chains and inherited attachment lift passed\n",movements,bindings);
    return 0;
}
