/* Source-pose identities shared by native ride capture and authored guest rigs.
 * These high keys are reserved for guest rider poses, not animation timers.
 * Standard: 65024 + mirror/flip * 64 + pose [0,47).
 * Alternate: 65280 + mirror/flip * 4 + pose [0,2).
 * A rig authors the posture in its seat's local space. The ride's seat matrix
 * supplies position/orientation. Mirrored states have explicit pose keys.
 */
#ifndef TP_REMASTER_RIDER_H
#define TP_REMASTER_RIDER_H
#include <stdint.h>
typedef struct {
    uint16_t pose;
    uint8_t variant, present, visible;
} TPRMRider;
static inline uint16_t tp_remaster_rider_key(int pose,int alternate,unsigned flags){
    if((alternate!=0 && alternate!=1) || pose<0 || pose>=(alternate?2:47) ||
       (flags & ~0x83u))return 0;
    return (uint16_t)((alternate?65280:65024)+(flags&3)*(alternate?4:64)+pose);
}
static inline int tp_remaster_rider_key_valid(unsigned key){
    if(key>=65024 && key<65280)return (key-65024)%64<47;
    return key>=65280 && key<65296 && (key-65280)%4<2;
}
static inline int tp_remaster_riders_valid(int seats,const uint8_t expected[40],
                                          const TPRMRider actual[40]){
    if(seats<0 || seats>40)return 0;
    for(int i=0;i<40;i++){
        const TPRMRider*r=&actual[i];
        if(expected[i]==255){if(r->present)return 0;continue;}
        if(i>=seats || expected[i]>=8 || !r->present ||
           r->variant!=expected[i] || r->visible>1 ||
           !tp_remaster_rider_key_valid(r->pose))return 0;
    }
    return 1;
}
#endif
