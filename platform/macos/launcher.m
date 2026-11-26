/* Theme Park's launch window: the midway at dusk (the player's choice of three directions, 2026-11-25; raised to the
 * Prince of Persia launcher's fidelity, the ports' standard). Laid out as the other new launchers are: the title on a lit
 * marquee sign, a framed poster on the right, the settings' groups as ride tickets ("attractions"), a row of quick
 * settings, and Saved Parks / Updates / Quit and an ADMIT ONE ticket for Play along the bottom. A ticket opens its page
 * (a board over the window, Back returns).
 *
 * Behind it all, the player's own park at dusk (<data>/art/your-park.png, saved by main.m at quit; GOG's park
 * screenshots until then), panning slowly at whole-pixel sizes; searchlights sweeping, fireworks in the open sky
 * behind the tagline; bunting along the top, marquee bulbs chasing round the sign, the poster and Play,
 * a glint across the logo, motes of light rising. Core Animation layers set from one clock (TPMidway), so the settings
 * are drawn only when they change; Reduce Motion holds it still.
 *
 * Art (Resources, put there by tools/build_app.sh, never in this project): keyart.jpg and logo.png are GOG's store art,
 * downloaded at install time for this Mac only (the poster is the riders from the key art, one image pixel per display
 * pixel or finer); banner.png is the player's own title screen, rendered by the compiled game (its logo if GOG's is
 * missing). Without any of it the window falls back to drawn shapes and type. Settings live in the app's user defaults
 * and are read by main.m.
 * TP_LAUNCHER_SHOT=<png> renders the window to an image and exits (TP_LAUNCHER_OPEN=<group> with a group open,
 * TP_LAUNCHER_FOCUS=<n> with item n focused, TP_LAUNCHER_HOVER=<hit> with something under the pointer, TP_LAUNCHER_TIME=<s>
 * the animation's clock, TP_LAUNCHER_PARK=<png> another park's picture, TP_LAUNCHER_NO_ART=1 the drawn fallbacks only,
 * TP_LAUNCHER_PROFILE=<s> ticks' cost, then quits). The rows, tickets, tiles, controls and words: runtime/app_spec.c. */
#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#include "app_spec.h"                 /* (runtime/: the pages, rows, tickets, tiles, controls and words, shared with platform/sdl) */

static NSColor *rgb(int r, int g, int b) { return [NSColor colorWithSRGBRed:r / 255.0 green:g / 255.0 blue:b / 255.0 alpha:1]; }
static NSColor *rgba(int r, int g, int b, double a) { return [NSColor colorWithSRGBRed:r / 255.0 green:g / 255.0 blue:b / 255.0 alpha:a]; }
static NSColor *gold(void) { return rgb(250, 204, 82); }
static NSColor *gold_dk(void) { return rgb(176, 118, 36); }
static NSColor *cream(void) { return rgb(255, 247, 226); }
static NSColor *muted(void) { return rgb(190, 186, 214); }        /* notes: a dusk lilac grey */
static NSColor *night(double a) { return rgba(14, 16, 42, a); }
static NSColor *bulb(void) { return rgb(255, 236, 150); }
static NSFont *poster(CGFloat n) { return [NSFont fontWithName:@"Futura-CondensedExtraBold" size:n] ?: [NSFont systemFontOfSize:n weight:NSFontWeightBlack]; }
static NSFont *caps(CGFloat n) { return [NSFont fontWithName:@"Futura-Bold" size:n] ?: [NSFont systemFontOfSize:n weight:NSFontWeightBold]; }
static NSFont *print_f(CGFloat n) { return [NSFont fontWithName:@"AmericanTypewriter-Bold" size:n] ?: [NSFont monospacedSystemFontOfSize:n weight:NSFontWeightBold]; }
static NSFont *heavy(CGFloat n) { return [NSFont fontWithName:@"Optima-Bold" size:n] ?: [NSFont systemFontOfSize:n weight:NSFontWeightSemibold]; }
static NSFont *plain(CGFloat n) { return [NSFont fontWithName:@"Optima-Regular" size:n] ?: [NSFont systemFontOfSize:n]; }
static NSFont *ital(CGFloat n) { return [NSFont fontWithName:@"Optima-Italic" size:n] ?: [NSFont systemFontOfSize:n]; }

static NSShadow *shadow_of(double a, CGFloat dy, CGFloat blur) { NSShadow *s = [NSShadow new]; s.shadowColor = [NSColor colorWithWhite:0 alpha:a]; s.shadowOffset = NSMakeSize(0, -dy); s.shadowBlurRadius = blur; return s; }
static void label(NSString *s, NSFont *f, NSColor *c, NSRect r, NSTextAlignment al, CGFloat kern, BOOL shadow) {
    NSMutableParagraphStyle *p = [NSMutableParagraphStyle new]; p.alignment = al; p.lineBreakMode = NSLineBreakByTruncatingTail;
    NSMutableDictionary *a = [@{NSFontAttributeName: f, NSForegroundColorAttributeName: c, NSParagraphStyleAttributeName: p, NSKernAttributeName: @(kern)} mutableCopy];
    if (shadow) a[NSShadowAttributeName] = shadow_of(0.85, 1.5, 3);
    CGFloat h = f.ascender - f.descender;
    [s drawInRect:NSMakeRect(r.origin.x, NSMidY(r) - h / 2, r.size.width, h + 2) withAttributes:a];
}
static void wrapped(NSString *s, NSFont *f, NSColor *c, NSRect r, NSTextAlignment al) {   /* (several lines, top-aligned) */
    NSMutableParagraphStyle *p = [NSMutableParagraphStyle new]; p.alignment = al; p.lineSpacing = 2;
    [s drawWithRect:r options:NSStringDrawingUsesLineFragmentOrigin attributes:@{NSFontAttributeName: f, NSForegroundColorAttributeName: c,
        NSParagraphStyleAttributeName: p, NSShadowAttributeName: shadow_of(0.8, 1, 3)}];
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
/* a part of an image, by fractions of its size (top-down), at its own pixels */
static NSImage *crop_frac(NSImage *img, CGFloat fx, CGFloat fy, CGFloat fw, CGFloat fh) {
    CGImageRef c = [img CGImageForProposedRect:NULL context:nil hints:nil]; if (!c) return nil;
    CGFloat W = CGImageGetWidth(c), H = CGImageGetHeight(c);
    CGImageRef part = CGImageCreateWithImageInRect(c, CGRectMake(round(W * fx), round(H * fy), round(W * fw), round(H * fh))); if (!part) return nil;
    NSImage *r = [[NSImage alloc] initWithCGImage:part size:NSMakeSize(CGImageGetWidth(part), CGImageGetHeight(part))]; CGImageRelease(part); return r;
}
static CGImageRef cg_of(NSImage *img) { return img ? [img CGImageForProposedRect:NULL context:nil hints:nil] : NULL; }
static NSImage *drawn(NSSize sz, void (^draw)(NSRect r)) {   /* (two pixels a point: sharp on Retina) */
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
static void rfill(NSRect r) { NSRectFillUsingOperation(r, NSCompositingOperationSourceOver); }   /* (NSRectFill copies: a see-through colour punched holes) */
static NSString *S(const char *c) { return c ? @(c) : @""; }
static double frand(unsigned k) { k ^= k >> 13; k *= 0x5bd1e995u; k ^= k >> 15; return (k & 0xffffff) / (double)0x1000000; }   /* (steady: the same each run) */
static NSImage *bulb_img(BOOL lit, NSColor *c) {
    return drawn(NSMakeSize(22, 22), ^(NSRect r) {
        NSPoint p = NSMakePoint(11, 11);
        if (lit) { NSGradient *glow = [[NSGradient alloc] initWithStartingColor:[c colorWithAlphaComponent:0.8] endingColor:[c colorWithAlphaComponent:0]];
                   [glow drawFromCenter:p radius:0 toCenter:p radius:11 options:0]; }
        [rgb(96, 56, 16) setFill]; [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(7, 7, 8, 8)] fill];
        NSGradient *g = [[NSGradient alloc] initWithStartingColor:lit ? NSColor.whiteColor : rgb(170, 130, 70) endingColor:lit ? c : rgb(110, 76, 34)];
        [g drawInBezierPath:[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(8, 8, 6, 6)] relativeCenterPosition:NSMakePoint(-0.3, 0.3)];
    });
}
static NSImage *glow_img(NSColor *c, CGFloat rad) {          /* a soft point of light */
    return drawn(NSMakeSize(rad * 2, rad * 2), ^(NSRect r) {
        NSGradient *g = [[NSGradient alloc] initWithColorsAndLocations:NSColor.whiteColor, 0.0, [c colorWithAlphaComponent:0.9], 0.22, [c colorWithAlphaComponent:0], 1.0, nil];
        [g drawFromCenter:NSMakePoint(rad, rad) radius:0 toCenter:NSMakePoint(rad, rad) radius:rad options:0];
    });
}

/* The controls, one table for the launch window's Controls page and the in-game card (main.m, ⌘/ or Help > Controls). */
NSArray<NSArray<NSString *> *> *tp_controls_rows(void) {
    const TpAppSpec *sp = tp_app_spec(); NSMutableArray *a = [NSMutableArray array];
    for (int i = 0; i < sp->ncontrols; i++) [a addObject:@[S(sp->controls[i][0]), S(sp->controls[i][1])]];
    return a;
}

/* ---- the midway: animated layers behind and over the settings (Core Animation, set from one clock) ---- */
static CGFloat flipY(NSSize sz, CGFloat y) { return sz.height - y; }   /* (layers count up from the bottom; the window's layout counts down) */
static NSArray<NSValue *> *ring_points(NSRect r, CGFloat step) {     /* round a rectangle, clockwise from its top-left corner */
    NSMutableArray *a = [NSMutableArray array];
    CGFloat per = 2 * (r.size.width + r.size.height); int n = (int)lround(per / step);
    for (int i = 0; i < n; i++) {
        CGFloat d = per * i / n, x, y;
        if (d < r.size.width) { x = r.origin.x + d; y = r.origin.y; }
        else if ((d -= r.size.width) < r.size.height) { x = NSMaxX(r); y = r.origin.y + d; }
        else if ((d -= r.size.height) < r.size.width) { x = NSMaxX(r) - d; y = NSMaxY(r); }
        else { d -= r.size.width; x = r.origin.x; y = NSMaxY(r) - d; }
        [a addObject:[NSValue valueWithPoint:NSMakePoint(x, y)]];
    }
    return a;
}
@interface TPMidway : NSObject
- (instancetype)initWithSize:(NSSize)size park:(NSImage *)park soft:(BOOL)soft sign:(NSRect)sign logo:(NSRect)logo poster:(NSRect)poster play:(NSRect)play;
@property (readonly) CALayer *back, *front;   /* behind the settings; over them (no clicks) */
- (void)setTime:(double)t;
- (void)setPageOpen:(BOOL)open;               /* a page over the window: the sign's and the poster's lights go out */
@end
@implementation TPMidway {
    NSSize _sz; CALayer *_park; NSSize _parkSize; double _parkScale;
    NSMutableArray<CALayer *> *_beams, *_sparks, *_bunting, *_motes, *_mainOnly;
    NSMutableArray<NSArray<CALayer *> *> *_chase;   /* each ring of bulbs: the sign's, the poster's, Play's */
    CALayer *_glint; CGFloat _glintW;
}
- (CALayer *)bulbAt:(NSPoint)p into:(NSMutableArray *)ring color:(NSColor *)c {
    CALayer *b = [CALayer layer]; b.bounds = CGRectMake(0, 0, 22, 22); b.position = CGPointMake(p.x, flipY(_sz, p.y));
    b.contents = (__bridge id)cg_of(bulb_img(NO, c));
    CALayer *lit = [CALayer layer]; lit.frame = b.bounds; lit.contents = (__bridge id)cg_of(bulb_img(YES, c)); [b addSublayer:lit];
    [_front addSublayer:b]; [ring addObject:lit]; return b;
}
- (instancetype)initWithSize:(NSSize)size park:(NSImage *)park soft:(BOOL)soft sign:(NSRect)sign logo:(NSRect)logo poster:(NSRect)poster play:(NSRect)play {
    if (!(self = [super init])) return nil;
    _sz = size; _mainOnly = [NSMutableArray array];
    _back = [CALayer layer]; _back.frame = CGRectMake(0, 0, size.width, size.height); _back.backgroundColor = CGColorCreateGenericRGB(0.04, 0.04, 0.10, 1);
    _front = [CALayer layer]; _front.frame = _back.frame;
    /* the player's park (or GOG's pictures), the whole window, panning */
    if (park) {
        _parkSize = park.size; _parkScale = 0.6;                       /* GOG's pictures: shrunk a little (soft) */
        if (!soft) { _parkScale = 1;                                   /* the player's park: the smallest whole size with room to pan */
            while (_parkScale < 4 && (_parkSize.width * _parkScale < size.width * 1.15 || _parkSize.height * _parkScale < size.height * 1.15)) _parkScale++; }
        _park = [CALayer layer]; _park.contents = (__bridge id)cg_of(park); _park.magnificationFilter = kCAFilterNearest; _park.minificationFilter = kCAFilterTrilinear;
        _park.bounds = CGRectMake(0, 0, _parkSize.width * _parkScale, _parkSize.height * _parkScale); _park.anchorPoint = CGPointZero;
        [_back addSublayer:_park];
    }
    /* dusk over it: deep blue sky at the top, a plum glow at the horizon, night at the bottom */
    CAGradientLayer *dusk = [CAGradientLayer layer]; dusk.frame = _back.bounds;
    dusk.colors = @[(__bridge_transfer id)CGColorCreateGenericRGB(0.03, 0.03, 0.09, park ? 0.86 : 1), (__bridge_transfer id)CGColorCreateGenericRGB(0.10, 0.05, 0.18, park ? 0.74 : 1),
                    (__bridge_transfer id)CGColorCreateGenericRGB(0.26, 0.08, 0.22, park ? 0.66 : 1), (__bridge_transfer id)CGColorCreateGenericRGB(0.03, 0.03, 0.10, park ? 0.80 : 1)];
    dusk.locations = @[@0, @0.38, @0.62, @1]; [_back addSublayer:dusk];
    /* searchlights from the foot of the window, sweeping the sky */
    _beams = [NSMutableArray array];
    NSImage *beamImg = drawn(NSMakeSize(160, 760), ^(NSRect r) {
        NSBezierPath *b = [NSBezierPath bezierPath]; [b moveToPoint:NSMakePoint(80, 0)]; [b lineToPoint:NSMakePoint(160, 760)]; [b lineToPoint:NSMakePoint(0, 760)]; [b closePath];
        [NSGraphicsContext saveGraphicsState]; [b addClip];
        NSGradient *g = [[NSGradient alloc] initWithStartingColor:[NSColor colorWithSRGBRed:1 green:0.95 blue:0.8 alpha:0.55] endingColor:[NSColor colorWithSRGBRed:0.7 green:0.75 blue:1 alpha:0]];
        [g drawInRect:r angle:90];
        [NSGraphicsContext restoreGraphicsState];
    });
    for (int i = 0; i < 2; i++) {
        CALayer *b = [CALayer layer]; b.contents = (__bridge id)cg_of(beamImg); b.bounds = CGRectMake(0, 0, 160, 760); b.anchorPoint = CGPointMake(0.5, 0);
        b.position = CGPointMake(i ? size.width * 0.78 : size.width * 0.18, -30); b.opacity = 0.16; [_back addSublayer:b]; [_beams addObject:b];
    }
    /* fireworks: bursts of sparks in the sky, one after another */
    _sparks = [NSMutableArray array];
    NSArray *fc = @[rgb(255, 120, 90), rgb(255, 214, 90), rgb(120, 200, 255), rgb(190, 130, 255), rgb(120, 240, 150)];
    for (int b = 0; b < 3; b++) for (int i = 0; i < 26; i++) {
        CALayer *s = [CALayer layer]; s.contents = (__bridge id)cg_of(glow_img(fc[(b * 2 + (i % 2)) % fc.count], 12)); s.bounds = CGRectMake(0, 0, 24, 24); s.opacity = 0;
        [_back addSublayer:s]; [_sparks addObject:s];
    }
    /* bunting across the top: two swags of pennants */
    _bunting = [NSMutableArray array];
    NSArray *flagc = @[rgb(214, 40, 40), rgb(250, 204, 44), rgb(40, 120, 220), rgb(60, 170, 80), rgb(245, 130, 30), rgb(150, 80, 210)];
    for (int line = 0; line < 2; line++) {
        CGFloat x0 = line ? size.width * 0.48 : -10, x1 = line ? size.width + 10 : size.width * 0.52, sag = 14, ytop = 2 + line * 6;
        CAShapeLayer *str = [CAShapeLayer layer]; CGMutablePathRef sp = CGPathCreateMutable();
        CGPathMoveToPoint(sp, NULL, x0, flipY(size, ytop)); CGPathAddQuadCurveToPoint(sp, NULL, (x0 + x1) / 2, flipY(size, ytop + 2 * sag), x1, flipY(size, ytop));
        str.path = sp; CGPathRelease(sp); str.strokeColor = CGColorCreateGenericRGB(0.95, 0.92, 0.85, 0.7); str.fillColor = NULL; str.lineWidth = 1.2; [_front addSublayer:str];
        int n = (int)((x1 - x0) / 24);
        for (int i = 1; i < n; i++) {
            double u = (double)i / n; CGFloat x = x0 + (x1 - x0) * u, y = ytop + 4 * sag * u * (1 - u);
            CAShapeLayer *f = [CAShapeLayer layer]; CGMutablePathRef fp = CGPathCreateMutable();
            CGPathMoveToPoint(fp, NULL, -7, 0); CGPathAddLineToPoint(fp, NULL, 7, 0); CGPathAddLineToPoint(fp, NULL, 0, -17); CGPathCloseSubpath(fp);
            f.path = fp; CGPathRelease(fp); f.fillColor = [(NSColor *)flagc[(i + line * 3) % flagc.count] CGColor];
            f.strokeColor = CGColorCreateGenericRGB(0, 0, 0, 0.3); f.lineWidth = 0.6; f.shadowOpacity = 0.4; f.shadowOffset = CGSizeMake(0, -2); f.shadowRadius = 2;
            f.position = CGPointMake(x, flipY(size, y)); [_front addSublayer:f]; [_bunting addObject:f];
        }
    }
    /* marquee bulbs: round the sign, round the poster, round Play */
    _chase = [NSMutableArray array];
    NSArray *rings = @[@[[NSValue valueWithRect:NSInsetRect(sign, 7, 7)], @15.0, @YES], @[[NSValue valueWithRect:NSInsetRect(poster, 6, 6)], @18.0, @YES],
                       @[[NSValue valueWithRect:NSInsetRect(play, -8, -8)], @16.0, @NO]];
    for (NSArray *rd in rings) {
        NSMutableArray *ring = [NSMutableArray array];
        for (NSValue *v in ring_points([rd[0] rectValue], [rd[1] doubleValue])) {
            CALayer *b = [self bulbAt:v.pointValue into:ring color:bulb()]; if ([rd[2] boolValue]) [_mainOnly addObject:b];
        }
        [_chase addObject:ring];
    }
    /* a glint across the logo now and then */
    CALayer *logoClip = [CALayer layer]; logoClip.frame = CGRectMake(logo.origin.x, flipY(size, NSMaxY(logo)), logo.size.width, logo.size.height); logoClip.masksToBounds = YES;
    _glintW = 90; _glint = [CALayer layer]; _glint.bounds = CGRectMake(0, 0, _glintW, logo.size.height * 2.2); _glint.transform = CATransform3DMakeRotation(-0.42, 0, 0, 1);
    _glint.contents = (__bridge id)cg_of(drawn(_glint.bounds.size, ^(NSRect r) {
        NSGradient *g = [[NSGradient alloc] initWithColorsAndLocations:[NSColor colorWithWhite:1 alpha:0], 0.0, [NSColor colorWithWhite:1 alpha:0.32], 0.5, [NSColor colorWithWhite:1 alpha:0], 1.0, nil];
        [g drawInRect:r angle:0]; }));
    [logoClip addSublayer:_glint]; [_front addSublayer:logoClip]; [_mainOnly addObject:logoClip];
    /* motes of light rising through the whole window */
    _motes = [NSMutableArray array];
    CGImageRef mote = cg_of(glow_img(rgb(255, 220, 150), 5));
    for (int i = 0; i < 22; i++) { CALayer *m = [CALayer layer]; m.contents = (__bridge id)mote; m.bounds = CGRectMake(0, 0, 10, 10); [_front addSublayer:m]; [_motes addObject:m]; [_mainOnly addObject:m]; }
    return self;
}
- (void)setPageOpen:(BOOL)open {
    [CATransaction begin]; [CATransaction setDisableActions:YES];
    for (CALayer *l in _mainOnly) l.hidden = open;
    [CATransaction commit];
}
- (void)setTime:(double)t {
    [CATransaction begin]; [CATransaction setDisableActions:YES];
    NSSize sz = _sz;
    if (_park) {                                       /* a slow pan round the park: corner to corner, there and back (~2 min) */
        CGFloat w = _park.bounds.size.width, h = _park.bounds.size.height, mx = MAX(0, w - sz.width), my = MAX(0, h - sz.height);
        double u = 0.5 - 0.5 * cos(t * 2 * M_PI / 130), v = 0.5 - 0.5 * cos(t * 2 * M_PI / 97 + 1.1);
        CGFloat q = MAX(_parkScale, 0.5), x = -round(mx * u / q) * q, y = -round(my * v / q) * q;   /* (whole park pixels: no shimmer) */
        if (w < sz.width) x = round((sz.width - w) / 2); if (h < sz.height) y = round((sz.height - h) / 2);
        _park.position = CGPointMake(x, y);
    }
    for (NSUInteger i = 0; i < _beams.count; i++)       /* searchlights: slow sweeps, out of step */
        _beams[i].transform = CATransform3DMakeRotation((i ? 0.32 : -0.32) + 0.38 * sin(t * (i ? 0.23 : 0.19) + i * 2.1), 0, 0, 1);
    for (int b = 0; b < 3; b++) {                       /* fireworks: each burst 2.4 s, then dark; three, out of step */
        double cyc = 7.5, d = fmod(t + b * 2.6, cyc);
        unsigned round = (unsigned)floor((t + b * 2.6) / cyc) * 3 + b;
        CGFloat cx = 110 + 460 * frand(round * 41 + 7), cy = sz.height - (205 + 80 * frand(round * 43 + 11));   /* (the sky round the tagline: no panel over it) */
        for (int i = 0; i < 26; i++) {
            CALayer *s = _sparks[b * 26 + i];
            if (d > 2.4) { s.opacity = 0; continue; }
            double a = i * 2 * M_PI / 26 + frand(round * 7 + i) * 0.2, sp = 70 + 40 * frand(round * 13 + i);
            double r = sp * (1 - exp(-d * 2.2));
            s.position = CGPointMake(cx + r * cos(a), cy + r * sin(a) - 22 * d * d);
            s.opacity = d < 0.15 ? d / 0.15 : (float)MAX(0, 1 - (d - 0.15) / 2.25);
            CGFloat k = 1 - 0.5 * d / 2.4; s.transform = CATransform3DMakeScale(k, k, 1);
        }
    }
    for (NSUInteger i = 0; i < _bunting.count; i++) _bunting[i].transform = CATransform3DMakeRotation(0.08 * sin(t * 2.1 + i * 0.7), 0, 0, 1);
    long step = (long)floor(t * 8);                     /* the chase: every third bulb, moving on 8 times a second */
    for (NSUInteger r = 0; r < _chase.count; r++) {
        NSArray<CALayer *> *ring = _chase[r];
        for (NSUInteger i = 0; i < ring.count; i++) { long k = (long)i - step * (r == 1 ? -1 : 1); ring[i].opacity = ((k % 3) + 3) % 3 == 0 ? 1 : 0.1; }
    }
    {   double g = fmod(t, 7.0);                        /* the glint: across the logo in 1.1 s, every 7 s */
        CGFloat W = _glint.superlayer.bounds.size.width, H = _glint.superlayer.bounds.size.height;
        _glint.position = CGPointMake(g < 1.1 ? -_glintW + (W + 2 * _glintW) * g / 1.1 : -999, H / 2); }
    for (NSUInteger i = 0; i < _motes.count; i++) {     /* motes rising, swaying, twinkling */
        CALayer *m = _motes[i]; double sp = 9 + 10 * frand((unsigned)i * 11 + 3), span = sz.height + 40;
        double y = fmod(t * sp + span * frand((unsigned)i * 13 + 5), span) - 20;
        double x = sz.width * frand((unsigned)i * 17 + 9) + 14 * sin(t * (0.4 + 0.3 * frand((unsigned)i * 19)) + i);
        m.position = CGPointMake(x, y); m.opacity = (float)(0.25 + 0.45 * (0.5 + 0.5 * sin(t * (1.3 + frand((unsigned)i * 23)) + i * 1.7)));
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
NSString *tp_tune_name(int t) { return S(tp_tune_label(t)); }
NSString *tp_tune_note(int t) { return S(tp_tune_about(t)); }    /* which rides play it in the game */
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
        NSTextField *l = [NSTextField labelWithString:S(tp_ride_type_name(i))]; l.frame = NSMakeRect(12, y + 3, 180, 20); [doc addSubview:l];
        NSPopUpButton *pb = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(200, y, 250, 26) pullsDown:NO];
        [pb addItemWithTitle:tp_ride_type_tune(i) ? [NSString stringWithFormat:@"The Game's (%@)", tp_tune_name(tp_ride_type_tune(i))] : @"The Game's (none)"];
        [pb addItemWithTitle:@"None"];
        for (int t = 1; t <= TP_TUNES; t++) [pb addItemWithTitle:tp_tune_name(t)];
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
@property (weak) id midway;               /* TPMidway: told when a page opens */
- (NSImage *)park; - (BOOL)parkSoft;
- (NSRect)signRect; - (NSRect)logoRect; - (NSRect)posterRect; - (NSRect)playRect;
@end

enum { HIT_NONE = -1, HIT_PLAY = 1000, HIT_QUIT = 1001, HIT_BACK = 1002, HIT_SAVES = 1003, HIT_UPDATES = 1004 };
enum { N_CARDS = 5, N_TILES = 4, IT_TILE = 5, IT_SAVES = 9, IT_UPDATES = 10, IT_QUIT = 11, IT_PLAY = 12, N_ITEMS = 13 };
#define WIN_W 1000
#define WIN_H 700

/* ---- drawing pieces ---- */
/* Fill inside `draw` clipped to a ticket: semicircle notches cut where the stub's perforation meets the edges
 * (vertical: across the ticket at y = n; else down it at x = n). */
static void ticket_clip(NSRect r, BOOL across, CGFloat n, CGFloat nr, void (^draw)(void)) {
    [NSGraphicsContext saveGraphicsState];
    NSBezierPath *clip = [NSBezierPath bezierPathWithRect:NSInsetRect(r, -30, -30)];
    if (across) { [clip appendBezierPathWithOvalInRect:NSMakeRect(r.origin.x - nr, n - nr, 2 * nr, 2 * nr)];
                  [clip appendBezierPathWithOvalInRect:NSMakeRect(NSMaxX(r) - nr, n - nr, 2 * nr, 2 * nr)]; }
    else { [clip appendBezierPathWithOvalInRect:NSMakeRect(n - nr, r.origin.y - nr, 2 * nr, 2 * nr)];
           [clip appendBezierPathWithOvalInRect:NSMakeRect(n - nr, NSMaxY(r) - nr, 2 * nr, 2 * nr)]; }
    clip.windingRule = NSWindingRuleEvenOdd; [clip addClip];
    draw();
    [NSGraphicsContext restoreGraphicsState];
}
static void perforation(NSPoint a, NSPoint b, NSColor *c) {
    CGFloat len = hypot(b.x - a.x, b.y - a.y); int n = (int)(len / 6);
    [c setFill];
    for (int i = 1; i < n; i++) { CGFloat u = (CGFloat)i / n; NSPoint p = NSMakePoint(a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u);
        [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(p.x - 1.3, p.y - 1.3, 2.6, 2.6)] fill]; }
}
static void star(NSPoint c, CGFloat r, NSColor *col) {
    NSBezierPath *s = [NSBezierPath bezierPath];
    for (int i = 0; i < 10; i++) { double a = -M_PI / 2 + i * M_PI / 5; CGFloat rr = i % 2 ? r * 0.45 : r;
        NSPoint p = NSMakePoint(c.x + rr * cos(a), c.y + rr * sin(a)); if (i) [s lineToPoint:p]; else [s moveToPoint:p]; }
    [s closePath]; [col setFill]; [s fill];
}
/* A section's heading: kerned gold caps, a star, a rule running on to `x1`. */
static void heading(NSString *t, NSPoint at, CGFloat x1) {
    NSDictionary *a = @{NSFontAttributeName: caps(11), NSForegroundColorAttributeName: gold(), NSKernAttributeName: @5, NSShadowAttributeName: shadow_of(0.9, 1, 3)};
    [t drawAtPoint:NSMakePoint(at.x, at.y - 8) withAttributes:a];
    CGFloat w = [t sizeWithAttributes:a].width;
    star(NSMakePoint(at.x + w + 10, at.y + 1), 4.5, gold());
    NSGradient *g = [[NSGradient alloc] initWithStartingColor:rgba(250, 204, 82, 0.7) endingColor:rgba(250, 204, 82, 0)];
    [g drawInRect:NSMakeRect(at.x + w + 20, at.y + 0.5, x1 - at.x - w - 20, 1) angle:0];
}
/* The icons on the tickets (cream line drawings in a 44-point box) */
static void icon(NSInteger g, NSRect b, NSColor *c) {
    NSPoint m = NSMakePoint(NSMidX(b), NSMidY(b)); [c setStroke]; [c setFill];
    switch (g) {
    case 0: {   /* Game: a Ferris wheel */
        CGFloat R = 15; NSBezierPath *p = [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(m.x - R, m.y - R - 3, 2 * R, 2 * R)]; p.lineWidth = 2.2; [p stroke];
        for (int k = 0; k < 8; k++) { double a = k * M_PI / 4; NSBezierPath *s = [NSBezierPath bezierPath]; s.lineWidth = 1.2;
            [s moveToPoint:NSMakePoint(m.x, m.y - 3)]; [s lineToPoint:NSMakePoint(m.x + R * cos(a), m.y - 3 + R * sin(a))]; [s stroke];
            [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(m.x + R * cos(a) - 2.6, m.y - 3 + R * sin(a) - 2.6, 5.2, 5.2)] fill]; }
        NSBezierPath *l = [NSBezierPath bezierPath]; l.lineWidth = 2.2; l.lineCapStyle = NSLineCapStyleRound;
        [l moveToPoint:NSMakePoint(m.x - 12, m.y + 20)]; [l lineToPoint:NSMakePoint(m.x, m.y - 3)]; [l lineToPoint:NSMakePoint(m.x + 12, m.y + 20)]; [l stroke];
        break; }
    case 1: {   /* Display: a 1994 monitor */
        NSRect s = NSMakeRect(m.x - 18, m.y - 16, 36, 27); rounded(s, 4, nil, c, 2.2);
        rounded(NSInsetRect(s, 5, 4.5), 3, [c colorWithAlphaComponent:0.35], nil, 0);
        NSBezierPath *st = [NSBezierPath bezierPath]; st.lineWidth = 2.2; st.lineCapStyle = NSLineCapStyleRound;
        [st moveToPoint:NSMakePoint(m.x - 5, m.y + 12)]; [st lineToPoint:NSMakePoint(m.x - 8, m.y + 18)]; [st lineToPoint:NSMakePoint(m.x + 8, m.y + 18)]; [st lineToPoint:NSMakePoint(m.x + 5, m.y + 12)]; [st stroke];
        break; }
    case 2: {   /* Sound: a speaker and its waves */
        NSBezierPath *sp = [NSBezierPath bezierPath];
        [sp moveToPoint:NSMakePoint(m.x - 16, m.y - 6)]; [sp lineToPoint:NSMakePoint(m.x - 9, m.y - 6)]; [sp lineToPoint:NSMakePoint(m.x, m.y - 14)];
        [sp lineToPoint:NSMakePoint(m.x, m.y + 14)]; [sp lineToPoint:NSMakePoint(m.x - 9, m.y + 6)]; [sp lineToPoint:NSMakePoint(m.x - 16, m.y + 6)]; [sp closePath]; [sp fill];
        for (int k = 1; k <= 3; k++) { NSBezierPath *w = [NSBezierPath bezierPath]; w.lineWidth = 2; w.lineCapStyle = NSLineCapStyleRound;
            [w appendBezierPathWithArcWithCenter:NSMakePoint(m.x + 1, m.y) radius:5 + 5 * k startAngle:-40 endAngle:40]; [w stroke]; }
        break; }
    case 3: {   /* Free Mode: endless (a lemniscate) */
        NSBezierPath *p = [NSBezierPath bezierPath]; p.lineWidth = 3; p.lineJoinStyle = NSLineJoinStyleRound;
        for (int i = 0; i <= 64; i++) { double t = i * 2 * M_PI / 64, d = 1 + sin(t) * sin(t);
            NSPoint q = NSMakePoint(m.x + 19 * cos(t) / d, m.y + 19 * sin(t) * cos(t) / d); if (i) [p lineToPoint:q]; else [p moveToPoint:q]; }
        [p stroke]; break; }
    default: {  /* Controls: a mouse */
        NSRect mo = NSMakeRect(m.x - 11, m.y - 12, 22, 31); rounded(mo, 11, nil, c, 2.2);
        NSBezierPath *s = [NSBezierPath bezierPath]; s.lineWidth = 1.6;
        [s moveToPoint:NSMakePoint(m.x, m.y - 12)]; [s lineToPoint:NSMakePoint(m.x, m.y - 1)]; [s moveToPoint:NSMakePoint(m.x - 11, m.y - 1)]; [s lineToPoint:NSMakePoint(m.x + 11, m.y - 1)]; [s stroke];
        NSBezierPath *cord = [NSBezierPath bezierPath]; cord.lineWidth = 1.6;
        [cord moveToPoint:NSMakePoint(m.x, m.y - 12)]; [cord curveToPoint:NSMakePoint(m.x + 12, m.y - 20) controlPoint1:NSMakePoint(m.x, m.y - 20) controlPoint2:NSMakePoint(m.x + 8, m.y - 14)]; [cord stroke];
        break; }
    }
}

@implementation TPLaunchView {
    NSImage *_logo, *_park, *_posterImg; BOOL _parkSoft;
    Setting _rows[64]; int _nrows;                 /* every row of runtime/app_spec.c, as the view uses them */
    int _ustate; NSString *_latest, *_updater, *_branch, *_built;   /* the Updates button (below) */
    NSArray<NSString *> *_groups;                  /* the pages: the five tickets', then Music and Effects (within Sound) */
    Setting *_page[7][10]; int _npage[7];
    Setting *_tile[N_TILES];
}
- (instancetype)initWithFrame:(NSRect)f {
    if ((self = [super initWithFrame:f])) {
        NSBundle *b = NSBundle.mainBundle; NSString *p;
        NSString *art = getenv("TP_IMAGE_DIR") && getenv("TP_DATA") ? [@(getenv("TP_DATA")) stringByAppendingPathComponent:@"art"] : nil;   /* the downloaded app: setup's art */
        NSImage *keyart = nil;
        if ((p = [b pathForResource:@"keyart" ofType:@"jpg"]) || (art && [NSFileManager.defaultManager fileExistsAtPath:(p = [art stringByAppendingPathComponent:@"keyart.jpg"])]))
            keyart = [[NSImage alloc] initWithContentsOfFile:p];
        if ((p = [b pathForResource:@"logo" ofType:@"png"]) || (art && [NSFileManager.defaultManager fileExistsAtPath:(p = [art stringByAppendingPathComponent:@"logo.png"])]))
            _logo = trimmed([[NSImage alloc] initWithContentsOfFile:p]);
        NSImage *title = (p = [b pathForResource:@"banner" ofType:@"png"]) ? [[NSImage alloc] initWithContentsOfFile:p] : nil;
        if (title && !_logo) _logo = pixel_crop(title, CGRectMake(58, 17, 206, 36), 8);   /* the title screen's logo box, if GOG's art is missing */
        if (keyart && [keyart CGImageForProposedRect:NULL context:nil hints:nil] && CGImageGetWidth([keyart CGImageForProposedRect:NULL context:nil hints:nil]) >= 1600)
            _posterImg = crop_frac(keyart, 0.484, 0, 0.25, 0.978);   /* the riders at the front of the car (above the key art's grey foot) */
        {   /* the player's own park (main.m saves its picture at quit), for the backdrop */
            const char *d = getenv("TP_DATA"), *h = getenv("HOME");
            NSString *root = d && *d ? @(d) : [(h && *h ? @(h) : NSHomeDirectory()) stringByAppendingPathComponent:@"Games/ThemeParkNative"];
            NSString *pp = [root stringByAppendingPathComponent:@"art/your-park.png"];
            if (getenv("TP_LAUNCHER_PARK")) pp = @(getenv("TP_LAUNCHER_PARK"));    /* (development: another park's picture) */
            NSImage *pk = [[NSImage alloc] initWithContentsOfFile:pp];
            NSBitmapImageRep *rep = (NSBitmapImageRep *)pk.representations.firstObject;
            if (pk && [rep isKindOfClass:NSBitmapImageRep.class]) { pk.size = NSMakeSize(rep.pixelsWide, rep.pixelsHigh); if (pk.size.width >= 320) _park = pk; }
            if (!_park) {   /* none yet: GOG's pictures of a park (fetched at install), one image pixel per display pixel */
                NSString *art2 = [root stringByAppendingPathComponent:@"art"]; NSMutableArray *shots = [NSMutableArray array];
                for (NSString *f2 in [[NSFileManager.defaultManager contentsOfDirectoryAtPath:art2 error:nil] sortedArrayUsingSelector:@selector(compare:)])
                    if ([f2 hasPrefix:@"park-shot-"] && [f2 hasSuffix:@".png"]) { NSImage *i = [[NSImage alloc] initWithContentsOfFile:[art2 stringByAppendingPathComponent:f2]]; if (i) [shots addObject:i]; }
                if (shots.count) {   /* side by side, two rows, the second turned round so no picture meets itself */
                    NSBitmapImageRep *r0 = (NSBitmapImageRep *)[shots[0] representations].firstObject; CGFloat sw = r0.pixelsWide, sh = r0.pixelsHigh;
                    NSUInteger n = MIN(shots.count, 4);
                    _park = drawn_px(NSMakeSize(sw * n, sh * 2), ^(NSRect r) {
                        for (NSUInteger k = 0; k < n; k++) {
                            [(NSImage *)shots[k] drawInRect:NSMakeRect(sw * k, sh, sw, sh)];
                            [(NSImage *)shots[(k + 1) % n] drawInRect:NSMakeRect(sw * k, 0, sw, sh)]; } });
                    _parkSoft = YES;
                }
            }
        }
        if (getenv("TP_LAUNCHER_NO_ART")) { _logo = nil; _posterImg = nil; _park = nil; _parkSoft = NO; }   /* (references: the drawn fallbacks only,
                                                        * no GOG art and no park picture: tools/launcher_reference.sh) */
        {   /* the settings, pages and tiles: runtime/app_spec.c */
            const TpAppSpec *sp = tp_app_spec(); NSMutableArray *groups = [NSMutableArray array];
            for (int g = 0; g < TP_NPAGES; g++) {
                const TpPage *pg = &sp->pages[g]; [groups addObject:S(pg->title)]; _npage[g] = 0;
                for (int k = 0; k < pg->n && k < 10; k++) _page[g][_npage[g]++] = [self settingFor:pg->rows[k]];
            }
            _groups = groups;
            for (int k = 0; k < sp->ntiles && k < N_TILES; k++) _tile[k] = [self settingFor:sp->tiles[k]];
        }
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
/* A row of the spec as the view's Setting (made once per row) */
- (Setting *)settingFor:(const TpRow *)r {
    NSString *key = r->kind == TP_ROW_PAGE ? [NSString stringWithFormat:@"page:%d", r->page] : r->kind == TP_ROW_FILES ? @"files" : S(r->key);
    for (int i = 0; i < _nrows; i++) if ([_rows[i].key isEqual:key]) return &_rows[i];
    NSMutableArray *v = [NSMutableArray array], *l = [NSMutableArray array], *n = [NSMutableArray array];
    for (int i = 0; i < r->n; i++) {
        double x = r->choices[i].value;
        [v addObject:r->kind == TP_ROW_BOOL ? @(x != 0) : x == floor(x) ? @((long long)x) : @(x)];
        [l addObject:S(r->choices[i].label)]; [n addObject:S(r->choices[i].note)];
    }
    if (!r->n) { [v addObject:@""]; [l addObject:r->kind == TP_ROW_PAGE ? @"" : S(r->value)]; [n addObject:S(r->note)]; }
    Setting st = {S(r->title), key, v, l, n, r->kind == TP_ROW_SLIDER};
    _rows[_nrows] = st; return &_rows[_nrows++];
}
- (Setting *)key:(NSString *)k { const TpRow *r = tp_app_row(k.UTF8String); return r ? [self settingFor:r] : NULL; }
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
- (NSImage *)park { return _park; }
- (BOOL)parkSoft { return _parkSoft; }
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent *)e { return YES; }
- (BOOL)mouseDownCanMoveWindow { return NO; }

- (NSInteger)rowCount { return self.open < 0 ? 0 : _npage[self.open]; }
- (Setting *)rowSetting:(NSInteger)k { return _page[self.open][k]; }
static int page_parent_of(NSInteger g) { return g >= 0 && g < TP_NPAGES ? tp_app_spec()->pages[g].parent : -1; }
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
- (void)stepSetting:(Setting *)s by:(NSInteger)d {
    if ([s->key hasPrefix:@"page:"]) { if (d > 0) { self.open = [[s->key substringFromIndex:5] integerValue]; self.focus = -1; } self.needsDisplay = YES; return; }
    if ([s->key isEqual:@"files"]) { if (d > 0) [self openFiles]; return; }
    if ([s->key isEqual:@"RideTunes"]) {                 /* → the per-ride window, ← back to the game's */
        if (d < 0) [NSUserDefaults.standardUserDefaults removeObjectForKey:@"RideTunes"]; else tp_ride_tunes_window(self.window);
        self.needsDisplay = YES; return;
    }
    if ([s->key isEqual:@"SoundBank"]) {                 /* → opens a file, ← clears it */
        if (d < 0) [NSUserDefaults.standardUserDefaults removeObjectForKey:@"SoundBank"];
        else {
            NSOpenPanel *op = [NSOpenPanel openPanel];
            op.message = @"Choose a SoundFont (.sf2) or DLS file for the music";
            op.allowedFileTypes = @[@"sf2", @"dls", @"SF2", @"DLS"];
            if ([op runModal] == NSModalResponseOK) [NSUserDefaults.standardUserDefaults setObject:op.URL.path forKey:@"SoundBank"];
        }
        self.needsDisplay = YES; return;
    }
    NSInteger n = (NSInteger)s->values.count;
    [NSUserDefaults.standardUserDefaults setObject:s->values[([self valueIndex:s] + d + n) % n] forKey:s->key];
    self.needsDisplay = YES;
}
- (void)step:(NSInteger)k by:(NSInteger)d { [self stepSetting:[self rowSetting:k] by:d]; }

/* ---- layout (top-down points) ---- */
- (NSRect)signRect { return NSMakeRect(44, 30, 584, 166); }
- (NSRect)logoRect {
    NSRect s = self.signRect, a = NSMakeRect(s.origin.x + 44, s.origin.y + 50, s.size.width - 88, s.size.height - 72);
    if (!_logo.size.width) return a;
    CGFloat k = MIN(a.size.width / _logo.size.width, a.size.height / _logo.size.height), w = round(_logo.size.width * k), h = round(_logo.size.height * k);
    return NSMakeRect(round(NSMidX(a) - w / 2), round(NSMidY(a) - h / 2), w, h);
}
- (NSRect)posterRect { return NSMakeRect(664, 30, 292, 412); }
- (NSRect)cardRect:(NSInteger)g { CGFloat gap = 12, w = (584 - 4 * gap) / N_CARDS; return NSMakeRect(44 + g * (w + gap), 310, w, 132); }
- (NSRect)tileRect:(NSInteger)k { CGFloat gap = 12, w = (912 - 3 * gap) / N_TILES; return NSMakeRect(44 + k * (w + gap), 484, w, 82); }
- (NSRect)savesRect { return NSMakeRect(44, 592, 176, 56); }
- (NSRect)updatesRect { return NSMakeRect(232, 592, 236, 56); }
- (NSRect)quitRect { return NSMakeRect(556, 592, 162, 56); }
- (NSRect)playRect { return NSMakeRect(734, 588, 222, 64); }
- (NSRect)board { return NSMakeRect(44, 30, 912, 536); }
- (NSRect)rowRect:(NSInteger)k {
    NSRect p = self.board; CGFloat top = p.origin.y + 96, avail = NSMaxY(p) - 20 - top - (tp_app_spec()->pages[self.open].footnote[0] ? 56 : 0);
    CGFloat pitch = MIN(72, floor(avail / MAX(1, self.rowCount)));
    return NSMakeRect(p.origin.x + 34, top + pitch * k, p.size.width - 68, pitch);
}
- (BOOL)compact { return self.rowCount > 6; }
- (NSRect)arrow:(NSInteger)k right:(BOOL)right {
    NSRect r = [self rowRect:k];
    return NSMakeRect(right ? NSMaxX(r) - 44 : NSMaxX(r) - 420, r.origin.y + (self.compact ? 3 : 8), 34, 30);
}
- (NSRect)trackRect:(NSInteger)k {
    NSRect la = [self arrow:k right:NO], ra = [self arrow:k right:YES], r = [self rowRect:k];
    return NSMakeRect(NSMaxX(la) + 16, r.origin.y + (self.compact ? 34 : 44), ra.origin.x - NSMaxX(la) - 32, 9);
}
- (void)slide:(NSInteger)k to:(NSPoint)p {
    Setting *s = [self rowSetting:k]; NSRect t = [self trackRect:k]; NSInteger n = (NSInteger)s->values.count;
    double f = (p.x - t.origin.x) / t.size.width; NSInteger i = (NSInteger)lround(fmin(fmax(f, 0), 1) * (n - 1));
    if (i != [self valueIndex:s]) { [NSUserDefaults.standardUserDefaults setObject:s->values[i] forKey:s->key]; self.needsDisplay = YES; }
}

/* ---- input ---- */
- (NSInteger)hit:(NSPoint)p {
    if (NSPointInRect(p, NSInsetRect(self.playRect, -4, -4))) return HIT_PLAY;
    if (NSPointInRect(p, self.quitRect)) return self.open >= 0 ? HIT_BACK : HIT_QUIT;
    if (self.open < 0) {
        if (NSPointInRect(p, self.savesRect)) return HIT_SAVES;
        if (NSPointInRect(p, self.updatesRect)) return HIT_UPDATES;
        for (NSInteger g = 0; g < N_CARDS; g++) if (NSPointInRect(p, [self cardRect:g])) return 300 + g;
        for (NSInteger k = 0; k < N_TILES; k++) if (NSPointInRect(p, [self tileRect:k])) return 500 + k;
        return HIT_NONE;
    }
    for (NSInteger k = 0; k < self.rowCount; k++) {
        if ([self rowSetting:k]->slider && NSPointInRect(p, NSInsetRect([self trackRect:k], -6, -8))) return 400 + k;
        if (!is_link([self rowSetting:k]) && NSPointInRect(p, NSInsetRect([self arrow:k right:NO], -6, -6))) return 100 + k;
        if (NSPointInRect(p, [self rowRect:k])) return 200 + k;
    }
    return HIT_NONE;
}
- (void)press:(NSInteger)h back:(BOOL)back {
    if (h == HIT_PLAY) { self.result = YES; self.done = YES; }
    else if (h == HIT_QUIT) { self.result = NO; self.done = YES; }
    else if (h == HIT_BACK) { self.open = self.open >= 0 ? page_parent_of(self.open) : -1; self.focus = -1; }
    else if (h == HIT_SAVES) [self openFiles];
    else if (h == HIT_UPDATES) [self runUpdate];
    else if (h >= 500) [self stepSetting:_tile[h - 500] by:back ? -1 : 1];
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
    self.dragRow = -1;
    BOOL back = h >= 500 && h < 600 && (e.modifierFlags & NSEventModifierFlagShift || p.x < NSMinX([self tileRect:h - 500]) + 44);   /* (a tile's left end, or shift: back a step) */
    if (h >= 500 && h < 600) self.focus = IT_TILE + h - 500;
    [self press:h back:back];
}
- (void)rightMouseDown:(NSEvent *)e { NSInteger h = [self hit:[self convertPoint:e.locationInWindow fromView:nil]]; if (h >= 500 && h < 600) [self press:h back:YES]; }
- (void)mouseDragged:(NSEvent *)e { if (self.dragRow >= 0) [self slide:self.dragRow to:[self convertPoint:e.locationInWindow fromView:nil]]; }
- (void)mouseUp:(NSEvent *)e { self.dragRow = -1; }
- (NSInteger)hitOfItem:(NSInteger)i {
    return i < N_CARDS ? 300 + i : i < IT_SAVES ? 500 + i - IT_TILE : i == IT_SAVES ? HIT_SAVES : i == IT_UPDATES ? HIT_UPDATES : i == IT_QUIT ? HIT_QUIT : HIT_PLAY;
}
- (void)keyDown:(NSEvent *)e {
    if (self.open < 0) {                                   /* the main window: tickets, tiles, the bottom row */
        NSInteger f = self.focus, row = f < 0 ? -1 : f < N_CARDS ? 0 : f < IT_SAVES ? 1 : 2;
        NSInteger start[3] = {0, IT_TILE, IT_SAVES}, count[3] = {N_CARDS, N_TILES, 4};
        switch (e.keyCode) {
        case 0x24: case 0x4c: case 0x31: [self press:f < 0 ? HIT_PLAY : [self hitOfItem:f] back:NO]; return;
        case 0x35: [self press:HIT_QUIT back:NO]; return;
        case 0x7b: case 0x7c: {
            if (f < 0) { self.focus = e.keyCode == 0x7c ? 0 : IT_PLAY; break; }
            NSInteger d = e.keyCode == 0x7c ? 1 : -1;
            if (row == 1 && (e.modifierFlags & NSEventModifierFlagOption)) { [self stepSetting:_tile[f - IT_TILE] by:d]; break; }   /* (⌥←/→: a tile's value) */
            self.focus = start[row] + (f - start[row] + d + count[row]) % count[row]; break; }
        case 0x7e: case 0x7d: {
            NSInteger d = e.keyCode == 0x7d ? 1 : -1;
            if (f < 0) { self.focus = d > 0 ? 0 : IT_PLAY; break; }
            NSInteger nr = (row + d + 3) % 3, rel = f - start[row];
            self.focus = start[nr] + MIN(rel * count[nr] / count[row], count[nr] - 1); break; }
        default: [super keyDown:e]; return;
        }
        self.needsDisplay = YES; return;
    }
    NSInteger rows = self.rowCount;
    switch (e.keyCode) {
    case 0x24: case 0x4c: [self press:HIT_PLAY back:NO]; break;
    case 0x35: [self press:HIT_BACK back:NO]; break;
    case 0x7e: self.focus = self.focus < 0 ? rows - 1 : (self.focus + rows - 1) % MAX(rows, 1); break;
    case 0x7d: self.focus = (self.focus + 1) % MAX(rows, 1); break;
    case 0x7b: if (self.focus >= 0 && self.focus < rows) [self step:self.focus by:-1]; break;
    case 0x7c: case 0x31: if (self.focus >= 0 && self.focus < rows) [self step:self.focus by:1]; break;
    default: [super keyDown:e]; return;
    }
    self.needsDisplay = YES;
}

/* ---- drawing ---- */
- (NSString *)summary:(NSInteger)g {
    switch (g) {
    case 0: return [NSString stringWithFormat:@"%@ · %@", [self valueOf:[self key:@"Cycles"]], [self valueOf:[self key:@"Language"]]];
    case 1: return [NSString stringWithFormat:@"%@ · %@", [self valueOf:[self key:@"StartFullScreen"]], [self valueOf:[self key:@"StartHiRes"]]];
    case 2: return [self valueIndex:[self key:@"Sound"]] ? @"Off" : [NSString stringWithFormat:@"Music %@ · Effects %@", [self valueOf:[self key:@"MusicVolume"]], [self valueOf:[self key:@"SoundVolume"]]];
    case 3: return [NSString stringWithFormat:@"%@ · %@", [self valueOf:[self key:@"FreeRules"]], [self valueOf:[self key:@"FreeMapSize"]]];
    case 4: return @"Mouse, keys, controller";
    case 5: return [self valueOf:[self key:@"MusicType"]];
    case 6: return [[NSString stringWithFormat:@"Ew %@ · Till %@", [self valueOf:[self key:@"GrossOutGap"]], [self valueOf:[self key:@"PurchaseGap"]]] stringByReplacingOccurrencesOfString:@"Every " withString:@""];
    default: return @"";
    }
}
/* dark night glass, edged in gold, with small gold studs at the corners */
- (void)plate:(NSRect)r radius:(CGFloat)rad lit:(int)lit {    /* lit: 0, 1 hover, 2 focus */
    [NSGraphicsContext saveGraphicsState]; [shadow_of(0.7, 4, 12) set]; rounded(r, rad, night(lit ? 0.95 : 0.92), nil, 0); [NSGraphicsContext restoreGraphicsState];
    if (lit) { NSGradient *g = [[NSGradient alloc] initWithStartingColor:rgba(250, 204, 82, 0.16) endingColor:rgba(250, 204, 82, 0.02)];
               [g drawInBezierPath:[NSBezierPath bezierPathWithRoundedRect:r xRadius:rad yRadius:rad] angle:-90]; }
    rounded(NSInsetRect(r, 0.5, 0.5), rad, nil, lit ? gold() : rgba(250, 204, 82, 0.75), lit ? 1.6 : 1.1);
    rounded(NSInsetRect(r, 4.5, 4.5), MAX(rad - 4, 2), nil, rgba(250, 204, 82, 0.22), 1);
    for (int i = 0; i < 4; i++) { NSPoint c = NSMakePoint(i % 2 ? NSMaxX(r) - 9 : r.origin.x + 9, i / 2 ? NSMaxY(r) - 9 : r.origin.y + 9);
        [gold_dk() setFill]; [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(c.x - 2.2, c.y - 2.2, 4.4, 4.4)] fill];
        [gold() setFill]; [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(c.x - 1.3, c.y - 1.7, 2.4, 2.4)] fill]; }
    if (lit == 2) rounded(NSInsetRect(r, -3, -3), rad + 3, nil, rgba(255, 247, 226, 0.85), 2);
}
- (void)drawSign {
    NSRect s = self.signRect;
    [NSGraphicsContext saveGraphicsState]; [shadow_of(0.75, 6, 18) set]; rounded(s, 18, rgb(46, 8, 16), nil, 0); [NSGraphicsContext restoreGraphicsState];
    NSGradient *band = [[NSGradient alloc] initWithColorsAndLocations:rgb(255, 228, 140), 0.0, rgb(232, 172, 54), 0.45, rgb(150, 94, 22), 1.0, nil];
    [band drawInBezierPath:[NSBezierPath bezierPathWithRoundedRect:NSInsetRect(s, 2, 2) xRadius:16 yRadius:16] angle:-90];
    NSRect in = NSInsetRect(s, 14, 14);
    NSBezierPath *ip = [NSBezierPath bezierPathWithRoundedRect:in xRadius:8 yRadius:8];
    NSGradient *lac = [[NSGradient alloc] initWithColorsAndLocations:rgb(214, 38, 46), 0.0, rgb(168, 20, 32), 0.55, rgb(104, 10, 22), 1.0, nil];
    [lac drawInBezierPath:ip angle:-90];
    [NSGraphicsContext saveGraphicsState]; [ip addClip];          /* candy stripes in the lacquer, and a sheen across its top */
    [[NSColor colorWithWhite:1 alpha:0.055] setFill];
    for (CGFloat x = in.origin.x - in.size.height; x < NSMaxX(in); x += 26) {
        NSBezierPath *st = [NSBezierPath bezierPath]; [st moveToPoint:NSMakePoint(x, NSMaxY(in))]; [st lineToPoint:NSMakePoint(x + 12, NSMaxY(in))];
        [st lineToPoint:NSMakePoint(x + 12 + in.size.height, in.origin.y)]; [st lineToPoint:NSMakePoint(x + in.size.height, in.origin.y)]; [st closePath]; [st fill]; }
    NSGradient *sheen = [[NSGradient alloc] initWithStartingColor:[NSColor colorWithWhite:1 alpha:0.20] endingColor:[NSColor colorWithWhite:1 alpha:0]];
    [sheen drawInRect:NSMakeRect(in.origin.x, in.origin.y, in.size.width, 40) angle:90];
    [NSGraphicsContext restoreGraphicsState];
    rounded(in, 8, nil, rgb(70, 8, 16), 2);
    rounded(NSInsetRect(in, 4, 4), 5, nil, rgba(255, 220, 120, 0.5), 1);
    for (int i = 0; i < 2; i++) star(NSMakePoint(i ? NSMaxX(in) - 22 : in.origin.x + 22, in.origin.y + 22), 7, gold());
    label(S(tp_app_spec()->presents), caps(10.5), rgb(255, 226, 150), NSMakeRect(in.origin.x, in.origin.y + 12, in.size.width, 20), NSTextAlignmentCenter, 4.2, YES);
    if (_logo) {
        [NSGraphicsContext saveGraphicsState]; [shadow_of(0.6, 4, 9) set];
        [_logo drawInRect:self.logoRect fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1 respectFlipped:YES hints:@{NSImageHintInterpolation: @(NSImageInterpolationHigh)}];
        [NSGraphicsContext restoreGraphicsState];
    } else label(@"THEME PARK", poster(64), cream(), self.logoRect, NSTextAlignmentCenter, 3, YES);
}
- (void)drawPoster {
    NSRect r = self.posterRect;
    [NSGraphicsContext saveGraphicsState]; [shadow_of(0.8, 8, 20) set]; [rgb(36, 18, 10) setFill]; rfill(r); [NSGraphicsContext restoreGraphicsState];
    NSGradient *gilt = [[NSGradient alloc] initWithColorsAndLocations:rgb(255, 232, 150), 0.0, rgb(214, 156, 46), 0.35, rgb(255, 214, 110), 0.6, rgb(140, 86, 20), 1.0, nil];
    [gilt drawInRect:NSInsetRect(r, 1, 1) angle:-60];
    [rgb(80, 46, 12) setStroke]; NSFrameRectWithWidth(NSInsetRect(r, 11.5, 11.5), 1.5);
    NSRect img = NSInsetRect(r, 14, 14); img.size.height -= 58;
    NSRect plaque = NSMakeRect(img.origin.x, NSMaxY(img), img.size.width, 58 - 2);
    [NSColor.blackColor setFill]; rfill(NSUnionRect(img, plaque));
    if (_posterImg) {
        NSSize s = _posterImg.size; CGFloat k = MAX(img.size.width / s.width, img.size.height / s.height);
        NSRect src = NSMakeRect((s.width - img.size.width / k) / 2, 0, img.size.width / k, img.size.height / k);   /* (aspect fill; the top kept: the faces) */
        src.origin.y = s.height - src.size.height;         /* (NSImage's source rect counts from the bottom) */
        [_posterImg drawInRect:img fromRect:src operation:NSCompositingOperationSourceOver fraction:1 respectFlipped:YES hints:@{NSImageHintInterpolation: @(NSImageInterpolationHigh)}];
    } else {
        NSGradient *g = [[NSGradient alloc] initWithStartingColor:rgb(40, 140, 220) endingColor:rgb(230, 120, 60)]; [g drawInRect:img angle:-70];
    }
    NSGradient *vig = [[NSGradient alloc] initWithStartingColor:[NSColor colorWithWhite:0 alpha:0] endingColor:[NSColor colorWithWhite:0 alpha:0.75]];
    [vig drawInRect:NSMakeRect(img.origin.x, NSMaxY(img) - 70, img.size.width, 70) angle:90];
    [NSGraphicsContext saveGraphicsState]; NSRectClip(img);   /* a NOW OPEN sash across the top-left corner */
    NSAffineTransform *t = [NSAffineTransform transform]; [t translateXBy:img.origin.x + 40 yBy:img.origin.y + 40]; [t rotateByDegrees:-45]; [t concat];
    NSRect sash = NSMakeRect(-90, -14, 180, 28);
    [NSGraphicsContext saveGraphicsState]; [shadow_of(0.6, 2, 5) set]; [rgb(196, 28, 36) setFill]; rfill(sash); [NSGraphicsContext restoreGraphicsState];
    [rgba(255, 220, 120, 0.9) setFill]; rfill(NSMakeRect(-90, -12, 180, 1)); rfill(NSMakeRect(-90, 11, 180, 1));
    label(S(tp_app_spec()->sash), caps(11), cream(), NSMakeRect(-90, -10, 180, 20), NSTextAlignmentCenter, 3.5, NO);
    [NSGraphicsContext restoreGraphicsState];
    rounded(plaque, 0, night(0.95), nil, 0);
    [rgba(250, 204, 82, 0.8) setFill]; rfill(NSMakeRect(plaque.origin.x, plaque.origin.y, plaque.size.width, 1.5));
    label(S(tp_app_spec()->plaque), caps(13), gold(), NSMakeRect(plaque.origin.x, plaque.origin.y + 7, plaque.size.width, 22), NSTextAlignmentCenter, 4.5, YES);
    label(S(tp_app_spec()->plaque_sub), ital(12.5), cream(), NSMakeRect(plaque.origin.x + 6, plaque.origin.y + 30, plaque.size.width - 12, 18), NSTextAlignmentCenter, 0.2, YES);
}
- (void)drawCard:(NSInteger)g {
    const unsigned char *col = tp_app_spec()->tickets[g].rgb;
    NSRect r = [self cardRect:g];
    int st = self.focus == g ? 2 : self.hover == 300 + g ? 1 : 0;
    if (st) r = NSOffsetRect(r, 0, -4);
    NSColor *base = rgb(col[0], col[1], col[2]);
    CGFloat ny = r.origin.y + 92;
    ticket_clip(r, YES, ny, 7, ^{
        NSShadow *sh = st ? ({ NSShadow *s = [NSShadow new]; s.shadowColor = rgba(255, 214, 110, 0.85); s.shadowBlurRadius = 16; s.shadowOffset = NSZeroSize; s; }) : shadow_of(0.7, 4, 6);
        [NSGraphicsContext saveGraphicsState]; [sh set]; rounded(r, 9, base, nil, 0); [NSGraphicsContext restoreGraphicsState];
        NSGradient *body = [[NSGradient alloc] initWithStartingColor:[base blendedColorWithFraction:st ? 0.38 : 0.25 ofColor:NSColor.whiteColor]
                                                          endingColor:[base blendedColorWithFraction:0.3 ofColor:NSColor.blackColor]];
        [body drawInBezierPath:[NSBezierPath bezierPathWithRoundedRect:r xRadius:9 yRadius:9] angle:-90];
        NSGradient *stub = [[NSGradient alloc] initWithStartingColor:[base blendedColorWithFraction:0.55 ofColor:NSColor.blackColor] endingColor:[base blendedColorWithFraction:0.7 ofColor:NSColor.blackColor]];
        [NSGraphicsContext saveGraphicsState]; [[NSBezierPath bezierPathWithRoundedRect:r xRadius:9 yRadius:9] addClip];
        [stub drawInRect:NSMakeRect(r.origin.x, ny, r.size.width, NSMaxY(r) - ny) angle:-90];
        [[NSColor colorWithWhite:1 alpha:0.06] setFill];                       /* the paper's printed stripe */
        for (CGFloat x = r.origin.x - 60; x < NSMaxX(r); x += 14) { NSBezierPath *p = [NSBezierPath bezierPath];
            [p moveToPoint:NSMakePoint(x, ny)]; [p lineToPoint:NSMakePoint(x + 6, ny)]; [p lineToPoint:NSMakePoint(x + 6 + 60, r.origin.y)]; [p lineToPoint:NSMakePoint(x + 60, r.origin.y)]; [p closePath]; [p fill]; }
        [NSGraphicsContext restoreGraphicsState];
        rounded(NSMakeRect(r.origin.x + 5.5, r.origin.y + 5.5, r.size.width - 11, ny - r.origin.y - 11), 5, nil, rgba(255, 247, 226, 0.55), 1);
    });
    perforation(NSMakePoint(r.origin.x + 8, ny), NSMakePoint(NSMaxX(r) - 8, ny), rgba(255, 247, 226, 0.7));
    [NSGraphicsContext saveGraphicsState]; [shadow_of(0.55, 1.5, 2) set];
    icon(g, NSMakeRect(NSMidX(r) - 22, r.origin.y + 12, 44, 44), cream());
    [NSGraphicsContext restoreGraphicsState];
    label([S(tp_app_spec()->tickets[g].title) uppercaseString], poster(g == 3 ? 17 : 19), cream(), NSMakeRect(r.origin.x + 4, r.origin.y + 62, r.size.width - 8, 24), NSTextAlignmentCenter, 1.2, YES);
    wrapped([[self summary:g] stringByReplacingOccurrencesOfString:@" × " withString:@"\u00a0×\u00a0"], print_f(9.5), rgba(255, 247, 226, 0.92), NSMakeRect(r.origin.x + 7, ny + 6, r.size.width - 14, 32), NSTextAlignmentCenter);
    if (st == 2) rounded(NSInsetRect(r, -3, -3), 11, nil, rgba(255, 247, 226, 0.9), 2);
}
- (void)drawTile:(NSInteger)k {
    NSRect r = [self tileRect:k]; Setting *s = _tile[k];
    int st = self.focus == IT_TILE + k ? 2 : self.hover == 500 + k ? 1 : 0;
    [self plate:r radius:10 lit:st];
    label(s->title.uppercaseString, caps(10.5), gold(), NSMakeRect(r.origin.x + 18, r.origin.y + 12, r.size.width - 80, 16), NSTextAlignmentLeft, 2.8, YES);
    NSInteger n = (NSInteger)s->values.count, i = [self valueIndex:s];
    for (NSInteger j = 0; j < n; j++) {                     /* where the value is among its choices */
        NSRect d = NSMakeRect(NSMaxX(r) - 18 - (n - j) * 9, r.origin.y + 16, 5.5, 5.5);
        [(j == i ? gold() : rgba(250, 204, 82, 0.28)) setFill]; [[NSBezierPath bezierPathWithOvalInRect:d] fill];
    }
    label([self valueOf:s], poster(25), cream(), NSMakeRect(r.origin.x + 18, r.origin.y + 28, r.size.width - 36, 30), NSTextAlignmentLeft, 0.8, YES);
    label([self noteOf:s], plain(11.5), muted(), NSMakeRect(r.origin.x + 18, r.origin.y + 58, r.size.width - 28, 16), NSTextAlignmentLeft, 0.1, YES);
    if (st) for (int side = 0; side < 2; side++) {          /* chevrons: the left end steps back */
        NSBezierPath *c = [NSBezierPath bezierPath]; CGFloat x = side ? NSMaxX(r) - 12 : r.origin.x + 9, y = NSMidY(r) + 4, d = side ? 1 : -1;
        [c moveToPoint:NSMakePoint(x - 3 * d, y - 6)]; [c lineToPoint:NSMakePoint(x + 3 * d, y)]; [c lineToPoint:NSMakePoint(x - 3 * d, y + 6)];
        c.lineWidth = 2; c.lineCapStyle = NSLineCapStyleRound; [gold() setStroke]; [c stroke];
    }
}
- (void)drawButton:(NSRect)r title:(NSString *)t sub:(NSString *)sub dot:(NSColor *)dot state:(int)st {
    [self plate:r radius:9 lit:st];
    if (sub) {
        label(t, caps(13), st ? gold() : cream(), NSMakeRect(r.origin.x, r.origin.y + 10, r.size.width, 20), NSTextAlignmentCenter, 3.6, YES);
        NSDictionary *a = @{NSFontAttributeName: plain(11.5), NSKernAttributeName: @0.3};
        CGFloat w = [sub sizeWithAttributes:a].width + (dot ? 12 : 0), x = NSMidX(r) - w / 2;
        if (dot) { [dot setFill]; [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(x, r.origin.y + 36, 6, 6)] fill]; x += 12; }
        label(sub, plain(11.5), muted(), NSMakeRect(x, r.origin.y + 31, w + 4, 16), NSTextAlignmentLeft, 0.3, YES);
    } else label(t, caps(14), st ? gold() : cream(), NSInsetRect(r, 6, 0), NSTextAlignmentCenter, 4, YES);
}
- (void)drawPlay {
    NSRect r = self.playRect; int st = self.focus == IT_PLAY ? 2 : self.hover == HIT_PLAY ? 1 : 0;
    CGFloat nx = r.origin.x + 56;
    ticket_clip(r, NO, nx, 8, ^{
        NSShadow *sh = st ? ({ NSShadow *s = [NSShadow new]; s.shadowColor = rgba(255, 120, 90, 0.9); s.shadowBlurRadius = 18; s.shadowOffset = NSZeroSize; s; }) : shadow_of(0.8, 5, 14);
        [NSGraphicsContext saveGraphicsState]; [sh set]; rounded(r, 8, rgb(150, 16, 22), nil, 0); [NSGraphicsContext restoreGraphicsState];
        NSGradient *g = [[NSGradient alloc] initWithColorsAndLocations:st ? rgb(255, 92, 72) : rgb(236, 58, 52), 0.0, st ? rgb(210, 34, 38) : rgb(186, 24, 32), 0.55, rgb(120, 10, 20), 1.0, nil];
        [g drawInBezierPath:[NSBezierPath bezierPathWithRoundedRect:r xRadius:8 yRadius:8] angle:-90];
        [NSGraphicsContext saveGraphicsState]; NSRectClip(NSMakeRect(r.origin.x, r.origin.y, nx - r.origin.x, r.size.height));
        [rgba(60, 0, 8, 0.35) setFill]; rfill(r); [NSGraphicsContext restoreGraphicsState];
        rounded(NSInsetRect(r, 5, 5), 5, nil, rgba(255, 214, 110, 0.9), 1.5);
        NSGradient *sheen = [[NSGradient alloc] initWithStartingColor:[NSColor colorWithWhite:1 alpha:0.22] endingColor:[NSColor colorWithWhite:1 alpha:0]];
        [sheen drawInRect:NSMakeRect(r.origin.x, r.origin.y, r.size.width, r.size.height * 0.45) angle:90];
    });
    perforation(NSMakePoint(nx, r.origin.y + 9), NSMakePoint(nx, NSMaxY(r) - 9), rgba(255, 230, 170, 0.85));
    label(@"ADMIT", print_f(10), rgb(255, 226, 150), NSMakeRect(r.origin.x + 4, NSMidY(r) - 15, nx - r.origin.x - 8, 14), NSTextAlignmentCenter, 0.6, NO);   /* ADMIT ONE on the stub */
    label(@"ONE", print_f(10), rgb(255, 226, 150), NSMakeRect(r.origin.x + 4, NSMidY(r) + 1, nx - r.origin.x - 8, 14), NSTextAlignmentCenter, 1.4, NO);
    NSRect body = NSMakeRect(nx, r.origin.y, NSMaxX(r) - nx, r.size.height);
    NSDictionary *pa = @{NSFontAttributeName: poster(36), NSForegroundColorAttributeName: rgb(255, 222, 110), NSKernAttributeName: @6, NSShadowAttributeName: shadow_of(0.8, 2, 4)};
    NSSize ps = [@"PLAY" sizeWithAttributes:pa];
    CGFloat tri = 16, x = NSMidX(body) - (ps.width + tri + 10) / 2;
    NSBezierPath *tp = [NSBezierPath bezierPath]; CGFloat cy = NSMidY(body);
    [tp moveToPoint:NSMakePoint(x, cy - 11)]; [tp lineToPoint:NSMakePoint(x + tri, cy)]; [tp lineToPoint:NSMakePoint(x, cy + 11)]; [tp closePath];
    [NSGraphicsContext saveGraphicsState]; [shadow_of(0.8, 2, 4) set]; [rgb(255, 222, 110) setFill]; [tp fill]; [NSGraphicsContext restoreGraphicsState];
    [@"PLAY" drawAtPoint:NSMakePoint(x + tri + 10, cy - (poster(36).ascender - poster(36).descender) / 2 - 1) withAttributes:pa];
    if (st == 2) rounded(NSInsetRect(r, -4, -4), 11, nil, rgba(255, 247, 226, 0.9), 2);
}
- (void)drawRow:(NSInteger)k {
    Setting *s = [self rowSetting:k];
    NSRect r = [self rowRect:k];
    BOOL focused = self.focus == k, hot = self.hover == 100 + k || self.hover == 200 + k || self.hover == 400 + k;
    CGFloat top = self.compact ? 3 : 8;
    if (focused || hot) rounded(NSInsetRect(r, -10, 2), 8, rgba(250, 204, 82, focused ? 0.16 : 0.09), focused ? rgba(250, 204, 82, 0.5) : nil, 1);
    label(s->title.uppercaseString, caps(13.5), gold(), NSMakeRect(r.origin.x + 8, r.origin.y + top, 340, 30), NSTextAlignmentLeft, 2.6, YES);
    NSRect la = [self arrow:k right:NO], ra = [self arrow:k right:YES];
    NSRect va = NSMakeRect(NSMaxX(la) + 4, r.origin.y + top, ra.origin.x - NSMaxX(la) - 8, 30);
    label([self valueOf:s], poster(22), cream(), va, NSTextAlignmentCenter, 1, YES);
    NSString *note = [self noteOf:s];
    if (s->slider) {                                     /* the track, gold to the knob */
        NSRect t = [self trackRect:k]; NSInteger n = (NSInteger)s->values.count, i = [self valueIndex:s];
        CGFloat x = t.origin.x + t.size.width * (n > 1 ? (CGFloat)i / (n - 1) : 0);
        rounded(t, 4.5, rgb(34, 30, 64), rgba(250, 204, 82, 0.6), 1);
        NSGradient *fill = [[NSGradient alloc] initWithStartingColor:rgb(255, 228, 140) endingColor:rgb(214, 150, 40)];
        if (x > t.origin.x) [fill drawInBezierPath:[NSBezierPath bezierPathWithRoundedRect:NSMakeRect(t.origin.x, t.origin.y, x - t.origin.x, t.size.height) xRadius:4.5 yRadius:4.5] angle:-90];
        for (NSInteger j = 0; j < n; j++) { CGFloat tx = t.origin.x + t.size.width * (n > 1 ? (CGFloat)j / (n - 1) : 0); [rgba(20, 16, 40, 0.8) setFill]; rfill(NSMakeRect(tx - 0.5, t.origin.y + 2, 1, t.size.height - 4)); }
        NSRect knob = NSMakeRect(x - 9, NSMidY(t) - 9, 18, 18);
        [NSGraphicsContext saveGraphicsState]; [shadow_of(0.7, 1.5, 3) set];
        NSBezierPath *kp = [NSBezierPath bezierPathWithOvalInRect:knob]; [cream() setFill]; [kp fill]; [NSGraphicsContext restoreGraphicsState];
        [gold_dk() setStroke]; NSBezierPath *ko = [NSBezierPath bezierPathWithOvalInRect:knob]; ko.lineWidth = 2; [ko stroke];
    } else if (note.length) label(note, plain(13), muted(), NSMakeRect(va.origin.x - 70, r.origin.y + (self.compact ? 30 : 40), va.size.width + 140, 18), NSTextAlignmentCenter, 0.3, YES);
    for (int side = is_link(s) ? 1 : 0; side < 2; side++) {   /* gold pennant arrows (a page's row: the right one, it opens) */
        NSRect a = side ? ra : la; BOOL lit = self.hover == (side ? 200 : 100) + k;
        NSBezierPath *t = [NSBezierPath bezierPath];
        CGFloat cx = NSMidX(a), cy = NSMidY(a), d = side ? 1 : -1;
        [t moveToPoint:NSMakePoint(cx + 10 * d, cy)]; [t lineToPoint:NSMakePoint(cx - 8 * d, cy - 10)]; [t lineToPoint:NSMakePoint(cx - 8 * d, cy + 10)]; [t closePath];
        [NSGraphicsContext saveGraphicsState]; [shadow_of(0.8, 2, 3) set];
        [(lit ? rgb(255, 236, 160) : rgb(236, 180, 64)) setFill]; [t fill];
        [NSGraphicsContext restoreGraphicsState];
        [rgb(110, 50, 14) setStroke]; t.lineWidth = 1.1; [t stroke];
    }
}
- (void)drawBoard {
    NSRect p = self.board;
    [NSGraphicsContext saveGraphicsState]; [shadow_of(0.85, 8, 22) set]; rounded(p, 16, night(0.94), nil, 0); [NSGraphicsContext restoreGraphicsState];
    rounded(NSInsetRect(p, 1, 1), 16, nil, gold(), 2);
    rounded(NSInsetRect(p, 8, 8), 10, nil, rgba(250, 204, 82, 0.3), 1);
    NSRect head = NSMakeRect(p.origin.x + 24, p.origin.y + 22, p.size.width - 48, 56);   /* the page's marquee: a red board in gold */
    NSGradient *band = [[NSGradient alloc] initWithColorsAndLocations:rgb(255, 228, 140), 0.0, rgb(232, 172, 54), 0.45, rgb(150, 94, 22), 1.0, nil];
    [band drawInBezierPath:[NSBezierPath bezierPathWithRoundedRect:head xRadius:10 yRadius:10] angle:-90];
    NSRect hi = NSInsetRect(head, 5, 5);
    NSGradient *lac = [[NSGradient alloc] initWithColorsAndLocations:rgb(214, 38, 46), 0.0, rgb(150, 16, 28), 1.0, nil];
    [lac drawInBezierPath:[NSBezierPath bezierPathWithRoundedRect:hi xRadius:6 yRadius:6] angle:-90];
    for (int i = 0; i < 2; i++) star(NSMakePoint(i ? NSMaxX(hi) - 22 : hi.origin.x + 22, NSMidY(hi)), 7, gold());
    NSString *parent = page_parent_of(self.open) >= 0 ? [_groups[page_parent_of(self.open)] uppercaseString] : nil;
    NSString *title = parent ? [NSString stringWithFormat:@"%@  ·  %@", parent, [_groups[self.open] uppercaseString]] : [_groups[self.open] uppercaseString];
    label(title, poster(30), rgb(255, 226, 130), hi, NSTextAlignmentCenter, 5, YES);
    if (self.open == 4) {                                   /* the controls: a table */
        NSArray *rows = tp_controls_rows(); CGFloat y0 = p.origin.y + 100, step = MIN(31, (NSMaxY(p) - 24 - y0) / rows.count);
        for (NSUInteger i = 0; i < rows.count; i++) {
            CGFloat y = y0 + step * i;
            if (i % 2 == 0) rounded(NSMakeRect(p.origin.x + 40, y - 2, p.size.width - 80, step), 5, rgba(250, 204, 82, 0.05), nil, 0);
            label([rows[i][0] uppercaseString], caps(12.5), gold(), NSMakeRect(p.origin.x + 60, y, 320, step - 4), NSTextAlignmentLeft, 2, YES);
            label(rows[i][1], plain(16), cream(), NSMakeRect(p.origin.x + 400, y, p.size.width - 450, step - 4), NSTextAlignmentLeft, 0.3, YES);
        }
        return;
    }
    for (NSInteger k = 0; k < self.rowCount; k++) {
        [self drawRow:k];
        if (k + 1 < self.rowCount) { NSGradient *g = [[NSGradient alloc] initWithColorsAndLocations:rgba(250, 204, 82, 0), 0.0, rgba(250, 204, 82, 0.35), 0.5, rgba(250, 204, 82, 0), 1.0, nil];
            NSRect rr = [self rowRect:k]; [g drawInRect:NSMakeRect(rr.origin.x, NSMaxY(rr) - 0.5, rr.size.width, 1) angle:0]; }
    }
    if (tp_app_spec()->pages[self.open].footnote[0]) {
        const char *const *fn = tp_app_spec()->pages[self.open].footnote; NSArray *lines = fn[1] ? @[S(fn[0]), S(fn[1])] : @[S(fn[0])];
        CGFloat y0 = NSMaxY([self rowRect:self.rowCount - 1]) + 12;
        for (NSUInteger i = 0; i < lines.count; i++)
            label(lines[i], ital(14.5), muted(), NSMakeRect(p.origin.x + 50, y0 + 22 * i, p.size.width - 100, 20), NSTextAlignmentCenter, 0.2, YES);
    }
}
- (void)drawRect:(NSRect)dirty {
    [(TPMidway *)self.midway setPageOpen:self.open >= 0];
    if (self.open < 0) {
        [self drawSign];
        label(S(tp_app_spec()->credits), caps(10.5), gold(), NSMakeRect(44, 204, 584, 18), NSTextAlignmentCenter, 3.4, YES);
        wrapped(S(tp_app_spec()->tagline), ital(16.5), cream(),
                NSMakeRect(44, 228, 584, 50), NSTextAlignmentCenter);
        heading(@"ATTRACTIONS", NSMakePoint(44, 294), 628);
        for (NSInteger g = 0; g < N_CARDS; g++) if (g != self.hover - 300 && g != self.focus) [self drawCard:g];
        for (NSInteger g = 0; g < N_CARDS; g++) if (g == self.hover - 300 || g == self.focus) [self drawCard:g];   /* (a lifted ticket over its neighbours) */
        [self drawPoster];
        heading(@"QUICK SETTINGS", NSMakePoint(44, 468), 956);
        for (NSInteger k = 0; k < N_TILES; k++) [self drawTile:k];
        NSColor *dot = _ustate == UPD_CURRENT ? rgb(120, 220, 120) : _ustate == UPD_AVAILABLE ? rgb(255, 190, 60) : _ustate == UPD_ERROR ? rgb(230, 90, 80) : rgb(150, 150, 170);
        NSString *usub = _ustate == UPD_AVAILABLE ? [NSString stringWithFormat:@"New: %@ · press", short_commit(_latest)] :
                         _ustate == UPD_CURRENT ? [NSString stringWithFormat:@"Up to date · %@", short_commit(_built)] : update_label(_ustate);
        [self drawButton:self.savesRect title:@"SAVED PARKS" sub:@"Show in Finder" dot:nil state:self.focus == IT_SAVES ? 2 : self.hover == HIT_SAVES];
        [self drawButton:self.updatesRect title:@"UPDATES" sub:usub dot:dot state:self.focus == IT_UPDATES ? 2 : self.hover == HIT_UPDATES];
        [self drawButton:self.quitRect title:@"QUIT" sub:nil dot:nil state:self.focus == IT_QUIT ? 2 : self.hover == HIT_QUIT];
    } else {
        [self drawBoard];
        [self drawButton:self.quitRect title:@"BACK" sub:nil dot:nil state:self.hover == HIT_BACK];
    }
    [self drawPlay];
    NSString *hint = S(self.open < 0 ? tp_app_spec()->hint_main : tp_app_spec()->hint_page);
    label(hint, plain(12), rgba(210, 206, 230, 0.75), NSMakeRect(44, 670, 640, 18), NSTextAlignmentLeft, 0.3, YES);
    NSString *build = NSBundle.mainBundle.infoDictionary[@"CFBundleVersion"] ?: @"dev";
    label([NSString stringWithFormat:[_branch isEqualToString:@"release"] ? @"VERSION %@" : @"NIGHTLY %@", build.uppercaseString], caps(9.5), rgba(250, 204, 82, 0.6),
          NSMakeRect(WIN_W - 300, 673, 256, 14), NSTextAlignmentRight, 3, NO);
}
@end

/* Returns YES to play. */
BOOL tp_launcher_run(void) {
    NSRect frame = NSMakeRect(0, 0, WIN_W, WIN_H);
    TPLaunchView *v = [[TPLaunchView alloc] initWithFrame:frame];
    TPMidway *mid = [[TPMidway alloc] initWithSize:frame.size park:v.park soft:v.parkSoft sign:v.signRect logo:v.logoRect poster:v.posterRect play:v.playRect];
    v.midway = mid;
    const char *te = getenv("TP_LAUNCHER_TIME"); double t0 = te ? atof(te) : 3.0;
    [mid setTime:t0];
    NSString *shot = NSProcessInfo.processInfo.environment[@"TP_LAUNCHER_SHOT"];
    if (shot) {                                             /* bounded preview render: the layers behind, the settings, the layers over */
        int W = WIN_W * 2, H = WIN_H * 2;
        CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        CGContextRef cx = CGBitmapContextCreate(NULL, W, H, 8, 0, cs, (CGBitmapInfo)kCGImageAlphaPremultipliedLast); CGColorSpaceRelease(cs);
        CGContextScaleCTM(cx, 2, 2);
        [v displayIfNeeded];
        [mid setPageOpen:v.open >= 0];
        [mid.back renderInContext:cx];
        NSBitmapImageRep *rep = [v bitmapImageRepForCachingDisplayInRect:v.bounds];
        [v cacheDisplayInRect:v.bounds toBitmapImageRep:rep];
        CGContextDrawImage(cx, CGRectMake(0, 0, WIN_W, WIN_H), rep.CGImage);
        [mid.front renderInContext:cx];
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
    [root.layer addSublayer:mid.back];
    v.wantsLayer = YES; v.layer.backgroundColor = NSColor.clearColor.CGColor; [root addSubview:v];   /* (drawn only when a setting changes) */
    v.layer.zPosition = 1;
    mid.front.zPosition = 2; [root.layer addSublayer:mid.front];
    w.contentView = root;
    [w center];
    [w makeKeyAndOrderFront:nil];
    [w makeFirstResponder:v];
    [NSApp activateIgnoringOtherApps:YES];
    BOOL still = NSWorkspace.sharedWorkspace.accessibilityDisplayShouldReduceMotion;   /* (Reduce Motion: it holds still) */
    double start = CACurrentMediaTime() - t0;
    static double prof_n, prof_sum, prof_max, prof_t0; const char *prof = getenv("TP_LAUNCHER_PROFILE");   /* (development: ticks' cost, then quits) */
    NSTimer *tick = still ? nil : [NSTimer timerWithTimeInterval:1.0 / 60 repeats:YES block:^(NSTimer *tm) { (void)tm;
        double a = CACurrentMediaTime(); [mid setTime:a - start]; [CATransaction flush]; double d = CACurrentMediaTime() - a;
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
