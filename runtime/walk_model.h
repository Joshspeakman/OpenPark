/* Authored surface meshes for Visitor View. Geometry/UV assets remain local
 * under TP_DATA/visitor3d; no original art is embedded in this module.
 * TPMESH1: vertex_count triangle_count texture_width texture_height, followed by
 * xyzuv vertices (world cells / original source pixels) and indexed triangles.
 * A changed composite extent falls back to its sprite until explicitly modeled.
 */
#ifndef TP_WALK_MODEL_H
#define TP_WALK_MODEL_H
#include <limits.h>
typedef struct { float x,y,z,u,v; } WMModelVertex;
typedef struct {
    int cls,type,nv,nt,tw,th;
    WMModelVertex *vertices; unsigned *indices;
} WMModel;
static WMModel wm_models[96];
static int wm_model_count;

static void wm_models_reset(void) {
    for(int i=0;i<wm_model_count;i++) {
        free(wm_models[i].vertices);free(wm_models[i].indices);
    }
    memset(wm_models,0,sizeof wm_models);wm_model_count=0;
}
static int wm_model_read(FILE *f,WMModel *m) {
    char magic[16];int nv,nt,tw,th;
    if(fscanf(f,"%15s %d %d %d %d",magic,&nv,&nt,&tw,&th)!=5 || strcmp(magic,"TPMESH1") ||
       nv<3 || nv>8192 || nt<1 || nt>16384 || tw<1 || th<1 || tw>2048 || th>2048)return 0;
    WMModelVertex *v=calloc((size_t)nv,sizeof *v);unsigned *ind=calloc((size_t)nt*3,sizeof *ind);
    if(!v || !ind){free(v);free(ind);return 0;}
    for(int i=0;i<nv;i++) {
        if(fscanf(f,"%f %f %f %f %f",&v[i].x,&v[i].y,&v[i].z,&v[i].u,&v[i].v)!=5 ||
           !isfinite(v[i].x)||!isfinite(v[i].y)||!isfinite(v[i].z)||!isfinite(v[i].u)||!isfinite(v[i].v)||
           fabsf(v[i].x)>32 || fabsf(v[i].y)>32 || v[i].z<0 || v[i].z>64 ||
           fabsf(v[i].u)>2048 || fabsf(v[i].v)>2048)goto invalid;
    }
    for(int i=0;i<nt*3;i++)if(fscanf(f,"%u",&ind[i])!=1 || ind[i]>=(unsigned)nv)goto invalid;
    m->nv=nv;m->nt=nt;m->tw=tw;m->th=th;m->vertices=v;m->indices=ind;return 1;
invalid:
    free(v);free(ind);return 0;
}
static const WMModel *wm_model_find(int cls,int type) {
    if(type<0)return NULL;
    for(int i=0;i<wm_model_count;i++)if(wm_models[i].cls==cls && wm_models[i].type==type)
        return wm_models[i].nv ? &wm_models[i]:NULL;
    if(wm_model_count>=96)return NULL;
    WMModel*m=&wm_models[wm_model_count++];m->cls=cls;m->type=type;
    const char *root=getenv("TP_DATA");if(!root || !*root)return NULL;
    char path[PATH_MAX];int n=snprintf(path,sizeof path,"%s/visitor3d/%02x-%d.mesh",root,cls,type);
    if(n<0 || (size_t)n>=sizeof path)return NULL;
    FILE*f=fopen(path,"r");if(!f)return NULL;
    int ok=wm_model_read(f,m);fclose(f);
    if(!ok)fprintf(stderr,"Visitor View: invalid authored mesh %s; using original sprite\n",path);
    return ok ? m:NULL;
}
static int wm_model_draw(WMRender*r,const WMModel*m,float x,float y,float scale) {
    if(r->tw!=m->tw || r->th!=m->th)return 0;
    for(int i=0;i<m->nt;i++) {
        WMVertex p[3];
        for(int j=0;j<3;j++) {
            const WMModelVertex *v=&m->vertices[m->indices[i*3+j]];
            p[j]=wm_camera(r,x+v->x*scale,y+v->y*scale,v->z*scale,
                v->u,v->v);
        }
        wm_triangle(r,p[0],p[1],p[2]);
    }
    return 1;
}
#endif
