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
 * TP_LAUNCHER_FOCUS=<n> with a row focused, TP_LAUNCHER_HOVER=<hit> with something under the pointer, TP_LAUNCHER_TIME=<s>
 * the animation's clock).
 * It moves (TPFair, below): the player's own park (<data>/art/your-park.png, saved by main.m at quit) pans behind the
 * settings at whole-pixel sizes, the key art drifts, marquee bulbs chase along the banner's edge and around Play, the
 * bunting sways, balloons rise and confetti falls now and then. Core Animation layers set from one clock, so the
 * settings are drawn only when they change; with Reduce Motion on, it holds still. */
#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>

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


/* ---- the fairground: animated layers behind and over the settings (Core Animation, set from one clock) ---- */
static CGImageRef cg_of(NSImage *img) { return img ? [img CGImageForProposedRect:NULL context:nil hints:nil] : NULL; }
static NSImage *drawn(NSSize sz, void (^draw)(NSRect r)) {
    NSImage *i = [[NSImage alloc] initWithSize:sz];
    NSBitmapImageRep *rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL pixelsWide:(NSInteger)(sz.width * 2) pixelsHigh:(NSInteger)(sz.height * 2)
        bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO colorSpaceName:NSCalibratedRGBColorSpace bytesPerRow:0 bitsPerPixel:0];
    rep.size = sz;
    [NSGraphicsContext saveGraphicsState]; [NSGraphicsContext setCurrentContext:[NSGraphicsContext graphicsContextWithBitmapImageRep:rep]];
    draw(NSMakeRect(0, 0, sz.width, sz.height));
    [NSGraphicsContext restoreGraphicsState];
    [i addRepresentation:rep]; return i;
}
static NSImage *drawn_px(NSSize px, void (^draw)(NSRect r)) {   /* (one point a pixel) */
    NSBitmapImageRep *rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL pixelsWide:(NSInteger)px.width pixelsHigh:(NSInteger)px.height
        bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO colorSpaceName:NSCalibratedRGBColorSpace bytesPerRow:0 bitsPerPixel:0];
    [NSGraphicsContext saveGraphicsState]; [NSGraphicsContext setCurrentContext:[NSGraphicsContext graphicsContextWithBitmapImageRep:rep]];
    draw(NSMakeRect(0, 0, px.width, px.height)); [NSGraphicsContext restoreGraphicsState];
    NSImage *i = [[NSImage alloc] initWithSize:px]; [i addRepresentation:rep]; return i;
}
static double frand(unsigned k) { k ^= k >> 13; k *= 0x5bd1e995u; k ^= k >> 15; return (k & 0xffffff) / (double)0x1000000; }   /* (steady: the same each run) */
static NSImage *balloon_img(NSColor *c) {
    return drawn(NSMakeSize(30, 64), ^(NSRect r) {
        NSBezierPath *str = [NSBezierPath bezierPath]; [str moveToPoint:NSMakePoint(15, 26)];
        [str curveToPoint:NSMakePoint(14, 0) controlPoint1:NSMakePoint(20, 16) controlPoint2:NSMakePoint(9, 9)];
        [[NSColor colorWithWhite:0.92 alpha:0.75] setStroke]; str.lineWidth = 0.8; [str stroke];
        NSRect b = NSMakeRect(2, 26, 26, 34);
        NSGradient *g = [[NSGradient alloc] initWithColorsAndLocations:[c blendedColorWithFraction:0.45 ofColor:NSColor.whiteColor], 0.0, c, 0.5,
                         [c blendedColorWithFraction:0.45 ofColor:NSColor.blackColor], 1.0, nil];
        [g drawInBezierPath:[NSBezierPath bezierPathWithOvalInRect:b] relativeCenterPosition:NSMakePoint(-0.35, 0.4)];
        [[NSColor colorWithWhite:1 alpha:0.55] setFill]; [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(8, 46, 6, 9)] fill];
        NSBezierPath *knot = [NSBezierPath bezierPath]; [knot moveToPoint:NSMakePoint(12, 24)]; [knot lineToPoint:NSMakePoint(18, 24)]; [knot lineToPoint:NSMakePoint(15, 28)]; [knot closePath];
        [[c blendedColorWithFraction:0.3 ofColor:NSColor.blackColor] setFill]; [knot fill];
    });
}
static NSImage *bulb_img(BOOL lit) {
    return drawn(NSMakeSize(22, 22), ^(NSRect r) {
        NSPoint p = NSMakePoint(11, 11);
        if (lit) { NSGradient *glow = [[NSGradient alloc] initWithStartingColor:[bulb() colorWithAlphaComponent:0.75] endingColor:[bulb() colorWithAlphaComponent:0]];
                   [glow drawFromCenter:p radius:0 toCenter:p radius:11 options:0]; }
        [rgb(110, 62, 18) setFill]; [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(7, 7, 8, 8)] fill];
        [(lit ? rgb(255, 252, 222) : rgb(150, 112, 60)) setFill]; [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(8.2, 8.2, 5.6, 5.6)] fill];
    });
}

@interface TPFair : NSObject
- (instancetype)initWithSize:(NSSize)size park:(NSImage *)park soft:(BOOL)soft keyart:(NSImage *)keyart grass:(NSImage *)grass banner:(CGFloat)bannerH play:(NSRect)play quit:(NSRect)quit;
@property (readonly) CALayer *back, *front;   /* behind the settings; over them (no clicks) */
- (void)setTime:(double)t;
- (void)setPageOpen:(BOOL)open;   /* a settings page over the window: the banner's bulbs and strip, bunting and confetti hide */
@end
@implementation TPFair {
    NSSize _sz; CGFloat _bh; CALayer *_park, *_key, *_parkDim; NSMutableArray<CALayer *> *_balloons, *_bunting, *_bulbs, *_confetti;
    NSMutableArray<NSValue *> *_bulbAt; NSSize _parkSize; double _parkScale; NSInteger _nMarquee; NSMutableArray<CALayer *> *_bannerOnly;
}
static CGFloat flipY(NSSize sz, CGFloat y) { return sz.height - y; }   /* (layers count up from the bottom; the window's layout counts down) */
- (instancetype)initWithSize:(NSSize)size park:(NSImage *)park soft:(BOOL)soft keyart:(NSImage *)keyart grass:(NSImage *)grass banner:(CGFloat)bannerH play:(NSRect)play quit:(NSRect)quit {
    if (!(self = [super init])) return nil;
    _sz = size; _bh = bannerH;
    _back = [CALayer layer]; _back.frame = CGRectMake(0, 0, size.width, size.height); _back.backgroundColor = CGColorCreateGenericRGB(0.04, 0.07, 0.03, 1);
    _front = [CALayer layer]; _front.frame = _back.frame;
    /* the park below the banner: the player's own at whole-pixel sizes (sharp on any display), GOG's pictures, else the title's lawn */
    CALayer *lower = [CALayer layer]; lower.frame = CGRectMake(0, 0, size.width, size.height - bannerH); lower.masksToBounds = YES; [_back addSublayer:lower];
    if (park) {
        _parkSize = park.size; _parkScale = 0.6;                       /* GOG's pictures: shrunk a little (soft) */
        if (!soft) { _parkScale = 1;                                   /* the player's park: the smallest whole size with room to pan */
            while (_parkScale < 4 && (_parkSize.width * _parkScale < size.width * 1.15 || _parkSize.height * _parkScale < (size.height - bannerH) * 1.15)) _parkScale++; }
        _park = [CALayer layer]; _park.contents = (__bridge id)cg_of(park); _park.magnificationFilter = kCAFilterNearest; _park.minificationFilter = kCAFilterTrilinear;
        _park.bounds = CGRectMake(0, 0, _parkSize.width * _parkScale, _parkSize.height * _parkScale); _park.anchorPoint = CGPointZero;
        [lower addSublayer:_park];
    } else if (grass) {
        _park = [CALayer layer]; _park.anchorPoint = CGPointZero; _parkScale = 0;
        _park.backgroundColor = [NSColor colorWithPatternImage:grass].CGColor; _park.bounds = CGRectMake(0, 0, size.width + 160, size.height - bannerH + 160);
        [lower addSublayer:_park];
    }
    _parkDim = [CAGradientLayer layer]; _parkDim.frame = lower.bounds;   /* darker at the top (the settings) and the bottom (the signs) */
    ((CAGradientLayer *)_parkDim).colors = @[(__bridge_transfer id)CGColorCreateGenericRGB(0.02, 0.03, 0.02, park ? 0.66 : 0.66), (__bridge_transfer id)CGColorCreateGenericRGB(0.02, 0.03, 0.02, park ? 0.46 : 0.55),
                                             (__bridge_transfer id)CGColorCreateGenericRGB(0.02, 0.03, 0.02, park ? 0.70 : 0.72)];
    ((CAGradientLayer *)_parkDim).locations = @[@0, @0.55, @1]; [lower addSublayer:_parkDim];
    /* the key art across the top, drifting */
    CALayer *top = [CALayer layer]; top.frame = CGRectMake(0, size.height - bannerH, size.width, bannerH); top.masksToBounds = YES; [_back addSublayer:top];
    if (keyart) {
        _key = [CALayer layer]; _key.contents = (__bridge id)cg_of(keyart); _key.contentsGravity = kCAGravityResizeAspectFill;
        NSSize s = keyart.size; CGFloat k = MAX(size.width / (s.width * 0.83), bannerH / s.height) * 1.08;
        _key.bounds = CGRectMake(0, 0, s.width * k, s.height * k); _key.anchorPoint = CGPointMake(1, 0.5);
        [top addSublayer:_key];
        CAGradientLayer *left = [CAGradientLayer layer]; left.frame = top.bounds; left.startPoint = CGPointMake(0, 0.5); left.endPoint = CGPointMake(0.55, 0.5);
        left.colors = @[(__bridge_transfer id)CGColorCreateGenericRGB(16 / 255.0, 70 / 255.0, 150 / 255.0, 0.6), (__bridge_transfer id)CGColorCreateGenericRGB(16 / 255.0, 70 / 255.0, 150 / 255.0, 0)];
        [top addSublayer:left];
    } else { top.backgroundColor = CGColorCreateGenericRGB(0.1, 0.38, 0.75, 1); }
    CAGradientLayer *fade = [CAGradientLayer layer]; fade.frame = top.bounds;
    fade.colors = @[(__bridge_transfer id)CGColorCreateGenericRGB(0, 0, 0, 0.75), (__bridge_transfer id)CGColorCreateGenericRGB(0, 0, 0, 0)]; fade.locations = @[@0, @0.3]; [top addSublayer:fade];
    /* bunting across the top of the banner: two strings of pennants */
    _bunting = [NSMutableArray array]; _bannerOnly = [NSMutableArray array];
    NSArray *flagc = @[rgb(214, 40, 40), rgb(250, 204, 44), rgb(40, 120, 220), rgb(60, 170, 80), rgb(245, 130, 30)];
    for (int line = 0; line < 2; line++) {
        CGFloat x0 = line ? size.width * 0.46 : -10, x1 = line ? size.width + 10 : size.width * 0.54, sag = 18, ytop = 6 + line * 10;
        CAShapeLayer *str = [CAShapeLayer layer]; CGMutablePathRef sp = CGPathCreateMutable();
        CGPathMoveToPoint(sp, NULL, x0, flipY(size, ytop)); CGPathAddQuadCurveToPoint(sp, NULL, (x0 + x1) / 2, flipY(size, ytop + 2 * sag), x1, flipY(size, ytop));
        str.path = sp; CGPathRelease(sp); str.strokeColor = CGColorCreateGenericRGB(0.95, 0.92, 0.85, 0.8); str.fillColor = NULL; str.lineWidth = 1.2; [_front addSublayer:str]; [_bannerOnly addObject:str];
        int n = (int)((x1 - x0) / 26);
        for (int i = 1; i < n; i++) {
            double u = (double)i / n; CGFloat x = x0 + (x1 - x0) * u, y = ytop + 4 * sag * u * (1 - u);
            CAShapeLayer *f = [CAShapeLayer layer]; CGMutablePathRef fp = CGPathCreateMutable();
            CGPathMoveToPoint(fp, NULL, -8, 0); CGPathAddLineToPoint(fp, NULL, 8, 0); CGPathAddLineToPoint(fp, NULL, 0, -20); CGPathCloseSubpath(fp);
            f.path = fp; CGPathRelease(fp); f.fillColor = [(NSColor *)flagc[(i + line * 2) % flagc.count] CGColor];
            f.strokeColor = CGColorCreateGenericRGB(0, 0, 0, 0.25); f.lineWidth = 0.6; f.shadowOpacity = 0.35; f.shadowOffset = CGSizeMake(0, -2); f.shadowRadius = 2;
            f.position = CGPointMake(x, flipY(size, y)); [_front addSublayer:f]; [_bunting addObject:f]; [_bannerOnly addObject:f];
        }
    }
    /* balloons rising on the right of the banner */
    _balloons = [NSMutableArray array];
    NSArray *bc = @[rgb(226, 44, 52), rgb(250, 196, 40), rgb(46, 132, 226), rgb(236, 90, 170), rgb(70, 180, 90), rgb(250, 130, 40), rgb(140, 80, 210)];
    for (int i = 0; i < 9; i++) {
        CALayer *b = [CALayer layer]; b.contents = (__bridge id)cg_of(balloon_img(bc[i % bc.count])); b.bounds = CGRectMake(0, 0, 30, 64);
        CGFloat k = 0.75 + 0.45 * frand(i * 7 + 1); b.transform = CATransform3DMakeScale(k, k, 1); b.zPosition = k;
        [_back addSublayer:b]; [_balloons addObject:b];     /* (behind the logo and riders' frame: they rise through the sky) */
    }
    /* marquee bulbs along the banner's edge and around Play and Quit */
    _bulbs = [NSMutableArray array]; _bulbAt = [NSMutableArray array];
    CGImageRef on = cg_of(bulb_img(YES)), off = cg_of(bulb_img(NO));
    for (CGFloat x = 11; x < size.width; x += 22) [_bulbAt addObject:[NSValue valueWithPoint:NSMakePoint(x, bannerH)]];
    _nMarquee = (NSInteger)_bulbAt.count;
    for (NSValue *rv in @[[NSValue valueWithRect:play], [NSValue valueWithRect:quit]]) {
        NSRect r = NSInsetRect(rv.rectValue, -7, -7);
        CGFloat per = 2 * (r.size.width + r.size.height); int n = (int)lround(per / 17);
        for (int i = 0; i < n; i++) {                  /* round the sign, clockwise from its top-left corner */
            CGFloat d = per * i / n, x, y;
            if (d < r.size.width) { x = r.origin.x + d; y = r.origin.y; }
            else if ((d -= r.size.width) < r.size.height) { x = NSMaxX(r); y = r.origin.y + d; }
            else if ((d -= r.size.height) < r.size.width) { x = NSMaxX(r) - d; y = NSMaxY(r); }
            else { d -= r.size.width; x = r.origin.x; y = NSMaxY(r) - d; }
            [_bulbAt addObject:[NSValue valueWithPoint:NSMakePoint(x, y)]];
        }
    }
    CALayer *strip = [CALayer layer]; strip.frame = CGRectMake(0, flipY(size, bannerH) - 7, size.width, 14);
    strip.backgroundColor = CGColorCreateGenericRGB(0.28, 0.13, 0.04, 1); strip.borderColor = CGColorCreateGenericRGB(0.75, 0.5, 0.16, 1); strip.borderWidth = 1;
    [_front addSublayer:strip]; [_bannerOnly addObject:strip];
    NSInteger bi = 0;
    for (NSValue *v in _bulbAt) {
        NSPoint p = v.pointValue;
        CALayer *b = [CALayer layer]; b.bounds = CGRectMake(0, 0, 22, 22); b.position = CGPointMake(p.x, flipY(size, p.y)); b.contents = (__bridge id)off;
        CALayer *lit = [CALayer layer]; lit.frame = b.bounds; lit.contents = (__bridge id)on; [b addSublayer:lit];
        [_front addSublayer:b]; [_bulbs addObject:lit]; if (bi++ < _nMarquee) [_bannerOnly addObject:b];
    }
    /* confetti (a burst over the banner now and then) */
    _confetti = [NSMutableArray array];
    CALayer *sky = [CALayer layer]; sky.frame = CGRectMake(0, size.height - bannerH + 7, size.width, bannerH - 7); sky.masksToBounds = YES;   /* (the banner only) */
    [_front insertSublayer:sky atIndex:0]; [_bannerOnly addObject:sky];
    for (int i = 0; i < 34; i++) {
        CALayer *c = [CALayer layer]; c.bounds = CGRectMake(0, 0, 5 + 3 * frand(i * 3), 9 + 4 * frand(i * 5));
        c.backgroundColor = [(NSColor *)flagc[i % flagc.count] CGColor]; c.opacity = 0; [sky addSublayer:c]; [_confetti addObject:c];
    }
    return self;
}
- (void)setPageOpen:(BOOL)open {
    [CATransaction begin]; [CATransaction setDisableActions:YES];
    for (CALayer *l in _bannerOnly) l.hidden = open;
    [CATransaction commit];
}
- (void)setTime:(double)t {
    [CATransaction begin]; [CATransaction setDisableActions:YES];
    NSSize sz = _sz; CGFloat lowerH = sz.height - _bh;
    if (_park && _parkScale > 0) {                     /* a slow pan round the park: corner to corner, there and back (~2 min) */
        CGFloat w = _park.bounds.size.width, h = _park.bounds.size.height, mx = MAX(0, w - sz.width), my = MAX(0, h - lowerH);
        double u = 0.5 - 0.5 * cos(t * 2 * M_PI / 130), v = 0.5 - 0.5 * cos(t * 2 * M_PI / 97 + 1.1);
        CGFloat q = MAX(_parkScale, 0.5), x = -round(mx * u / q) * q, y = -round(my * v / q) * q;   /* (whole park pixels: no shimmer) */
        if (w < sz.width) x = round((sz.width - w) / 2); if (h < lowerH) y = round((lowerH - h) / 2);
        _park.position = CGPointMake(x, y);
    } else if (_park) _park.position = CGPointMake(-fmod(t * 6, 80), -fmod(t * 3, 80));
    if (_key) {                                       /* the riders: a slow drift and breath */
        double z = 1 + 0.035 * (0.5 - 0.5 * cos(t * 2 * M_PI / 28));
        _key.position = CGPointMake(sz.width + 14 * sin(t * 2 * M_PI / 40), _bh / 2 + 6 * sin(t * 2 * M_PI / 33));
        _key.transform = CATransform3DMakeScale(z, z, 1);
    }
    for (NSUInteger i = 0; i < _bunting.count; i++) {
        CALayer *f = _bunting[i]; f.transform = CATransform3DMakeRotation(0.09 * sin(t * 2.1 + i * 0.7), 0, 0, 1);
    }
    for (NSUInteger i = 0; i < _balloons.count; i++) {   /* up through the sky on the right, each at its own pace, round again */
        CALayer *b = _balloons[i]; double sp = 16 + 12 * frand((unsigned)i * 11 + 3), span = _bh + 140;
        double y = fmod(t * sp + span * frand((unsigned)i * 13 + 5), span) - 70;     /* from below the banner to above it */
        double x = sz.width * (0.5 + 0.48 * frand((unsigned)i * 17 + 9)) + 12 * sin(t * (0.6 + 0.3 * frand((unsigned)i * 19)) + i);
        b.position = CGPointMake(x, sz.height - _bh + y); b.opacity = y < 10 ? MAX(0, (y + 70) / 80) : 1;
    }
    long step = (long)floor(t * 7);                    /* the chase: every third bulb, moving on 7 times a second */
    for (NSUInteger i = 0; i < _bulbs.count; i++) {
        long k = (long)i < _nMarquee ? (long)i - step : (long)i + step;
        _bulbs[i].opacity = ((k % 3) + 3) % 3 == 0 ? 1 : 0.08;
    }
    double cyc = fmod(t, 14);                          /* confetti for 4 s of every 14 */
    for (NSUInteger i = 0; i < _confetti.count; i++) {
        CALayer *c = _confetti[i]; double d = cyc - 0.6 * frand((unsigned)i * 23);
        if (d < 0 || d > 4) { c.opacity = 0; continue; }
        double x = sz.width * (0.08 + 0.84 * frand((unsigned)i * 29 + 1)) + 22 * sin(d * 3 + i);
        double y = _bh - 7 - (d * (55 + 40 * frand((unsigned)i * 31))) + 10;   /* (in the banner's own layer) */
        c.position = CGPointMake(x, y); c.opacity = d > 3.2 ? (float)((4 - d) / 0.8) : 1;
        c.transform = CATransform3DMakeRotation(d * (3 + 5 * frand((unsigned)i * 37)) + i, 0.3, 1, 0.2);
    }
    [CATransaction commit];
}
@end

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

/* ---- updates ----
 * tools/build_app.sh gives the app an updater (Contents/Resources/update) when it is built from a checkout with a GitHub
 * remote: "update --check" prints the newest commit of the branch the app follows (TPUpdateBranch in its Info.plist,
 * nightly here); "update" fetches it into a worktree of that checkout and builds and installs the app from it, the
 * game files, saves and settings kept. The launcher asks GitHub in the background each time it opens. (After the
 * other ports' launchers: freedom-force-macos recomp/host/launcher.m.) */
enum { UPD_NONE, UPD_CHECKING, UPD_CURRENT, UPD_AVAILABLE, UPD_ERROR };
static NSString *short_commit(NSString *c) { return c.length > 7 ? [c substringToIndex:7] : (c ?: @""); }
static NSString *update_label(int st) {
    return st == UPD_CHECKING ? @"Checking…" : st == UPD_CURRENT ? @"Up to Date" : st == UPD_AVAILABLE ? @"Update Available"
         : st == UPD_ERROR ? @"Couldn't Check" : @"Not Available";
}
static NSString *update_note(int st, NSString *branch, NSString *built, NSString *latest) {
    switch (st) {
    case UPD_CHECKING: return [NSString stringWithFormat:@"Asking GitHub for the newest %@", branch];
    case UPD_CURRENT: return [NSString stringWithFormat:@"%@ %@ · → checks again", branch, short_commit(built)];
    case UPD_AVAILABLE: return [NSString stringWithFormat:@"%@ → %@ · press to update", short_commit(built), short_commit(latest)];
    case UPD_ERROR: return @"GitHub couldn't be reached · → tries again";
    default: return @"This copy wasn't built from the GitHub repository";
    }
}
static NSString *update_check(NSString *updater) {        /* the newest commit of the branch on GitHub; nil if unreachable */
    NSTask *t = [NSTask new]; t.executableURL = [NSURL fileURLWithPath:@"/bin/bash"]; t.arguments = @[updater, @"--check"];
    NSPipe *p = [NSPipe pipe]; t.standardOutput = p; t.standardError = [NSFileHandle fileHandleWithNullDevice];
    if (![t launchAndReturnError:nil]) return nil;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 20 * NSEC_PER_SEC), dispatch_get_global_queue(0, 0), ^{ if (t.running) [t terminate]; });
    NSData *d = [p.fileHandleForReading readDataToEndOfFile]; [t waitUntilExit];
    NSString *out = [[[NSString alloc] initWithData:d encoding:NSUTF8StringEncoding] stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
    if (t.terminationStatus != 0 || !out.length) return nil;
    if (out.length >= 40) return [out substringToIndex:40];   /* a commit (an app built from source) */
    return [out rangeOfCharacterFromSet:NSCharacterSet.whitespaceAndNewlineCharacterSet].location == NSNotFound ? out : nil;   /* a release's tag (the downloaded app) */
}
static NSString *update_phase(NSString *line, NSString *branch) {   /* the updater's output, in words */
    if ([branch isEqualToString:@"release"]) {   /* the downloaded app's updater (setup/update.sh) */
        if ([line hasPrefix:@"== downloading "]) return [NSString stringWithFormat:@"Downloading %@ from GitHub", [line substringFromIndex:15]];
        if ([line hasPrefix:@"== unpacking"]) return @"Unpacking it";
        if ([line hasPrefix:@"== installing"]) return @"Installing the new app";
        return nil;
    }
    if ([line hasPrefix:@"== fetching"]) return @"Downloading the update from GitHub";
    if ([line hasPrefix:[NSString stringWithFormat:@"== %@:", branch]]) return [@"Got " stringByAppendingString:[line substringFromIndex:branch.length + 4]];
    if ([line hasPrefix:@"== making the sound"]) return @"Making the sound drivers from your game (once)";
    if ([line hasPrefix:@"== translating"]) return @"Rebuilding from your game: translating its code (1 of 3)";
    if ([line hasPrefix:@"== compiling"]) return @"Rebuilding from your game: compiling (2 of 3)";
    if ([line hasPrefix:@"== installing"]) return @"Installing the new app (3 of 3)";
    return nil;
}
@interface TPBlockTarget : NSObject
@property (copy) void (^fire)(void);
- (void)clicked:(id)sender;
@end
@implementation TPBlockTarget
- (void)clicked:(id)sender { (void)sender; if (self.fire) self.fire(); }
@end
static void on_main(dispatch_block_t b) { CFRunLoopPerformBlock(CFRunLoopGetMain(), kCFRunLoopCommonModes, b); CFRunLoopWakeUp(CFRunLoopGetMain()); }
/* The updater with a progress window (modal); YES when the new app is installed. */
static BOOL update_run(NSWindow *parent, NSString *updater, NSString *branch, NSString *logPath) {
    NSPanel *pn = [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, 520, 170) styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
    pn.title = @"Updating";
    NSTextField *title = [NSTextField labelWithString:@"Updating to the newest version"]; title.font = [NSFont boldSystemFontOfSize:14]; title.frame = NSMakeRect(20, 128, 480, 22);
    NSTextField *status = [NSTextField wrappingLabelWithString:@"Starting…"]; status.frame = NSMakeRect(20, 64, 480, 56);
    NSProgressIndicator *spin = [[NSProgressIndicator alloc] initWithFrame:NSMakeRect(20, 44, 480, 12)]; spin.indeterminate = YES; [spin startAnimation:nil];
    NSTextField *time = [NSTextField labelWithString:@"This takes a few minutes. Your saves and settings are kept."]; time.font = [NSFont systemFontOfSize:11]; time.textColor = NSColor.secondaryLabelColor; time.frame = NSMakeRect(20, 14, 360, 18);
    NSButton *btn = [NSButton buttonWithTitle:@"Close" target:nil action:nil]; btn.frame = NSMakeRect(400, 8, 100, 30); btn.hidden = YES;
    for (NSView *x in @[title, status, spin, time, btn]) [pn.contentView addSubview:x];
    if (parent) [pn setFrameOrigin:NSMakePoint(NSMidX(parent.frame) - 260, NSMidY(parent.frame) - 85)]; else [pn center];
    [NSFileManager.defaultManager createFileAtPath:logPath contents:nil attributes:nil];
    NSFileHandle *log = [NSFileHandle fileHandleForWritingAtPath:logPath];
    NSTask *t = [NSTask new]; t.executableURL = [NSURL fileURLWithPath:@"/bin/bash"]; t.arguments = @[updater];
    NSPipe *pipe = [NSPipe pipe]; t.standardOutput = pipe; t.standardError = pipe;
    NSMutableString *pending = [NSMutableString new], *tail = [NSMutableString new];
    pipe.fileHandleForReading.readabilityHandler = ^(NSFileHandle *h) {
        NSData *d = h.availableData; if (!d.length) return;
        [log writeData:d];
        NSString *chunk = [[NSString alloc] initWithData:d encoding:NSUTF8StringEncoding] ?: @"";
        @synchronized (pending) {
            [pending appendString:chunk];
            NSRange nl;
            while ((nl = [pending rangeOfString:@"\n"]).location != NSNotFound) {
                NSString *line = [pending substringToIndex:nl.location]; [pending deleteCharactersInRange:NSMakeRange(0, nl.location + 1)];
                [tail appendFormat:@"%@\n", line]; if (tail.length > 600) [tail deleteCharactersInRange:NSMakeRange(0, tail.length - 600)];
                NSString *ph = update_phase(line, branch);
                if (ph) on_main(^{ status.stringValue = ph; });
            }
        }
    };
    __block BOOL ok = NO;
    __attribute__((objc_precise_lifetime)) TPBlockTarget *bt = [TPBlockTarget new];   /* (a button's target is weak: kept here) */
    bt.fire = ^{ [NSApp stopModal]; };
    btn.target = bt; btn.action = @selector(clicked:);
    t.terminationHandler = ^(NSTask *task) {
        int code = task.terminationStatus;
        on_main(^{
            pipe.fileHandleForReading.readabilityHandler = nil; [log closeFile];
            [spin stopAnimation:nil]; spin.hidden = YES; btn.hidden = NO;
            if (code == 0) { ok = YES; title.stringValue = @"Updated"; status.stringValue = @"The new version is installed. It opens when you press Restart."; btn.title = @"Restart"; }
            else {
                NSString *last; @synchronized (pending) { last = [[tail componentsSeparatedByString:@"\n"] componentsJoinedByString:@" "]; }
                title.stringValue = @"The update didn't finish";
                status.stringValue = [NSString stringWithFormat:@"The app is unchanged if it stopped before installing. Details: %@  (%@)", logPath,
                                      last.length > 160 ? [last substringFromIndex:last.length - 160] : last];
                time.stringValue = @"";
            }
        });
    };
    if (![t launchAndReturnError:nil]) { [log closeFile]; return NO; }
    [pn makeKeyAndOrderFront:nil];
    [NSApp runModalForWindow:pn];
    [pn orderOut:nil];
    return ok;
}

@interface TPLaunchView : NSView
@property NSInteger focus, hover, open;   /* open: -1 or group index */
@property NSInteger dragRow;              /* a slider being dragged, or -1 */
@property BOOL result, done;
@property (weak) id fair;                 /* TPFair: told when a page opens */
- (NSImage *)keyart; - (NSImage *)grass; - (NSImage *)park; - (BOOL)parkSoft; - (NSRect)playRect; - (NSRect)quitRect;
@end

enum { HIT_NONE = -1, HIT_PLAY = 1000, HIT_QUIT = 1001, HIT_BACK = 1002 };
#define WIN_W 780
#define WIN_H 760
#define BANNER_H 250

@implementation TPLaunchView {
    NSImage *_keyart, *_logo, *_grass, *_park; BOOL _parkSoft;
    Setting _top[4];
    int _ustate; NSString *_latest, *_updater, *_branch, *_built;   /* the Updates row (below) */
    NSArray<NSString *> *_groups;                  /* the pages: Game, Display, Sound (the main screen's buttons), and nested */
    Setting _display[8], _sound[9], _tweaks[7], _free[5], _links[5];
    Setting *_page[7][10]; int _npage[7];          /* each page's rows (the main screen: _top[0], _top[3]) */
}
- (instancetype)initWithFrame:(NSRect)f {
    if ((self = [super initWithFrame:f])) {
        NSBundle *b = NSBundle.mainBundle; NSString *p;
        NSString *art = getenv("TP_IMAGE_DIR") && getenv("TP_DATA") ? [@(getenv("TP_DATA")) stringByAppendingPathComponent:@"art"] : nil;   /* the downloaded app: setup's art */
        if ((p = [b pathForResource:@"keyart" ofType:@"jpg"]) || (art && [NSFileManager.defaultManager fileExistsAtPath:(p = [art stringByAppendingPathComponent:@"keyart.jpg"])]))
            _keyart = [[NSImage alloc] initWithContentsOfFile:p];
        if ((p = [b pathForResource:@"logo" ofType:@"png"]) || (art && [NSFileManager.defaultManager fileExistsAtPath:(p = [art stringByAppendingPathComponent:@"logo.png"])]))
            _logo = trimmed([[NSImage alloc] initWithContentsOfFile:p]);
        NSImage *title = (p = [b pathForResource:@"banner" ofType:@"png"]) ? [[NSImage alloc] initWithContentsOfFile:p] : nil;
        if (title) {
            _grass = pixel_crop(title, CGRectMake(272, 64, 40, 40), 2);           /* the title screen's lawn */
            if (!_logo) _logo = pixel_crop(title, CGRectMake(58, 17, 206, 36), 8); /* its logo box, if GOG's art is missing */
        }
        if (_keyart.size.width < 400) _keyart = nil;
        {   /* the player's own park (main.m saves its picture at quit), for the backdrop */
            const char *d = getenv("TP_DATA"), *h = getenv("HOME");
            NSString *root = d && *d ? @(d) : [(h && *h ? @(h) : NSHomeDirectory()) stringByAppendingPathComponent:@"Games/ThemeParkNative"];
            NSString *pp = [root stringByAppendingPathComponent:@"art/your-park.png"];
            if (getenv("TP_LAUNCHER_PARK")) pp = @(getenv("TP_LAUNCHER_PARK"));    /* (development: another park's picture) */
            NSImage *pk = [[NSImage alloc] initWithContentsOfFile:pp];
            NSBitmapImageRep *rep = (NSBitmapImageRep *)pk.representations.firstObject;
            if (pk && [rep isKindOfClass:NSBitmapImageRep.class]) { pk.size = NSMakeSize(rep.pixelsWide, rep.pixelsHigh); if (pk.size.width >= 320) _park = pk; }
            if (!_park) {   /* none yet: GOG's pictures of a park (fetched at install), one image pixel per display pixel */
                NSString *art = [root stringByAppendingPathComponent:@"art"]; NSMutableArray *shots = [NSMutableArray array];
                for (NSString *f in [[NSFileManager.defaultManager contentsOfDirectoryAtPath:art error:nil] sortedArrayUsingSelector:@selector(compare:)])
                    if ([f hasPrefix:@"park-shot-"] && [f hasSuffix:@".png"]) { NSImage *i = [[NSImage alloc] initWithContentsOfFile:[art stringByAppendingPathComponent:f]]; if (i) [shots addObject:i]; }
                if (shots.count) {   /* side by side, as one strip */
                    NSBitmapImageRep *r0 = (NSBitmapImageRep *)[shots[0] representations].firstObject; CGFloat sw = r0.pixelsWide, sh = r0.pixelsHigh;
                    NSUInteger n = MIN(shots.count, 4);   /* two rows, the second one turned round so no picture meets itself */
                    _park = drawn_px(NSMakeSize(sw * n, sh * 2), ^(NSRect r) {
                        for (NSUInteger k = 0; k < n; k++) {
                            [(NSImage *)shots[k] drawInRect:NSMakeRect(sw * k, sh, sw, sh)];
                            [(NSImage *)shots[(k + 1) % n] drawInRect:NSMakeRect(sw * k, 0, sw, sh)]; } });
                    _parkSoft = YES;
                }
            }
        }
        NSArray *vol = @[@0.0, @0.1, @0.2, @0.3, @0.4, @0.5, @0.6, @0.7, @0.8, @0.9, @1.0];
        NSArray *volL = @[@"Off", @"10%", @"20%", @"30%", @"40%", @"50%", @"60%", @"70%", @"80%", @"90%", @"100%"];
        NSArray *volN = @[@"", @"", @"", @"", @"", @"", @"", @"", @"", @"", @""];
        _top[0] = setting(@"Game Speed", @"Cycles", @[@10000, @12500, @15000, @20000, @40000], @[@"Original", @"1.25×", @"1.5×", @"2×", @"4×"],
                          @[@"Exactly as GOG's DOSBox runs it", @"A quarter faster than GOG's DOSBox", @"Half again as fast as GOG's DOSBox",
                            @"Twice GOG's DOSBox", @"Four times: the seasons fly by"]);
        _top[2] = setting(@"Language", @"Language", @[@0, @1, @2, @3, @4], @[@"English", @"Deutsch", @"Italiano", @"Español", @"Français"],
                          @[@"The game's text and advisors", @"Texte und Berater des Spiels", @"Testi e consiglieri del gioco",
                            @"Textos y asesores del juego", @"Textes et conseillers du jeu"]);
        _top[3] = setting(@"Updates", @"Updates", @[@""], @[@""], @[@""]);   /* (its value and note: the updater's state) */
        _top[1] = setting(@"Intro", @"PlayIntro", @[@YES, @NO], @[@"Play", @"Skip"],
                          @[@"The 1994 intro movie; Escape skips it", @"Straight to the title screen"]);
        _groups = @[@"Game", @"Display", @"Sound", @"Free Mode", @"Controls", @"Music", @"Effects"];
        _display[7] = setting(@"Ride Videos", @"RideVideos", @[@0, @1, @2], @[@"PC", @"PC + 3DO", @"3DO"],
                              @[@"The PC's ride videos only", @"The PC's, and the 3DO's for Space Shuttle and Tree House",
                                @"The 3DO's in full colour, the PC's for the rest"]);
        _display[0] = setting(@"Screen", @"StartFullScreen", @[@NO, @YES], @[@"Window", @"Full Screen"],
                              @[@"A window you can resize; ⌃⌘F switches", @"The whole display; ⌃⌘F switches"]);
        _display[1] = setting(@"Pixels", @"SmoothPixels", @[@0, @1, @2], @[@"Sharp", @"Smooth", @"CRT"],
                              @[@"Square pixels, as the game drew them", @"Softened as they are scaled up", @"Soft, with a 1994 monitor's scanlines"]);
        _display[2] = setting(@"Aspect", @"Stretch", @[@NO, @YES], @[@"4:3", @"Fill the Window"],
                              @[@"The shape a 1994 monitor gave it", @"Stretched to the window's shape"]);
        _sound[0] = setting(@"Sound", @"Sound", @[@YES, @NO], @[@"On", @"Off"],
                            @[@"Sound Blaster effects and General MIDI music", @"The park is silent"]);
        _sound[8] = setting(@"Music Type", @"MusicType", @[@0, @1, @2], @[@"General MIDI", @"Amiga CD32", @"3DO Recorded"],
                            @[@"The game's MIDI tunes, as on the PC", @"The CD32 disc's own tracks (tools/extras.py imports them)",
                              @"The 3DO's recordings of the PC tunes (tools/extras.py imports them)"]);
        _sound[7] = setting(@"Music Instruments", @"SoundBank", @[@""], @[@"Built-in"],
                            @[@"The Mac's synthesizer; → picks a SoundFont"]);
        _sound[1] = slider(@"Effects Volume", @"SoundVolume", vol, volL, volN);
        _sound[6] = slider(@"Music Volume", @"MusicVolume", vol, volL, volN);
        _sound[2] = slider(@"Game Sounds", @"GameSoundVolume", vol, volL, volN);     /* (qol.c: rides, people, the ew, purchases) */
        _sound[5] = slider(@"System Sounds", @"SystemSoundVolume", vol, volL, volN); /* (the button clicks) */
        NSArray *lv = @[@0.0, @0.25, @0.5, @0.75, @1.0], *lvL = @[@"Off", @"Quarter", @"Half", @"Three Quarters", @"Full"];   /* (as the game's EXTRAS menu) */
        _sound[3] = slider(@"Ew Volume", @"EwVolume", lv, lvL, @[@"Visitors keep their disgust to themselves", @"The \"ew\" at a quarter of the game sounds",
                           @"The \"ew\" at half the game sounds", @"The \"ew\" at three quarters of the game sounds", @"The \"ew\" as loud as the other game sounds"]);
        _sound[4] = slider(@"Till Volume", @"PurchaseVolume", lv, lvL, @[@"Visitors pay quietly", @"The till at a quarter of the game sounds",
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
        _tweaks[0] = setting(@"Ew How Often", @"GrossOutGap", @[@0, @5, @15, @30, @-1], @[@"Every Time", @"Every 5 s", @"Every 15 s", @"Every 30 s", @"Off"],
                             @[@"The \"ew\" each time a visitor sees a mess, as in 1994", @"At most one \"ew\" every 5 seconds",
                               @"At most one \"ew\" every 15 seconds", @"At most one \"ew\" every 30 seconds", @"Visitors keep their disgust to themselves"]);
        _tweaks[1] = setting(@"Till How Often", @"PurchaseGap", @[@0, @5, @15, @30, @-1], @[@"Every Time", @"Every 5 s", @"Every 15 s", @"Every 30 s", @"Off"],
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
        /* The pages: the main screen keeps Game Speed and Updates; a row with a › opens a page within its page. */
        _links[0] = setting(@"Free Mode", @"page:3", @[@""], @[@""], @[@"Rules for new sandbox parks"]);
        _links[1] = setting(@"Controls", @"page:4", @[@""], @[@"Mouse and Keyboard"], @[@"The mouse, the keys and a game controller"]);
        _links[2] = setting(@"Music Options", @"page:5", @[@""], @[@""], @[@"Music type, instruments, ride and park tunes"]);
        _links[3] = setting(@"Effect Options", @"page:6", @[@""], @[@""], @[@"Game and system sounds, the ew and the till"]);
        _links[4] = setting(@"Saved Parks", @"files", @[@""], @[@"Show in Finder"], @[@"Your saved parks' folder"]);
        Setting *pages[7][10] = {
            {&_top[1], &_top[2], &_tweaks[5], &_links[0], &_links[1], &_links[4]},
            {&_display[0], &_display[5], &_display[2], &_display[4], &_tweaks[6], &_display[1], &_display[3], &_display[6], &_display[7]},
            {&_sound[0], &_sound[6], &_sound[1], &_links[2], &_links[3]},
            {&_free[0], &_free[1], &_free[2], &_free[4], &_free[3]},
            {0},
            {&_sound[8], &_sound[7], &_tweaks[2], &_tweaks[3], &_tweaks[4]},
            {&_sound[2], &_sound[5], &_sound[3], &_tweaks[0], &_sound[4], &_tweaks[1]}};
        for (int g = 0; g < 7; g++) { _npage[g] = 0; for (int k = 0; k < 10 && pages[g][k]; k++) _page[g][_npage[g]++] = pages[g][k]; }
        self.open = -1; self.hover = HIT_NONE; self.focus = -1; self.dragRow = -1;
        _updater = [NSBundle.mainBundle pathForResource:@"update" ofType:nil];
        if (![NSFileManager.defaultManager fileExistsAtPath:_updater ?: @""]) _updater = nil;
        _branch = NSBundle.mainBundle.infoDictionary[@"TPUpdateBranch"] ?: @"nightly"; _built = NSBundle.mainBundle.infoDictionary[@"TPBuiltCommit"] ?: @"";
        [self checkUpdates];
        NSDictionary *env = NSProcessInfo.processInfo.environment;
        if (env[@"TP_LAUNCHER_OPEN"]) {
            NSUInteger g = [_groups indexOfObject:[env[@"TP_LAUNCHER_OPEN"] capitalizedString]];
            self.open = g == NSNotFound ? -1 : (NSInteger)g;
        }
        if (env[@"TP_LAUNCHER_FOCUS"]) self.focus = [env[@"TP_LAUNCHER_FOCUS"] integerValue];
        if (env[@"TP_LAUNCHER_HOVER"]) self.hover = [env[@"TP_LAUNCHER_HOVER"] integerValue];
    }
    return self;
}
- (void)checkUpdates {
    if (!_updater) { _ustate = UPD_NONE; return; }
    _ustate = UPD_CHECKING; self.needsDisplay = YES;
    NSString *u = _updater, *built = _built; __weak TPLaunchView *me = self;
    if (NSProcessInfo.processInfo.environment[@"TP_LAUNCHER_SHOT"]) {   /* (development: the result in the image) */
        NSString *l = update_check(u); _latest = l; _ustate = !l ? UPD_ERROR : [l isEqual:built] ? UPD_CURRENT : UPD_AVAILABLE; return;
    }
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
        NSString *l = update_check(u);
        on_main(^{ TPLaunchView *v = me; if (!v) return;
            v->_latest = l; v->_ustate = !l ? UPD_ERROR : [l isEqual:built] ? UPD_CURRENT : UPD_AVAILABLE; v.needsDisplay = YES; });
    });
}
- (void)runUpdate {
    if (_ustate == UPD_CHECKING || _ustate == UPD_NONE) return;
    if (_ustate != UPD_AVAILABLE) { [self checkUpdates]; return; }
    NSAlert *a = [NSAlert new];
    a.messageText = [NSString stringWithFormat:@"Update to the newest %@?", [_branch isEqual:@"nightly"] ? @"Nightly" : @"version"];
    a.informativeText = [_branch isEqualToString:@"release"]   /* the downloaded app (recomp's setup/update.sh) */
        ? [NSString stringWithFormat:@"%@ is downloaded from GitHub and replaces this app. Your game, saves and settings are kept; if the game's code changed, it is rebuilt from your copy when the app starts again.", _latest ?: @"The newest release"]
        : @"The app is rebuilt from your copy of the game (a few minutes). Your saves and settings are kept.";
    [a addButtonWithTitle:@"Update"]; [a addButtonWithTitle:@"Cancel"];
    if ([a runModal] != NSAlertFirstButtonReturn) return;
    NSString *env = NSProcessInfo.processInfo.environment[@"TP_DATA"];
    NSString *root = env.length ? env : [NSHomeDirectory() stringByAppendingPathComponent:@"Games/ThemeParkNative"];
    [NSFileManager.defaultManager createDirectoryAtPath:root withIntermediateDirectories:YES attributes:nil error:nil];
    if (update_run(self.window, _updater, _branch, [root stringByAppendingPathComponent:@"update.log"])) {
        /* the new app (installed where this one was) opens, this one ends */
        NSString *app = [[NSHomeDirectory() stringByAppendingPathComponent:@"Applications"] stringByAppendingPathComponent:@"Theme Park Nightly.app"];
        if ([_branch isEqualToString:@"release"] || ![NSFileManager.defaultManager fileExistsAtPath:app]) app = NSBundle.mainBundle.bundlePath;   /* the downloaded app: replaced in place */
        NSTask *o = [NSTask new]; o.executableURL = [NSURL fileURLWithPath:@"/usr/bin/open"]; o.arguments = @[@"-n", app]; [o launchAndReturnError:nil];
        exit(0);
    }
    [self checkUpdates];
}
- (NSImage *)keyart { return _keyart; }
- (NSImage *)grass { return _grass; }
- (NSImage *)park { return _park; }
- (BOOL)parkSoft { return _parkSoft; }
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent *)e { return YES; }
- (BOOL)mouseDownCanMoveWindow { return NO; }

- (NSInteger)rowCount { return self.open < 0 ? 2 : _npage[self.open]; }
- (Setting *)rowSetting:(NSInteger)k { return self.open < 0 ? &_top[k ? 3 : 0] : _page[self.open][k]; }
static const int page_parent[7] = {-1, -1, -1, 0, 0, 2, 2};
static BOOL is_link(Setting *s) { return [s->key hasPrefix:@"page:"] || [s->key isEqual:@"files"]; }
- (NSInteger)valueIndex:(Setting *)s {
    id v = [NSUserDefaults.standardUserDefaults objectForKey:s->key];
    NSUInteger i = v ? [s->values indexOfObject:v] : NSNotFound;
    if (i == NSNotFound && [v isKindOfClass:NSNumber.class])          /* nearest numeric value */
        for (NSUInteger k = 0; k < s->values.count; k++)
            if (fabs([s->values[k] doubleValue] - [v doubleValue]) < 0.05) i = k;
    return i == NSNotFound ? 0 : (NSInteger)i;
}
- (NSString *)valueOf:(Setting *)s {
    if ([s->key isEqual:@"Updates"]) return update_label(_ustate);
    if ([s->key hasPrefix:@"page:"] && !s->labels[0].length) return [self summary:[[s->key substringFromIndex:5] integerValue]];
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
    if ([s->key isEqual:@"Updates"]) return update_note(_ustate, _branch, _built, _latest);
    if (([s->key isEqual:@"MusicType"] && [self valueIndex:s]) || ([s->key isEqual:@"RideVideos"] && [self valueIndex:s])) {
        NSString *pack = [s->key isEqual:@"MusicType"] && [self valueIndex:s] == 1 ? @"amiga-cd32" : @"3do-usa";
        NSString *root = NSProcessInfo.processInfo.environment[@"TP_DATA"] ?: [NSHomeDirectory() stringByAppendingPathComponent:@"Games/ThemeParkNative"];
        if (![NSFileManager.defaultManager fileExistsAtPath:[root stringByAppendingFormat:@"/extras/%@/manifest.json", pack]])
            if (getenv("TP_IMAGE_DIR"))   /* the downloaded app: discs are dropped on it (main.m application:openFiles:) */
                return [s->key isEqual:@"MusicType"] ? @"Not imported yet: drop your disc's image (.cue) on the app; MIDI plays until then"
                                                     : @"Not imported yet: drop your 3DO disc's image (.cue) on the app";
            return [s->key isEqual:@"MusicType"] ? @"Not imported yet: tools/extras.py <your disc's .cue>; MIDI plays until then"
                                                 : @"Not imported yet: tools/extras.py <your 3DO disc's .cue>";
    }
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
    if ([s->key isEqual:@"Updates"]) { [self runUpdate]; return; }
    if ([s->key hasPrefix:@"page:"]) { if (d > 0) { self.open = [[s->key substringFromIndex:5] integerValue]; self.focus = -1; } self.needsDisplay = YES; return; }
    if ([s->key isEqual:@"files"]) { if (d > 0) [self openFiles]; return; }
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
- (NSRect)topFrame { return NSMakeRect(56, BANNER_H + 30, WIN_W - 112, 2 * 63 + 14); }
- (NSRect)panel { return NSMakeRect(42, 132, WIN_W - 84, 512); }
- (NSRect)rowRect:(NSInteger)k {
    if (self.open < 0) { NSRect f = self.topFrame; return NSMakeRect(f.origin.x + 10, f.origin.y + 7 + 63 * k, f.size.width - 20, 63); }
    NSRect p = self.panel; CGFloat pitch = self.rowCount > 8 ? 47 : self.rowCount > 7 ? 54 : self.rowCount > 6 ? 60 : self.rowCount > 5 ? 69 : self.rowCount > 4 ? 70 : 74;
    return NSMakeRect(p.origin.x + 26, p.origin.y + (self.compact ? 74 : 80) + pitch * k, p.size.width - 52, self.compact ? pitch : 70);
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
    return NSMakeRect(NSMaxX(la) + 16, r.origin.y + (self.rowCount > 8 ? 33 : self.compact ? 36 : 46), ra.origin.x - NSMaxX(la) - 32, 10);
}
- (void)slide:(NSInteger)k to:(NSPoint)p {
    Setting *s = [self rowSetting:k]; NSRect t = [self trackRect:k]; NSInteger n = (NSInteger)s->values.count;
    double f = (p.x - t.origin.x) / t.size.width; NSInteger i = (NSInteger)lround(fmin(fmax(f, 0), 1) * (n - 1));
    if (i != [self valueIndex:s]) { [NSUserDefaults.standardUserDefaults setObject:s->values[i] forKey:s->key]; self.needsDisplay = YES; }
}
- (NSRect)groupRect:(NSInteger)g {                      /* three across: Game Display Sound */
    int w = 212, gap = 16, x0 = (WIN_W - 3 * w - 2 * gap) / 2;
    return NSMakeRect(x0 + (int)g * (w + gap), NSMaxY(self.topFrame) + 78, w, 60);
}
- (NSRect)quitRect { return NSMakeRect(WIN_W / 2 - 14 - 230, WIN_H - 92, 230, 58); }
- (NSRect)playRect { return NSMakeRect(WIN_W / 2 + 14, WIN_H - 92, 230, 58); }

- (NSInteger)hit:(NSPoint)p {
    if (NSPointInRect(p, self.playRect)) return HIT_PLAY;
    if (NSPointInRect(p, self.quitRect)) return self.open >= 0 ? HIT_BACK : HIT_QUIT;
    for (NSInteger k = 0; k < self.rowCount; k++) {
        if ([self rowSetting:k]->slider && NSPointInRect(p, NSInsetRect([self trackRect:k], -6, -8))) return 400 + k;
        if (!is_link([self rowSetting:k]) && NSPointInRect(p, NSInsetRect([self arrow:k right:NO], -6, -6))) return 100 + k;
        if (NSPointInRect(p, [self rowRect:k])) return 200 + k;
    }
    if (self.open < 0) for (NSInteger g = 0; g < 3; g++) if (NSPointInRect(p, [self groupRect:g])) return 300 + g;
    return HIT_NONE;
}
- (void)press:(NSInteger)h {
    if (h == HIT_PLAY) { self.result = YES; self.done = YES; }
    else if (h == HIT_QUIT) { self.result = NO; self.done = YES; }
    else if (h == HIT_BACK) { self.open = self.open >= 0 ? page_parent[self.open] : -1; self.focus = -1; }
    else if (h >= 300) { self.open = h - 300; self.focus = -1; }
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
    NSInteger rows = self.rowCount, items = rows + (self.open < 0 ? 3 : 0);
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
    rounded(NSInsetRect(r, 0.5, 0.5), 10, nil, [NSColor colorWithWhite:0 alpha:0.35], 1);   /* (a dark rim; no gold) */
    if (state == 2) rounded(NSInsetRect(r, 1.5, 1.5), 9, nil, [NSColor colorWithWhite:1 alpha:0.55], 2);   /* (keyboard focus) */
    label(t.uppercaseString, display(primary ? 25 : 21), primary ? gold() : cream(), NSInsetRect(r, 10, 0), NSTextAlignmentCenter, 2, YES);
}
- (void)drawBanner {                                  /* (the key art, its shading and the bulbs are TPFair's layers) */
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
    } else if (note.length) label(note, plain(14), note_c(), NSMakeRect(va.origin.x - 60, r.origin.y + (self.rowCount > 8 ? 29 : self.compact ? 32 : 42), va.size.width + 120, 20), NSTextAlignmentCenter, 0.6, YES);
    for (int side = is_link(s) ? 1 : 0; side < 2; side++) {   /* gold pennant arrows (a page's row: the right one, it opens) */
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
    switch (g) {
    case 0: return [NSString stringWithFormat:@"%@ · Intro %@ · %@", [self valueOf:&_top[2]], [self valueOf:&_top[1]], [self valueOf:&_free[0]]];
    case 1: return [NSString stringWithFormat:@"%@ · %@ · %@", [self valueOf:&_display[0]], [self valueOf:&_display[5]], [self valueOf:&_display[1]]];
    case 2: return [self valueIndex:&_sound[0]] ? @"Off" : [NSString stringWithFormat:@"Music %@ · Effects %@", [self valueOf:&_sound[6]], [self valueOf:&_sound[1]]];
    case 3: return [NSString stringWithFormat:@"%@ · %@", [self valueOf:&_free[0]], [self valueOf:&_free[4]]];
    case 5: return [self valueOf:&_sound[8]];
    case 6: return [[NSString stringWithFormat:@"Ew %@ · Till %@", [self valueOf:&_tweaks[0]], [self valueOf:&_tweaks[1]]] stringByReplacingOccurrencesOfString:@"Every " withString:@""];
    default: return @"";
    }
}
- (void)drawRect:(NSRect)dirty {
    [(TPFair *)self.fair setPageOpen:self.open >= 0];
    [self drawBanner];
    if (self.open < 0) {
        NSRect f = self.topFrame;
        NSShadow *bs = [NSShadow new]; bs.shadowColor = [NSColor colorWithWhite:0 alpha:0.7]; bs.shadowOffset = NSMakeSize(0, -5); bs.shadowBlurRadius = 14;
        [NSGraphicsContext saveGraphicsState]; [bs set]; rounded(f, 8, [rgb(16, 22, 12) colorWithAlphaComponent:0.82], nil, 0); [NSGraphicsContext restoreGraphicsState];
        rounded(f, 8, nil, rgb(198, 134, 48), 2);
        rounded(NSInsetRect(f, 6, 6), 5, nil, [rgb(170, 110, 40) colorWithAlphaComponent:0.45], 1);
        [[rgb(170, 110, 40) colorWithAlphaComponent:0.5] setFill];
        for (NSInteger k = 0; k + 1 < self.rowCount; k++) NSRectFill(NSMakeRect(f.origin.x + 24, NSMaxY([self rowRect:k]), f.size.width - 48, 1));
        for (NSInteger k = 0; k < self.rowCount; k++) [self drawRow:k];
        for (NSInteger gi = 0; gi < 3; gi++) {
            NSRect r = [self groupRect:gi];
            int st = self.focus == self.rowCount + gi ? 2 : self.hover == 300 + gi ? 1 : 0;
            [self sign:r title:_groups[gi] primary:NO state:st];
            NSString *sum = [self summary:gi];   /* (on a dark pill: the park moves behind it) */
            CGFloat sw = [sum sizeWithAttributes:@{NSFontAttributeName: plain(14), NSKernAttributeName: @0.6}].width + 26;
            rounded(NSMakeRect(NSMidX(r) - sw / 2, NSMaxY(r) + 5, sw, 22), 11, [rgb(12, 16, 8) colorWithAlphaComponent:0.78], [rgb(198, 134, 48) colorWithAlphaComponent:0.45], 1);
            label(sum, plain(14), note_c(), NSMakeRect(r.origin.x - 10, NSMaxY(r) + 6, r.size.width + 20, 20), NSTextAlignmentCenter, 0.6, YES);
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
        if (self.open == 4) [self drawControlsIn:p];
        if (self.open == 3) {
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
    label([NSString stringWithFormat:[_branch isEqualToString:@"release"] ? @"Version %@" : @"Nightly %@", build], plain(11), [note_c() colorWithAlphaComponent:0.6], NSMakeRect(WIN_W - 216, WIN_H - 22, 200, 16), NSTextAlignmentRight, 0.5, NO);
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
    /* (the signs' rects count down from the top, as TPFair's do) */
    TPFair *fair = [[TPFair alloc] initWithSize:frame.size park:v.park soft:v.parkSoft keyart:v.keyart grass:v.grass banner:BANNER_H play:v.playRect quit:v.quitRect];
    v.fair = fair;
    const char *te = getenv("TP_LAUNCHER_TIME"); double t0 = te ? atof(te) : 3.0;
    [fair setTime:t0];
    NSString *shot = NSProcessInfo.processInfo.environment[@"TP_LAUNCHER_SHOT"];
    if (shot) {                                             /* bounded preview render: the layers behind, the settings, the layers over */
        int W = WIN_W * 2, H = WIN_H * 2;
        CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        CGContextRef cx = CGBitmapContextCreate(NULL, W, H, 8, 0, cs, kCGImageAlphaPremultipliedLast); CGColorSpaceRelease(cs);
        CGContextScaleCTM(cx, 2, 2);
        [fair.back renderInContext:cx];
        NSBitmapImageRep *rep = [v bitmapImageRepForCachingDisplayInRect:v.bounds];
        [v cacheDisplayInRect:v.bounds toBitmapImageRep:rep];
        CGContextDrawImage(cx, CGRectMake(0, 0, WIN_W, WIN_H), rep.CGImage);
        [fair.front renderInContext:cx];
        CGImageRef img = CGBitmapContextCreateImage(cx);
        NSBitmapImageRep *out = [[NSBitmapImageRep alloc] initWithCGImage:img];
        [[out representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:shot atomically:YES];
        CGImageRelease(img); CGContextRelease(cx);
        return NO;
    }
    NSWindow *w = [[NSWindow alloc] initWithContentRect:frame
                                              styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskFullSizeContentView
                                                backing:NSBackingStoreBuffered defer:NO];
    w.releasedWhenClosed = NO; w.titlebarAppearsTransparent = YES; w.titleVisibility = NSWindowTitleHidden; w.movableByWindowBackground = YES;
    w.title = @"Theme Park"; w.backgroundColor = NSColor.blackColor; w.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
    NSView *root = [[NSView alloc] initWithFrame:frame]; root.wantsLayer = YES;
    [root.layer addSublayer:fair.back];
    v.wantsLayer = YES; v.layer.backgroundColor = NSColor.clearColor.CGColor; [root addSubview:v];   /* (drawn only when a setting changes) */
    v.layer.zPosition = 1;
    fair.front.zPosition = 2; [root.layer addSublayer:fair.front];
    w.contentView = root;
    [w center];
    [w makeKeyAndOrderFront:nil];
    [w makeFirstResponder:v];
    [NSApp activateIgnoringOtherApps:YES];
    BOOL still = NSWorkspace.sharedWorkspace.accessibilityDisplayShouldReduceMotion;   /* (Reduce Motion: it holds still) */
    double start = CACurrentMediaTime() - t0;
    static double prof_n, prof_sum, prof_max, prof_t0; const char *prof = getenv("TP_LAUNCHER_PROFILE");   /* (development: ticks' cost, then quits) */
    NSTimer *tick = still ? nil : [NSTimer timerWithTimeInterval:1.0 / 60 repeats:YES block:^(NSTimer *tm) { (void)tm;
        double a = CACurrentMediaTime(); [fair setTime:a - start]; [CATransaction flush]; double d = CACurrentMediaTime() - a;
        if (prof) { if (!prof_t0) prof_t0 = a; prof_n++; prof_sum += d; if (d > prof_max) prof_max = d;
                    if (a - prof_t0 > atof(prof)) { fprintf(stderr, "launcher: %.0f ticks in %.1f s (%.1f/s), %.2f ms each, longest %.2f ms\n", prof_n, a - prof_t0, prof_n / (a - prof_t0), 1000 * prof_sum / prof_n, 1000 * prof_max); v.done = YES; } } }];
    if (tick) [NSRunLoop.mainRunLoop addTimer:tick forMode:NSRunLoopCommonModes];
    while (!v.done && w.isVisible) {
        @autoreleasepool {
            NSEvent *e = [NSApp nextEventMatchingMask:NSEventMaskAny untilDate:[NSDate dateWithTimeIntervalSinceNow:1.0 / 60]
                                               inMode:NSDefaultRunLoopMode dequeue:YES];
            if (e) [NSApp sendEvent:e];
        }
    }
    [tick invalidate];
    [w orderOut:nil];
    return v.done && v.result;
}
