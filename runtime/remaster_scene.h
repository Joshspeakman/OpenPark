/* Resolve native capture into view-independent model/attachment placement.
 * This reads simulation state and selected draw metadata, never pixel crops.
 * No rig loading, rendering, guessed animation or simulation mutation here. */
#ifndef TP_REMASTER_SCENE_H
#define TP_REMASTER_SCENE_H
#include <math.h>
#include "remaster_capture.h"
typedef struct {
    int lo;
    double tws,ths,shear,aspect,world_x,world_y;
    int origin_x,origin_y; /* decoded screen position of the reference world cell */
    int cell_y;          /* this thing's cell origin, before the low-res shift */
} TPRMProjection;
typedef struct {
    TPRMFigure draw;
    uint16_t person;
    uint8_t variant,valid;
    int occupants;
} TPRMSelectedFigure;
typedef struct {
    int idx,cls,type,rig_ok;
    uint16_t frame;
    float x,y,z;
    uint8_t seats[40];TPRMRider riders[40];uint16_t rider_ids[40];
    TPRMVehicle vehicle;
    TPRMSelectedFigure figure;int figure_count;
    unsigned ncomponents;TPRMPlacedComponent components[TP_RM_ASSEMBLY_MAX];
} TPRMSceneObject;
static inline uint16_t tp_rm_scene_u16(const uint8_t*m,size_t at){
    uint16_t value;memcpy(&value,m+at,sizeof value);return value;
}
static inline const uint8_t*tp_rm_scene_thing(const uint8_t*m,size_t size,unsigned idx,int linked){
    if(!idx || idx>=16384)return NULL;
    uint64_t at=(uint64_t)tp_remaster_capture_u32(m,0x1d3b10u)+idx*200u;
    if(at+200>size || (linked && at>tp_remaster_capture_u32(m,0x1d3b78u)))return NULL;
    return m+at;
}
static inline void tp_rm_scene_position(const TPRMProjection*g,int x,int y,float*out_x,float*out_y){
    double vy=(((int16_t)y>>g->lo)-g->origin_y)/g->ths;
    double vx=(((int16_t)x>>g->lo)-g->origin_x-vy*g->shear)/g->tws;
    *out_x=(float)(g->world_x+vx);*out_y=(float)(g->world_y+vy);
}
/* Rejected states retain their native class/type/frame metadata for diagnostics.
 * A true result means eligible geometry, not that its required artwork exists.
 * The renderer must still resolve every exact model, pose and attachment. */
static inline int tp_remaster_scene_resolve(const uint8_t*m,size_t size,unsigned extent,
        int idx,const TPRMCapture*b,const TPRMProjection*g,TPRMSceneObject*out){
    TPRMSceneObject*c=out;memset(c,0,sizeof *c);c->idx=idx;c->type=-1;
    memset(c->seats,255,sizeof c->seats);
    if(!m || size<0x1de5f8u || !b || !g || !idx || idx < -32768 || idx>=16384 ||
       !extent || extent>128 || (g->lo!=0 && g->lo!=1) ||
       !isfinite(g->tws) || !isfinite(g->ths) || !isfinite(g->shear) || !isfinite(g->aspect) ||
       !isfinite(g->world_x) || !isfinite(g->world_y) || g->tws<=0 || g->ths<=0 || g->aspect<=0 ||
       b->frame_count<0 || (b->frame_count==1 && (b->drawn_frame<0 || b->drawn_frame>65535)) ||
       b->ncomponents<0 || b->ncomponents>TP_RM_ASSEMBLY_MAX ||
       b->figure_count<0 || b->figure_count>1)return 0;
    c->cls=idx<0?255:b->cls;
    c->figure.draw=b->figure;c->figure_count=b->figure_count;
    memcpy(c->riders,b->rig_riders,sizeof c->riders);
    const uint8_t*t=idx>0?tp_rm_scene_thing(m,size,(unsigned)idx,0):NULL;
    if(idx>0 && (!t || t[4]!=b->cls))return 0;
    if(t){
        c->frame=tp_rm_scene_u16(t,10);
        if(b->cls==2){
            c->type=t[0x59];
            c->rig_ok=c->type<8 && b->frame_count==1 && b->drawn_frame==c->frame &&
                !tp_rm_scene_u16(t,0x52) && !t[0xb9] &&
                !((m[0x1b14cc]&7) && (t[5]&0x80)) && idx!=tp_rm_scene_u16(m,0x1d6678);
        }else if(b->cls==8){
            c->type=t[0x59];
            c->rig_ok=c->type<9 && b->frame_count==1 && b->drawn_frame==c->frame;
        }
    }
    if(t && b->cls==4){
        int type=tp_rm_scene_u16(t,0x16)/200;
        if(type<29){
            c->type=type;const uint8_t*rec=m+0x1cced0+type*200;
            int cap=tp_rm_scene_u16(rec,0xa6),kind=rec[0xb3],n=0,valid=1;
            uint8_t guests[36],indices[36],order[40];uint16_t ids[36];unsigned next=tp_rm_scene_u16(t,0x47);
            while(next && n<36){
                const uint8_t*p=tp_rm_scene_thing(m,size,next,1);
                if(!p || p[4]!=2){valid=0;break;}
                indices[n]=(uint8_t)n;ids[n]=(uint16_t)next;guests[n++]=p[0x59];next=tp_rm_scene_u16(p,0x47);
            }
            if(next)valid=0;
            /* Maze figures are native-selected overlays, not descending
             * seat substitutions. Preserve their actual identity/decision
             * for both views. Geometry remains ineligible until its complete
             * body, figure and occupancy rendering path is implemented. */
            int maze=type==4 && kind==8 && cap==16 && n<=cap && valid &&
               tp_rm_scene_u16(t,0x16)==800 && c->frame==1486 &&
               b->frame_count==1 && b->drawn_frame==c->frame &&
               (t[12]==2 || t[12]==4 || t[12]==6) &&
               tp_rm_scene_u16(t,0x2e)<rec[0xa8];
            for(int i=0;maze && i<n;i++)if(guests[i]>=8)maze=0;
            if(maze){
                c->figure.occupants=n;
                /* Kind 8 has no seat rotation. Keep every linked person in
                 * chain order, including those omitted by the source image. */
                for(int i=0;i<n;i++){c->seats[i]=guests[i];c->rider_ids[i]=ids[i];}
            }
            if(maze && b->figure_count==1 && !b->figure_error && !b->rig_rider_error &&
               tp_remaster_figure_valid(&b->figure)){
                int selector=b->figure.selector,person=selector<0?-selector:selector;
                int remembered=(int16_t)tp_rm_scene_u16(t,0x4d),clean=1;
                for(int i=0;i<40;i++)if(c->riders[i].present)clean=0;
                if(clean && (!remembered || remembered==selector))for(int i=0;i<n;i++){
                    if(ids[i]==person && guests[i]<8){
                        c->figure.person=(uint16_t)person;c->figure.variant=guests[i];
                        c->figure.valid=1;break;
                    }
                }
            }
            /* Castle loading/unloading has no attached bodies until this
             * native flag enables the substituted-passenger dispatch. */
            int seated_n=(kind==9 && !tp_rm_scene_u16(t,0x32))?0:n;
            c->rig_ok=valid && !b->rig_rider_error && b->frame_count==1 &&
                b->drawn_frame==c->frame && (t[12]==2 || t[12]==4 || t[12]==6) &&
                tp_rm_scene_u16(t,0x2e)<rec[0xa8] &&
                rm_seat_order(indices,seated_n,cap,kind,tp_remaster_capture_u32(t,0x20),order);
            if(c->rig_ok)for(int i=0;i<40;i++)if(order[i]!=255){
                /* Rotate the actual person and appearance together. Two
                 * same-looking guests must remain distinct occupants. */
                c->seats[i]=guests[order[i]];c->rider_ids[i]=ids[order[i]];
            }
            /* Selected opaque-cabin/interior frames have no sprite seat
             * markers. Retain actual linked passengers for their authored
             * interiors; never infer this for another ride or frame or
             * overwrite native rider metadata. The structure supplies occlusion. */
            uint16_t enclosed=type==1?tp_remaster_tree_hidden_key(c->frame):
                tp_remaster_enclosed_ride_key((unsigned)type,c->frame);
            if(c->rig_ok && enclosed){
                if(tp_rm_scene_u16(t,0x16)!=type*200 || kind!=5 || cap!=(type==1?1:5) || n>cap)c->rig_ok=0;
                for(int i=0;i<40;i++)if(c->riders[i].present)c->rig_ok=0;
                if(c->rig_ok)for(int i=0;i<cap;i++)if(c->seats[i]!=255){
                    if(c->seats[i]>=8){c->rig_ok=0;break;}
                    c->riders[i]=(TPRMRider){enclosed,c->seats[i],1,0};
                }
            }
        }
    }else if(t && b->cls==10){
        int offset=tp_rm_scene_u16(t,0x16),type=offset/107;
        if(type<17 && offset%107==0)c->type=type;
        unsigned customer=tp_rm_scene_u16(t,0x47);
        const uint8_t*person=customer?tp_rm_scene_thing(m,size,customer,1):NULL;
        int valid=!b->rig_rider_error && tp_rm_scene_u16(m,0x1d667c)!=8;
        if(customer && (!person || person[4]!=2 || person[0x59]>=8))valid=0;
        for(int i=0;i<40;i++)if(c->riders[i].present){
            if(!valid || !person || c->riders[i].variant!=person[0x59] ||
               !tp_remaster_shop_key_valid(c->riders[i].pose)){valid=0;break;}
            c->seats[i]=c->riders[i].variant;
            c->rider_ids[i]=(uint16_t)customer;
        }
        c->rig_ok=c->type>=0 && (t[12]==2 || t[12]==4) && valid &&
            !tp_rm_scene_u16(t,0x5f) && b->frame_count==1;
    }
    if(t && b->cls==14 && (t[12]==8 || t[12]==10 || t[12]==0x22) &&
       b->car_origins==1 && b->car_tracks==1 && !b->car_error && !b->rig_rider_error &&
       !tp_rm_scene_u16(m,0x1d667c) && b->frame_count==1 && b->car_origin.frame==b->drawn_frame){
        uint8_t guests[40];uint16_t ids[40];int n=0,valid=1;
        const uint8_t*track=tp_rm_scene_thing(m,size,(unsigned)b->car_track,1);
        if(!track || track[4]!=18 || track[7]>=extent || track[9]>=extent ||
           t[7]>=extent || t[9]>=extent || t[0x12]>3 ||
           tp_rm_scene_u16(track,0x16)!=b->car_origin.family)valid=0;
        unsigned next=tp_rm_scene_u16(t,0x47);
        while(next && n<40){
            const uint8_t*p=tp_rm_scene_thing(m,size,next,1);
            if(!p || p[4]!=2 || p[0x59]>=8){valid=0;break;}
            ids[n]=(uint16_t)next;guests[n++]=p[0x59];next=tp_rm_scene_u16(p,0x47);
        }
        if(next)valid=0;
        for(int i=0;i<40;i++)if(c->riders[i].present){
            const TPRMRider*r=&c->riders[i];
            if(i>=n || r->variant!=guests[i] || !tp_remaster_car_pose_valid(r->pose,r->visible))valid=0;
            else c->rider_ids[i]=ids[i];
            c->seats[i]=r->variant;
        }
        if(valid){
            const TPRMComponent*p=&b->car_origin;
            tp_rm_scene_position(g,p->x,p->y,&c->x,&c->y);c->z=p->height/40.0f;
            c->frame=p->frame;c->type=p->family;c->rig_ok=1;
            c->vehicle=(TPRMVehicle){p->family,track[0x44],track[0x79],t[0x12],
                track[7],track[9],t[7],t[9],t[6],t[8],(uint8_t)b->car_bounce};
        }
    }
    if(t && b->cls==18 && !tp_rm_scene_u16(m,0x1d667c) && t[12]!=0x13 && t[12]!=0x15 &&
       !b->component_error && b->ncomponents>0 && b->ncomponents==b->frame_count){
        int valid=!b->rig_rider_error;
        for(int i=0;i<40;i++)if(b->rig_riders[i].present)valid=0;
        for(int i=0;i<b->ncomponents;i++)if(b->components[i].family!=tp_rm_scene_u16(t,0x16))valid=0;
        if(valid){
            c->ncomponents=(unsigned)b->ncomponents;
            for(int i=0;i<b->ncomponents;i++){
                const TPRMComponent*p=&b->components[i];TPRMPlacedComponent*q=&c->components[i];
                q->frame=p->frame;q->z=p->height/40.0f;tp_rm_scene_position(g,p->x,p->y,&q->x,&q->y);
            }
        }
    }
    if(idx<0)c->type=tp_rm_scene_u16(m,0x1b44c1+(unsigned)(-idx)*2);
    if((idx<0 || b->cls==24 || ((b->cls==10 || b->cls==2 || b->cls==4 || b->cls==8) && c->rig_ok)) && b->frame_count==1){
        int ground_y=b->frame_y;
        if(t && b->cls==2 && (t[12]==0x0c || t[12]==0x32)){
            /* Keep the native vertical offset as height, not a move across
             * the ground; cell_y works for both pass and per-cell origins. */
            int hires=tp_rm_scene_u16(m,0x1de5f6)==8;
            ground_y=(int16_t)(g->cell_y+(hires?(t[8]>>3)>>1:t[8]>>3));
            c->z=(float)(((ground_y>>g->lo)-((int16_t)b->frame_y>>g->lo))*g->aspect/g->tws);
        }
        tp_rm_scene_position(g,b->frame_x,ground_y,&c->x,&c->y);
        c->frame=(uint16_t)b->drawn_frame;c->rig_ok=1;
    }
    /* Never silently omit an unsupported or malformed animated figure. */
    if(b->figure_count || b->figure_error){c->rig_ok=0;c->ncomponents=0;}
    return c->rig_ok || c->ncomponents;
}
#endif
