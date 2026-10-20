/* One selection shared by the launcher, park and visitor renderers. */
#ifndef TP_ART_MODE_H
#define TP_ART_MODE_H
#include <stdlib.h>
#include <string.h>
enum { TP_ART_ORIGINAL, TP_ART_FAITHFUL, TP_ART_REIMAGINED };
static inline int tp_art_mode(void) {
    const char *s = getenv("TP_ART_MODE");
    if (s && !strcmp(s, "reimagined")) return TP_ART_REIMAGINED;
    if (s && (!strcmp(s, "faithful3d") || !strcmp(s, "faithful"))) return TP_ART_FAITHFUL;
    return TP_ART_ORIGINAL;
}
#endif
