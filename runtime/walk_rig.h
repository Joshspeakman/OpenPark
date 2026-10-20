/* Local Reimagined meshes with poses keyed by original frame ID. No wall-clock
 * animation or simulation mutation. Pose matrices are baked by the asset author;
 * an unknown pose deliberately returns to original art. Game-derived rig files
 * stay in TP_DATA/reimagined, outside source control.
 * TPRIG1 nv nt bones poses seats
 * vertices: xyz bone; triangles: indices RGBhex; poses: frame then 12 floats
 * per bone and seat, row-major affine matrices. Frame 65535 is seated guest.
 * TPRIG2: the same counts, then tw th and tw*th RRGGBBAA hex texels.
 * Vertices gain u v after bone (pixel coordinates). Triangle colours tint
 * the atlas; ffffff is neutral. This stores authored, camera-independent light.
 */
#ifndef TP_WALK_RIG_H
#define TP_WALK_RIG_H
#include <limits.h>
typedef struct {float x,y,z;unsigned bone;float u,v;} RMVertex;
typedef struct {unsigned a,b,c;uint32_t colour;} RMTriangle;
typedef struct {uint16_t frame;float *matrices;} RMPose;
typedef struct {int cls,type,nv,nt,nb,np,ns;RMVertex*v;RMTriangle*t;RMPose*p;float*matrices;
    int tw,th;uint32_t*texture;} RMRig;
static RMRig rm_cache[96];static int rm_count;
/* Source-frame aliases share one canonical model/cache entry. They are resolved
 * only on the render thread, never while the game thread captures a snapshot.
 * TPRMAP1 count; rows: source_class source_frame model_class model_type.
 * Decimal values, sorted by (source_class, source_frame), no duplicate keys. */
typedef struct { unsigned cls,frame,model_cls,type; } RMBinding;
static RMBinding *rm_bindings;static unsigned rm_nbindings;static int rm_bindings_loaded;
static void rm_reset(void){for(int i=0;i<rm_count;i++){free(rm_cache[i].v);free(rm_cache[i].t);free(rm_cache[i].p);free(rm_cache[i].matrices);free(rm_cache[i].texture);}memset(rm_cache,0,sizeof rm_cache);rm_count=0;
    free(rm_bindings);rm_bindings=NULL;rm_nbindings=0;rm_bindings_loaded=0;}
static int rm_read_bindings(FILE*f,RMBinding**out,unsigned*count){
    char magic[16];unsigned n;RMBinding*b=NULL;
    *out=NULL;*count=0;
    if(fscanf(f,"%15s%u",magic,&n)!=2 || strcmp(magic,"TPRMAP1") || !n || n>8192)return 0;
    b=calloc(n,sizeof*b);if(!b)return 0;
    for(unsigned i=0;i<n;i++){
        if(fscanf(f,"%u%u%u%u",&b[i].cls,&b[i].frame,&b[i].model_cls,&b[i].type)!=4 ||
           b[i].cls>255 || b[i].frame>65535 || b[i].model_cls>255 || b[i].type>32767 ||
           (i && (b[i].cls<b[i-1].cls || (b[i].cls==b[i-1].cls && b[i].frame<=b[i-1].frame)))){free(b);return 0;}
    }
    char extra;if(fscanf(f," %c",&extra)==1){free(b);return 0;}
    *out=b;*count=n;return 1;
}
static int rm_read(FILE*f,RMRig*r){
    char magic[16];int nv,nt,nb,np,ns,tw=0,th=0;uint32_t*texture=NULL;
    if(fscanf(f,"%15s%d%d%d%d%d",magic,&nv,&nt,&nb,&np,&ns)!=6 || (strcmp(magic,"TPRIG1") && strcmp(magic,"TPRIG2")) ||
       nv<3 || nv>8192 || nt<1 || nt>16384 || nb<1 || nb>64 || np<1 || np>8192 || ns<0 || ns>40 ||
       (uint64_t)np*(nb+ns)*12>2000000)return 0;
    RMVertex*v=calloc((size_t)nv,sizeof*v);RMTriangle*t=calloc((size_t)nt,sizeof*t);
    RMPose*p=calloc((size_t)np,sizeof*p);float*mat=calloc((size_t)np*(nb+ns)*12,sizeof*mat);
    if(!v||!t||!p||!mat)goto bad;
    if(!strcmp(magic,"TPRIG2")) {
        if(fscanf(f,"%d%d",&tw,&th)!=2 || tw<1||th<1||tw>2048||th>2048)goto bad;
        texture=calloc((size_t)tw*th,sizeof*texture);if(!texture)goto bad;
        for(int i=0;i<tw*th;i++){
            unsigned rgba;if(fscanf(f,"%x",&rgba)!=1)goto bad;
            texture[i]=((rgba>>24)&255)|((rgba>>8)&0xff00)|((rgba<<8)&0xff0000)|((rgba&255)<<24);
        }
    }
    for(int i=0;i<nv;i++){
        if(fscanf(f,"%f%f%f%u",&v[i].x,&v[i].y,&v[i].z,&v[i].bone)!=4 ||
           !isfinite(v[i].x)||!isfinite(v[i].y)||!isfinite(v[i].z)||fabsf(v[i].x)>32||fabsf(v[i].y)>32||fabsf(v[i].z)>32||v[i].bone>=(unsigned)nb)goto bad;
        if(texture && (fscanf(f,"%f%f",&v[i].u,&v[i].v)!=2 ||
           !isfinite(v[i].u)||!isfinite(v[i].v)||v[i].u<0||v[i].v<0||v[i].u>tw||v[i].v>th))goto bad;
    }
    for(int i=0;i<nt;i++){unsigned rgb;if(fscanf(f,"%u%u%u%x",&t[i].a,&t[i].b,&t[i].c,&rgb)!=4 ||
        t[i].a>=(unsigned)nv||t[i].b>=(unsigned)nv||t[i].c>=(unsigned)nv||rgb>0xffffff)goto bad;
        t[i].colour=0xff000000u|((rgb>>16)&255)|(rgb&0xff00)|((rgb&255)<<16);}
    for(int i=0;i<np;i++){unsigned frame;if(fscanf(f,"%u",&frame)!=1 || frame>65535 || (i && frame<=p[i-1].frame))goto bad;
        p[i].frame=(uint16_t)frame;p[i].matrices=mat+(size_t)i*(nb+ns)*12;
        for(int k=0;k<(nb+ns)*12;k++){float *a=&p[i].matrices[k];if(fscanf(f,"%f",a)!=1||!isfinite(*a)||fabsf(*a)>64)goto bad;}}
    r->nv=nv;r->nt=nt;r->nb=nb;r->np=np;r->ns=ns;r->v=v;r->t=t;r->p=p;r->matrices=mat;r->tw=tw;r->th=th;r->texture=texture;return 1;
bad:free(v);free(t);free(p);free(mat);free(texture);return 0;
}
static const RMRig*rm_find(int cls,int type){
    if(type<0)return NULL;for(int i=0;i<rm_count;i++)if(rm_cache[i].cls==cls&&rm_cache[i].type==type)return rm_cache[i].nv?&rm_cache[i]:NULL;
    if(rm_count>=96)return NULL;RMRig*r=&rm_cache[rm_count++];r->cls=cls;r->type=type;
    const char*root=getenv("TP_DATA");if(!root)return NULL;char path[PATH_MAX];
    int n=snprintf(path,sizeof path,"%s/reimagined/%02x-%d.rig",root,cls,type);if(n<0||(size_t)n>=sizeof path)return NULL;
    FILE*f=fopen(path,"r");if(!f)return NULL;int ok=rm_read(f,r);fclose(f);
    if(!ok)fprintf(stderr,"Reimagined: invalid rig %s; original art retained\n",path);return ok?r:NULL;
}
static const RMRig*rm_find_frame(unsigned cls,unsigned frame){
    if(!rm_bindings_loaded){
        rm_bindings_loaded=1;const char*root=getenv("TP_DATA");char path[PATH_MAX];
        if(root){
            int n=snprintf(path,sizeof path,"%s/reimagined/models.map",root);
            if(n>0 && (size_t)n<sizeof path){
                FILE*f=fopen(path,"r");
                if(f){int ok=rm_read_bindings(f,&rm_bindings,&rm_nbindings);fclose(f);
                    if(!ok)fprintf(stderr,"Reimagined: invalid model bindings %s; original art retained\n",path);}
            }
        }
    }
    unsigned lo=0,hi=rm_nbindings;
    while(lo<hi){unsigned mid=lo+(hi-lo)/2;const RMBinding*b=&rm_bindings[mid];
        if(b->cls<cls || (b->cls==cls && b->frame<frame))lo=mid+1;else hi=mid;}
    if(lo==rm_nbindings || rm_bindings[lo].cls!=cls || rm_bindings[lo].frame!=frame)return NULL;
    return rm_find((int)rm_bindings[lo].model_cls,(int)rm_bindings[lo].type);
}
static const RMPose*rm_pose(const RMRig*r,unsigned frame){int lo=0,hi=r->np;
    while(lo<hi){int mid=lo+(hi-lo)/2;if(r->p[mid].frame<frame)lo=mid+1;else hi=mid;}
    return lo<r->np&&r->p[lo].frame==frame?&r->p[lo]:NULL;
}
static void rm_point(const float*m,float x,float y,float z,float*out){
    out[0]=m[0]*x+m[1]*y+m[2]*z+m[3];out[1]=m[4]*x+m[5]*y+m[6]*z+m[7];out[2]=m[8]*x+m[9]*y+m[10]*z+m[11];
}
static void rm_draw(WMRender*r,const RMRig*rig,const RMPose*pose,float x,float y,float scale,const float*parent){
    const uint32_t*oldpal=r->palette,*oldrgba=r->rgba;uint32_t oldtint=r->texture_tint;
    const uint16_t*oldtex=r->texture;int tw=r->tw,th=r->th,fill=r->side_fill;
    uint16_t tex=0;uint32_t colour;r->texture=&tex;r->palette=&colour;r->tw=r->th=1;r->side_fill=0;
    r->rgba=rig->texture;if(r->rgba){r->tw=rig->tw;r->th=rig->th;}
    WMVertex transformed[8192];
    for(int i=0;i<rig->nv;i++){
        const RMVertex*v=&rig->v[i];float p[3],q[3];rm_point(pose->matrices+v->bone*12,v->x,v->y,v->z,p);
        if(parent){rm_point(parent,p[0],p[1],p[2],q);memcpy(p,q,sizeof p);}
        transformed[i]=wm_camera(r,x+p[0]*scale,y+p[1]*scale,p[2]*scale,v->u,v->v);
    }
    for(int i=0;i<rig->nt;i++){const RMTriangle*t=&rig->t[i];colour=t->colour;
        r->texture_tint=rig->texture?t->colour:0;
        wm_triangle(r,transformed[t->a],transformed[t->b],transformed[t->c]);}
    r->palette=oldpal;r->texture=oldtex;r->tw=tw;r->th=th;r->side_fill=fill;r->rgba=oldrgba;r->texture_tint=oldtint;
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
