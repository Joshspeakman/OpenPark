/* Local Reimagined meshes with poses keyed by original frame ID. No wall-clock
 * animation or simulation mutation. Pose matrices are baked by the asset author;
 * an unknown pose deliberately returns to original art. Game-derived rig files
 * stay in TP_DATA/reimagined, outside source control.
 * TPRIG1 nv nt bones poses seats
 * vertices: xyz bone; triangles: indices RGBhex; poses: frame then 12 floats
 * per bone and seat, row-major affine matrices. Frame 65535 is seated guest.
 */
#ifndef TP_WALK_RIG_H
#define TP_WALK_RIG_H
#include <limits.h>
typedef struct {float x,y,z;unsigned bone;} RMVertex;
typedef struct {unsigned a,b,c;uint32_t colour;} RMTriangle;
typedef struct {uint16_t frame;float *matrices;} RMPose;
typedef struct {int cls,type,nv,nt,nb,np,ns;RMVertex*v;RMTriangle*t;RMPose*p;float*matrices;} RMRig;
static RMRig rm_cache[96];static int rm_count;
static void rm_reset(void){for(int i=0;i<rm_count;i++){free(rm_cache[i].v);free(rm_cache[i].t);free(rm_cache[i].p);free(rm_cache[i].matrices);}memset(rm_cache,0,sizeof rm_cache);rm_count=0;}
static int rm_read(FILE*f,RMRig*r){
    char magic[16];int nv,nt,nb,np,ns;
    if(fscanf(f,"%15s%d%d%d%d%d",magic,&nv,&nt,&nb,&np,&ns)!=6 || strcmp(magic,"TPRIG1") ||
       nv<3 || nv>8192 || nt<1 || nt>16384 || nb<1 || nb>64 || np<1 || np>8192 || ns<0 || ns>40 ||
       (uint64_t)np*(nb+ns)*12>2000000)return 0;
    RMVertex*v=calloc((size_t)nv,sizeof*v);RMTriangle*t=calloc((size_t)nt,sizeof*t);
    RMPose*p=calloc((size_t)np,sizeof*p);float*mat=calloc((size_t)np*(nb+ns)*12,sizeof*mat);
    if(!v||!t||!p||!mat)goto bad;
    for(int i=0;i<nv;i++)if(fscanf(f,"%f%f%f%u",&v[i].x,&v[i].y,&v[i].z,&v[i].bone)!=4 ||
        !isfinite(v[i].x)||!isfinite(v[i].y)||!isfinite(v[i].z)||fabsf(v[i].x)>32||fabsf(v[i].y)>32||fabsf(v[i].z)>32||v[i].bone>=(unsigned)nb)goto bad;
    for(int i=0;i<nt;i++){unsigned rgb;if(fscanf(f,"%u%u%u%x",&t[i].a,&t[i].b,&t[i].c,&rgb)!=4 ||
        t[i].a>=(unsigned)nv||t[i].b>=(unsigned)nv||t[i].c>=(unsigned)nv||rgb>0xffffff)goto bad;
        t[i].colour=0xff000000u|((rgb>>16)&255)|(rgb&0xff00)|((rgb&255)<<16);}
    for(int i=0;i<np;i++){unsigned frame;if(fscanf(f,"%u",&frame)!=1 || frame>65535 || (i && frame<=p[i-1].frame))goto bad;
        p[i].frame=(uint16_t)frame;p[i].matrices=mat+(size_t)i*(nb+ns)*12;
        for(int k=0;k<(nb+ns)*12;k++){float *a=&p[i].matrices[k];if(fscanf(f,"%f",a)!=1||!isfinite(*a)||fabsf(*a)>64)goto bad;}}
    r->nv=nv;r->nt=nt;r->nb=nb;r->np=np;r->ns=ns;r->v=v;r->t=t;r->p=p;r->matrices=mat;return 1;
bad:free(v);free(t);free(p);free(mat);return 0;
}
static const RMRig*rm_find(int cls,int type){
    if(type<0)return NULL;for(int i=0;i<rm_count;i++)if(rm_cache[i].cls==cls&&rm_cache[i].type==type)return rm_cache[i].nv?&rm_cache[i]:NULL;
    if(rm_count>=96)return NULL;RMRig*r=&rm_cache[rm_count++];r->cls=cls;r->type=type;
    const char*root=getenv("TP_DATA");if(!root)return NULL;char path[PATH_MAX];
    int n=snprintf(path,sizeof path,"%s/reimagined/%02x-%d.rig",root,cls,type);if(n<0||(size_t)n>=sizeof path)return NULL;
    FILE*f=fopen(path,"r");if(!f)return NULL;int ok=rm_read(f,r);fclose(f);
    if(!ok)fprintf(stderr,"Reimagined: invalid rig %s; original art retained\n",path);return ok?r:NULL;
}
static const RMPose*rm_pose(const RMRig*r,unsigned frame){int lo=0,hi=r->np;
    while(lo<hi){int mid=lo+(hi-lo)/2;if(r->p[mid].frame<frame)lo=mid+1;else hi=mid;}
    return lo<r->np&&r->p[lo].frame==frame?&r->p[lo]:NULL;
}
static void rm_point(const float*m,float x,float y,float z,float*out){
    out[0]=m[0]*x+m[1]*y+m[2]*z+m[3];out[1]=m[4]*x+m[5]*y+m[6]*z+m[7];out[2]=m[8]*x+m[9]*y+m[10]*z+m[11];
}
static void rm_draw(WMRender*r,const RMRig*rig,const RMPose*pose,float x,float y,float scale,const float*parent){
    const uint32_t*oldpal=r->palette;const uint16_t*oldtex=r->texture;int tw=r->tw,th=r->th,fill=r->side_fill;
    uint16_t tex=0;uint32_t colour;r->texture=&tex;r->palette=&colour;r->tw=r->th=1;r->side_fill=0;
    WMVertex transformed[8192];
    for(int i=0;i<rig->nv;i++){
        const RMVertex*v=&rig->v[i];float p[3],q[3];rm_point(pose->matrices+v->bone*12,v->x,v->y,v->z,p);
        if(parent){rm_point(parent,p[0],p[1],p[2],q);memcpy(p,q,sizeof p);}
        transformed[i]=wm_camera(r,x+p[0]*scale,y+p[1]*scale,p[2]*scale,0,0);
    }
    for(int i=0;i<rig->nt;i++){const RMTriangle*t=&rig->t[i];colour=t->colour;wm_triangle(r,transformed[t->a],transformed[t->b],transformed[t->c]);}
    r->palette=oldpal;r->texture=oldtex;r->tw=tw;r->th=th;r->side_fill=fill;
}
/* Same descending slot order as native_land.c:ride_draw. 255 is an empty seat. */
static int rm_seat_order(const uint8_t*guests,int n,int cap,int kind,unsigned phase,uint8_t out[40]){
    memset(out,255,40);if(n<0||n>36||cap<1||cap>40 || !(kind==0||kind==1||kind==5||kind==7||kind==9))return 0;
    int slots[40]={0},count=n,start=n;
    for(int i=0;i<n;i++)slots[i]=(int)guests[i]+1;
    int rot=kind<=1?(int)(phase%(unsigned)cap):0;start=kind<=1?n-cap+rot:n;
    if(n>cap){for(int i=n-cap-1;i>=0;i--)slots[i]=0;}
    else if(n<cap){if(kind<=1)start=(n+rot)%cap;for(;count<cap;count++)slots[count]=-1;}
    for(int seat=0;seat<cap;seat++){
        int v=0;for(int k=0;k<41;k++){if(start==0)start=40;start--;v=slots[start];if(v)break;}
        if(!v)return 0;slots[start]=0;if(v>0)out[seat]=(uint8_t)(v-1);
    }return 1;
}
#endif
