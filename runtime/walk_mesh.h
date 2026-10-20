/* Private, portable pixel-mesh rasterizer for walk.c. Original palette texels,
 * perspective-correct nearest sampling, alpha cutouts, clipped triangles and a
 * shared depth buffer. No artwork or simulation state is owned by this module.
 *
 * Single-view reconstruction: caps are warped into a fixed world-space shell;
 * silhouette edges close its sides. Hidden surfaces reuse source texels. This
 * cannot recover authentic unseen geometry from the one original projection.
 */
#ifndef TP_WALK_MESH_H
#define TP_WALK_MESH_H
#include <float.h>

typedef struct { float x, y, z, u, v; } WMVertex;
typedef struct {
    uint32_t *pixels; float *depth; int w, h;
    float cx, cy, cz, fx, fy, fz, rx, ry, ux, uy, uz, focal;
    const uint32_t *palette; const uint16_t *texture; int tw, th;
    int side_fill; uint16_t side_colour;
    int row_lo[480],row_hi[480];
    unsigned triangles, fragments;
    /* Canonical remaster textures can use full colour without changing the UI
     * palette. Orthographic projection is also used by the offline park baker. */
    const uint32_t *rgba;
    int orthographic;
    float offset_x,offset_y;
} WMRender;

static WMVertex wm_camera(const WMRender *r, float x, float y, float z, float u, float v) {
    x -= r->cx; y -= r->cy; z -= r->cz;
    return (WMVertex){x*r->rx+y*r->ry, x*r->ux+y*r->uy+z*r->uz,
                      x*r->fx+y*r->fy+z*r->fz+(r->orthographic?64:0), u, v};
}
static WMVertex wm_lerp(WMVertex a, WMVertex b, float t) {
    return (WMVertex){a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t,
                      a.u+(b.u-a.u)*t,a.v+(b.v-a.v)*t};
}
static float wm_edge(float ax,float ay,float bx,float by,float x,float y) {
    return (x-ax)*(by-ay)-(y-ay)*(bx-ax);
}
static void wm_triangle_inner(WMRender *r, WMVertex a, WMVertex b, WMVertex c) {
    float ia=r->orthographic?1:1/a.z, ib=r->orthographic?1:1/b.z, ic=r->orthographic?1:1/c.z;
    float ax=r->w*.5f+r->offset_x+a.x*r->focal*ia, ay=r->h*.5f+r->offset_y-a.y*r->focal*ia;
    float bx=r->w*.5f+r->offset_x+b.x*r->focal*ib, by=r->h*.5f+r->offset_y-b.y*r->focal*ib;
    float cx=r->w*.5f+r->offset_x+c.x*r->focal*ic, cy=r->h*.5f+r->offset_y-c.y*r->focal*ic;
    float area=wm_edge(ax,ay,bx,by,cx,cy);
    if (!isfinite(area) || fabsf(area)<1e-5f) return;
    float xmin=fminf(ax,fminf(bx,cx)),xmax=fmaxf(ax,fmaxf(bx,cx));
    float ymin=fminf(ay,fminf(by,cy)),ymax=fmaxf(ay,fmaxf(by,cy));
    if (xmax<0 || ymax<0 || xmin>=r->w || ymin>=r->h) return;
    int x0=(int)fmaxf(0,ceilf(xmin-.5f)),x1=(int)fminf(r->w-1,floorf(xmax-.5f));
    int y0=(int)fmaxf(0,ceilf(ymin-.5f)),y1=(int)fminf(r->h-1,floorf(ymax-.5f));
    float inv=1/area;
    a.u*=ia; a.v*=ia; b.u*=ib; b.v*=ib; c.u*=ic; c.v*=ic;
    r->triangles++;
    for (int y=y0;y<=y1;y++) for (int x=x0;x<=x1;x++) {
        float wa=wm_edge(bx,by,cx,cy,x+.5f,y+.5f)*inv;
        float wb=wm_edge(cx,cy,ax,ay,x+.5f,y+.5f)*inv, wc=1-wa-wb;
        if (wa<-.00001f || wb<-.00001f || wc<-.00001f) continue;
        float iz=wa*ia+wb*ib+wc*ic;
        if (!(iz>0)) continue;
        float z=r->orthographic?wa*a.z+wb*b.z+wc*c.z:1/iz;
        float uvscale=r->orthographic?1:z;size_t at=(size_t)y*r->w+x;
        /* Coplanar cards can share a guest position. Barycentric roundoff must
         * not alternate their ownership pixel by pixel; keep the first surface
         * within a tiny relative depth tolerance. Genuine nearer surfaces win. */
        float old_depth=r->depth[at];
        if (z>=old_depth-fmaxf(1e-6f,old_depth*1e-6f)) continue;
        int u=(int)floorf((wa*a.u+wb*b.u+wc*c.u)*uvscale);
        int v=(int)floorf((wa*a.v+wb*b.v+wc*c.v)*uvscale);
        if (u<0) u=0; if (u>=r->tw) u=r->tw-1;
        if (v<0) v=0; if (v>=r->th) v=r->th-1;
        uint32_t output;
        if(r->rgba) {
            output=r->rgba[(size_t)v*r->tw+u];
            if((output>>24)<128)continue;
            output|=0xff000000u; /* cutout material, not premultiplied translucency */
        } else {
            uint16_t colour=r->texture[(size_t)v*r->tw+u];
            if (colour>=256) { if (!r->side_fill) continue; colour=r->side_colour; }
            output=r->palette[colour];
        }
        r->pixels[at]=output; r->depth[at]=z; r->fragments++;
    }
}
static void wm_triangle(WMRender *r, WMVertex a, WMVertex b, WMVertex c) {
    /* Clip in camera space before projection; near crossings must not disappear. */
    WMVertex in[3]={a,b,c}, out[4]; int n=0;
    for (int i=0;i<3;i++) {
        WMVertex p=in[i],q=in[(i+1)%3]; int pin=p.z>=.08f,qin=q.z>=.08f;
        if (pin) out[n++]=p;
        if (pin!=qin) out[n++]=wm_lerp(p,q,(.08f-p.z)/(q.z-p.z));
    }
    for (int i=1;i+1<n;i++) wm_triangle_inner(r,out[0],out[i],out[i+1]);
}
static void wm_quad(WMRender *r, WMVertex a, WMVertex b, WMVertex c, WMVertex d) {
    wm_triangle(r,a,b,c); wm_triangle(r,a,c,d);
}

enum { WM_BOX, WM_ROUND, WM_OPEN, WM_TREE, WM_TRACK, WM_SMALL, WM_GROUND };
typedef struct {
    float x,y,kx,ky,depth,base,ax,ay; int w,h,shape;
    /* right and inward directions are fixed to the source projection, never
     * turned to face the current viewer. */
    float rx,ry,dx,dy;
} WMShell;
static float wm_shell_depth(const WMRender *r,const WMShell *s,float u,float v) {
    if(s->shape==WM_BOX || s->shape==WM_OPEN || s->shape==WM_TRACK || s->shape==WM_SMALL)
        return s->depth;
    int row=(int)fminf(s->h-1,fmaxf(0,v));
    float left=r->row_lo[row],right=r->row_hi[row]+1,width=right-left;
    if(width<=0)return 0;
    float nx=fabsf((2*u-left-right)/width), d=s->depth;
    /* Silhouette-constrained loft: a dome's narrow crown, a tree's trunk and
     * a roof's taper have distinct cross-sections, rather than a full-image
     * cylinder. Opposing surfaces meet along the source silhouette. */
    float cross=sqrtf(fmaxf(0,1-nx*nx));
    if(s->shape==WM_BOX) cross=fminf(1,cross*2);
    return d*cross*fminf(1,width/s->w*1.15f);
}
static WMVertex wm_shell_vertex(WMRender *r,const WMShell *s,float u,float v,int side) {
    if (s->shape==WM_GROUND) {
        /* The source footprint is a sheared overhead projection. Undo that
         * shear for lakes/maze foundations instead of erecting their ground. */
        float across=(u-s->w*.5f)*s->kx;
        float along=(v/s->h-.5f)*s->depth*2;
        float z=.015f;
        if (s->base>0) { /* maze: original green hedge texels form a low relief */
            int ix=(int)fminf(s->w-1,fmaxf(0,u)),iy=(int)fminf(s->h-1,fmaxf(0,v));
            uint16_t p=r->texture[iy*s->w+ix];
            if(p<256) { uint32_t c=r->palette[p];int red=c&255,green=(c>>8)&255,blue=(c>>16)&255;
                if(green>red*.85f && green>blue*1.4f && green>25) z+=s->base; }
        }
        return wm_camera(r,s->x+s->rx*across+s->dx*along,
                           s->y+s->ry*across+s->dy*along,z,u,v);
    }
    float d=wm_shell_depth(r,s,u,v)*side, across=(u-s->ax)*s->kx;
    /* Lower pixels in large sprites include the ground seen from above. Fold
     * that strip onto the footprint, rather than stand the entire slab upright.
     * Small props and open mechanisms have no guessed ground strip. */
    float z=(s->ay-v)*s->ky, along=d;
    if (s->base>0 && z<s->base) {
        float t=fmaxf(0,z/s->base);
        along=-s->depth+2*s->depth*t; z=.008f;
    } else {
        z-=s->base;
        /* Original art looks down on the object. Undo its elevation so roofs
         * occupy their top surface instead of being painted upright on a wall. */
        if(s->shape==WM_TREE || s->shape==WM_ROUND)
            z=(z-(d+s->depth)*.25f)/.968246f;
    }
    return wm_camera(r,s->x+s->rx*across+s->dx*along,
                       s->y+s->ry*across+s->dy*along,fmaxf(.008f,z),u,v);
}
static int wm_opaque(const WMRender *r,int x,int y) {
    return x>=0 && y>=0 && x<r->tw && y<r->th && r->texture[(size_t)y*r->tw+x]<256;
}
static void wm_shell(WMRender *r,const WMShell *s,float distance) {
    if(s->shape==WM_SMALL) {
        wm_quad(r,wm_shell_vertex(r,s,0,0,-1),wm_shell_vertex(r,s,s->w,0,-1),
                  wm_shell_vertex(r,s,s->w,s->h,-1),wm_shell_vertex(r,s,0,s->h,-1));
        return;
    }
    if(s->h>480)return;
    for(int y=0;y<s->h;y++) {
        r->row_lo[y]=s->w;r->row_hi[y]=-1;
        for(int x=0;x<s->w;x++)if(wm_opaque(r,x,y)) {
            if(x<r->row_lo[y])r->row_lo[y]=x;r->row_hi[y]=x;
        }
    }
    int step=distance<8 ? 3 : distance<18 ? 6 : 10;
    /* Both caps: the z buffer resolves buildings, cars and people consistently.
     * Texture sampling retains original pixel resolution independently of mesh LOD. */
    for (int side=-1;side<=(s->shape==WM_GROUND ? -1:1);side+=2) {
        for (int y=0;y<s->h;y+=step) for (int x=0;x<s->w;x+=step) {
            int x1=x+step<s->w ? x+step:s->w, y1=y+step<s->h ? y+step:s->h;
            int any=0;
            for (int yy=y;yy<y1 && !any;yy++) for (int xx=x;xx<x1;xx++) if (wm_opaque(r,xx,yy)) { any=1; break; }
            if (!any) continue;
            wm_quad(r,wm_shell_vertex(r,s,x,y,side),wm_shell_vertex(r,s,x1,y,side),
                       wm_shell_vertex(r,s,x1,y1,side),wm_shell_vertex(r,s,x,y1,side));
        }
    }
    if(s->shape==WM_GROUND) return;
    /* Close every alpha boundary, including the holes of open rides. Side
     * texels repeat the exact boundary pixel; no generated side artwork. */
    for (int y=0;y<s->h;y++) for (int x=0;x<s->w;x++) if (wm_opaque(r,x,y)) {
        static const int dx[4]={-1,1,0,0},dy[4]={0,0,-1,1};
        for (int e=0;e<4;e++) if (!wm_opaque(r,x+dx[e],y+dy[e])) {
            float u0=x,u1=x+1,v0=y,v1=y+1;
            if (e==0) u1=u0; else if (e==1) u0=u1;
            else if (e==2) v1=v0; else v0=v1;
            WMVertex a=wm_shell_vertex(r,s,u0,v0,-1),b=wm_shell_vertex(r,s,u1,v1,-1);
            WMVertex c=wm_shell_vertex(r,s,u1,v1,1),d=wm_shell_vertex(r,s,u0,v0,1);
            /* Reuse source wall/roof texels across inferred hidden surfaces,
             * avoiding a single stretched silhouette colour down a whole wall. */
            a.v=b.v=c.v=d.v=y+.5f;
            if (e<2) { a.u=b.u=s->w*.25f; c.u=d.u=s->w*.75f; }
            else { a.u=d.u=x+.5f;b.u=c.u=x+.5f; }
            r->side_fill=1;r->side_colour=r->texture[y*s->w+x];
            wm_quad(r,a,b,c,d);
            r->side_fill=0;
        }
    }
}
#endif
