/* Visitor View (experimental): walk through the park at a visitor's height, with authored 3D models
 * drawn natively from the game's own state and art (docs/PLAN.md, "Visitor View").
 *
 * What it rests on (all from the native renderer, native_land.c, which is pixel-exact against
 * the game's):
 * - The park is a 64x64 grid drawn with a plain shear: a cell is tw x th in the game's 640-wide
 *   units ([0x1d663a], [0x1d6654]; 24 x 16 screen pixels), each row shifted tw/3 to the right of
 *   the one above. Cell (X, Y) has its corner at x = tw (X - cx0) + tw/3 (Y - cy0) + C0,
 *   y = th (Y - cy0) for camera (cx0, cy0), C0 = 0x280 - tw ((0x280 - [0x1d6624]) / tw) (from the
 *   landscape loop 0x11b8fe). A shear can be undone exactly, so the ground (each cell's block and
 *   its decorations, as the game draws them) becomes a flat texture, one square per cell.
 * - A thing's place is its cell (bytes 7, 9 of the 200-byte record) and its spot in the cell in
 *   1/256ths (bytes 6, 8): people are drawn at x + tw b6/256 + (tw/3) b8/256, y + th b8/256.
 * - Each thing is drawn by native_land.c into a picture of its own (tp_native_cell_things marks
 *   where each thing starts): that picture, the game's art with its animation, riders in their
 *   seats, carried items and thoughts, supplies the original palette texture for an authored
 *   model (walk_model.h), or the original card if no compatible model exists. P compares cards.
 * - One scale for everything: an art pixel is 1/tws of a cell across and aspect/tws up (320x200's
 *   pixels are 1.2 times as tall as wide on a 4:3 screen), so people, stalls and rides keep the
 *   sizes the artists gave them relative to each other and to the cells. The eye is at a
 *   visitor's height (the median height of the visitors' pictures).
 * - Who rides what: a ride's riders are a chain through word 0x47 (class 4 rides, class 0x0e
 *   cars; class 0x0a holds one), a train's cars a chain through word 0x7a (class 0x12).
 *
 * Layered rides (walk_layer.h) use a fixed authored footprint, source-palette surfaces,
 * poses keyed by native frames, and separately composited riders at world-space foot points.
 * Limits (explicit, see the plan): eight static studies and one layered ride prototype.
 * Hidden surfaces are inferred; guests retain original sprites, including layered riders.
 * The rejected generic extrusion/lofting study is opt-in through TP_WALK_SHELL_STUDY.
 * The visitor AI is not reverse-engineered, so you cannot steer
 * a visitor, buy or queue yourself: you walk as an onlooker, or ride along with a real visitor
 * (follow, hop to another, join someone on a ride) and see what they do and buy. Rides are seen
 * from your seat (cars: from the car; other rides: turning with them); a video plays instead if
 * the app finds one for the ride (platform/macos/main.m).
 *
 * Threads: tp_walk_frame runs on the native game thread at the game's per-frame render (view.c's
 * FRAME_RENDER hook) and makes a snapshot; the app thread moves the camera (tp_walk_input) and
 * draws (tp_walk_render) from the newest snapshot. The snapshot only reads game memory; the one
 * write is the game's camera, kept on the walker so the game's ride music and sounds are those
 * of the rides around you (the renderer picks them from what is on its screen, qol.c). */
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "walk_mesh.h"
#include "walk_model.h"
#include "walk_rig.h"
#include "walk_layer.h"
#include "art_mode.h"
#include "remaster_art.h"
#include "remaster_assembly.h"

uint8_t *tp_host_memory(void);
void tp_host_palette_rgba(uint32_t pal[256]);
extern uint32_t tp_hook_pcs[32];
void tp_hooks_changed(void);
typedef void (*DrawLog)(int kind, int a, int b, int c, uint32_t d, int e, void *ctx);
int tp_native_cell_ground(const uint8_t *mem, int cx, int cy, int gx, int gy, uint8_t *buf, int pitch, int w, int h);
int tp_native_cell_things(const uint8_t *mem, int cx, int cy, int gx, int gy, DrawLog lg, void *ctx);
#include "native_draw.h"
extern const uint8_t *tp_native_text;
extern int tp_native_stable_rng;

#define MAP_PTR     0x1d3b08u
#define MAPN        128                 /* tables sized for the biggest park; the map itself: tp_map_ext square, rows tp_map_w apart */
extern uint32_t tp_map_w, tp_map_ext;
#define MAPE ((int)tp_map_ext)
#define MAPW ((int)tp_map_w)
#define THINGS      0x1d3b10u
#define TILE_W      0x1d663au
#define TILE_H      0x1d6654u
#define VIEW_OFF    0x1d6624u
#define SCREEN_MODE 0x1de5f6u
#define CAM_X       0x1d667au
#define CAM_Y       0x1d6666u
#define LAND_DIRTY  0x1d666eu
#define WEATHER     0x1b14ccu      /* bit 0 rain, 1 particles, 2 storm */
#define RIDE_TYPES  0x1cced0u      /* 29 records of 0xc8: the name first */
#define GHOST_TAB   0x1ba2f8u
#define FRAME_RENDER 0x113b9au
#define NTH         16384          /* room for the largest things pool (bigmap.c) */
#define THING_LAST  0x1d3b78u      /* -> the pool's last record (1200 records, more in a 128x128 park) */
#define GAME_CX0 4
#define GAME_CX1 (MAPE - 13)                         /* (51 for the 64x64 park, as view.c) */
#define GAME_CY1 (MAPE - 13)

static const uint8_t *M;
static int r16(uint32_t a) { int16_t v; memcpy(&v, M + a, 2); return v; }
static uint32_t r32(uint32_t a) { uint32_t v; memcpy(&v, M + a, 4); return v; }
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

/* ---- geometry of the game's screen mode ---- */
typedef struct { int lo, tw, th, sw, wh; double tws, ths, shear, aspect, c0; } Geom;
static int geom(Geom *g) {
    g->tw = (uint16_t)r16(TILE_W); g->th = (uint16_t)r16(TILE_H);
    if (g->tw < 6 || g->th < 2 || g->tw > 128 || g->th > 128) return 0;
    int mode = r16(SCREEN_MODE); g->lo = mode & 1;
    if (!g->lo && mode != 8) return 0;
    g->tws = g->tw >> g->lo; g->ths = g->th >> g->lo; g->shear = (g->tw / 3) >> g->lo;
    g->aspect = g->lo ? 1.2 : 1.0;                         /* 320x200 on a 4:3 screen: tall pixels */
    g->sw = g->lo ? 320 : 640; g->wh = g->lo ? 178 : 436;
    int off = r16(VIEW_OFF);
    g->c0 = (0x280 - g->tw * ((0x280 - off) / g->tw)) >> g->lo;
    return 1;
}

/* ---- the snapshot ---- */
#define TMAX 32                     /* floor texels per cell, at most */
#define MAXCARDS 16384
#define POOL (12u << 20)            /* source pixels (16-bit: 0xffff transparent), including large parks */
#define MAXCONT 256
#define SHR 8                       /* shadow samples per cell */
typedef struct { float x, y, r, rig_x, rig_y, rig_z; uint16_t w, h, ax, ay, frame; uint32_t off; int16_t idx, type;
    uint8_t cls, big, shape, rig_ok, seats[40], layer_ok, nriders; LPRider riders[40];
    TPRMRider rig_riders[40]; uint16_t rider_ids[40]; uint32_t component_first; uint16_t ncomponents; TPRMVehicle vehicle; } Card;
typedef struct { int16_t idx, rider; uint8_t cls; float x, y; } Cont;
typedef struct {
    int valid; uint32_t serial; Geom g; uint32_t pal[256]; int fi, T; uint8_t weather;
    Card cards[MAXCARDS]; int ncards;
    TPRMPlacedComponent components[TP_RM_COMPONENT_POOL]; unsigned ncomponents;
    uint16_t pix[POOL]; uint32_t npix;
    uint8_t blocked[MAPN * MAPN];
    uint8_t shade[MAPN * SHR * MAPN * SHR];   /* contact shadows on the floor (SHR samples a cell, 0..255 dark) */
    int16_t riding[NTH];            /* rider -> the ride, car or train it is on (0: none) */
    Cont conts[MAXCONT]; int nconts;
    float eye;                      /* a visitor's eye height, cells */
    /* the visitor followed (0: none) */
    int f_idx, f_ok, f_cont, f_cls, f_type; float f_x, f_y, f_elev; char f_name[40];
} Snap;
static Snap snaps[2]; static int front = -1;
static uint8_t floor_tex[2][MAPN * TMAX * MAPN * TMAX], off_tex[2][TMAX * TMAX];
static uint16_t floor_key[2][MAPN * MAPN]; static uint32_t floor_geo[2]; static int floor_valid[2];   /* each cell's ground as drawn */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

/* ---- the walker (app thread; under the lock) ---- */
enum { MODE_STREET, MODE_DIORAMA };
static struct {
    int on, mode, noclip, follow, placed, mesh;
    double x, y, yaw, pitch, head;  /* free walk: position (cells), heading; looking: pitch, head turn when following */
    double cam_x, cam_y, cam_z, cam_yaw, cam_pitch;   /* the camera as last moved */
    double fx, fy, fyaw, orbit, ride_t; uint32_t fserial; int have_f;   /* following: smoothed place and heading */
    double hscale;                  /* TP_WALK_SCALE: everything's size (calibration), 1 = the art's */
} W = {0};
static const double ART_YAW = -1.2490457723982544;   /* atan2(-1, 1/3): the overhead view looks this way */
static char note[128]; static double note_until;

static void set_note(const char *s) { snprintf(note, sizeof note, "%s", s); note_until = now() + 3; }

/* ---- floor ---- */
static uint8_t ga[96 * 160], gb[96 * 160];
/* One cell's ground into a T x T texture: the cell's parallelogram (corner (0, 0), top edge tws across,
 * the bottom edge shear further right, ths down), sampled at texel centres. */
static void cell_texture(const Geom *g, int cx, int cy, uint8_t *dst, int dpitch, int T, uint8_t fallback) {
    int W2 = (int)(g->tws + g->shear) + 16, H2 = (int)g->ths * 4 + 16, ox = 4, oy = H2 - (int)g->ths - 8;
    if (W2 > 96) W2 = 96; if (H2 > 160) H2 = 160;
    memset(ga, 0, (size_t)(W2 * H2)); memset(gb, 0xff, (size_t)(W2 * H2));
    tp_native_cell_ground(M, cx, cy, ox << g->lo, oy << g->lo, ga, W2, W2, H2);
    tp_native_cell_ground(M, cx, cy, ox << g->lo, oy << g->lo, gb, W2, W2, H2);
    for (int j = 0; j < T; j++) for (int i = 0; i < T; i++) {
        double u = (i + 0.5) / T, v = (j + 0.5) / T;
        int sx = (int)(ox + u * g->tws + v * g->shear), sy = (int)(oy + v * g->ths);
        uint8_t c = fallback;                               /* (the art's edges are jagged: half a pixel of shear a row) */
        for (int k = 0; k < 3; k++) {
            int xx = sx + (k == 1 ? (i < T / 2 ? 1 : -1) : k == 2 ? (i < T / 2 ? -1 : 1) : 0);
            if (xx >= 0 && xx < W2 && sy >= 0 && sy < H2 && ga[sy * W2 + xx] == gb[sy * W2 + xx]) { c = ga[sy * W2 + xx]; break; }
        }
        dst[j * dpitch + i] = c;
    }
}
/* Floor fi brought up to date with the map: the cells whose ground (blocks byte 2, decorations byte 3)
 * changed since that floor was drawn, or all of them when the art or the mode did. */
static void build_floor(const Geom *g, int fi, int T, uint32_t geo, const uint16_t *key) {
    uint8_t *ft = floor_tex[fi], *ot = off_tex[fi];
    int all = !floor_valid[fi] || floor_geo[fi] != geo;
    if (all) cell_texture(g, -1, -1, ot, T, T, 0);
    uint8_t grass = ot[(T / 2) * T + T / 2];
    for (int i = 0; i < MAPN * MAPN; i++) {
        if (i % MAPN >= MAPE || i / MAPN >= MAPE || (!all && floor_key[fi][i] == key[i])) continue;
        cell_texture(g, i % MAPN, i / MAPN, ft + (size_t)(i / MAPN) * T * MAPN * T + (size_t)(i % MAPN) * T, MAPN * T, T, grass);
        floor_key[fi][i] = key[i];
    }
    floor_geo[fi] = geo; floor_valid[fi] = 1;
}

/* ---- things into cards ---- */
#define SW_ 640
#define SH_ 480
#define OXS 200                     /* the cell's corner in the scratch picture (screen pixels) */
#define OYS 320
static uint8_t sa[SW_ * SH_], sb[SW_ * SH_], ta[0x280 * 480], tb[0x280 * 480];
typedef struct {
    Snap *s; const Geom *g; int cx, cy;
    int idx, cls, colour, any; int x0, y0, x1, y1;      /* the thing being drawn and what it touched */
    NDTarget a, b;
    int rider_slot, rider_variant, nriders, rider_error; LPRider riders[40];
    int rig_rider_error; TPRMRider rig_riders[40];
    int drawn_frame, frame_x, frame_y, frame_count;
    TPRMComponent components[TP_RM_ASSEMBLY_MAX]; int ncomponents, component_error;
    TPRMComponent car_origin; int car_origins, car_error, car_track, car_tracks, car_bounce;
} Build;
static void touch(Build *B, int x0, int y0, int x1, int y1) {
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 > SW_) x1 = SW_; if (y1 > SH_) y1 = SH_;
    if (x0 >= x1 || y0 >= y1) return;
    if (!B->any) { B->x0 = x0; B->y0 = y0; B->x1 = x1; B->y1 = y1; B->any = 1; return; }
    if (x0 < B->x0) B->x0 = x0; if (y0 < B->y0) B->y0 = y0; if (x1 > B->x1) B->x1 = x1; if (y1 > B->y1) B->y1 = y1;
}
static int known_place(const uint8_t *t, int cls) {      /* things placed in their cell by bytes 6 and 8 */
    int sub = t[0xc];
    return cls == 2 || cls == 8 || cls == 0x0a || cls == 0x0e || (cls == 0x14 && sub == 2)
        || (cls == 4 && (sub == 2 || sub == 4 || sub == 6 || sub == 0x1e || sub == 0x26));
}
static void finish(Build *B) {
    if (!B->any) return;
    Snap *s = B->s; const Geom *g = B->g;
    int x0 = SW_, y0 = SH_, x1 = -1, y1 = -1;
    for (int y = B->y0; y < B->y1; y++) for (int x = B->x0; x < B->x1; x++)
        if (sa[y * SW_ + x] == sb[y * SW_ + x]) { if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; }
    int w = x1 - x0 + 1, h = y1 - y0 + 1;
    if (B->idx && x1 >= 0 && s->ncards < MAXCARDS && s->npix + (uint32_t)(w * h) <= POOL) {
        Card *c = &s->cards[s->ncards];
        uint16_t *p = s->pix + s->npix;
        for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
            int i = (y0 + y) * SW_ + x0 + x;
            p[y * w + x] = sa[i] == sb[i] ? sa[i] : 0xffff;
        }
        /* the foot: where the thing stands (people and the like: their place; others: the middle of
         * the picture's bottom, its footprint's front edge, moved back half the footprint's depth) */
        double fx, fy, wx, wy; int big = 0;
        const uint8_t *t = B->idx > 0 ? M + r32(THINGS) + (uint32_t)B->idx * 0xc8 : NULL;
        if (t && known_place(t, B->cls)) {
            fx = OXS + g->tws * t[6] / 256.0 + g->shear * t[8] / 256.0;
            wx = B->cx + t[6] / 256.0; wy = B->cy + t[8] / 256.0;
            /* rides and shops placed this way stand at their footprint's front edge (the picture's
             * bottom); their middle is half a footprint behind (the footprint: about as deep as the
             * picture is wide) */
            double depth = w / (g->tws + g->shear);
            if ((B->cls == 4 || B->cls == 0x0a) && depth >= 1.5) {
                depth = floor(depth + 0.5); if (depth > 6) depth = 6;
                wy -= depth / 2; big = 1; c->r = (float)(depth / 2);
            }
        } else {
            long sum = 0, n = 0;
            for (int y = h - 3 < 0 ? 0 : h - 3; y < h; y++) for (int x = 0; x < w; x++) if (p[y * w + x] != 0xffff) { sum += x; n++; }
            fx = x0 + (n ? (double)sum / n : w / 2.0) + 0.5;
            double v = (y1 + 1 - OYS) / g->ths, u = (fx - OXS - v * g->shear) / g->tws;
            double depth = w / (g->tws + g->shear);          /* a square footprint as wide as the picture */
            if (depth > 6) depth = 6;
            if (depth >= 1.5) { v -= depth / 2; big = 1; c->r = (float)(depth / 2); }
            wx = B->cx + u; wy = B->cy + v;
        }
        fy = y1 + 1;
        if (!big) c->r = 0;
        c->x = (float)wx; c->y = (float)wy; c->w = (uint16_t)w; c->h = (uint16_t)h;
        c->rig_x=c->x;c->rig_y=c->y;c->rig_z=0;
        double ax = fx - x0; c->ax = (uint16_t)(ax < 0 ? 0 : ax > w ? w : ax); c->ay = (uint16_t)(fy - y0);
        c->off = s->npix; c->idx = (int16_t)B->idx; c->cls = (uint8_t)(B->idx < 0 ? 0xff : B->cls); c->big = (uint8_t)big;
        c->type = -1; c->shape = WM_SMALL;
        c->frame=0;c->rig_ok=0;c->layer_ok=0;c->nriders=(uint8_t)B->nriders;
        c->component_first=0;c->ncomponents=0;
        memcpy(c->riders,B->riders,(size_t)B->nriders*sizeof(LPRider));memset(c->seats,255,sizeof c->seats);
        memcpy(c->rig_riders,B->rig_riders,sizeof c->rig_riders);memset(c->rider_ids,0,sizeof c->rider_ids);
        if(t) {
            uint32_t at=(uint32_t)(t-M);c->frame=(uint16_t)r16(at+10);
            if(B->cls==2) {
                c->type=t[0x59];
                /* The native common path supplies one complete body frame.
                 * Bind its actual key/origin for walking and reactions alike.
                 * Substituted riders and attached overlays require separate
                 * composite placement, not a body-only rig. Vertical offsets
                 * on the direct/common paths are separated below. */
                c->rig_ok=c->type<8 &&
                    B->frame_count==1 && B->drawn_frame==c->frame &&
                    !r16(at+0x52) && !t[0xb9] &&
                    !((M[0x1b14cc]&7) && (t[5]&0x80)) && B->idx!=(uint16_t)r16(0x1d6678);
            } else if(B->cls==8) {
                c->type=t[0x59];
                /* Staff IDs include Shark Man at zero and Guard at eight.
                 * Job/selection/special overlays add native frames; a body
                 * pose alone must not consume those composite states. */
                c->rig_ok=c->type<9 && B->frame_count==1 && B->drawn_frame==c->frame;
            }
        }
        if (t && B->cls == 4) {
            int type = (uint16_t)r16((uint32_t)(t-M)+0x16)/0xc8;
            if (type<29) c->type=(int16_t)type;
            if(type<29) {
                uint32_t at=(uint32_t)(t-M),rec=0x1cced0+(uint32_t)type*200;
                int cap=(uint16_t)r16(rec+0xa6),kind=M[rec+0xb3],n=0;
                c->layer_ok=!B->rider_error && (kind==0 || kind==1 || kind==5 || kind==7 || kind==9) && (t[12]==2 || t[12]==4 || t[12]==6) && (uint16_t)r16(at+0x2e)<M[rec+0xa8];
                uint8_t guests[36];uint32_t pool=r32(THINGS),last=r32(THING_LAST);
                int next=(uint16_t)r16(at+0x47),valid=1;
                while(next && n<36) {
                    uint64_t p=(uint64_t)pool+(unsigned)next*200;
                    if(next>=NTH || p>last || p+200>(64u<<20) || M[p+4]!=2){valid=0;break;}
                    guests[n++]=M[p+0x59];next=(uint16_t)r16((uint32_t)p+0x47);
                }
                if(next)valid=0;
                /* The castle's loading/unloading dispatch draws its body
                 * without substituted riders until word 0x32 is set. Linked
                 * guests alone do not mean occupied attachment geometry. */
                int seated_n=(kind==9 && !r16(at+0x32))?0:n;
                c->rig_ok=valid && !B->rig_rider_error &&
                    B->frame_count==1 && B->drawn_frame==c->frame &&
                    (t[12]==2 || t[12]==4 || t[12]==6) &&
                    /* The overlay key can stay populated while inactive.
                     * Native speed dispatch and frame_count decide whether
                     * an overlay was actually drawn. */
                    (uint16_t)r16(at+0x2e)<M[rec+0xa8] &&
                    rm_seat_order(guests,seated_n,cap,kind,r32(at+0x20),c->seats);
            }
            c->shape=WM_ROUND;
            if(type==0 || type==1 || type==5 || type==6 || type==14 || (type>=22 && type<=25)) c->shape=WM_BOX;
            if(type==2 || type==3 || type==10 || type==11 || type==12 || type==15 || type==17 || type==19 || type==21 || type==26 || type==28) c->shape=WM_OPEN;
            if(type==4) c->shape=WM_GROUND;
        } else if (B->cls==0xa || B->cls==0x14 || B->cls==6 || B->cls==0x1c) {
            c->shape=WM_BOX;
            if(t && B->cls==0xa) {
                uint32_t at=(uint32_t)(t-M);
                int offset=(uint16_t)r16(at+0x16),type=offset/107;
                if(type<17 && offset%107==0)c->type=(int16_t)type;
                /* Every actually drawn customer part needs the actual guest
                 * and source posture. A linked but undrawn guest adds no body.
                 * The render path additionally requires all attachment poses
                 * and enough authored seat matrices before consuming the card. */
                unsigned customer=(uint16_t)r16(at+0x47);
                uint64_t person=(uint64_t)r32(THINGS)+customer*200u;
                int valid=!B->rig_rider_error && r16(0x1d667cu)!=8;
                if(customer && (customer>=NTH || person>r32(THING_LAST) ||
                   person+200>(64u<<20) || M[person+4]!=2 || M[person+0x59]>=8))valid=0;
                for(int i=0;i<40;i++)if(c->rig_riders[i].present){
                    if(!valid || !customer || c->rig_riders[i].variant!=M[person+0x59] ||
                       !tp_remaster_shop_key_valid(c->rig_riders[i].pose)){valid=0;break;}
                    c->seats[i]=c->rig_riders[i].variant;
                }
                c->rig_ok=c->type>=0 && (t[12]==2 || t[12]==4) &&
                    valid && !r16(at+0x5f) && B->frame_count==1;
            }
        }
        else if (B->cls==0x12 || B->cls==0xe) c->shape=WM_TRACK;
        else if (B->idx<0 || B->cls==0x18) c->shape=WM_TREE;
        if(t && B->cls==0xe && (t[12]==8 || t[12]==0xa || t[12]==0x22) &&
           B->car_origins==1 && B->car_tracks==1 && !B->car_error && !B->rig_rider_error &&
           !r16(0x1d667cu) && B->frame_count==1 && B->car_origin.frame==B->drawn_frame){
            uint8_t guests[40];uint16_t ids[40];int n=0,valid=1;
            const uint8_t*track=M+r32(THINGS)+(unsigned)B->car_track*200u;
            if(track[7]>=MAPE || track[9]>=MAPE || t[7]>=MAPE || t[9]>=MAPE || t[0x12]>3 ||
               (uint16_t)r16((uint32_t)(track-M)+0x16)!=B->car_origin.family)valid=0;
            unsigned next=(uint16_t)r16((uint32_t)(t-M)+0x47);
            while(next && n<40){
                uint64_t person=(uint64_t)r32(THINGS)+next*200u;
                if(next>=NTH || person>r32(THING_LAST) || person+200>(64u<<20) ||
                   M[person+4]!=2 || M[person+0x59]>=8){valid=0;break;}
                ids[n]=(uint16_t)next;guests[n++]=M[person+0x59];next=(uint16_t)r16((uint32_t)person+0x47);
            }
            if(next)valid=0;
            for(int i=0;i<40;i++)if(c->rig_riders[i].present){
                const TPRMRider *r=&c->rig_riders[i];
                if(i>=n || r->variant!=guests[i] || !tp_remaster_car_pose_valid(r->pose,r->visible))valid=0;
                else c->rider_ids[i]=ids[i];
                c->seats[i]=r->variant;
            }
            if(valid){
                const TPRMComponent *p=&B->car_origin;
                double vy=((p->y>>g->lo)-OYS)/g->ths;
                double vx=((p->x>>g->lo)-OXS-vy*g->shear)/g->tws;
                c->rig_x=(float)(B->cx+vx);c->rig_y=(float)(B->cy+vy);c->rig_z=p->height/40.0f;
                c->frame=p->frame;c->type=p->family;c->rig_ok=1;
                c->vehicle=(TPRMVehicle){p->family,track[0x44],track[0x79],t[0x12],
                    track[7],track[9],t[7],t[9],t[6],t[8],(uint8_t)B->car_bounce};
            }
        }
        if(t && B->cls==0x12 && !r16(0x1d667cu) && t[12]!=0x13 && t[12]!=0x15 &&
           !B->component_error && B->ncomponents>0 && B->ncomponents==B->frame_count &&
           s->ncomponents<=TP_RM_COMPONENT_POOL-(unsigned)B->ncomponents){
            int valid=!B->rig_rider_error;
            for(int i=0;i<40;i++)if(B->rig_riders[i].present)valid=0;
            for(int i=0;i<B->ncomponents;i++)
                if(B->components[i].family!=(uint16_t)r16((uint32_t)(t-M)+0x16))valid=0;
            if(valid){
                c->component_first=s->ncomponents;c->ncomponents=(uint16_t)B->ncomponents;
                for(int i=0;i<B->ncomponents;i++){
                    const TPRMComponent *p=&B->components[i];
                    double vy=((p->y>>g->lo)-OYS)/g->ths;
                    double vx=((p->x>>g->lo)-OXS-vy*g->shear)/g->tws;
                    s->components[s->ncomponents++]=(TPRMPlacedComponent){p->frame,
                        (float)(B->cx+vx),(float)(B->cy+vy),p->height/40.0f};
                }
            }
        }
        if(B->idx<0) {
            int animation=(uint16_t)r16(0x1b44c1+(uint32_t)(-B->idx)*2);
            c->type=(int16_t)animation; /* stable scenery key for authored models */
            if(animation==431) c->shape=WM_GROUND; /* lake */
            if(animation==427 || animation==428 || animation==429 || animation==434) c->shape=WM_BOX;
        }
        if((B->idx<0 || B->cls==0x18 || ((B->cls==0xa || B->cls==2 || B->cls==4 || B->cls==8) && c->rig_ok)) && B->frame_count==1) {
            /* The model is authored around the native frame origin. The old
             * card's cropped bottom is only a billboard placement heuristic. */
            double sy=((int16_t)B->frame_y>>g->lo)-OYS;
            double sx=((int16_t)B->frame_x>>g->lo)-OXS;
            if(t && B->cls==2 && (t[12]==0x0c || t[12]==0x32)){
                /* Native bench/elevated states raise the frame on screen.
                 * Preserve that as height, not a slide across the ground. */
                int hires=r16(SCREEN_MODE)==8;
                int base_y=((OYS<<g->lo)+(hires?(t[8]>>3)>>1:t[8]>>3))>>g->lo;
                c->rig_z=(float)((base_y-(((int16_t)B->frame_y)>>g->lo))*g->aspect/g->tws);
                sy=base_y-OYS;
            }
            double vy=sy/g->ths,vx=(sx-vy*g->shear)/g->tws;
            c->rig_x=(float)(B->cx+vx);c->rig_y=(float)(B->cy+vy);
            c->frame=(uint16_t)B->drawn_frame;c->rig_ok=1;
        }
        const LPLayer*layer=c->layer_ok?lp_find(c->cls,c->type):NULL;
        if(layer && (!lp_pose(layer,c->frame)||!lp_valid_riders(layer,c->riders,c->nriders)))layer=NULL;
        if(layer && t){
            // A fixed authored footprint, never the current animation crop.
            wx=B->cx+t[6]/256.0+layer->ox;wy=B->cy+t[8]/256.0+layer->oy;
            c->x=(float)wx;c->y=(float)wy;c->r=fmaxf(layer->hx,layer->hy);c->big=1;
        }
        if(B->idx>=0 && B->cls!=0x18 && !((B->cls==0xa || B->cls==0xe || B->cls==2 || B->cls==4 || B->cls==8) && c->rig_ok)){c->rig_x=c->x;c->rig_y=c->y;}
        s->npix += (uint32_t)(w * h); s->ncards++;
        /* cells the walker cannot enter: rides, shops and scenery (their estimated footprint) */
        if (B->idx < 0 || B->cls == 4 || B->cls == 0x12) {
            double d = B->idx < 0 ? 1 : w / (g->tws + g->shear);
            if (d < 1) d = 1; if (d > 6) d = 6;
            for (int yy = (int)floor(wy - (layer?layer->hy:d/2) + 0.25); yy <= (int)floor(wy + (layer?layer->hy:d/2) - 0.25); yy++)
                for (int xx = (int)floor(wx - (layer?layer->hx:d/2) + 0.25); xx <= (int)floor(wx + (layer?layer->hx:d/2) - 0.25); xx++)
                    if (xx >= 0 && xx < MAPE && yy >= 0 && yy < MAPE) s->blocked[yy * MAPN + xx] = 1;
        }
    }
    for (int y = B->y0; y < B->y1; y++) { memset(sa + y * SW_ + B->x0, 0, (size_t)(B->x1 - B->x0)); memset(sb + y * SW_ + B->x0, 0xff, (size_t)(B->x1 - B->x0)); }
    B->any = 0;B->nriders=0;B->rider_error=0;B->rider_slot=-1;
}
static int parts_log;                                    /* development: TP_WALK_PARTS logs one snapshot's sprites */
static void card_log(int kind, int a, int b, int c, uint32_t d, int e, void *ctx) {
    Build *B = ctx; int lo = B->g->lo;
    if (kind == 6) { finish(B); B->idx = a; B->cls = b; B->colour = 0; B->rider_slot=-1;B->nriders=0;B->rider_error=0;B->frame_count=0;
        B->ncomponents=0;B->component_error=0;
        B->car_origins=0;B->car_error=0;B->car_tracks=0;B->car_track=0;B->car_bounce=0;
        B->rig_rider_error=0;memset(B->rig_riders,0,sizeof B->rig_riders);return; }
    if (kind == 8) { B->drawn_frame=a;B->frame_x=b;B->frame_y=c;B->frame_count++;return; }
    if(kind==11){
        uint64_t at=(uint64_t)r32(THINGS)+(unsigned)a*200u;
        if(B->cls!=0xe || B->car_tracks || b<0 || b>3 || a<1 || a>=NTH || at>r32(THING_LAST) ||
           at+200>(64u<<20) || M[at+4]!=0x12)B->car_error=1;
        else {B->car_track=a;B->car_bounce=b;}
        B->car_tracks++;return;
    }
    if(kind==10){
        int height=(int32_t)d;
        if(B->cls!=0xe || B->car_origins || a<1 || a>65535 || e<1 || e>3 ||
           height < -256 || height > 24*256+256)B->car_error=1;
        else B->car_origin=(TPRMComponent){(uint16_t)a,(uint8_t)e,(int16_t)b,(int16_t)c,(int16_t)height};
        B->car_origins++;return;
    }
    if(kind==9){
        int height=(int32_t)d;
        if(B->cls!=0x12 || B->ncomponents>=TP_RM_ASSEMBLY_MAX ||
           !tp_remaster_component_valid(a,e,height))B->component_error=1;
        else B->components[B->ncomponents++]=(TPRMComponent){(uint16_t)a,(uint8_t)e,
            (int16_t)b,(int16_t)c,(int16_t)height};
        return;
    }
    if (kind == 5) { B->colour = c; return; }
    if (kind == 7) {
        B->rider_slot=a;B->rider_variant=b;
        if(a>=0){
            if(a>=40 || b<0 || b>=8 || B->rig_riders[a].present || d>1)B->rig_rider_error=1;
            else B->rig_riders[a]=(TPRMRider){(uint16_t)c,(uint8_t)b,1,(uint8_t)d};
        }
        return;
    }
    if (kind == 2) {
        int w = (c & 0xff) >> lo, h = (c >> 8) >> lo, x = (int16_t)a >> lo, y = (int16_t)b >> lo;
        int bounds[4];
        tp_native_draw_world_sprite(&B->a,M,d,c&255,c>>8,a,b,e,B->colour,bounds);
        tp_native_draw_world_sprite(&B->b,M,d,c&255,c>>8,a,b,e,B->colour,NULL);
        touch(B,bounds[0]-2,bounds[1]-2,bounds[2]+2,bounds[3]+2);
        if(B->cls==4 && B->rider_slot>=0 && lp_find(4,(uint16_t)r16(r32(THINGS)+(uint32_t)B->idx*200+22)/200)) {
            static uint8_t ra[128*128],rb[128*128];
            if(w<1 || h<1) { /* Native empty rider pose: preserve its absence. */ }
            else if(B->nriders>=40 || B->rider_slot>=40 || w>128 || h>128 || B->s->npix+(uint32_t)w*h>POOL)B->rider_error=1;
            else {
                memset(ra,0,sizeof ra);memset(rb,255,sizeof rb);
                NDTarget rt={ra,sizeof ra,0,128,128,128,127,lo,M+r32(GHOST_TAB),0};
                tp_native_draw_sprite(&rt,M+d,c&255,c>>8,0,0,e,B->colour);rt.buf=rb;
                tp_native_draw_sprite(&rt,M+d,c&255,c>>8,0,0,e,B->colour);
                LPRider*q=&B->riders[B->nriders++];*q=(LPRider){B->s->npix,(uint16_t)w,(uint16_t)h,(uint8_t)B->rider_slot,(uint8_t)B->rider_variant};
                for(int yy=0;yy<h;yy++)for(int xx=0;xx<w;xx++)B->s->pix[B->s->npix++]=ra[yy*128+xx]==rb[yy*128+xx]?ra[yy*128+xx]:256;
            }
        }
        if (parts_log) fprintf(stderr, "walk part: thing %d cls %d cell %d,%d sprite %d,%d %dx%d at %06x flags %x\n", B->idx, B->cls, B->cx, B->cy, x - OXS, y - OYS, w, h, d, e);
        B->colour = 0; return;
    }
    if (kind == 4 && tp_native_text) {                     /* the small font: written at its own pitch, then copied */
        int pitch = lo ? 0x140 : 0x280, len = (int)strnlen((const char *)tp_native_text, 32);
        NDTarget t1 = {ta, sizeof ta, 0, pitch, pitch, 480, pitch - 1, lo, NULL, 0}, t2 = t1; t2.buf = tb;
        int x = (uint16_t)a >> lo, y = (uint16_t)b >> lo, x1 = x + len * 4 + 2, y1 = y + 7;
        if (x < 0 || y < 0 || x1 > pitch || y1 > 480) return;
        for (int yy = y; yy < y1; yy++) { memset(ta + yy * pitch + x, 0, (size_t)(x1 - x)); memset(tb + yy * pitch + x, 0xff, (size_t)(x1 - x)); }
        tp_native_draw_small_text(&t1, M, a, b, tp_native_text, c);
        tp_native_draw_small_text(&t2, M, a, b, tp_native_text, c);
        for (int yy = y; yy < y1 && yy < SH_; yy++) for (int xx = x; xx < x1 && xx < SW_; xx++)
            if (ta[yy * pitch + xx] == tb[yy * pitch + xx]) { sa[yy * SW_ + xx] = sb[yy * SW_ + xx] = ta[yy * pitch + xx]; }
        touch(B, x, y, x1, y1);
    }
}

/* ---- riders ---- */
static int nth(void) {
    uint32_t t = r32(THINGS), l = r32(THING_LAST); int n = t && l > t ? (int)((l - t) / 0xc8) + 1 : 1200;
    return n < NTH ? n : NTH;
}
static int valid_idx(int i) { return i > 0 && i < nth(); }
static void find_riders(Snap *s) {
    memset(s->riding, 0, sizeof s->riding); s->nconts = 0;
    uint32_t things = r32(THINGS);
    for (int i = 1, n = nth(); i < n; i++) {
        const uint8_t *t = M + things + (uint32_t)i * 0xc8; int cls = t[4], first = 0;
        if (cls == 4 || cls == 0x0e) {
            int r = r16(things + (uint32_t)i * 0xc8 + 0x47);
            for (int k = 0; valid_idx(r) && k < 40; k++) {
                if (!s->riding[r]) s->riding[r] = (int16_t)i;
                if (!first) first = r;
                r = r16(things + (uint32_t)r * 0xc8 + 0x47);
            }
        } else if (cls == 0x0a) {
            int r = r16(things + (uint32_t)i * 0xc8 + 0x47);
            if (valid_idx(r)) { if (!s->riding[r]) s->riding[r] = (int16_t)i; first = r; }
        } else if (cls == 0x12) {                            /* a train: its cars' riders */
            int car = r16(things + (uint32_t)i * 0xc8 + 0x7a);
            for (int k = 0; valid_idx(car) && k < 40; k++) {
                int r = r16(things + (uint32_t)car * 0xc8 + 0x47);
                if (valid_idx(r)) { if (!s->riding[r]) s->riding[r] = (int16_t)i; if (!first) first = r; }
                car = r16(things + (uint32_t)car * 0xc8 + 0x7a);
            }
        }
        if (first && s->nconts < MAXCONT) {
            double ox = cls == 0x12 ? 0.5 : t[6] / 256.0, oy = cls == 0x12 ? 0.5 : t[8] / 256.0;
            s->conts[s->nconts++] = (Cont){(int16_t)i, (int16_t)first, (uint8_t)cls, (float)(t[7] + ox), (float)(t[9] + oy)};
        }
    }
    /* Car chains can include guests subsequently consumed by later cars.
     * Native seat occurrences identify the actual container, not chain order. */
    for(int k=0;k<s->ncards;k++){
        const Card*c=&s->cards[k];if(c->cls!=14||!c->rig_ok)continue;
        for(int seat=0;seat<40;seat++)if(c->rider_ids[seat]&&c->rider_ids[seat]<NTH)
            s->riding[c->rider_ids[seat]]=c->idx;
    }
}
/* The ride type (0..28) of a container, or -1: rides keep the type record's offset in word 0x16;
 * a car keeps its ride's thing there. */
static int ride_type(int i) {
    uint32_t things = r32(THINGS), t = things + (uint32_t)i * 0xc8;
    int cls = M[t + 4], off = (uint16_t)r16(t + 0x16);
    if (cls == 0x0e || cls == 0x0a) {
        if (!valid_idx(off) || M[things + (uint32_t)off * 0xc8 + 4] != 4) return -1;
        t = things + (uint32_t)off * 0xc8; off = (uint16_t)r16(t + 0x16); cls = 4;
    }
    if (cls != 4 || off % 0xc8 || off / 0xc8 >= 29) return -1;
    return off / 0xc8;
}
/* How high a car rides (cells): stacked rides lift their cars 24 game units a level (classE, 0x11feb5). */
static double car_lift(const Geom *g, int i) {
    uint32_t things = r32(THINGS), t = things + (uint32_t)i * 0xc8, map = r32(MAP_PTR);
    int cls = M[t + 4], units = 0;
    if (cls == 0x12) units = (M[t + 0x79] + 1) * 0x18;
    else if (cls == 0x0e && M[t + 7] < MAPE && M[t + 9] < MAPE) {
        for (int k = r16(map + (uint32_t)((M[t + 9] * MAPW + M[t + 7]) * 4)), n = 0; valid_idx(k) && n < 64; k = r16(things + (uint32_t)k * 0xc8 + 2), n++)
            if (M[things + (uint32_t)k * 0xc8 + 4] == 0x12) { uint32_t rd = things + (uint32_t)k * 0xc8; units = r16(rd + 0x16) == 3 ? 0x18 : M[rd + 0x79] * 0x18; break; }
    }
    return units / 2.0 * g->aspect / g->tws;               /* (both modes: half the units on screen) */
}

/* Survey (TP_WALK_WATCH=1): the bytes of the followed visitor's record (and of the ride it is on) that
 * change from one snapshot to the next, with the game's tick, to find what steering and buying would
 * need (its heading, state, target, cash, needs): docs/PLAN.md, "Visitor View", next steps. */
static void watch(int idx, const uint8_t *t, int cont) {
    static int on = -1; if (on < 0) on = getenv("TP_WALK_WATCH") != NULL;
    if (!on) return;
    static uint8_t prev[0xc8], prevc[0xc8]; static int pidx, pcont; static long lines;
    if (idx != pidx) { memcpy(prev, t, 0xc8); pidx = idx; fprintf(stderr, "walk watch: following thing %d (class %d, kind %d)\n", idx, t[4], t[0x59]); }
    if (lines > 20000) return;
    char line[2048]; int n = 0;
    for (int i = 0; i < 0xc8 && n < 1900; i++) if (t[i] != prev[i]) n += snprintf(line + n, sizeof line - (size_t)n, " +%02x:%02x>%02x", i, prev[i], t[i]);
    if (n) { fprintf(stderr, "walk watch: tick %u visitor%s\n", r32(0x1d3afc), line); lines++; }
    memcpy(prev, t, 0xc8);
    if (cont) {
        const uint8_t *c = M + r32(THINGS) + (uint32_t)cont * 0xc8;
        if (cont != pcont) { memcpy(prevc, c, 0xc8); pcont = cont; fprintf(stderr, "walk watch: on thing %d (class %d)\n", cont, c[4]); }
        n = 0;
        for (int i = 0; i < 0xc8 && n < 1900; i++) if (c[i] != prevc[i]) n += snprintf(line + n, sizeof line - (size_t)n, " +%02x:%02x>%02x", i, prevc[i], c[i]);
        if (n) { fprintf(stderr, "walk watch: tick %u ride%s\n", r32(0x1d3afc), line); lines++; }
        memcpy(prevc, c, 0xc8);
    } else pcont = 0;
}

/* ---- contact shadows ----
 * The game's art has no shadows on the ground of its own (they are in the pictures, standing up
 * with the cards), so things looked stood on the floor rather than in it. Each thing darkens the
 * floor where it stands: a big thing its footprint and a soft edge round it, others a soft blob
 * as wide as their foot. Riders cast none (they are up on their rides). */
static void splat(Snap *s, double x, double y, double r, double soft, int dark, int square) {
    int n = MAPE * SHR, x0 = (int)floor((x - r - soft) * SHR), x1 = (int)ceil((x + r + soft) * SHR);
    int y0 = (int)floor((y - r - soft) * SHR), y1 = (int)ceil((y + r + soft) * SHR);
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 > n) x1 = n; if (y1 > n) y1 = n;
    for (int yy = y0; yy < y1; yy++) for (int xx = x0; xx < x1; xx++) {
        double dx = fabs((xx + 0.5) / SHR - x), dy = fabs((yy + 0.5) / SHR - y);
        double d = square ? fmax(dx, dy) : sqrt(dx * dx + dy * dy);   /* (distance from the middle) */
        double k = d <= r ? 1 : d >= r + soft ? 0 : 1 - (d - r) / soft;
        int v = (int)(dark * k * k * (3 - 2 * k) + 0.5);
        uint8_t *o = &s->shade[(size_t)yy * MAPN * SHR + xx];
        if (v > *o) *o = (uint8_t)v;
    }
}
static void make_shade(Snap *s, const Geom *g) {
    memset(s->shade, 0, sizeof s->shade);
    for (int i = 0; i < s->ncards; i++) {
        const Card *c = &s->cards[i];
        if (c->idx > 0 && c->idx < NTH && s->riding[c->idx]) continue;
        double wc = c->w / (g->tws + g->shear);              /* the picture's width, cells */
        if (c->big) splat(s, c->x, c->y, c->r * 0.9, 0.75, 115, 1);
        else if (c->cls == 2) splat(s, c->x, c->y, 0.06, 0.13, 120, 0);   /* a visitor's feet */
        else splat(s, c->x, c->y, fmin(0.5, wc * 0.22), fmin(0.5, wc * 0.2 + 0.1), 75, 0);
    }
    if (getenv("TP_WALK_PARTS")) { static int done; if (!done++) { int nz = 0, nr = 0, nb = 0; for (size_t i = 0; i < sizeof s->shade; i++) nz += s->shade[i] != 0;
        for (int i = 0; i < s->ncards; i++) { const Card *c = &s->cards[i]; nb += c->big; if (c->idx > 0 && c->idx < NTH && s->riding[c->idx]) nr++; }
        for (int i = 0; i < s->ncards; i++) { const Card *c = &s->cards[i]; if (c->cls != 2 && c->idx > 0) fprintf(stderr, "walk card: %d cls %d sub %d big %d %dx%d foot %d,%d at %.2f,%.2f r %.2f\n", c->idx, c->cls, M[r32(THINGS) + (uint32_t)c->idx * 0xc8 + 0xc], c->big, c->w, c->h, c->ax, c->ay, c->x, c->y, c->r); }
        fprintf(stderr, "walk shade: %d samples, %d cards, %d big, %d riding; tws %.2f ths %.2f shear %.2f\n", nz, s->ncards, nb, nr, g->tws, g->ths, g->shear); } }
}

/* ---- the snapshot (emulation thread) ---- */
static int cmp_float(const void *a, const void *b) { float x = *(const float *)a, y = *(const float *)b; return x < y ? -1 : x > y; }
static void snapshot(Snap *s, const Geom *g, double ax, double ay, int follow) {
    s->g = *g; s->ncards = 0; s->npix = 0; s->ncomponents=0; memset(s->blocked, 0, sizeof s->blocked);
    tp_host_palette_rgba(s->pal);
    s->weather = M[WEATHER];
    int T = (int)g->tws; if (T > TMAX) T = TMAX; if (T < 8) T = 8;
    /* floor: the cells whose ground changed (a path laid, litter, land bought), into the floor not on show */
    static uint16_t key[MAPN * MAPN]; uint32_t map = r32(MAP_PTR);
    for (int y = 0; y < MAPE; y++) for (int x = 0; x < MAPE; x++) { uint32_t c = map + (uint32_t)(y * MAPW + x) * 4; key[y * MAPN + x] = (uint16_t)(M[c + 2] | M[c + 3] << 8); }
    uint32_t geo = (uint32_t)(g->tw * 131 + g->th * 7 + g->lo) * 2654435761u ^ r32(0x1d3b90) ^ (uint32_t)T << 24;
    int fi = front >= 0 ? snaps[front].fi : 0;
    if (!floor_valid[fi] || floor_geo[fi] != geo || memcmp(floor_key[fi], key, sizeof key)) {
        fi = front >= 0 ? 1 - snaps[front].fi : 0;
        build_floor(g, fi, T, geo, key);
    }
    s->fi = fi; s->T = T;
    /* things: every cell near the walker */
    Build B = {0};
    B.s = s; B.g = g;
    NDTarget base = {sa, sizeof sa, 0, SW_, SW_, SH_, SW_ - 1, g->lo, M + r32(GHOST_TAB), 0};
    B.a = base; B.b = base; B.b.buf = sb;
    static int cleared; if (!cleared) { memset(sa, 0, sizeof sa); memset(sb, 0xff, sizeof sb); cleared = 1; }
    static int parts_done; parts_log = !parts_done && getenv("TP_WALK_PARTS"); if (parts_log) parts_done = 1;
    int keep = tp_native_stable_rng; tp_native_stable_rng = 1;
    for (int cy = 0; cy < MAPE; cy++) for (int cx = 0; cx < MAPE; cx++) {
        double dx = cx + 0.5 - ax, dy = cy + 0.5 - ay;
        if (dx * dx + dy * dy > 44 * 44) continue;
        B.cx = cx; B.cy = cy; B.idx = 0; B.any = 0;
        tp_native_cell_things(M, cx, cy, OXS << g->lo, OYS << g->lo, card_log, &B);
        finish(&B);
    }
    tp_native_stable_rng = keep; parts_log = 0;
    find_riders(s);
    make_shade(s, g);
    /* a visitor's height: the median of the visitors' pictures */
    static float hs[MAXCARDS]; int nh = 0;
    for (int i = 0; i < s->ncards; i++) if (s->cards[i].cls == 2) hs[nh++] = (float)(s->cards[i].h * g->aspect / g->tws);
    if (nh) { qsort(hs, (size_t)nh, sizeof *hs, cmp_float); s->eye = hs[nh / 2] * 0.92f; } else s->eye = 0.7f;
    /* the visitor followed */
    s->f_idx = follow; s->f_ok = 0; s->f_cont = 0; s->f_name[0] = 0; s->f_type = -1; s->f_elev = 0;
    if (valid_idx(follow)) {
        const uint8_t *t = M + r32(THINGS) + (uint32_t)follow * 0xc8;
        int cont = s->riding[follow];
        if (cont) {
            const uint8_t *c = M + r32(THINGS) + (uint32_t)cont * 0xc8;
            s->f_ok = 1; s->f_cont = cont; s->f_cls = c[4];
            for (int k = 0; k < s->nconts; k++) if (s->conts[k].idx == cont) { s->f_x = s->conts[k].x; s->f_y = s->conts[k].y; }
            s->f_type = ride_type(cont); s->f_elev = (float)car_lift(g, cont);
            if (s->f_type >= 0) snprintf(s->f_name, sizeof s->f_name, "%.39s", (const char *)M + RIDE_TYPES + (uint32_t)s->f_type * 0xc8);
        } else if (t[4] == 2 && t[7] < MAPE && t[9] < MAPE) {
            s->f_ok = 1; s->f_x = (float)(t[7] + t[6] / 256.0); s->f_y = (float)(t[9] + t[8] / 256.0);
        }
        watch(follow, t, s->f_cont);
    }
}
static double last_snap;
int tp_walk_active(void) { return W.on; }
/* At the game's per-frame render (view.c's FRAME_RENDER hook). */
void tp_walk_frame(void) {
    if (!W.on) return;
    double t = now(); if (t - last_snap < 1.0 / 60) return;
    last_snap = t;
    M = tp_host_memory();
    Geom g; if (!geom(&g) || !r32(MAP_PTR) || !r32(THINGS)) return;
    pthread_mutex_lock(&lock);
    double ax = W.follow && W.have_f ? W.fx : W.x, ay = W.follow && W.have_f ? W.fy : W.y; int follow = W.follow;
    int b = front < 0 ? 0 : 1 - front;
    pthread_mutex_unlock(&lock);
    Snap *s = &snaps[b];
    snapshot(s, &g, ax, ay, follow);
    /* the game's camera on the walker: its music and sounds are then those of the rides around */
    int cy = (int)lround(ay - g.wh / 2.0 / g.ths);
    int cx = (int)lround(ax + (g.shear * (ay - cy) + g.c0 - g.sw / 2.0) / g.tws);
    cx = cx < GAME_CX0 ? GAME_CX0 : cx > GAME_CX1 ? GAME_CX1 : cx; cy = cy < 0 ? 0 : cy > GAME_CY1 ? GAME_CY1 : cy;
    if (!getenv("TP_WALK_NOCAM") && (r16(CAM_X) != cx || r16(CAM_Y) != cy)) {
        uint8_t *m = tp_host_memory(); int16_t v;
        v = (int16_t)cx; memcpy(m + CAM_X, &v, 2); v = (int16_t)cy; memcpy(m + CAM_Y, &v, 2); v = 1; memcpy(m + LAND_DIRTY, &v, 2);
    }
    pthread_mutex_lock(&lock);
    s->serial = front >= 0 ? snaps[front].serial + 1 : 1; s->valid = 1; front = b;
    pthread_mutex_unlock(&lock);
}

/* ---- the app's side ---- */
int tp_walk_start(double wx, double wy) {
    M = tp_host_memory();
    Geom g; if (!M || !geom(&g) || !r32(MAP_PTR)) return 0;
    pthread_mutex_lock(&lock);
    memset(&W, 0, sizeof W);
    W.on = 1; W.x = wx < 0.5 ? 0.5 : wx > MAPE - 0.5 ? MAPE - 0.5 : wx; W.y = wy < 0.5 ? 0.5 : wy > MAPE - 0.5 ? MAPE - 0.5 : wy;
    W.yaw = ART_YAW; W.mode = getenv("TP_WALK_DIORAMA") ? MODE_DIORAMA : MODE_STREET;
    W.mesh = !getenv("TP_WALK_MESH") || atoi(getenv("TP_WALK_MESH")) != 0;
    wm_models_reset();
    rm_reset();
    lp_load();
    W.hscale = getenv("TP_WALK_SCALE") ? atof(getenv("TP_WALK_SCALE")) : 1; if (W.hscale < 0.25 || W.hscale > 4) W.hscale = 1;
    snaps[0].valid = snaps[1].valid = 0; last_snap = 0;     /* (front stays: the emulation thread may be swapping) */
    pthread_mutex_unlock(&lock);
    tp_hook_pcs[0] = FRAME_RENDER; tp_hooks_changed();     /* (view.c keeps it while walking) */
    set_note("Visitor View: WASD walk, drag to look, F follow a visitor, Esc leave");
    return 1;
}
void tp_walk_stop(void) { pthread_mutex_lock(&lock); W.on = 0; pthread_mutex_unlock(&lock); }
/* Where the walker is (cells), for the overhead view to return to. */
void tp_walk_position(double *x, double *y) {
    pthread_mutex_lock(&lock); *x = W.cam_x; *y = W.cam_y; pthread_mutex_unlock(&lock);
}

static int blocked(const Snap *s, double x, double y) {
    if (x < 0.2 || y < 0.2 || x > MAPE - 0.2 || y > MAPE - 0.2) return 1;
    return s->blocked[(int)y * MAPN + (int)x];
}
static double wrap(double a) { while (a > M_PI) a -= 2 * M_PI; while (a < -M_PI) a += 2 * M_PI; return a; }
/* Nearest visitor to (x, y) within r cells, ahead (cos of the angle off yaw at least `cone`), not `skip`. */
static int nearest_visitor(const Snap *s, double x, double y, double yaw, double r, double cone, int skip) {
    int best = 0; double bd = r * r;
    for (int i = 0; i < s->ncards; i++) {
        const Card *c = &s->cards[i];
        if (c->cls != 2 || c->idx == skip) continue;
        double dx = c->x - x, dy = c->y - y, d = dx * dx + dy * dy;
        if (d > bd) continue;
        if (cone > -1 && d > 1e-6 && (dx * cos(yaw) + dy * sin(yaw)) / sqrt(d) < cone) continue;
        bd = d; best = c->idx;
    }
    return best;
}
enum { WALK_FOLLOW = 1, WALK_HOP, WALK_RIDE, WALK_VIEW, WALK_NOCLIP, WALK_MESH };
void tp_walk_command(int cmd) {
    pthread_mutex_lock(&lock);
    const Snap *s = front >= 0 ? &snaps[front] : NULL;
    if (!W.on || !s || !s->valid) { pthread_mutex_unlock(&lock); return; }
    double x = W.cam_x, y = W.cam_y;
    if (cmd == WALK_FOLLOW) {
        if (W.follow) { W.x = W.fx; W.y = W.fy; W.yaw = W.cam_yaw; W.follow = 0; set_note("Walking on your own"); }
        else {
            int v = nearest_visitor(s, x, y, W.cam_yaw, 6, -2, 0);
            if (v) { W.follow = v; W.have_f = 0; W.head = 0; set_note("Following a visitor: Space hops to another, R joins a ride, F lets go"); }
            else set_note("No visitor within 6 cells");
        }
    } else if (cmd == WALK_HOP) {
        int v = nearest_visitor(s, x, y, W.cam_yaw, 5, 0.5, W.follow);
        if (!v) v = nearest_visitor(s, x, y, W.cam_yaw, 5, -2, W.follow);
        if (v) { W.follow = v; W.have_f = 0; W.head = 0; set_note("Following another visitor"); } else set_note("No other visitor near");
    } else if (cmd == WALK_RIDE) {                       /* someone on the ride in front of you, or the nearest */
        int best = -1; double bd = 1e9;
        for (int k = 0; k < s->nconts; k++) {
            double dx = s->conts[k].x - x, dy = s->conts[k].y - y, d = sqrt(dx * dx + dy * dy);
            if (d > 12) continue;
            double ahead = d > 1e-6 ? (dx * cos(W.cam_yaw) + dy * sin(W.cam_yaw)) / d : 1;
            double score = d - 4 * ahead;
            if (score < bd) { bd = score; best = k; }
        }
        if (best >= 0) { W.follow = s->conts[best].rider; W.have_f = 0; W.head = 0; set_note("Riding along with a visitor"); }
        else set_note("No ride with riders within 12 cells");
    } else if (cmd == WALK_VIEW) {
        W.mode = W.mode == MODE_STREET ? MODE_DIORAMA : MODE_STREET;
        set_note(W.mode == MODE_STREET ? "Eye level" : "Diorama: above and behind");
    } else if (cmd == WALK_NOCLIP) {
        W.noclip = !W.noclip; set_note(W.noclip ? "Walking through everything" : "Rides and scenery block the way");
    } else if (cmd == WALK_MESH) {
        W.mesh=!W.mesh; set_note(W.mesh ? "Authored 3D preview; original sprites for remaining assets" : "Original camera-facing sprites");
    }
    pthread_mutex_unlock(&lock);
}
/* Moves the camera: fwd/strafe/turn in -1..1, look deltas in radians, dt seconds. */
void tp_walk_input(double fwd, double strafe, double turn, double look_yaw, double look_pitch, int run, double dt) {
    pthread_mutex_lock(&lock);
    const Snap *s = front >= 0 ? &snaps[front] : NULL;
    if (!W.on) { pthread_mutex_unlock(&lock); return; }
    if (dt > 0.1) dt = 0.1;
    double eye = s && s->valid ? s->eye : 0.7;
    W.pitch += look_pitch; W.pitch = W.pitch < -0.7 ? -0.7 : W.pitch > 0.9 ? 0.9 : W.pitch;
    if (W.follow && s && s->valid) {
        if (fwd || strafe) {                              /* walking yourself lets go */
            W.x = W.fx; W.y = W.fy; W.yaw = W.cam_yaw; W.follow = 0; set_note("Walking on your own");
        }
    }
    if (W.follow && s && s->valid) {
        if (s->f_idx == W.follow && !s->f_ok) {           /* gone (left the park) */
            W.x = W.fx; W.y = W.fy; W.yaw = W.cam_yaw; W.follow = 0; set_note("The visitor has gone");
        }
    }
    if (W.follow && s && s->valid && s->f_idx == W.follow && s->f_ok) {
        double tx = s->f_x, ty = s->f_y;
        if (!W.have_f) { W.fx = tx; W.fy = ty; W.fyaw = W.cam_yaw; W.have_f = 1; W.fserial = s->serial; }
        double k = 1 - exp(-dt * 10);
        double ox = W.fx, oy = W.fy;
        W.fx += (tx - W.fx) * k; W.fy += (ty - W.fy) * k;
        double vx = W.fx - ox, vy = W.fy - oy;
        if (vx * vx + vy * vy > 1e-7) W.fyaw += wrap(atan2(vy, vx) - W.fyaw) * (1 - exp(-dt * 4));
        W.head = wrap(W.head + look_yaw + turn * 1.8 * dt);
        double z = eye;
        W.cam_x = W.fx; W.cam_y = W.fy; W.cam_yaw = W.fyaw + W.head;
        if (s->f_cont) {
            W.ride_t += dt; z += s->f_elev;
            if (s->f_cls == 4) {                          /* a ride that turns or swings: turn with it */
                W.orbit += dt * 1.4;
                W.cam_x = W.fx + 0.45 * cos(W.orbit); W.cam_y = W.fy + 0.45 * sin(W.orbit);
                W.cam_yaw = W.orbit + M_PI / 2 + W.head; z += 0.15 * sin(W.ride_t * 3);
            }
        } else W.ride_t = 0;
        W.cam_z = z; W.cam_pitch = W.pitch;
    } else {
        W.yaw = wrap(W.yaw + look_yaw + turn * 1.8 * dt);
        double sp = (run ? 5.0 : 2.5) * dt;
        double dx = (cos(W.yaw) * fwd - sin(W.yaw) * strafe) * sp, dy = (sin(W.yaw) * fwd + cos(W.yaw) * strafe) * sp;
        if (s && s->valid && !W.noclip) {
            if (!W.placed && blocked(s, W.x, W.y)) {      /* dropped on a ride or a tree: the nearest open cell */
                for (int r = 1; r < 8 && !W.placed; r++) for (int yy = -r; yy <= r && !W.placed; yy++) for (int xx = -r; xx <= r; xx++)
                    if (!blocked(s, (int)W.x + xx + 0.5, (int)W.y + yy + 0.5)) { W.x = (int)W.x + xx + 0.5; W.y = (int)W.y + yy + 0.5; W.placed = 1; break; }
            }
            W.placed = 1;
            if (!blocked(s, W.x + dx + (dx > 0 ? 0.2 : -0.2), W.y)) W.x += dx;
            if (!blocked(s, W.x, W.y + dy + (dy > 0 ? 0.2 : -0.2))) W.y += dy;
        } else { W.x += dx; W.y += dy; }
        W.x = W.x < 0.2 ? 0.2 : W.x > MAPE - 0.2 ? MAPE - 0.2 : W.x; W.y = W.y < 0.2 ? 0.2 : W.y > MAPE - 0.2 ? MAPE - 0.2 : W.y;
        W.cam_x = W.x; W.cam_y = W.y; W.cam_z = eye; W.cam_yaw = W.yaw; W.cam_pitch = W.pitch;
    }
    if (W.mode == MODE_DIORAMA) {                         /* above and behind, near the art's own angle */
        double lim = 0.6, yaw = ART_YAW + fmax(-lim, fmin(lim, wrap(W.cam_yaw - ART_YAW)));
        double back = 2.4 * W.hscale;
        W.cam_x -= cos(yaw) * back; W.cam_y -= sin(yaw) * back; W.cam_z = 1.9 * W.hscale + (W.cam_z - eye);
        W.cam_yaw = yaw; W.cam_pitch = 0.62 + W.pitch * 0.3;
    }
    pthread_mutex_unlock(&lock);
}
/* Development and tests: put the walker somewhere, looking along yaw (radians; NAN keeps it). */
void tp_walk_teleport(double x, double y, double yaw) {
    pthread_mutex_lock(&lock);
    W.x = x; W.y = y; if (!isnan(yaw)) W.yaw = yaw; W.follow = 0; W.placed = 1;
    W.cam_x = x; W.cam_y = y; W.cam_yaw = W.yaw;
    pthread_mutex_unlock(&lock);
}
int tp_walk_follow_nearest(void) { tp_walk_command(WALK_FOLLOW); pthread_mutex_lock(&lock); int f = W.follow; pthread_mutex_unlock(&lock); return f; }

/* 1 while the visitor followed is on a ride: its type (0..28, or -1) and name. */
int tp_walk_ride(int *type, char *name, int n) {
    pthread_mutex_lock(&lock);
    const Snap *s = front >= 0 ? &snaps[front] : NULL;
    int on = W.on && W.follow && s && s->valid && s->f_idx == W.follow && s->f_cont;
    if (on) { *type = s->f_type; if (name && n > 0) snprintf(name, (size_t)n, "%s", s->f_name); }
    pthread_mutex_unlock(&lock);
    return on;
}
int tp_walk_status(char *buf, int n) {
    pthread_mutex_lock(&lock);
    const Snap *s = front >= 0 ? &snaps[front] : NULL;
    if (!W.on) { pthread_mutex_unlock(&lock); if (n) buf[0] = 0; return 0; }
    char what[96];
    if (W.follow && s && s->valid && s->f_cont) snprintf(what, sizeof what, "On %s with a visitor", s->f_name[0] ? s->f_name : "a ride");
    else if (W.follow) snprintf(what, sizeof what, "Following a visitor");
    else snprintf(what, sizeof what, "%s", !W.mesh ? "Walking (original cards)" :
        tp_art_mode()==TP_ART_REIMAGINED ? "Reimagined" :
        getenv("TP_ART_MODE") && tp_art_mode()==TP_ART_ORIGINAL ? "Original artwork" : "Faithful 3D");
    if (now() < note_until) snprintf(buf, (size_t)n, "%s   ·   %s", what, note);
    else snprintf(buf, (size_t)n, "%s   ·   cell %d,%d   ·   F follow  R ride  V view  P compare  Esc leave", what, (int)W.cam_x, (int)W.cam_y);
    pthread_mutex_unlock(&lock);
    return 1;
}

/* ---- drawing (app thread) ---- */
static uint32_t rgb(int r, int g, int b) { return 0xff000000u | (uint32_t)b << 16 | (uint32_t)g << 8 | (uint32_t)r; }
static uint32_t mix(uint32_t a, uint32_t b, double f) {
    if (f <= 0) return a; if (f >= 1) return b;
    int k = (int)(f * 256);
    uint32_t r = ((a & 255) * (256 - k) + (b & 255) * k) >> 8, g = (((a >> 8) & 255) * (256 - k) + ((b >> 8) & 255) * k) >> 8;
    uint32_t bl = (((a >> 16) & 255) * (256 - k) + ((b >> 16) & 255) * k) >> 8;
    return 0xff000000u | bl << 16 | g << 8 | r;
}
static uint32_t darken(uint32_t c, int k) {            /* k 0..255 */
    if (k <= 0) return c;
    uint32_t m = 256 - (uint32_t)k;
    return 0xff000000u | ((((c >> 16) & 255) * m) >> 8) << 16 | ((((c >> 8) & 255) * m) >> 8) << 8 | ((c & 255) * m) >> 8;
}
static int shade_at(const Snap *s, double x, double y) {  /* the contact shadow at a floor point, smoothly */
    double fx = x * SHR - 0.5, fy = y * SHR - 0.5; int n = MAPE * SHR;
    int x0 = (int)floor(fx), y0 = (int)floor(fy); double ax = fx - x0, ay = fy - y0;
    if (x0 < 0 || y0 < 0 || x0 + 1 >= n || y0 + 1 >= n) return 0;
    const uint8_t *p = s->shade + (size_t)y0 * MAPN * SHR + x0;
    if (!(p[0] | p[1] | p[MAPN * SHR] | p[MAPN * SHR + 1])) return 0;
    return (int)((p[0] * (1 - ax) + p[1] * ax) * (1 - ay) + (p[MAPN * SHR] * (1 - ax) + p[MAPN * SHR + 1] * ax) * ay);
}
typedef struct { float z; int i; float x, y; } Vis;
static int cmp_vis(const void *a, const void *b) { float x = ((const Vis *)a)->z, y = ((const Vis *)b)->z; return x > y ? -1 : x < y; }
/* Resolve the whole assembly before writing any pixels. Missing bases, caps,
 * supports or poses must never leave a partially replaced track in the park. */
static int draw_track_assembly(const Snap *s,const Card *c,WMRender *mr,float scale){
    unsigned n=c->ncomponents,first=c->component_first;
    if(c->cls!=0x12 || !n || n>TP_RM_ASSEMBLY_MAX || s->ncomponents>TP_RM_COMPONENT_POOL ||
       first>s->ncomponents || n>s->ncomponents-first)return 0;
    const RMRig *rigs[TP_RM_ASSEMBLY_MAX];const RMPose *poses[TP_RM_ASSEMBLY_MAX];
    for(unsigned i=0;i<n;i++){
        const TPRMPlacedComponent *p=&s->components[first+i];
        if(!isfinite(p->x)||!isfinite(p->y)||!isfinite(p->z))return 0;
        rigs[i]=rm_find_frame(0x12,p->frame);
        if(!rigs[i] || rigs[i]->ns || !(poses[i]=rm_pose(rigs[i],p->frame)))return 0;
    }
    for(unsigned i=0;i<n;i++){
        const TPRMPlacedComponent *p=&s->components[first+i];
        float placement[12]={1,0,0,0,0,1,0,0,0,0,1,p->z};
        rm_draw(mr,rigs[i],poses[i],p->x,p->y,scale,placement);
    }
    return 1;
}
/* App/render thread only: no asset loading while the game captures memory.
 * Camera and visible body share the exact vehicle, seat and guest-head pose. */
static int vehicle_eye(const Snap*s,int guest,float scale,float*position,float*forward){
    if(guest<=0)return 0;
    const Card*c=NULL;int seat=-1;
    for(int k=0;k<s->ncards;k++){
        const Card*q=&s->cards[k];if(q->cls!=14||!q->rig_ok)continue;
        for(int j=0;j<40;j++)if(q->rider_ids[j]==guest){if(c)return 0;c=q;seat=j;}
    }
    if(!c)return 0;
    const RMRig*ride=rm_find_frame(c->cls,c->frame);const RMPose*pose=ride?rm_pose(ride,c->frame):NULL;
    const RMRig*guests[40];const RMPose*poses[40];
    if(!pose||!rm_resolve_riders(ride,c->seats,c->rig_riders,guests,poses))return 0;
    const RMRig*person=guests[seat];const RMPose*seated=poses[seat];
    if(!person)return 0;
    const RMEye*eye=rm_eye_find(person->cls,person->type);
    float placement[12],attachment[12];if(!rm_vehicle_transform(&c->vehicle,ride,pose,placement))return 0;
    rm_affine_product(attachment,placement,pose->matrices+(ride->nb+seat)*12);
    if(!rm_eye_transform(eye,person,seated,attachment,position,forward))return 0;
    for(int k=0;k<3;k++)position[k]*=scale;
    position[0]+=c->vehicle.track_x+1.f/3;position[1]+=c->vehicle.track_y+.5f;
    return 1;
}
#define FOG0 20.0
#define FOG1 42.0
/* The view at ow x oh RGBA (0xAABBGGRR as the app's bitmaps). Returns 0 when there is nothing yet. */
int tp_walk_render(uint32_t *out, int ow, int oh) {
    pthread_mutex_lock(&lock);
    const Snap *s = front >= 0 ? &snaps[front] : NULL;
    if (!W.on || !s || !s->valid || ow < 8 || oh < 8) { pthread_mutex_unlock(&lock); return 0; }
    const Geom *g = &s->g; int T = s->T;
    static float *depth; static size_t depth_n;
    size_t pixels=(size_t)ow*oh;
    if (W.mesh && pixels>depth_n) {
        float *p=realloc(depth,pixels*sizeof *p);
        if (!p) { pthread_mutex_unlock(&lock); return 0; }
        depth=p; depth_n=pixels;
    }
    if (W.mesh) for (size_t i=0;i<pixels;i++) depth[i]=FLT_MAX;
    const uint8_t *ft = floor_tex[s->fi], *ot = off_tex[s->fi];
    double cx = W.cam_x, cy = W.cam_y, cz = W.cam_z * W.hscale, yaw = W.cam_yaw, pitch = W.cam_pitch;
    if (W.mode == MODE_DIORAMA) cz = W.cam_z;
    int seated_camera=0;float eye_position[3],eye_forward[3];
    if(W.mesh&&W.mode==MODE_STREET&&tp_art_mode()==TP_ART_REIMAGINED&&
       vehicle_eye(s,W.follow,(float)W.hscale,eye_position,eye_forward)){
        cx=eye_position[0];cy=eye_position[1];cz=eye_position[2];
        yaw=atan2(eye_forward[1],eye_forward[0])+W.head;
        pitch=-atan2(eye_forward[2],hypot(eye_forward[0],eye_forward[1]))+W.pitch;
        seated_camera=1;
    }
    double fov = (W.mode == MODE_STREET ? 72 : 58) * M_PI / 180, f = ow / 2.0 / tan(fov / 2);
    /* camera basis: forward, right (horizontal), up; world x east, y south (down the map), z up */
    double fx = cos(yaw) * cos(pitch), fy = sin(yaw) * cos(pitch), fz = -sin(pitch);
    double rx = -sin(yaw), ry = cos(yaw);
    double ux = cos(yaw) * sin(pitch), uy = sin(yaw) * sin(pitch), uz = cos(pitch);   /* (forward x right) */
    if (getenv("TP_WALK_FLIP")) { rx = -rx; ry = -ry; }  /* (development: if east shows on the left) */
    int rain = s->weather & 5;
    uint32_t sky_top = rain ? rgb(96, 104, 120) : rgb(92, 152, 228), sky_low = rain ? rgb(150, 156, 166) : rgb(206, 226, 244);
    uint32_t fogc = sky_low;
    /* sky and floor, row by row (the camera does not roll: along a row the ray's height is constant) */
    for (int y = 0; y < oh; y++) {
        uint32_t *o = out + (size_t)y * ow;
        double b = -(y + 0.5 - oh / 2.0) / f;
        double dz = fz + uz * b;
        double hx = fx + ux * b, hy = fy + uy * b;          /* the ray at the row's centre */
        if (dz > -1e-4) {
            double e = dz / sqrt(hx * hx + hy * hy + dz * dz);
            uint32_t c = mix(sky_low, sky_top, e * 2.2);
            for (int x = 0; x < ow; x++) o[x] = c;
            continue;
        }
        double t = -cz / dz;                                   /* distance along forward (view z) */
        if (W.mesh) for (int x=0;x<ow;x++) depth[(size_t)y*ow+x]=(float)t;
        double gx = cx + t * hx, gy = cy + t * hy, sx = t * rx / f, sy = t * ry / f;
        gx += sx * (0.5 - ow / 2.0); gy += sy * (0.5 - ow / 2.0);
        double fog = (t - FOG0) / (FOG1 - FOG0);
        if (fog >= 1) { for (int x = 0; x < ow; x++) o[x] = fogc; continue; }
        /* far away a pixel covers many texels (most of all in depth): there, four samples across its footprint */
        double b2 = b - 0.5 / f, dz2 = fz + uz * b2, t2 = dz2 < -1e-6 ? -cz / dz2 : t * 4;
        double qx = cx + t2 * (fx + ux * b2) - (cx + t * hx), qy = cy + t2 * (fy + uy * b2) - (cy + t * hy);   /* half a row deeper */
        if (!W.mesh && ((qx * qx + qy * qy) * T * T * 4 > 1 || (sx * sx + sy * sy) * T * T > 1)) {
            for (int x = 0; x < ow; x++, gx += sx, gy += sy) {
                uint32_t r = 0, gg = 0, bb = 0;
                for (int k = 0; k < 4; k++) {
                    double px = gx + ((k & 1) - 0.5) * 0.5 * sx + ((k >> 1) - 0.25) * qx, py = gy + ((k & 1) - 0.5) * 0.5 * sy + ((k >> 1) - 0.25) * qy;
                    int X = (int)floor(px), Y = (int)floor(py), u = (int)((px - X) * T), v = (int)((py - Y) * T);
                    if (u >= T) u = T - 1; if (v >= T) v = T - 1;
                    uint32_t c = s->pal[X >= 0 && X < MAPE && Y >= 0 && Y < MAPE ? ft[(size_t)(Y * T + v) * MAPN * T + (size_t)X * T + u] : ot[v * T + u]];
                    r += c & 255; gg += (c >> 8) & 255; bb += (c >> 16) & 255;
                }
                uint32_t c = darken(0xff000000u | (bb / 4) << 16 | (gg / 4) << 8 | r / 4, shade_at(s, gx, gy));
                o[x] = fog > 0 ? mix(c, fogc, fog) : c;
            }
            continue;
        }
        for (int x = 0; x < ow; x++, gx += sx, gy += sy) {
            int X = (int)floor(gx), Y = (int)floor(gy), u = (int)((gx - X) * T), v = (int)((gy - Y) * T);
            if (u >= T) u = T - 1; if (v >= T) v = T - 1;
            uint8_t c = X >= 0 && X < MAPE && Y >= 0 && Y < MAPE ? ft[(size_t)(Y * T + v) * MAPN * T + (size_t)X * T + u] : ot[v * T + u];
            uint32_t col = W.mesh ? s->pal[c] : darken(s->pal[c], shade_at(s, gx, gy));
            o[x] = fog > 0 && !W.mesh ? mix(col, fogc, fog) : col;
        }
    }
    if (W.mesh) {
        WMRender mr={.pixels=out,.depth=depth,.w=ow,.h=oh,.cx=cx,.cy=cy,.cz=cz,
            .fx=fx,.fy=fy,.fz=fz,.rx=rx,.ry=ry,.ux=ux,.uy=uy,.uz=uz,.focal=f,.palette=s->pal};
        float kx=(float)(W.hscale/g->tws),ky=(float)(W.hscale*g->aspect/g->tws);
        int hide=W.mode==MODE_STREET ? W.follow:0, rendered=0, rigs_drawn=0, layers_drawn=0;
        double began=now();
        for (int i=0;i<s->ncards;i++) {
            const Card *c=&s->cards[i]; if (hide && c->idx==hide) continue;
            float dx=c->x-cx,dy=c->y-cy,dist=sqrtf(dx*dx+dy*dy), radius=c->w*kx;
            float forward=dx*cos(yaw)+dy*sin(yaw), lateral=dx*rx+dy*ry;
            if (dist-radius>FOG1 || forward+radius<.08f || fabsf(lateral)>fmaxf(0,forward)*tan(fov/2)+radius) continue;
            mr.texture=s->pix+c->off; mr.tw=c->w; mr.th=c->h;
            int reimagined=tp_art_mode()==TP_ART_REIMAGINED;
            /* Retain the historical study default only if no explicit choice
             * exists. The launcher always supplies an explicit selection. */
            int original=getenv("TP_ART_MODE") && tp_art_mode()==TP_ART_ORIGINAL;
            const LPLayer *layer_asset=(!reimagined&&!original)?lp_find(c->cls,c->type):NULL;
            const LPLayer *layer=c->layer_ok?layer_asset:NULL;
            const LPPose *layer_pose=layer?lp_pose(layer,c->frame):NULL;
            if(layer_pose && lp_valid_riders(layer,c->riders,c->nriders)) {
                lp_draw(&mr,layer,layer_pose,c->x,c->y,(float)W.hscale,c->riders,c->nriders,s->pix,kx,ky);
                layers_drawn++;rendered++;continue;
            }
            if(reimagined && draw_track_assembly(s,c,&mr,(float)W.hscale)){
                rigs_drawn++;rendered++;continue;
            }
            const RMRig *rig=reimagined && c->rig_ok ?
                ((c->cls==0x18 || c->cls==0xe)?rm_find_frame(c->cls,c->frame):rm_find(c->cls,c->type)):NULL;
            const RMPose *pose=rig?rm_pose(rig,c->frame):NULL;
            if(pose) {
                const RMRig *guests[40]={0};const RMPose *seated[40]={0};
                int ok=rm_resolve_riders(rig,c->seats,c->rig_riders,guests,seated);
                float placement[12]={1,0,0,0,0,1,0,0,0,0,1,c->rig_z};
                float model_x=c->rig_x,model_y=c->rig_y;
                if(ok && c->cls==0xe){
                    ok=rm_vehicle_transform(&c->vehicle,rig,pose,placement);
                    model_x=c->vehicle.track_x+1.f/3;model_y=c->vehicle.track_y+.5f;
                }
                if(ok) {
                    rm_draw(&mr,rig,pose,model_x,model_y,(float)W.hscale,placement);
                    rigs_drawn++;
                    for(int seat=0;seat<rig->ns;seat++)if(guests[seat]&&!(seated_camera&&c->rider_ids[seat]==W.follow)){
                        float attachment[12];rm_affine_product(attachment,placement,pose->matrices+(rig->nb+seat)*12);
                        rm_draw(&mr,guests[seat],seated[seat],model_x,model_y,(float)W.hscale,attachment);
                    }
                    rendered++;continue;
                }
            }
            const WMModel *model=(reimagined||original||layer_asset)?NULL:wm_model_find(c->cls,c->type);
            if(model && wm_model_draw(&mr,model,c->x,c->y,(float)W.hscale)) {
                rendered++;continue;
            }
            if(reimagined || original || layer_asset || !getenv("TP_WALK_SHELL_STUDY")) {
                /* Unmodeled assets retain their original presentation. A
                 * failed generic reconstruction is not a substitute model. */
                WMShell card={.x=c->x,.y=c->y,.kx=kx,.ky=ky,.ax=c->ax,.ay=c->ay,
                    .w=c->w,.h=c->h,.shape=WM_SMALL,.rx=-sinf(yaw),.ry=cosf(yaw),
                    .dx=cosf(yaw),.dy=sinf(yaw)};
                wm_shell(&mr,&card,dist);rendered++;continue;
            }
            /* Keep the original direction-dependent character animation on a
             * camera-facing card; structures stay fixed in world space. */
            int person=c->cls==2 || c->cls==8;
            float angle=person ? (float)yaw : (float)ART_YAW;
            float thick=c->r>0 ? c->r*.65f : fminf(.45f,c->w*kx*.25f);
            if (c->shape==WM_TRACK) thick=fminf(.18f,c->w*kx*.08f);
            if (c->shape==WM_OPEN) thick=fminf(.32f,c->w*kx*.12f);
            if (person) thick=.025f;
            if (c->cls==6 || c->cls==0x1c) thick=.10f;
            float base=c->big ? fminf(c->h*ky*.22f,thick*.6f):0;
            if(c->shape==WM_OPEN || c->shape==WM_TRACK) base=0;
            if(c->shape==WM_GROUND) {thick=c->w*kx*.4f;base=c->type==4 ? .35f:0;}
            WMShell shell={.x=c->x,.y=c->y,.kx=kx,.ky=ky,.depth=thick,.base=base,
                .ax=c->ax,.ay=c->ay,.w=c->w,.h=c->h,.shape=c->shape,
                .rx=-sinf(angle),.ry=cosf(angle),.dx=cosf(angle),.dy=sinf(angle)};
            wm_shell(&mr,&shell,dist); rendered++;
        }
        if (getenv("TP_WALK_MESH_LOG")) {
            static double last; if (now()-last>1) { last=now();
                fprintf(stderr,"pixel mesh: %d objects (%d Reimagined rigs, %d Faithful layers), %u triangles, %u fragments, %.2f ms, %dx%d\n",
                    rendered,rigs_drawn,layers_drawn,mr.triangles,mr.fragments,(now()-began)*1000,ow,oh); }
        }
        pthread_mutex_unlock(&lock); return 1;
    }
    /* original cards, far to near (TP_WALK_MESH=0, for A/B checks) */
    static Vis vis[MAXCARDS]; int nv = 0;
    int hide = W.mode == MODE_STREET && W.follow ? W.follow : 0;
    for (int i = 0; i < s->ncards; i++) {
        const Card *c = &s->cards[i];
        if (hide && c->idx == hide) continue;
        /* A big thing's picture shows its front, from its footprint's front edge up: it stands on the
         * edge of its footprint nearest the camera (not the middle, where its own ground showed in
         * front of it, as if it floated), whichever way it is seen from. */
        double px = c->x, py = c->y;
        if (c->r > 0) {
            double ex = cx - px, ey = cy - py, d = sqrt(ex * ex + ey * ey);
            if (d > 1e-3) {
                double m = fmax(fabs(ex), fabs(ey)) / d, k = fmin(c->r / m, c->r * 1.2);   /* (to the square's edge, corners a little short) */
                if (k > d - 0.3) k = d - 0.3 > 0 ? d - 0.3 : 0;
                px += ex / d * k; py += ey / d * k;
            }
        }
        double dx = px - cx, dy = py - cy, dz = -cz;
        double vz = dx * fx + dy * fy + dz * fz;
        if (vz < 0.12 || vz > FOG1) continue;
        vis[nv++] = (Vis){(float)vz, i, (float)px, (float)py};
    }
    qsort(vis, (size_t)nv, sizeof *vis, cmp_vis);
    double kx = W.hscale / g->tws, ky = W.hscale * g->aspect / g->tws;   /* cells per art pixel across, up */
    for (int k = 0; k < nv; k++) {
        const Card *c = &s->cards[vis[k].i];
        double dx = vis[k].x - cx, dy = vis[k].y - cy, dz = -cz;
        double vz = vis[k].z, vx = dx * rx + dy * ry, vy = dx * ux + dy * uy + dz * uz;
        double px = ow / 2.0 + f * vx / vz, py = oh / 2.0 - f * vy / vz;   /* the foot */
        double pw = f * kx / vz, ph = f * ky / vz;            /* screen pixels per art pixel */
        double left = px - c->ax * pw, top = py - c->ay * ph;
        int x0 = (int)ceil(left - 0.5), x1 = (int)ceil(left + c->w * pw - 0.5), y0 = (int)ceil(top - 0.5), y1 = (int)ceil(top + c->h * ph - 0.5);
        if (x1 <= 0 || y1 <= 0 || x0 >= ow || y0 >= oh) continue;
        if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 > ow) x1 = ow; if (y1 > oh) y1 = oh;
        double fog = (vz - FOG0) / (FOG1 - FOG0);
        const uint16_t *img = s->pix + c->off;
        for (int y = y0; y < y1; y++) {
            int iy = (int)((y + 0.5 - top) / ph); if (iy < 0 || iy >= c->h) continue;
            const uint16_t *row = img + (size_t)iy * c->w; uint32_t *o = out + (size_t)y * ow;
            for (int x = x0; x < x1; x++) {
                int ix = (int)((x + 0.5 - left) / pw); if (ix < 0 || ix >= c->w) continue;
                uint16_t v = row[ix]; if (v == 0xffff) continue;
                o[x] = fog > 0 ? mix(s->pal[v], fogc, fog) : s->pal[v];
            }
        }
    }
    pthread_mutex_unlock(&lock);
    return 1;
}

/* ---- development and tests ---- */
/* The floor's palette index at world point (x, y), or -1. */
int tp_walk_floor_at(double x, double y) {
    pthread_mutex_lock(&lock);
    const Snap *s = front >= 0 ? &snaps[front] : NULL; int c = -1;
    if (s && s->valid && x >= 0 && y >= 0 && x < MAPE && y < MAPE) {
        int T = s->T, X = (int)x, Y = (int)y, u = (int)((x - X) * T), v = (int)((y - Y) * T);
        c = floor_tex[s->fi][(size_t)(Y * T + v) * MAPN * T + (size_t)X * T + u];
    }
    pthread_mutex_unlock(&lock);
    return c;
}
/* Thing idx's card: place (cells), size and foot (art pixels); 0 if it has none. */
int tp_walk_card_of(int idx, float *x, float *y, int *w, int *h, int *ax, int *ay) {
    pthread_mutex_lock(&lock);
    const Snap *s = front >= 0 ? &snaps[front] : NULL; int ok = 0;
    for (int i = 0; s && s->valid && i < s->ncards && !ok; i++) {
        const Card *c = &s->cards[i];
        if (c->idx != idx) continue;
        *x = c->x; *y = c->y; *w = c->w; *h = c->h; *ax = c->ax; *ay = c->ay; ok = 1;
    }
    pthread_mutex_unlock(&lock);
    return ok;
}
