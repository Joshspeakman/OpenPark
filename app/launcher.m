/* Theme Park's launch window, laid out like Dungeon Siege's (the other ports' launch window): the game's key art
 * across the top with its logo, a framed box of the main choices (each with a line saying what it does), big
 * buttons for the groups with a summary under each, and Quit/Play; a group opens as a panel over the window.
 * Dungeon Siege dresses it in its own stone and wood; here it is the park's grass and fairground lights.
 *
 * Art (Resources, put there by tools/build_app.sh, never in this project): keyart.jpg and logo.png are GOG's store
 * art for Theme Park, downloaded at install time for this Mac only; banner.png is the player's own title screen,
 * rendered by the compiled game, which gives the grass and, if the download failed, the logo. Without any of it the
 * window falls back to plain colours and type. Settings live in the app's user defaults and are read by main.m.
 * TP_LAUNCHER_SHOT=<png> renders the window to an image and exits (TP_LAUNCHER_OPEN=<group> with a group open,
 * TP_LAUNCHER_FOCUS=<n> with a row focused, TP_LAUNCHER_HOVER=<hit> with something under the pointer). */
#import <Cocoa/Cocoa.h>

static NSColor *rgb(int r, int g, int b) { return [NSColor colorWithSRGBRed:r / 255.0 green:g / 255.0 blue:b / 255.0 alpha:1]; }
static NSColor *gold(void) { return rgb(247, 201, 72); }
static NSColor *cream(void) { return rgb(250, 244, 226); }
static NSColor *note_c(void) { return rgb(196, 204, 176); }
static NSColor *bulb(void) { return rgb(255, 236, 150); }
static NSFont *display(CGFloat n) { return [NSFont fontWithName:@"Optima-ExtraBlack" size:n] ?: [NSFont boldSystemFontOfSize:n]; }
static NSFont *heavy(CGFloat n) { return [NSFont fontWithName:@"Optima-Bold" size:n] ?: [NSFont systemFontOfSize:n weight:NSFontWeightSemibold]; }
static NSFont *plain(CGFloat n) { return [NSFont fontWithName:@"Optima-Regular" size:n] ?: [NSFont systemFontOfSize:n]; }

static void label(NSString *s, NSFont *f, NSColor *c, NSRect r, NSTextAlignment al, CGFloat kern, BOOL shadow) {
    NSMutableParagraphStyle *p = [NSMutableParagraphStyle new]; p.alignment = al; p.lineBreakMode = NSLineBreakByTruncatingTail;
    NSMutableDictionary *a = [@{NSFontAttributeName: f, NSForegroundColorAttributeName: c, NSParagraphStyleAttributeName: p, NSKernAttributeName: @(kern)} mutableCopy];
    if (shadow) { NSShadow *sh = [NSShadow new]; sh.shadowColor = [NSColor colorWithWhite:0 alpha:0.85]; sh.shadowOffset = NSMakeSize(0, -1.5); sh.shadowBlurRadius = 3; a[NSShadowAttributeName] = sh; }
    CGFloat h = f.ascender - f.descender;
    [s drawInRect:NSMakeRect(r.origin.x, NSMidY(r) - h / 2, r.size.width, h + 2) withAttributes:a];
}
static void rounded(NSRect r, CGFloat rad, NSColor *fill, NSColor *line, CGFloat w) {
    NSBezierPath *b = [NSBezierPath bezierPathWithRoundedRect:r xRadius:rad yRadius:rad];
    if (fill) { [fill setFill]; [b fill]; }
    if (line) { [line setStroke]; b.lineWidth = w; [b stroke]; }
}
/* `r` (top-down pixels) of an image enlarged k times with hard pixel edges */
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
/* the part of an image with any opacity (GOG's logo comes with wide margins) */
static NSImage *trimmed(NSImage *img) {
    CGImageRef c = [img CGImageForProposedRect:NULL context:nil hints:nil]; if (!c) return img;
    size_t w = CGImageGetWidth(c), h = CGImageGetHeight(c); uint8_t *px = calloc(w * h, 4);
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB(); CGContextRef g = CGBitmapContextCreate(px, w, h, 8, w * 4, cs, (CGBitmapInfo)kCGImageAlphaPremultipliedLast);
    CGContextDrawImage(g, CGRectMake(0, 0, w, h), c);
    size_t x0 = w, y0 = h, x1 = 0, y1 = 0;
    for (size_t y = 0; y < h; y++) for (size_t x = 0; x < w; x++) if (px[(y * w + x) * 4 + 3] > 24) { if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; }
    CGContextRelease(g); CGColorSpaceRelease(cs); free(px);
    if (x1 <= x0 || y1 <= y0) return img;
    CGImageRef t = CGImageCreateWithImageInRect(c, CGRectMake(x0, y0, x1 - x0 + 1, y1 - y0 + 1));
    NSImage *r = [[NSImage alloc] initWithCGImage:t size:NSMakeSize(x1 - x0 + 1, y1 - y0 + 1)]; CGImageRelease(t); return r;
}
/* fairground bulbs along a rectangle's edges, every `step` points */
static void bulbs(NSRect r, CGFloat step, BOOL sides) {
    NSMutableArray<NSValue *> *pts = [NSMutableArray array];
    NSInteger nx = MAX(1, (NSInteger)lround(r.size.width / step)), ny = MAX(1, (NSInteger)lround(r.size.height / step));
    for (NSInteger i = 0; i <= nx; i++) {
        CGFloat x = r.origin.x + r.size.width * i / nx;
        [pts addObject:[NSValue valueWithPoint:NSMakePoint(x, r.origin.y)]]; [pts addObject:[NSValue valueWithPoint:NSMakePoint(x, NSMaxY(r))]];
    }
    if (sides) for (NSInteger i = 1; i < ny; i++) {
        CGFloat y = r.origin.y + r.size.height * i / ny;
        [pts addObject:[NSValue valueWithPoint:NSMakePoint(r.origin.x, y)]]; [pts addObject:[NSValue valueWithPoint:NSMakePoint(NSMaxX(r), y)]];
    }
    for (NSValue *v in pts) {
        NSPoint p = v.pointValue;
        NSGradient *glow = [[NSGradient alloc] initWithStartingColor:[bulb() colorWithAlphaComponent:0.55] endingColor:[bulb() colorWithAlphaComponent:0]];
        [glow drawFromCenter:p radius:0 toCenter:p radius:8 options:0];
        [rgb(120, 70, 20) setFill]; [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(p.x - 3.6, p.y - 3.6, 7.2, 7.2)] fill];
        [rgb(255, 250, 215) setFill]; [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(p.x - 2.6, p.y - 2.6, 5.2, 5.2)] fill];
    }
}

/* ---- settings ---- */
typedef struct { NSString *title, *key; NSArray *values; NSArray<NSString *> *labels, *notes; BOOL slider; } Setting;
static Setting setting(NSString *t, NSString *k, NSArray *v, NSArray *l, NSArray *n) { Setting s = {t, k, v, l, n, NO}; return s; }
/* A slider: the values on a track under the value (drag or click it; the arrows still step) */
static Setting slider(NSString *t, NSString *k, NSArray *v, NSArray *l, NSArray *n) { Setting s = {t, k, v, l, n, YES}; return s; }

/* ---- music: the game's tunes and its ride types (the type table at 0x1cced0, in order) ---- */
static NSString *const ride_types[29] = {@"Flight Sim", @"Tree House", @"Pirate Boat", @"Plane Flyer", @"Maze", @"Ghost House",
    @"Haunted House", @"Rubber Tubing", @"Race Car Ride", @"Roller Coaster", @"Loop the Loop", @"Cork Screw", @"Big Wheel",
    @"Merry-Go-Round", @"Bouncy Castle", @"Space Shuttle", @"Big Dipper", @"Water Splash", @"Monorail", @"Snakes and Ladders",
    @"Observation Tower", @"Parasol Chairs", @"Cowboy Acts", @"Clown Acts", @"Dolphin Acts", @"Medieval Acts", @"Super Spinner",
    @"Observatory", @"Planet Rocket"};
static const int ride_tune[29] = {13, 5, 6, 12, 5, 4, 4, 9, 15, 2, 0, 0, 1, 1, 15, 13, 14, 0, 17, 11, 12, 17, 7, 8, 16, 10, 12, 3, 1};
NSString *tp_tune_name(int t) {
    static NSString *const jingles[5] = {@"Heartbeat", @"Fanfare", @"Takeover", @"Winner", @"Funeral"};
    return t <= 0 ? @"None" : t <= 17 ? [NSString stringWithFormat:@"Ride Tune %d", t] : jingles[t - 18];
}
NSString *tp_tune_note(int t) {                         /* which rides play it in the game */
    if (t > 17) return @"One of the game's jingles, played on and on";
    NSMutableArray *who = [NSMutableArray array];
    for (int i = 0; i < 29; i++) if (ride_tune[i] == t) [who addObject:ride_types[i]];
    return who.count ? [@"As played by " stringByAppendingString:[who componentsJoinedByString:@", "]] : @"A tune no ride plays in the game";
}
/* Per ride type: the game's tune, none, or another (RideTunes: {type index: tune, 0 none}). */
void tp_ride_tunes_window(NSWindow *parent) {
    NSDictionary *cur = [NSUserDefaults.standardUserDefaults dictionaryForKey:@"RideTunes"] ?: @{};
    NSPanel *w = [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, 520, 560) styleMask:NSWindowStyleMaskTitled
                                              backing:NSBackingStoreBuffered defer:NO];
    w.title = @"Ride Tunes";
    NSView *content = w.contentView;
    NSScrollView *sv = [[NSScrollView alloc] initWithFrame:NSMakeRect(20, 60, 480, 480)];
    sv.hasVerticalScroller = YES; sv.borderType = NSBezelBorder;
    NSView *doc = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 460, 29 * 30 + 10)];
    NSMutableArray<NSPopUpButton *> *pops = [NSMutableArray array];
    for (int i = 0; i < 29; i++) {
        CGFloat y = doc.frame.size.height - 36 - i * 30;
        NSTextField *l = [NSTextField labelWithString:ride_types[i]]; l.frame = NSMakeRect(12, y + 3, 180, 20); [doc addSubview:l];
        NSPopUpButton *pb = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(200, y, 250, 26) pullsDown:NO];
        [pb addItemWithTitle:ride_tune[i] ? [NSString stringWithFormat:@"The Game's (%@)", tp_tune_name(ride_tune[i])] : @"The Game's (none)"];
        [pb addItemWithTitle:@"None"];
        for (int t = 1; t <= 22; t++) [pb addItemWithTitle:tp_tune_name(t)];
        NSNumber *v = cur[[NSString stringWithFormat:@"%d", i]];
        [pb selectItemAtIndex:v ? [v intValue] + 1 : 0];
        [doc addSubview:pb]; [pops addObject:pb];
    }
    sv.documentView = doc; [content addSubview:sv];
    [doc scrollPoint:NSMakePoint(0, doc.frame.size.height)];
    NSButton *done = [NSButton buttonWithTitle:@"Done" target:NSApp action:@selector(stopModal)];
    done.frame = NSMakeRect(410, 16, 90, 30); done.keyEquivalent = @"\r"; [content addSubview:done];
    NSButton *reset = [NSButton buttonWithTitle:@"All The Game's" target:nil action:nil];
    reset.frame = NSMakeRect(20, 16, 150, 30); [content addSubview:reset];
    NSBlockOperation *resetAll = [NSBlockOperation blockOperationWithBlock:^{ for (NSPopUpButton *pb in pops) [pb selectItemAtIndex:0]; }];
    reset.target = resetAll; reset.action = @selector(main);       /* (a button's target is weak: resetAll lives to the end) */
    [w center];
    [NSApp runModalForWindow:w];
    (void)resetAll;
    NSMutableDictionary *out = [NSMutableDictionary dictionary];
    for (int i = 0; i < 29; i++) { NSInteger k = pops[i].indexOfSelectedItem; if (k > 0) out[[NSString stringWithFormat:@"%d", i]] = @(k - 1); }
    if (out.count) [NSUserDefaults.standardUserDefaults setObject:out forKey:@"RideTunes"];
    else [NSUserDefaults.standardUserDefaults removeObjectForKey:@"RideTunes"];
    [w orderOut:nil];
    (void)parent;
}

@interface TPLaunchView : NSView
@property NSInteger focus, hover, open;   /* open: -1 or group index */
@property NSInteger dragRow;              /* a slider being dragged, or -1 */
@property BOOL result, done;
@end

enum { HIT_NONE = -1, HIT_PLAY = 1000, HIT_QUIT = 1001, HIT_BACK = 1002 };
#define WIN_W 780
#define WIN_H 760
#define BANNER_H 250

@implementation TPLaunchView {
    NSImage *_keyart, *_logo, *_grass;
    Setting _top[3];
    NSArray<NSString *> *_groups;
    Setting _display[7], _sound[8], _tweaks[7], _free[5];
}
- (instancetype)initWithFrame:(NSRect)f {
    if ((self = [super initWithFrame:f])) {
        NSBundle *b = NSBundle.mainBundle; NSString *p;
        if ((p = [b pathForResource:@"keyart" ofType:@"jpg"])) _keyart = [[NSImage alloc] initWithContentsOfFile:p];
        if ((p = [b pathForResource:@"logo" ofType:@"png"])) _logo = trimmed([[NSImage alloc] initWithContentsOfFile:p]);
        NSImage *title = (p = [b pathForResource:@"banner" ofType:@"png"]) ? [[NSImage alloc] initWithContentsOfFile:p] : nil;
        if (title) {
            _grass = pixel_crop(title, CGRectMake(272, 64, 40, 40), 2);           /* the title screen's lawn */
            if (!_logo) _logo = pixel_crop(title, CGRectMake(58, 17, 206, 36), 8); /* its logo box, if GOG's art is missing */
        }
        if (_keyart.size.width < 400) _keyart = nil;
        NSArray *vol = @[@0.0, @0.1, @0.2, @0.3, @0.4, @0.5, @0.6, @0.7, @0.8, @0.9, @1.0];
        NSArray *volL = @[@"Off", @"10%", @"20%", @"30%", @"40%", @"50%", @"60%", @"70%", @"80%", @"90%", @"100%"];
        NSArray *volN = @[@"", @"", @"", @"", @"", @"", @"", @"", @"", @"", @""];
        _top[0] = setting(@"Game Speed", @"Cycles", @[@10000, @12500, @15000, @20000, @40000], @[@"Original", @"1.25×", @"1.5×", @"2×", @"4×"],
                          @[@"Exactly as GOG's DOSBox runs it", @"A quarter faster than GOG's DOSBox", @"Half again as fast as GOG's DOSBox",
                            @"Twice GOG's DOSBox", @"Four times: the seasons fly by"]);
        _top[2] = setting(@"Language", @"Language", @[@0, @1, @2, @3, @4], @[@"English", @"Deutsch", @"Italiano", @"Español", @"Français"],
                          @[@"The game's text and advisors", @"Texte und Berater des Spiels", @"Testi e consiglieri del gioco",
                            @"Textos y asesores del juego", @"Textes et conseillers du jeu"]);
        _top[1] = setting(@"Intro", @"PlayIntro", @[@YES, @NO], @[@"Play", @"Skip"],
                          @[@"The 1994 intro movie; Escape skips it", @"Straight to the title screen"]);
        _groups = @[@"Display", @"Sound", @"Controls", @"Files", @"Tweaks", @"Free Mode"];
        _display[0] = setting(@"Screen", @"StartFullScreen", @[@NO, @YES], @[@"Window", @"Full Screen"],
                              @[@"A window you can resize; ⌃⌘F switches", @"The whole display; ⌃⌘F switches"]);
        _display[1] = setting(@"Pixels", @"SmoothPixels", @[@0, @1, @2], @[@"Sharp", @"Smooth", @"CRT"],
                              @[@"Square pixels, as the game drew them", @"Softened as they are scaled up", @"Soft, with a 1994 monitor's scanlines"]);
        _display[2] = setting(@"Aspect", @"Stretch", @[@NO, @YES], @[@"4:3", @"Fill the Window"],
                              @[@"The shape a 1994 monitor gave it", @"Stretched to the window's shape"]);
        _sound[0] = setting(@"Sound", @"Sound", @[@YES, @NO], @[@"On", @"Off"],
                            @[@"Sound Blaster effects and General MIDI music", @"The park is silent"]);
        _sound[7] = setting(@"Music Instruments", @"SoundBank", @[@""], @[@"Built-in"],
                            @[@"The Mac's synthesizer; → picks a SoundFont"]);
        _sound[1] = slider(@"Effects Volume", @"SoundVolume", vol, volL, volN);
        _sound[6] = slider(@"Music Volume", @"MusicVolume", vol, volL, volN);
        _sound[2] = slider(@"Game Sounds", @"GameSoundVolume", vol, volL, volN);     /* (qol.c: rides, people, the ew, purchases) */
        _sound[5] = slider(@"System Sounds", @"SystemSoundVolume", vol, volL, volN); /* (the button clicks) */
        NSArray *lv = @[@0.0, @0.25, @0.5, @0.75, @1.0], *lvL = @[@"Off", @"Quarter", @"Half", @"Three Quarters", @"Full"];   /* (as the game's EXTRAS menu) */
        _sound[3] = slider(@"Ew Sounds", @"EwVolume", lv, lvL, @[@"Visitors keep their disgust to themselves", @"The \"ew\" at a quarter of the game sounds",
                           @"The \"ew\" at half the game sounds", @"The \"ew\" at three quarters of the game sounds", @"The \"ew\" as loud as the other game sounds"]);
        _sound[4] = slider(@"Purchase Sounds", @"PurchaseVolume", lv, lvL, @[@"Visitors pay quietly", @"The till at a quarter of the game sounds",
                           @"The till at half the game sounds", @"The till at three quarters of the game sounds", @"The till as loud as the other game sounds"]);
        _display[5] = setting(@"Start In", @"StartHiRes", @[@NO, @YES], @[@"320 × 200", @"640 × 480"],
                              @[@"The game's usual screen; R in the park switches", @"The game's SVGA mode: more of the park at once"]);
        _display[4] = setting(@"Wide Windows", @"Widescreen", @[@YES, @NO], @[@"More Park", @"Black Bars"],
                              @[@"The park fills the window; the panel stays centred", @"The game's 4:3 picture, as on a CRT"]);
        _display[6] = setting(@"Zoomed Drawing", @"NativePasses", @[@YES, @NO], @[@"Native", @"The Game's"],
                              @[@"Zoomed-out views drawn by the new engine: faster", @"Zoomed-out views drawn by the game's own code"]);
        _tweaks[6] = slider(@"Panel Size", @"PanelScale", @[@0.5, @0.75, @1.0, @1.25, @1.5, @1.75, @2.0], @[@"0.5×", @"0.75×", @"The Game's", @"1.25×", @"1.5×", @"1.75×", @"2×"],
                              @[@"The panel and the advisor half size, more park in view (wide windows)", @"The panel and the advisor three quarters size (wide windows)",
                                @"The panel and the advisor as drawn in 1994", @"The panel and the advisor a quarter larger (wide windows)",
                                @"The panel and the advisor half as large again (wide windows)", @"The panel and the advisor 1.75 times as large (wide windows)",
                                @"The panel and the advisor twice as large (wide windows)"]);
        _display[3] = setting(@"Mouse Pointer", @"ShowMacCursor", @[@NO, @YES], @[@"The Game's", @"Both"],
                            @[@"The Mac's pointer hides over the game", @"The Mac's pointer stays visible too"]);
        /* Tweaks: the whole game (runtime/qol.c). Free Mode: sandbox parks only. */
        NSMutableArray *tunes = [NSMutableArray arrayWithObject:@0], *tuneL = [NSMutableArray arrayWithObject:@"None"],
                       *tuneN = [NSMutableArray arrayWithObject:@"Quiet where no ride's music plays"];
        for (int t = 1; t <= 22; t++) { [tunes addObject:@(t)]; [tuneL addObject:tp_tune_name(t)]; [tuneN addObject:tp_tune_note(t)]; }
        _tweaks[0] = setting(@"Grossed-Out Sounds", @"GrossOutGap", @[@0, @5, @15, @30, @-1], @[@"Every Time", @"Every 5 s", @"Every 15 s", @"Every 30 s", @"Off"],
                             @[@"The \"ew\" each time a visitor sees a mess, as in 1994", @"At most one \"ew\" every 5 seconds",
                               @"At most one \"ew\" every 15 seconds", @"At most one \"ew\" every 30 seconds", @"Visitors keep their disgust to themselves"]);
        _tweaks[1] = setting(@"Purchase Sounds", @"PurchaseGap", @[@0, @5, @15, @30, @-1], @[@"Every Time", @"Every 5 s", @"Every 15 s", @"Every 30 s", @"Off"],
                             @[@"The till rings for every purchase on screen", @"At most one till every 5 seconds",
                               @"At most one till every 15 seconds", @"At most one till every 30 seconds", @"Visitors pay quietly"]);
        _tweaks[2] = setting(@"Ride Music", @"RideMusic", @[@YES, @NO], @[@"On", @"Off"],
                             @[@"Running rides on screen play their tunes", @"No ride tunes (the game's fanfares still play)"]);
        _tweaks[3] = setting(@"Ride Tunes", @"RideTunes", @[@""], @[@"The Game's"], @[@"Each ride plays its own tune; → chooses per ride"]);
        _tweaks[4] = setting(@"Park Music", @"ParkTune", tunes, tuneL, tuneN);
        _tweaks[5] = setting(@"Frame Pacing", @"SteadyPacing", @[@YES, @NO], @[@"Steady", @"Like DOSBox"],
                             @[@"Every frame the same length, in step with the display", @"The game's speed follows how busy the screen is, as in 1994"]);
        _free[0] = setting(@"Rules", @"FreeRules", @[@0, @1, @2], @[@"Sandbox", @"Endless", @"True Freeplay"],
                           @[@"The game's sandbox, at the settings you choose", @"Starts as easy as the first parks and grows to the hardest; research as normal",
                             @"Every ride and shop researched, and money that never runs out"]);
        _free[1] = setting(@"Endless Ramp", @"FreeRamp", @[@36, @60, @120], @[@"3 Years", @"5 Years", @"10 Years"],
                           @[@"Endless parks reach the hardest in three game years", @"Endless parks reach the hardest in five game years",
                             @"Endless parks reach the hardest in ten game years"]);
        _free[2] = setting(@"Visitors", @"FreeVisitors", @[@0, @1000, @2500, @5000, @10000], @[@"The Game's", @"1,000", @"2,500", @"5,000", @"10,000"],
                           @[@"800 in a 64 × 64 park, 10,000 in a 128 × 128 one", @"Up to 1,000, about all a 64 × 64 park can hold",
                             @"Up to 2,500 in a 128 × 128 park (1,000 in a 64 × 64 one)", @"Up to 5,000 in a 128 × 128 park (1,000 in a 64 × 64 one)",
                             @"Up to 10,000 in a 128 × 128 park (1,000 in a 64 × 64 one)"]);
        _free[4] = setting(@"Map Size", @"FreeMapSize", @[@64, @128], @[@"64 × 64", @"128 × 128"],
                           @[@"The game's park size", @"Four times the land for new sandbox parks: buses bring four times the visitors, who stay twice as long"]);
        _free[3] = setting(@"Bus Size", @"FreeBus", @[@0, @60, @100, @150, @255], @[@"The Game's", @"60", @"100", @"150", @"255"],
                           @[@"20 to 150 a bus, growing with the park", @"At least 60 visitors a bus", @"At least 100 visitors a bus",
                             @"At least 150 visitors a bus", @"Up to 255 visitors a bus, the most it can carry"]);
        self.open = -1; self.hover = HIT_NONE; self.focus = -1; self.dragRow = -1;
        NSDictionary *env = NSProcessInfo.processInfo.environment;
        if (env[@"TP_LAUNCHER_OPEN"]) {
            NSUInteger g = [_groups indexOfObject:[env[@"TP_LAUNCHER_OPEN"] capitalizedString]];
            self.open = g == NSNotFound || g == 3 ? -1 : (NSInteger)g;
        }
        if (env[@"TP_LAUNCHER_FOCUS"]) self.focus = [env[@"TP_LAUNCHER_FOCUS"] integerValue];
        if (env[@"TP_LAUNCHER_HOVER"]) self.hover = [env[@"TP_LAUNCHER_HOVER"] integerValue];
    }
    return self;
}
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent *)e { return YES; }
- (BOOL)mouseDownCanMoveWindow { return NO; }

- (NSInteger)rowCount { return self.open == 0 ? 7 : self.open == 1 ? 8 : self.open == 4 ? 7 : self.open == 5 ? 5 : self.open < 0 ? 3 : 0; }
- (Setting *)rowSetting:(NSInteger)k {
    if (self.open == 0) return &_display[k];
    if (self.open == 1) return &_sound[k];
    if (self.open == 4) return &_tweaks[k];
    if (self.open == 5) return &_free[k];
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
- (NSString *)valueOf:(Setting *)s {
    if ([s->key isEqual:@"RideTunes"]) {
        NSUInteger n = [[NSUserDefaults.standardUserDefaults dictionaryForKey:@"RideTunes"] count];
        return n ? [NSString stringWithFormat:@"%lu Changed", (unsigned long)n] : @"The Game's";
    }
    if ([s->key isEqual:@"SoundBank"]) {                 /* a file the player chose, or the built-in synth */
        NSString *b = [NSUserDefaults.standardUserDefaults stringForKey:@"SoundBank"];
        return b.length ? b.lastPathComponent.stringByDeletingPathExtension : @"Built-in";
    }
    return s->labels[[self valueIndex:s]];
}
- (NSString *)noteOf:(Setting *)s {
    if ([s->key isEqual:@"RideTunes"])
        return [[NSUserDefaults.standardUserDefaults dictionaryForKey:@"RideTunes"] count] ? @"→ changes them; ← puts every ride back" : s->notes[0];
    if ([s->key isEqual:@"SoundBank"]) {
        NSString *b = [NSUserDefaults.standardUserDefaults stringForKey:@"SoundBank"];
        return b.length ? @"Your SoundFont for the music; ← goes back to built-in" : s->notes[0];
    }
    return s->notes[[self valueIndex:s]];
}
- (void)step:(NSInteger)k by:(NSInteger)d {
    Setting *s = [self rowSetting:k];
    if ([s->key isEqual:@"RideTunes"]) {                 /* → the per-ride window, ← back to the game's */
        if (d < 0) [NSUserDefaults.standardUserDefaults removeObjectForKey:@"RideTunes"]; else tp_ride_tunes_window(self.window);
        self.needsDisplay = YES;
        return;
    }
    if ([s->key isEqual:@"SoundBank"]) {                 /* → opens a file, ← clears it */
        if (d < 0) [NSUserDefaults.standardUserDefaults removeObjectForKey:@"SoundBank"];
        else {
            NSOpenPanel *op = [NSOpenPanel openPanel];
            op.message = @"Choose a SoundFont (.sf2) or DLS file for the music";
            op.allowedFileTypes = @[@"sf2", @"dls", @"SF2", @"DLS"];
            if ([op runModal] == NSModalResponseOK) [NSUserDefaults.standardUserDefaults setObject:op.URL.path forKey:@"SoundBank"];
        }
        self.needsDisplay = YES;
        return;
    }
    NSInteger n = (NSInteger)s->values.count;
    [NSUserDefaults.standardUserDefaults setObject:s->values[([self valueIndex:s] + d + n) % n] forKey:s->key];
    self.needsDisplay = YES;
}

/* ---- layout ---- */
- (NSRect)topFrame { return NSMakeRect(56, BANNER_H + 22, WIN_W - 112, 3 * 63 + 14); }
- (NSRect)panel { return NSMakeRect(42, 132, WIN_W - 84, 512); }
- (NSRect)rowRect:(NSInteger)k {
    if (self.open < 0) { NSRect f = self.topFrame; return NSMakeRect(f.origin.x + 10, f.origin.y + 7 + 63 * k, f.size.width - 20, 63); }
    NSRect p = self.panel; CGFloat pitch = self.rowCount > 7 ? 54 : self.rowCount > 6 ? 60 : self.rowCount > 5 ? 69 : self.rowCount > 4 ? 70 : 74;
    return NSMakeRect(p.origin.x + 26, p.origin.y + (self.compact ? 74 : 80) + pitch * k, p.size.width - 52, self.compact ? 54 : 70);
}
/* Eight rows (Sound): each drawn closer, its value, note and slider a few points higher. */
- (BOOL)compact { return self.open >= 0 && self.rowCount > 7; }
- (CGFloat)rowTop { return self.compact ? 4 : 10; }
- (NSRect)arrow:(NSInteger)k right:(BOOL)right {
    NSRect r = [self rowRect:k];
    return NSMakeRect(right ? NSMaxX(r) - 46 : NSMaxX(r) - 326, r.origin.y + self.rowTop, 34, 30);
}
- (NSRect)trackRect:(NSInteger)k {
    NSRect la = [self arrow:k right:NO], ra = [self arrow:k right:YES], r = [self rowRect:k];
    return NSMakeRect(NSMaxX(la) + 16, r.origin.y + (self.compact ? 36 : 46), ra.origin.x - NSMaxX(la) - 32, 10);
}
- (void)slide:(NSInteger)k to:(NSPoint)p {
    Setting *s = [self rowSetting:k]; NSRect t = [self trackRect:k]; NSInteger n = (NSInteger)s->values.count;
    double f = (p.x - t.origin.x) / t.size.width; NSInteger i = (NSInteger)lround(fmin(fmax(f, 0), 1) * (n - 1));
    if (i != [self valueIndex:s]) { [NSUserDefaults.standardUserDefaults setObject:s->values[i] forKey:s->key]; self.needsDisplay = YES; }
}
- (NSRect)groupRect:(NSInteger)g {                      /* three across: Display Sound Tweaks / Free Mode Controls Files */
    static const int slot[6] = {0, 1, 4, 5, 2, 3};
    int k = slot[g], w = 212, gap = 16, x0 = (WIN_W - 3 * w - 2 * gap) / 2;
    return NSMakeRect(x0 + (k % 3) * (w + gap), BANNER_H + 240 + (k / 3) * 84, w, 54);
}
- (NSRect)quitRect { return NSMakeRect(WIN_W / 2 - 14 - 230, WIN_H - 92, 230, 58); }
- (NSRect)playRect { return NSMakeRect(WIN_W / 2 + 14, WIN_H - 92, 230, 58); }

- (NSInteger)hit:(NSPoint)p {
    if (NSPointInRect(p, self.playRect)) return HIT_PLAY;
    if (NSPointInRect(p, self.quitRect)) return self.open >= 0 ? HIT_BACK : HIT_QUIT;
    for (NSInteger k = 0; k < self.rowCount; k++) {
        if ([self rowSetting:k]->slider && NSPointInRect(p, NSInsetRect([self trackRect:k], -6, -8))) return 400 + k;
        if (NSPointInRect(p, NSInsetRect([self arrow:k right:NO], -6, -6))) return 100 + k;
        if (NSPointInRect(p, [self rowRect:k])) return 200 + k;
    }
    if (self.open < 0) for (NSInteger g = 0; g < 6; g++) if (NSPointInRect(p, [self groupRect:g])) return 300 + g;
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
    else if (h >= 200) { self.focus = h - 200; [self step:h - 200 by:1]; }
    else if (h >= 100) { self.focus = h - 100; [self step:h - 100 by:-1]; }
    self.needsDisplay = YES;
}
- (void)openFiles {
    NSString *env = NSProcessInfo.processInfo.environment[@"TP_DATA"];
    NSString *root = env.length ? env : [NSHomeDirectory() stringByAppendingPathComponent:@"Games/ThemeParkNative"];
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
- (void)mouseDown:(NSEvent *)e {
    NSPoint p = [self convertPoint:e.locationInWindow fromView:nil]; NSInteger h = [self hit:p];
    if (h >= 400 && h < 500) { self.focus = h - 400; self.dragRow = h - 400; [self slide:h - 400 to:p]; return; }
    self.dragRow = -1; [self press:h];
}
- (void)mouseDragged:(NSEvent *)e { if (self.dragRow >= 0) [self slide:self.dragRow to:[self convertPoint:e.locationInWindow fromView:nil]]; }
- (void)mouseUp:(NSEvent *)e { self.dragRow = -1; }
- (void)keyDown:(NSEvent *)e {
    NSInteger rows = self.rowCount, items = rows + (self.open < 0 ? 6 : 0);
    switch (e.keyCode) {
    case 0x24: case 0x4c:                                  /* return: play, or press the focused group */
        if (self.focus >= rows && self.open < 0) [self press:300 + self.focus - rows]; else [self press:HIT_PLAY]; break;
    case 0x35: [self press:self.open >= 0 ? HIT_BACK : HIT_QUIT]; break;
    case 0x7e: self.focus = self.focus < 0 ? items - 1 : (self.focus + items - 1) % MAX(items, 1); break;
    case 0x7d: self.focus = (self.focus + 1) % MAX(items, 1); break;
    case 0x7b: if (self.focus >= 0 && self.focus < rows) [self step:self.focus by:-1]; break;
    case 0x7c: case 0x31: if (self.focus >= 0 && self.focus < rows) [self step:self.focus by:1]; break;
    default: [super keyDown:e]; return;
    }
    self.needsDisplay = YES;
}

/* ---- drawing ---- */
- (void)fillGrass:(NSRect)r darken:(CGFloat)k {
    [rgb(10, 18, 8) setFill]; NSRectFill(r);
    if (_grass) {
        [NSGraphicsContext saveGraphicsState]; NSRectClip(r);
        CGFloat t = 80;                                     /* 40 game pixels at 2 points each */
        for (CGFloat y = floor(r.origin.y / t) * t; y < NSMaxY(r); y += t)
            for (CGFloat x = floor(r.origin.x / t) * t; x < NSMaxX(r); x += t)
                [_grass drawInRect:NSMakeRect(x, y, t, t) fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1 respectFlipped:YES
                             hints:@{NSImageHintInterpolation: @(NSImageInterpolationNone)}];
        [[NSColor colorWithWhite:0 alpha:k] setFill]; NSRectFillUsingOperation(r, NSCompositingOperationSourceOver);
        [NSGraphicsContext restoreGraphicsState];
    }
}
- (void)sign:(NSRect)r title:(NSString *)t primary:(BOOL)primary state:(int)state {   /* state: 0 idle, 1 hover, 2 focused */
    NSShadow *sh = [NSShadow new]; sh.shadowColor = [NSColor colorWithWhite:0 alpha:0.7]; sh.shadowOffset = NSMakeSize(0, -4); sh.shadowBlurRadius = 8;
    [NSGraphicsContext saveGraphicsState]; [sh set]; rounded(r, 10, rgb(60, 14, 10), nil, 0); [NSGraphicsContext restoreGraphicsState];
    NSColor *top = state ? rgb(222, 64, 48) : rgb(190, 42, 32), *bottom = state ? rgb(150, 28, 22) : rgb(118, 20, 16);
    NSGradient *g = [[NSGradient alloc] initWithStartingColor:top endingColor:bottom];
    [g drawInBezierPath:[NSBezierPath bezierPathWithRoundedRect:r xRadius:10 yRadius:10] angle:90];
    rounded(NSInsetRect(r, 1, 1), 9, nil, rgb(255, 214, 120), state == 2 ? 3 : 1.5);
    rounded(NSInsetRect(r, 7, 7), 5, nil, [rgb(255, 214, 120) colorWithAlphaComponent:0.45], 1);
    bulbs(NSInsetRect(r, 7, 7), 22, NO);
    label(t.uppercaseString, display(primary ? 25 : 21), primary ? gold() : cream(), NSInsetRect(r, 10, 0), NSTextAlignmentCenter, 2, YES);
}
- (void)drawBanner {
    NSRect br = NSMakeRect(0, 0, WIN_W, BANNER_H);
    if (_keyart) {   /* the riders on the right, the sky on the left for the logo */
        NSSize s = _keyart.size; CGFloat k = MAX(br.size.width / s.width, br.size.height / s.height), w = br.size.width / k, h = br.size.height / k;
        NSRect from = NSMakeRect(MAX(0, s.width * 0.83 - w), (s.height - h) * 0.5, w, h);   /* GOG's art fades out on its right */
        [_keyart drawInRect:br fromRect:from operation:NSCompositingOperationCopy fraction:1 respectFlipped:YES hints:@{NSImageHintInterpolation: @(NSImageInterpolationHigh)}];
        NSGradient *left = [[NSGradient alloc] initWithColorsAndLocations:[rgb(16, 70, 150) colorWithAlphaComponent:0.55], 0.0, [rgb(16, 70, 150) colorWithAlphaComponent:0.0], 0.55, nil];
        [left drawInRect:br angle:0];
    } else {
        NSGradient *sky = [[NSGradient alloc] initWithStartingColor:rgb(40, 120, 210) endingColor:rgb(20, 70, 150)];
        [sky drawInRect:br angle:90];
    }
    NSGradient *fade = [[NSGradient alloc] initWithColorsAndLocations:[NSColor colorWithWhite:0 alpha:0], 0.0, [NSColor colorWithWhite:0 alpha:0], 0.72, [NSColor colorWithWhite:0 alpha:0.75], 1.0, nil];
    [fade drawInRect:br angle:90];
    if (_logo) {
        CGFloat w = 372, h = w * _logo.size.height / _logo.size.width;
        NSRect dst = NSMakeRect(44, 58, w, h);
        NSShadow *s = [NSShadow new]; s.shadowColor = [NSColor colorWithWhite:0 alpha:0.55]; s.shadowOffset = NSMakeSize(0, -5); s.shadowBlurRadius = 12;
        [NSGraphicsContext saveGraphicsState]; [s set];
        [_logo drawInRect:dst fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1 respectFlipped:YES hints:@{NSImageHintInterpolation: @(NSImageInterpolationHigh)}];
        [NSGraphicsContext restoreGraphicsState];
        label(@"NATIVE EDITION FOR MACOS", heavy(15), cream(), NSMakeRect(44, NSMaxY(dst) + 14, w, 22), NSTextAlignmentCenter, 3.2, YES);
    } else {
        label(@"THEME PARK", display(56), cream(), NSMakeRect(44, 60, 460, 70), NSTextAlignmentLeft, 2, YES);
        label(@"NATIVE EDITION FOR MACOS", heavy(15), cream(), NSMakeRect(46, 136, 460, 22), NSTextAlignmentLeft, 3.2, YES);
    }
    [rgb(160, 96, 30) setFill]; NSRectFill(NSMakeRect(0, BANNER_H, WIN_W, 2));
}
- (void)drawRow:(NSInteger)k {
    Setting *s = [self rowSetting:k];
    NSRect r = [self rowRect:k];
    BOOL focused = self.focus == k, hot = self.hover == 100 + k || self.hover == 200 + k;
    if (focused || hot) rounded(NSInsetRect(r, 0, 3), 6, [rgb(120, 80, 20) colorWithAlphaComponent:focused ? 0.38 : 0.24], nil, 0);
    label(s->title.uppercaseString, heavy(19), gold(), NSMakeRect(r.origin.x + 18, r.origin.y + self.rowTop, 300, 30), NSTextAlignmentLeft, 2, YES);
    NSRect la = [self arrow:k right:NO], ra = [self arrow:k right:YES];
    NSRect va = NSMakeRect(NSMaxX(la) + 4, r.origin.y + self.rowTop, ra.origin.x - NSMaxX(la) - 8, 30);
    label([self valueOf:s], heavy(20), cream(), va, NSTextAlignmentCenter, 1.2, YES);
    NSString *note = [self noteOf:s];
    if (s->slider) {                                     /* the track, gold to the knob */
        NSRect t = [self trackRect:k]; NSInteger n = (NSInteger)s->values.count, i = [self valueIndex:s];
        CGFloat x = t.origin.x + t.size.width * (n > 1 ? (CGFloat)i / (n - 1) : 0);
        rounded(t, 5, rgb(60, 36, 14), rgb(160, 110, 50), 1);
        rounded(NSMakeRect(t.origin.x, t.origin.y, x - t.origin.x, t.size.height), 5, gold(), nil, 0);
        for (NSInteger j = 0; j < n; j++) { CGFloat tx = t.origin.x + t.size.width * (n > 1 ? (CGFloat)j / (n - 1) : 0); [rgb(60, 36, 14) setFill]; NSRectFill(NSMakeRect(tx - 0.5, t.origin.y + 2, 1, t.size.height - 4)); }
        NSRect knob = NSMakeRect(x - 9, NSMidY(t) - 9, 18, 18);
        NSBezierPath *kp = [NSBezierPath bezierPathWithOvalInRect:knob]; [cream() setFill]; [kp fill]; [rgb(120, 70, 20) setStroke]; kp.lineWidth = 2; [kp stroke];
    } else if (note.length) label(note, plain(14), note_c(), NSMakeRect(va.origin.x - 60, r.origin.y + (self.compact ? 32 : 42), va.size.width + 120, 20), NSTextAlignmentCenter, 0.6, YES);
    for (int side = 0; side < 2; side++) {   /* gold pennant arrows */
        NSRect a = side ? ra : la; BOOL lit = self.hover == (side ? 200 : 100) + k;
        NSBezierPath *t = [NSBezierPath bezierPath];
        CGFloat cx = NSMidX(a), cy = NSMidY(a), d = side ? 1 : -1;
        [t moveToPoint:NSMakePoint(cx + 10 * d, cy)]; [t lineToPoint:NSMakePoint(cx - 9 * d, cy - 11)]; [t lineToPoint:NSMakePoint(cx - 9 * d, cy + 11)]; [t closePath];
        NSShadow *sh = [NSShadow new]; sh.shadowColor = [NSColor colorWithWhite:0 alpha:0.8]; sh.shadowOffset = NSMakeSize(0, -2); sh.shadowBlurRadius = 3;
        [NSGraphicsContext saveGraphicsState]; [sh set];
        [(lit ? rgb(255, 228, 140) : rgb(222, 170, 60)) setFill]; [t fill];
        [NSGraphicsContext restoreGraphicsState];
        [rgb(110, 50, 14) setStroke]; t.lineWidth = 1.2; [t stroke];
    }
}
- (NSString *)summary:(NSInteger)g {
    if (g == 0) return [NSString stringWithFormat:@"%@ · %@ · %@", [self valueOf:&_display[0]], [self valueOf:&_display[1]], [self valueOf:&_display[2]]];
    if (g == 1) return [self valueIndex:&_sound[0]] ? @"Off" : [NSString stringWithFormat:@"Effects %@ · Music %@ · Game %@", [self valueOf:&_sound[1]], [self valueOf:&_sound[6]], [self valueOf:&_sound[2]]];
    if (g == 2) return @"Mouse and keyboard";
    if (g == 4) return [NSString stringWithFormat:@"Ew %@ · Till %@ · Rides %@", [self valueOf:&_tweaks[0]], [self valueOf:&_tweaks[1]], [self valueOf:&_tweaks[2]]];
    if (g == 5) return [NSString stringWithFormat:@"%@ · %@ visitors · %@", [self valueOf:&_free[0]], [self valueOf:&_free[2]], [self valueOf:&_free[4]]];
    return @"Saved parks, in the Finder";
}
- (void)drawRect:(NSRect)dirty {
    [self fillGrass:self.bounds darken:0.64];
    [self drawBanner];
    if (self.open < 0) {
        NSRect f = self.topFrame;
        rounded(f, 8, [NSColor colorWithWhite:0 alpha:0.45], rgb(170, 110, 40), 2);
        rounded(NSInsetRect(f, 6, 6), 5, nil, [rgb(170, 110, 40) colorWithAlphaComponent:0.45], 1);
        [[rgb(170, 110, 40) colorWithAlphaComponent:0.5] setFill];
        for (NSInteger k = 0; k + 1 < self.rowCount; k++) NSRectFill(NSMakeRect(f.origin.x + 24, NSMaxY([self rowRect:k]), f.size.width - 48, 1));
        for (NSInteger k = 0; k < self.rowCount; k++) [self drawRow:k];
        for (NSInteger gi = 0; gi < 6; gi++) {
            NSRect r = [self groupRect:gi];
            int st = self.focus == self.rowCount + gi ? 2 : self.hover == 300 + gi ? 1 : 0;
            [self sign:r title:_groups[gi] primary:NO state:st];
            label([self summary:gi], plain(14), note_c(), NSMakeRect(r.origin.x - 10, NSMaxY(r) + 6, r.size.width + 20, 20), NSTextAlignmentCenter, 0.6, YES);
        }
    } else {
        NSRect p = self.panel;
        NSShadow *sh = [NSShadow new]; sh.shadowColor = [NSColor colorWithWhite:0 alpha:0.8]; sh.shadowOffset = NSMakeSize(0, -6); sh.shadowBlurRadius = 18;
        [NSGraphicsContext saveGraphicsState]; [sh set]; [NSColor.blackColor setFill]; NSRectFill(p); [NSGraphicsContext restoreGraphicsState];
        [self fillGrass:p darken:0.72];
        [rgb(170, 110, 40) setStroke]; NSBezierPath *o = [NSBezierPath bezierPathWithRect:NSInsetRect(p, 1.5, 1.5)]; o.lineWidth = 3; [o stroke];
        [[rgb(170, 110, 40) colorWithAlphaComponent:0.5] setStroke]; NSBezierPath *in = [NSBezierPath bezierPathWithRect:NSInsetRect(p, 9, 9)]; in.lineWidth = 1; [in stroke];
        bulbs(NSInsetRect(p, 1.5, 1.5), 34, YES);
        NSString *title = [_groups[self.open] uppercaseString];
        label(title, display(30), gold(), NSMakeRect(p.origin.x, p.origin.y + 26, p.size.width, 38), NSTextAlignmentCenter, 4, YES);
        [[rgb(170, 110, 40) colorWithAlphaComponent:0.7] setFill]; NSRectFill(NSMakeRect(p.origin.x + 50, p.origin.y + 74, p.size.width - 100, 1));
        if (self.open == 2) [self drawControlsIn:p];
        if (self.open == 5) {
            NSArray *lines = @[@"For new sandbox parks (Sim Level Sandbox); other parks play by the game's rules.",
                               @"Saved parks keep the size and rules they were started with."];
            CGFloat y0 = NSMaxY([self rowRect:self.rowCount - 1]) + 12;
            for (NSUInteger i = 0; i < lines.count; i++)
                label(lines[i], plain(15), note_c(), NSMakeRect(p.origin.x + 50, y0 + 24 * i, p.size.width - 100, 22), NSTextAlignmentLeft, 0.3, NO);
        }
        for (NSInteger k = 0; k < self.rowCount; k++) {
            [self drawRow:k];
            if (k + 1 < self.rowCount) { [[rgb(170, 110, 40) colorWithAlphaComponent:0.4] setFill]; NSRectFill(NSMakeRect(p.origin.x + 50, NSMaxY([self rowRect:k]) + 2, p.size.width - 100, 1)); }
        }
    }
    [self sign:self.quitRect title:self.open >= 0 ? @"Back" : @"Quit" primary:NO state:self.hover == HIT_QUIT || self.hover == HIT_BACK];
    [self sign:self.playRect title:@"Play" primary:YES state:self.hover == HIT_PLAY];
    NSString *build = NSBundle.mainBundle.infoDictionary[@"CFBundleVersion"] ?: @"dev";
    label([NSString stringWithFormat:@"Nightly %@", build], plain(11), [note_c() colorWithAlphaComponent:0.6], NSMakeRect(WIN_W - 216, WIN_H - 22, 200, 16), NSTextAlignmentRight, 0.5, NO);
}
- (void)drawControlsIn:(NSRect)p {
    NSArray *rows = @[
        @[@"Click", @"Select, build, place"], @[@"Right- or Control-click", @"Cancel, rotate, close panels"],
        @[@"Pinch or ⌘-scroll", @"Zoom the park in and out"], @[@"⌘=  ⌘−  ⌘0", @"Zoom in, zoom out, actual size"],
        @[@"Scroll, arrows, drag", @"Move the park view (middle- or Option-drag)"], @[@"Hold Tab", @"Fast forward"],
        @[@"R in the park", @"The game's 640 × 480 mode, and back"], @[@"⌘E", @"Save a picture of the whole park"],
        @[@"Game controller", @"Sticks move; A, B click; shoulders zoom"],
        @[@"Escape", @"Skips the intro"], @[@"⌃⌘F", @"Full screen"], @[@"⌘Q", @"Quit (save in the game first)"],
    ];
    for (NSUInteger i = 0; i < rows.count; i++) {
        CGFloat y = p.origin.y + 88 + 34 * i;
        label([rows[i][0] uppercaseString], heavy(16), gold(), NSMakeRect(p.origin.x + 50, y, 300, 26), NSTextAlignmentLeft, 1.5, YES);
        label(rows[i][1], plain(17), cream(), NSMakeRect(p.origin.x + 350, y, p.size.width - 390, 26), NSTextAlignmentLeft, 0.4, YES);
    }
}
@end

/* Returns YES to play. */
BOOL tp_launcher_run(void) {
    NSRect frame = NSMakeRect(0, 0, WIN_W, WIN_H);
    TPLaunchView *v = [[TPLaunchView alloc] initWithFrame:frame];
    NSString *shot = NSProcessInfo.processInfo.environment[@"TP_LAUNCHER_SHOT"];
    if (shot) {                                             /* bounded preview render */
        NSBitmapImageRep *rep = [v bitmapImageRepForCachingDisplayInRect:v.bounds];
        [v cacheDisplayInRect:v.bounds toBitmapImageRep:rep];
        [[rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:shot atomically:YES];
        return NO;
    }
    NSWindow *w = [[NSWindow alloc] initWithContentRect:frame
                                              styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskFullSizeContentView
                                                backing:NSBackingStoreBuffered defer:NO];
    w.releasedWhenClosed = NO; w.titlebarAppearsTransparent = YES; w.titleVisibility = NSWindowTitleHidden; w.movableByWindowBackground = YES;
    w.title = @"Theme Park"; w.backgroundColor = NSColor.blackColor; w.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
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
