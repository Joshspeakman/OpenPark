/* Faithful layered rides: source-palette texture, articulated surfaces and
 * independently composited native riders. Assets stay outside source control.
 * TPLAYER1 nv nt bones poses seats tw th center_x center_y half_x half_y
 * atlas: tw*th palette indices (256 transparent); vertices: xyz uv bone;
 * triangles: three indices; poses: frame then affine matrices per bone/seat.
 * Poses follow simulation frame keys. No separate animation timer.
 */
#ifndef TP_WALK_LAYER_H
#define TP_WALK_LAYER_H
#include <limits.h>
typedef struct {float x,y,z,u,v;unsigned bone;} LPVertex;
typedef struct {unsigned frame;float*m;} LPPose;
typedef struct {
 int cls,type,nv,nt,nb,np,ns,tw,th;float ox,oy,hx,hy;
 uint16_t*tex;LPVertex*v;unsigned*tri;LPPose*p;float*mat;
} LPLayer;
typedef struct {uint32_t off;uint16_t w,h;uint8_t slot,variant;} LPRider;
static LPLayer lp_cache[96];static int lp_count,lp_loaded;
static void lp_free(LPLayer*r){free(r->tex);free(r->v);free(r->tri);free(r->p);free(r->mat);memset(r,0,sizeof*r);}
static void lp_reset(void){for(int i=0;i<lp_count;i++)lp_free(&lp_cache[i]);lp_count=0;lp_loaded=0;}
static int lp_read(FILE*f,LPLayer*r){
 LPLayer a={0};char magic[16];
 if(fscanf(f,"%15s%d%d%d%d%d%d%d%f%f%f%f",magic,&a.nv,&a.nt,&a.nb,&a.np,&a.ns,&a.tw,&a.th,&a.ox,&a.oy,&a.hx,&a.hy)!=12 || strcmp(magic,"TPLAYER1") ||
    a.nv<3||a.nv>8192||a.nt<1||a.nt>16384||a.nb<1||a.nb>64||a.np<1||a.np>4096||a.ns<0||a.ns>40||
    a.tw<1||a.tw>512||a.th<1||a.th>512||(uint64_t)a.np*(a.nb+a.ns)*12>2000000 ||
    !isfinite(a.ox)||!isfinite(a.oy)||!isfinite(a.hx)||!isfinite(a.hy)||fabsf(a.ox)>16||fabsf(a.oy)>16||a.hx<=0||a.hy<=0||a.hx>16||a.hy>16)return 0;
 a.tex=calloc((size_t)a.tw*a.th,sizeof*a.tex);a.v=calloc(a.nv,sizeof*a.v);a.tri=calloc((size_t)a.nt*3,sizeof*a.tri);
 a.p=calloc(a.np,sizeof*a.p);a.mat=calloc((size_t)a.np*(a.nb+a.ns)*12,sizeof*a.mat);
 if(!a.tex||!a.v||!a.tri||!a.p||!a.mat)goto bad;
 for(int i=0;i<a.tw*a.th;i++){unsigned c;if(fscanf(f,"%u",&c)!=1||c>256)goto bad;a.tex[i]=(uint16_t)c;}
 for(int i=0;i<a.nv;i++){LPVertex*v=&a.v[i];if(fscanf(f,"%f%f%f%f%f%u",&v->x,&v->y,&v->z,&v->u,&v->v,&v->bone)!=6||
    !isfinite(v->x)||!isfinite(v->y)||!isfinite(v->z)||!isfinite(v->u)||!isfinite(v->v)||fabsf(v->x)>32||fabsf(v->y)>32||v->z<0||v->z>32||v->u<0||v->u>a.tw||v->v<0||v->v>a.th||v->bone>=(unsigned)a.nb)goto bad;}
 for(int i=0;i<a.nt*3;i++)if(fscanf(f,"%u",&a.tri[i])!=1||a.tri[i]>=(unsigned)a.nv)goto bad;
 for(int i=0;i<a.np;i++){
  if(fscanf(f,"%u",&a.p[i].frame)!=1||a.p[i].frame>65535||(i&&a.p[i].frame<=a.p[i-1].frame))goto bad;
  a.p[i].m=a.mat+(size_t)i*(a.nb+a.ns)*12;
  for(int k=0;k<(a.nb+a.ns)*12;k++)if(fscanf(f,"%f",&a.p[i].m[k])!=1||!isfinite(a.p[i].m[k])||fabsf(a.p[i].m[k])>64)goto bad;
 }
 *r=a;return 1;
bad:lp_free(&a);return 0;
}
/* Preloaded at walk start while locked; snapshot/render threads only read it. */
static void lp_load(void){
 if(lp_loaded)return;
 const char*root=getenv("TP_DATA");if(!root)return;lp_loaded=1;
 /* Immutable for the process lifetime: snapshot readers never race a reload. */
 for(int type=0;type<29;type++){
  char path[PATH_MAX];int n=snprintf(path,sizeof path,"%s/faithful/04-%d.layer",root,type);if(n<0||(size_t)n>=sizeof path)continue;
  FILE*f=fopen(path,"r");if(!f)continue;LPLayer*r=&lp_cache[lp_count];int ok=lp_read(f,r);fclose(f);
  if(ok){r->cls=4;r->type=type;lp_count++;}else fprintf(stderr,"Faithful: invalid layered asset %s; original art retained\n",path);
 }
}
static const LPLayer*lp_find(int cls,int type){for(int i=0;i<lp_count;i++)if(lp_cache[i].cls==cls&&lp_cache[i].type==type)return &lp_cache[i];return NULL;}
static const LPPose*lp_pose(const LPLayer*r,unsigned frame){int lo=0,hi=r->np;while(lo<hi){int m=(lo+hi)/2;if(r->p[m].frame<frame)lo=m+1;else hi=m;}return lo<r->np&&r->p[lo].frame==frame?&r->p[lo]:NULL;}
static int lp_valid_riders(const LPLayer*l,const LPRider*rs,int nr){
 uint64_t slots=0;if(nr<0||nr>40)return 0;for(int i=0;i<nr;i++){if(rs[i].slot>=l->ns||!rs[i].w||!rs[i].h||rs[i].w>128||rs[i].h>128||(slots&(1ull<<rs[i].slot)))return 0;slots|=1ull<<rs[i].slot;}return 1;
}
static void lp_draw(WMRender*r,const LPLayer*l,const LPPose*p,float x,float y,float scale,
                    const LPRider*rs,int nr,const uint16_t*pixels,float kx,float ky){
 const uint16_t*old=r->texture;int tw=r->tw,th=r->th,fill=r->side_fill;r->texture=l->tex;r->tw=l->tw;r->th=l->th;r->side_fill=0;
 WMVertex v[8192];for(int i=0;i<l->nv;i++){const LPVertex*q=&l->v[i];float pos[3];rm_point(p->m+q->bone*12,q->x,q->y,q->z,pos);v[i]=wm_camera(r,x+pos[0]*scale,y+pos[1]*scale,pos[2]*scale,q->u,q->v);}
 for(int i=0;i<l->nt;i++)wm_triangle(r,v[l->tri[i*3]],v[l->tri[i*3+1]],v[l->tri[i*3+2]]);
 for(int i=0;i<nr;i++){
  const LPRider*q=&rs[i];const float*m=p->m+(l->nb+q->slot)*12;
  float cx=x+m[3]*scale,cy=y+m[7]*scale,z=m[11]*scale,hw=q->w*kx*.5f,h=q->h*ky;
  r->texture=pixels+q->off;r->tw=q->w;r->th=q->h;
  // Original guest art at a true world seat/foot point, occluded by geometry.
  wm_quad(r,wm_camera(r,cx-r->rx*hw,cy-r->ry*hw,z,0,q->h),wm_camera(r,cx+r->rx*hw,cy+r->ry*hw,z,q->w,q->h),
   wm_camera(r,cx+r->rx*hw,cy+r->ry*hw,z+h,q->w,0),wm_camera(r,cx-r->rx*hw,cy-r->ry*hw,z+h,0,0));
 }
 r->texture=old;r->tw=tw;r->th=th;r->side_fill=fill;
}
#endif
