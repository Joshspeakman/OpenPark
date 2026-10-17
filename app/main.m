/* Theme Park for macOS: a Cocoa window around the recompiled ARM64 game code
 * and the native DOS host (runtime/host.c). The game runs on its own thread;
 * the window shows the game's frame (320x200 or its 640x480 SVGA mode) at 4:3
 * and forwards keyboard/mouse.
 * Data: ~/Games/ThemeParkNative/{game,saves} (game files copied from the
 * user's own GOG package by tools/build_app.sh). */
#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#import <ImageIO/ImageIO.h>
#import <GameController/GameController.h>
#include <pthread.h>
#include "host.h"
int tp_audio_start(void);
void tp_audio_stop(void);
void tp_audio_set_volumes(float sfx, float music);
BOOL tp_launcher_run(void);
struct Ctx;
void tp_select_code(int (*dispatch)(struct Ctx *), void (*init)(void));
int tp_dispatch(struct Ctx *); void tp_dispatch_init(void);
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

@interface GameView : NSView
- (void)zoomTo:(double)z at:(NSPoint)p;
- (void)panBy:(double)dx :(double)dy;
- (void)pointerAt:(double)fx :(double)fy buttons:(uint32_t)b;
- (void)zoomIn:(id)sender;
- (void)zoomOut:(id)sender;
- (void)centerOn:(double)cx :(double)cy;
@end

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
    uint32_t _buttons;
    /* The park view (runtime/view.c): on when zoomed, or when the window is wider
     * than 4:3 (widescreen: the park fills it, the interface stays in a centred
     * 4:3 column); off, the game draws its own view. */
    BOOL _viewOn, _live;
    double _zoom, _cx, _cy, _offAt;      /* zoom, canvas point at the park area's centre, when the view last ended */
    uint32_t *_big; int _bigW, _bigH;
    NSPoint _mouse; BOOL _mouseIn;
}
- (instancetype)initWithFrame:(NSRect)frame {
    if ((self = [super initWithFrame:frame])) {
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
- (double)layoutW {
    NSRect r = [self viewRect];
    return [self wide] ? _sw * (r.size.width / r.size.height) * 0.75 : _sw;
}
- (void)layout {
    [super layout];
    [CATransaction begin]; [CATransaction setDisableActions:YES];
    _screen.frame = _live ? [self viewRect] : [self screenRect];
    _scan.frame = _screen.frame;
    [CATransaction commit];
}
- (void)present {
    int w, h;
    if (!tp_host_frame_size(&w, &h) || !tp_host_frame_rgba_any(_pixels, 640 * 480)) return;
    int fw = w, fh = h; uint32_t *px = _pixels;
    [self updateGeometry];
    if ((_zoom != 1 || [self wide]) != _viewOn || (_viewOn && fabs([self layoutW] - _lastLW) > 0.5)) [self applyView];
    if (_viewOn && tp_view_camera_moves() != _camMoves) {   /* the game moved its camera itself: look there */
        _camMoves = tp_view_camera_moves();
        tp_view_camera_center(&_cx, &_cy); [self clampCenter];
        tp_view_set(1, _cx, _cy, _zoom, _lastLW = [self layoutW]);
    }
    if (_viewOn) [self edgePan];
    BOOL live = _viewOn && tp_view_live() && fw == _sw;
    if (live) {                                      /* the park view, the game's interface and its panel */
        NSRect vr = [self viewRect];
        CGFloat scale = self.window.backingScaleFactor ?: 2;
        int k = (int)lround([self screenRect].size.width * scale / fw); k = k < 1 ? 1 : k > 6 ? 6 : k;
        if (fw == 320 && k < 2) k = 2;
        /* square output pixels over the view's shape (the layout maps onto it, 320x200's tall pixels included) */
        int oh = fh * k, ow = (int)lround(oh * vr.size.width / vr.size.height);
        if (ow < 16) ow = 16;
        if (!_big || _bigW != ow || _bigH != oh) { free(_big); _bigW = ow; _bigH = oh; _big = malloc(sizeof *_big * _bigW * _bigH); }
        tp_view_compose(_big, _bigW, _bigH, _pixels, fw, fh, [NSUserDefaults.standardUserDefaults boolForKey:@"SmoothPixels"]);
        px = _big; w = _bigW; h = _bigH;
    }
    if (live != _live) { _live = live; [self setNeedsLayout:YES]; [self layout]; }
    [self updateScanlines:fh];
    CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef ctx = CGBitmapContextCreate(px, w, h, 8, w * 4, cs,
                                             kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    CGImageRef img = CGBitmapContextCreateImage(ctx);
    [CATransaction begin]; [CATransaction setDisableActions:YES];
    _screen.contents = (__bridge id)img;
    [CATransaction commit];
    CGImageRelease(img); CGContextRelease(ctx); CGColorSpaceRelease(cs);
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
- (void)sendMouse:(NSEvent *)e {
    NSPoint p = [self convertPoint:e.locationInWindow fromView:nil];
    _mouse = p; _mouseIn = YES;
    NSRect r = _live ? [self viewRect] : [self screenRect];
    double fx = (p.x - r.origin.x) / r.size.width, fy = 1.0 - (p.y - r.origin.y) / r.size.height;
    BOOL inside = fx >= 0 && fx < 1 && fy >= 0 && fy < 1;
    /* In the park view, the view draws the game's cursor at the pointer (view.c), as the game does. */
    [self setCursorHidden:inside && self.window.isKeyWindow && ![NSUserDefaults.standardUserDefaults boolForKey:@"ShowMacCursor"]];
    fx = fx < 0 ? 0 : fx > 0.9999 ? 0.9999 : fx;
    fy = fy < 0 ? 0 : fy > 0.9999 ? 0.9999 : fy;
    if (_live) tp_view_mouse(fx, fy, _buttons);      /* mapped into the park */
    else tp_host_mouse(fx, fy, _buttons);            /* the game's own screen */
}

/* A pointer from elsewhere (a game controller), in 0..1 of what is shown. */
- (void)pointerAt:(double)fx :(double)fy buttons:(uint32_t)b {
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
    _zoom = z;
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
    if (fy > (double)_wh / _sh && fx >= col && fx < 1 - col) return;   /* over the panel */
    double dx = p.x < NSMinX(r) + m ? -v : p.x > NSMaxX(r) - m ? v : 0;
    double dy = p.y > NSMaxY(r) - m ? -v : p.y < NSMinY(r) + m ? v : 0;
    if (dx || dy) [self panBy:dx :dy];
}
- (void)magnifyWithEvent:(NSEvent *)e {
    [self zoomTo:_zoom * (1 + e.magnification) at:[self convertPoint:e.locationInWindow fromView:nil]];
}
- (void)scrollWheel:(NSEvent *)e {
    double dy = e.scrollingDeltaY, dx = e.scrollingDeltaX;
    if (!e.hasPreciseScrollingDeltas) { dx *= 12; dy *= 12; }
    if (e.modifierFlags & (NSEventModifierFlagCommand | NSEventModifierFlagOption))
        [self zoomTo:_zoom * pow(1.006, dy) at:[self convertPoint:e.locationInWindow fromView:nil]];
    else [self panBy:-dx :-dy];
}
- (void)zoomIn:(id)sender { [self zoomTo:_zoom * 1.25 at:NSMakePoint(NAN, NAN)]; }
- (void)zoomOut:(id)sender { [self zoomTo:_zoom / 1.25 at:NSMakePoint(NAN, NAN)]; }
- (void)zoomActual:(id)sender { [self zoomTo:1 at:NSMakePoint(NAN, NAN)]; }
- (void)mouseMoved:(NSEvent *)e { [self sendMouse:e]; }
- (void)mouseDragged:(NSEvent *)e { if (_panning) [self dragPan:e]; else [self sendMouse:e]; }
- (void)rightMouseDragged:(NSEvent *)e { [self sendMouse:e]; }
- (void)otherMouseDragged:(NSEvent *)e { if (_panning) [self dragPan:e]; else [self sendMouse:e]; }
- (void)mouseExited:(NSEvent *)e { [self setCursorHidden:NO]; _mouseIn = NO; }
- (void)mouseEntered:(NSEvent *)e { [self sendMouse:e]; }
- (void)mouseDown:(NSEvent *)e {
    if (_live && (e.modifierFlags & NSEventModifierFlagOption)) { [self startPan:e]; return; }   /* Option-drag pans too */
    /* Control-click acts as the right button for one-button trackpads. */
    _buttons |= (e.modifierFlags & NSEventModifierFlagControl) ? 2 : 1;
    [self sendMouse:e];
}
- (void)mouseUp:(NSEvent *)e {
    if (_panning) { _panning = NO; return; } _buttons &= ~3u; [self sendMouse:e]; }
- (void)rightMouseDown:(NSEvent *)e { _buttons |= 2; [self sendMouse:e]; }
- (void)rightMouseUp:(NSEvent *)e { _buttons &= ~2u; [self sendMouse:e]; }
- (void)otherMouseDown:(NSEvent *)e {
    if (_live) { [self startPan:e]; return; }          /* the park view: the middle button drags it (the game does not use it) */
    _buttons |= 4; [self sendMouse:e];
}
- (void)otherMouseUp:(NSEvent *)e { if (_panning) { _panning = NO; return; } _buttons &= ~4u; [self sendMouse:e]; }
- (void)startPan:(NSEvent *)e { _panning = YES; _panFrom = [self convertPoint:e.locationInWindow fromView:nil]; }
- (void)dragPan:(NSEvent *)e {                        /* the park follows the pointer */
    NSPoint p = [self convertPoint:e.locationInWindow fromView:nil];
    [self panBy:-(p.x - _panFrom.x) :(p.y - _panFrom.y)];
    _panFrom = p;
}
- (void)keyDown:(NSEvent *)e {
    if (e.modifierFlags & NSEventModifierFlagCommand) { [super keyDown:e]; return; }
    unsigned short kc = e.keyCode;
    if (kc == 0x30) {                                   /* Tab, held: fast forward (the game does not use Tab) */
        if (!e.isARepeat && !getenv("TP_CYCLES")) {
            tp_host_set_cycles([NSUserDefaults.standardUserDefaults doubleForKey:@"Cycles"] * 4);
            self.window.subtitle = @"Fast forward";
        }
        return;
    }
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
    if (!e.isARepeat) tp_host_key_raw(scan, 1);
    if (ascii) tp_host_key((uint32_t)scan << 8 | ascii);
    else tp_host_key(function_key(scan, e.modifierFlags));
}
- (void)keyUp:(NSEvent *)e {
    if (e.keyCode == 0x30) {
        if (!getenv("TP_CYCLES")) tp_host_set_cycles([NSUserDefaults.standardUserDefaults doubleForKey:@"Cycles"]);
        self.window.subtitle = @"";
        return;
    }
    if (e.modifierFlags & NSEventModifierFlagCommand) return;
    uint8_t scan = e.keyCode < 128 ? scan_of[e.keyCode] : 0;
    if (scan) tp_host_key_raw(scan, 0);
}
- (void)flagsChanged:(NSEvent *)e {
    NSEventModifierFlags m = e.modifierFlags; uint32_t f = 0;
    if (m & NSEventModifierFlagShift) f |= 3;
    if (m & NSEventModifierFlagControl) f |= 4;
    if (m & NSEventModifierFlagOption) f |= 8;
    if (m & NSEventModifierFlagCapsLock) f |= 0x40;
    tp_host_shift(f);
}
@end

@interface AppDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate>
@end

@implementation AppDelegate {
    NSWindow *_window;
    GameView *_view;
    NSTimer *_timer;
    NSString *_game, *_saves, *_image, *_intro;
    FILE *_log;
}
static AppDelegate *app;
static volatile int game_done, game_result;

static void *game_thread(void *arg) {
    AppDelegate *d = (__bridge AppDelegate *)arg;
    pthread_setname_np("Theme Park game");
    /* THEME.BAT runs INTRO.EXE, then MAIN.EXE. */
    if (d->_intro) {
        setenv("TP_IMAGE", d->_intro.fileSystemRepresentation, 1);
        tp_select_code(intro_dispatch, intro_dispatch_init);
        tp_host_set_args("");
        if (!tp_host_init(d->_game.fileSystemRepresentation, d->_saves.fileSystemRepresentation, 0, NULL)) {
            int r; do r = tp_host_run(4000, 0, 50); while (r == 0);
            fprintf(stderr, "intro ended: %s\n", tp_host_status());
            if (!strcmp(tp_host_status(), "quit")) { game_result = 1; game_done = 1; return NULL; }
        }
        tp_select_code(tp_dispatch, tp_dispatch_init);
    }
    setenv("TP_IMAGE", d->_image.fileSystemRepresentation, 1);
    {   /* the launcher's language: the game's own -l option (GOG's THEME.BAT passes -l0) */
        long lang = [NSUserDefaults.standardUserDefaults integerForKey:@"Language"];
        char args[16] = ""; if (lang > 0 && lang < 5) snprintf(args, sizeof args, " -l%ld", lang);
        tp_host_set_args(args);
    }
    for (;;) {
        tp_view_enable(1);                               /* the zoomable park view knows MAIN.EXE's code */
        if (!getenv("TP_NATIVE_PASS") && ![NSUserDefaults.standardUserDefaults boolForKey:@"NativePasses"])
            setenv("TP_NATIVE_PASS", "0", 1);            /* Launcher: Zoomed Drawing = The Game's */
        tp_view_request_hires([NSUserDefaults.standardUserDefaults boolForKey:@"StartHiRes"] || getenv("TP_START_HIRES"));
        if (tp_host_init(d->_game.fileSystemRepresentation, d->_saves.fileSystemRepresentation, 0, NULL)) {
            game_result = -1; break;
        }
        int r;
        do r = tp_host_run(4000, 0, 50); while (r == 0);
        game_result = r;
        /* THEME.BAT restarts the game when it exits with errorlevel >= 10. */
        if (r == 1 && tp_host_exit_code() >= 10) continue;
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
    [u registerDefaults:@{@"SoundVolume": @1.0, @"MusicVolume": @0.8, @"Sound": @YES, @"Cycles": @15000, @"PlayIntro": @YES, @"Widescreen": @YES, @"NativePasses": @YES}];
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
    fprintf(stderr, "--- Theme Park started %s\n", NSDate.date.description.UTF8String);

    NSRect frame = NSMakeRect(0, 0, 960, 720);
    _window = [[NSWindow alloc] initWithContentRect:frame
        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
        backing:NSBackingStoreBuffered defer:NO];
    _window.title = @"Theme Park";
    _window.delegate = self;
    if (![u boolForKey:@"Widescreen"]) _window.contentAspectRatio = NSMakeSize(4, 3);   /* widescreen: any shape, the park fills it */
    _window.contentMinSize = NSMakeSize(320, 240);
    _window.collectionBehavior = NSWindowCollectionBehaviorFullScreenPrimary;
    _window.backgroundColor = NSColor.blackColor;
    _view = [[GameView alloc] initWithFrame:frame];
    _window.contentView = _view;
    [_window center];
    [_window setFrameAutosaveName:@"ThemeParkWindow"];
    if (!selfTest) [_window makeKeyAndOrderFront:nil];     /* self-tests stay in the background */
    [_window makeFirstResponder:_view];
    _window.acceptsMouseMovedEvents = YES;
    if ([NSUserDefaults.standardUserDefaults boolForKey:@"StartFullScreen"]) [_window toggleFullScreen:nil];

    {
        if ([u boolForKey:@"Sound"] && tp_audio_start() == 0)
            tp_audio_set_volumes([u floatForKey:@"SoundVolume"], [u floatForKey:@"MusicVolume"]);
    }
    pthread_t t;
    pthread_attr_t attr; pthread_attr_init(&attr);
    pthread_attr_set_qos_class_np(&attr, QOS_CLASS_USER_INTERACTIVE, 0);
    pthread_attr_setstacksize(&attr, 8 << 20);
    pthread_create(&t, &attr, game_thread, (__bridge void *)self);
    _timer = [NSTimer scheduledTimerWithTimeInterval:1.0 / 60 target:self selector:@selector(tick) userInfo:nil repeats:YES];
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
        else if ([op isEqual:@"vshot"]) {       /* what the window shows (zoomed or not), 1280x800 */
            static uint32_t frame[640 * 480]; int fw = 320, fh = 200;
            tp_host_frame_size(&fw, &fh); tp_host_frame_rgba_any(frame, 640 * 480);
            NSRect vb = _view.bounds; int oh = 960, ow = 1280;
            if (tp_view_live() && vb.size.width * 3 > vb.size.height * 4 * 1.02) ow = (int)lround(oh * vb.size.width / vb.size.height);
            uint32_t *big = malloc(4 * (size_t)ow * oh);
            if (tp_view_live()) tp_view_compose(big, ow, oh, frame, fw, fh, 0);
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
 * sleep S | ack TAG | quit. mouse/click are in the game frame's pixels (320x200 or 640x480);
 * vmouse/vclick in a vshot's pixels (what the window shows, 960 rows high). ack appends TAG to
 * <file>.ack once everything before it has run. */
static int vshot_size(int *ow, int *oh, NSView *v) {
    *oh = 960; *ow = 1280;
    NSRect vb = v.bounds;
    if (tp_view_live() && vb.size.width * 3 > vb.size.height * 4 * 1.02) *ow = (int)lround(*oh * vb.size.width / vb.size.height);
    return 1;
}
- (void)vshot:(NSString *)path {
    static uint32_t frame[640 * 480]; int fw = 320, fh = 200;
    tp_host_frame_size(&fw, &fh); tp_host_frame_rgba_any(frame, 640 * 480);
    int ow, oh; vshot_size(&ow, &oh, _view);
    uint32_t *big = malloc(4 * (size_t)ow * oh);
    if (tp_view_live()) tp_view_compose(big, ow, oh, frame, fw, fh, 0);
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
        else if ([op isEqual:@"quit"]) [NSApp terminate:nil];
        else fprintf(stderr, "control: unknown %s\n", line.UTF8String);
    }
}

- (void)tick {
    [self runControl];
    [self runTestScript];
    [self pollController];
    [_view present];
    if (game_done) {
        [_timer invalidate];
        _timer = nil;
        fprintf(stderr, "game ended: %s\n", tp_host_status());
        fflush(stderr);
        if (game_result < 0 && strcmp(tp_host_status(), "quit"))
            [self fail:[NSString stringWithFormat:@"%s\n\nDetails are in ~/Games/ThemeParkNative/theme-park.log.", tp_host_status()]];
        else
            [NSApp terminate:nil];
    }
}

/* Hidden or minimised, the park waits (and is silent) until it is back. */
- (void)holdGame:(BOOL)hold {
    tp_host_pause(hold);
    NSUserDefaults *u = NSUserDefaults.standardUserDefaults;
    if (hold) tp_audio_set_volumes(0, 0);
    else tp_audio_set_volumes([u floatForKey:@"SoundVolume"], [u floatForKey:@"MusicVolume"]);
}
- (void)applicationDidHide:(NSNotification *)n { [self holdGame:YES]; }
- (void)applicationDidUnhide:(NSNotification *)n { [self holdGame:NO]; }
- (void)windowDidMiniaturize:(NSNotification *)n { [self holdGame:YES]; }
- (void)windowDidDeminiaturize:(NSNotification *)n { [self holdGame:NO]; }
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)a { return YES; }
- (void)applicationWillTerminate:(NSNotification *)n {
    tp_host_quit();
    tp_audio_stop();
    for (int i = 0; i < 100 && !game_done; i++) usleep(10000);
    fflush(stderr);
}
- (void)toggleFull:(id)sender { [_window toggleFullScreen:nil]; }
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
    CGImageRef img = CGBitmapContextCreateImage(ctx);
    CGImageDestinationRef d = CGImageDestinationCreateWithURL((__bridge CFURLRef)[NSURL fileURLWithPath:path], (__bridge CFStringRef)UTTypePNG.identifier, 1, NULL);
    if (d) { CGImageDestinationAddImage(d, img, NULL); CGImageDestinationFinalize(d); CFRelease(d); }
    CGImageRelease(img); CGContextRelease(ctx); CGColorSpaceRelease(cs); free(px);
    fprintf(stderr, "park picture %dx%d -> %s\n", w, h, path.fileSystemRepresentation);
}
@end

int main(int argc, const char **argv) {
    @autoreleasepool {
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
        NSMenuItem *fs = [viewMenu addItemWithTitle:@"Enter Full Screen" action:@selector(toggleFullScreen:) keyEquivalent:@"f"];
        fs.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagControl;
        [viewMenu addItem:NSMenuItem.separatorItem];
        [viewMenu addItemWithTitle:@"Zoom In" action:@selector(zoomIn:) keyEquivalent:@"="];
        [viewMenu addItemWithTitle:@"Zoom Out" action:@selector(zoomOut:) keyEquivalent:@"-"];
        [viewMenu addItemWithTitle:@"Actual Size" action:@selector(zoomActual:) keyEquivalent:@"0"];
        viewItem.submenu = viewMenu;
        a.mainMenu = bar;
        AppDelegate *d = [AppDelegate new];
        a.delegate = d;
        [a run];
    }
    return 0;
}
