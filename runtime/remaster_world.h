/* Complete canonical assemblies in the normal native pass. This translation
 * unit owns its game-thread rig cache; Visitor's render-thread cache is separate.
 * Source draws replay into private indexed storage, preserving their order and
 * ghost/text behavior without changing the game's indexed/picking buffer. */
#ifndef TP_REMASTER_WORLD_H
#define TP_REMASTER_WORLD_H
#include "art_mode.h"
#include "native_draw.h"
#include "native_scene.h"
#include "remaster_scene.h"
#include "remaster_surface.h"
#include "walk_mesh.h"
#include "walk_rig.h"

static int rm_world_model(WMRender*r,const TPRMSceneObject*c){
    if(c->figure_count)return 0; /* complete figure assemblies are not authored yet */
    if(c->ncomponents){
        if(c->cls!=18 || c->ncomponents>TP_RM_ASSEMBLY_MAX)return 0;
        const RMRig*rigs[TP_RM_ASSEMBLY_MAX];const RMPose*poses[TP_RM_ASSEMBLY_MAX];
        for(unsigned i=0;i<c->ncomponents;i++){
            const TPRMPlacedComponent*p=&c->components[i];
            if(!isfinite(p->x)||!isfinite(p->y)||!isfinite(p->z))return 0;
            rigs[i]=rm_find_frame(18,p->frame);
            if(!rigs[i] || rigs[i]->ns || !(poses[i]=rm_pose(rigs[i],p->frame)))return 0;
        }
        for(unsigned i=0;i<c->ncomponents;i++){
            const TPRMPlacedComponent*p=&c->components[i];
            float placement[12]={1,0,0,0,0,1,0,0,0,0,1,p->z};
            rm_draw(r,rigs[i],poses[i],p->x,p->y,1,placement);
        }
        return 1;
    }
    if(!c->rig_ok)return 0;
    const RMRig*rig=(c->cls==14 || c->cls==24)?rm_find_frame(c->cls,c->frame):rm_find(c->cls,c->type);
    const RMPose*pose=rig?rm_pose(rig,c->frame):NULL;
    const RMRig*guests[40];const RMPose*poses[40];
    if(!pose || !rm_resolve_riders(rig,c->seats,c->riders,guests,poses))return 0;
    float placement[12]={1,0,0,0,0,1,0,0,0,0,1,c->z},x=c->x,y=c->y;
    if(c->cls==14){
        if(!rm_vehicle_transform(&c->vehicle,rig,pose,placement))return 0;
        x=c->vehicle.track_x+1.f/3;y=c->vehicle.track_y+.5f;
    }
    /* Every attachment resolves before the first body pixel is written. */
    rm_draw(r,rig,pose,x,y,1,placement);
    for(int i=0;i<rig->ns;i++)if(guests[i]){
        float attachment[12];rm_affine_product(attachment,placement,pose->matrices+(rig->nb+i)*12);
        rm_draw(r,guests[i],poses[i],x,y,1,attachment);
    }
    return 1;
}

typedef struct {
    int idx,cell_y,replaceable,replaced;
    TPRMCapture capture;
} RMWorldItem;
typedef struct {
    int kind,owner,a,b,c,e,colour;uint32_t d;uint8_t text[16];
} RMWorldEvent;
typedef struct {
    const uint8_t*memory;size_t memory_size;unsigned extent;
    TPRemasterPaint*paint;TPRMProjection projection;WMRender renderer;
    NDTarget source;
    uint8_t*indexed;size_t indexed_capacity;
    float*depth;size_t depth_capacity;
    RMWorldItem*items;size_t item_capacity;
    RMWorldEvent*events;size_t event_capacity;
    unsigned nitems,nevents,level;unsigned stack[64];int colour,failed;
    unsigned models,source_objects;
} RMWorld;
static void rm_world_fail(RMWorld*w){w->failed=1;if(w->paint)w->paint->failed=1;}
static void*rm_world_reserve(void*p,size_t*capacity,size_t need,size_t size,size_t limit){
    if(need>limit)return NULL;if(need<=*capacity)return p;
    size_t n=*capacity?*capacity:16;while(n<need)n=n>limit/2?limit:n*2;
    void*q=realloc(p,n*size);if(!q)return NULL;*capacity=n;return q;
}
static void rm_world_release(RMWorld*w){
    free(w->indexed);free(w->depth);free(w->items);free(w->events);memset(w,0,sizeof *w);
}
static int rm_world_begin(RMWorld*w,const uint8_t*m,size_t memory_size,unsigned extent,
                          int cx,int cy,NDTarget*source,TPRemasterPaint*paint){
    w->paint=paint;w->failed=0;w->level=w->nitems=w->nevents=0;w->colour=0;
    w->models=w->source_objects=0;
    if(!paint || !m || memory_size<0x1de5f8u || !source || !source->buf ||
       !source->size || source->size>640u*720u || !extent || extent>128){rm_world_fail(w);return 0;}
    TPRemasterTarget*t=&paint->target;
    if(!t->pixels || !paint->palette || paint->density<1 || paint->density>8 ||
       t->width<1 || t->height<1 || t->width>16384 || t->height>16384 ||
       t->stride<(size_t)t->width || t->stride>SIZE_MAX/(sizeof(uint32_t)*(size_t)t->height) ||
       (size_t)t->width*t->height>TP_REMASTER_SURFACE_PIXELS){rm_world_fail(w);return 0;}
    int tw=tp_rm_scene_u16(m,0x1d663a),th=tp_rm_scene_u16(m,0x1d6654);
    int mode=tp_rm_scene_u16(m,0x1de5f6),lo=mode&1,off=(int16_t)tp_rm_scene_u16(m,0x1d6624);
    if(tw<6 || tw>128 || th<2 || th>128 || (!lo && mode!=8) || source->lowres!=lo){rm_world_fail(w);return 0;}
    size_t pixels=(size_t)t->width*t->height;
    if(source->size>w->indexed_capacity){
        uint8_t*p=realloc(w->indexed,source->size);if(!p){rm_world_fail(w);return 0;}
        w->indexed=p;w->indexed_capacity=source->size;
    }
    if(pixels>w->depth_capacity){
        float*p=realloc(w->depth,pixels*sizeof *p);if(!p){rm_world_fail(w);return 0;}
        w->depth=p;w->depth_capacity=pixels;
    }
    w->memory=m;w->memory_size=memory_size;w->extent=extent;
    w->source=*source;w->source.buf=w->indexed;w->source.paint=paint;w->source.unsupported=0;
    memcpy(w->indexed,source->buf,source->size);
    w->projection=(TPRMProjection){lo,tw>>lo,th>>lo,(tw/3)>>lo,lo?1.2:1,cx,cy,
        (640-tw*((640-off)/tw))>>lo,0,0};
    const TPRMProjection*g=&w->projection;int density=paint->density;
    double bx=paint->origin_x+(g->origin_x-g->world_x*g->tws-g->world_y*g->shear)*density;
    double by=paint->origin_y+(g->origin_y-g->world_y*g->ths)*density;
    w->renderer=(WMRender){.pixels=t->pixels,.depth=w->depth,.w=t->width,.h=t->height,
        .rx=1,.ry=(float)(g->shear/g->tws),.uy=(float)(-g->ths/g->tws),.uz=(float)(1/g->aspect),
        .fy=-1,.fz=-1,.focal=(float)(g->tws*density),.orthographic=1,.orthographic_bias=512,
        .offset_x=(float)(bx-t->width*.5),.offset_y=(float)(by-t->height*.5),
        .stride=t->stride,.clipped=1,.clip_x0=t->x0>0?t->x0:0,.clip_y0=t->y0>0?t->y0:0,
        .clip_x1=t->x1<t->width?t->x1:t->width,.clip_y1=t->y1<t->height?t->y1:t->height};
    WMRender*r=&w->renderer;
    for(int y=r->clip_y0;y<r->clip_y1;y++){
        /* Flat native terrain occludes buried geometry. A .001-world-unit
         * depth bias lets coplanar authored decals sit on its surface. */
        float ground=(float)(512-(y+.5-by)/(g->ths*density)+(1+g->tws/g->aspect/g->ths)*.001);
        for(int x=r->clip_x0;x<r->clip_x1;x++)w->depth[(size_t)y*t->width+x]=ground;
    }
    return 1;
}
static void rm_world_flush(RMWorld*w){
    for(unsigned i=0;i<w->nevents && !w->failed;i++){
        const RMWorldEvent*e=&w->events[i];RMWorldItem*item=&w->items[e->owner];
        if(e->kind==6){
            TPRMProjection g=w->projection;g.cell_y=item->cell_y;TPRMSceneObject object;
            if(item->replaceable && tp_remaster_scene_resolve(w->memory,w->memory_size,w->extent,
                    item->idx,&item->capture,&g,&object))item->replaced=rm_world_model(&w->renderer,&object);
            if(item->replaced)w->models++;else w->source_objects++;
        }else{
            w->source.paint=item->replaced?NULL:w->paint;
            if(e->kind==2)tp_native_draw_world_sprite(&w->source,w->memory,e->d,e->c&255,e->c>>8,
                                                     e->a,e->b,e->e,e->colour,NULL);
            else if(e->kind==4)tp_native_draw_small_text(&w->source,w->memory,e->a,e->b,e->text,e->c);
        }
    }
    w->nitems=w->nevents=0;
}
static void rm_world_event(RMWorld*w,int kind,int a,int b,int c,uint32_t d,int e,const uint8_t*text){
    if(w->failed)return;
    if(kind==5){w->colour=c;return;}
    if(kind==6){
        if(w->level>=64){rm_world_fail(w);return;}
        RMWorldItem*p=rm_world_reserve(w->items,&w->item_capacity,w->nitems+1,sizeof *p,1024);
        if(!p){rm_world_fail(w);return;}w->items=p;
        RMWorldItem*item=&w->items[w->nitems];memset(item,0,sizeof *item);
        item->idx=a;item->cell_y=(int32_t)d;item->replaceable=1;tp_remaster_capture_begin(&item->capture,b);
        w->stack[w->level++]=w->nitems++;
    }else if(kind==12){
        if(!w->level || w->items[w->stack[w->level-1]].idx!=a ||
           w->items[w->stack[w->level-1]].capture.cls!=b){rm_world_fail(w);return;}
        if(!--w->level)rm_world_flush(w);return;
    }else if(kind==1 || kind==3)return;
    if(!w->level){rm_world_fail(w);return;}
    unsigned owner=w->stack[w->level-1];RMWorldItem*item=&w->items[owner];
    if(tp_remaster_capture_event(&item->capture,w->memory,w->memory_size,kind,a,b,c,d,e))return;
    if(kind!=6 && kind!=2 && kind!=4)return;
    RMWorldEvent*p=rm_world_reserve(w->events,&w->event_capacity,w->nevents+1,sizeof *p,65536);
    if(!p){rm_world_fail(w);return;}w->events=p;
    RMWorldEvent*event=&w->events[w->nevents++];
    *event=(RMWorldEvent){.kind=kind,.owner=(int)owner,.a=a,.b=b,.c=c,.d=d,.e=e,.colour=w->colour};
    if(kind==2){if(e&~0x83)item->replaceable=0;w->colour=0;}
    if(kind==4){
        item->replaceable=0;if(!text){rm_world_fail(w);return;}
        unsigned i=0;for(;i<sizeof event->text-1 && text[i];i++)event->text[i]=text[i];
        if(text[i])rm_world_fail(w); /* never silently truncate native sign text */
    }
}
static int rm_world_end(RMWorld*w){
    if(w->level || w->nevents || w->source.unsupported)rm_world_fail(w);
    return !w->failed && !w->paint->failed;
}
#endif
