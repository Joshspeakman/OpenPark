/* Authored vehicle paths, independent of sprite projection offsets.
 * TPRMOTION1 body_count route_count
 * body: model_class model_type root_bone contact_pivot_xyz
 * route: family shape base_height level_step point_count
 * points: xyz tangent_xyz, ordered along the authored module.
 * Native simulation chooses the module, fraction, direction and rider poses.
 * This file supplies geometry only; missing paths never invent a vehicle. */
#ifndef TP_REMASTER_MOTION_H
#define TP_REMASTER_MOTION_H
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>
#include <limits.h>
#include "remaster_assembly.h"
#define RM_ROUTE_POINTS 129
typedef struct {unsigned cls,type,bone;float pivot[3];} RMMotionBody;
typedef struct {unsigned family,shape,n;float base,step,p[RM_ROUTE_POINTS][6];} RMRoute;
typedef struct {RMMotionBody*bodies;RMRoute*routes;unsigned nb,nr;} RMMotion;
static RMMotion rm_motion;static int rm_motion_loaded;
static void rm_motion_free(RMMotion*m){free(m->bodies);free(m->routes);memset(m,0,sizeof*m);}
static float rm_dot3(const float*a,const float*b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
static int rm_unit3(float*v){float n=sqrtf(rm_dot3(v,v));if(!isfinite(n)||n<1e-6f)return 0;for(int k=0;k<3;k++)v[k]/=n;return 1;}
static int rm_motion_read(FILE*f,RMMotion*out){
    char magic[16],extra;RMMotion m={0};
    memset(out,0,sizeof*out);
    if(fscanf(f,"%15s%u%u",magic,&m.nb,&m.nr)!=3||strcmp(magic,"TPRMOTION1")||
       !m.nb||m.nb>64||!m.nr||m.nr>128)return 0;
    m.bodies=calloc(m.nb,sizeof*m.bodies);m.routes=calloc(m.nr,sizeof*m.routes);
    if(!m.bodies||!m.routes)goto bad;
    for(unsigned i=0;i<m.nb;i++){
        RMMotionBody*b=&m.bodies[i];
        if(fscanf(f,"%u%u%u%f%f%f",&b->cls,&b->type,&b->bone,b->pivot,b->pivot+1,b->pivot+2)!=6||
           b->cls>255||b->type>32767||b->bone>=64||
           (i&&(b->cls<m.bodies[i-1].cls||(b->cls==m.bodies[i-1].cls&&b->type<=m.bodies[i-1].type))))goto bad;
        for(int k=0;k<3;k++)if(!isfinite(b->pivot[k])||fabsf(b->pivot[k])>32)goto bad;
    }
    for(unsigned i=0;i<m.nr;i++){
        RMRoute*r=&m.routes[i];
        if(fscanf(f,"%u%u%f%f%u",&r->family,&r->shape,&r->base,&r->step,&r->n)!=5||
           r->family<1||r->family>3||r->shape>255||r->n<2||r->n>RM_ROUTE_POINTS||
           !isfinite(r->base)||fabsf(r->base)>32||!isfinite(r->step)||r->step<0||r->step>2||
           (i&&(r->family<m.routes[i-1].family||(r->family==m.routes[i-1].family&&r->shape<=m.routes[i-1].shape))))goto bad;
        for(unsigned j=0;j<r->n;j++){
            for(int k=0;k<6;k++)if(fscanf(f,"%f",&r->p[j][k])!=1||!isfinite(r->p[j][k])||fabsf(r->p[j][k])>32)goto bad;
            if(!rm_unit3(r->p[j]+3))goto bad;
            if(j){float d[3];for(int k=0;k<3;k++)d[k]=r->p[j][k]-r->p[j-1][k];
                if(d[0]*d[0]+d[1]*d[1]<1e-10f||rm_dot3(d,r->p[j]+3)<=0||rm_dot3(d,r->p[j-1]+3)<=0)goto bad;}
        }
    }
    if(fscanf(f," %c",&extra)==1)goto bad;
    *out=m;return 1;
bad:rm_motion_free(&m);return 0;
}
static void rm_motion_load(void){
    if(rm_motion_loaded)return;rm_motion_loaded=1;
    const char*root=getenv("TP_DATA");char path[PATH_MAX];if(!root)return;
    int n=snprintf(path,sizeof path,"%s/reimagined/vehicles.motion",root);
    if(n<0||(size_t)n>=sizeof path)return;
    FILE*f=fopen(path,"r");if(!f)return;
    if(!rm_motion_read(f,&rm_motion))fprintf(stderr,"Reimagined: invalid vehicle paths %s; original art retained\n",path);
    fclose(f);
}
static const RMMotionBody*rm_motion_body(unsigned cls,unsigned type){
    rm_motion_load();for(unsigned i=0;i<rm_motion.nb;i++)if(rm_motion.bodies[i].cls==cls&&rm_motion.bodies[i].type==type)return &rm_motion.bodies[i];return NULL;
}
static const RMRoute*rm_motion_route(unsigned family,unsigned shape){
    rm_motion_load();for(unsigned i=0;i<rm_motion.nr;i++)if(rm_motion.routes[i].family==family&&rm_motion.routes[i].shape==shape)return &rm_motion.routes[i];return NULL;
}
static void rm_affine_product(float*out,const float*a,const float*b){
    float m[12];for(int i=0;i<3;i++)for(int j=0;j<4;j++){
        m[i*4+j]=(j==3?a[i*4+3]:0);for(int k=0;k<3;k++)m[i*4+j]+=a[i*4+k]*b[k*4+j];}
    memcpy(out,m,sizeof m);
}
static int rm_affine_inverse(float*out,const float*m){
    for(int i=0;i<12;i++)if(!isfinite(m[i]))return 0;
    float d=m[0]*(m[5]*m[10]-m[6]*m[9])-m[1]*(m[4]*m[10]-m[6]*m[8])+m[2]*(m[4]*m[9]-m[5]*m[8]);
    if(!isfinite(d)||fabsf(d)<1e-6f)return 0;
    float q[12]={(m[5]*m[10]-m[6]*m[9])/d,(m[2]*m[9]-m[1]*m[10])/d,(m[1]*m[6]-m[2]*m[5])/d,0,
        (m[6]*m[8]-m[4]*m[10])/d,(m[0]*m[10]-m[2]*m[8])/d,(m[2]*m[4]-m[0]*m[6])/d,0,
        (m[4]*m[9]-m[5]*m[8])/d,(m[1]*m[8]-m[0]*m[9])/d,(m[0]*m[5]-m[1]*m[4])/d,0};
    for(int i=0;i<3;i++)q[i*4+3]=-(q[i*4]*m[3]+q[i*4+1]*m[7]+q[i*4+2]*m[11]);
    for(int i=0;i<12;i++)if(!isfinite(q[i])||fabsf(q[i])>256)return 0;
    memcpy(out,q,sizeof q);return 1;
}
static int rm_motion_transform(const RMRoute*r,const TPRMVehicle*v,const float*pivot,const float*posed_root,float*out){
    if(!r||!v||v->family!=r->family||v->shape!=r->shape||v->direction>3||v->bounce>3||r->n<2||r->n>RM_ROUTE_POINTS)return 0;
    float x=(int)v->car_x-(int)v->track_x+v->frac_x/256.f-.5f;
    float y=(int)v->car_y-(int)v->track_y+v->frac_y/256.f-.5f;
    float best=FLT_MAX,t=0;unsigned segment=0;
    for(unsigned i=0;i+1<r->n;i++){
        float dx=r->p[i+1][0]-r->p[i][0],dy=r->p[i+1][1]-r->p[i][1],dd=dx*dx+dy*dy;
        if(dd<1e-10f)return 0;
        float u=((x-r->p[i][0])*dx+(y-r->p[i][1])*dy)/dd;u=fmaxf(0,fminf(1,u));
        float ex=x-r->p[i][0]-u*dx,ey=y-r->p[i][1]-u*dy,d=ex*ex+ey*ey;
        if(d<best){best=d;t=u;segment=i;}
    }
    if(best>.75f*.75f)return 0;
    float p[3],forward[3];for(int k=0;k<3;k++){
        p[k]=r->p[segment][k]*(1-t)+r->p[segment+1][k]*t;
        forward[k]=r->p[segment][k+3]*(1-t)+r->p[segment+1][k+3]*t;
    }
    if(!rm_unit3(forward))return 0;
    const float hx[4]={0,1,0,-1},hy[4]={-1,0,1,0};
    float sign=forward[0]*hx[v->direction]+forward[1]*hy[v->direction];if(fabsf(sign)<.001f)return 0;
    if(sign<0)for(int k=0;k<3;k++)forward[k]=-forward[k];
    float right[3]={-forward[1],forward[0],0};if(!rm_unit3(right))return 0;
    float up[3]={-right[1]*forward[2],right[0]*forward[2],right[1]*forward[0]-right[0]*forward[1]};
    p[2]+=r->base+v->level*r->step+v->bounce/40.f;
    float placement[12],inverse[12];
    for(int i=0;i<3;i++){
        placement[i*4]=right[i];placement[i*4+1]=-forward[i];placement[i*4+2]=up[i];
        placement[i*4+3]=p[i]-right[i]*pivot[0]+forward[i]*pivot[1]-up[i]*pivot[2];
    }
    if(!rm_affine_inverse(inverse,posed_root))return 0;
    rm_affine_product(out,placement,inverse);return 1;
}
#endif
