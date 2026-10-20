/* Source-free geometry, loader and attachment tests for authored car paths. */
#include <stdio.h>
#include <math.h>
#include "../runtime/remaster_motion.h"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"motion line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static int read_text(const char*s,RMMotion*m){FILE*f=tmpfile();if(!f)return 0;fputs(s,f);rewind(f);int ok=rm_motion_read(f,m);fclose(f);return ok;}
static void point(const float*m,const float*p,float*out){for(int i=0;i<3;i++)out[i]=m[i*4]*p[0]+m[i*4+1]*p[1]+m[i*4+2]*p[2]+m[i*4+3];}
static int near(float a,float b){return fabsf(a-b)<2e-5f;}
int main(void){
    RMMotion m={0};
    const char*good="TPRMOTION1 1 1\n14 0 0 0 0 .565\n1 5 .6 .6 2\n0 -.5 .2 0 1 0\n0 .5 .2 0 1 0\n";
    CHECK(read_text(good,&m));CHECK(m.nb==1&&m.nr==1&&m.routes[0].n==2);rm_motion_free(&m);
    const char*bad[]={"", "TPRMOTION2 1 1", "TPRMOTION1 0 1", "TPRMOTION1 65 1", "TPRMOTION1 1 129",
      "TPRMOTION1 1 1\n14 0 64 0 0 .5\n", "TPRMOTION1 1 1\n14 0 0 nan 0 .5\n",
      "TPRMOTION1 1 1\n14 0 0 0 0 .5\n1 5 .6 -.6 2\n",
      "TPRMOTION1 1 1\n14 0 0 0 0 .5\n1 5 .6 .6 1\n",
      "TPRMOTION1 1 1\n14 0 0 0 0 .5\n1 5 .6 .6 2\n0 0 0 0 0 0\n0 1 0 0 1 0\n",
      "TPRMOTION1 1 1\n14 0 0 0 0 .5\n1 5 .6 .6 2\n0 0 0 0 1 0\n0 0 1 0 1 0\n",
      "TPRMOTION1 1 1\n14 0 0 0 0 .5\n1 5 .6 .6 2\n0 0 0 0 -1 0\n0 1 0 0 -1 0\n",
      "TPRMOTION1 2 1\n14 0 0 0 0 .5\n14 0 0 0 0 .5\n",
      "TPRMOTION1 1 2\n14 0 0 0 0 .5\n1 5 0 0 2\n0 0 0 0 1 0\n0 1 0 0 1 0\n1 5 0 0 2\n0 0 0 0 1 0\n0 1 0 0 1 0\n"};
    for(unsigned i=0;i<sizeof bad/sizeof*bad;i++){CHECK(!read_text(bad[i],&m));CHECK(!m.bodies&&!m.routes);}
    char extra[512];snprintf(extra,sizeof extra,"%s junk",good);CHECK(!read_text(extra,&m));
    const float roots[2][12]={{1,0,0,0,0,1,0,0,0,0,1,0},{0,-1,0,.7f,1,0,0,-.2f,0,0,1,1.3f}};
    const float slopes[]={0,.4f,-.6f,1.2f};float pivot[3]={.03f,-.02f,.565f};unsigned cases=0;
    for(int axis=0;axis<2;axis++)for(int slope=0;slope<4;slope++)for(int reverse=0;reverse<2;reverse++)
    for(int root=0;root<2;root++)for(int fraction=0;fraction<256;fraction++){
        RMRoute route={.family=1,.shape=5,.n=2,.base=.6f,.step=.6f};
        float grade=slopes[slope];
        for(int j=0;j<2;j++){
            route.p[j][axis]=j-.5f;route.p[j][2]=.2f+grade*(j-.5f);
            route.p[j][axis+3]=1;route.p[j][5]=grade;CHECK(rm_unit3(route.p[j]+3));
        }
        TPRMVehicle v={.family=1,.shape=5,.level=3,.track_x=12,.track_y=14,.car_x=12,.car_y=14,
            .frac_x=axis?128:(uint8_t)fraction,.frac_y=axis?(uint8_t)fraction:128,
            .direction=(uint8_t)(axis?(reverse?0:2):(reverse?3:1)),.bounce=2};
        float parent[12],rooted[12],posed[3],actual[3];
        CHECK(rm_motion_transform(&route,&v,pivot,roots[root],parent));
        point(roots[root],pivot,posed);point(parent,posed,actual);
        CHECK(near(actual[axis],fraction/256.f-.5f)&&near(actual[1-axis],0));
        CHECK(near(actual[2],.2f+grade*(fraction/256.f-.5f)+2.4f+.05f));
        rm_affine_product(rooted,parent,roots[root]);
        float sign=reverse?-1:1;
        for(int k=0;k<3;k++)CHECK(near(-rooted[k*4+1],route.p[0][k+3]*sign));
        float seat[12]={1,0,0,.1f,0,1,0,.2f,0,0,1,.85f},posed_seat[12],attached[12],expected[12];
        rm_affine_product(posed_seat,roots[root],seat);rm_affine_product(attached,parent,posed_seat);
        rm_affine_product(expected,rooted,seat);for(int k=0;k<12;k++)CHECK(near(attached[k],expected[k]));
        cases++;
    }
    /* Analytic quarter turn: continuous tangents, both travel directions,
     * no 90-degree body snap at the native direction change in its middle. */
    RMRoute curve={.family=2,.shape=1,.n=17,.base=.6f,.step=.6f};
    for(int j=0;j<17;j++){
        float a=(float)M_PI-j*(float)M_PI/32;
        curve.p[j][0]=.5f+.5f*cosf(a);curve.p[j][1]=-.5f+.5f*sinf(a);curve.p[j][2]=.525f;
        curve.p[j][3]=sinf(a);curve.p[j][4]=-cosf(a);
    }
    TPRMVehicle v={.family=2,.shape=1,.level=1,.track_x=12,.track_y=14,.car_x=12,.car_y=14,.frac_x=128,.frac_y=128,.direction=2};
    float a[12],b[12];CHECK(rm_motion_transform(&curve,&v,pivot,roots[0],a));v.direction=1;
    CHECK(rm_motion_transform(&curve,&v,pivot,roots[0],b));for(int k=0;k<12;k++)CHECK(near(a[k],b[k]));
    v.direction=0;CHECK(rm_motion_transform(&curve,&v,pivot,roots[0],b));
    CHECK(near(a[1],-b[1])&&near(a[5],-b[5]));
    float zero[12]={0};CHECK(!rm_motion_transform(&curve,&v,pivot,zero,b));
    v.direction=4;CHECK(!rm_motion_transform(&curve,&v,pivot,roots[0],b));v.direction=0;
    v.bounce=4;CHECK(!rm_motion_transform(&curve,&v,pivot,roots[0],b));v.bounce=0;
    v.car_x=20;CHECK(!rm_motion_transform(&curve,&v,pivot,roots[0],b));v.car_x=12;
    v.family=1;CHECK(!rm_motion_transform(&curve,&v,pivot,roots[0],b));
    float inverse[12],identity[12];CHECK(rm_affine_inverse(inverse,roots[1]));
    rm_affine_product(identity,roots[1],inverse);for(int k=0;k<12;k++)CHECK(near(identity[k],roots[0][k]));
    memcpy(identity,roots[1],sizeof identity);rm_affine_product(identity,identity,inverse);
    for(int k=0;k<12;k++)CHECK(near(identity[k],roots[0][k]));
    puts("remaster motion: bounded loading, 8192 contacts/attachments, slopes, bidirectional turns, source-root removal and invalid paths passed");
    CHECK(cases==8192);return 0;
}
