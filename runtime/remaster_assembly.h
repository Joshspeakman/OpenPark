/* Native-selected modular artwork. A component is an authored frame/pose,
 * never a procedural substitute for missing art. Heights use the original
 * unscaled game units (40 units per canonical world-height unit). */
#ifndef TP_REMASTER_ASSEMBLY_H
#define TP_REMASTER_ASSEMBLY_H
#include <stdint.h>
#define TP_RM_ASSEMBLY_MAX 260
#define TP_RM_COMPONENT_POOL 32768
typedef struct { uint16_t frame; uint8_t family; int16_t x,y,height; } TPRMComponent;
typedef struct { uint16_t frame; float x,y,z; } TPRMPlacedComponent;
static inline int tp_remaster_component_valid(int frame,int family,int height){
    return frame>0 && frame<=65535 && family>=1 && family<=3 &&
           height>=-24 && height<=24*256;
}
#endif
