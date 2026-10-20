/* Pixel-independent native metadata collection shared by normal and Visitor
 * views. It owns no artwork, does not select animations and never writes game
 * memory. One instance belongs to one (possibly nested) source thing. */
#ifndef TP_REMASTER_CAPTURE_H
#define TP_REMASTER_CAPTURE_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "remaster_assembly.h"
#include "remaster_rider.h"
typedef struct {
    int cls,drawn_frame,frame_x,frame_y,frame_count;
    int rig_rider_error;TPRMRider rig_riders[40];
    TPRMComponent components[TP_RM_ASSEMBLY_MAX];int ncomponents,component_error;
    TPRMComponent car_origin;int car_origins,car_error,car_track,car_tracks,car_bounce;
} TPRMCapture;
static inline void tp_remaster_capture_begin(TPRMCapture*s,int cls){
    memset(s,0,sizeof *s);s->cls=cls;
}
static inline uint32_t tp_remaster_capture_u32(const uint8_t*m,size_t at){
    uint32_t value;memcpy(&value,m+at,sizeof value);return value;
}
/* Returns one for a consumed metadata event. Begin/end nesting is the caller's
 * responsibility; native_scene.h describes the normal pass's event protocol. */
static inline int tp_remaster_capture_event(TPRMCapture*s,const uint8_t*m,size_t size,
                                            int kind,int a,int b,int c,uint32_t d,int e){
    if(kind==8){s->drawn_frame=a;s->frame_x=b;s->frame_y=c;s->frame_count++;return 1;}
    if(kind==11){
        uint64_t at=0,last=0;
        if(m && size>=0x1d3b7cu){
            at=(uint64_t)tp_remaster_capture_u32(m,0x1d3b10u)+(unsigned)a*200u;
            last=tp_remaster_capture_u32(m,0x1d3b78u);
        }
        if(!m || size<0x1d3b7cu || s->cls!=0xe || s->car_tracks || b<0 || b>3 ||
           a<1 || a>=16384 || at>last || at+200>size || m[at+4]!=0x12)s->car_error=1;
        else {s->car_track=a;s->car_bounce=b;}
        s->car_tracks++;return 1;
    }
    if(kind==10){
        int height=(int32_t)d;
        if(s->cls!=0xe || s->car_origins || a<1 || a>65535 || e<1 || e>3 ||
           height < -256 || height > 24*256+256)s->car_error=1;
        else s->car_origin=(TPRMComponent){(uint16_t)a,(uint8_t)e,(int16_t)b,(int16_t)c,(int16_t)height};
        s->car_origins++;return 1;
    }
    if(kind==9){
        int height=(int32_t)d;
        if(s->cls!=0x12 || s->ncomponents>=TP_RM_ASSEMBLY_MAX ||
           !tp_remaster_component_valid(a,e,height))s->component_error=1;
        else s->components[s->ncomponents++]=(TPRMComponent){(uint16_t)a,(uint8_t)e,
            (int16_t)b,(int16_t)c,(int16_t)height};
        return 1;
    }
    if(kind==7){
        if(a>=0){
            if(a>=40 || b<0 || b>=8 || s->rig_riders[a].present || d>1 || c<0 || c>65535)s->rig_rider_error=1;
            else s->rig_riders[a]=(TPRMRider){(uint16_t)c,(uint8_t)b,1,(uint8_t)d};
        }
        return 1;
    }
    return 0;
}
#endif
