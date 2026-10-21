/* Theme Park for macOS: a Cocoa window around the recompiled ARM64 game code
 * and the native DOS host (runtime/host.c). The game runs on its own thread;
 * the window shows the park view (runtime/view.c: wide and zoomable by default) or the game's own frame
 * (320x200 or its 640x480 SVGA mode) at 4:3, and forwards keyboard/mouse.
 * Data: ~/Games/ThemeParkNative/{game,saves} (game files copied from the
 * user's own GOG package by tools/build_app.sh). */
#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#import <IOSurface/IOSurface.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#import <ImageIO/ImageIO.h>
#import <GameController/GameController.h>
#import <AVFoundation/AVFoundation.h>
#include <pthread.h>
#include <mach-o/dyld.h>
#include <mach-o/getsect.h>
#include <sys/mman.h>
#include "host.h"
int tp_audio_start(void);
void tp_audio_stop(void);
void tp_audio_set_volumes(float sfx, float music);
int tp_audio_cd_play(const char *path, int loop); void tp_audio_cd_stop(void);
BOOL tp_launcher_run(void);
struct Ctx;
void tp_select_code(int (*dispatch)(struct Ctx *), void (*init)(void));
int tp_dispatch(struct Ctx *); void tp_dispatch_init(void);
void tp_qol_init(void);
extern long (*tp_extras_get)(const char *key); extern void (*tp_extras_set)(const char *key, long value);   /* (qol.c's EXTRAS menu) */
extern double (*tp_extras_getf)(const char *key); extern void (*tp_extras_setf)(const char *key, double value);
/* (thread-safe; read live. Test runs (DS_TEST) share the app's settings domain: their choices stay in memory) */
static NSMutableDictionary *extras_test;
static long extras_get(const char *k) { NSNumber *t; @synchronized ([NSApp class]) { t = extras_test[@(k)]; } return t ? t.longValue : [NSUserDefaults.standardUserDefaults integerForKey:@(k)]; }
static void extras_set(const char *k, long v) {
    if (getenv("DS_TEST")) { @synchronized ([NSApp class]) { if (!extras_test) extras_test = [NSMutableDictionary dictionary]; extras_test[@(k)] = @(v); } return; }
    [NSUserDefaults.standardUserDefaults setInteger:v forKey:@(k)];
}
static double extras_getf(const char *k) { NSNumber *t; @synchronized ([NSApp class]) { t = extras_test[@(k)]; } return t ? t.doubleValue : [NSUserDefaults.standardUserDefaults doubleForKey:@(k)]; }
static void extras_setf(const char *k, double v) {
    if (getenv("DS_TEST")) { @synchronized ([NSApp class]) { if (!extras_test) extras_test = [NSMutableDictionary dictionary]; extras_test[@(k)] = @(v); } }
    else [NSUserDefaults.standardUserDefaults setDouble:v forKey:@(k)];
    if (!strcmp(k, "MusicVolume") || !strcmp(k, "SoundVolume")) {   /* (heard at once) */
        float sfx = (float)extras_getf("SoundVolume"), mus = (float)extras_getf("MusicVolume");
        dispatch_async(dispatch_get_main_queue(), ^{ tp_audio_set_volumes(sfx, mus); });
    }
}
uint8_t *tp_host_memory(void);
extern int tp_view_hw_cursor, tp_view_debug_fps, tp_view_debug_grid, tp_view_out_bgra;
void tp_view_compose_stride(size_t px);
void tp_view_debug_hud(uint32_t *out, int ow, int oh);
void tp_view_count_game_frames(int on);
void tp_view_set_ui_scale(double k); double tp_view_ui_scale(void); double tp_view_ui_share(void); extern volatile int tp_game_video;
int tp_view_frame_due(void);
void tp_view_vsync_now(double period, double ago);
int tp_view_cursor_rgba(uint32_t *out, int *w, int *h, int *ox, int *oy);
int intro_dispatch(struct Ctx *); void intro_dispatch_init(void);

static NSString *dataRoot(void) {
    NSString *env = NSProcessInfo.processInfo.environment[@"TP_DATA"];
    return env.length ? env : [NSHomeDirectory() stringByAppendingPathComponent:@"Games/ThemeParkNative"];
}

/* ---- keyboard: macOS virtual key -> PC scan code ---- */
static const uint8_t scan_of[128] = {
    [0x00]=0x1e,[0x01]=0x1f,[0x02]=0x20,[0x03]=0x21,[0x04]=0x23,[0x05]=0x22,[0x06]=0x2c,[0x07]=0x2d,
    [0x08]=0x2e,[0x09]=0x2f,[0x0b]=0x30,[0x0c]=0x10,[0x0d]=0x11,[0x0e]=0x12,[0x0f]=0x13,[0x10]=0x15,
    [0x11]=0x14,[0x12]=0x02,[0x13]=0x03,[0x14]=0x04,[0x15]=0x05,[0x16]=0x07,[0x17]=0x06,[0x18]=0x0d,
    [0x19]=0x0a,[0x1a]=0x08,[0x1b]=0x0c,[0x1c]=0x09,[0x1d]=0x0b,[0x1e]=0x1b,[0x1f]=0x18,[0x20]=0x16,
    [0x21]=0x1a,[0x22]=0x17,[0x23]=0x19,[0x24]=0x1c,[0x25]=0x26,[0x26]=0x24,[0x27]=0x28,[0x28]=0x25,
    [0x29]=0x27,[0x2a]=0x2b,[0x2b]=0x33,[0x2c]=0x35,[0x2d]=0x31,[0x2e]=0x32,[0x2f]=0x34,[0x30]=0x0f,
    [0x31]=0x39,[0x32]=0x29,[0x33]=0x0e,[0x35]=0x01,
    [0x7a]=0x3b,[0x78]=0x3c,[0x63]=0x3d,[0x76]=0x3e,[0x60]=0x3f,[0x61]=0x40,[0x62]=0x41,[0x64]=0x42,
    [0x65]=0x43,[0x6d]=0x44,[0x67]=0x85,[0x6f]=0x86,
    [0x7b]=0x4b,[0x7c]=0x4d,[0x7d]=0x50,[0x7e]=0x48,[0x73]=0x47,[0x77]=0x4f,[0x74]=0x49,[0x79]=0x51,
    [0x75]=0x53,[0x72]=0x52,
    [0x52]=0x52,[0x53]=0x4f,[0x54]=0x50,[0x55]=0x51,[0x56]=0x4b,[0x57]=0x4c,[0x58]=0x4d,[0x59]=0x47,
    [0x5b]=0x48,[0x5c]=0x49,[0x4c]=0x1c,[0x45]=0x4e,[0x4e]=0x4a,[0x43]=0x37,[0x4b]=0x35,[0x41]=0x53,
};
/* BIOS codes for function keys with Shift/Ctrl/Alt (F1..F10). */
static uint32_t function_key(uint8_t scan, NSEventModifierFlags m) {
    if (scan >= 0x3b && scan <= 0x44) {
        if (m & NSEventModifierFlagOption) return (uint32_t)(scan - 0x3b + 0x68) << 8;
        if (m & NSEventModifierFlagControl) return (uint32_t)(scan - 0x3b + 0x5e) << 8;
        if (m & NSEventModifierFlagShift) return (uint32_t)(scan - 0x3b + 0x54) << 8;
    }
    return (uint32_t)scan << 8;
}

typedef struct TpVideo TpVideo; TpVideo *tp_video_open(const char *path); const uint8_t *tp_video_next(TpVideo *v);
int tp_video_fps(TpVideo *v); int tp_video_width(TpVideo *v); int tp_video_height(TpVideo *v); void tp_video_close(TpVideo *v);
int tp_audio_video_play(const char *path); void tp_audio_video_stop(void);
@interface GameView : NSView
- (void)zoomTo:(double)z at:(NSPoint)p;
- (void)panBy:(double)dx :(double)dy;
- (void)pointerAt:(double)fx :(double)fy buttons:(uint32_t)b;
- (BOOL)menuWide;
- (uint32_t *)shownPicture:(int *)w :(int *)h;   /* (tests: the wide picture with a game window, RGBA, malloc'd; else NULL) */
- (void)zoomIn:(id)sender;
- (void)zoomOut:(id)sender;
- (void)centerOn:(double)cx :(double)cy;
- (BOOL)walkStartAt:(double)x :(double)y;
- (void)walkStop;
- (void)walkHold:(double)fwd :(double)strafe :(double)turn :(double)seconds;
- (void)walkPad:(double)fwd :(double)strafe :(double)lookX :(double)lookY;
- (void)setCornerGuard:(BOOL)on;
- (void)releaseInput;
- (BOOL)playing3DO;
@end

static GameView *game_view;                         /* (the 3DO ride videos: ride_video_3do) */
static void refresh_hold(void);                     /* (AppDelegate refreshHold: paused while away or under a 3DO video) */
@implementation GameView {
    CALayer *_screen;
    uint32_t _pixels[640 * 480];
    double _lastLW;                      /* the layout width last given to view.c */
    int _camMoves;                       /* tp_view_camera_moves seen */
    BOOL _panning; NSPoint _panFrom;     /* dragging the park view */
    CALayer *_scan; int _scanRows;       /* CRT scanlines (Display > Pixels > CRT) */
    int _sw, _sh, _wh;                   /* the game's screen now: size, park rows (tp_view_geometry) */
    NSTrackingArea *_tracking;
    BOOL _cursorHidden;
    NSCursor *_gameCursor; int _curSerial; double _curKx, _curKy;   /* the game's cursor as a Mac cursor (park view) */
    uint32_t _buttons;
    /* The park view (runtime/view.c): on when zoomed, or when the window is wider
     * than 4:3 (widescreen: the park fills it, the interface stays in a centred
     * 4:3 column); off, the game draws its own view. */
    BOOL _viewOn, _live, _viewMoved, _menuWide;
    uint32_t _curHash;                             /* the game pointer's pixels, as last made (updateGameCursor) */   /* _menuWide: a full-screen game window in the wide view (see present) */
    IOSurfaceRef _surf[2]; int _surfIdx;  /* the park view's pictures, shown in turn */    /* _viewMoved: pan or zoom since the last picture (compose now) */
    double _zoom, _cx, _cy, _offAt;      /* zoom, canvas point at the park area's centre, when the view last ended */
    uint32_t *_big; int _bigW, _bigH;
    NSPoint _mouse; BOOL _mouseIn;
    /* Visitor View (runtime/walk.c): ⇧⌘V, then a click in the park drops you there */
    BOOL _walkDrop;                      /* waiting for the click */
    BOOL _wk[8], _wkRun;                 /* keys held: forward, back, left, right, turn left, turn right */
    double _walkLast, _lookDX, _lookDY, _hold[3], _holdUntil, _pad[4];
    uint32_t *_walkPx; int _walkW, _walkH;
    CATextLayer *_hud; char _hudText[256];
    AVQueuePlayer *_video; AVPlayerLayer *_videoLayer; AVPlayerLooper *_looper; int _videoType;
    uint32_t *_clean; int _cleanW, _cleanH; double _cleanAt, _coverAt;   /* the park picture without a game window over it (present: menuWide) */
    TpVideo *_v3; CALayer *_v3Layer, *_v3Pic; double _v3t0; int _v3shown; dispatch_semaphore_t _v3done;   /* a 3DO ride video (play3DO) */
    BOOL _cornerGuard, _held; NSPoint _virt; NSTimeInterval _letGoAt, _warpAt; CGFloat _warpDX, _warpDY;   /* full screen: the pointer kept out of the corners (pointOf:) */
    double _lastFx, _lastFy; uint8_t _keyDown[128]; BOOL _fastForward;   /* what the game was last told: releaseInput */
}
- (instancetype)initWithFrame:(NSRect)frame {
    if ((self = [super initWithFrame:frame])) {
        game_view = self;
        self.wantsLayer = YES;
        self.layer.backgroundColor = NSColor.blackColor.CGColor;
        _screen = [CALayer layer];
        _screen.magnificationFilter = [NSUserDefaults.standardUserDefaults boolForKey:@"SmoothPixels"] ? kCAFilterLinear : kCAFilterNearest;
        _screen.minificationFilter = kCAFilterLinear;
        _screen.actions = @{@"contents": [NSNull null], @"bounds": [NSNull null], @"position": [NSNull null]};
        [self.layer addSublayer:_screen];
        _scan = [CALayer layer];
        _scan.actions = @{@"contents": [NSNull null], @"bounds": [NSNull null], @"position": [NSNull null], @"hidden": [NSNull null]};
        _scan.magnificationFilter = kCAFilterLinear; _scan.hidden = YES;
        [self.layer addSublayer:_scan];
        _zoom = 1;
    }
    return self;
}
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent *)e { return YES; }
/* The game's own 4:3 picture (or the whole view, stretched). */
- (NSRect)screenRect {
    NSRect b = self.bounds;
    CGFloat w = b.size.width, h = b.size.height;
    if ([NSUserDefaults.standardUserDefaults boolForKey:@"Stretch"]) return b;
    if (w * 3 > h * 4) w = h * 4 / 3; else h = w * 3 / 4;
    return NSMakeRect(floor((b.size.width - w) / 2), floor((b.size.height - h) / 2), floor(w), floor(h));
}
- (BOOL)wide {
    NSRect b = self.bounds;
    return [NSUserDefaults.standardUserDefaults boolForKey:@"Widescreen"] && b.size.height > 0 && b.size.width * 3 > b.size.height * 4 * 1.02;
}
/* What the park view occupies, and its width in the game screen's pixels. */
- (NSRect)viewRect { return [self wide] ? self.bounds : [self screenRect]; }
/* The game's own screen inside the wide view: the column its interface takes there (the panel's). */
/* A full-screen game window in the wide view (present: menuWide): the window at its full size, its panel at the
 * panel's size (at most 1, over nothing of the window), anchored at the bottom middle as in the park (view.c
 * band_map), so opening one from the panel leaves the panel as it was. The game's own videos: no panel. */
- (double)menuPanelScale { double k = tp_view_ui_scale(); return tp_game_video || k > 1 ? 1 : k; }
- (int)panelRows:(int)h { return h == 480 ? 44 : h * 22 / 200; }   /* (the panel's rows: view.c band_map's) */
/* A point in the column (0..1) to the game's screen: in the smaller panel, its place on the panel; elsewhere in the
 * panel's strip (the park shows there), just above it. */
- (void)menuMapX:(double *)fx y:(double *)fy {
    double k = [self menuPanelScale]; if (k == 1 || _sh <= 0) return;
    double band = (double)[self panelRows:_sh] / _sh, top = 1 - band, py0 = 1 - band * k, px0 = (1 - k) / 2;
    if (*fy < top) return;
    if (*fy >= py0 && *fx >= px0 && *fx < px0 + k) { *fx = (*fx - px0) / k; *fy = top + (*fy - py0) / k; }
    else *fy = top - 0.5 / _sh;
    *fx = fmin(fmax(*fx, 0), 0.9999); *fy = fmin(fmax(*fy, 0), 0.9999);
}
- (NSRect)columnRect {
    NSRect v = [self viewRect]; double lw = _lastLW > 0 ? _lastLW : [self layoutW];
    CGFloat w = v.size.width * _sw / lw;
    return NSMakeRect(v.origin.x + (v.size.width - w) / 2, v.origin.y, w, v.size.height);
}
- (NSRect)pointerRect { return _live ? [self viewRect] : _menuWide ? [self columnRect] : [self screenRect]; }
- (double)layoutW {
    NSRect r = [self viewRect];
    return [self wide] ? _sw * (r.size.width / r.size.height) * 0.75 : _sw;
}
- (void)layout {
    [super layout];
    [CATransaction begin]; [CATransaction setDisableActions:YES];
    _screen.frame = _live || _menuWide ? [self viewRect] : [self screenRect];
    _scan.frame = _screen.frame;
    [CATransaction commit];
}
- (void)present {
    if (tp_walk_active()) { [self presentWalk]; return; }
    if (_hud && !_hud.hidden) { [CATransaction begin]; [CATransaction setDisableActions:YES]; _hud.hidden = YES; [CATransaction commit]; [self stopRideVideo]; }
    int w, h;
    if (!tp_host_frame_size(&w, &h) || !tp_host_frame_rgba_any(_pixels, 640 * 480)) return;
    int fw = w, fh = h; uint32_t *px = _pixels;
    [self updateGeometry];
    if ((_zoom != 1 || [self wide]) != _viewOn || (_viewOn && fabs([self layoutW] - _lastLW) > 0.5)) [self applyView];
    if (_viewOn && tp_view_camera_moves() != _camMoves) {   /* the game moved its camera itself: look there */
        if (getenv("TP_VIEW_DEBUG")) fprintf(stderr, "app: recentre on the game's camera (moves %d -> %d)\n", _camMoves, tp_view_camera_moves());
        _camMoves = tp_view_camera_moves();
        tp_view_camera_center(&_cx, &_cy); [self clampCenter];
        tp_view_set(1, _cx, _cy, _zoom, _lastLW = [self layoutW]);
    }
    if (_viewOn) [self edgePan];
    BOOL live = _viewOn && tp_view_live() && fw == _sw;
    /* A full-screen game window coming up: its first frames are still the park view's, the window and its panel at
     * their own size (the panel's size changes when a window covers it), before the window takes the screen
     * (menuWide). The last picture stays meanwhile (at most 0.3 s). */
    if (live && _live && [self wide] && tp_view_ui_share() > 0.9) {
        double t = CACurrentMediaTime(); if (!_coverAt) _coverAt = t;
        if (t - _coverAt < 0.3) return;
    } else _coverAt = 0;
    if (live && _live && _surf[_surfIdx] && !_viewMoved && !tp_view_frame_due()) return;   /* steady pacing: the last picture stays up */
    _viewMoved = NO;
    if (live) {                                      /* the park view, the game's interface and its panel */
        NSRect vr = [self viewRect];
        CGFloat scale = self.window.backingScaleFactor ?: 2;
        int k = (int)lround([self screenRect].size.width * scale / fw); k = k < 1 ? 1 : k > 6 ? 6 : k;
        if (fw == 320 && k < 2) k = 2;
        /* square output pixels over the view's shape (the layout maps onto it, 320x200's tall pixels included) */
        int oh = fh * k, ow = (int)lround(oh * vr.size.width / vr.size.height);
        if (ow < 16) ow = 16;
        /* Into one of two IOSurfaces (BGRA, shared with the GPU: no copy when the layer shows it), in turn. */
        _surfIdx ^= 1;
        IOSurfaceRef sf = _surf[_surfIdx];
        if (!sf || (int)IOSurfaceGetWidth(sf) != ow || (int)IOSurfaceGetHeight(sf) != oh) {
            if (sf) CFRelease(sf);
            NSDictionary *props = @{(id)kIOSurfaceWidth: @(ow), (id)kIOSurfaceHeight: @(oh), (id)kIOSurfaceBytesPerElement: @4,
                                    (id)kIOSurfacePixelFormat: @((uint32_t)'BGRA')};
            sf = _surf[_surfIdx] = IOSurfaceCreate((__bridge CFDictionaryRef)props);
        }
        double tc0 = CACurrentMediaTime();
        IOSurfaceLock(sf, 0, NULL);
        tp_view_out_bgra = 1; tp_view_compose_stride(IOSurfaceGetBytesPerRow(sf) / 4);
        tp_view_compose((uint32_t *)IOSurfaceGetBaseAddress(sf), ow, oh, _pixels, fw, fh, [NSUserDefaults.standardUserDefaults boolForKey:@"SmoothPixels"]);
        tp_view_out_bgra = 0; tp_view_compose_stride(0);
        if (tc0 - _cleanAt > 0.25 && tp_view_ui_share() < 0.5) {   /* (a few times a second, while no game window covers the park) */
            if (_cleanW != ow || _cleanH != oh) { free(_clean); _clean = malloc((size_t)ow * oh * 4); _cleanW = ow; _cleanH = oh; }
            size_t bp = IOSurfaceGetBytesPerRow(sf); const uint8_t *b = IOSurfaceGetBaseAddress(sf);
            for (int y = 0; y < oh; y++) memcpy(_clean + (size_t)y * ow, b + (size_t)y * bp, (size_t)ow * 4);
            _cleanAt = tc0;
        }
        IOSurfaceUnlock(sf, 0, NULL);
        _bigW = ow; _bigH = oh;
        if (getenv("TP_PACE_DEBUG")) { static double l, worst, sum; static int n; double d = CACurrentMediaTime() - tc0; sum += d; n++; if (d > worst) worst = d;
            if (tc0 - l > 1) { fprintf(stderr, "compose %dx%d: avg %.2f ms, worst %.2f ms\n", _bigW, _bigH, sum / n * 1e3, worst * 1e3); l = tc0; worst = sum = 0; n = 0; } }
        if (live != _live || _menuWide) { _live = live; _menuWide = NO; [self setNeedsLayout:YES]; [self layout]; [self setCursorHidden:_held]; }
        [self updateGameCursor];
        [self updateScanlines:fh];
        [CATransaction begin]; [CATransaction setDisableActions:YES];
        _screen.contents = (__bridge id)sf;
        [CATransaction commit];
        return;
    }
    /* A full-screen game window (the ride and shop catalogues, the research and bank screens) in the wide view:
     * the game stops drawing the park, and the picture used to change to its 4:3 screen (stretched or boxed), a
     * jump each way. The wide picture stays instead: the last park picture at the sides (the park is not drawn
     * meanwhile), the game's screen in the column its interface takes in the park view, every frame. */
    BOOL menuWide = !live && _viewOn && [self wide] && fw == _sw && _bigW > 0 && _bigH > 0 && _surf[_surfIdx]
                    && (int)IOSurfaceGetWidth(_surf[_surfIdx]) == _bigW && (int)IOSurfaceGetHeight(_surf[_surfIdx]) == _bigH && !tp_view_debug_fps;
    if (!live && getenv("TP_MENU_DEBUG")) { static double l; if (CACurrentMediaTime() - l > 1) { l = CACurrentMediaTime();
        fprintf(stderr, "menu wide %d: viewOn %d wide %d fw %d sw %d big %dx%d surf %p\n", menuWide, _viewOn, [self wide], fw, _sw, _bigW, _bigH, _surf[_surfIdx]); } }
    if (menuWide) {
        IOSurfaceRef cur = _surf[_surfIdx], nx = _surf[_surfIdx ^ 1];
        if (!nx || (int)IOSurfaceGetWidth(nx) != _bigW || (int)IOSurfaceGetHeight(nx) != _bigH) {
            if (nx) CFRelease(nx);
            NSDictionary *props = @{(id)kIOSurfaceWidth: @(_bigW), (id)kIOSurfaceHeight: @(_bigH), (id)kIOSurfaceBytesPerElement: @4,
                                    (id)kIOSurfacePixelFormat: @((uint32_t)'BGRA')};
            nx = _surf[_surfIdx ^ 1] = IOSurfaceCreate((__bridge CFDictionaryRef)props);
        }
        IOSurfaceLock(cur, kIOSurfaceLockReadOnly, NULL); IOSurfaceLock(nx, 0, NULL);
        size_t sp = IOSurfaceGetBytesPerRow(cur), dp = IOSurfaceGetBytesPerRow(nx);
        uint8_t *src = IOSurfaceGetBaseAddress(cur), *dst = IOSurfaceGetBaseAddress(nx);
        if (getenv("TP_DEV_GAME_VIDEO")) tp_game_video = 1;   /* (development: any game window drawn as the game's video) */
        double lw = _lastLW > 0 ? _lastLW : [self layoutW], pk = [self menuPanelScale]; BOOL video = tp_game_video;
        int cw = (int)lround(_bigW * _sw / lw), cx0 = (_bigW - cw) / 2; if (cw > _bigW) cw = _bigW; if (cx0 < 0) cx0 = 0;
        BOOL clean = _clean && _cleanW == _bigW && _cleanH == _bigH && CACurrentMediaTime() - _cleanAt < 5;
        /* The panel's rows at the panel's size, at the bottom middle as in the park; the rest of their strip shows the
         * park (as it was before the window came); the window above at its full size. */
        int pr = [self panelRows:fh], ys = video || pk == 1 ? _bigH : (int)lround((double)_bigH * (fh - pr) / fh);
        int pw = (int)lround(cw * pk), ph = (int)lround((double)_bigH * pr / fh * pk), px0 = (_bigW - pw) / 2, py0 = _bigH - ph;
        for (int y = 0; y < _bigH; y++) {
            uint32_t *d = (uint32_t *)(dst + (size_t)y * dp);
            if (video) for (int x = 0; x < _bigW; x++) d[x] = 0xff000000u;   /* (a video: black beside it) */
            else if (!_menuWide && clean) memcpy(d, _clean + (size_t)y * _bigW, (size_t)_bigW * 4);   /* (entering: the park picture around it, */
            else if (!_menuWide || y >= ys) memcpy(d, src + (size_t)y * sp, (size_t)_bigW * 4);   /* as it was before the window came) */
            else { memcpy(d, src + (size_t)y * sp, (size_t)cx0 * 4); memcpy(d + cx0 + cw, src + (size_t)y * sp + (size_t)(cx0 + cw) * 4, (size_t)(_bigW - cx0 - cw) * 4); }
            if (y >= ys) {                                  /* the panel's strip */
                if (y < py0 || ph <= 0) continue;
                const uint32_t *row = _pixels + (size_t)(fh - pr + (y - py0) * pr / ph) * fw;
                for (int x = 0; x < pw; x++) { uint32_t c = row[(size_t)x * fw / pw]; d[px0 + x] = (c & 0xff00ff00u) | (c & 0xffu) << 16 | (c >> 16 & 0xffu); }
                continue;
            }
            const uint32_t *row = _pixels + (size_t)(y * fh / _bigH) * fw;
            for (int x = 0; x < cw; x++) { uint32_t c = row[(size_t)x * fw / cw];   /* (RGBA in memory -> BGRA) */
                d[cx0 + x] = (c & 0xff00ff00u) | (c & 0xffu) << 16 | (c >> 16 & 0xffu); }
        }
        IOSurfaceUnlock(nx, 0, NULL); IOSurfaceUnlock(cur, kIOSurfaceLockReadOnly, NULL);
        _surfIdx ^= 1;
        if (!_menuWide || _live) { _menuWide = YES; _live = NO; [self setNeedsLayout:YES]; [self layout]; }
        [self updateGameCursor];
        [self updateScanlines:fh];
        [CATransaction begin]; [CATransaction setDisableActions:YES];
        _screen.contents = (__bridge id)nx;
        [CATransaction commit];
        return;
    }
    if (_menuWide) { _menuWide = NO; [self setNeedsLayout:YES]; [self layout]; }
    if (!live && tp_view_debug_fps) {                /* the frame-rate line over the game's own picture: drawn at 3x */
        int ow = fw * 3, oh = fh * 3;
        if (!_big || _bigW != ow || _bigH != oh) { free(_big); _bigW = ow; _bigH = oh; _big = malloc(sizeof *_big * _bigW * _bigH); }
        for (int y = 0; y < oh; y++) for (int x = 0; x < ow; x++) _big[(size_t)y * ow + x] = _pixels[(y / 3) * fw + x / 3];
        tp_view_debug_hud(_big, ow, oh);
        px = _big; w = ow; h = oh;
    }
    if (live != _live) { _live = live; [self setNeedsLayout:YES]; [self layout]; if (live) [self setCursorHidden:_held]; }
    [self updateGameCursor];
    [self updateScanlines:fh];
    CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef ctx = CGBitmapContextCreate(px, w, h, 8, w * 4, cs,
                                             kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    double ti0 = CACurrentMediaTime();
    CGImageRef img = CGBitmapContextCreateImage(ctx);
    [CATransaction begin]; [CATransaction setDisableActions:YES];
    _screen.contents = (__bridge id)img;
    [CATransaction commit];
    CGImageRelease(img); CGContextRelease(ctx); CGColorSpaceRelease(cs);
    if (getenv("TP_PACE_DEBUG")) { static double l, worst; double d = CACurrentMediaTime() - ti0; if (d > worst) worst = d; if (ti0 - l > 1) { fprintf(stderr, "image upload worst %.2f ms\n", worst * 1e3); l = ti0; worst = 0; } }
}
/* CRT: a dark line under every row of the game's screen, over the picture (smooth pixels). */
- (void)updateScanlines:(int)rows {
    BOOL crt = [NSUserDefaults.standardUserDefaults integerForKey:@"SmoothPixels"] == 2;
    [CATransaction begin]; [CATransaction setDisableActions:YES];
    _scan.hidden = !crt;
    if (crt && rows != _scanRows) {
        _scanRows = rows;
        int h = rows * 4; uint32_t *px = calloc((size_t)h, 4);
        for (int y = 0; y < h; y++) px[y] = (y % 4 == 3) ? 0x70000000u : (y % 4 == 2 ? 0x30000000u : 0);
        CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        CGContextRef ctx = CGBitmapContextCreate(px, 1, h, 8, 4, cs, kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
        double ti0 = CACurrentMediaTime();
    CGImageRef img = CGBitmapContextCreateImage(ctx);
        _scan.contents = (__bridge id)img;
        CGImageRelease(img); CGContextRelease(ctx); CGColorSpaceRelease(cs); free(px);
    }
    if (crt) _scan.frame = _screen.frame;
    _screen.magnificationFilter = [NSUserDefaults.standardUserDefaults integerForKey:@"SmoothPixels"] ? kCAFilterLinear : kCAFilterNearest;
    [CATransaction commit];
}
- (void)updateTrackingAreas {
    if (_tracking) [self removeTrackingArea:_tracking];
    _tracking = [[NSTrackingArea alloc] initWithRect:NSZeroRect
        options:NSTrackingMouseMoved | NSTrackingMouseEnteredAndExited | NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect
        owner:self userInfo:nil];
    [self addTrackingArea:_tracking];
    [super updateTrackingAreas];
}
- (void)setCursorHidden:(BOOL)hidden {
    if (hidden == _cursorHidden) return;
    _cursorHidden = hidden;
    if (hidden) [NSCursor hide]; else [NSCursor unhide];
}
/* Full screen: macOS's hot corners act on the Mac's pointer wherever it is (Quick Note in the bottom right, reached
 * by the game's edge scrolling). There the Mac's pointer is detached from the mouse (CGAssociateMouseAndMouseCursor-
 * Position) and left where it was, hidden; the game is given the mouse's own place from its movements, corners
 * included, and its cursor is drawn by the view (tp_view_hw_cursor off). When it is let go (the menu bar's row, other
 * apps, leaving full screen) the Mac's pointer is put there, 6 points from any corner. The menu bar's row is held
 * too (letting go there, the top edge's scrolling, put the pointer back in the middle: events queued before the let-go
 * carried the middle as their place, and the next hold started there), so in full screen the menu bar is reached
 * from the keyboard; the Visitor View and the tests are left to the Mac. (The first version moved the
 * Mac's pointer with every event, and macOS ignores the mouse and trackpad for a quarter of a second after each such
 * move: the pointer barely moved in full screen.) */
- (void)setCornerGuard:(BOOL)on { _cornerGuard = on; if (!on) [self releasePointer]; }   /* (let go when the app or window loses focus: releaseInput) */
/* The window lost the keyboard (another app, another window): what the game was told is held is let go, since the
 * releases will go elsewhere — a mouse button (a drag that never ends), Shift, Ctrl, Alt, keys, Tab's fast forward. */
- (void)releaseInput {
    [self releasePointer];
    _panning = NO;
    if (_buttons) { _buttons = 0; if (_live) tp_view_mouse(_lastFx, _lastFy, 0); else tp_host_mouse(_lastFx, _lastFy, 0); }
    tp_host_shift(0);
    for (int k = 0; k < 128; k++) if (_keyDown[k]) { _keyDown[k] = 0; tp_host_key_raw((uint8_t)k, 0); }
    if (_fastForward) {
        _fastForward = NO; self.window.subtitle = @"";
        if (!getenv("TP_CYCLES")) tp_host_set_cycles([NSUserDefaults.standardUserDefaults doubleForKey:@"Cycles"]);
    }
}
- (BOOL)playing3DO { return _v3 != NULL; }
- (void)releasePointer {
    if (!_held) return;
    _held = NO; _letGoAt = NSProcessInfo.processInfo.systemUptime;
    NSScreen *sc = self.window.screen;
    if (sc) {                                             /* (the Mac's pointer where the mouse is, off the corners) */
        NSRect s = sc.frame; const CGFloat k = 6; NSPoint c = _virt;
        BOOL ex = c.x < NSMinX(s) + k || c.x > NSMaxX(s) - 1 - k, ey = c.y < NSMinY(s) + k || c.y > NSMaxY(s) - 1 - k;
        if (ex && ey) c.y = c.y < NSMidY(s) ? NSMinY(s) + k : NSMaxY(s) - 1 - k;
        CGWarpMouseCursorPosition(CGPointMake(c.x, NSMaxY(NSScreen.screens[0].frame) - c.y));
    }
    CGAssociateMouseAndMouseCursorPosition(true);         /* (also ends the warp's suppression of the mouse) */
}
- (NSPoint)pointOf:(NSEvent *)e {
    NSPoint loc = e.locationInWindow;
    NSScreen *sc = self.window.screen;
    static int test = -1; if (test < 0) test = getenv("DS_TEST") && !getenv("TP_CORNER_GUARD");   /* (TP_CORNER_GUARD=1: in tests too) */
    if (!_cornerGuard || !sc || test || !self.window.isKeyWindow || !NSApp.isActive || tp_walk_active() || _walkDrop) {
        [self releasePointer];
        if (e.timestamp < _letGoAt && sc) loc = [self.window convertPointFromScreen:_virt];   /* (queued while held) */
        return [self convertPoint:loc fromView:nil];
    }
    NSRect s = sc.frame;
    if (!_held) {
        /* (an event from before the last let-go has the held pointer's place, the window's middle: where the mouse
         * was is _virt) */
        NSPoint g = e.timestamp < _letGoAt ? _virt : [self.window convertPointToScreen:loc];
        /* The Mac's pointer, hidden, to the middle of the window: clicks go to the window under it, and where the
         * mouse was can be outside it (the strip beside the notch above a window that keeps clear of it: every
         * click went there and the game took none). (Associated again right after the move: no quarter second of
         * ignored input after it.) */
        NSRect wf = self.window.frame;
        CGWarpMouseCursorPosition(CGPointMake(NSMidX(wf), NSMaxY(NSScreen.screens[0].frame) - NSMidY(wf)));
        CGAssociateMouseAndMouseCursorPosition(true);
        _virt = g; _held = YES; CGAssociateMouseAndMouseCursorPosition(false);
        _warpAt = NSProcessInfo.processInfo.systemUptime; _warpDX = NSMidX(wf) - g.x; _warpDY = g.y - NSMidY(wf);
        fprintf(stderr, "mouse: held at %.0f,%.0f\n", g.x, g.y);
    } else {
        NSEventType t = e.type;
        if (t == NSEventTypeMouseMoved || t == NSEventTypeLeftMouseDragged || t == NSEventTypeRightMouseDragged || t == NSEventTypeOtherMouseDragged) {
            CGFloat dx = e.deltaX, dy = e.deltaY;
            /* The first move after the hold's warp can carry the warp's own jump (to the window's middle): left out. */
            if (_warpAt && fabs(_warpDX) + fabs(_warpDY) > 16 && e.timestamp - _warpAt < 0.5 &&
                fabs(dx - _warpDX) < 0.25 * fabs(_warpDX) + 24 && fabs(dy - _warpDY) < 0.25 * fabs(_warpDY) + 24) {
                fprintf(stderr, "mouse: the hold's warp (%.0f,%.0f) left out of a move (%.0f,%.0f)\n", _warpDX, _warpDY, dx, dy);
                dx -= _warpDX; dy -= _warpDY;
            }
            _warpAt = 0;
            if (fabs(dx) + fabs(dy) > 300) fprintf(stderr, "mouse: a move of %.0f,%.0f in one event\n", dx, dy);
            _virt.x += dx; _virt.y -= dy;
        }
    }
    _virt.x = fmin(fmax(_virt.x, NSMinX(s)), NSMaxX(s) - 1);
    _virt.y = fmin(fmax(_virt.y, NSMinY(s)), NSMaxY(s) - 1);
    return [self convertPoint:[self.window convertPointFromScreen:_virt] fromView:nil];
}
- (void)sendMouse:(NSEvent *)e {
    /* The Mac sends a plain move only while no button is down: a button still held here then lost its release (the
     * game kept it down: the right button scrolls the park and the game takes no clicks meanwhile). Let go. */
    if (e.type == NSEventTypeMouseMoved && (_buttons & 7)) {
        fprintf(stderr, "mouse: buttons %#x were still held at a plain move, let go\n", _buttons & 7); _buttons &= ~7u;
    }
    NSPoint p = [self pointOf:e];
    _mouse = p; _mouseIn = YES;
    if (_walkDrop || tp_walk_active()) { [self setCursorHidden:NO]; return; }   /* (the Visitor View takes the pointer) */
    NSRect r = [self pointerRect];
    double fx = (p.x - r.origin.x) / r.size.width, fy = 1.0 - (p.y - r.origin.y) / r.size.height;
    BOOL inside = fx >= 0 && fx < 1 && fy >= 0 && fy < 1;
    /* The game's own screen draws its cursor: the Mac's hides over it. The park view shows the game's
     * cursor as a Mac cursor instead (smooth at the display's rate; the menu bar keeps its own). */
    [self setCursorHidden:(inside && !_live && self.window.isKeyWindow && ![NSUserDefaults.standardUserDefaults boolForKey:@"ShowMacCursor"]) || _held];
    fx = fx < 0 ? 0 : fx > 0.9999 ? 0.9999 : fx;
    fy = fy < 0 ? 0 : fy > 0.9999 ? 0.9999 : fy;
    if (_menuWide) [self menuMapX:&fx y:&fy];
    _lastFx = fx; _lastFy = fy;
    if (_live) tp_view_mouse(fx, fy, _buttons);      /* mapped into the park */
    else tp_host_mouse(fx, fy, _buttons);            /* the game's own screen */
}

/* The game's cursor (view.c reads it from the game) as the Mac cursor over the park view, at the view's scale. */
- (void)updateGameCursor {
    BOOL want = _live && !_walkDrop && !_held && ![NSUserDefaults.standardUserDefaults boolForKey:@"ShowMacCursor"];   /* (held: drawn by the view) */
    tp_view_hw_cursor = want;
    if (!want) { if (_gameCursor) { _gameCursor = nil; [self.window invalidateCursorRectsForView:self]; } return; }
    static uint32_t px[64 * 64]; int w = 0, h = 0, ox = 0, oy = 0;
    int serial = tp_view_cursor_rgba(px, &w, &h, &ox, &oy);
    NSRect vr = [self viewRect];
    double kx = vr.size.width / [self layoutW], ky = vr.size.height / _sh;   /* points per game pixel */
    /* (its colours too: the serial follows its shape, but a new game fades the palette in from black, and a pointer
     * made then stayed black until its shape changed) */
    uint32_t hash = 2166136261u; for (int y = 0; y < h && y < 64; y++) for (int x = 0; x < w && x < 64; x++) hash = (hash ^ px[y * 64 + x]) * 16777619u;
    if (serial == _curSerial && hash == _curHash && fabs(kx - _curKx) < 0.01 && fabs(ky - _curKy) < 0.01 && (_gameCursor || !serial)) return;
    _curSerial = serial; _curHash = hash; _curKx = kx; _curKy = ky;
    if (!serial || w <= 0 || h <= 0) { _gameCursor = nil; [self.window invalidateCursorRectsForView:self]; return; }
    double cap = getenv("TP_CURSOR_CAP") ? atof(getenv("TP_CURSOR_CAP")) : 64;      /* macOS shows no cursor larger than 64 points */
    if (w * kx > cap || h * ky > cap) { double f = cap / fmax(w * kx, h * ky); kx *= f; ky *= f; }
    CGFloat scale = self.window.backingScaleFactor ?: 2;
    int pw = (int)lround(w * kx * scale), ph = (int)lround(h * ky * scale);
    NSBitmapImageRep *rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL pixelsWide:pw pixelsHigh:ph bitsPerSample:8 samplesPerPixel:4
                                                                     hasAlpha:YES isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:pw * 4 bitsPerPixel:32];
    uint8_t *d = rep.bitmapData;
    for (int y = 0; y < ph; y++) for (int x = 0; x < pw; x++) {
        uint32_t c = px[(y * h / ph) * 64 + x * w / pw];
        uint8_t *o = d + (size_t)y * pw * 4 + x * 4;
        if (!c) { o[0] = o[1] = o[2] = o[3] = 0; continue; }
        o[0] = c & 255; o[1] = (c >> 8) & 255; o[2] = (c >> 16) & 255; o[3] = 255;
    }
    rep.size = NSMakeSize(w * kx, h * ky);
    NSImage *img = [[NSImage alloc] initWithSize:rep.size]; [img addRepresentation:rep];
    _gameCursor = [[NSCursor alloc] initWithImage:img hotSpot:NSMakePoint(fmax(0, -ox * kx), fmax(0, -oy * ky))];
    [self.window invalidateCursorRectsForView:self];
    if (_mouseIn && self.window.isKeyWindow) [_gameCursor set];
    if (getenv("TP_CURSOR_DEBUG")) fprintf(stderr, "cursor: serial %d, %dx%d px -> %.0fx%.0f pt, mouse in %d, key %d, hidden %d\n", serial, w, h, w * kx, h * ky, _mouseIn, self.window.isKeyWindow, _cursorHidden);
}
- (void)resetCursorRects {
    if (_walkDrop) [self addCursorRect:self.bounds cursor:NSCursor.crosshairCursor];
    else if (tp_walk_active()) [self addCursorRect:self.bounds cursor:NSCursor.openHandCursor];
    else if (_gameCursor) [self addCursorRect:[self viewRect] cursor:_gameCursor];
}

- (BOOL)menuWide { return _menuWide; }
- (uint32_t *)shownPicture:(int *)w :(int *)h {
    if (!_menuWide || !_surf[_surfIdx]) return NULL;
    IOSurfaceRef sf = _surf[_surfIdx]; int W = (int)IOSurfaceGetWidth(sf), H = (int)IOSurfaceGetHeight(sf);
    uint32_t *out = malloc(4 * (size_t)W * H); IOSurfaceLock(sf, kIOSurfaceLockReadOnly, NULL);
    const uint8_t *b = IOSurfaceGetBaseAddress(sf); size_t bp = IOSurfaceGetBytesPerRow(sf);
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) { uint32_t c = ((const uint32_t *)(b + (size_t)y * bp))[x]; out[(size_t)y * W + x] = (c & 0xff00ff00u) | (c & 0xffu) << 16 | (c >> 16 & 0xffu); }
    IOSurfaceUnlock(sf, kIOSurfaceLockReadOnly, NULL); *w = W; *h = H; return out;
}
/* A pointer from elsewhere (a game controller), in 0..1 of what is shown. */
- (void)pointerAt:(double)fx :(double)fy buttons:(uint32_t)b {
    if (_menuWide) { NSRect v = [self viewRect], c = [self columnRect]; fx = (fx * v.size.width - (c.origin.x - v.origin.x)) / c.size.width; fx = fmin(fmax(fx, 0), 0.9999);   /* (into the column) */
                     [self menuMapX:&fx y:&fy]; }
    if (_live) tp_view_mouse(fx, fy, b); else tp_host_mouse(fx, fy, b);
}

/* ---- the park view: zoom and widescreen ---- */
/* The game's screen mode decides the canvas scale; when it changes ([R] in the
 * park), the view carries over to the new scale. */
- (void)updateGeometry {
    int sw, sh, wh; tp_view_geometry(&sw, &sh, &wh);
    if (sw == _sw) return;
    double k = _sw ? (double)sw / _sw : 1;
    _sw = sw; _sh = sh; _wh = wh;
    if (_viewOn) { _cx *= k; _cy *= k; [self clampCenter]; _lastLW = [self layoutW]; tp_view_set(1, _cx, _cy, _zoom, _lastLW); }
}
- (double)minZoom {
    double w, h; tp_view_canvas_size(&w, &h);
    return fmax(0.1, fmin([self layoutW] / w, _wh / h));
}
- (void)clampCenter {
    double w, h; tp_view_canvas_size(&w, &h);
    double hw = [self layoutW] / 2.0 / _zoom, top = _wh / 2.0 / _zoom;
    double bottom = ([self wide] ? _sh - _wh / 2.0 : _wh / 2.0) / _zoom;   /* wide: the park shows down to the bottom beside the panel */
    _cx = hw * 2 >= w ? w / 2 : fmin(fmax(_cx, hw), w - hw);
    _cy = top + bottom >= h ? (h - bottom + top) / 2 : fmin(fmax(_cy, top), h - bottom);
}
/* Turns the park view on or off as zoom and the window's shape ask, and tells view.c where it looks. */
- (void)applyView {
    BOOL on = _zoom != 1 || [self wide];
    if (on && !_viewOn && CACurrentMediaTime() - _offAt > 1.0) tp_view_camera_center(&_cx, &_cy);   /* start where the game looks */
    if (on) [self clampCenter];
    if (!on && _viewOn) _offAt = CACurrentMediaTime();
    _lastLW = [self layoutW];
    if (!tp_view_set(on, _cx, _cy, _zoom, _lastLW)) { on = NO; _zoom = 1; }   /* not in the park (the intro, say) */
    _viewOn = on;
}
/* Zoom to z keeping the park point under p (view coordinates; NAN: the centre) in place. */
- (void)zoomTo:(double)z at:(NSPoint)p {
    [self updateGeometry];
    if (!_viewOn) { if (CACurrentMediaTime() - _offAt > 1.0) tp_view_camera_center(&_cx, &_cy); _zoom = 1; }
    NSRect r = [self viewRect]; double lw = [self layoutW];
    double fx = isnan(p.x) ? 0.5 : (p.x - r.origin.x) / r.size.width;
    double fy = isnan(p.x) ? _wh / 2.0 / _sh : 1.0 - (p.y - r.origin.y) / r.size.height;
    double ax = _cx + (fx - 0.5) * lw / _zoom, ay = _cy + (fy * _sh - _wh / 2.0) / _zoom;
    z = fmin(fmax(z, [self minZoom]), 4.0);
    if (fabs(z - 1) < 0.04) z = 1;                   /* a detent at the game's own scale */
    _zoom = z; _viewMoved = YES;
    _cx = ax - (fx - 0.5) * lw / z; _cy = ay - (fy * _sh - _wh / 2.0) / z;
    [self applyViewKeepingCenter];
}
- (void)applyViewKeepingCenter {
    BOOL on = _zoom != 1 || [self wide];
    if (on) [self clampCenter];
    if (!on) _offAt = CACurrentMediaTime();
    _lastLW = [self layoutW];
    if (!tp_view_set(on, _cx, _cy, _zoom, _lastLW)) { on = NO; _zoom = 1; }
    _viewOn = on;
}
- (void)panBy:(double)dx :(double)dy {               /* view points */
    if (!_viewOn) return;
    _viewMoved = YES;
    double k = [self layoutW] / _zoom / [self viewRect].size.width;
    _cx += dx * k; _cy += dy * k;
    [self clampCenter];
    tp_view_set(1, _cx, _cy, _zoom, _lastLW = [self layoutW]);
}
- (void)centerOn:(double)cx :(double)cy {           /* canvas point (development control) */
    [self updateGeometry];
    if (!_viewOn) _zoom = _zoom ?: 1;
    _cx = cx; _cy = cy; [self applyViewKeepingCenter];
}
- (void)edgePan {                                    /* the pointer at the window's edge scrolls, as in the game */
    if (!_mouseIn || !self.window.isKeyWindow || !_live) return;
    NSRect r = [self viewRect]; NSPoint p = _mouse; double m = 6, v = 9;
    double fx = (p.x - r.origin.x) / r.size.width, fy = 1.0 - (p.y - r.origin.y) / r.size.height;
    double lw = [self layoutW], col = (lw - _sw) / 2 / lw;
    BOOL panel = fy > (double)_wh / _sh && fx >= col && fx < 1 - col;   /* over the panel: only its outer edge scrolls */
    double dx = p.x < NSMinX(r) + m ? -v : p.x > NSMaxX(r) - m ? v : 0;
    double dy = p.y > NSMaxY(r) - m ? -v : p.y < NSMinY(r) + m ? v : 0;   /* (corners: both, diagonally) */
    if (panel && !(p.y < NSMinY(r) + m) && !(p.x < NSMinX(r) + m) && !(p.x > NSMaxX(r) - m)) return;
    if (dx || dy) [self panBy:dx :dy];
}
- (void)magnifyWithEvent:(NSEvent *)e {
    if (tp_walk_active()) return;
    [self zoomTo:_zoom * (1 + e.magnification) at:[self pointOf:e]];
}
- (void)scrollWheel:(NSEvent *)e {
    double dy = e.scrollingDeltaY, dx = e.scrollingDeltaX;
    if (tp_walk_active()) {                            /* two-finger scroll looks around too */
        if (!e.hasPreciseScrollingDeltas) { dx *= 12; dy *= 12; }
        _lookDX += dx * 0.004; _lookDY += dy * 0.004; return;
    }
    if (!e.hasPreciseScrollingDeltas) { dx *= 12; dy *= 12; }
    if (e.modifierFlags & (NSEventModifierFlagCommand | NSEventModifierFlagOption))
        [self zoomTo:_zoom * pow(1.006, dy) at:[self pointOf:e]];
    else [self panBy:-dx :-dy];
}
- (void)zoomIn:(id)sender { [self zoomTo:_zoom * 1.25 at:NSMakePoint(NAN, NAN)]; }
- (void)zoomOut:(id)sender { [self zoomTo:_zoom / 1.25 at:NSMakePoint(NAN, NAN)]; }
- (void)zoomActual:(id)sender { [self zoomTo:1 at:NSMakePoint(NAN, NAN)]; }
- (void)mouseMoved:(NSEvent *)e { [self sendMouse:e]; }
- (void)mouseDragged:(NSEvent *)e {
    if (tp_walk_active()) { _lookDX -= e.deltaX * 0.005; _lookDY -= e.deltaY * 0.005; return; }   /* grab and turn the view, as Street View */
    if (_panning) [self dragPan:e]; else [self sendMouse:e];
}
- (void)rightMouseDragged:(NSEvent *)e { [self sendMouse:e]; }
- (void)otherMouseDragged:(NSEvent *)e { if (_panning) [self dragPan:e]; else [self sendMouse:e]; }
- (void)mouseExited:(NSEvent *)e { [self setCursorHidden:NO]; _mouseIn = NO; }
- (void)mouseEntered:(NSEvent *)e { [self sendMouse:e]; }
- (void)mouseDown:(NSEvent *)e {
    if (_v3) { [self end3DO]; return; }                 /* (a click ends a 3DO ride video, as the game's own) */
    if (_walkDrop) {                                    /* the Visitor View: drop in here */
        NSPoint p = [self convertPoint:e.locationInWindow fromView:nil];
        NSRect r = _live ? [self viewRect] : [self screenRect];
        double fx = (p.x - r.origin.x) / r.size.width, fy = 1.0 - (p.y - r.origin.y) / r.size.height, wx, wy;
        if (fx >= 0 && fx < 1 && fy >= 0 && fy < 1 && tp_view_point_to_world(fx, fy, &wx, &wy) && [self walkStartAt:wx :wy]) return;
        NSBeep(); return;
    }
    if (tp_walk_active()) { [NSCursor.closedHandCursor set]; return; }
    if (_live && (e.modifierFlags & NSEventModifierFlagOption)) { [self startPan:e]; return; }   /* Option-drag pans too */
    /* Control-click acts as the right button for one-button trackpads. */
    _buttons |= (e.modifierFlags & NSEventModifierFlagControl) ? 2 : 1;
    [self sendMouse:e];
}
- (void)mouseUp:(NSEvent *)e {
    if (tp_walk_active() || _walkDrop) { _buttons &= ~3u;   /* (the Visitor View came during a drag: no button left held) */ if (tp_walk_active()) [NSCursor.openHandCursor set]; return; }
    if (_panning) { _panning = NO; return; } _buttons &= ~3u; [self sendMouse:e]; }
- (void)rightMouseDown:(NSEvent *)e { if (_v3) { [self end3DO]; return; } if (tp_walk_active() || _walkDrop) return; _buttons |= 2; [self sendMouse:e]; }
- (void)rightMouseUp:(NSEvent *)e { _buttons &= ~2u; [self sendMouse:e]; }
- (void)otherMouseDown:(NSEvent *)e {
    if (_live) { [self startPan:e]; return; }          /* the park view: the middle button drags it (the game does not use it) */
    _buttons |= 4; [self sendMouse:e];
}
- (void)otherMouseUp:(NSEvent *)e { if (_panning) { _panning = NO; return; } _buttons &= ~4u; [self sendMouse:e]; }
- (void)startPan:(NSEvent *)e { _panning = YES; _panFrom = [self pointOf:e]; }
- (void)dragPan:(NSEvent *)e {                        /* the park follows the pointer */
    NSPoint p = [self pointOf:e];
    [self panBy:-(p.x - _panFrom.x) :(p.y - _panFrom.y)];
    _panFrom = p;
}
- (void)keyDown:(NSEvent *)e {
    if (e.modifierFlags & NSEventModifierFlagCommand) { [super keyDown:e]; return; }
    if (_v3) { [self end3DO]; return; }
    unsigned short kc = e.keyCode;
    if (kc == 0x30) {                                   /* Tab, held: fast forward (the game does not use Tab) */
        if (!e.isARepeat && !getenv("TP_CYCLES")) {
            tp_host_set_cycles([NSUserDefaults.standardUserDefaults doubleForKey:@"Cycles"] * 4);
            self.window.subtitle = @"Fast forward"; _fastForward = YES;
        }
        return;
    }
    if (_walkDrop && kc == 0x35) { [self endDrop]; return; }   /* Escape: no Visitor View after all */
    if (tp_walk_active()) { [self walkKey:kc down:YES]; return; }
    if (_viewOn && kc >= 0x7b && kc <= 0x7e) {          /* the park view: the arrows move it */
        double step = 40;
        [self panBy:(kc == 0x7b ? -step : kc == 0x7c ? step : 0) :(kc == 0x7e ? -step : kc == 0x7d ? step : 0)];
        return;
    }
    uint8_t scan = kc < 128 ? scan_of[kc] : 0;
    if (!scan) return;
    uint32_t ascii = 0;
    NSString *chars = e.characters;
    if (chars.length == 1) {
        unichar ch = [chars characterAtIndex:0];
        if (ch == 0x7f) ch = 8;                 /* Mac delete key is DOS backspace */
        if (ch == 3) ch = 13;                   /* keypad enter */
        if (ch < 128 && !(e.modifierFlags & NSEventModifierFlagOption)) ascii = ch;
    }
    if (kc == 0x75) ascii = 0;                  /* forward delete */
    if (!e.isARepeat) { tp_host_key_raw(scan, 1); _keyDown[scan & 127] = 1; }
    if (ascii) tp_host_key((uint32_t)scan << 8 | ascii);
    else tp_host_key(function_key(scan, e.modifierFlags));
}
- (void)keyUp:(NSEvent *)e {
    if (e.keyCode == 0x30) {
        if (!getenv("TP_CYCLES")) tp_host_set_cycles([NSUserDefaults.standardUserDefaults doubleForKey:@"Cycles"]);
        self.window.subtitle = @""; _fastForward = NO;
        return;
    }
    if (e.modifierFlags & NSEventModifierFlagCommand) return;
    if (tp_walk_active()) { [self walkKey:e.keyCode down:NO]; return; }
    uint8_t scan = e.keyCode < 128 ? scan_of[e.keyCode] : 0;
    if (scan) { tp_host_key_raw(scan, 0); _keyDown[scan & 127] = 0; }
}
- (void)flagsChanged:(NSEvent *)e {
    NSEventModifierFlags m = e.modifierFlags; uint32_t f = 0;
    if (m & NSEventModifierFlagShift) f |= 3;
    if (m & NSEventModifierFlagControl) f |= 4;
    if (m & NSEventModifierFlagOption) f |= 8;
    if (m & NSEventModifierFlagCapsLock) f |= 0x40;
    _wkRun = (m & NSEventModifierFlagShift) != 0;
    if (tp_walk_active()) return;
    tp_host_shift(f);
}

/* ---- the Visitor View (runtime/walk.c) ---- */
- (void)visitorView:(id)sender {
    if (tp_walk_active()) { [self walkStop]; return; }
    if (_walkDrop) { [self endDrop]; return; }
    _walkDrop = YES; _gameCursor = nil;
    self.window.subtitle = @"Visitor View: click where you want to stand (Esc cancels)";
    [self.window invalidateCursorRectsForView:self];
    [NSCursor.crosshairCursor set];
}
- (void)endDrop {
    _walkDrop = NO; self.window.subtitle = @"";
    [self.window invalidateCursorRectsForView:self];
}
- (BOOL)walkStartAt:(double)x :(double)y {
    if (!tp_walk_start(x, y)) return NO;
    [self endDrop];
    memset(_wk, 0, sizeof _wk); _walkLast = 0; _lookDX = _lookDY = 0; _holdUntil = 0; _videoType = -2;
    [self setCursorHidden:NO]; _gameCursor = nil;
    [self.window invalidateCursorRectsForView:self];
    return YES;
}
- (void)walkStop {
    tp_walk_stop();
    [self stopRideVideo];
    _hud.hidden = YES; self.window.subtitle = @"";
    [self.window invalidateCursorRectsForView:self];
    [self setNeedsLayout:YES]; [self layout];        /* the game's picture where it was; the park view looks where you were */
}
- (void)walkHold:(double)fwd :(double)strafe :(double)turn :(double)seconds {
    _hold[0] = fwd; _hold[1] = strafe; _hold[2] = turn; _holdUntil = CACurrentMediaTime() + seconds;
}
- (void)walkPad:(double)fwd :(double)strafe :(double)lookX :(double)lookY { _pad[0] = fwd; _pad[1] = strafe; _pad[2] = lookX; _pad[3] = lookY; }
/* W/↑ forward, S/↓ back, A/D sideways, Q/E or ←/→ turn, Shift runs; F follow a visitor, Space hop to
 * another, R ride along, V eye level or diorama, N walk through things, Esc leave. */
- (void)walkKey:(unsigned short)kc down:(BOOL)down {
    int k = kc == 0x0d || kc == 0x7e ? 0 : kc == 0x01 || kc == 0x7d ? 1 : kc == 0x00 ? 2 : kc == 0x02 ? 3
          : kc == 0x0c || kc == 0x7b ? 4 : kc == 0x0e || kc == 0x7c ? 5 : -1;
    if (k >= 0) { _wk[k] = down; return; }
    if (!down) return;
    if (kc == 0x35) [self walkStop];
    else if (kc == 0x03) tp_walk_command(TP_WALK_FOLLOW);
    else if (kc == 0x31) tp_walk_command(TP_WALK_HOP);
    else if (kc == 0x0f) tp_walk_command(TP_WALK_RIDE);
    else if (kc == 0x09) tp_walk_command(TP_WALK_VIEW);
    else if (kc == 0x2d) tp_walk_command(TP_WALK_NOCLIP);
}
- (void)presentWalk {
    double t = CACurrentMediaTime(), dt = _walkLast ? t - _walkLast : 1.0 / 60; _walkLast = t;
    double fwd = _wk[0] - _wk[1] + _pad[0], strafe = _wk[3] - _wk[2] + _pad[1], turn = _wk[5] - _wk[4];
    if (t < _holdUntil) { fwd += _hold[0]; strafe += _hold[1]; turn += _hold[2]; }
    fwd = fmax(-1, fmin(1, fwd)); strafe = fmax(-1, fmin(1, strafe)); turn = fmax(-1, fmin(1, turn));
    tp_walk_input(fwd, strafe, turn, _lookDX + _pad[2] * dt * 2.2, _lookDY + _pad[3] * dt * 1.6, _wkRun, dt);
    _lookDX = _lookDY = 0;
    /* drawn at a chunky size (the game's pixels are big too) and scaled up without smoothing */
    NSRect b = self.bounds; if (b.size.width < 8 || b.size.height < 8) return;
    long rows = [NSUserDefaults.standardUserDefaults integerForKey:@"WalkRows"]; if (rows < 150 || rows > 1200) rows = 450;
    int oh = (int)rows, ow = (int)lround(oh * b.size.width / b.size.height);
    if (!_walkPx || _walkW != ow || _walkH != oh) { free(_walkPx); _walkW = ow; _walkH = oh; _walkPx = calloc((size_t)ow * oh, 4); }
    if (!tp_walk_render(_walkPx, ow, oh)) for (int i = 0; i < ow * oh; i++) _walkPx[i] = 0xff000000u;   /* (the first picture is on its way) */
    CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef ctx = CGBitmapContextCreate(_walkPx, ow, oh, 8, ow * 4, cs, kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    CGImageRef img = CGBitmapContextCreateImage(ctx);
    [CATransaction begin]; [CATransaction setDisableActions:YES];
    _screen.frame = b; _screen.magnificationFilter = kCAFilterNearest; _screen.contents = (__bridge id)img; _scan.hidden = YES;
    if (!_hud) {
        _hud = [CATextLayer layer];
        _hud.actions = @{@"contents": [NSNull null], @"bounds": [NSNull null], @"position": [NSNull null], @"hidden": [NSNull null], @"string": [NSNull null]};
        _hud.fontSize = 13; _hud.alignmentMode = kCAAlignmentCenter; _hud.foregroundColor = NSColor.whiteColor.CGColor;
        _hud.backgroundColor = [NSColor colorWithWhite:0 alpha:0.55].CGColor;
        [self.layer addSublayer:_hud];
    }
    char st[256]; tp_walk_status(st, sizeof st);
    if (strcmp(st, _hudText)) { snprintf(_hudText, sizeof _hudText, "%s", st); _hud.string = [NSString stringWithUTF8String:st] ?: @""; }
    _hud.contentsScale = self.window.backingScaleFactor ?: 2;
    _hud.frame = NSMakeRect(0, 0, b.size.width, 22); _hud.hidden = NO;
    [CATransaction commit];
    CGImageRelease(img); CGContextRelease(ctx); CGColorSpaceRelease(cs);
    [self updateRideVideo];
}
/* A ride's video, if the player put one in ~/Games/ThemeParkNative/rides: named by the ride's type
 * number (0..28, the game's type table order) or its name, .mp4, .mov or .m4v. It plays (looping)
 * while the visitor followed is on that ride; otherwise the view from the seat shows. */
- (NSURL *)rideVideoFor:(int)type name:(const char *)name {
    NSString *dir = [dataRoot() stringByAppendingPathComponent:@"rides"];
    NSMutableArray *stems = [NSMutableArray array];
    if (type >= 0) [stems addObject:[NSString stringWithFormat:@"%d", type]];
    if (name && *name) [stems addObject:[NSString stringWithUTF8String:name] ?: @""];
    for (NSString *stem in stems) for (NSString *ext in @[@"mp4", @"mov", @"m4v"]) {
        NSString *p = [[dir stringByAppendingPathComponent:stem] stringByAppendingPathExtension:ext];
        if ([NSFileManager.defaultManager fileExistsAtPath:p]) return [NSURL fileURLWithPath:p];
    }
    return nil;
}
- (void)updateRideVideo {
    int type = -1; char name[64] = "";
    if (!tp_walk_ride(&type, name, sizeof name)) { [self stopRideVideo]; return; }
    if (_videoType == type) { if (_videoLayer) _videoLayer.frame = self.bounds; return; }
    [self stopRideVideo]; _videoType = type;
    NSURL *u = [self rideVideoFor:type name:name];
    if (!u) return;
    _video = [AVQueuePlayer new];
    _looper = [AVPlayerLooper playerLooperWithPlayer:_video templateItem:[AVPlayerItem playerItemWithURL:u]];
    _videoLayer = [AVPlayerLayer playerLayerWithPlayer:_video];
    _videoLayer.videoGravity = AVLayerVideoGravityResizeAspect; _videoLayer.backgroundColor = NSColor.blackColor.CGColor;
    _videoLayer.frame = self.bounds;
    [self.layer insertSublayer:_videoLayer below:_hud];
    [_video play];
    fprintf(stderr, "walk: ride video %s\n", u.path.UTF8String);
}
/* A 3DO ride video (imported by tools/extras.py): Cinepak frames from runtime/cinepak.c in full colour over the
 * park, at the stream's rate, with its sound; the game is paused until it ends or is clicked away. */
- (void)play3DO:(NSString *)stream sound:(NSString *)wav done:(dispatch_semaphore_t)done {
    if (_v3) [self end3DO];
    _v3 = tp_video_open(stream.fileSystemRepresentation);
    if (!_v3) { refresh_hold(); if (done) dispatch_semaphore_signal(done); return; }
    _v3done = done; _v3shown = 0; _v3t0 = CACurrentMediaTime();
    _v3Layer = [CALayer layer];
    _v3Layer.backgroundColor = NSColor.blackColor.CGColor;     /* (black around the picture, which is 4:3 at the full height: video3DOTick) */
    _v3Layer.actions = @{@"bounds": [NSNull null], @"position": [NSNull null]};
    _v3Pic = [CALayer layer]; _v3Pic.contentsGravity = kCAGravityResize; _v3Pic.magnificationFilter = kCAFilterLinear;
    _v3Pic.actions = @{@"contents": [NSNull null], @"bounds": [NSNull null], @"position": [NSNull null]};
    [_v3Layer addSublayer:_v3Pic];
    _v3Layer.frame = self.bounds; _v3Layer.zPosition = 50;
    [self.layer addSublayer:_v3Layer];
    if (wav && [NSFileManager.defaultManager fileExistsAtPath:wav]) tp_audio_video_play(wav.fileSystemRepresentation);
    fprintf(stderr, "ride video: %s\n", stream.lastPathComponent.UTF8String);
}
- (void)video3DOTick {
    if (!_v3) return;
    [CATransaction begin]; [CATransaction setDisableActions:YES];
    _v3Layer.frame = self.bounds;
    { CGSize b = self.bounds.size; CGFloat h = fmin(b.height, b.width * 3 / 4), w = h * 4 / 3;
      _v3Pic.frame = CGRectMake((b.width - w) / 2, (b.height - h) / 2, w, h); }
    [CATransaction commit];
    int due = (int)((CACurrentMediaTime() - _v3t0) * tp_video_fps(_v3));
    const uint8_t *px = NULL;
    while (_v3shown <= due) { px = tp_video_next(_v3); _v3shown++; if (!px) { [self end3DO]; return; } }
    if (!px) return;
    int w = tp_video_width(_v3), h = tp_video_height(_v3);
    CFDataRef data = CFDataCreate(NULL, px, (CFIndex)w * h * 3);
    CGDataProviderRef prov = CGDataProviderCreateWithCFData(data);
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGImageRef img = CGImageCreate(w, h, 8, 24, w * 3, cs, kCGImageAlphaNone, prov, NULL, NO, kCGRenderingIntentDefault);
    _v3Pic.contents = (__bridge id)img;
    CGImageRelease(img); CGColorSpaceRelease(cs); CGDataProviderRelease(prov); CFRelease(data);
}
- (void)end3DO {
    if (!_v3) return;
    tp_audio_video_stop();
    tp_video_close(_v3); _v3 = NULL;
    [_v3Layer removeFromSuperlayer]; _v3Layer = nil; _v3Pic = nil;
    if (_v3done) { dispatch_semaphore_signal(_v3done); _v3done = nil; }
    refresh_hold();                                     /* (on, unless the window is away) */
}
- (void)stopRideVideo {
    if (_video) { [_video pause]; [_videoLayer removeFromSuperlayer]; }
    _video = nil; _videoLayer = nil; _looper = nil; _videoType = -2;
}
@end

/* The ride panel's view button (qol.c's tp_ride_video, on the game thread). Ride Videos (Launcher > Display):
 * 0 the PC's (data/rideani.NNN) only; 1 the PC's, and the 3DO's for rides the PC has none for (Space Shuttle,
 * Tree House); 2 the 3DO's wherever there is one. */
static int ride_video_3do(int ride) {
    NSUserDefaults *u = NSUserDefaults.standardUserDefaults;
    long mode = getenv("TP_RIDE_VIDEOS") ? atol(getenv("TP_RIDE_VIDEOS")) : [u integerForKey:@"RideVideos"];
    if (!mode || !game_view) return 0;
    NSString *dir = [dataRoot() stringByAppendingPathComponent:@"extras/3do-usa"];
    NSDictionary *man = [NSJSONSerialization JSONObjectWithData:[NSData dataWithContentsOfFile:[dir stringByAppendingPathComponent:@"manifest.json"]] ?: [NSData data] options:0 error:nil];
    NSString *stem = [man isKindOfClass:NSDictionary.class] ? man[@"ride_videos"][[NSString stringWithFormat:@"%d", ride]] : nil;
    if (!stem) return 0;
    if (mode == 1) {                                     /* (the PC's own first: ride 16 borrows 9's, as the game does) */
        NSString *pc = [dataRoot() stringByAppendingPathComponent:[NSString stringWithFormat:@"game/DATA/RIDEANI.%03d", ride == 16 ? 9 : ride]];
        if ([NSFileManager.defaultManager fileExistsAtPath:pc]) return 0;
    }
    NSString *stream = [[dir stringByAppendingPathComponent:stem] stringByAppendingPathExtension:@"STREAM"];
    if (![NSFileManager.defaultManager fileExistsAtPath:stream]) return 0;
    NSString *wav = [[dir stringByAppendingPathComponent:stem] stringByAppendingPathExtension:@"wav"];
    /* The game waits while it plays, as for its own video: paused like a hidden window (tp_host_pause), which
     * resumes without replaying the stall (blocking here instead let the game race to catch up afterwards). */
    tp_host_pause(1);
    dispatch_async(dispatch_get_main_queue(), ^{ [game_view play3DO:stream sound:wav done:nil]; });
    return 1;
}

/* A window that can be key without a title bar (the borderless full screen). */
@interface GameWindow : NSWindow
@end
@implementation GameWindow
- (BOOL)canBecomeKeyWindow { return YES; }
- (BOOL)canBecomeMainWindow { return YES; }
@end

@interface AppDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate>
@end

@implementation AppDelegate {
    NSWindow *_window;
    GameView *_view;
    NSView *_container;                  /* black, holds the view (full screen: below the notch) */
    BOOL _full; NSRect _windowedFrame;   /* the borderless full screen, and the window's frame before it */
    NSTimer *_timer;
    id _link;                            /* CADisplayLink (macOS 14+) */
    double _lastDisplayTick;
    NSString *_game, *_saves, *_image, *_intro;
    FILE *_log;
}
static AppDelegate *app;
static volatile int game_done, game_result, game_started;
static volatile int main_running;                 /* MAIN.EXE is the image (not the intro): its music words are valid */

/* The launcher's settings into the environment, where the C modules read them (qol.c, freemode.c, bigmap.c,
 * view.c; a variable already set wins, for tests). On the main thread before the game thread starts: setenv is
 * not safe against getenv on another thread (issue #3). They were read once anyway (the first value stays). */
static void settings_to_env(AppDelegate *d) {
    if (!getenv("TP_NATIVE_PASS") && ![NSUserDefaults.standardUserDefaults boolForKey:@"NativePasses"])
        setenv("TP_NATIVE_PASS", "0", 1);            /* Launcher: Zoomed Drawing = The Game's */
    {   /* Launcher: Tweaks and Free Mode (qol.c reads them; the environment wins, for tests) */
        NSUserDefaults *u = NSUserDefaults.standardUserDefaults; char v[32];
        #define QOL_ENV(name, value) do { if (!getenv(name)) { snprintf(v, sizeof v, "%ld", (long)(value)); setenv(name, v, 1); } } while (0)
        QOL_ENV("TP_EW_GAP", [u integerForKey:@"GrossOutGap"]);
        QOL_ENV("TP_CASH_GAP", [u integerForKey:@"PurchaseGap"]);
        QOL_ENV("TP_GAME_SOUND_VOL", lround([u doubleForKey:@"GameSoundVolume"] * 100));   /* (qol.c: each effect by its kind) */
        QOL_ENV("TP_EW_VOL", lround([u doubleForKey:@"EwVolume"] * 100));
        QOL_ENV("TP_CASH_VOL", lround([u doubleForKey:@"PurchaseVolume"] * 100));
        QOL_ENV("TP_SYSTEM_SOUND_VOL", lround([u doubleForKey:@"SystemSoundVolume"] * 100));
        QOL_ENV("TP_RIDE_MUSIC", [u boolForKey:@"RideMusic"]);
        QOL_ENV("TP_PARK_TUNE", [u integerForKey:@"ParkTune"]);
        QOL_ENV("TP_FREE_VISITORS", [u integerForKey:@"FreeVisitors"]);
        QOL_ENV("TP_FREE_BUS", [u integerForKey:@"FreeBus"]);
        QOL_ENV("TP_FREE_RULES", [u integerForKey:@"FreeRules"]);   /* (freemode.c: 0 sandbox, 1 endless, 2 true freeplay) */
        QOL_ENV("TP_FREE_RAMP", [u integerForKey:@"FreeRamp"]);
        QOL_ENV("TP_BIG_MAPS", [u integerForKey:@"FreeMapSize"] == 128);   /* (bigmap.c: room for 128x128 sandbox parks) */
        {   /* a saved 128x128 park (its marker, <save>.big): room to load it whatever the setting */
            BOOL big = NO;
            for (NSString *f in [NSFileManager.defaultManager enumeratorAtPath:d->_saves]) if ([f hasSuffix:@".big"]) { big = YES; break; }
            QOL_ENV("TP_BIG_ROOM", big);
        }
        if (![u boolForKey:@"SteadyPacing"] && !getenv("TP_PACING")) setenv("TP_PACING", "dosbox", 1);
        #undef QOL_ENV
        NSDictionary *tunes = [u dictionaryForKey:@"RideTunes"];
        if (tunes.count && !getenv("TP_RIDE_TUNES")) {
            NSMutableArray *parts = [NSMutableArray array];
            for (NSString *k in tunes) [parts addObject:[NSString stringWithFormat:@"%@:%@", k, tunes[k]]];
            setenv("TP_RIDE_TUNES", [parts componentsJoinedByString:@","].UTF8String, 1);
        }
    }
}

static void *game_thread(void *arg) {
    AppDelegate *d = (__bridge AppDelegate *)arg;
    pthread_setname_np("Theme Park game");
    /* THEME.BAT runs INTRO.EXE, then MAIN.EXE. */
    if (d->_intro) {
        tp_host_set_image(d->_intro.fileSystemRepresentation);
        tp_select_code(intro_dispatch, intro_dispatch_init);
        tp_host_set_args("");
        if (!tp_host_init(d->_game.fileSystemRepresentation, d->_saves.fileSystemRepresentation, 0, NULL)) {
            int r; do r = tp_host_run(4000, 0, 50); while (r == 0);
            fprintf(stderr, "intro ended: %s\n", tp_host_status());
            if (!strcmp(tp_host_status(), "quit")) { game_result = 1; game_done = 1; return NULL; }
        }
        tp_select_code(tp_dispatch, tp_dispatch_init);
    }
    tp_host_set_image(d->_image.fileSystemRepresentation);
    {   /* the launcher's language: the game's own -l option (GOG's THEME.BAT passes -l0) */
        long lang = [NSUserDefaults.standardUserDefaults integerForKey:@"Language"];
        char args[16] = ""; if (lang > 0 && lang < 5) snprintf(args, sizeof args, " -l%ld", lang);
        tp_host_set_args(args);
    }
    for (;;) {
        main_running = 0;
        tp_view_enable(1);                               /* the zoomable park view knows MAIN.EXE's code */
        tp_view_request_hires([NSUserDefaults.standardUserDefaults boolForKey:@"StartHiRes"] || getenv("TP_START_HIRES"));
        if (tp_host_init(d->_game.fileSystemRepresentation, d->_saves.fileSystemRepresentation, 0, NULL)) {
            game_result = -1; break;
        }
        { extern int (*tp_ride_video)(int); tp_ride_video = ride_video_3do; }
        tp_extras_get = extras_get; tp_extras_set = extras_set; tp_extras_getf = extras_getf; tp_extras_setf = extras_setf;
        tp_qol_init();                                   /* quality-of-life hooks (qol.c), MAIN.EXE's code */
        if (tp_view_debug_fps) tp_view_count_game_frames(1);
        int r;
        main_running = 1;
        do r = tp_host_run(4000, 0, 50); while (r == 0);
        main_running = 0;
        game_result = r;
        /* THEME.BAT restarts the game when it exits with errorlevel >= 10. */
        if (r == 1 && tp_host_exit_code() >= 10) { tp_walk_stop(); continue; }   /* (Visitor View does not outlive its game, issue #6) */
        break;
    }
    game_done = 1;
    return NULL;
}

- (void)fail:(NSString *)message {
    NSAlert *a = [NSAlert new];
    a.messageText = @"Theme Park stopped";
    a.informativeText = message;
    [a runModal];
    [NSApp terminate:nil];
}

- (void)applicationDidFinishLaunching:(NSNotification *)n {
    app = self;
    NSString *root = dataRoot();
    _game = [root stringByAppendingPathComponent:@"game"];
    _saves = [root stringByAppendingPathComponent:@"saves"];
    _image = [NSBundle.mainBundle pathForResource:@"MAIN" ofType:@"TPIMG"];
    if (![NSFileManager.defaultManager fileExistsAtPath:[_game stringByAppendingPathComponent:@"DATA"]] || !_image) {
        [self fail:[NSString stringWithFormat:@"The game files were not found in %@. Run tools/build_app.sh with your Theme Park package to install them.", root]];
        return;
    }
    setenv("TP_IMAGE", _image.fileSystemRepresentation, 1);
    NSUserDefaults *u = NSUserDefaults.standardUserDefaults;
    [u registerDefaults:@{@"SoundVolume": @1.0, @"MusicVolume": @0.8, @"MusicType": @0, @"RideVideos": @1, @"Sound": @YES, @"Cycles": @15000, @"PlayIntro": @YES, @"Widescreen": @YES, @"NativePasses": @YES,
                          @"GrossOutGap": @0, @"PurchaseGap": @0, @"RideMusic": @YES, @"ParkTune": @0, @"FreeVisitors": @0, @"FreeBus": @0, @"FreeMapSize": @64, @"FreeRules": @0, @"FreeRamp": @60, @"SteadyPacing": @YES, @"PanelScale": @0.75,
                          @"GameSoundVolume": @1.0, @"SystemSoundVolume": @1.0, @"EwVolume": @1.0, @"PurchaseVolume": @1.0}];
    /* Builds before 2026-10-17 counted a REP string instruction as one instruction, where DOSBox
     * charges every repetition: their "Original" ran about 1.9 times GOG's speed. The player found it
     * about 20% too fast, so a saved Original becomes 1.5x once (the launcher offers exact Original). */
    if ([u integerForKey:@"SpeedModel"] < 2) {
        if ([u objectForKey:@"Cycles"] && [u doubleForKey:@"Cycles"] == 10000) [u setDouble:15000 forKey:@"Cycles"];
        [u setInteger:2 forKey:@"SpeedModel"];
    }
    BOOL selfTest = NSProcessInfo.processInfo.environment[@"TP_TEST"].length > 0 || NSProcessInfo.processInfo.environment[@"TP_CONTROL"].length > 0;
    if (NSProcessInfo.processInfo.environment[@"TP_LAUNCHER_SHOT"]) { tp_launcher_run(); [NSApp terminate:nil]; return; }
    if (!selfTest && !tp_launcher_run()) { [NSApp terminate:nil]; return; }
    if (!getenv("TP_CYCLES")) tp_host_set_cycles([u doubleForKey:@"Cycles"]);
    if ([u boolForKey:@"PlayIntro"] && !getenv("TP_SKIP_INTRO")) _intro = [NSBundle.mainBundle pathForResource:@"INTRO" ofType:@"TPIMG"];
    [NSFileManager.defaultManager createDirectoryAtPath:_saves withIntermediateDirectories:YES attributes:nil error:nil];
    NSString *logPath = [root stringByAppendingPathComponent:@"theme-park.log"];
    freopen(logPath.fileSystemRepresentation, "a", stderr);
    setvbuf(stderr, NULL, _IOLBF, 0);                  /* (a file: whole lines at once, so the log is current) */
    fprintf(stderr, "--- Theme Park started %s\n", NSDate.date.description.UTF8String);

    NSRect frame = NSMakeRect(0, 0, 960, 720);
    _window = [[GameWindow alloc] initWithContentRect:frame
        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
        backing:NSBackingStoreBuffered defer:NO];
    _window.title = @"Theme Park";
    _window.delegate = self;
    if (![u boolForKey:@"Widescreen"]) _window.contentAspectRatio = NSMakeSize(4, 3);   /* widescreen: any shape, the park fills it */
    _window.contentMinSize = NSMakeSize(320, 240);
    _window.collectionBehavior = NSWindowCollectionBehaviorFullScreenPrimary;
    _window.backgroundColor = NSColor.blackColor;
    _view = [[GameView alloc] initWithFrame:frame];
    _container = [[NSView alloc] initWithFrame:frame];
    _container.wantsLayer = YES; _container.layer.backgroundColor = NSColor.blackColor.CGColor;
    _view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [_container addSubview:_view];
    _window.contentView = _container;
    [_window center];
    [_window setFrameAutosaveName:@"ThemeParkWindow"];
    if (!selfTest) [_window makeKeyAndOrderFront:nil];     /* self-tests stay in the background */
    [_window makeFirstResponder:_view];
    _window.acceptsMouseMovedEvents = YES;
    if ([NSUserDefaults.standardUserDefaults boolForKey:@"StartFullScreen"]) [self toggleFull:nil];
    tp_view_debug_fps = [u boolForKey:@"DebugFPS"]; tp_view_debug_grid = [u boolForKey:@"DebugGrid"];
    tp_view_set_ui_scale([u doubleForKey:@"PanelScale"]);

    {
        if ([u boolForKey:@"Sound"] && tp_audio_start() == 0)
            tp_audio_set_volumes([u floatForKey:@"SoundVolume"], [u floatForKey:@"MusicVolume"]);
    }
    settings_to_env(self);
    pthread_t t;
    pthread_attr_t attr; pthread_attr_init(&attr);
    pthread_attr_set_qos_class_np(&attr, QOS_CLASS_USER_INTERACTIVE, 0);
    pthread_attr_setstacksize(&attr, 8 << 20);
    game_started = pthread_create(&t, &attr, game_thread, (__bridge void *)self) == 0;
    /* The display's refresh drives presentation (and tells the game thread its timing, for steady pacing). */
    if (@available(macOS 14.0, *)) {
        _link = [_view displayLinkWithTarget:self selector:@selector(displayTick:)];
        [_link addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
    }
    /* (and a timer for when the display link does not fire: the window hidden, offscreen, or macOS 13) */
    _timer = [NSTimer scheduledTimerWithTimeInterval:1.0 / 60 target:self selector:@selector(timerTick) userInfo:nil repeats:YES];
    [NSRunLoop.mainRunLoop addTimer:_timer forMode:NSRunLoopCommonModes];
    if (!selfTest) [NSApp activateIgnoringOtherApps:YES];
}

/* Bounded self-test (TP_TEST="t:key:1c0d,t:click:x:y,t:shot:/path.png,t:quit", t in seconds).
 * Clicks use the game's 640x400 mouse space. */
- (void)runTestScript {
    static double start; static int next;
    NSString *spec = NSProcessInfo.processInfo.environment[@"TP_TEST"];
    if (!spec.length) return;
    if (!start) start = CACurrentMediaTime();
    NSArray *steps = [spec componentsSeparatedByString:@","];
    while (next < (int)steps.count) {
        NSArray *p = [steps[next] componentsSeparatedByString:@":"];
        if (CACurrentMediaTime() - start < [p[0] doubleValue]) return;
        next++;
        NSString *op = p.count > 1 ? p[1] : @"";
        if ([op isEqual:@"key"]) {
            uint32_t k = (uint32_t)strtoul([p[2] UTF8String], NULL, 16);
            tp_host_key(k); tp_host_key_raw(k >> 8, 1); tp_host_key_raw(k >> 8, 0);
        }
        else if ([op isEqual:@"click"] || [op isEqual:@"move"]) {
            double x = [p[2] doubleValue] / 640.0, y = [p[3] doubleValue] / 400.0;
            tp_host_mouse(x, y, 0);
            if ([op isEqual:@"click"]) {        /* hold the button ~150 ms, like a real click */
                tp_host_mouse(x, y, 1);
                dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 150 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{ tp_host_mouse(x, y, 0); });
            }
        }
        else if ([op isEqual:@"shot"]) {
            char hex[65];
            tp_host_png([[p subarrayWithRange:NSMakeRange(2, p.count - 2)] componentsJoinedByString:@":"].fileSystemRepresentation, hex);
            fprintf(stderr, "shot %s blocks=%llu status=%s\n", hex, (unsigned long long)tp_host_blocks(), tp_host_status());
        }
        else if ([op isEqual:@"zoom"]) [_view zoomTo:[p[2] doubleValue] at:NSMakePoint(NAN, NAN)];
        else if ([op isEqual:@"size"]) [_window setContentSize:NSMakeSize([p[2] doubleValue], [p[3] doubleValue])];
        else if ([op isEqual:@"vclick"]) {      /* a click through the zoomed view, 640x400 screen space */
            double x = [p[2] doubleValue] / 640.0, y = [p[3] doubleValue] / 400.0;
            tp_view_mouse(x, y, 0); tp_view_mouse(x, y, 1);
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 150 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{ tp_view_mouse(x, y, 0); });
        }
        else if ([op isEqual:@"vshot"]) {       /* what the window shows (zoomed or not), 960 high (1280 at 4:3, wider in the wide view) */
            static uint32_t frame[640 * 480]; int fw = 320, fh = 200;
            tp_host_frame_size(&fw, &fh); tp_host_frame_rgba_any(frame, 640 * 480);
            NSRect vb = _view.bounds; int oh = 960, ow = 1280;
            if (tp_view_live() && vb.size.width * 3 > vb.size.height * 4 * 1.02) ow = (int)lround(oh * vb.size.width / vb.size.height);
            uint32_t *big = [_view shownPicture:&ow :&oh];
            if (big) {}                                  /* (a game window in the wide view: what is shown) */
            else if ((big = malloc(4 * (size_t)ow * oh)) && tp_view_live()) tp_view_compose(big, ow, oh, frame, fw, fh, 0);
            else for (int i = 0; i < ow * oh; i++) big[i] = frame[(i / ow * fh / oh) * fw + (i % ow) * fw / ow];
            NSString *path = [[p subarrayWithRange:NSMakeRange(2, p.count - 2)] componentsJoinedByString:@":"];
            int tp_host_png_rgba(const char *, const uint32_t *, int, int);
            tp_host_png_rgba(path.fileSystemRepresentation, big, ow, oh); free(big);
            fprintf(stderr, "vshot live=%d\n", tp_view_live());
        }
        else if ([op isEqual:@"picture"]) [self savePicture:nil];
        else if ([op isEqual:@"quit"]) [NSApp terminate:nil];
    }
}

/* A game controller: left stick moves the pointer, A and B click (left, right), the right stick
 * moves the park view, the shoulder buttons zoom, Menu is Escape. */
- (void)pollController {
    GCExtendedGamepad *g = GCController.controllers.firstObject.extendedGamepad;
    if (!g) return;
    if (tp_walk_active()) {                             /* Visitor View: left stick walks, right stick looks, A follow, X hop, Y ride, Menu leaves */
        static BOOL a0, x0, y0, m0;
        float lx = g.leftThumbstick.xAxis.value, ly = g.leftThumbstick.yAxis.value, rx = g.rightThumbstick.xAxis.value, ry = g.rightThumbstick.yAxis.value;
        [_view walkPad:fabsf(ly) > 0.15 ? ly : 0 :fabsf(lx) > 0.15 ? lx : 0 :fabsf(rx) > 0.15 ? rx : 0 :fabsf(ry) > 0.15 ? -ry : 0];
        if (g.buttonA.pressed && !a0) tp_walk_command(TP_WALK_FOLLOW);
        if (g.buttonX.pressed && !x0) tp_walk_command(TP_WALK_HOP);
        if (g.buttonY.pressed && !y0) tp_walk_command(TP_WALK_RIDE);
        if (g.buttonMenu.pressed && !m0) [_view walkStop];
        a0 = g.buttonA.pressed; x0 = g.buttonX.pressed; y0 = g.buttonY.pressed; m0 = g.buttonMenu.pressed;
        return;
    }
    [_view walkPad:0 :0 :0 :0];
    static double px = 0.5, py = 0.4; static BOOL a, b, ls, rs, menu;
    float lx = g.leftThumbstick.xAxis.value, ly = g.leftThumbstick.yAxis.value;
    BOOL moved = NO;
    if (fabsf(lx) > 0.15 || fabsf(ly) > 0.15) {
        px = fmin(fmax(px + lx * 0.010, 0), 0.999); py = fmin(fmax(py - ly * 0.013, 0), 0.999); moved = YES;
    }
    BOOL na = g.buttonA.pressed, nb = g.buttonB.pressed;
    if (moved || na != a || nb != b) [_view pointerAt:px :py buttons:(na ? 1u : 0u) | (nb ? 2u : 0u)];
    a = na; b = nb;
    float rx = g.rightThumbstick.xAxis.value, ry = g.rightThumbstick.yAxis.value;
    if (fabsf(rx) > 0.15 || fabsf(ry) > 0.15) [_view panBy:rx * 14 :-ry * 14];
    if (g.leftShoulder.pressed && !ls) [_view zoomOut:nil];
    if (g.rightShoulder.pressed && !rs) [_view zoomIn:nil];
    ls = g.leftShoulder.pressed; rs = g.rightShoulder.pressed;
    BOOL nm = g.buttonMenu.pressed;
    if (nm && !menu) { tp_host_key(0x011b); tp_host_key_raw(1, 1); tp_host_key_raw(1, 0); }
    menu = nm;
}

/* Development: live control for playtests (TP_CONTROL=<file>). Lines appended to the file run in
 * order, one batch per tick: key HEX | type TEXT | mouse X Y B | click X Y [B] | vmouse X Y B |
 * vclick X Y [B] | raw SCAN 1|0 | zoom Z | center CX CY | pan DX DY | size W H | cycles N | shot PATH | vshot PATH |
 * sleep S | ack TAG | show (the window, which control runs keep hidden) | quit. mouse/click are in the game frame's pixels (320x200 or 640x480);
 * vmouse/vclick in a vshot's pixels (what the window shows, 960 rows high). ack appends TAG to
 * <file>.ack once everything before it has run. */
static int vshot_size(int *ow, int *oh, NSView *v) {
    *oh = 960; *ow = 1280;
    NSRect vb = v.bounds;
    if ((tp_view_live() || [(GameView *)v menuWide]) && vb.size.width * 3 > vb.size.height * 4 * 1.02) *ow = (int)lround(*oh * vb.size.width / vb.size.height);
    return 1;
}
- (void)vshot:(NSString *)path {
    static uint32_t frame[640 * 480]; int fw = 320, fh = 200;
    tp_host_frame_size(&fw, &fh); tp_host_frame_rgba_any(frame, 640 * 480);
    int ow, oh; vshot_size(&ow, &oh, _view);
    uint32_t *big = [_view shownPicture:&ow :&oh];       /* (a game window in the wide view: what is shown) */
    if (big) {}
    else if ((big = malloc(4 * (size_t)ow * oh)) && tp_view_live()) tp_view_compose(big, ow, oh, frame, fw, fh, 0);
    else for (int i = 0; i < ow * oh; i++) big[i] = frame[(i / ow * fh / oh) * fw + (i % ow) * fw / ow];
    int tp_host_png_rgba(const char *, const uint32_t *, int, int);
    tp_host_png_rgba(path.fileSystemRepresentation, big, ow, oh); free(big);
    fprintf(stderr, "vshot %s %dx%d live=%d\n", path.UTF8String, ow, oh, tp_view_live());
}
static uint32_t key_of_char(unichar c) {
    static const char *row1 = "1234567890", *q = "QWERTYUIOP", *a = "ASDFGHJKL", *z = "ZXCVBNM";
    c = (unichar)toupper(c);
    for (int i = 0; row1[i]; i++) if (row1[i] == c) return (uint32_t)(0x02 + i) << 8 | c;
    for (int i = 0; q[i]; i++) if (q[i] == c) return (uint32_t)(0x10 + i) << 8 | c;
    for (int i = 0; a[i]; i++) if (a[i] == c) return (uint32_t)(0x1e + i) << 8 | c;
    for (int i = 0; z[i]; i++) if (z[i] == c) return (uint32_t)(0x2c + i) << 8 | c;
    if (c == ' ') return 0x3920;
    return 0;
}
- (void)runControl {
    static NSString *path; static unsigned long long off; static NSMutableArray *queue; static double until;
    if (!path) { path = NSProcessInfo.processInfo.environment[@"TP_CONTROL"]; if (!path.length) return; queue = [NSMutableArray array]; }
    NSFileHandle *h = [NSFileHandle fileHandleForReadingAtPath:path];
    if (h) {
        [h seekToFileOffset:off]; NSData *d = [h readDataToEndOfFile]; [h closeFile];
        const char *b = d.bytes; NSUInteger end = d.length; while (end && b[end - 1] != '\n') end--;   /* whole lines only */
        if (end) {
            NSString *txt = [[NSString alloc] initWithBytes:b length:end encoding:NSUTF8StringEncoding]; off += end;
            for (NSString *l in [txt componentsSeparatedByString:@"\n"]) if (l.length) [queue addObject:l];
        }
    }
    double t = CACurrentMediaTime();
    while (queue.count && t >= until) {
        NSString *line = queue[0]; [queue removeObjectAtIndex:0];
        NSArray *p = [line componentsSeparatedByString:@" "]; NSString *op = p[0];
        double a1 = p.count > 1 ? [p[1] doubleValue] : 0, a2 = p.count > 2 ? [p[2] doubleValue] : 0;
        uint32_t b = p.count > 3 ? (uint32_t)[p[3] intValue] : 1;
        int fw = 320, fh = 200; tp_host_frame_size(&fw, &fh);
        int ow, oh; vshot_size(&ow, &oh, _view);
        if ([op isEqual:@"key"]) { uint32_t k = (uint32_t)strtoul([p[1] UTF8String], NULL, 16); tp_host_key(k); tp_host_key_raw(k >> 8, 1); tp_host_key_raw(k >> 8, 0); }
        else if ([op isEqual:@"shift"]) tp_host_shift((uint32_t)strtoul([p[1] UTF8String], NULL, 16));   /* BIOS shift flags */
        else if ([op isEqual:@"raw"]) tp_host_key_raw((uint8_t)strtoul([p[1] UTF8String], NULL, 16), (int)a2);   /* raw SCAN 1|0 */
        else if ([op isEqual:@"type"]) {
            NSString *s = [line substringFromIndex:5];
            for (NSUInteger i = 0; i < s.length; i++) { uint32_t k = key_of_char([s characterAtIndex:i]); if (k) { tp_host_key(k); tp_host_key_raw(k >> 8, 1); tp_host_key_raw(k >> 8, 0); } }
        }
        else if ([op isEqual:@"mouse"]) tp_host_mouse(a1 / fw, a2 / fh, p.count > 3 ? b : 0);
        else if ([op isEqual:@"vmouse"]) [_view pointerAt:a1 / ow :a2 / oh buttons:p.count > 3 ? b : 0];
        else if ([op isEqual:@"click"] || [op isEqual:@"vclick"]) {   /* press, hold 150 ms, release */
            BOOL v = [op isEqual:@"vclick"];
            NSString *m = v ? @"vmouse" : @"mouse";
            if (v) [_view pointerAt:a1 / ow :a2 / oh buttons:0]; else tp_host_mouse(a1 / fw, a2 / fh, 0);
            [queue insertObject:[NSString stringWithFormat:@"%@ %g %g %u", m, a1, a2, b] atIndex:0];
            [queue insertObject:@"sleep 0.15" atIndex:1];
            [queue insertObject:[NSString stringWithFormat:@"%@ %g %g 0", m, a1, a2] atIndex:2];
            [queue insertObject:@"sleep 0.1" atIndex:3];
            until = t + 0.05; continue;
        }
        else if ([op isEqual:@"zoom"]) [_view zoomTo:a1 at:NSMakePoint(NAN, NAN)];
        else if ([op isEqual:@"center"]) [_view centerOn:a1 :a2];
        else if ([op isEqual:@"pan"]) [_view panBy:a1 :a2];
        else if ([op isEqual:@"size"]) [_window setContentSize:NSMakeSize(a1, a2)];
        else if ([op isEqual:@"cycles"]) tp_host_set_cycles(a1);
        else if ([op isEqual:@"shot"]) { char hex[65]; tp_host_png([line substringFromIndex:5].fileSystemRepresentation, hex); fprintf(stderr, "shot %s\n", [line substringFromIndex:5].UTF8String); }
        else if ([op isEqual:@"vshot"]) [self vshot:[line substringFromIndex:6]];
        else if ([op isEqual:@"sleep"]) { until = t + a1; break; }
        else if ([op isEqual:@"ack"]) {
            FILE *f = fopen([path stringByAppendingString:@".ack"].fileSystemRepresentation, "a");
            if (f) { fprintf(f, "%s\n", p.count > 1 ? [p[1] UTF8String] : ""); fclose(f); }
        }
        else if ([op isEqual:@"peek"]) {                 /* peek ADDR(hex) LEN: guest memory to the log */
            uint8_t *m = tp_host_memory(); uint32_t a = (uint32_t)strtoul([p[1] UTF8String], NULL, 16); int len = p.count > 2 ? [p[2] intValue] : 4;
            fprintf(stderr, "peek %06x:", a); for (int i = 0; i < len && i < 64; i++) fprintf(stderr, " %02x", m[a + i]); fprintf(stderr, "\n");
        }
        else if ([op isEqual:@"coverage"] && p.count > 1) {   /* coverage PATH: the block coverage so far, added into PATH (TP_COVERAGE=1) */
            extern void tp_coverage_dump(const char *); tp_coverage_dump([p[1] fileSystemRepresentation]);
        }
        else if ([op isEqual:@"dump"] && p.count > 3) {  /* dump ADDR(hex) LEN PATH: guest memory to a file (survey diffs) */
            uint8_t *m = tp_host_memory(); uint32_t a = (uint32_t)strtoul([p[1] UTF8String], NULL, 16); uint32_t len = (uint32_t)strtoul([p[2] UTF8String], NULL, 0);
            FILE *f = len <= 0x400000 ? fopen([p[3] fileSystemRepresentation], "wb") : NULL; if (f) { fwrite(m + a, 1, len, f); fclose(f); }
        }
        else if ([op isEqual:@"full"]) [self toggleFull:nil];
        else if ([op isEqual:@"fps"]) [self toggleFPS:nil];
        else if ([op isEqual:@"poke16"] && p.count > 2) {  /* (tests: a guest word, e.g. the camera 0x1d667a / 0x1d6666) */
            uint8_t *tp_host_memory(void); uint16_t v = (uint16_t)strtoul([p[2] UTF8String], NULL, 0);
            memcpy(tp_host_memory() + (uint32_t)strtoul([p[1] UTF8String], NULL, 0), &v, 2);
        }
        else if ([op isEqual:@"unlockall"]) {           /* (tests: every ride and shop researched: the runtime tables' flags) */
            uint8_t *tp_host_memory(void); uint8_t *m = tp_host_memory();
            for (int k = 0; k < 29; k++) m[0x1cced0 + k * 0xc8 + 0xc5] = 1;
            for (int k = 0; k < 17; k++) m[0x1d3c62 + 0x22 + k * 0x6b + 0x44] = 1;
        }
        else if ([op isEqual:@"poke32"] && p.count > 2) {   /* (tests: a guest dword, e.g. the player's money with "money") */
            uint8_t *tp_host_memory(void); uint8_t *m = tp_host_memory(); uint32_t a = (uint32_t)strtoul([p[1] UTF8String], NULL, 0), v = (uint32_t)strtoul([p[2] UTF8String], NULL, 0);
            if ([p[1] isEqual:@"money"]) { uint32_t pl; memcpy(&pl, m + 0x1d3b00, 4); a = pl + 0x0a; }   /* (the player's money: *COMPUTER block +0x0a) */
            memcpy(m + a, &v, 4);
        }
        else if ([op isEqual:@"mousestate"]) { void tp_host_mouse_debug(char *, int); char st[256]; tp_host_mouse_debug(st, sizeof st); fprintf(stderr, "mousestate: %s\n", st); }
        else if ([op isEqual:@"camto"]) {                /* camto X Y: the game's camera to map point X,Y; where it is on its screen */
            int tp_view_camera_to(double, double, double *, double *), tp_view_world_to_canvas(double, double, double *, double *);
            void tp_view_canvas_to_frac(double, double, double *, double *); double sx = 0, sy = 0, px, py;
            if (tp_view_live() && tp_view_world_to_canvas(a1, a2, &px, &py)) {   /* the park view: centred there; its vshot pixels */
                [_view centerOn:px :py]; tp_view_canvas_to_frac(px, py, &sx, &sy);
                fprintf(stderr, "camto %g %g: view at %.1f %.1f\n", a1, a2, sx * ow, sy * oh);
            } else {
                int ok = tp_view_camera_to(a1, a2, &sx, &sy);
                fprintf(stderr, "camto %g %g: %d at %.1f %.1f\n", a1, a2, ok, sx, sy);
            }
        }
        else if ([op isEqual:@"panel"]) tp_view_set_ui_scale(a1);
        else if ([op isEqual:@"grid"]) [self toggleGrid:nil];
        else if ([op isEqual:@"walk"]) {                 /* walk X Y: the Visitor View at map point X,Y (X < 0: the middle of the game's view) */
            double x = a1, y = a2;
            if (x < 0 && !tp_view_point_to_world(0.5, 0.45, &x, &y)) x = y = 32;
            fprintf(stderr, "control: walk %.2f,%.2f: %d\n", x, y, [_view walkStartAt:x :y]);
        }
        else if ([op isEqual:@"walkcmd"]) tp_walk_command((int)a1);   /* 1 follow, 2 hop, 3 ride along, 4 view, 5 noclip */
        else if ([op isEqual:@"walkhold"]) [_view walkHold:a1 :a2 :p.count > 3 ? [p[3] doubleValue] : 0 :p.count > 4 ? [p[4] doubleValue] : 1];   /* walkhold FWD STRAFE TURN SECONDS */
        else if ([op isEqual:@"walkteleport"]) tp_walk_teleport(a1, a2, p.count > 3 ? [p[3] doubleValue] * M_PI / 180 : NAN);   /* X Y YAW(degrees) */
        else if ([op isEqual:@"walkstop"]) [_view walkStop];
        else if ([op isEqual:@"walkshot"]) {             /* walkshot PATH: the Visitor View at 960x600 */
            uint32_t *img = malloc(4 * 960 * 600); int ok = tp_walk_render(img, 960, 600);
            int tp_host_png_rgba(const char *, const uint32_t *, int, int); if (ok) tp_host_png_rgba([line substringFromIndex:9].fileSystemRepresentation, img, 960, 600);
            char st[256]; tp_walk_status(st, sizeof st); free(img);
            fprintf(stderr, "walkshot %s drawn=%d | %s\n", [line substringFromIndex:9].UTF8String, ok, st);
        }
        else if ([op isEqual:@"show"]) { [_window makeKeyAndOrderFront:nil]; [NSApp activateIgnoringOtherApps:YES]; }   /* a run to watch */
        else if ([op isEqual:@"quit"]) [NSApp terminate:nil];
        else fprintf(stderr, "control: unknown %s\n", line.UTF8String);
    }
}

- (void)displayTick:(id)link API_AVAILABLE(macos(14.0)) {
    CADisplayLink *l = link; double period = l.targetTimestamp - l.timestamp;
    if (period > 0.002 && period < 0.05) tp_view_vsync_now(period, CACurrentMediaTime() - l.timestamp);   /* (the refresh's own time, not the callback's) */
    _lastDisplayTick = CACurrentMediaTime();
    [self tick];
}
- (void)timerTick { if (CACurrentMediaTime() - _lastDisplayTick > 0.05) [self tick]; }
/* Music Type (Launcher > Sound): 0 the game's General MIDI; 1 the Amiga CD32's CD-audio tracks; 2 the 3DO's
 * recordings of the PC tunes (both imported from the owner's discs by tools/extras.py into extras/). The game
 * runs its music as always; this follows its current tune (word 0x1ba71a: 1-17 ride tunes, 18 heartbeat, 19
 * fanfare, 20 takeover, 21 win, 22 funeral) and plays the source's recording of it: the Amiga game's own tune ->
 * track table, or the 3DO's RIDEn per tune. Ride tunes and the heartbeat loop; jingles play out even after the
 * game's shorter MIDI version has ended; a tune the source lacks plays the game's MIDI. */
- (void)musicTick {
    static int mode = -1, last = -1; static NSMutableDictionary<NSNumber *, NSString *> *files; static double next_check;
    double t = CACurrentMediaTime();
    if (t >= next_check) {                               /* (the setting can change in the launcher) */
        next_check = t + 1;
        int m = (int)[NSUserDefaults.standardUserDefaults integerForKey:@"MusicType"];
        if (getenv("TP_MUSIC_TYPE")) m = atoi(getenv("TP_MUSIC_TYPE"));
        if (m != mode) {
            mode = m; last = -1; tp_audio_cd_stop(); files = [NSMutableDictionary new];
            NSString *dir = [dataRoot() stringByAppendingPathComponent:m == 1 ? @"extras/amiga-cd32" : @"extras/3do-usa"];
            NSDictionary *man = m ? [NSJSONSerialization JSONObjectWithData:[NSData dataWithContentsOfFile:[dir stringByAppendingPathComponent:@"manifest.json"]] ?: [NSData data] options:0 error:nil] : nil;
            if ([man isKindOfClass:NSDictionary.class] && m == 1) {
                NSArray *tt = man[@"tune_tracks"];
                for (NSUInteger tune = 1; tune < tt.count; tune++) if ([tt[tune] intValue])
                    files[@(tune)] = [dir stringByAppendingPathComponent:[NSString stringWithFormat:@"track%02d.wav", [tt[tune] intValue]]];
            } else if ([man isKindOfClass:NSDictionary.class] && m == 2) {
                NSDictionary *tt = man[@"tunes"];
                for (NSString *k in tt) files[@(k.intValue)] = [dir stringByAppendingPathComponent:tt[k]];
            }
            if (m) fprintf(stderr, "music: %s %s\n", m == 1 ? "Amiga CD32" : "3DO", files.count ? "recordings found" : "not imported (tools/extras.py): the game's MIDI plays");
        }
    }
    if (!mode || !files.count || !main_running) return;
    uint8_t *tp_host_memory(void); uint8_t *mem = tp_host_memory();
    if (!mem) return;
    int16_t tune; memcpy(&tune, mem + 0x1ba71a, 2);
    if (tune == last) return;
    int was = last; last = tune;
    NSString *f = tune > 0 ? files[@(tune)] : nil;
    if (f && [NSFileManager.defaultManager fileExistsAtPath:f]) {
        tp_audio_cd_play(f.fileSystemRepresentation, tune <= 18);
        if (getenv("TP_SOUND_LOG")) fprintf(stderr, "music: tune %d -> %s\n", tune, f.lastPathComponent.UTF8String);
    } else if (!(tune == 0 && was >= 19 && was <= 22)) {
        tp_audio_cd_stop();                              /* (none, or a tune without a recording: the game's MIDI) */
    }
}
- (void)tick {
    [self runControl];
    [self musicTick];
    { void tp_audio_report(void); tp_audio_report(); }
    [_view video3DOTick];
    [self runTestScript];
    [self pollController];
    [_view present];
    if (game_done) {
        [_timer invalidate]; if (@available(macOS 14.0, *)) [(CADisplayLink *)_link invalidate];
        _timer = nil;
        fprintf(stderr, "game ended: %s\n", tp_host_status());
        fflush(stderr);
        if (game_result < 0 && strcmp(tp_host_status(), "quit"))
            [self fail:[NSString stringWithFormat:@"%s\n\nDetails are in ~/Games/ThemeParkNative/theme-park.log.", tp_host_status()]];
        else
            [NSApp terminate:nil];
    }
}

/* Hidden or minimised, the park waits (and is silent) until it is back; under a 3DO ride video it waits too (the
 * video's sound plays). Worked out from all three each time: one event alone resumed a game still minimised, or
 * under a video. */
- (void)refreshHold {
    BOOL away = NSApp.isHidden || _window.miniaturized;
    tp_host_pause(away || [_view playing3DO]);
    NSUserDefaults *u = NSUserDefaults.standardUserDefaults;
    if (away) tp_audio_set_volumes(0, 0);
    else tp_audio_set_volumes([u floatForKey:@"SoundVolume"], [u floatForKey:@"MusicVolume"]);
}
/* The Dock icon brings the window back (a test run's hidden window too). */
- (BOOL)applicationShouldHandleReopen:(NSApplication *)a hasVisibleWindows:(BOOL)visible {
    if (_window && !_window.isVisible) [_window makeKeyAndOrderFront:nil];
    return YES;
}
- (void)applicationDidHide:(NSNotification *)n { [self refreshHold]; }
- (void)applicationDidUnhide:(NSNotification *)n { [self refreshHold]; }
- (void)windowDidMiniaturize:(NSNotification *)n { [self refreshHold]; }
- (void)windowDidDeminiaturize:(NSNotification *)n { [self refreshHold]; }
- (void)windowDidResignKey:(NSNotification *)n { [_view releaseInput]; }
- (void)applicationDidResignActive:(NSNotification *)n { [_view releaseInput]; }
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)a { return YES; }
- (void)applicationWillTerminate:(NSNotification *)n {
    tp_host_quit();
    if (game_started) for (int i = 0; i < 100 && !game_done; i++) usleep(10000);   /* (the game thread out first: it sends the synth MIDI) */
    tp_audio_stop();
    fflush(stderr);
}
/* Full screen: the window borderless over the whole display, the Dock and menu bar hidden (not macOS's full-
 * screen space, whose menu bar and title bar slide down when the pointer reaches the top: the game opens its
 * own top bar there). The picture stays below a camera notch. ⌃⌘F switches back. */
- (void)toggleFull:(id)sender {
    NSScreen *sc = _window.screen ?: NSScreen.mainScreen;
    if (!_full) {
        _windowedFrame = _window.frame;
        [_window setFrameAutosaveName:@""];              /* (the screen-sized frame is not the window's to remember) */
        [NSApp setPresentationOptions:NSApplicationPresentationHideDock | NSApplicationPresentationHideMenuBar];
        _window.styleMask = NSWindowStyleMaskBorderless;
        _window.contentAspectRatio = NSZeroSize; _window.resizeIncrements = NSMakeSize(1, 1);
        [_window setFrame:sc.frame display:YES];
        CGFloat top = 0;
        if (@available(macOS 12.0, *)) top = sc.safeAreaInsets.top;
        _view.frame = NSMakeRect(0, 0, _container.bounds.size.width, _container.bounds.size.height - top);
        _full = YES;
    } else {
        [NSApp setPresentationOptions:NSApplicationPresentationDefault];
        _window.styleMask = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
        _window.title = @"Theme Park";
        if (![NSUserDefaults.standardUserDefaults boolForKey:@"Widescreen"]) _window.contentAspectRatio = NSMakeSize(4, 3);
        [_window setFrameAutosaveName:@"ThemeParkWindow"];
        [_window setFrame:_windowedFrame display:YES];
        _view.frame = _container.bounds;
        _full = NO;
    }
    [_view setCornerGuard:_full];
    [_window makeKeyAndOrderFront:nil]; [_window makeFirstResponder:_view];
    fprintf(stderr, "full screen %d (%.0fx%.0f)\n", _full, _view.frame.size.width, _view.frame.size.height);
}
- (BOOL)validateMenuItem:(NSMenuItem *)item {
    if (item.action == @selector(toggleFull:)) item.title = _full ? @"Exit Full Screen" : @"Enter Full Screen";
    if (item.action == @selector(toggleFPS:)) item.state = tp_view_debug_fps ? NSControlStateValueOn : NSControlStateValueOff;
    if (item.action == @selector(toggleGrid:)) item.state = tp_view_debug_grid ? NSControlStateValueOn : NSControlStateValueOff;
    return YES;
}
/* Debug overlays (View menu, remembered): frame rates, and the grid of the sections the park view draws. */
- (void)toggleFPS:(id)sender {
    tp_view_debug_fps = !tp_view_debug_fps; tp_view_count_game_frames(tp_view_debug_fps);
    [NSUserDefaults.standardUserDefaults setBool:tp_view_debug_fps forKey:@"DebugFPS"];
}
/* View > Larger / Smaller Panel (remembered; the launcher's Display > Panel Size): the panel and the advisor. */
- (void)panelLarger:(id)sender { [self panelStep:1]; }
- (void)panelSmaller:(id)sender { [self panelStep:-1]; }
- (void)panelStep:(int)d {
    static const double steps[] = {0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0};
    double k = [NSUserDefaults.standardUserDefaults doubleForKey:@"PanelScale"]; int i = 0;
    for (int j = 0; j < 7; j++) if (fabs(steps[j] - k) < fabs(steps[i] - k)) i = j;
    i += d; if (i < 0) i = 0; if (i > 6) i = 6;
    [NSUserDefaults.standardUserDefaults setDouble:steps[i] forKey:@"PanelScale"];
    tp_view_set_ui_scale(steps[i]);
}
- (void)toggleGrid:(id)sender {
    tp_view_debug_grid = !tp_view_debug_grid;
    [NSUserDefaults.standardUserDefaults setBool:tp_view_debug_grid forKey:@"DebugGrid"];
}
/* File > Save Park Picture: the whole park as one PNG (the view renders it on the game's thread). */
- (void)savePicture:(id)sender {
    tp_view_request_poster();
    double t0 = CACurrentMediaTime(); uint32_t *px = NULL; int w = 0, h = 0;
    while (!tp_view_take_poster(&px, &w, &h) && CACurrentMediaTime() - t0 < 3) [NSThread sleepForTimeInterval:0.02];
    if (!px) {
        NSAlert *a = [NSAlert new]; a.messageText = @"No park on screen";
        a.informativeText = @"Open your park in the game, then save its picture."; [a runModal]; return;
    }
    NSString *path = NSProcessInfo.processInfo.environment[@"TP_PICTURE"];     /* self-test */
    if (!path) {
        NSSavePanel *sp = [NSSavePanel savePanel];
        sp.allowedContentTypes = @[UTTypePNG]; sp.nameFieldStringValue = @"Theme Park.png";
        if ([sp runModal] != NSModalResponseOK) { free(px); return; }
        path = sp.URL.path;
    }
    CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef ctx = CGBitmapContextCreate(px, w, h, 8, w * 4, cs, kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    double ti0 = CACurrentMediaTime();
    CGImageRef img = CGBitmapContextCreateImage(ctx);
    CGImageDestinationRef d = CGImageDestinationCreateWithURL((__bridge CFURLRef)[NSURL fileURLWithPath:path], (__bridge CFStringRef)UTTypePNG.identifier, 1, NULL);
    if (d) { CGImageDestinationAddImage(d, img, NULL); CGImageDestinationFinalize(d); CFRelease(d); }
    CGImageRelease(img); CGContextRelease(ctx); CGColorSpaceRelease(cs); free(px);
    fprintf(stderr, "park picture %dx%d -> %s\n", w, h, path.fileSystemRepresentation);
}
@end

/* The game's code (27 MB of it) read into memory at the start, in the background: otherwise each part runs from the
 * disk the first time it is needed (the first storm, building in a new corner), and on a cold disk cache the game
 * thread waited on the reads, long enough for the sound to drop. Every page touched once, at low priority. */
static void prefault_code(void) {
    unsigned long size = 0;
    uint8_t *p = getsegmentdata((const struct mach_header_64 *)_dyld_get_image_header(0), "__TEXT", &size);
    if (!p || !size) return;
    madvise(p, size, MADV_WILLNEED);
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
        volatile uint8_t sum = 0; long page = getpagesize();
        for (unsigned long i = 0; i < size; i += (unsigned long)page) sum += p[i];
        (void)sum;
    });
}
int main(int argc, const char **argv) {
    @autoreleasepool {
        prefault_code();
        NSApplication *a = NSApplication.sharedApplication;
        a.activationPolicy = NSApplicationActivationPolicyRegular;
        NSMenu *bar = [NSMenu new];
        NSMenuItem *appItem = [NSMenuItem new]; [bar addItem:appItem];
        NSMenu *appMenu = [NSMenu new];
        NSString *name = NSBundle.mainBundle.infoDictionary[@"CFBundleName"] ?: @"Theme Park";
        [appMenu addItemWithTitle:[@"Hide " stringByAppendingString:name] action:@selector(hide:) keyEquivalent:@"h"];
        [appMenu addItem:NSMenuItem.separatorItem];
        [appMenu addItemWithTitle:[@"Quit " stringByAppendingString:name] action:@selector(terminate:) keyEquivalent:@"q"];
        appItem.submenu = appMenu;
        NSMenuItem *fileItem = [NSMenuItem new]; [bar addItem:fileItem];
        NSMenu *fileMenu = [[NSMenu alloc] initWithTitle:@"File"];
        [fileMenu addItemWithTitle:@"Save Park Picture…" action:@selector(savePicture:) keyEquivalent:@"e"];
        fileItem.submenu = fileMenu;
        NSMenuItem *viewItem = [NSMenuItem new]; [bar addItem:viewItem];
        NSMenu *viewMenu = [[NSMenu alloc] initWithTitle:@"View"];
        NSMenuItem *fs = [viewMenu addItemWithTitle:@"Enter Full Screen" action:@selector(toggleFull:) keyEquivalent:@"f"];
        fs.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagControl;
        [viewMenu addItem:NSMenuItem.separatorItem];
        [viewMenu addItemWithTitle:@"Zoom In" action:@selector(zoomIn:) keyEquivalent:@"="];
        [viewMenu addItemWithTitle:@"Zoom Out" action:@selector(zoomOut:) keyEquivalent:@"-"];
        [viewMenu addItemWithTitle:@"Actual Size" action:@selector(zoomActual:) keyEquivalent:@"0"];
        [viewMenu addItem:NSMenuItem.separatorItem];
        [viewMenu addItemWithTitle:@"Larger Panel" action:@selector(panelLarger:) keyEquivalent:@"]"];
        [viewMenu addItemWithTitle:@"Smaller Panel" action:@selector(panelSmaller:) keyEquivalent:@"["];
        [viewMenu addItem:NSMenuItem.separatorItem];
        NSMenuItem *dbg = [viewMenu addItemWithTitle:@"Show Frame Rate" action:@selector(toggleFPS:) keyEquivalent:@"f"];
        dbg.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagOption;
        dbg = [viewMenu addItemWithTitle:@"Show Section Grid" action:@selector(toggleGrid:) keyEquivalent:@"g"];
        dbg.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagOption;
        [viewMenu addItemWithTitle:@"Visitor View" action:@selector(visitorView:) keyEquivalent:@"V"];   /* ⇧⌘V (experimental) */
        viewItem.submenu = viewMenu;
        a.mainMenu = bar;
        AppDelegate *d = [AppDelegate new];
        a.delegate = d;
        [a run];
    }
    return 0;
}

static void refresh_hold(void) { [(AppDelegate *)NSApp.delegate refreshHold]; }
