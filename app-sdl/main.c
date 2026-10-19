/* Theme Park for Linux and Windows: an SDL2 window around the recompiled game code and the native DOS host
 * (runtime/host.c). The counterpart of app/main.m (macOS), kept in the same order so the two read side by side:
 * the game runs on its own thread; the window shows the game's frame (320x200 or its 640x480 SVGA mode) at 4:3,
 * or the zoomable widescreen park view (runtime/view.c), and forwards keyboard, mouse and game controller.
 * Data: TP_DATA, else ~/.local/share/ThemeParkNative (Linux) or %LOCALAPPDATA%\ThemeParkNative (Windows), with
 * game/ (the files from the player's own package, put there by the build script), saves/ and settings.ini.
 * MAIN.TPIMG and INTRO.TPIMG sit beside the program. Window coordinates here are SDL's: points, y down. */
#include <SDL.h>
#include <ctype.h>
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "host.h"
#include "platform.h"
#include "app.h"

struct Ctx;
void tp_select_code(int (*dispatch)(struct Ctx *), void (*init)(void));
int tp_dispatch(struct Ctx *); void tp_dispatch_init(void);
int intro_dispatch(struct Ctx *); void intro_dispatch_init(void);
void tp_qol_init(void);
extern long (*tp_extras_get)(const char *key); extern void (*tp_extras_set)(const char *key, long value);   /* (qol.c's EXTRAS menu) */
extern double (*tp_extras_getf)(const char *key); extern void (*tp_extras_setf)(const char *key, double value);
uint8_t *tp_host_memory(void);
int tp_host_png_rgba(const char *path, const uint32_t *px, int w, int h);
extern int tp_view_hw_cursor, tp_view_debug_fps, tp_view_debug_grid;
void tp_view_debug_hud(uint32_t *out, int ow, int oh);
void tp_view_count_game_frames(int on);
void tp_view_set_ui_scale(double k);
int tp_view_frame_due(void);
void tp_view_vsync_now(double period, double ago);
int tp_view_cursor_rgba(uint32_t *out, int *w, int *h, int *ox, int *oy);
void tp_coverage_dump(const char *path);

static double now(void) { return tp_platform_now(); }

/* The EXTRAS menu's settings (any thread). */
static long extras_get(const char *k) { return settings_get(k); }
static void extras_set(const char *k, long v) { settings_set(k, v); }
static double extras_getf(const char *k) { return settings_getf(k); }
static void extras_setf(const char *k, double v) {
    settings_setf(k, v);
    if (!strcmp(k, "MusicVolume") || !strcmp(k, "SoundVolume")) tp_audio_set_volumes((float)settings_getf("SoundVolume"), (float)settings_getf("MusicVolume"));
}

static char data_root[1024], game_dir[1100], saves_dir[1100], image_path[1100], intro_path[1100], base_dir[1024];
static void data_paths(void) {
    const char *env = getenv("TP_DATA");
    if (env && *env) snprintf(data_root, sizeof data_root, "%s", env);
#ifdef _WIN32
    else snprintf(data_root, sizeof data_root, "%s\\ThemeParkNative", getenv("LOCALAPPDATA") ? getenv("LOCALAPPDATA") : ".");
#else
    else if (getenv("XDG_DATA_HOME") && *getenv("XDG_DATA_HOME")) snprintf(data_root, sizeof data_root, "%s/ThemeParkNative", getenv("XDG_DATA_HOME"));
    else snprintf(data_root, sizeof data_root, "%s/.local/share/ThemeParkNative", getenv("HOME") ? getenv("HOME") : ".");
#endif
    snprintf(game_dir, sizeof game_dir, "%s/game", data_root);
    snprintf(saves_dir, sizeof saves_dir, "%s/saves", data_root);
    char *b = SDL_GetBasePath(); snprintf(base_dir, sizeof base_dir, "%s", b ? b : "./"); SDL_free(b);
    snprintf(image_path, sizeof image_path, "%sMAIN.TPIMG", base_dir);
}
static int exists(const char *p) { struct stat st; return stat(p, &st) == 0; }

/* ---- keyboard: SDL scan codes -> PC scan codes ---- */
static uint8_t pc_scan(SDL_Scancode s) {
    static const uint8_t letters[26] = {0x1e,0x30,0x2e,0x20,0x12,0x21,0x22,0x23,0x17,0x24,0x25,0x26,0x32,0x31,0x18,0x19,0x10,0x13,0x1f,0x14,0x16,0x2f,0x11,0x2d,0x15,0x2c};
    if (s >= SDL_SCANCODE_A && s <= SDL_SCANCODE_Z) return letters[s - SDL_SCANCODE_A];
    if (s >= SDL_SCANCODE_1 && s <= SDL_SCANCODE_0) return (uint8_t)(0x02 + (s - SDL_SCANCODE_1));
    if (s >= SDL_SCANCODE_F1 && s <= SDL_SCANCODE_F10) return (uint8_t)(0x3b + (s - SDL_SCANCODE_F1));
    switch (s) {
    case SDL_SCANCODE_RETURN: return 0x1c; case SDL_SCANCODE_ESCAPE: return 0x01; case SDL_SCANCODE_BACKSPACE: return 0x0e;
    case SDL_SCANCODE_TAB: return 0x0f; case SDL_SCANCODE_SPACE: return 0x39; case SDL_SCANCODE_MINUS: return 0x0c;
    case SDL_SCANCODE_EQUALS: return 0x0d; case SDL_SCANCODE_LEFTBRACKET: return 0x1a; case SDL_SCANCODE_RIGHTBRACKET: return 0x1b;
    case SDL_SCANCODE_BACKSLASH: return 0x2b; case SDL_SCANCODE_NONUSBACKSLASH: return 0x56; case SDL_SCANCODE_SEMICOLON: return 0x27;
    case SDL_SCANCODE_APOSTROPHE: return 0x28; case SDL_SCANCODE_GRAVE: return 0x29; case SDL_SCANCODE_COMMA: return 0x33;
    case SDL_SCANCODE_PERIOD: return 0x34; case SDL_SCANCODE_SLASH: return 0x35;
    case SDL_SCANCODE_F11: return 0x85; case SDL_SCANCODE_F12: return 0x86;
    case SDL_SCANCODE_LEFT: return 0x4b; case SDL_SCANCODE_RIGHT: return 0x4d; case SDL_SCANCODE_DOWN: return 0x50; case SDL_SCANCODE_UP: return 0x48;
    case SDL_SCANCODE_HOME: return 0x47; case SDL_SCANCODE_END: return 0x4f; case SDL_SCANCODE_PAGEUP: return 0x49; case SDL_SCANCODE_PAGEDOWN: return 0x51;
    case SDL_SCANCODE_DELETE: return 0x53; case SDL_SCANCODE_INSERT: return 0x52;
    case SDL_SCANCODE_KP_0: return 0x52; case SDL_SCANCODE_KP_1: return 0x4f; case SDL_SCANCODE_KP_2: return 0x50; case SDL_SCANCODE_KP_3: return 0x51;
    case SDL_SCANCODE_KP_4: return 0x4b; case SDL_SCANCODE_KP_5: return 0x4c; case SDL_SCANCODE_KP_6: return 0x4d; case SDL_SCANCODE_KP_7: return 0x47;
    case SDL_SCANCODE_KP_8: return 0x48; case SDL_SCANCODE_KP_9: return 0x49; case SDL_SCANCODE_KP_ENTER: return 0x1c; case SDL_SCANCODE_KP_PLUS: return 0x4e;
    case SDL_SCANCODE_KP_MINUS: return 0x4a; case SDL_SCANCODE_KP_MULTIPLY: return 0x37; case SDL_SCANCODE_KP_DIVIDE: return 0x35; case SDL_SCANCODE_KP_PERIOD: return 0x53;
    default: return 0;
    }
}
/* BIOS codes for function keys with Shift/Ctrl/Alt (F1..F10). */
static uint32_t function_key(uint8_t scan, SDL_Keymod m) {
    if (scan >= 0x3b && scan <= 0x44) {
        if (m & KMOD_ALT) return (uint32_t)(scan - 0x3b + 0x68) << 8;
        if (m & KMOD_CTRL) return (uint32_t)(scan - 0x3b + 0x5e) << 8;
        if (m & KMOD_SHIFT) return (uint32_t)(scan - 0x3b + 0x54) << 8;
    }
    return (uint32_t)scan << 8;
}
static uint32_t key_of_char(int c) {
    static const char *row1 = "1234567890", *q = "QWERTYUIOP", *a = "ASDFGHJKL", *z = "ZXCVBNM";
    c = toupper(c);
    for (int i = 0; row1[i]; i++) if (row1[i] == c) return (uint32_t)(0x02 + i) << 8 | (uint32_t)c;
    for (int i = 0; q[i]; i++) if (q[i] == c) return (uint32_t)(0x10 + i) << 8 | (uint32_t)c;
    for (int i = 0; a[i]; i++) if (a[i] == c) return (uint32_t)(0x1e + i) << 8 | (uint32_t)c;
    for (int i = 0; z[i]; i++) if (z[i] == c) return (uint32_t)(0x2c + i) << 8 | (uint32_t)c;
    if (c == ' ') return 0x3920;
    return 0;
}

/* ---- the window and what it shows ---- */
typedef struct { double x, y, w, h; } Rect;
static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Texture *tex; static int tex_w, tex_h, tex_smooth = -1;
static SDL_Texture *scan_tex; static int scan_rows;
static Rect shown;                                  /* where the picture goes (points) */
static int show_scan;
static uint32_t pixels[640 * 480];
static double last_lw;                              /* the layout width last given to view.c */
static int cam_moves;                               /* tp_view_camera_moves seen */
static int panning; static double pan_fx, pan_fy;   /* dragging the park view */
static int sw, sh, wh;                              /* the game's screen now: size, park rows (tp_view_geometry) */
static int cursor_hidden;
static SDL_Cursor *game_cursor, *sys_cursor; static int cur_serial; static uint32_t cur_hash; static double cur_kx, cur_ky;
static uint32_t buttons;
/* The park view: on when zoomed, or when the window is wider than 4:3 (widescreen: the park fills it, the
 * interface stays in a centred 4:3 column); off, the game draws its own view. */
static int view_on, live, view_moved, menu_wide;
static double zoom = 1, cx, cy, off_at;
static uint32_t *big; static int big_w, big_h;      /* the last park picture (RGBA) */
static uint32_t *dbg; static int dbg_w, dbg_h;
static double mouse_x, mouse_y; static int mouse_in, focused = 1;
/* Visitor View (runtime/walk.c): Ctrl+Shift+V, then a click in the park drops you there */
static int walk_drop, wk[8], wk_run;
static double walk_last, look_dx, look_dy, hold[3], hold_until, pad[4];
static uint32_t *walk_px; static int walk_w, walk_h;
static char hud_text[256], subtitle[160];
static int full;

static void set_subtitle(const char *s) {
    snprintf(subtitle, sizeof subtitle, "%s", s);
    char t[300]; snprintf(t, sizeof t, *s ? "Theme Park - %s" : "Theme Park", s);
    SDL_SetWindowTitle(win, t);
}
static Rect bounds(void) { int w, h; SDL_GetWindowSize(win, &w, &h); return (Rect){0, 0, w, h}; }
static double pixel_scale(void) { int w, h, pw, ph; SDL_GetWindowSize(win, &w, &h); SDL_GetRendererOutputSize(ren, &pw, &ph); return w > 0 ? (double)pw / w : 1; }
/* The game's own 4:3 picture (or the whole window, stretched). */
static Rect screen_rect(void) {
    Rect b = bounds(); double w = b.w, h = b.h;
    if (settings_get("Stretch")) return b;
    if (w * 3 > h * 4) w = h * 4 / 3; else h = w * 3 / 4;
    return (Rect){floor((b.w - w) / 2), floor((b.h - h) / 2), floor(w), floor(h)};
}
static int wide(void) { Rect b = bounds(); return settings_get("Widescreen") && b.h > 0 && b.w * 3 > b.h * 4 * 1.02; }
/* What the park view occupies, and its width in the game screen's pixels. */
static Rect view_rect(void) { return wide() ? bounds() : screen_rect(); }
static double layout_w(void) { Rect r = view_rect(); return wide() ? sw * (r.w / r.h) * 0.75 : sw; }
/* The game's own screen inside the wide view: the column its interface takes there (the panel's). */
static Rect column_rect(void) {
    Rect v = view_rect(); double lw = last_lw > 0 ? last_lw : layout_w(), w = v.w * sw / lw;
    return (Rect){v.x + (v.w - w) / 2, v.y, w, v.h};
}
static Rect pointer_rect(void) { return live ? view_rect() : menu_wide ? column_rect() : screen_rect(); }

static void set_cursor_hidden(int hidden) {
    if (hidden == cursor_hidden) return;
    cursor_hidden = hidden; SDL_ShowCursor(hidden ? SDL_DISABLE : SDL_ENABLE);
}
static void use_cursor(SDL_SystemCursor id) {
    static SDL_SystemCursor last = SDL_NUM_SYSTEM_CURSORS;
    if (id != last) { if (sys_cursor) SDL_FreeCursor(sys_cursor); sys_cursor = SDL_CreateSystemCursor(id); last = id; }
    SDL_SetCursor(sys_cursor);
}
static void send_mouse(double x, double y) {
    mouse_x = x; mouse_y = y; mouse_in = 1;
    if (walk_drop || tp_walk_active()) { set_cursor_hidden(0); return; }   /* (the Visitor View takes the pointer) */
    Rect r = pointer_rect();
    double fx = (x - r.x) / r.w, fy = (y - r.y) / r.h;
    int inside = fx >= 0 && fx < 1 && fy >= 0 && fy < 1;
    /* The game's own screen draws its cursor: the system's hides over it. The park view shows the game's cursor
     * as the system cursor instead (smooth at the display's rate). */
    set_cursor_hidden(inside && !live && focused && !settings_get("ShowMacCursor"));
    fx = fx < 0 ? 0 : fx > 0.9999 ? 0.9999 : fx;
    fy = fy < 0 ? 0 : fy > 0.9999 ? 0.9999 : fy;
    if (live) tp_view_mouse(fx, fy, buttons);   /* mapped into the park */
    else tp_host_mouse(fx, fy, buttons);        /* the game's own screen */
}

/* The game's cursor (view.c reads it from the game) as the system cursor over the park view, at the view's scale. */
static void update_game_cursor(void) {
    int want = live && !walk_drop && !settings_get("ShowMacCursor");
    tp_view_hw_cursor = want;
    if (walk_drop) { use_cursor(SDL_SYSTEM_CURSOR_CROSSHAIR); return; }
    if (tp_walk_active()) { use_cursor(SDL_SYSTEM_CURSOR_HAND); return; }
    if (!want) {
        if (game_cursor) { use_cursor(SDL_SYSTEM_CURSOR_ARROW); SDL_FreeCursor(game_cursor); game_cursor = NULL; cur_serial = 0; }
        return;
    }
    static uint32_t px[64 * 64]; int w = 0, h = 0, ox = 0, oy = 0;
    int serial = tp_view_cursor_rgba(px, &w, &h, &ox, &oy);
    Rect vr = view_rect();
    double kx = vr.w / layout_w(), ky = vr.h / sh;   /* points per game pixel */
    /* (its colours too: a new game fades the palette in from black) */
    uint32_t hash = 2166136261u; for (int y = 0; y < h && y < 64; y++) for (int x = 0; x < w && x < 64; x++) hash = (hash ^ px[y * 64 + x]) * 16777619u;
    if (serial == cur_serial && hash == cur_hash && fabs(kx - cur_kx) < 0.01 && fabs(ky - cur_ky) < 0.01 && (game_cursor || !serial)) return;
    cur_serial = serial; cur_hash = hash; cur_kx = kx; cur_ky = ky;
    if (game_cursor) { use_cursor(SDL_SYSTEM_CURSOR_ARROW); SDL_FreeCursor(game_cursor); game_cursor = NULL; }
    if (!serial || w <= 0 || h <= 0) return;
    double cap = getenv("TP_CURSOR_CAP") ? atof(getenv("TP_CURSOR_CAP")) : 64;
    if (w * kx > cap || h * ky > cap) { double f = cap / fmax(w * kx, h * ky); kx *= f; ky *= f; }
    double scale = pixel_scale();
    int pw = (int)lround(w * kx * scale), ph = (int)lround(h * ky * scale);
    if (pw < 1 || ph < 1) return;
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, pw, ph, 32, SDL_PIXELFORMAT_RGBA32);
    if (!s) return;
    for (int y = 0; y < ph; y++) for (int x = 0; x < pw; x++) {
        uint32_t c = px[(y * h / ph) * 64 + x * w / pw];
        uint8_t *o = (uint8_t *)s->pixels + (size_t)y * s->pitch + x * 4;
        if (!c) { o[0] = o[1] = o[2] = o[3] = 0; continue; }
        o[0] = c & 255; o[1] = (c >> 8) & 255; o[2] = (c >> 16) & 255; o[3] = 255;
    }
    game_cursor = SDL_CreateColorCursor(s, (int)lround(fmax(0, -ox * kx) * scale), (int)lround(fmax(0, -oy * ky) * scale));
    SDL_FreeSurface(s);
    if (game_cursor) SDL_SetCursor(game_cursor);
}

/* ---- the park view: zoom and widescreen ---- */
static void clamp_center(void) {
    double w, h; tp_view_canvas_size(&w, &h);
    double hw = layout_w() / 2.0 / zoom, top = wh / 2.0 / zoom;
    double bottom = (wide() ? sh - wh / 2.0 : wh / 2.0) / zoom;   /* wide: the park shows down to the bottom beside the panel */
    cx = hw * 2 >= w ? w / 2 : fmin(fmax(cx, hw), w - hw);
    cy = top + bottom >= h ? (h - bottom + top) / 2 : fmin(fmax(cy, top), h - bottom);
}
/* The game's screen mode decides the canvas scale; when it changes ([R] in the park), the view carries over. */
static void update_geometry(void) {
    int nsw, nsh, nwh; tp_view_geometry(&nsw, &nsh, &nwh);
    if (nsw == sw) return;
    double k = sw ? (double)nsw / sw : 1;
    sw = nsw; sh = nsh; wh = nwh;
    if (view_on) { cx *= k; cy *= k; clamp_center(); last_lw = layout_w(); tp_view_set(1, cx, cy, zoom, last_lw); }
}
static double min_zoom(void) { double w, h; tp_view_canvas_size(&w, &h); return fmax(0.1, fmin(layout_w() / w, wh / h)); }
/* Turns the park view on or off as zoom and the window's shape ask, and tells view.c where it looks. */
static void apply_view(void) {
    int on = zoom != 1 || wide();
    if (on && !view_on && now() - off_at > 1.0) tp_view_camera_center(&cx, &cy);   /* start where the game looks */
    if (on) clamp_center();
    if (!on && view_on) off_at = now();
    last_lw = layout_w();
    if (!tp_view_set(on, cx, cy, zoom, last_lw)) { on = 0; zoom = 1; }   /* not in the park (the intro, say) */
    view_on = on;
}
static void apply_view_keeping_center(void) {
    int on = zoom != 1 || wide();
    if (on) clamp_center();
    if (!on) off_at = now();
    last_lw = layout_w();
    if (!tp_view_set(on, cx, cy, zoom, last_lw)) { on = 0; zoom = 1; }
    view_on = on;
}
/* Zoom to z keeping the park point under (px, py) (window points; NAN: the centre) in place. */
static void zoom_to(double z, double px, double py) {
    update_geometry();
    if (!sw) return;
    if (!view_on) { if (now() - off_at > 1.0) tp_view_camera_center(&cx, &cy); zoom = 1; }
    Rect r = view_rect(); double lw = layout_w();
    double fx = isnan(px) ? 0.5 : (px - r.x) / r.w;
    double fy = isnan(px) ? wh / 2.0 / sh : (py - r.y) / r.h;
    double ax = cx + (fx - 0.5) * lw / zoom, ay = cy + (fy * sh - wh / 2.0) / zoom;
    z = fmin(fmax(z, min_zoom()), 4.0);
    if (fabs(z - 1) < 0.04) z = 1;                   /* a detent at the game's own scale */
    zoom = z; view_moved = 1;
    cx = ax - (fx - 0.5) * lw / z; cy = ay - (fy * sh - wh / 2.0) / z;
    apply_view_keeping_center();
}
static void pan_by(double dx, double dy) {          /* window points */
    if (!view_on) return;
    view_moved = 1;
    double k = layout_w() / zoom / view_rect().w;
    cx += dx * k; cy += dy * k;
    clamp_center();
    tp_view_set(1, cx, cy, zoom, last_lw = layout_w());
}
static void center_on(double ncx, double ncy) {     /* canvas point (development control) */
    update_geometry();
    if (!view_on && zoom == 0) zoom = 1;
    cx = ncx; cy = ncy; apply_view_keeping_center();
}
static void edge_pan(void) {                        /* the pointer at the window's edge scrolls, as in the game */
    if (!mouse_in || !focused || !live) return;
    Rect r = view_rect(); double x = mouse_x, y = mouse_y, m = 6, v = 9;
    double fx = (x - r.x) / r.w, fy = (y - r.y) / r.h;
    double lw = layout_w(), col = (lw - sw) / 2 / lw;
    int panel = fy > (double)wh / sh && fx >= col && fx < 1 - col;   /* over the panel: only its outer edge scrolls */
    double dx = x < r.x + m ? -v : x > r.x + r.w - m ? v : 0;
    double dy = y < r.y + m ? -v : y > r.y + r.h - m ? v : 0;    /* (corners: both, diagonally) */
    if (panel && !(y > r.y + r.h - m) && !(x < r.x + m) && !(x > r.x + r.w - m)) return;
    if (dx || dy) pan_by(dx, dy);
}
static void zoom_in(void) { zoom_to(zoom * 1.25, NAN, NAN); }
static void zoom_out(void) { zoom_to(zoom / 1.25, NAN, NAN); }
/* A pointer from elsewhere (a game controller, control runs), in 0..1 of what is shown. */
static void pointer_at(double fx, double fy, uint32_t b) {
    if (menu_wide) { Rect v = view_rect(), c = column_rect(); fx = (fx * v.w - (c.x - v.x)) / c.w; fx = fmin(fmax(fx, 0), 0.9999); }   /* (into the column) */
    if (live) tp_view_mouse(fx, fy, b); else tp_host_mouse(fx, fy, b);
}

/* ---- presentation: pictures into a texture, drawn every refresh ---- */
static void upload(const uint32_t *px, int w, int h, Rect where, int smooth) {
    if (!tex || tex_w != w || tex_h != h) {
        if (tex) SDL_DestroyTexture(tex);
        tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, w, h);
        tex_w = w; tex_h = h; tex_smooth = -1;
    }
    if (!tex) return;
    if (smooth != tex_smooth) { SDL_SetTextureScaleMode(tex, smooth ? SDL_ScaleModeLinear : SDL_ScaleModeNearest); tex_smooth = smooth; }
    SDL_UpdateTexture(tex, NULL, px, w * 4);
    shown = where;
}
/* CRT: a dark line under every row of the game's screen, over the picture (smooth pixels). */
static void update_scanlines(int rows) {
    show_scan = settings_get("SmoothPixels") == 2;
    if (!show_scan || rows == scan_rows) return;
    scan_rows = rows;
    if (scan_tex) SDL_DestroyTexture(scan_tex);
    int h = rows * 4; uint32_t *px = calloc((size_t)h, 4);
    for (int y = 0; y < h; y++) px[y] = (y % 4 == 3) ? 0x70000000u : (y % 4 == 2 ? 0x30000000u : 0);
    scan_tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, 1, h);
    if (scan_tex) { SDL_UpdateTexture(scan_tex, NULL, px, 4); SDL_SetTextureBlendMode(scan_tex, SDL_BLENDMODE_BLEND); SDL_SetTextureScaleMode(scan_tex, SDL_ScaleModeLinear); }
    free(px);
}
static void present_walk(void);
static void present(void) {
    if (tp_walk_active()) { present_walk(); return; }
    if (*hud_text) { hud_text[0] = 0; set_subtitle(""); }
    int w, h;
    if (!tp_host_frame_size(&w, &h) || !tp_host_frame_rgba_any(pixels, 640 * 480)) return;
    int fw = w, fh = h; int smooth = settings_get("SmoothPixels") != 0;
    update_geometry();
    if ((zoom != 1 || wide()) != view_on || (view_on && fabs(layout_w() - last_lw) > 0.5)) apply_view();
    if (view_on && tp_view_camera_moves() != cam_moves) {   /* the game moved its camera itself: look there */
        cam_moves = tp_view_camera_moves();
        tp_view_camera_center(&cx, &cy); clamp_center();
        tp_view_set(1, cx, cy, zoom, last_lw = layout_w());
    }
    if (view_on) edge_pan();
    int now_live = view_on && tp_view_live() && fw == sw;
    if (now_live && live && big && !view_moved && !tp_view_frame_due()) return;   /* steady pacing: the last picture stays up */
    view_moved = 0;
    if (now_live) {                                  /* the park view, the game's interface and its panel */
        Rect vr = view_rect();
        int k = (int)lround(screen_rect().w * pixel_scale() / fw); k = k < 1 ? 1 : k > 6 ? 6 : k;
        if (fw == 320 && k < 2) k = 2;
        /* square output pixels over the view's shape (the layout maps onto it, 320x200's tall pixels included) */
        int oh = fh * k, ow = (int)lround(oh * vr.w / vr.h);
        if (ow < 16) ow = 16;
        if (!big || big_w != ow || big_h != oh) { free(big); big_w = ow; big_h = oh; big = malloc(4 * (size_t)ow * oh); }
        double tc0 = now();
        tp_view_compose(big, ow, oh, pixels, fw, fh, smooth);
        if (getenv("TP_PACE_DEBUG")) { static double l, worst, sum; static int n; double d = now() - tc0; sum += d; n++; if (d > worst) worst = d;
            if (tc0 - l > 1) { fprintf(stderr, "compose %dx%d: avg %.2f ms, worst %.2f ms\n", ow, oh, sum / n * 1e3, worst * 1e3); l = tc0; worst = sum = 0; n = 0; } }
        if (!live || menu_wide) { live = 1; menu_wide = 0; set_cursor_hidden(0); }
        upload(big, ow, oh, vr, smooth);
        update_game_cursor();
        update_scanlines(fh);
        return;
    }
    /* A full-screen game window (the ride and shop catalogues, the research and bank screens) in the wide view:
     * the last park picture stays at the sides (the park is not drawn meanwhile), the game's screen in the column
     * its interface takes in the park view, every frame. */
    if (!now_live && view_on && wide() && fw == sw && big && !tp_view_debug_fps) {
        double lw = last_lw > 0 ? last_lw : layout_w();
        int cw = (int)lround(big_w * sw / lw), cx0 = (big_w - cw) / 2; if (cw > big_w) cw = big_w; if (cx0 < 0) cx0 = 0;
        for (int y = 0; y < big_h; y++) {
            uint32_t *d = big + (size_t)y * big_w; const uint32_t *row = pixels + (size_t)(y * fh / big_h) * fw;
            for (int x = 0; x < cw; x++) d[cx0 + x] = row[(size_t)x * fw / cw];
        }
        if (!menu_wide || live) { menu_wide = 1; live = 0; }
        upload(big, big_w, big_h, view_rect(), smooth);
        update_game_cursor();
        update_scanlines(fh);
        return;
    }
    menu_wide = 0;
    const uint32_t *px = pixels;
    if (tp_view_debug_fps) {                          /* the frame-rate line over the game's own picture: drawn at 3x */
        int ow = fw * 3, oh = fh * 3;
        if (!dbg || dbg_w != ow || dbg_h != oh) { free(dbg); dbg_w = ow; dbg_h = oh; dbg = malloc(4 * (size_t)ow * oh); }
        for (int y = 0; y < oh; y++) for (int x = 0; x < ow; x++) dbg[(size_t)y * ow + x] = pixels[(y / 3) * fw + x / 3];
        tp_view_debug_hud(dbg, ow, oh);
        px = dbg; w = ow; h = oh;
    }
    if (now_live != live) { live = now_live; if (live) set_cursor_hidden(0); }
    upload(px, w, h, screen_rect(), smooth);
    update_game_cursor();
    update_scanlines(fh);
}
static void draw(void) {
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    if (tex && shown.w > 0) {
        double k = pixel_scale();
        SDL_Rect d = {(int)lround(shown.x * k), (int)lround(shown.y * k), (int)lround(shown.w * k), (int)lround(shown.h * k)};
        SDL_RenderCopy(ren, tex, NULL, &d);
        if (show_scan && scan_tex && !tp_walk_active()) SDL_RenderCopy(ren, scan_tex, NULL, &d);
    }
}

/* ---- the Visitor View (runtime/walk.c) ---- */
static void end_drop(void) { walk_drop = 0; set_subtitle(""); update_game_cursor(); }
static int walk_start_at(double x, double y) {
    if (!tp_walk_start(x, y)) return 0;
    end_drop();
    memset(wk, 0, sizeof wk); walk_last = 0; look_dx = look_dy = 0; hold_until = 0;
    set_cursor_hidden(0);
    if (game_cursor) { SDL_FreeCursor(game_cursor); game_cursor = NULL; cur_serial = 0; }
    return 1;
}
static void walk_stop(void) {
    tp_walk_stop();
    hud_text[0] = 0; set_subtitle("");
    use_cursor(SDL_SYSTEM_CURSOR_ARROW);
}
static void visitor_view(void) {
    if (tp_walk_active()) { walk_stop(); return; }
    if (walk_drop) { end_drop(); return; }
    walk_drop = 1;
    if (game_cursor) { SDL_FreeCursor(game_cursor); game_cursor = NULL; cur_serial = 0; }
    set_subtitle("Visitor View: click where you want to stand (Esc cancels)");
    use_cursor(SDL_SYSTEM_CURSOR_CROSSHAIR);
}
/* W/Up forward, S/Down back, A/D sideways, Q/E or Left/Right turn, Shift runs; F follow a visitor, Space hop to
 * another, R ride along, V eye level or diorama, N walk through things, Esc leave. */
static void walk_key(SDL_Scancode s, int down) {
    int k = s == SDL_SCANCODE_W || s == SDL_SCANCODE_UP ? 0 : s == SDL_SCANCODE_S || s == SDL_SCANCODE_DOWN ? 1 : s == SDL_SCANCODE_A ? 2
          : s == SDL_SCANCODE_D ? 3 : s == SDL_SCANCODE_Q || s == SDL_SCANCODE_LEFT ? 4 : s == SDL_SCANCODE_E || s == SDL_SCANCODE_RIGHT ? 5 : -1;
    if (k >= 0) { wk[k] = down; return; }
    if (!down) return;
    if (s == SDL_SCANCODE_ESCAPE) walk_stop();
    else if (s == SDL_SCANCODE_F) tp_walk_command(TP_WALK_FOLLOW);
    else if (s == SDL_SCANCODE_SPACE) tp_walk_command(TP_WALK_HOP);
    else if (s == SDL_SCANCODE_R) tp_walk_command(TP_WALK_RIDE);
    else if (s == SDL_SCANCODE_V) tp_walk_command(TP_WALK_VIEW);
    else if (s == SDL_SCANCODE_N) tp_walk_command(TP_WALK_NOCLIP);
}
static void present_walk(void) {
    double t = now(), dt = walk_last ? t - walk_last : 1.0 / 60; walk_last = t;
    double fwd = wk[0] - wk[1] + pad[0], strafe = wk[3] - wk[2] + pad[1], turn = wk[5] - wk[4];
    if (t < hold_until) { fwd += hold[0]; strafe += hold[1]; turn += hold[2]; }
    fwd = fmax(-1, fmin(1, fwd)); strafe = fmax(-1, fmin(1, strafe)); turn = fmax(-1, fmin(1, turn));
    tp_walk_input(fwd, strafe, turn, look_dx + pad[2] * dt * 2.2, look_dy + pad[3] * dt * 1.6, wk_run, dt);
    look_dx = look_dy = 0;
    /* drawn at a chunky size (the game's pixels are big too) and scaled up without smoothing */
    Rect b = bounds(); if (b.w < 8 || b.h < 8) return;
    long rows = settings_get("WalkRows"); if (rows < 150 || rows > 1200) rows = 450;
    int oh = (int)rows, ow = (int)lround(oh * b.w / b.h);
    if (!walk_px || walk_w != ow || walk_h != oh) { free(walk_px); walk_w = ow; walk_h = oh; walk_px = calloc((size_t)ow * oh, 4); }
    if (!tp_walk_render(walk_px, ow, oh)) for (int i = 0; i < ow * oh; i++) walk_px[i] = 0xff000000u;   /* (the first picture is on its way) */
    upload(walk_px, ow, oh, b, 0);
    char st[256]; tp_walk_status(st, sizeof st);   /* (the status line goes in the title: no text drawing here) */
    if (strcmp(st, hud_text)) { snprintf(hud_text, sizeof hud_text, "%s", st); set_subtitle(*st ? st : "Visitor View"); }
}

/* ---- the game ---- */
static volatile int game_done, game_result;
static void env_default(const char *name, long value) {
    if (getenv(name)) return;
    char v[32]; snprintf(v, sizeof v, "%ld", value); setenv(name, v, 1);
}
/* The settings into the environment, where the C modules read them (qol.c, freemode.c, bigmap.c, view.c; a
 * variable already set wins, for tests). On the main thread before the game thread starts (issue #3). */
static void settings_to_env(void) {
    if (!getenv("TP_NATIVE_PASS") && !settings_get("NativePasses")) setenv("TP_NATIVE_PASS", "0", 1);
    env_default("TP_EW_GAP", settings_get("GrossOutGap"));
    env_default("TP_CASH_GAP", settings_get("PurchaseGap"));
    env_default("TP_GAME_SOUND_VOL", lround(settings_getf("GameSoundVolume") * 100));   /* (qol.c: each effect by its kind) */
    env_default("TP_EW_VOL", lround(settings_getf("EwVolume") * 100));
    env_default("TP_CASH_VOL", lround(settings_getf("PurchaseVolume") * 100));
    env_default("TP_SYSTEM_SOUND_VOL", lround(settings_getf("SystemSoundVolume") * 100));
    env_default("TP_RIDE_MUSIC", settings_get("RideMusic"));
    env_default("TP_PARK_TUNE", settings_get("ParkTune"));
    env_default("TP_FREE_VISITORS", settings_get("FreeVisitors"));
    env_default("TP_FREE_BUS", settings_get("FreeBus"));
    env_default("TP_FREE_RULES", settings_get("FreeRules"));   /* (freemode.c: 0 sandbox, 1 endless, 2 true freeplay) */
    env_default("TP_FREE_RAMP", settings_get("FreeRamp"));
    env_default("TP_BIG_MAPS", settings_get("FreeMapSize") == 128);   /* (bigmap.c: room for 128x128 sandbox parks) */
    {   /* a saved 128x128 park (its marker, <save>.big): room to load it whatever the setting */
        int found = 0; DIR *d = opendir(saves_dir); struct dirent *e;
        while (d && (e = readdir(d))) { size_t n = strlen(e->d_name); if (n > 4 && !strcmp(e->d_name + n - 4, ".big")) found = 1; }
        if (d) closedir(d);
        env_default("TP_BIG_ROOM", found);
    }
    if (!settings_get("SteadyPacing") && !getenv("TP_PACING")) setenv("TP_PACING", "dosbox", 1);
    char tunes[464]; settings_gets("RideTunes", tunes, sizeof tunes);
    if (*tunes && !getenv("TP_RIDE_TUNES")) setenv("TP_RIDE_TUNES", tunes, 1);
}
static int game_thread(void *arg) {
    (void)arg;
    /* THEME.BAT runs INTRO.EXE, then MAIN.EXE. */
    if (*intro_path) {
        tp_host_set_image(intro_path);
        tp_select_code(intro_dispatch, intro_dispatch_init);
        tp_host_set_args("");
        if (!tp_host_init(game_dir, saves_dir, 0, NULL)) {
            int r; do r = tp_host_run(4000, 0, 50); while (r == 0);
            fprintf(stderr, "intro ended: %s\n", tp_host_status());
            if (!strcmp(tp_host_status(), "quit")) { game_result = 1; game_done = 1; return 0; }
        }
        tp_select_code(tp_dispatch, tp_dispatch_init);
    }
    tp_host_set_image(image_path);
    {   /* the game's own -l option (GOG's THEME.BAT passes -l0) */
        long lang = settings_get("Language");
        char args[16] = ""; if (lang > 0 && lang < 5) snprintf(args, sizeof args, " -l%ld", lang);
        tp_host_set_args(args);
    }
    for (;;) {
        tp_view_enable(1);                               /* the zoomable park view knows MAIN.EXE's code */
        tp_view_request_hires(settings_get("StartHiRes") || getenv("TP_START_HIRES"));
        if (tp_host_init(game_dir, saves_dir, 0, NULL)) { game_result = -1; break; }
        tp_extras_get = extras_get; tp_extras_set = extras_set; tp_extras_getf = extras_getf; tp_extras_setf = extras_setf;
        tp_qol_init();                                   /* quality-of-life hooks (qol.c), MAIN.EXE's code */
        if (tp_view_debug_fps) tp_view_count_game_frames(1);
        int r;
        do r = tp_host_run(4000, 0, 50); while (r == 0);
        game_result = r;
        /* THEME.BAT restarts the game when it exits with errorlevel >= 10. */
        if (r == 1 && tp_host_exit_code() >= 10) { tp_walk_stop(); continue; }   /* (Visitor View does not outlive its game, issue #6) */
        break;
    }
    game_done = 1;
    return 0;
}

static void fail(const char *message) {
    fprintf(stderr, "stopped: %s\n", message);
    if (!getenv("DS_TEST")) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Theme Park stopped", message, win);
}

/* Full screen: the window over the whole display (F11 or Alt+Enter switches back). */
static void toggle_full(void) {
    full = !full;
    SDL_SetWindowFullscreen(win, full ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    int w, h; SDL_GetWindowSize(win, &w, &h);
    fprintf(stderr, "full screen %d (%dx%d)\n", full, w, h);
}
static void toggle_fps(void) { tp_view_debug_fps = !tp_view_debug_fps; tp_view_count_game_frames(tp_view_debug_fps); settings_set("DebugFPS", tp_view_debug_fps); }
static void toggle_grid(void) { tp_view_debug_grid = !tp_view_debug_grid; settings_set("DebugGrid", tp_view_debug_grid); }
/* Larger / Smaller Panel (remembered): the panel and the advisor. */
static void panel_step(int d) {
    static const double steps[] = {0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0};
    double k = settings_getf("PanelScale"); int i = 0;
    for (int j = 0; j < 7; j++) if (fabs(steps[j] - k) < fabs(steps[i] - k)) i = j;
    i += d; if (i < 0) i = 0; if (i > 6) i = 6;
    settings_setf("PanelScale", steps[i]);
    tp_view_set_ui_scale(steps[i]);
}
/* Save Park Picture (Ctrl+E): the whole park as one PNG in <data>/pictures (the view renders it on the game's thread). */
static void save_picture(void) {
    tp_view_request_poster();
    double t0 = now(); uint32_t *px = NULL; int w = 0, h = 0;
    while (!tp_view_take_poster(&px, &w, &h) && now() - t0 < 3) SDL_Delay(20);
    if (!px) { if (!getenv("DS_TEST")) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "No park on screen", "Open your park in the game, then save its picture.", win); return; }
    char path[1200];
    if (getenv("TP_PICTURE")) snprintf(path, sizeof path, "%s", getenv("TP_PICTURE"));     /* self-test */
    else {
        char dir[1100]; snprintf(dir, sizeof dir, "%s/pictures", data_root); tp_mkdir(dir);
        time_t t = time(NULL); struct tm tm; tm = *localtime(&t);
        snprintf(path, sizeof path, "%s/Theme Park %04d-%02d-%02d %02d%02d%02d.png", dir, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    }
    int ok = tp_host_png_rgba(path, px, w, h); free(px);
    fprintf(stderr, "park picture %dx%d -> %s%s\n", w, h, path, ok ? "" : " (failed)");
    char note[300]; snprintf(note, sizeof note, ok ? "Park picture saved in %s/pictures" : "The park picture could not be saved", data_root);
    set_subtitle(note);
}

/* What the window shows (zoomed or not), 1280x960 or wider; a game window in the wide view: what is shown. */
static void vshot_size(int *ow, int *oh) {
    *oh = 960; *ow = 1280; Rect vb = bounds();
    if ((tp_view_live() || menu_wide) && vb.w * 3 > vb.h * 4 * 1.02) *ow = (int)lround(*oh * vb.w / vb.h);
}
static void vshot(const char *path) {
    static uint32_t frame[640 * 480]; int fw = 320, fh = 200;
    tp_host_frame_size(&fw, &fh); tp_host_frame_rgba_any(frame, 640 * 480);
    int ow, oh; vshot_size(&ow, &oh);
    uint32_t *out;
    if (menu_wide && big) { ow = big_w; oh = big_h; out = malloc(4 * (size_t)ow * oh); memcpy(out, big, 4 * (size_t)ow * oh); }
    else if ((out = malloc(4 * (size_t)ow * oh)) && tp_view_live()) tp_view_compose(out, ow, oh, frame, fw, fh, 0);
    else for (int i = 0; i < ow * oh; i++) out[i] = frame[(i / ow * fh / oh) * fw + (i % ow) * fw / ow];
    tp_host_png_rgba(path, out, ow, oh); free(out);
    fprintf(stderr, "vshot %s %dx%d live=%d\n", path, ow, oh, tp_view_live());
}

/* Mouse events due later (a click's release 150 ms after its press). */
static struct { double at, x, y; uint32_t b; int view; } later[32]; static int n_later;
static void mouse_later(double at, double x, double y, uint32_t b, int view) {
    if (n_later < 32) { later[n_later].at = at; later[n_later].x = x; later[n_later].y = y; later[n_later].b = b; later[n_later].view = view; n_later++; }
}
static void run_later(void) {
    double t = now();
    for (int i = 0; i < n_later; )
        if (t >= later[i].at) { if (later[i].view) tp_view_mouse(later[i].x, later[i].y, later[i].b); else tp_host_mouse(later[i].x, later[i].y, later[i].b); later[i] = later[--n_later]; }
        else i++;
}

/* Bounded self-test (TP_TEST="t:key:1c0d,t:click:x:y,t:shot:/path.png,t:quit", t in seconds).
 * Clicks use the game's 640x400 mouse space. */
static void run_test_script(void) {
    static double start; static int next; static char *steps[256]; static int n_steps = -1;
    const char *spec = getenv("TP_TEST");
    if (!spec || !*spec) return;
    if (n_steps < 0) {
        n_steps = 0; char *copy = strdup(spec);
        for (char *s = strtok(copy, ","); s && n_steps < 256; s = strtok(NULL, ",")) steps[n_steps++] = s;
        start = now();
    }
    while (next < n_steps) {
        char step[512]; snprintf(step, sizeof step, "%s", steps[next]);
        char *op = strchr(step, ':'); if (!op) { next++; continue; }
        *op++ = 0;
        if (now() - start < atof(step)) return;
        next++;
        char *rest = strchr(op, ':'); if (rest) *rest++ = 0; else rest = "";   /* (a path keeps its colons) */
        double x, y;
        if (!strcmp(op, "key")) { uint32_t k = (uint32_t)strtoul(rest, NULL, 16); tp_host_key(k); tp_host_key_raw((uint8_t)(k >> 8), 1); tp_host_key_raw((uint8_t)(k >> 8), 0); }
        else if ((!strcmp(op, "click") || !strcmp(op, "move")) && sscanf(rest, "%lf:%lf", &x, &y) == 2) {
            x /= 640.0; y /= 400.0;
            tp_host_mouse(x, y, 0);
            if (!strcmp(op, "click")) { tp_host_mouse(x, y, 1); mouse_later(now() + 0.15, x, y, 0, 0); }   /* hold the button ~150 ms, like a real click */
        }
        else if (!strcmp(op, "shot")) { char hex[65]; tp_host_png(rest, hex); fprintf(stderr, "shot %s blocks=%llu status=%s\n", hex, (unsigned long long)tp_host_blocks(), tp_host_status()); }
        else if (!strcmp(op, "zoom")) zoom_to(atof(rest), NAN, NAN);
        else if (!strcmp(op, "size") && sscanf(rest, "%lf:%lf", &x, &y) == 2) SDL_SetWindowSize(win, (int)x, (int)y);
        else if (!strcmp(op, "vclick") && sscanf(rest, "%lf:%lf", &x, &y) == 2) {   /* a click through the zoomed view, 640x400 screen space */
            x /= 640.0; y /= 400.0;
            tp_view_mouse(x, y, 0); tp_view_mouse(x, y, 1); mouse_later(now() + 0.15, x, y, 0, 1);
        }
        else if (!strcmp(op, "vshot")) vshot(rest);
        else if (!strcmp(op, "picture")) save_picture();
        else if (!strcmp(op, "quit")) { SDL_Event q = {.type = SDL_QUIT}; SDL_PushEvent(&q); }
    }
}

/* Development: live control for playtests (TP_CONTROL=<file>), as the Mac app's. Lines appended to the file run
 * in order, one batch per tick: key HEX | shift HEX | raw SCAN 1|0 | type TEXT | mouse X Y B | click X Y [B] |
 * vmouse X Y B | vclick X Y [B] | zoom Z | center CX CY | pan DX DY | size W H | cycles N | shot PATH | vshot PATH |
 * sleep S | ack TAG | peek ADDR LEN | dump ADDR LEN PATH | coverage PATH | full | fps | grid | panel K |
 * poke16 ADDR V | poke32 ADDR|money V | unlockall | walk X Y | walkcmd N | walkhold F S T SECONDS |
 * walkteleport X Y YAW | walkstop | walkshot PATH | show | quit. mouse/click are in the game frame's pixels;
 * vmouse/vclick in a vshot's pixels. ack appends TAG to <file>.ack once everything before it has run. */
static char *queue[4096]; static int q_head = 64, q_tail = 64;    /* (room in front for a click's steps) */
static void q_push_front(const char *line) { if (q_head > 0) queue[--q_head] = strdup(line); }
static void run_control(void) {
    static const char *path; static long off; static double until;
    if (!path) { path = getenv("TP_CONTROL"); if (!path || !*path) { path = ""; return; } }
    if (!*path) return;
    if (q_head == q_tail) q_head = q_tail = 64;
    FILE *f = fopen(path, "rb");
    if (f) {
        fseek(f, off, SEEK_SET); char line[2048];
        while (fgets(line, sizeof line, f)) {
            size_t n = strlen(line); if (!n || line[n - 1] != '\n') break;   /* whole lines only */
            off += (long)n; line[--n] = 0; if (n && line[n - 1] == '\r') line[--n] = 0;
            if (n && q_tail < 4096) queue[q_tail++] = strdup(line);
        }
        fclose(f);
    }
    double t = now();
    while (q_head < q_tail && t >= until) {
        char *line = queue[q_head++], buf[2048]; snprintf(buf, sizeof buf, "%s", line);
        char *rest = strchr(buf, ' '); const char *arg = rest ? rest + 1 : "";
        char *p[6] = {0}; int np = 0; { char tmp[2048]; snprintf(tmp, sizeof tmp, "%s", line); for (char *s = strtok(tmp, " "); s && np < 6; s = strtok(NULL, " ")) p[np++] = strdup(s); }
        const char *op = np ? p[0] : "";
        double a1 = np > 1 ? atof(p[1]) : 0, a2 = np > 2 ? atof(p[2]) : 0;
        uint32_t b = np > 3 ? (uint32_t)atoi(p[3]) : 1;
        int fw = 320, fh = 200; tp_host_frame_size(&fw, &fh);
        int ow, oh; vshot_size(&ow, &oh);
        if (!strcmp(op, "key") && np > 1) { uint32_t k = (uint32_t)strtoul(p[1], NULL, 16); tp_host_key(k); tp_host_key_raw((uint8_t)(k >> 8), 1); tp_host_key_raw((uint8_t)(k >> 8), 0); }
        else if (!strcmp(op, "shift") && np > 1) tp_host_shift((uint32_t)strtoul(p[1], NULL, 16));
        else if (!strcmp(op, "raw") && np > 2) tp_host_key_raw((uint8_t)strtoul(p[1], NULL, 16), (int)a2);
        else if (!strcmp(op, "type")) for (const char *c = arg; *c; c++) { uint32_t k = key_of_char((unsigned char)*c); if (k) { tp_host_key(k); tp_host_key_raw((uint8_t)(k >> 8), 1); tp_host_key_raw((uint8_t)(k >> 8), 0); } }
        else if (!strcmp(op, "mouse")) tp_host_mouse(a1 / fw, a2 / fh, np > 3 ? b : 0);
        else if (!strcmp(op, "vmouse")) pointer_at(a1 / ow, a2 / oh, np > 3 ? b : 0);
        else if (!strcmp(op, "click") || !strcmp(op, "vclick")) {   /* press, hold 150 ms, release */
            int v = !strcmp(op, "vclick"); const char *m = v ? "vmouse" : "mouse"; char s[128];
            if (v) pointer_at(a1 / ow, a2 / oh, 0); else tp_host_mouse(a1 / fw, a2 / fh, 0);
            q_push_front("sleep 0.1");
            snprintf(s, sizeof s, "%s %g %g 0", m, a1, a2); q_push_front(s);
            q_push_front("sleep 0.15");
            snprintf(s, sizeof s, "%s %g %g %u", m, a1, a2, b); q_push_front(s);
            until = t + 0.05; free(line); for (int i = 0; i < np; i++) free(p[i]); continue;
        }
        else if (!strcmp(op, "zoom")) zoom_to(a1, NAN, NAN);
        else if (!strcmp(op, "center")) center_on(a1, a2);
        else if (!strcmp(op, "pan")) pan_by(a1, a2);
        else if (!strcmp(op, "size")) SDL_SetWindowSize(win, (int)a1, (int)a2);
        else if (!strcmp(op, "cycles")) tp_host_set_cycles(a1);
        else if (!strcmp(op, "shot")) { char hex[65]; tp_host_png(arg, hex); fprintf(stderr, "shot %s\n", arg); }
        else if (!strcmp(op, "vshot")) vshot(arg);
        else if (!strcmp(op, "sleep")) { until = t + a1; free(line); for (int i = 0; i < np; i++) free(p[i]); break; }
        else if (!strcmp(op, "ack")) { char ap[1100]; snprintf(ap, sizeof ap, "%s.ack", path); FILE *af = fopen(ap, "a"); if (af) { fprintf(af, "%s\n", np > 1 ? p[1] : ""); fclose(af); } }
        else if (!strcmp(op, "peek") && np > 1) {        /* peek ADDR(hex) LEN: guest memory to the log */
            uint8_t *m = tp_host_memory(); uint32_t a = (uint32_t)strtoul(p[1], NULL, 16); int len = np > 2 ? atoi(p[2]) : 4;
            fprintf(stderr, "peek %06x:", a); for (int i = 0; i < len && i < 64; i++) fprintf(stderr, " %02x", m[a + i]); fprintf(stderr, "\n");
        }
        else if (!strcmp(op, "coverage") && np > 1) tp_coverage_dump(p[1]);
        else if (!strcmp(op, "dump") && np > 3) {
            uint8_t *m = tp_host_memory(); uint32_t a = (uint32_t)strtoul(p[1], NULL, 16), len = (uint32_t)strtoul(p[2], NULL, 0);
            FILE *df = len <= 0x400000 ? fopen(p[3], "wb") : NULL; if (df) { fwrite(m + a, 1, len, df); fclose(df); }
        }
        else if (!strcmp(op, "full")) toggle_full();
        else if (!strcmp(op, "fps")) toggle_fps();
        else if (!strcmp(op, "grid")) toggle_grid();
        else if (!strcmp(op, "panel")) tp_view_set_ui_scale(a1);
        else if (!strcmp(op, "poke16") && np > 2) { uint16_t v = (uint16_t)strtoul(p[2], NULL, 0); memcpy(tp_host_memory() + (uint32_t)strtoul(p[1], NULL, 0), &v, 2); }
        else if (!strcmp(op, "unlockall")) {           /* (tests: every ride and shop researched) */
            uint8_t *m = tp_host_memory();
            for (int k = 0; k < 29; k++) m[0x1cced0 + k * 0xc8 + 0xc5] = 1;
            for (int k = 0; k < 17; k++) m[0x1d3c62 + 0x22 + k * 0x6b + 0x44] = 1;
        }
        else if (!strcmp(op, "poke32") && np > 2) {
            uint8_t *m = tp_host_memory(); uint32_t a = (uint32_t)strtoul(p[1], NULL, 0), v = (uint32_t)strtoul(p[2], NULL, 0);
            if (!strcmp(p[1], "money")) { uint32_t pl; memcpy(&pl, m + 0x1d3b00, 4); a = pl + 0x0a; }   /* (the player's money: *COMPUTER block +0x0a) */
            memcpy(m + a, &v, 4);
        }
        else if (!strcmp(op, "walk")) {
            double x = a1, y = a2;
            if (x < 0 && !tp_view_point_to_world(0.5, 0.45, &x, &y)) x = y = 32;
            fprintf(stderr, "control: walk %.2f,%.2f: %d\n", x, y, walk_start_at(x, y));
        }
        else if (!strcmp(op, "walkcmd")) tp_walk_command((int)a1);
        else if (!strcmp(op, "walkhold")) { hold[0] = a1; hold[1] = a2; hold[2] = np > 3 ? atof(p[3]) : 0; hold_until = now() + (np > 4 ? atof(p[4]) : 1); }
        else if (!strcmp(op, "walkteleport")) tp_walk_teleport(a1, a2, np > 3 ? atof(p[3]) * M_PI / 180 : NAN);
        else if (!strcmp(op, "walkstop")) walk_stop();
        else if (!strcmp(op, "walkshot")) {
            uint32_t *img = malloc(4 * 960 * 600); int ok = tp_walk_render(img, 960, 600);
            if (ok) tp_host_png_rgba(arg, img, 960, 600);
            char st[256]; tp_walk_status(st, sizeof st); free(img);
            fprintf(stderr, "walkshot %s drawn=%d | %s\n", arg, ok, st);
        }
        else if (!strcmp(op, "show")) { SDL_ShowWindow(win); SDL_RaiseWindow(win); }   /* a run to watch */
        else if (!strcmp(op, "quit")) { SDL_Event q = {.type = SDL_QUIT}; SDL_PushEvent(&q); }
        else fprintf(stderr, "control: unknown %s\n", line);
        free(line); for (int i = 0; i < np; i++) free(p[i]);
    }
}

/* A game controller: left stick moves the pointer, A and B click (left, right), the right stick moves the park
 * view, the shoulder buttons zoom, Start is Escape. */
static SDL_GameController *pad_dev;
static double axis(SDL_GameControllerAxis a) { return SDL_GameControllerGetAxis(pad_dev, a) / 32767.0; }
static int button(SDL_GameControllerButton b) { return SDL_GameControllerGetButton(pad_dev, b); }
static void poll_controller(void) {
    if (!pad_dev) return;
    double lx = axis(SDL_CONTROLLER_AXIS_LEFTX), ly = -axis(SDL_CONTROLLER_AXIS_LEFTY), rx = axis(SDL_CONTROLLER_AXIS_RIGHTX), ry = -axis(SDL_CONTROLLER_AXIS_RIGHTY);
    if (tp_walk_active()) {                             /* Visitor View: left stick walks, right stick looks, A follow, X hop, Y ride, Start leaves */
        static int a0, x0, y0, m0;
        pad[0] = fabs(ly) > 0.15 ? ly : 0; pad[1] = fabs(lx) > 0.15 ? lx : 0; pad[2] = fabs(rx) > 0.15 ? rx : 0; pad[3] = fabs(ry) > 0.15 ? -ry : 0;
        int a = button(SDL_CONTROLLER_BUTTON_A), x = button(SDL_CONTROLLER_BUTTON_X), y = button(SDL_CONTROLLER_BUTTON_Y), m = button(SDL_CONTROLLER_BUTTON_START);
        if (a && !a0) tp_walk_command(TP_WALK_FOLLOW);
        if (x && !x0) tp_walk_command(TP_WALK_HOP);
        if (y && !y0) tp_walk_command(TP_WALK_RIDE);
        if (m && !m0) walk_stop();
        a0 = a; x0 = x; y0 = y; m0 = m;
        return;
    }
    pad[0] = pad[1] = pad[2] = pad[3] = 0;
    static double px = 0.5, py = 0.4; static int a, b, ls, rs, menu;
    int moved = 0;
    if (fabs(lx) > 0.15 || fabs(ly) > 0.15) { px = fmin(fmax(px + lx * 0.010, 0), 0.999); py = fmin(fmax(py - ly * 0.013, 0), 0.999); moved = 1; }
    int na = button(SDL_CONTROLLER_BUTTON_A), nb = button(SDL_CONTROLLER_BUTTON_B);
    if (moved || na != a || nb != b) pointer_at(px, py, (na ? 1u : 0u) | (nb ? 2u : 0u));
    a = na; b = nb;
    if (fabs(rx) > 0.15 || fabs(ry) > 0.15) pan_by(rx * 14, -ry * 14);
    int nls = button(SDL_CONTROLLER_BUTTON_LEFTSHOULDER), nrs = button(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    if (nls && !ls) zoom_out();
    if (nrs && !rs) zoom_in();
    ls = nls; rs = nrs;
    int nm = button(SDL_CONTROLLER_BUTTON_START);
    if (nm && !menu) { tp_host_key(0x011b); tp_host_key_raw(1, 1); tp_host_key_raw(1, 0); }
    menu = nm;
}

/* Hidden or minimised, the park waits (and is silent) until it is back. */
static void hold_game(int hold_it) {
    tp_host_pause(hold_it);
    if (hold_it) tp_audio_set_volumes(0, 0);
    else tp_audio_set_volumes((float)settings_getf("SoundVolume"), (float)settings_getf("MusicVolume"));
}

/* ---- keys: a printable key waits for its character (SDL's text event), others go at once ---- */
static uint8_t pend_scan; static uint32_t pend_guess; static int pend; static double pend_at;
static void flush_pending(void) { if (pend) { tp_host_key((uint32_t)pend_scan << 8 | pend_guess); pend = 0; } }
static void shift_state(void) {
    SDL_Keymod m = SDL_GetModState(); uint32_t f = 0;
    if (m & KMOD_SHIFT) f |= 3;
    if (m & KMOD_CTRL) f |= 4;
    if (m & KMOD_ALT) f |= 8;
    if (m & KMOD_CAPS) f |= 0x40;
    wk_run = (m & KMOD_SHIFT) != 0;
    if (!tp_walk_active()) tp_host_shift(f);
}
/* The app's own keys (Ctrl, Alt+Enter, F11); 1 when used. */
static int app_key(const SDL_KeyboardEvent *k) {
    SDL_Keycode c = k->keysym.sym; SDL_Keymod m = k->keysym.mod;
    if (c == SDLK_F11 || ((m & KMOD_ALT) && (c == SDLK_RETURN || c == SDLK_KP_ENTER))) { if (!k->repeat) toggle_full(); return 1; }
    if (!(m & KMOD_CTRL)) return 0;
    if ((m & KMOD_ALT) && c == SDLK_f) { toggle_fps(); return 1; }
    if ((m & KMOD_ALT) && c == SDLK_g) { toggle_grid(); return 1; }
    if ((m & KMOD_SHIFT) && c == SDLK_v) { visitor_view(); return 1; }
    switch (c) {
    case SDLK_EQUALS: case SDLK_PLUS: case SDLK_KP_PLUS: zoom_in(); return 1;
    case SDLK_MINUS: case SDLK_KP_MINUS: zoom_out(); return 1;
    case SDLK_0: case SDLK_KP_0: zoom_to(1, NAN, NAN); return 1;
    case SDLK_RIGHTBRACKET: panel_step(1); return 1;
    case SDLK_LEFTBRACKET: panel_step(-1); return 1;
    case SDLK_e: if (!k->repeat) save_picture(); return 1;
    case SDLK_q: { SDL_Event q = {.type = SDL_QUIT}; SDL_PushEvent(&q); return 1; }
    default: return 0;
    }
}
static void key_down(const SDL_KeyboardEvent *k) {
    SDL_Scancode s = k->keysym.scancode; SDL_Keycode c = k->keysym.sym; SDL_Keymod m = k->keysym.mod;
    shift_state();
    if (app_key(k)) return;
    if (s == SDL_SCANCODE_TAB) {                          /* Tab, held: fast forward (the game does not use Tab) */
        if (!k->repeat && !getenv("TP_CYCLES")) { tp_host_set_cycles(settings_getf("Cycles") * 4); set_subtitle("Fast forward"); }
        return;
    }
    if (walk_drop && s == SDL_SCANCODE_ESCAPE) { end_drop(); return; }   /* no Visitor View after all */
    if (tp_walk_active()) { walk_key(s, 1); return; }
    if (view_on && (s == SDL_SCANCODE_LEFT || s == SDL_SCANCODE_RIGHT || s == SDL_SCANCODE_UP || s == SDL_SCANCODE_DOWN)) {   /* the park view: the arrows move it */
        double step = 40;
        pan_by(s == SDL_SCANCODE_LEFT ? -step : s == SDL_SCANCODE_RIGHT ? step : 0, s == SDL_SCANCODE_UP ? -step : s == SDL_SCANCODE_DOWN ? step : 0);
        return;
    }
    uint8_t scan = pc_scan(s);
    if (!scan) return;
    flush_pending();
    if (!k->repeat) tp_host_key_raw(scan, 1);
    uint32_t ascii = 0;
    if (c == SDLK_RETURN || c == SDLK_KP_ENTER) ascii = 13;
    else if (c == SDLK_ESCAPE) ascii = 27;
    else if (c == SDLK_BACKSPACE) ascii = 8;
    else if ((m & KMOD_CTRL) && c >= SDLK_a && c <= SDLK_z) ascii = (uint32_t)(c - SDLK_a + 1);
    else if (c >= 32 && c < 127 && !(m & (KMOD_CTRL | KMOD_ALT))) {   /* its character comes as text (layout and Shift applied) */
        pend = 1; pend_scan = scan; pend_guess = (uint32_t)c; pend_at = now(); return;
    }
    else if (s >= SDL_SCANCODE_KP_1 && s <= SDL_SCANCODE_KP_PERIOD && (m & KMOD_NUM)) {
        static const char kp[] = "1234567890."; ascii = (uint32_t)kp[s - SDL_SCANCODE_KP_1];
    }
    else if (s == SDL_SCANCODE_KP_PLUS) ascii = '+';
    else if (s == SDL_SCANCODE_KP_MINUS) ascii = '-';
    else if (s == SDL_SCANCODE_KP_MULTIPLY) ascii = '*';
    else if (s == SDL_SCANCODE_KP_DIVIDE) ascii = '/';
    if (ascii) tp_host_key((uint32_t)scan << 8 | ascii);
    else tp_host_key(function_key(scan, m));
}
static void text_input(const char *text) {
    if (!pend) return;                                   /* (keypad digits and composed text: not from a pending key) */
    unsigned char ch = (unsigned char)text[0];
    if (ch && ch < 128 && !text[1]) { tp_host_key((uint32_t)pend_scan << 8 | ch); pend = 0; }
    else flush_pending();
}
static void key_up(const SDL_KeyboardEvent *k) {
    SDL_Scancode s = k->keysym.scancode;
    shift_state();
    if (s == SDL_SCANCODE_TAB) { if (!getenv("TP_CYCLES")) tp_host_set_cycles(settings_getf("Cycles")); set_subtitle(""); return; }
    if (tp_walk_active()) { walk_key(s, 0); return; }
    uint8_t scan = pc_scan(s);
    if (scan) tp_host_key_raw(scan, 0);
}

static void mouse_down(const SDL_MouseButtonEvent *e) {
    if (e->button == SDL_BUTTON_LEFT) {
        if (walk_drop) {                                    /* the Visitor View: drop in here */
            Rect r = live ? view_rect() : screen_rect();
            double fx = (e->x - r.x) / r.w, fy = (e->y - r.y) / r.h, wx, wy;
            if (fx >= 0 && fx < 1 && fy >= 0 && fy < 1 && tp_view_point_to_world(fx, fy, &wx, &wy) && walk_start_at(wx, wy)) return;
            return;
        }
        if (tp_walk_active()) return;
        if (live && (SDL_GetModState() & KMOD_ALT)) { panning = 1; pan_fx = e->x; pan_fy = e->y; return; }   /* Alt-drag pans too */
        buttons |= 1;
    } else if (e->button == SDL_BUTTON_RIGHT) {
        if (tp_walk_active() || walk_drop) return;
        buttons |= 2;
    } else if (e->button == SDL_BUTTON_MIDDLE) {
        if (live) { panning = 1; pan_fx = e->x; pan_fy = e->y; return; }   /* the park view: the middle button drags it (the game does not use it) */
        buttons |= 4;
    } else return;
    send_mouse(e->x, e->y);
}
static void mouse_up(const SDL_MouseButtonEvent *e) {
    if (tp_walk_active() || walk_drop) return;
    if (panning && (e->button == SDL_BUTTON_LEFT || e->button == SDL_BUTTON_MIDDLE)) { panning = 0; return; }
    buttons &= ~(e->button == SDL_BUTTON_LEFT ? 1u : e->button == SDL_BUTTON_RIGHT ? 2u : e->button == SDL_BUTTON_MIDDLE ? 4u : 0u);
    send_mouse(e->x, e->y);
}
static void mouse_motion(const SDL_MouseMotionEvent *e) {
    if (tp_walk_active()) { if (e->state & SDL_BUTTON_LMASK) { look_dx -= e->xrel * 0.005; look_dy -= e->yrel * 0.005; } return; }   /* grab and turn the view */
    if (panning) { pan_by(-(e->x - pan_fx), -(e->y - pan_fy)); pan_fx = e->x; pan_fy = e->y; return; }   /* the park follows the pointer */
    send_mouse(e->x, e->y);
}
static void mouse_wheel(const SDL_MouseWheelEvent *e) {
    double k = e->direction == SDL_MOUSEWHEEL_FLIPPED ? -30 : 30;
    double dx = e->preciseX * k, dy = e->preciseY * k;
    if (tp_walk_active()) { look_dx -= dx * 0.004; look_dy += dy * 0.004; return; }   /* the wheel looks around too */
    if (SDL_GetModState() & (KMOD_CTRL | KMOD_ALT)) zoom_to(zoom * pow(1.006, dy), mouse_x, mouse_y);
    else pan_by(dx, -dy);
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "0");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS)) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    data_paths();
    if (getenv("TP_IMAGE") && *getenv("TP_IMAGE")) snprintf(image_path, sizeof image_path, "%s", getenv("TP_IMAGE"));
    char probe[1200]; snprintf(probe, sizeof probe, "%s/DATA", game_dir);
    tp_mkdir(data_root);
    char ini[1100]; snprintf(ini, sizeof ini, "%s/settings.ini", data_root);
    settings_load(ini);
    int self_test = (getenv("TP_TEST") && *getenv("TP_TEST")) || (getenv("TP_CONTROL") && *getenv("TP_CONTROL"));
    if (!exists(probe) || !exists(image_path)) {
        char m[1400]; snprintf(m, sizeof m, "The game files were not found in %s (or MAIN.TPIMG beside the program). Run the build script with your Theme Park package to install them.", data_root);
        fail(m); SDL_Quit(); return 1;
    }
    setenv("TP_IMAGE", image_path, 1);
    if (!getenv("TP_CYCLES")) tp_host_set_cycles(settings_getf("Cycles"));
    snprintf(intro_path, sizeof intro_path, "%sINTRO.TPIMG", base_dir);
    if (!settings_get("PlayIntro") || getenv("TP_SKIP_INTRO") || !exists(intro_path)) intro_path[0] = 0;
    tp_mkdir(saves_dir);
    if (!getenv("TP_LOG_STDERR")) {
        char logp[1100]; snprintf(logp, sizeof logp, "%s/theme-park.log", data_root);
        if (freopen(logp, "a", stderr)) setvbuf(stderr, NULL, _IOLBF, 1024);   /* (a file: whole lines at once, so the log is current) */
    }
    { time_t t = time(NULL); fprintf(stderr, "--- Theme Park started %s", ctime(&t)); }

    win = SDL_CreateWindow("Theme Park", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 960, 720,
                           SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | (self_test ? SDL_WINDOW_HIDDEN : 0));   /* self-tests stay in the background */
    if (!win) { fail(SDL_GetError()); return 1; }
    SDL_SetWindowMinimumSize(win, 320, 240);
    { char icon[1100]; snprintf(icon, sizeof icon, "%sicon.bmp", base_dir); SDL_Surface *s = SDL_LoadBMP(icon); if (s) { SDL_SetWindowIcon(win, s); SDL_FreeSurface(s); } }
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, 0);
    if (!ren) { fail(SDL_GetError()); return 1; }
    SDL_RendererInfo ri; SDL_GetRendererInfo(ren, &ri);
    int vsync = (ri.flags & SDL_RENDERER_PRESENTVSYNC) != 0;
    fprintf(stderr, "renderer %s, vsync %d\n", ri.name, vsync);
    SDL_StartTextInput();
    if (settings_get("StartFullScreen")) toggle_full();
    tp_view_debug_fps = (int)settings_get("DebugFPS"); tp_view_debug_grid = (int)settings_get("DebugGrid");
    tp_view_set_ui_scale(settings_getf("PanelScale"));
    if (settings_get("Sound") && tp_audio_start() == 0) tp_audio_set_volumes((float)settings_getf("SoundVolume"), (float)settings_getf("MusicVolume"));
    settings_to_env();
    SDL_Thread *game = SDL_CreateThreadWithStackSize(game_thread, "Theme Park game", 8 << 20, NULL);
    if (!game) { fail(SDL_GetError()); return 1; }
    SDL_DetachThread(game);

    int running = 1, minimized = 0;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_QUIT: running = 0; break;
            case SDL_KEYDOWN: key_down(&e.key); break;
            case SDL_KEYUP: key_up(&e.key); break;
            case SDL_TEXTINPUT: text_input(e.text.text); break;
            case SDL_MOUSEBUTTONDOWN: mouse_down(&e.button); break;
            case SDL_MOUSEBUTTONUP: mouse_up(&e.button); break;
            case SDL_MOUSEMOTION: mouse_motion(&e.motion); break;
            case SDL_MOUSEWHEEL: mouse_wheel(&e.wheel); break;
            case SDL_CONTROLLERDEVICEADDED: if (!pad_dev) pad_dev = SDL_GameControllerOpen(e.cdevice.which); break;
            case SDL_CONTROLLERDEVICEREMOVED:
                if (pad_dev && e.cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad_dev))) { SDL_GameControllerClose(pad_dev); pad_dev = NULL; }
                break;
            case SDL_WINDOWEVENT:
                switch (e.window.event) {
                case SDL_WINDOWEVENT_MINIMIZED: case SDL_WINDOWEVENT_HIDDEN: if (!minimized && !self_test) { minimized = 1; hold_game(1); } break;
                case SDL_WINDOWEVENT_RESTORED: case SDL_WINDOWEVENT_SHOWN: if (minimized) { minimized = 0; hold_game(0); } break;
                case SDL_WINDOWEVENT_LEAVE: set_cursor_hidden(0); mouse_in = 0; break;
                case SDL_WINDOWEVENT_FOCUS_GAINED: focused = 1; break;
                case SDL_WINDOWEVENT_FOCUS_LOST: focused = 0; set_cursor_hidden(0); break;
                }
                break;
            }
        }
        if (pend && now() - pend_at > 0.05) flush_pending();   /* (no character came for it) */
        run_later();
        run_control();
        run_test_script();
        poll_controller();
        present();
        draw();
        SDL_RenderPresent(ren);
        if (vsync && !minimized) {                       /* the display's refresh (for steady pacing) */
            SDL_DisplayMode dm; double period = 1.0 / 60;
            if (!SDL_GetCurrentDisplayMode(SDL_GetWindowDisplayIndex(win), &dm) && dm.refresh_rate > 0) period = 1.0 / dm.refresh_rate;
            tp_view_vsync_now(period, 0);
        } else SDL_Delay(minimized ? 20 : 8);
        if (game_done) {
            fprintf(stderr, "game ended: %s\n", tp_host_status());
            if (game_result < 0 && strcmp(tp_host_status(), "quit")) {
                char m[600]; snprintf(m, sizeof m, "%s\n\nDetails are in %s/theme-park.log.", tp_host_status(), data_root); fail(m);
            }
            running = 0;
        }
    }
    tp_host_quit();
    for (int i = 0; i < 100 && !game_done; i++) SDL_Delay(10);
    tp_audio_stop();
    fflush(stderr);
    SDL_Quit();
    return game_result < 0 && strcmp(tp_host_status(), "quit") ? 1 : 0;
}
