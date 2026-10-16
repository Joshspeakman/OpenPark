/* Theme Park's launch window, in the layout the other ports share (banner,
 * settings rows, group buttons, Quit/Play), dressed in the game's colours.
 * The banner and backdrop are frames of the player's own copy, rendered at
 * install time by tools/build_app.sh (Resources/banner.png, backdrop.png);
 * without them the window draws its title. Settings live in the app's user
 * defaults and are read by main.m. TP_LAUNCHER_SHOT=<png> renders the window
 * to an image and exits (TP_LAUNCHER_OPEN=<group> with a group open). */
#import <Cocoa/Cocoa.h>

static NSColor *rgb(int r, int g, int b) { return [NSColor colorWithSRGBRed:r / 255.0 green:g / 255.0 blue:b / 255.0 alpha:1]; }
static NSColor *grass(void) { return rgb(44, 92, 30); }
static NSColor *grass_dark(void) { return rgb(18, 44, 14); }
static NSColor *sun(void) { return rgb(255, 214, 40); }
static NSColor *sun_hot(void) { return rgb(255, 232, 120); }
static NSColor *cream(void) { return rgb(255, 248, 224); }
static NSColor *ink(void) { return rgb(16, 20, 12); }
static NSColor *ride_red(void) { return rgb(214, 46, 40); }
static NSColor *sky(void) { return rgb(52, 112, 196); }
static NSColor *muted(void) { return rgb(196, 214, 170); }
static NSFont *heavy(CGFloat n) { return [NSFont fontWithName:@"AvenirNext-Heavy" size:n] ?: [NSFont boldSystemFontOfSize:n]; }
static NSFont *demi(CGFloat n) { return [NSFont fontWithName:@"AvenirNext-DemiBold" size:n] ?: [NSFont systemFontOfSize:n weight:NSFontWeightSemibold]; }
static NSFont *regular(CGFloat n) { return [NSFont fontWithName:@"AvenirNext-Medium" size:n] ?: [NSFont systemFontOfSize:n]; }

static void text(NSString *s, NSFont *f, NSColor *c, NSRect r, NSTextAlignment al) {
    NSMutableParagraphStyle *p = [NSMutableParagraphStyle new]; p.alignment = al; p.lineBreakMode = NSLineBreakByTruncatingTail;
    [s drawInRect:r withAttributes:@{NSFontAttributeName: f, NSForegroundColorAttributeName: c, NSParagraphStyleAttributeName: p}];
}
static void rounded(NSRect r, CGFloat rad, NSColor *fill, NSColor *line, CGFloat w) {
    NSBezierPath *b = [NSBezierPath bezierPathWithRoundedRect:r xRadius:rad yRadius:rad];
    if (fill) { [fill setFill]; [b fill]; }
    if (line) { [line setStroke]; b.lineWidth = w; [b stroke]; }
}
static void cover(NSImage *img, NSRect r, CGFloat fraction) {
    NSSize s = img.size; CGFloat k = MAX(r.size.width / s.width, r.size.height / s.height);
    CGFloat w = r.size.width / k, h = r.size.height / k;
    [img drawInRect:r fromRect:NSMakeRect((s.width - w) / 2, (s.height - h) / 2, w, h)
          operation:NSCompositingOperationSourceOver fraction:fraction respectFlipped:YES
              hints:@{NSImageHintInterpolation: @(NSImageInterpolationNone)}];
}

/* ---- settings ---- */
typedef struct { NSString *title, *key; NSArray *values; NSArray<NSString *> *labels; } Setting;
static Setting setting(NSString *t, NSString *k, NSArray *v, NSArray *l) { Setting s = {t, k, v, l}; return s; }

@interface TPLaunchView : NSView
@property NSInteger focus, hover, open;   /* open: -1 or group index */
@property BOOL result, done;
@end

enum { HIT_NONE = -1, HIT_PLAY = 1000, HIT_QUIT = 1001, HIT_BACK = 1002 };

@implementation TPLaunchView {
    NSImage *_banner, *_backdrop;
    Setting _top[4];
    NSArray<NSString *> *_groups;
    Setting _display[3], _sound[3];
}
- (instancetype)initWithFrame:(NSRect)f {
    if ((self = [super initWithFrame:f])) {
        NSBundle *b = NSBundle.mainBundle;
        NSString *p;
        if ((p = [b pathForResource:@"banner" ofType:@"png"])) _banner = [[NSImage alloc] initWithContentsOfFile:p];
        if ((p = [b pathForResource:@"backdrop" ofType:@"png"])) _backdrop = [[NSImage alloc] initWithContentsOfFile:p];
        NSArray *vol = @[@0.0, @0.1, @0.2, @0.3, @0.4, @0.5, @0.6, @0.7, @0.8, @0.9, @1.0];
        NSArray *volL = @[@"Off", @"10%", @"20%", @"30%", @"40%", @"50%", @"60%", @"70%", @"80%", @"90%", @"100%"];
        _top[0] = setting(@"Screen", @"StartFullScreen", @[@NO, @YES], @[@"Window", @"Full screen"]);
        _top[1] = setting(@"Game speed", @"Cycles", @[@10000, @20000, @40000],
                          @[@"Original (as GOG's DOSBox)", @"Fast", @"Fastest"]);
        _top[2] = setting(@"Sound", @"Sound", @[@YES, @NO], @[@"On", @"Off"]);
        _top[3] = setting(@"Intro", @"PlayIntro", @[@YES, @NO], @[@"Play", @"Skip"]);
        _groups = @[@"Display", @"Sound", @"Controls", @"Files"];
        _display[0] = setting(@"Pixels", @"SmoothPixels", @[@NO, @YES], @[@"Sharp", @"Smooth"]);
        _display[1] = setting(@"Aspect", @"Stretch", @[@NO, @YES], @[@"4:3, as on a CRT", @"Fill the window"]);
        _display[2] = setting(@"Mouse pointer", @"ShowMacCursor", @[@NO, @YES], @[@"Game's own", @"Game's and the Mac's"]);
        _sound[0] = setting(@"Sound", @"Sound", @[@YES, @NO], @[@"On", @"Off"]);
        _sound[1] = setting(@"Effects volume", @"SoundVolume", vol, volL);
        _sound[2] = setting(@"Music volume", @"MusicVolume", vol, volL);
        self.open = -1; self.hover = HIT_NONE;
        NSString *o = NSProcessInfo.processInfo.environment[@"TP_LAUNCHER_OPEN"];
        if (o) self.open = [_groups indexOfObject:o.capitalizedString] == NSNotFound ? -1 : (NSInteger)[_groups indexOfObject:o.capitalizedString];
    }
    return self;
}
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent *)e { return YES; }

- (NSInteger)rowCount { return self.open == 0 ? 3 : self.open == 1 ? 3 : self.open < 0 ? 4 : 0; }
- (Setting *)rowSetting:(NSInteger)k {
    if (self.open == 0) return &_display[k];
    if (self.open == 1) return &_sound[k];
    return &_top[k];
}
- (NSInteger)valueIndex:(Setting *)s {
    id v = [NSUserDefaults.standardUserDefaults objectForKey:s->key];
    NSUInteger i = v ? [s->values indexOfObject:v] : NSNotFound;
    if (i == NSNotFound && [v isKindOfClass:NSNumber.class])          /* nearest numeric value */
        for (NSUInteger k = 0; k < s->values.count; k++)
            if (fabs([s->values[k] doubleValue] - [v doubleValue]) < 0.05) i = k;
    return i == NSNotFound ? 0 : (NSInteger)i;
}
- (void)step:(NSInteger)k by:(NSInteger)d {
    Setting *s = [self rowSetting:k];
    NSInteger n = (NSInteger)s->values.count, i = ([self valueIndex:s] + d + n) % n;
    [NSUserDefaults.standardUserDefaults setObject:s->values[i] forKey:s->key];
    self.needsDisplay = YES;
}

/* ---- layout ---- */
#define BANNER_H 196
- (NSRect)panel { NSRect b = self.bounds; return NSMakeRect(40, BANNER_H + 18, b.size.width - 80, b.size.height - BANNER_H - 118); }
- (NSRect)rowRect:(NSInteger)k { NSRect p = self.panel; return NSMakeRect(p.origin.x + 18, p.origin.y + 48 + 46 * k, p.size.width - 36, 40); }
- (NSRect)arrow:(NSInteger)k right:(BOOL)r { NSRect x = [self rowRect:k]; return NSMakeRect(r ? NSMaxX(x) - 34 : NSMaxX(x) - 312, x.origin.y + 4, 32, 32); }
- (NSRect)groupRect:(NSInteger)g {
    NSRect p = self.panel; CGFloat w = (p.size.width - 36 - 3 * 12) / 4;
    return NSMakeRect(p.origin.x + 18 + g * (w + 12), NSMaxY(p) - 60, w, 44);
}
- (NSRect)playRect { NSRect b = self.bounds; return NSMakeRect(NSMidX(b) + 10, NSMaxY(b) - 82, 220, 54); }
- (NSRect)quitRect { NSRect b = self.bounds; return NSMakeRect(NSMidX(b) - 230, NSMaxY(b) - 82, 220, 54); }

- (NSInteger)hit:(NSPoint)p {
    if (NSPointInRect(p, self.playRect)) return HIT_PLAY;
    if (NSPointInRect(p, self.quitRect)) return self.open >= 0 ? HIT_BACK : HIT_QUIT;
    for (NSInteger k = 0; k < self.rowCount; k++) {
        if (NSPointInRect(p, [self arrow:k right:NO])) return 100 + k;
        if (NSPointInRect(p, [self arrow:k right:YES]) || NSPointInRect(p, [self rowRect:k])) return 200 + k;
    }
    if (self.open < 0) for (NSInteger g = 0; g < 4; g++) if (NSPointInRect(p, [self groupRect:g])) return 300 + g;
    return HIT_NONE;
}
- (void)press:(NSInteger)h {
    if (h == HIT_PLAY) { self.result = YES; self.done = YES; }
    else if (h == HIT_QUIT) { self.result = NO; self.done = YES; }
    else if (h == HIT_BACK) { self.open = -1; }
    else if (h >= 300) {
        if (h == 303) [self openFiles];
        else self.open = h - 300;
    }
    else if (h >= 200) [self step:h - 200 by:1];
    else if (h >= 100) [self step:h - 100 by:-1];
    self.needsDisplay = YES;
}
- (void)openFiles {
    NSString *root = [NSHomeDirectory() stringByAppendingPathComponent:@"Games/ThemeParkNative"];
    NSString *saves = [root stringByAppendingPathComponent:@"saves/save"];
    [NSFileManager.defaultManager createDirectoryAtPath:saves withIntermediateDirectories:YES attributes:nil error:nil];
    [NSWorkspace.sharedWorkspace openURL:[NSURL fileURLWithPath:saves]];
}
- (void)updateTrackingAreas {
    for (NSTrackingArea *t in self.trackingAreas) [self removeTrackingArea:t];
    [self addTrackingArea:[[NSTrackingArea alloc] initWithRect:NSZeroRect options:NSTrackingMouseMoved | NSTrackingActiveAlways | NSTrackingInVisibleRect owner:self userInfo:nil]];
    [super updateTrackingAreas];
}
- (void)mouseMoved:(NSEvent *)e { NSInteger h = [self hit:[self convertPoint:e.locationInWindow fromView:nil]]; if (h != self.hover) { self.hover = h; self.needsDisplay = YES; } }
- (void)mouseDown:(NSEvent *)e { [self press:[self hit:[self convertPoint:e.locationInWindow fromView:nil]]]; }
- (void)keyDown:(NSEvent *)e {
    NSInteger rows = self.rowCount, items = rows + (self.open < 0 ? 4 : 0);
    switch (e.keyCode) {
    case 0x24: case 0x4c:                                  /* return: play, or press the focused group */
        if (self.focus >= rows && self.open < 0) [self press:300 + self.focus - rows]; else [self press:HIT_PLAY]; break;
    case 0x35: [self press:self.open >= 0 ? HIT_BACK : HIT_QUIT]; break;
    case 0x7e: self.focus = (self.focus + items - 1) % MAX(items, 1); break;
    case 0x7d: self.focus = (self.focus + 1) % MAX(items, 1); break;
    case 0x7b: if (self.focus < rows) [self step:self.focus by:-1]; break;
    case 0x7c: if (self.focus < rows) [self step:self.focus by:1]; break;
    default: [super keyDown:e]; return;
    }
    self.needsDisplay = YES;
}

/* ---- drawing ---- */
- (void)button:(NSRect)r title:(NSString *)t primary:(BOOL)primary lit:(BOOL)lit {
    NSColor *fill = primary ? (lit ? sun_hot() : sun()) : (lit ? rgb(70, 130, 52) : rgb(52, 104, 38));
    rounded(NSOffsetRect(r, 0, 4), 12, [NSColor colorWithWhite:0 alpha:0.45], nil, 0);
    rounded(r, 12, fill, ink(), 2.5);
    text(t, heavy(primary ? 22 : 18), primary ? ink() : cream(), NSMakeRect(r.origin.x, NSMidY(r) - (primary ? 16 : 13), r.size.width, 30), NSTextAlignmentCenter);
}
- (void)drawRect:(NSRect)dirty {
    NSRect b = self.bounds;
    [grass_dark() setFill]; NSRectFill(b);
    if (_backdrop) { cover(_backdrop, b, 0.35); }
    NSGradient *g = [[NSGradient alloc] initWithStartingColor:[grass_dark() colorWithAlphaComponent:0.2] endingColor:[grass_dark() colorWithAlphaComponent:0.95]];
    [g drawInRect:b angle:90];

    /* banner */
    NSRect br = NSMakeRect(0, 0, b.size.width, BANNER_H);
    if (_banner) {   /* the title screen's "theme PARK" logo box, at the CRT's 1.2 pixel aspect */
        NSRect src = NSMakeRect(28, _banner.size.height - 50, 240, 48);
        CGFloat w = 640, h = w / src.size.width * src.size.height * 1.2;
        NSRect dst = NSMakeRect((b.size.width - w) / 2, (BANNER_H - h) / 2 - 4, w, h);
        rounded(NSInsetRect(NSOffsetRect(dst, 0, 6), -6, -6), 10, [NSColor colorWithWhite:0 alpha:0.5], nil, 0);
        [_banner drawInRect:dst fromRect:src operation:NSCompositingOperationSourceOver fraction:1 respectFlipped:YES
                      hints:@{NSImageHintInterpolation: @(NSImageInterpolationNone)}];
    }
    else text(@"THEME PARK", heavy(54), sun(), NSMakeRect(0, 60, b.size.width, 70), NSTextAlignmentCenter);
    [[NSColor colorWithWhite:0 alpha:0.35] setFill]; NSRectFillUsingOperation(NSMakeRect(0, NSMaxY(br) - 3, b.size.width, 3), NSCompositingOperationSourceOver);
    text(@"NATIVE  •  APPLE SILICON  •  NIGHTLY", demi(11), muted(), NSMakeRect(0, BANNER_H - 2, b.size.width, 16), NSTextAlignmentCenter);

    /* panel */
    NSRect p = self.panel;
    rounded(p, 16, [rgb(24, 52, 18) colorWithAlphaComponent:0.92], [sun() colorWithAlphaComponent:0.7], 2);
    NSString *title = self.open == 0 ? @"Display" : self.open == 1 ? @"Sound" : self.open == 2 ? @"Controls" : @"Settings";
    text(title.uppercaseString, heavy(15), sun(), NSMakeRect(p.origin.x + 22, p.origin.y + 14, 300, 22), NSTextAlignmentLeft);

    if (self.open == 2) [self drawControlsIn:p];
    for (NSInteger k = 0; k < self.rowCount; k++) {
        Setting *s = [self rowSetting:k];
        NSRect r = [self rowRect:k];
        BOOL focused = self.focus == k, hot = self.hover == 100 + k || self.hover == 200 + k;
        rounded(r, 10, focused || hot ? [NSColor colorWithWhite:1 alpha:0.10] : [NSColor colorWithWhite:0 alpha:0.18], focused ? sun() : nil, 1.5);
        text(s->title, demi(16), cream(), NSMakeRect(r.origin.x + 16, r.origin.y + 9, 260, 24), NSTextAlignmentLeft);
        NSRect va = NSMakeRect(NSMaxX(r) - 278, r.origin.y + 9, 242, 24);
        text(s->labels[[self valueIndex:s]], demi(16), sun(), va, NSTextAlignmentCenter);
        for (int side = 0; side < 2; side++) {
            NSRect a = [self arrow:k right:side];
            BOOL lit = self.hover == (side ? 200 : 100) + k;
            rounded(a, 8, lit ? sun() : [NSColor colorWithWhite:1 alpha:0.12], nil, 0);
            text(side ? @"›" : @"‹", heavy(20), lit ? ink() : cream(), NSMakeRect(a.origin.x, a.origin.y + 1, 32, 28), NSTextAlignmentCenter);
        }
    }
    if (self.open < 0)
        for (NSInteger gi = 0; gi < 4; gi++)
            [self button:[self groupRect:gi] title:_groups[gi] primary:NO lit:self.hover == 300 + gi || self.focus == self.rowCount + gi];
    [self button:self.quitRect title:self.open >= 0 ? @"Back" : @"Quit" primary:NO lit:self.hover == HIT_QUIT || self.hover == HIT_BACK];
    [self button:self.playRect title:@"Play" primary:YES lit:self.hover == HIT_PLAY];
    NSString *build = NSBundle.mainBundle.infoDictionary[@"CFBundleShortVersionString"] ?: @"dev";
    text(build, regular(10), muted(), NSMakeRect(b.size.width - 220, b.size.height - 18, 210, 14), NSTextAlignmentRight);
}
- (void)drawControlsIn:(NSRect)p {
    NSArray *rows = @[
        @[@"Click", @"Select, build, place"], @[@"Right-click or Control-click", @"Cancel, rotate, close panels"],
        @[@"Mouse at a screen edge", @"Scroll the park"], @[@"Type on the nickname screen", @"Keys go to the game as on a PC keyboard"],
        @[@"⌃⌘F", @"Full screen"], @[@"⌘Q", @"Quit (the game does not save on its own)"],
    ];
    for (NSUInteger i = 0; i < rows.count; i++) {
        CGFloat y = p.origin.y + 50 + 30 * i;
        text(rows[i][0], demi(15), sun(), NSMakeRect(p.origin.x + 30, y, 300, 22), NSTextAlignmentLeft);
        text(rows[i][1], regular(15), cream(), NSMakeRect(p.origin.x + 330, y, p.size.width - 350, 22), NSTextAlignmentLeft);
    }
}
@end

/* Returns YES to play. */
BOOL tp_launcher_run(void) {
    NSRect frame = NSMakeRect(0, 0, 820, 626);
    TPLaunchView *v = [[TPLaunchView alloc] initWithFrame:frame];
    NSString *shot = NSProcessInfo.processInfo.environment[@"TP_LAUNCHER_SHOT"];
    if (shot) {                                             /* bounded preview render */
        NSBitmapImageRep *rep = [v bitmapImageRepForCachingDisplayInRect:v.bounds];
        [v cacheDisplayInRect:v.bounds toBitmapImageRep:rep];
        [[rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:shot atomically:YES];
        return NO;
    }
    NSWindow *w = [[NSWindow alloc] initWithContentRect:frame styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                                                backing:NSBackingStoreBuffered defer:NO];
    w.title = @"Theme Park";
    w.contentView = v;
    [w center];
    [w makeKeyAndOrderFront:nil];
    [w makeFirstResponder:v];
    [NSApp activateIgnoringOtherApps:YES];
    while (!v.done && w.isVisible) {
        @autoreleasepool {
            NSEvent *e = [NSApp nextEventMatchingMask:NSEventMaskAny untilDate:[NSDate dateWithTimeIntervalSinceNow:0.1]
                                               inMode:NSDefaultRunLoopMode dequeue:YES];
            if (e) [NSApp sendEvent:e];
        }
    }
    [w orderOut:nil];
    return v.done && v.result;
}
