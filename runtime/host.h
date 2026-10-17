/* Native DOS/DPMI platform host for the recompiled Theme Park (C port of
 * tools/dos.py). The compiled game blocks run on one thread; the app feeds
 * input through the thread-safe calls below and reads the VGA frame. */
#ifndef TP_HOST_H
#define TP_HOST_H
#include <stdint.h>

typedef struct TpScript {          /* deterministic test input (tools/dos.py) */
    struct { uint32_t at; uint32_t key; } keys[64];        /* poll:scan<<8|ascii */
    struct { uint32_t at, x, y; } clicks[64];               /* poll:x:y, released 50000 blocks later */
    struct { uint32_t at, x, y, buttons; } block_clicks[4096]; /* block:x:y[:buttons] */
    int nkeys, nclicks, nblock_clicks;
} TpScript;

/* deterministic: 1 = the verified scripted scheduling model (one timer tick
 * per idle keyboard poll, retrace toggling per read, fixed clock); 0 = real time. */
int  tp_host_init(const char *game_dir, const char *overlay_dir, int deterministic, const TpScript *script);
/* Run for at most `blocks` compiled blocks (deterministic) or `ms` milliseconds
 * (real time). Returns 0 to continue, 1 when the game exited, -1 on a stop. */
int  tp_host_run(uint32_t quantum, uint32_t blocks_limit, double ms);
const char *tp_host_status(void);
int  tp_host_exit_code(void);
uint64_t tp_host_blocks(void);

/* VGA output: 320x200 mode 13h converted through the DAC palette. Returns 0
 * when not in a graphics mode. */
int  tp_host_frame_rgba(uint32_t *out /* 320*200 */);
int  tp_host_png(const char *path, char sha256_hex[65]);

/* Input (any thread). key = scan<<8 | ascii, as INT 16h returns it. */
void tp_host_key(uint32_t key);
void tp_host_shift(uint32_t flags);
void tp_host_key_raw(uint8_t scan, int down);  /* make/break codes for programs with their own INT 9 */       /* BIOS shift-state byte (INT 16h AH=2) */
void tp_host_mouse(double fx, double fy, uint32_t buttons); /* fx/fy in 0..1 of the screen */
void tp_host_mouse_delta(double dx, double dy, uint32_t buttons); /* pixels at 320x200 */
void tp_host_quit(void);
void tp_host_set_realtime(void);
/* Sound: the app attaches an output and pulls PCM; MIDI bytes go to tp_audio_midi (weak). */
void tp_host_audio_attached(int on);
void sb16_render(float *out, int frames, double out_rate);
void tp_audio_midi(uint8_t byte);   /* switch a deterministic run to real time (tests) */
/* Emulated CPU speed in thousands of x86 instructions per second (DOSBox "cycles"); 0 = unthrottled. */
void tp_host_set_cycles(double per_ms);

int  tp_host_checkpoint(const char *path);   /* state snapshot for tools/spotcheck.py */

/* Native park view (view.c): the whole park drawn by passes of the game's own
 * renderer, shown at any zoom. Canvas coordinates are 320x200-scale pixels of
 * the park picture. */
void tp_view_enable(int main_game);           /* only for MAIN.EXE's code (not the intro) */
int  tp_view_set(int on, double cx, double cy, double zoom);   /* (cx, cy): canvas point at the park area's centre; 0: not now */
void tp_view_camera_center(double *cx, double *cy);          /* where the game's own view is */
void tp_view_canvas_size(double *w, double *h);
int  tp_view_live(void);                      /* the zoomed picture is current (the park is on screen) */
void tp_view_mouse(double fx, double fy, uint32_t buttons);  /* as tp_host_mouse; mapped when zoomed */
void tp_view_compose(uint32_t *out, int ow, int oh, const uint32_t *frame, int smooth);   /* ow x oh: 320x200 layout */
#define TP_VIEW_WORLD_ROWS 178                /* park rows of the 320x200 screen; the panel is below */

/* Saved files below the overlay (sha256), for tests. */
void tp_host_list_saved(void (*fn)(const char *rel, const char *sha256_hex, void *), void *ctx);
#endif
