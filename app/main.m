/* Theme Park for macOS: a Cocoa window around the recompiled ARM64 game code
 * and the native DOS host (runtime/host.c). The game runs on its own thread;
 * the window shows the 320x200 VGA frame at 4:3 and forwards keyboard/mouse.
 * Data: ~/Games/ThemeParkNative/{game,saves} (game files copied from the
 * user's own GOG package by tools/build_app.sh). */
#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
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
@end

@implementation GameView {
    CALayer *_screen;
    uint32_t _pixels[320 * 200];
    NSTrackingArea *_tracking;
    BOOL _cursorHidden;
    uint32_t _buttons;
    /* Zoom (runtime/view.c): off at 1x, where the game draws its own view. */
    BOOL _zoomed;
    double _zoom, _cx, _cy, _offAt;      /* zoom, canvas point at the park area's centre, when zoom last ended */
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
    }
    return self;
}
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent *)e { return YES; }
- (NSRect)screenRect {
    NSRect b = self.bounds;
    CGFloat w = b.size.width, h = b.size.height;
    if ([NSUserDefaults.standardUserDefaults boolForKey:@"Stretch"]) return b;
    if (w * 3 > h * 4) w = h * 4 / 3; else h = w * 3 / 4;
    return NSMakeRect(floor((b.size.width - w) / 2), floor((b.size.height - h) / 2), floor(w), floor(h));
}
- (void)layout {
    [super layout];
    [CATransaction begin]; [CATransaction setDisableActions:YES];
    _screen.frame = [self screenRect];
    [CATransaction commit];
}
- (void)present {
    if (!tp_host_frame_rgba(_pixels)) return;
    uint32_t *px = _pixels; int w = 320, h = 200;
    if (_zoomed) [self edgePan];
    if (_zoomed && tp_view_live()) {    /* the zoomed park, the game's interface and its panel */
        CGFloat scale = self.window.backingScaleFactor ?: 2;
        int k = (int)lround([self screenRect].size.width * scale / 320); k = k < 2 ? 2 : k > 6 ? 6 : k;
        if (!_big || _bigW != 320 * k) { free(_big); _bigW = 320 * k; _bigH = 200 * k; _big = malloc(sizeof *_big * _bigW * _bigH); }
        tp_view_compose(_big, _bigW, _bigH, _pixels, [NSUserDefaults.standardUserDefaults boolForKey:@"SmoothPixels"]);
        px = _big; w = _bigW; h = _bigH;
    }
    CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef ctx = CGBitmapContextCreate(px, w, h, 8, w * 4, cs,
                                             kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    CGImageRef img = CGBitmapContextCreateImage(ctx);
    [CATransaction begin]; [CATransaction setDisableActions:YES];
    _screen.contents = (__bridge id)img;
    [CATransaction commit];
    CGImageRelease(img); CGContextRelease(ctx); CGColorSpaceRelease(cs);
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
    NSRect r = [self screenRect];
    double fx = (p.x - r.origin.x) / r.size.width, fy = 1.0 - (p.y - r.origin.y) / r.size.height;
    BOOL inside = fx >= 0 && fx < 1 && fy >= 0 && fy < 1;
    /* Zoomed, the view draws the game's cursor at the pointer (view.c), as the game does at 1x. */
    [self setCursorHidden:inside && self.window.isKeyWindow && ![NSUserDefaults.standardUserDefaults boolForKey:@"ShowMacCursor"]];
    fx = fx < 0 ? 0 : fx > 0.9999 ? 0.9999 : fx;
    fy = fy < 0 ? 0 : fy > 0.9999 ? 0.9999 : fy;
    tp_view_mouse(fx, fy, _buttons);
}

/* ---- zoom ---- */
- (double)minZoom {
    double w, h; tp_view_canvas_size(&w, &h);
    return fmax(0.1, fmin(320.0 / w, TP_VIEW_WORLD_ROWS / h));
}
- (void)clampCenter {
    double w, h; tp_view_canvas_size(&w, &h);
    double hw = 160.0 / _zoom, hh = TP_VIEW_WORLD_ROWS / 2.0 / _zoom;
    _cx = hw * 2 >= w ? w / 2 : fmin(fmax(_cx, hw), w - hw);
    _cy = hh * 2 >= h ? h / 2 : fmin(fmax(_cy, hh), h - hh);
}
/* Zoom to z keeping the park point under p (view coordinates; NAN: the centre) in place. */
- (void)zoomTo:(double)z at:(NSPoint)p {
    if (!_zoomed) {
        if (CACurrentMediaTime() - _offAt > 1.0) tp_view_camera_center(&_cx, &_cy);   /* start where the game's view is */
        _zoom = 1;
    }
    NSRect r = [self screenRect];
    double fx = isnan(p.x) ? 0.5 : (p.x - r.origin.x) / r.size.width;
    double fy = isnan(p.x) ? TP_VIEW_WORLD_ROWS / 400.0 : 1.0 - (p.y - r.origin.y) / r.size.height;
    double ax = _cx + (fx - 0.5) * 320 / _zoom, ay = _cy + (fy * 200 - TP_VIEW_WORLD_ROWS / 2.0) / _zoom;
    z = fmin(fmax(z, [self minZoom]), 4.0);
    if (fabs(z - 1) < 0.04) z = 1;                   /* a detent at the game's own scale */
    _zoom = z;
    _cx = ax - (fx - 0.5) * 320 / z; _cy = ay - (fy * 200 - TP_VIEW_WORLD_ROWS / 2.0) / z;
    [self clampCenter];
    BOOL on = z != 1;
    if (!on && _zoomed) _offAt = CACurrentMediaTime();
    if (!tp_view_set(on, _cx, _cy, z)) { on = NO; _zoom = 1; }   /* not in the park (the intro, say) */
    _zoomed = on;
}
- (void)panBy:(double)dx :(double)dy {               /* view points */
    if (!_zoomed) return;
    double k = 320.0 / _zoom / [self screenRect].size.width;
    _cx += dx * k; _cy += dy * k;
    [self clampCenter];
    tp_view_set(1, _cx, _cy, _zoom);
}
- (void)edgePan {                                    /* the pointer at the window's edge scrolls, as in the game */
    if (!_mouseIn || !self.window.isKeyWindow) return;
    NSRect r = [self screenRect]; NSPoint p = _mouse; double m = 6, v = 9;
    if (1.0 - (p.y - r.origin.y) / r.size.height > TP_VIEW_WORLD_ROWS / 200.0) return;   /* over the panel */
    double dx = p.x < NSMinX(r) + m ? -v : p.x > NSMaxX(r) - m ? v : 0;
    double dy = p.y > NSMaxY(r) - m ? -v : 0;
    if (dx || dy) [self panBy:dx :dy];
}
- (void)magnifyWithEvent:(NSEvent *)e {
    [self zoomTo:(_zoomed ? _zoom : 1) * (1 + e.magnification) at:[self convertPoint:e.locationInWindow fromView:nil]];
}
- (void)scrollWheel:(NSEvent *)e {
    double dy = e.scrollingDeltaY, dx = e.scrollingDeltaX;
    if (!e.hasPreciseScrollingDeltas) { dx *= 12; dy *= 12; }
    if (e.modifierFlags & (NSEventModifierFlagCommand | NSEventModifierFlagOption))
        [self zoomTo:(_zoomed ? _zoom : 1) * pow(1.006, dy) at:[self convertPoint:e.locationInWindow fromView:nil]];
    else [self panBy:-dx :-dy];
}
- (void)zoomIn:(id)sender { [self zoomTo:(_zoomed ? _zoom : 1) * 1.25 at:NSMakePoint(NAN, NAN)]; }
- (void)zoomOut:(id)sender { [self zoomTo:(_zoomed ? _zoom : 1) / 1.25 at:NSMakePoint(NAN, NAN)]; }
- (void)zoomActual:(id)sender { if (_zoomed) [self zoomTo:1 at:NSMakePoint(NAN, NAN)]; }
- (void)mouseMoved:(NSEvent *)e { [self sendMouse:e]; }
- (void)mouseDragged:(NSEvent *)e { [self sendMouse:e]; }
- (void)rightMouseDragged:(NSEvent *)e { [self sendMouse:e]; }
- (void)otherMouseDragged:(NSEvent *)e { [self sendMouse:e]; }
- (void)mouseExited:(NSEvent *)e { [self setCursorHidden:NO]; _mouseIn = NO; }
- (void)mouseEntered:(NSEvent *)e { [self sendMouse:e]; }
- (void)mouseDown:(NSEvent *)e {
    /* Control-click acts as the right button for one-button trackpads. */
    _buttons |= (e.modifierFlags & NSEventModifierFlagControl) ? 2 : 1;
    [self sendMouse:e];
}
- (void)mouseUp:(NSEvent *)e { _buttons &= ~3u; [self sendMouse:e]; }
- (void)rightMouseDown:(NSEvent *)e { _buttons |= 2; [self sendMouse:e]; }
- (void)rightMouseUp:(NSEvent *)e { _buttons &= ~2u; [self sendMouse:e]; }
- (void)otherMouseDown:(NSEvent *)e { _buttons |= 4; [self sendMouse:e]; }
- (void)otherMouseUp:(NSEvent *)e { _buttons &= ~4u; [self sendMouse:e]; }
- (void)keyDown:(NSEvent *)e {
    if (e.modifierFlags & NSEventModifierFlagCommand) { [super keyDown:e]; return; }
    unsigned short kc = e.keyCode;
    if (_zoomed && kc >= 0x7b && kc <= 0x7e) {          /* zoomed: the arrows pan the view */
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
        if (!tp_host_init(d->_game.fileSystemRepresentation, d->_saves.fileSystemRepresentation, 0, NULL)) {
            int r; do r = tp_host_run(4000, 0, 50); while (r == 0);
            fprintf(stderr, "intro ended: %s\n", tp_host_status());
            if (!strcmp(tp_host_status(), "quit")) { game_result = 1; game_done = 1; return NULL; }
        }
        tp_select_code(tp_dispatch, tp_dispatch_init);
    }
    setenv("TP_IMAGE", d->_image.fileSystemRepresentation, 1);
    for (;;) {
        tp_view_enable(1);                               /* the zoomable park view knows MAIN.EXE's code */
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
    [u registerDefaults:@{@"SoundVolume": @1.0, @"MusicVolume": @0.8, @"Sound": @YES, @"Cycles": @15000, @"PlayIntro": @YES}];
    /* Builds before 2026-10-17 counted a REP string instruction as one instruction, where DOSBox
     * charges every repetition: their "Original" ran about 1.9 times GOG's speed. The player found it
     * about 20% too fast, so a saved Original becomes 1.5x once (the launcher offers exact Original). */
    if ([u integerForKey:@"SpeedModel"] < 2) {
        if ([u objectForKey:@"Cycles"] && [u doubleForKey:@"Cycles"] == 10000) [u setDouble:15000 forKey:@"Cycles"];
        [u setInteger:2 forKey:@"SpeedModel"];
    }
    BOOL selfTest = NSProcessInfo.processInfo.environment[@"TP_TEST"].length > 0;
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
    _window.contentAspectRatio = NSMakeSize(4, 3);
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
        else if ([op isEqual:@"vclick"]) {      /* a click through the zoomed view, 640x400 screen space */
            double x = [p[2] doubleValue] / 640.0, y = [p[3] doubleValue] / 400.0;
            tp_view_mouse(x, y, 0); tp_view_mouse(x, y, 1);
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 150 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{ tp_view_mouse(x, y, 0); });
        }
        else if ([op isEqual:@"vshot"]) {       /* what the window shows (zoomed or not), 1280x800 */
            static uint32_t frame[64000]; tp_host_frame_rgba(frame);
            uint32_t *big = malloc(4 * 1280 * 800);
            if (tp_view_live()) tp_view_compose(big, 1280, 800, frame, 0);
            else for (int i = 0; i < 1280 * 800; i++) big[i] = frame[(i / 1280 / 4) * 320 + (i % 1280) / 4];
            NSString *path = [[p subarrayWithRange:NSMakeRange(2, p.count - 2)] componentsJoinedByString:@":"];
            int tp_host_png_rgba(const char *, const uint32_t *, int, int);
            tp_host_png_rgba(path.fileSystemRepresentation, big, 1280, 800); free(big);
            fprintf(stderr, "vshot live=%d\n", tp_view_live());
        }
        else if ([op isEqual:@"quit"]) [NSApp terminate:nil];
    }
}

- (void)tick {
    [self runTestScript];
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

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)a { return YES; }
- (void)applicationWillTerminate:(NSNotification *)n {
    tp_host_quit();
    tp_audio_stop();
    for (int i = 0; i < 100 && !game_done; i++) usleep(10000);
    fflush(stderr);
}
- (void)toggleFull:(id)sender { [_window toggleFullScreen:nil]; }
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
