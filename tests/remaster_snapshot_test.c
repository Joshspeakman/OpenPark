/* Source-free scene fixtures: frame selection and model anchors must come from
 * the native draw call, not an animation guess or a cropped picture's bottom. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
static uint8_t *memory;
uint8_t *tp_host_memory(void){return memory;}
void tp_host_palette_rgba(uint32_t*p){memset(p,0,256*4);}
uint32_t tp_hook_pcs[32],tp_map_w=64,tp_map_ext=64;
void tp_hooks_changed(void){}
#include "../runtime/walk.c"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"snapshot line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static void put16(unsigned p,unsigned v){memory[p]=v;memory[p+1]=v>>8;}
static void put32(unsigned p,unsigned v){put16(p,v);put16(p+2,v>>16);}
static void file16(FILE*f,unsigned v){fputc(v&255,f);fputc((v>>8)&255,f);}
static void file32(FILE*f,unsigned v){file16(f,v);file16(f,v>>16);}
static int capture_missing(Snap*s,int idx,int expected_missing){
    s->ncards=0;s->npix=0;
    memset(sa,0,sizeof sa);memset(sb,255,sizeof sb);
    Build b={.s=s,.g=&s->g,.cx=12,.cy=14,.rider_slot=-1};
    b.a=(NDTarget){sa,sizeof sa,0,SW_,SW_,SH_,SW_-1,s->g.lo,NULL,0};
    b.b=b.a;b.b.buf=sb;
    put16(0x300000+(14*64+12)*4,idx);
    int missing=tp_native_cell_things(memory,12,14,OXS<<s->g.lo,OYS<<s->g.lo,card_log,&b);
    finish(&b);return missing==expected_missing && s->ncards==1;
}
static int capture(Snap*s,int idx){return capture_missing(s,idx,0);}
int main(void){
    memory=calloc(64u<<20,1);Snap*s=calloc(1,sizeof*s);CHECK(memory&&s);M=memory;
    put16(TILE_W,48);put16(TILE_H,32);put16(SCREEN_MODE,1);
    put32(MAP_PTR,0x300000);put32(0x1d3b04,0x300000+64*64*4);put32(THINGS,0x400000);
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
    memory[thing+4]=10;memory[thing+12]=2;memory[thing+6]=128;memory[thing+8]=64;
    put16(thing+0x16,3*107);memory[0x1d3c62+3*107+0x67]=255;
    CHECK(capture(s,1));c=s->cards[0];
    CHECK(c.rig_ok && c.cls==10 && c.type==3 && c.frame==3);
    CHECK(c.rig_x==12.5f && c.rig_y==14.25f);
    put16(thing+10,4);CHECK(capture(s,1));
    CHECK(s->cards[0].frame==4 && s->cards[0].rig_x==c.rig_x && s->cards[0].rig_y==c.rig_y);
    /* A linked customer or an extra overlay needs a complete composite rig. */
    put16(thing+0x47,2);CHECK(capture(s,1));CHECK(!s->cards[0].rig_ok);
    put16(thing+0x47,0);put16(thing+0x5f,3);CHECK(capture(s,1));CHECK(!s->cards[0].rig_ok);
    put16(thing+0x5f,0);memory[0x1d3c62+3*107+0x67]=0;
    put16(0x540000+0x49c,3); /* shop status marker is another drawn frame */
    CHECK(capture(s,1));CHECK(!s->cards[0].rig_ok);
    memory[0x1d3c62+3*107+0x67]=255;
    put16(thing+0x16,3*107+1);CHECK(capture(s,1));CHECK(!s->cards[0].rig_ok);
    put16(thing+0x16,3*107);memory[thing+12]=4;put16(SCREEN_MODE,8);
    CHECK(geom(&s->g));CHECK(capture(s,1));
    CHECK(s->cards[0].rig_ok && s->cards[0].rig_x==12.5f && s->cards[0].rig_y==14.125f);
    /* Reaction frames can have very different crops. Native origins and
     * source-frame keys must stay attached to the guest, in both modes. */
    memory[thing+4]=2;memory[thing+0x59]=7;
    static const uint8_t guest_states[]={2,4,6,8,0xa,0x14,0x18,0x1a,0x1e,
        0x20,0x22,0x28,0x2a,0x2c,0x2e,0x34,0x38,0x3a,0x3c,0x3e,0x40,0x42,0x4a,0x4c};
    for(int mode=0;mode<2;mode++){
        put16(SCREEN_MODE,mode?8:1);CHECK(geom(&s->g));
        for(unsigned st=0;st<sizeof guest_states;st++){
            memory[thing+12]=guest_states[st];
            for(unsigned frame=3;frame<=4;frame++){
                put16(thing+10,frame);CHECK(capture(s,1));c=s->cards[0];
                CHECK(c.rig_ok && c.cls==2 && c.type==7 && c.frame==frame);
                CHECK(c.rig_x==12.5f && c.rig_y==(mode?14.125f:14.25f));
                CHECK(c.x==12.5f && c.y==14.25f); /* legacy card placement */
            }
        }
    }
    memory[thing+12]=8;
    memory[thing+0x59]=8;CHECK(capture(s,1));CHECK(!s->cards[0].rig_ok);
    memory[thing+0x59]=0;
    put16(thing+0x52,1);CHECK(capture(s,1));CHECK(!s->cards[0].rig_ok);
    put16(thing+0x52,0);memory[thing+0xb9]=1;
    CHECK(capture(s,1));CHECK(!s->cards[0].rig_ok);memory[thing+0xb9]=0;
    put16(0x1d6678,1);CHECK(capture(s,1));CHECK(!s->cards[0].rig_ok);
    put16(0x1d6678,0);memory[0x1b14cc]=1;memory[thing+5]=0x80;
    CHECK(capture(s,1));CHECK(!s->cards[0].rig_ok);
    memory[0x1b14cc]=memory[thing+5]=0;
    for(int mode=0;mode<2;mode++){
        put16(SCREEN_MODE,mode?8:1);CHECK(geom(&s->g));
        memory[thing+12]=0xc;CHECK(capture(s,1));
        CHECK(s->cards[0].rig_ok && s->cards[0].rig_z==0);
        /* A no-art bench record triggers the native vertical placement.
         * The renderer reports this class unsupported; this fixture checks
         * the guest's offset only and preserves that diagnostic. */
        put16(thing+2,2);memory[thing+200+4]=0x1a;
        memory[thing+7]=12;memory[thing+9]=14;
        CHECK(capture_missing(s,1,0x1a));c=s->cards[0];
        CHECK(c.rig_ok && fabsf(c.rig_z-(mode?1.f/6:.2f))<1e-6f);
        CHECK(c.rig_x==12.5f && c.rig_y==(mode?14.125f:14.25f));
        put16(thing+2,0);memory[thing+200+4]=0;
        memory[thing+12]=0x32;put16(thing+0x5f,12);
        CHECK(capture(s,1));c=s->cards[0];
        CHECK(c.rig_ok && fabsf(c.rig_z-(mode?.125f:.3f))<1e-6f);
        CHECK(c.rig_x==12.5f && c.rig_y==(mode?14.125f:14.25f));
    }
    /* All nine native staff identities bind the actual body frame/origin;
     * selection, job and shared special overlays require separate coverage. */
    memory[thing+4]=8;put16(thing+0x5f,0);put16(thing+0x9c,65535);
    for(int mode=0;mode<2;mode++){
        put16(SCREEN_MODE,mode?8:1);CHECK(geom(&s->g));
        for(int variant=0;variant<9;variant++){
            memory[thing+0x59]=(uint8_t)variant;
            for(int state=2;state<=0x3e;state+=2){
                if(state==0x20||state==0x28||state==0x2a||state==0x3c)continue;
                memory[thing+12]=(uint8_t)state;
                for(int frame=3;frame<=4;frame++){
                    put16(thing+10,frame);CHECK(capture(s,1));c=s->cards[0];
                    CHECK(c.rig_ok && c.cls==8 && c.type==variant && c.frame==frame);
                    CHECK(c.rig_x==12.5f && c.rig_y==(mode?14.125f:14.25f) && c.rig_z==0);
                }
            }
        }
    }
    memory[thing+0x59]=9;CHECK(capture(s,1));CHECK(!s->cards[0].rig_ok);
    memory[thing+0x59]=2;put16(0x1d35ec+2*54,0x16);
    put16(thing+0x9c,0);put16(0x540000+0x500,3);
    CHECK(capture(s,1));CHECK(!s->cards[0].rig_ok);
    put16(thing+0x9c,65535);put16(0x540000+0x284,3);put16(0x1d6678,1);
    CHECK(capture(s,1));CHECK(!s->cards[0].rig_ok);put16(0x1d6678,0);
    memory[thing+12]=0x3c;put16(0x540000+0x4a0,3);
    CHECK(capture(s,1));CHECK(!s->cards[0].rig_ok);
    memory[thing+12]=0x3f;put16(thing+0x14,0);
    CHECK(capture(s,1));CHECK(s->cards[0].rig_ok);
    put16(thing+0x14,0x709);put16(0x540000+0x52a,3);
    CHECK(capture(s,1));CHECK(!s->cards[0].rig_ok);
    put16(thing+0x14,0);
    /* Restore the shop for the expanded-pixel fixture below. */
    memory[thing+4]=10;memory[thing+12]=4;put16(thing+0x5f,0);
    /* An expanded world part must be captured completely without moving the
     * model's native origin. This fixture contains no source game artwork. */
    mkdir("build",0755);mkdir("build/remaster_snapshot_fixture",0755);
    mkdir("build/remaster_snapshot_fixture/reimagined",0755);
    FILE*f=fopen("build/remaster_snapshot_fixture/reimagined/world.art","wb");CHECK(f);
    fwrite("TPRART02",1,8,f);file32(f,1);
    file16(f,0);file16(f,2);file16(f,4);file16(f,4);
    uint64_t hash=14695981039346656037ull;
    for(int i=0;i<16;i++){hash=(hash^42)*1099511628211ull;hash*=1099511628211ull;}
    file32(f,(unsigned)hash);file32(f,(unsigned)(hash>>32));
    file16(f,65534);file16(f,65534);file16(f,8);file16(f,8);
    for(int i=0;i<64;i++)file16(f,88);
    CHECK(fclose(f)==0);
    setenv("TP_DATA","build/remaster_snapshot_fixture",1);setenv("TP_ART_MODE","reimagined",1);
    put16(SCREEN_MODE,1);CHECK(geom(&s->g));CHECK(capture(s,1));
    CHECK(s->cards[0].w==8 && s->cards[0].h==8 && s->npix==64);
    CHECK(s->cards[0].rig_ok && s->cards[0].rig_x==12.5f && s->cards[0].rig_y==14.25f);
    for(int i=0;i<64;i++)CHECK(s->pix[i]==88);
    setenv("TP_ART_MODE","original",1);
    /* Actual ride capture must retain each substituted pose without relying
     * on a loaded Faithful asset. Two guests occupy different pose elements. */
    memset(memory+thing,0,600);put32(THING_LAST,thing+400);
    memory[thing+4]=4;memory[thing+12]=2;memory[thing+6]=memory[thing+8]=128;
    put16(thing+10,3);put16(thing+0x47,2);
    put16(0x1cced0+0xa6,2);memory[0x1cced0+0xa8]=1;memory[0x1cced0+0xb3]=5;
    memory[thing+200+4]=memory[thing+400+4]=2;
    memory[thing+200+0x59]=7;memory[thing+400+0x59]=3;put16(thing+200+0x47,3);
    put16(0x510000+10+8,2);
    put16(0x510000+20,6);put16(0x510000+20+6,0x80);put16(0x510000+20+8,3);
    put16(0x510000+30,12);put16(0x510000+30+2,10);put16(0x510000+30+4,-20);
    put16(0x510000+30+6,0x81);
    put16(0x1b9a78+1*2,6);put16(0x1b9a78+2*2,12);
    put16(0x1b9a78 + 3*0x5e + 1*2,12);put16(0x1b9a78 + 7*0x5e + 2*2,6);
    CHECK(capture(s,1));c=s->cards[0];
    CHECK(c.rig_ok && c.type==0 && c.seats[0]==3 && c.seats[1]==7);
    CHECK(c.nriders==0); /* no Faithful layer loaded; 3-D metadata is independent */
    CHECK(tp_remaster_riders_valid(2,c.seats,c.rig_riders));
    CHECK(c.rig_riders[0].pose==65025 && c.rig_riders[1].pose==65090);
    CHECK(c.rig_riders[0].visible && c.rig_riders[1].visible);
    /* Rotating rides advance the guest-to-seat mapping at the native phase
     * boundary; a repeated frame must not pin people to stationary seats. */
    memory[0x1cced0+0xb3]=0;
    for(unsigned phase=0;phase<8;phase++){
        put32(thing+0x20,phase);CHECK(capture(s,1));c=s->cards[0];
        CHECK(c.rig_ok && c.seats[0]==(phase%2?7:3) && c.seats[1]==(phase%2?3:7));
        CHECK(c.rig_riders[0].variant==c.seats[0] && c.rig_riders[1].variant==c.seats[1]);
        CHECK(c.rig_riders[0].pose==65025 && c.rig_riders[1].pose==65090);
    }
    memory[0x1cced0+0xb3]=5;put32(thing+0x20,0);
    /* Different body/rider crops and either screen mode preserve the ride's
     * native origin. Occupancy must not move the entire authored ride. */
    for(int mode=0;mode<2;mode++){
        put16(SCREEN_MODE,mode?8:1);CHECK(geom(&s->g));
        for(int crop=0;crop<2;crop++){
            put16(0x510000+10+2,crop?-80:-8);
            put16(0x510000+10+4,crop?-100:-24);
            CHECK(capture(s,1));c=s->cards[0];
            CHECK(c.rig_ok && c.frame==3 && c.rig_x==12.5f);
            CHECK(c.rig_y==(mode?14.25f:14.5f));
            CHECK(tp_remaster_riders_valid(2,c.seats,c.rig_riders));
        }
        put16(thing+0x47,0);CHECK(capture(s,1));c=s->cards[0];
        CHECK(c.rig_ok && c.rig_x==12.5f && c.rig_y==(mode?14.25f:14.5f));
        CHECK(tp_remaster_riders_valid(2,c.seats,c.rig_riders));
        put16(thing+0x47,2);
    }
    put16(SCREEN_MODE,1);CHECK(geom(&s->g));
    /* An intentionally absent substituted sprite is still a known pose. */
    put16(0x1b9a78 + 7*0x5e + 2*2,0);CHECK(capture(s,1));c=s->cards[0];
    CHECK(tp_remaster_riders_valid(2,c.seats,c.rig_riders));
    CHECK(c.rig_riders[1].present && !c.rig_riders[1].visible && c.rig_riders[1].pose==65090);
    put16(thing+0x92,65535);
    put16(0x1b9a58+3*4+2,6);put16(0x1b9a58+7*4+2,12);
    CHECK(capture(s,1));c=s->cards[0];
    CHECK(tp_remaster_riders_valid(2,c.seats,c.rig_riders));
    CHECK(c.rig_riders[0].pose==65281 && c.rig_riders[1].pose==65285);
    /* Same identity, distinct exact poses: never collapse to a stock sitter. */
    put16(thing+0x92,0);memory[thing+200+0x59]=3;put16(0x1b9a78 + 3*0x5e + 2*2,6);
    CHECK(capture(s,1));c=s->cards[0];
    CHECK(c.seats[0]==3 && c.seats[1]==3 && tp_remaster_riders_valid(2,c.seats,c.rig_riders));
    CHECK(c.rig_riders[0].pose!=c.rig_riders[1].pose);
    /* Castle links can be populated during loading, while native dispatch
     * intentionally omits all substituted riders. The body remains eligible. */
    memory[0x1cced0+0xb3]=9;put16(thing+0x32,0);
    CHECK(capture(s,1));c=s->cards[0];
    CHECK(c.rig_ok && c.seats[0]==255 && c.seats[1]==255);
    CHECK(tp_remaster_riders_valid(2,c.seats,c.rig_riders));
    put16(thing+0x32,1);CHECK(capture(s,1));c=s->cards[0];
    CHECK(c.rig_ok && c.seats[0]==3 && c.seats[1]==3);
    CHECK(tp_remaster_riders_valid(2,c.seats,c.rig_riders));
    put16(thing+0x8e,3);CHECK(capture(s,1));c=s->cards[0];
    CHECK(c.rig_ok); /* an inactive stored overlay key draws no extra frame */
    put16(thing+0x2e,1);CHECK(capture(s,1));c=s->cards[0];
    CHECK(!c.rig_ok); /* active native overlay requires a composite binding */
    /* Shop attachments use their separate 23-slot substitution table, even
     * when the same guest contributes multiple marked source parts. */
    memset(memory+thing,0,600);put32(THING_LAST,thing+400);
    memory[thing+4]=10;memory[thing+12]=2;memory[thing+6]=memory[thing+8]=128;
    put16(thing+10,3);put16(thing+0x16,12*107);put16(thing+0x47,2);
    memory[0x1d3c62+12*107+0x67]=255;memory[thing+200+4]=2;
    memset(memory+0x1b9d68,0,8*23*2);
    put16(0x500000+3*8,1);
    put16(0x510000+10,6);put16(0x510000+10+6,0);put16(0x510000+10+8,2);
    put16(0x510000+20,12);put16(0x510000+20+8,0);
    for(unsigned key=0;key<65536;key++){
        int shopkey=key>=65312 && key<65440 && (key-65312)%32<25;
        CHECK(tp_remaster_shop_key_valid(key)==shopkey);
        CHECK(!(shopkey && tp_remaster_rider_key_valid(key)));
    }
    CHECK(!tp_remaster_shop_key(-2,58*6,0));
    CHECK(!tp_remaster_shop_key(23,0,0));CHECK(!tp_remaster_shop_key(0,0,4));
    CHECK(!tp_remaster_shop_key(-1,57*6,0));
    for(int mode=0;mode<2;mode++){
        put16(SCREEN_MODE,mode?8:1);CHECK(geom(&s->g));
        for(int slot=0;slot<23;slot++){
            memset(memory+0x1b9d68,0,8*23*2);put16(0x1b9d68+slot*2,12);
            for(int variant=0;variant<8;variant++){
                memory[thing+200+0x59]=(uint8_t)variant;
                if(variant)put16(0x1b9d68+variant*46+slot*2,6);
                for(int flip=0;flip<4;flip++){
                    put16(0x510000+20+6,0x80+flip);CHECK(capture(s,1));c=s->cards[0];
                    CHECK(c.rig_ok && c.type==12 && c.seats[0]==variant);
                    CHECK(c.rig_x==12.5f && c.rig_y==(mode?14.25f:14.5f));
                    CHECK(c.rig_riders[0].pose==65312+flip*32+slot);
                    CHECK(c.rig_riders[0].visible && c.rig_riders[0].present);
                    CHECK(tp_remaster_riders_valid(1,c.seats,c.rig_riders));
                    CHECK(!tp_remaster_riders_valid(0,c.seats,c.rig_riders));
                }
            }
        }
    }
    put16(SCREEN_MODE,1);CHECK(geom(&s->g));
    put16(0x510000+20+6,0x80);memory[thing+200+0x59]=7;
    put16(0x1b9d68+7*46+22*2,0);CHECK(capture(s,1));c=s->cards[0];
    CHECK(c.rig_ok && c.rig_riders[0].present && !c.rig_riders[0].visible);
    CHECK(c.rig_riders[0].pose==65334);
    for(int exception=0;exception<2;exception++){
        unsigned offset=(exception?65:58)*6;
        memcpy(memory+0x520000+offset,memory+0x520000+12,6);
        put16(0x510000+20,offset);
        for(int variant=0;variant<8;variant++){
            memory[thing+200+0x59]=(uint8_t)variant;CHECK(capture(s,1));c=s->cards[0];
            CHECK(c.rig_ok && c.rig_riders[0].pose==65335+exception);
            CHECK(c.rig_riders[0].visible==(variant==0));
        }
    }
    put16(0x510000+20,57*6);CHECK(capture(s,1));CHECK(!s->cards[0].rig_ok);
    put16(0x510000+20,12);put16(0x510000+20+6,0x84);
    CHECK(capture(s,1));CHECK(!s->cards[0].rig_ok);
    put16(0x510000+20+6,0x80);put16(0x1d667cu,8);
    CHECK(!capture_missing(s,1,0x30a) && s->ncards==0);put16(0x1d667cu,0);
    put16(0x1b9d68+7*46+22*2,6);
    put16(0x510000+20+8,3);put16(0x510000+30,12);
    put16(0x510000+30+6,0x81);put16(0x510000+30+8,0);
    CHECK(capture(s,1));c=s->cards[0];
    CHECK(c.rig_ok && c.seats[0]==7 && c.seats[1]==7);
    CHECK(c.rig_riders[0].pose==65334 && c.rig_riders[1].pose==65366);
    CHECK(tp_remaster_riders_valid(2,c.seats,c.rig_riders));
    CHECK(!tp_remaster_riders_valid(1,c.seats,c.rig_riders));
    put16(0x510000+20+8,0);
    put16(thing+0x47,0);CHECK(capture(s,1));c=s->cards[0];
    CHECK(c.rig_ok && c.seats[0]==255 && !c.rig_riders[0].present);
    put16(thing+0x47,2);memory[thing+200+4]=8;
    CHECK(capture(s,1));CHECK(!s->cards[0].rig_ok);memory[thing+200+4]=2;
    memory[thing+200+0x59]=8;CHECK(capture(s,1));CHECK(!s->cards[0].rig_ok);
    free(s);free(memory);
    puts("remaster snapshot: scenery/shop/guest native origins, crop independence, tick-selected frames and composite guards passed");
    return 0;
}
