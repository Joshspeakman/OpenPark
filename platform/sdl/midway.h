/* The launch window's moving fairground: the SDL counterpart of the Mac's TPMidway (platform/macos/launcher.m, "the
 * midway at dusk"; spec docs/parity/launcher.md section 9). Behind the settings: the back fill, the player's park panning
 * at whole pixels, the dusk, two searchlights and the fireworks. Over them: the bunting, the marquee bulbs chasing round
 * the sign, the poster and Play, the glint across the logo and the motes of light.
 *
 * As on the Mac, every sprite is drawn once (draw.c, at the device scale; pennants and bulbs at twice it) onto a few
 * textures; a frame only places, turns, scales and fades them: about a dozen SDL_RenderGeometry calls (each kind of
 * sprite in one call, in the Mac's layer order), no CPU drawing. Needs SDL 2.0.18 or later.
 *
 * Blending: textures hold premultiplied alpha and use the custom blend mode ONE, ONE_MINUS_SRC_ALPHA (colour and alpha),
 * opacity as a vertex colour (o, o, o, o). A renderer that refuses custom blend modes (SDL's software renderer) gets
 * straight-alpha textures and SDL_BLENDMODE_BLEND instead. midway_upload makes other textures (the static view) the
 * same way.
 *
 * Coordinates: the window is 1000 x 700 points, top-down. The renderer draws in device pixels: point p lands on pixel
 * p * scale. Render into the window (or a target of the same size, 1000*scale x 700*scale) with no SDL_RenderSetScale
 * and no SDL_RenderSetLogicalSize. Each call keeps the caller's draw colour, blend mode and clip rect. If the scale
 * changes (the window moves to another display), free the midway and make a new one.
 *
 * Per frame: midway_draw_back(m, t), then the static view's texture, then midway_draw_front(m, t, page_open). One clock:
 * t in seconds, starting at MIDWAY_T0 (the Mac's TP_LAUNCHER_TIME default). Under reduce motion, draw every frame at
 * MIDWAY_T0 and don't advance it (the Mac stops its timer and keeps that frame).
 *
 * Added to the planned interface: midway_set_backdrop (GOG's screenshots: the Mac shows them at 0.6 of their size, "soft",
 * where the player's own park is shown at the smallest whole size 1..4 that leaves room to pan); midway_park_from_shots
 * builds that backdrop from GOG's park screenshots; midway_upload and midway_premultiplied; MIDWAY_T0. The Mac's bulbs
 * don't react to hover or focus (only the clock and an open page move them), so there are no parameters for that. */
#ifndef TP_MIDWAY_H
#define TP_MIDWAY_H
#include <SDL.h>
#include "draw.h"

#define MIDWAY_T0 3.0      /* the clock's start, and the still frame under reduce motion (at 3 s: burst 2 at 0.7 s, no glint) */

typedef struct Midway Midway;
typedef struct { Box sign, poster, play, logo; } MidwayLayout;   /* points, top-down, the 1000x700 window. logo: the box
                                                                  * the logo (or the THEME PARK words) is drawn in; the
                                                                  * glint sweeps across it, clipped to it */

/* scale: device pixels per point. park: the player's park picture (your-park.png), premultiplied; NULL: no picture, the
 * back fill under an opaque dusk (or call midway_set_backdrop with GOG's). Pictures are uploaded: the caller may free
 * them after the call. NULL if the renderer can't make the sprites. */
Midway *midway_new(SDL_Renderer *r, float scale, const Image *park, MidwayLayout lay);
void midway_set_park(Midway *m, const Image *park);             /* another picture of the player's park (or none) */
/* The backdrop, either kind: soft 0 the player's park, soft 1 GOG's park screenshots (midway_park_from_shots); NULL none.
 * The dusk follows (see-through over a picture, opaque without one). */
void midway_set_backdrop(Midway *m, const Image *park, int soft);
void midway_draw_back(Midway *m, double t);                    /* fill, park, dusk, searchlights, fireworks */
void midway_draw_front(Midway *m, double t, int page_open);    /* bunting, bulbs (sign, poster, Play), glint, motes. With
                                                                * a page open the sign's and poster's bulbs, the glint and
                                                                * the motes are hidden; the bunting and Play's bulbs stay */
/* The system's reduce-motion setting: TP_REDUCE_MOTION=1 (or 0, to ignore the system); Windows: "Animation effects" off
 * (SPI_GETCLIENTAREAANIMATION); Linux: GNOME's org.gnome.desktop.interface enable-animations (through gsettings, on
 * GNOME-based desktops), KDE's AnimationDurationFactor=0 (kdeglobals), gtk-enable-animations=0/false in GTK 3's or 4's
 * settings.ini. 1 when set. */
int midway_reduce_motion(void);
void midway_free(Midway *m);

/* A premultiplied RGBA picture (a draw.c Canvas's pixels, w*4 a row) as a texture in the sprites' convention (above),
 * linear filtering. Pass the last result as `reuse` to refresh it (in place when the size matches), e.g. the static view
 * each time it changes. NULL on failure. */
SDL_Texture *midway_upload(Midway *m, const uint8_t *px, int w, int h, SDL_Texture *reuse);
int midway_premultiplied(const Midway *m);                      /* 1: premultiplied + custom blend; 0: straight + BLEND */

/* GOG's park screenshots as one backdrop, as the Mac composes them: up to 4 shots (each drawn at the size of the first)
 * side by side, two rows, the second turned round one place so no picture meets itself (top 0,1,2,3; bottom 1,2,3,0).
 * Premultiplied in and out; NULL if n < 1. Free with img_free. */
Image *midway_park_from_shots(const Image *const *shots, int n);
#endif
