/* Theme Park's launch window, in the layout the other ports share (Freedom Force, Brutal Half-Life): a banner with the
 * title and a faint motif, gold labels beside menu fields, outlined group buttons, Quit/Play and the build line.
 * The title is the "theme PARK" box from the player's own title screen, rendered at install time by
 * tools/build_app.sh (Resources/banner.png) and scaled in whole steps so its pixels stay square; without it the
 * window sets the title in type. Settings live in the app's user defaults and are read by main.m.
 * TP_LAUNCHER_SHOT=<png> renders the window to an image and exits (TP_LAUNCHER_OPEN=<group> with a group open,
 * TP_LAUNCHER_FOCUS=<n> with a row focused). */
#import <Cocoa/Cocoa.h>

static NSColor *rgb(int r, int g, int b) { return [NSColor colorWithSRGBRed:r / 255.0 green:g / 255.0 blue:b / 255.0 alpha:1]; }
static NSColor *ground(void) { return rgb(11, 22, 15); }
static NSColor *ray(void) { return rgb(20, 42, 27); }
static NSColor *gold(void) { return rgb(245, 197, 66); }
static NSColor *cream(void) { return rgb(248, 241, 220); }
static NSColor *muted(void) { return rgb(132, 152, 136); }
static NSColor *panel(void) { return rgb(19, 38, 26); }
static NSColor *panel_lit(void) { return rgb(30, 58, 39); }
static NSColor *ride_red(void) { return rgb(196, 52, 40); }
static NSFont *heavy(CGFloat n) { return [NSFont fontWithName:@"AvenirNextCondensed-HeavyItalic" size:n] ?: [NSFont boldSystemFontOfSize:n]; }
static NSFont *demi(CGFloat n) { return [NSFont fontWithName:@"AvenirNextCondensed-DemiBold" size:n] ?: [NSFont systemFontOfSize:n weight:NSFontWeightSemibold]; }
static NSFont *medium(CGFloat n) { return [NSFont fontWithName:@"AvenirNextCondensed-Medium" size:n] ?: [NSFont systemFontOfSize:n]; }
static NSFont *mono(CGFloat n, BOOL bold) { return [NSFont monospacedSystemFontOfSize:n weight:bold ? NSFontWeightBold : NSFontWeightRegular]; }

static void text(NSString *s, NSFont *f, NSColor *c, NSRect r, NSTextAlignment al, CGFloat kern) {
    NSMutableParagraphStyle *p = [NSMutableParagraphStyle new]; p.alignment = al; p.lineBreakMode = NSLineBreakByWordWrapping;
    [s drawInRect:r withAttributes:@{NSFontAttributeName: f, NSForegroundColorAttributeName: c, NSParagraphStyleAttributeName: p, NSKernAttributeName: @(kern)}];
}
/* one line, vertically centred in r */
static void label(NSString *s, NSFont *f, NSColor *c, NSRect r, NSTextAlignment al, CGFloat kern) {
    CGFloat h = f.ascender - f.descender;
    text(s, f, c, NSMakeRect(r.origin.x, NSMidY(r) - h / 2, r.size.width, h + 2), al, kern);
}
static void rounded(NSRect r, CGFloat rad, NSColor *fill, NSColor *line, CGFloat w) {
    NSBezierPath *b = [NSBezierPath bezierPathWithRoundedRect:r xRadius:rad yRadius:rad];
    if (fill) { [fill setFill]; [b fill]; }
    if (line) { [line setStroke]; b.lineWidth = w; [b stroke]; }
}
/* `r` (top-down pixels) of an image, enlarged k times with hard pixel edges, to be drawn smoothly at any size */
static NSImage *pixel_crop(NSImage *img, CGRect r, int k) {
    CGImageRef c = [img CGImageForProposedRect:NULL context:nil hints:nil]; if (!c) return nil;
    CGImageRef part = CGImageCreateWithImageInRect(c, r); if (!part) return nil;
    size_t w = CGImageGetWidth(part) * k, h = CGImageGetHeight(part) * k;
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef g = CGBitmapContextCreate(NULL, w, h, 8, 0, cs, (CGBitmapInfo)kCGImageAlphaPremultipliedLast);
    CGContextSetInterpolationQuality(g, kCGInterpolationNone);
    CGContextDrawImage(g, CGRectMake(0, 0, w, h), part);
    CGImageRef big = CGBitmapContextCreateImage(g);
    NSImage *out = [[NSImage alloc] initWithCGImage:big size:NSMakeSize(w, h)];
    CGImageRelease(big); CGContextRelease(g); CGColorSpaceRelease(cs); CGImageRelease(part);
    return out;
}

/* ---- settings ---- */
typedef struct { NSString *title, *key; NSArray *values; NSArray<NSString *> *labels; } Setting;
static Setting setting(NSString *t, NSString *k, NSArray *v, NSArray *l) { Setting s = {t, k, v, l}; return s; }

@interface TPLaunchView : NSView
@property NSInteger focus, hover, open;   /* open: -1 or group index */
@property BOOL result, done;
@end

enum { HIT_NONE = -1, HIT_PLAY = 1000, HIT_QUIT = 1001, HIT_BACK = 1002 };
#define WIN_W 760
#define WIN_H 790
#define BANNER_H 232

@implementation TPLaunchView {
    NSImage *_logo;
    Setting _top[4];
    NSArray<NSString *> *_groups;
    Setting _display[3], _sound[3];
}
- (instancetype)initWithFrame:(NSRect)f {
    if ((self = [super initWithFrame:f])) {
        NSString *p = [NSBundle.mainBundle pathForResource:@"banner" ofType:@"png"];
        NSImage *title = p ? [[NSImage alloc] initWithContentsOfFile:p] : nil;
        if (title) _logo = pixel_crop(title, CGRectMake(58, 17, 206, 36), 8);   /* the title screen's logo box */
        NSArray *vol = @[@0.0, @0.1, @0.2, @0.3, @0.4, @0.5, @0.6, @0.7, @0.8, @0.9, @1.0];
        NSArray *volL = @[@"Off", @"10%", @"20%", @"30%", @"40%", @"50%", @"60%", @"70%", @"80%", @"90%", @"100%"];
        _top[0] = setting(@"Screen", @"StartFullScreen", @[@NO, @YES], @[@"Window", @"Full screen"]);
        _top[1] = setting(@"Game speed", @"Cycles", @[@10000, @20000, @40000], @[@"Original", @"Fast (2×)", @"Fastest (4×)"]);
        _top[2] = setting(@"Sound", @"Sound", @[@YES, @NO], @[@"On", @"Off"]);
        _top[3] = setting(@"Intro", @"PlayIntro", @[@YES, @NO], @[@"Play", @"Skip"]);
        _groups = @[@"Display", @"Sound", @"Controls", @"Files"];
        _display[0] = setting(@"Pixels", @"SmoothPixels", @[@NO, @YES], @[@"Sharp", @"Smooth"]);
        _display[1] = setting(@"Aspect", @"Stretch", @[@NO, @YES], @[@"4:3, as on a CRT", @"Fill the window"]);
        _display[2] = setting(@"Mouse pointer", @"ShowMacCursor", @[@NO, @YES], @[@"The game's own", @"The game's and the Mac's"]);
        _sound[0] = setting(@"Sound", @"Sound", @[@YES, @NO], @[@"On", @"Off"]);
        _sound[1] = setting(@"Effects volume", @"SoundVolume", vol, volL);
        _sound[2] = setting(@"Music volume", @"MusicVolume", vol, volL);
        self.open = -1; self.hover = HIT_NONE;
        NSDictionary *env = NSProcessInfo.processInfo.environment;
        if (env[@"TP_LAUNCHER_OPEN"]) {
            NSUInteger g = [_groups indexOfObject:[env[@"TP_LAUNCHER_OPEN"] capitalizedString]];
            self.open = g == NSNotFound || g == 3 ? -1 : (NSInteger)g;
        }
        self.focus = env[@"TP_LAUNCHER_FOCUS"] ? [env[@"TP_LAUNCHER_FOCUS"] integerValue] : -1;
    }
    return self;
}
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent *)e { return YES; }
- (BOOL)mouseDownCanMoveWindow { return NO; }

- (NSInteger)rowCount { return self.open == 0 || self.open == 1 ? 3 : self.open < 0 ? 4 : 0; }
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
- (void)set:(NSInteger)k to:(NSInteger)i {
    Setting *s = [self rowSetting:k];
    [NSUserDefaults.standardUserDefaults setObject:s->values[i] forKey:s->key];
    self.needsDisplay = YES;
}
- (void)step:(NSInteger)k by:(NSInteger)d {
    Setting *s = [self rowSetting:k];
    NSInteger n = (NSInteger)s->values.count;
    [self set:k to:([self valueIndex:s] + d + n) % n];
}

/* ---- layout ---- */
- (CGFloat)rowsTop { return self.open < 0 ? BANNER_H + 28 : BANNER_H + 84; }
- (NSRect)labelRect:(NSInteger)k { return NSMakeRect(44, self.rowsTop + 52 * k, 210, 30); }
- (NSRect)fieldRect:(NSInteger)k { return NSMakeRect(265, self.rowsTop + 52 * k, WIN_W - 265 - 44, 30); }
- (NSRect)groupRect:(NSInteger)g {
    CGFloat w = (WIN_W - 88 - 20) / 2;
    return NSMakeRect(44 + (g % 2) * (w + 20), BANNER_H + 252 + (g / 2) * 66, w, 48);
}
- (NSRect)quitRect { return NSMakeRect(WIN_W / 2 - 252, WIN_H - 116, 236, 48); }
- (NSRect)playRect { return NSMakeRect(WIN_W / 2 + 16, WIN_H - 116, 236, 48); }

- (NSInteger)hit:(NSPoint)p {
    if (NSPointInRect(p, self.playRect)) return HIT_PLAY;
    if (NSPointInRect(p, self.quitRect)) return self.open >= 0 ? HIT_BACK : HIT_QUIT;
    for (NSInteger k = 0; k < self.rowCount; k++) if (NSPointInRect(p, [self fieldRect:k])) return 200 + k;
    if (self.open < 0) for (NSInteger g = 0; g < 4; g++) if (NSPointInRect(p, [self groupRect:g])) return 300 + g;
    return HIT_NONE;
}
- (void)press:(NSInteger)h {
    if (h == HIT_PLAY) { self.result = YES; self.done = YES; }
    else if (h == HIT_QUIT) { self.result = NO; self.done = YES; }
    else if (h == HIT_BACK) { self.open = -1; self.focus = -1; }
    else if (h >= 300) {
        if (h == 303) [self openFiles];
        else { self.open = h - 300; self.focus = -1; }
    }
    else if (h >= 200) [self popUp:h - 200];
    self.needsDisplay = YES;
}
- (void)popUp:(NSInteger)k {   /* the field's choices as a menu, over the field like a pop-up button's */
    Setting *s = [self rowSetting:k];
    NSInteger cur = [self valueIndex:s];
    NSMenu *m = [NSMenu new]; m.autoenablesItems = NO; m.font = demi(15);
    for (NSUInteger i = 0; i < s->labels.count; i++) {
        NSMenuItem *it = [[NSMenuItem alloc] initWithTitle:s->labels[i] action:@selector(pick:) keyEquivalent:@""];
        it.target = self; it.tag = k * 100 + (NSInteger)i; it.state = (NSInteger)i == cur ? NSControlStateValueOn : NSControlStateValueOff;
        [m addItem:it];
    }
    NSRect f = [self fieldRect:k];
    m.minimumWidth = f.size.width;
    self.focus = k;
    [m popUpMenuPositioningItem:m.itemArray[cur] atLocation:NSMakePoint(f.origin.x, NSMaxY(f) - 6) inView:self];
}
- (void)pick:(NSMenuItem *)it { [self set:it.tag / 100 to:it.tag % 100]; }
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
    case 0x31: if (self.focus >= 0 && self.focus < rows) [self popUp:self.focus]; break;   /* space: the focused field's menu */
    case 0x35: [self press:self.open >= 0 ? HIT_BACK : HIT_QUIT]; break;
    case 0x7e: self.focus = self.focus < 0 ? items - 1 : (self.focus + items - 1) % MAX(items, 1); break;
    case 0x7d: self.focus = (self.focus + 1) % MAX(items, 1); break;
    case 0x7b: if (self.focus >= 0 && self.focus < rows) [self step:self.focus by:-1]; break;
    case 0x7c: if (self.focus >= 0 && self.focus < rows) [self step:self.focus by:1]; break;
    default: [super keyDown:e]; return;
    }
    self.needsDisplay = YES;
}

/* ---- drawing ---- */
- (void)button:(NSRect)r title:(NSString *)t primary:(BOOL)primary lit:(BOOL)lit focused:(BOOL)focused {
    NSColor *fill = primary ? (lit ? rgb(220, 66, 52) : ride_red()) : (lit ? panel_lit() : panel());
    [fill setFill]; NSRectFill(r);
    [gold() setStroke]; NSBezierPath *b = [NSBezierPath bezierPathWithRect:NSInsetRect(r, 0.75, 0.75)]; b.lineWidth = focused ? 3 : 1.5; [b stroke];
    label(t, heavy(primary ? 30 : 25), cream(), r, NSTextAlignmentCenter, 0);
}
- (void)drawWheelAt:(NSPoint)c radius:(CGFloat)r {   /* the banner's motif: a big wheel, faint like the other ports' emblems */
    NSColor *line = [gold() colorWithAlphaComponent:0.17];
    [line setStroke];
    NSBezierPath *legs = [NSBezierPath bezierPath]; legs.lineWidth = 7; legs.lineCapStyle = NSLineCapStyleRound;
    [legs moveToPoint:NSMakePoint(c.x - r * 0.62, BANNER_H - 2)]; [legs lineToPoint:c]; [legs lineToPoint:NSMakePoint(c.x + r * 0.62, BANNER_H - 2)];
    [legs stroke];
    NSBezierPath *rim = [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(c.x - r, c.y - r, 2 * r, 2 * r)]; rim.lineWidth = 6; [rim stroke];
    NSBezierPath *inner = [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(c.x - r * 0.82, c.y - r * 0.82, 1.64 * r, 1.64 * r)]; inner.lineWidth = 2; [inner stroke];
    NSBezierPath *spokes = [NSBezierPath bezierPath]; spokes.lineWidth = 2;
    for (int i = 0; i < 16; i++) {
        CGFloat a = i * M_PI / 8;
        [spokes moveToPoint:c]; [spokes lineToPoint:NSMakePoint(c.x + r * cos(a), c.y + r * sin(a))];
    }
    [spokes stroke];
    [[gold() colorWithAlphaComponent:0.17] setFill];
    [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(c.x - 10, c.y - 10, 20, 20)] fill];
    for (int i = 0; i < 8; i++) {   /* gondolas hang below the rim */
        CGFloat a = i * M_PI / 4 + M_PI / 8, x = c.x + r * cos(a), y = c.y + r * sin(a);
        [[NSBezierPath bezierPathWithRoundedRect:NSMakeRect(x - 13, y + 4, 26, 20) xRadius:5 yRadius:5] fill];
    }
}
- (void)drawBanner {
    NSRect br = NSMakeRect(0, 0, WIN_W, BANNER_H);
    NSGradient *g = [[NSGradient alloc] initWithStartingColor:rgb(16, 34, 22) endingColor:ground()];
    [g drawInRect:br angle:90];
    NSPoint c = NSMakePoint(WIN_W - 150, 108);
    [NSGraphicsContext saveGraphicsState];
    NSRectClip(br);
    [ray() setFill];                                        /* carnival rays from the wheel's hub */
    for (int i = 0; i < 12; i++) {
        CGFloat a = i * M_PI / 6, w = M_PI / 26;
        NSBezierPath *p = [NSBezierPath bezierPath];
        [p moveToPoint:c];
        [p lineToPoint:NSMakePoint(c.x + 1400 * cos(a - w), c.y + 1400 * sin(a - w))];
        [p lineToPoint:NSMakePoint(c.x + 1400 * cos(a + w), c.y + 1400 * sin(a + w))];
        [p closePath]; [p fill];
    }
    [self drawWheelAt:c radius:94];
    [NSGraphicsContext restoreGraphicsState];

    if (_logo) {   /* 206 x 36 game pixels at the CRT's 1.2 pixel aspect */
        NSRect dst = NSMakeRect(40, 56, 206 * 2, 36 * 2.4);
        NSShadow *s = [NSShadow new]; s.shadowColor = [NSColor colorWithWhite:0 alpha:0.6]; s.shadowOffset = NSMakeSize(0, -6); s.shadowBlurRadius = 14;
        [NSGraphicsContext saveGraphicsState]; [s set];
        [_logo drawInRect:dst fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1 respectFlipped:YES
                    hints:@{NSImageHintInterpolation: @(NSImageInterpolationHigh)}];
        [NSGraphicsContext restoreGraphicsState];
    } else {
        label(@"THEME", heavy(74), cream(), NSMakeRect(40, 34, 420, 70), NSTextAlignmentLeft, 0);
        label(@"PARK", heavy(74), gold(), NSMakeRect(40, 96, 420, 70), NSTextAlignmentLeft, 0);
    }
    label(@"NIGHTLY  ·  MAC NATIVE", mono(14, YES), cream(), NSMakeRect(44, 170, 400, 20), NSTextAlignmentLeft, 0.5);
    [gold() setFill]; NSRectFill(NSMakeRect(32, BANNER_H - 2, WIN_W - 64, 2));
}
- (void)drawRow:(NSInteger)k {
    Setting *s = [self rowSetting:k];
    NSRect f = [self fieldRect:k];
    BOOL lit = self.hover == 200 + k, focused = self.focus == k;
    label(s->title.uppercaseString, demi(17), gold(), [self labelRect:k], NSTextAlignmentLeft, 0.6);
    rounded(f, 6, [NSColor colorWithWhite:1 alpha:lit ? 0.13 : 0.08], focused ? gold() : nil, 1.5);
    label(s->labels[[self valueIndex:s]], medium(17), cream(), NSMakeRect(f.origin.x + 14, f.origin.y, f.size.width - 50, f.size.height), NSTextAlignmentLeft, 0.2);
    /* the up/down chevrons of a pop-up button */
    CGFloat x = NSMaxX(f) - 22, y = NSMidY(f);
    NSBezierPath *ch = [NSBezierPath bezierPath]; ch.lineWidth = 1.8; ch.lineCapStyle = NSLineCapStyleRound; ch.lineJoinStyle = NSLineJoinStyleRound;
    [ch moveToPoint:NSMakePoint(x - 4, y - 3)]; [ch lineToPoint:NSMakePoint(x, y - 7)]; [ch lineToPoint:NSMakePoint(x + 4, y - 3)];
    [ch moveToPoint:NSMakePoint(x - 4, y + 3)]; [ch lineToPoint:NSMakePoint(x, y + 7)]; [ch lineToPoint:NSMakePoint(x + 4, y + 3)];
    [cream() setStroke]; [ch stroke];
}
- (void)drawRect:(NSRect)dirty {
    [ground() setFill]; NSRectFill(self.bounds);
    [self drawBanner];
    NSString *hint;
    if (self.open >= 0) {
        NSString *title = self.open == 0 ? @"DISPLAY" : self.open == 1 ? @"SOUND" : @"CONTROLS";
        label(title, heavy(38), gold(), NSMakeRect(40, BANNER_H + 24, 400, 46), NSTextAlignmentLeft, 0);
    }
    if (self.open == 2) [self drawControls];
    for (NSInteger k = 0; k < self.rowCount; k++) [self drawRow:k];
    if (self.open < 0) {
        for (NSInteger gi = 0; gi < 4; gi++)
            [self button:[self groupRect:gi] title:_groups[gi] primary:NO lit:self.hover == 300 + gi focused:self.focus == self.rowCount + gi];
        hint = @"Original speed is the game as GOG's DOSBox runs it. Escape skips the intro.\nFiles opens your saved parks in the Finder.";
    } else if (self.open == 0)
        hint = @"Sharp keeps the game's 320 × 200 pixels crisp; Smooth softens them.\n4:3 shows the picture in the shape a 1994 monitor did.";
    else if (self.open == 1)
        hint = @"Effects come from the Sound Blaster, music from General MIDI\nplayed by the Mac's built-in synthesizer.";
    if (hint) text(hint, medium(16), cream(), NSMakeRect(44, self.open < 0 ? BANNER_H + 386 : BANNER_H + 262, WIN_W - 88, 48), NSTextAlignmentLeft, 0.1);
    [self button:self.quitRect title:self.open >= 0 ? @"BACK" : @"QUIT" primary:NO lit:self.hover == HIT_QUIT || self.hover == HIT_BACK focused:NO];
    [self button:self.playRect title:@"PLAY" primary:YES lit:self.hover == HIT_PLAY focused:NO];
    NSString *build = NSBundle.mainBundle.infoDictionary[@"CFBundleVersion"] ?: @"dev";
    label([NSString stringWithFormat:@"BUILD %@  ·  DOS RELEASE (GOG)", build], mono(12, NO), muted(), NSMakeRect(44, WIN_H - 42, 500, 18), NSTextAlignmentLeft, 0.3);
}
- (void)drawControls {
    NSArray *rows = @[
        @[@"Click", @"Select, build, place"], @[@"Right-click or Control-click", @"Cancel, rotate, close panels"],
        @[@"Mouse at a screen edge", @"Scroll the park"], @[@"Typing", @"Goes to the game as on a PC keyboard"],
        @[@"Escape", @"Skips the intro"], @[@"⌃⌘F", @"Full screen"], @[@"⌘Q", @"Quit (the game does not save on its own)"],
    ];
    for (NSUInteger i = 0; i < rows.count; i++) {
        NSRect r = NSMakeRect(44, BANNER_H + 92 + 36 * i, 260, 28);
        label([rows[i][0] uppercaseString], demi(16), gold(), r, NSTextAlignmentLeft, 0.5);
        label(rows[i][1], medium(17), cream(), NSMakeRect(300, r.origin.y, WIN_W - 344, 28), NSTextAlignmentLeft, 0.1);
    }
}
@end

/* Returns YES to play. */
BOOL tp_launcher_run(void) {
    NSRect frame = NSMakeRect(0, 0, WIN_W, WIN_H);
    TPLaunchView *v = [[TPLaunchView alloc] initWithFrame:frame];
    NSString *shot = NSProcessInfo.processInfo.environment[@"TP_LAUNCHER_SHOT"];
    if (shot) {                                             /* bounded preview render */
        v.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
        NSBitmapImageRep *rep = [v bitmapImageRepForCachingDisplayInRect:v.bounds];
        [v cacheDisplayInRect:v.bounds toBitmapImageRep:rep];
        [[rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:shot atomically:YES];
        return NO;
    }
    NSWindow *w = [[NSWindow alloc] initWithContentRect:frame
                                              styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskFullSizeContentView
                                                backing:NSBackingStoreBuffered defer:NO];
    w.releasedWhenClosed = NO; w.titlebarAppearsTransparent = YES; w.titleVisibility = NSWindowTitleHidden; w.movableByWindowBackground = YES;
    w.title = @"Theme Park"; w.backgroundColor = ground(); w.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
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
