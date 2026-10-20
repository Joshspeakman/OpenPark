/* Source-free actual-seat selection and authored camera composition. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
static uint8_t *memory;
uint8_t *tp_host_memory(void){return memory;}
void tp_host_palette_rgba(uint32_t*p){memset(p,0,256*4);}
uint32_t tp_hook_pcs[32],tp_map_w=64,tp_map_ext=64;
void tp_hooks_changed(void){}
#include "../runtime/walk.c"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"camera line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static int near(float a,float b){return fabsf(a-b)<2e-5f;}
static int anchors(const char*s,RMEye*out,unsigned*n){FILE*f=tmpfile();if(!f)return 0;fputs(s,f);rewind(f);int ok=rm_eyes_read(f,out,n);fclose(f);return ok;}
int main(void){
    RMEye eyes[256];unsigned n;
    CHECK(anchors("TPREYES1 1\n2 3 0 0 -.1 .3 0 -2 0\n",eyes,&n));CHECK(n==1&&near(eyes[0].forward[1],-1));
    const char*bad[]={"","TPREYES2 1","TPREYES1 0","TPREYES1 257",
      "TPREYES1 1 2 3 64 0 0 0 0 -1 0","TPREYES1 1 2 3 0 nan 0 0 0 -1 0",
      "TPREYES1 1 2 3 0 0 0 0 0 0 0","TPREYES1 1 2 3 0 0 0 0 0 -1 0 extra",
      "TPREYES1 2 2 3 0 0 0 0 0 -1 0 2 3 0 0 0 0 0 -1 0","TPREYES1 1 2 3 0 0 0"};
    for(unsigned i=0;i<sizeof bad/sizeof*bad;i++){CHECK(!anchors(bad[i],eyes,&n));CHECK(!n);}
    Snap*s=calloc(1,sizeof*s);CHECK(s);s->ncards=1;Card*c=&s->cards[0];
    c->cls=14;c->idx=1;c->frame=3;c->rig_ok=1;memset(c->seats,255,40);
    for(int seat=0;seat<2;seat++){c->seats[seat]=3;c->rider_ids[seat]=10+seat;c->rig_riders[seat]=(TPRMRider){65026,3,1,1};}
    c->vehicle=(TPRMVehicle){.family=1,.shape=5,.direction=0,.track_x=12,.track_y=14,.car_x=12,.car_y=14,.frac_x=128,.frac_y=128};
    rm_bindings=calloc(1,sizeof*rm_bindings);CHECK(rm_bindings);rm_nbindings=1;rm_bindings_loaded=1;
    rm_bindings[0]=(RMBinding){14,3,14,0};RMRig *models[2];
    const float identity[12]={1,0,0,0,0,1,0,0,0,0,1,0};
    for(int i=0;i<2;i++){
        RMRig **slot=rm_cache_slot(i?2:14,i?3:0);CHECK(slot);
        RMRig*r=models[i]=*slot=calloc(1,sizeof *r);CHECK(r);r->cls=i?2:14;r->type=i?3:0;r->nv=3;r->nb=1;r->np=1;r->ns=i?0:2;
        r->p=calloc(1,sizeof*r->p);r->matrices=calloc((1+r->ns)*12,sizeof(float));CHECK(r->p&&r->matrices);
        r->p[0]=(RMPose){i?65026:3,r->matrices};
        for(int j=0;j<=r->ns;j++)memcpy(r->matrices+j*12,identity,sizeof identity);
    }
    models[0]->matrices[12+3]=.2f;models[0]->matrices[12+11]=.5f;
    models[0]->matrices[24+3]=-.2f;models[0]->matrices[24+11]=.5f;
    models[1]->matrices[11]=.1f;
    rm_motion_loaded=1;rm_motion.nb=1;rm_motion.nr=1;
    rm_motion.bodies=calloc(1,sizeof*rm_motion.bodies);rm_motion.routes=calloc(1,sizeof*rm_motion.routes);
    CHECK(rm_motion.bodies&&rm_motion.routes);
    rm_motion.bodies[0]=(RMMotionBody){14,0,0,{0,0,.2f}};
    RMRoute*r=&rm_motion.routes[0];*r=(RMRoute){.family=1,.shape=5,.n=2,.base=.6f,.step=.6f};
    for(int j=0;j<2;j++){r->p[j][1]=j-.5f;r->p[j][2]=.1f;r->p[j][4]=1;}
    rm_eyes_loaded=1;rm_neye=1;rm_eyes[0]=(RMEye){2,3,0,{0,-.1f,.3f},{0,-1,0}};
    const float scales[]={.25f,1,4};float p[3],f[3];
    for(int k=0;k<3;k++)for(int seat=0;seat<2;seat++){
        float scale=scales[k];CHECK(vehicle_eye(s,10+seat,scale,p,f));
        CHECK(near(p[0],12+1.f/3+(seat?-.2f:.2f)*scale));
        CHECK(near(p[1],14.5f-.1f*scale)&&near(p[2],1.4f*scale));
        CHECK(near(f[0],0)&&near(f[1],-1)&&near(f[2],0));
    }
    CHECK(!vehicle_eye(s,0,1,p,f)&&!vehicle_eye(s,12,1,p,f));
    /* Identical guest variants remain different people; duplicate seat IDs
     * are ambiguous and must not silently choose the first car/seat. */
    c->rider_ids[1]=10;CHECK(!vehicle_eye(s,10,1,p,f));c->rider_ids[1]=11;
    s->ncards=2;s->cards[1]=*c;s->cards[1].idx=2;CHECK(!vehicle_eye(s,10,1,p,f));
    s->cards[0].rider_ids[0]=0;CHECK(vehicle_eye(s,10,1,p,f));s->ncards=1;c->rider_ids[0]=10;
    rm_neye=0;CHECK(!vehicle_eye(s,10,1,p,f));rm_neye=1;
    rm_eyes[0].bone=1;CHECK(!vehicle_eye(s,10,1,p,f));rm_eyes[0].bone=0;
    c->rig_riders[1].pose=65027;CHECK(!vehicle_eye(s,10,1,p,f));c->rig_riders[1].pose=65026;
    /* Invisible in the original projection still means an occupied 3D
     * seat. Both the rider renderer and first-person camera require its pose. */
    const RMRig*occupants[40];const RMPose*postures[40];
    for(int j=0;j<2;j++){c->rig_riders[j].visible=0;c->rig_riders[j].pose=65440;}
    CHECK(!rm_resolve_riders(models[0],c->seats,c->rig_riders,occupants,postures));
    CHECK(!vehicle_eye(s,10,1,p,f));
    models[1]->p[0].frame=65440;
    CHECK(rm_resolve_riders(models[0],c->seats,c->rig_riders,occupants,postures));
    CHECK(occupants[0]==models[1]&&occupants[1]==models[1]);
    CHECK(postures[0]&&postures[1]&&postures[0]->frame==65440);
    CHECK(vehicle_eye(s,10,1,p,f)&&vehicle_eye(s,11,1,p,f));
    c->rig_riders[0].visible=1;
    CHECK(!rm_resolve_riders(models[0],c->seats,c->rig_riders,occupants,postures));
    for(int j=0;j<2;j++){c->rig_riders[j].visible=1;c->rig_riders[j].pose=65026;}
    models[1]->p[0].frame=65026;
    memset(models[1]->matrices,0,12*sizeof(float));CHECK(!vehicle_eye(s,10,1,p,f));
    memcpy(models[1]->matrices,identity,sizeof identity);models[1]->matrices[11]=.1f;
    /* Uphill travel rotates the eye and gaze with the car, even stationary. */
    for(int j=0;j<2;j++){r->p[j][0]=j-.5f;r->p[j][1]=0;r->p[j][2]=.1f+.5f*(j-.5f);
        r->p[j][3]=1;r->p[j][4]=0;r->p[j][5]=.5f;CHECK(rm_unit3(r->p[j]+3));}
    c->vehicle.direction=1;CHECK(vehicle_eye(s,10,1,p,f));
    CHECK(near(f[0],2/sqrtf(5))&&near(f[1],0)&&near(f[2],1/sqrtf(5)));
    rm_reset();CHECK(!rm_neye&&!rm_eyes_loaded);free(s);
    puts("remaster camera: exact seat/person identity, anchored eye/gaze, scales, slopes, malformed and missing bindings passed");return 0;
}
