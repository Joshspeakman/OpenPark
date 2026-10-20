/* World-only baked artwork. UI blits never call this entry point. */
#ifndef TP_REMASTER_ART_H
#define TP_REMASTER_ART_H
#include <stdint.h>
const uint8_t *tp_remaster_source(const uint8_t *memory, uint32_t source,
                                  int width, int height, int lowres);
#endif
