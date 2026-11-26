// make_readme_art: the README's art in the launch window's look (platform/macos/launcher.m, "Midway Marquee"): ours only,
// drawn here, no publisher art. The screenshots it frames are the player's own park in the game, taken by tests/e2e.
//   make_readme_art banner <out.png>                                   the page's logo: OPENPARK on a lit marquee sign
//   make_readme_art card <shot> <title> <caption> <colour> <out.jpg>   a screenshot in a gilt frame of bulbs, a ribbon
//                       [--over <png>]                                 (another picture laid over the shot: the controls card)
//   colour: red, blue, gold, green, purple (the launcher's tickets)
// clang -fobjc-arc -framework AppKit make_readme_art.m -o make_readme_art
#import <Cocoa/Cocoa.h>
#include <math.h>

static NSColor *rgba(int r, int g, int b, double a) { return [NSColor colorWithSRGBRed:r / 255.0 green:g / 255.0 blue:b / 255.0 alpha:a]; }
static NSColor *rgb(int r, int g, int b) { return rgba(r, g, b, 1); }
static NSFont *poster(CGFloat n) { return [NSFont fontWithName:@"Futura-CondensedExtraBold" size:n] ?: [NSFont systemFontOfSize:n weight:NSFontWeightBlack]; }
static NSFont *caps(CGFloat n) { return [NSFont fontWithName:@"Futura-Bold" size:n] ?: [NSFont systemFontOfSize:n weight:NSFontWeightBold]; }
static NSFont *ital(CGFloat n) { return [NSFont fontWithName:@"Optima-Italic" size:n] ?: [NSFont systemFontOfSize:n]; }
static double frand(unsigned k) { k ^= k >> 13; k *= 0x5bd1e995u; k ^= k >> 15; return (k & 0xffffff) / (double)0x1000000; }
static NSShadow *shadow_of(double a, CGFloat dy, CGFloat blur) { NSShadow *s = [NSShadow new]; s.shadowColor = [NSColor colorWithWhite:0 alpha:a]; s.shadowOffset = NSMakeSize(0, -dy); s.shadowBlurRadius = blur; return s; }

/* A canvas of w x h points at 2 pixels a point, flipped (top-down), drawn by `draw`; saved as PNG or JPEG. */
static int render(CGFloat w, CGFloat h, NSString *out, void (^draw)(NSRect r)) {
    NSBitmapImageRep *rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL pixelsWide:(NSInteger)(w * 2) pixelsHigh:(NSInteger)(h * 2)
        bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO colorSpaceName:NSCalibratedRGBColorSpace bytesPerRow:0 bitsPerPixel:0];
        NSGraphicsContext *g0 = [NSGraphicsContext graphicsContextWithBitmapImageRep:rep];
    NSGraphicsContext *g = [NSGraphicsContext graphicsContextWithCGContext:g0.CGContext flipped:YES];   /* (images and text honour it) */
    [NSGraphicsContext saveGraphicsState]; [NSGraphicsContext setCurrentContext:g];
    CGContextScaleCTM(g.CGContext, 2, 2);
    NSAffineTransform *flip = [NSAffineTransform transform]; [flip translateXBy:0 yBy:h]; [flip scaleXBy:1 yBy:-1]; [flip concat];
    draw(NSMakeRect(0, 0, w, h));
    [NSGraphicsContext restoreGraphicsState];
    BOOL jpg = [out.pathExtension.lowercaseString hasPrefix:@"jp"];
    NSData *d = jpg ? [rep representationUsingType:NSBitmapImageFileTypeJPEG properties:@{NSImageCompressionFactor: @0.88}]
                    : [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
    return [d writeToFile:out atomically:YES] ? 0 : 1;
}
static void text_at(NSString *s, NSDictionary *a, NSRect r) { [s drawInRect:r withAttributes:a]; }
static NSDictionary *attrs(NSFont *f, NSColor *c, CGFloat kern, NSTextAlignment al, NSShadow *sh) {
    NSMutableParagraphStyle *p = [NSMutableParagraphStyle new]; p.alignment = al; p.lineBreakMode = NSLineBreakByTruncatingTail;
    NSMutableDictionary *a = [@{NSFontAttributeName: f, NSForegroundColorAttributeName: c, NSParagraphStyleAttributeName: p, NSKernAttributeName: @(kern)} mutableCopy];
    if (sh) a[NSShadowAttributeName] = sh;
    return a;
}
static void text_mid(NSString *s, NSDictionary *a, NSRect r) {   /* one line, vertically centred in r */
    NSFont *f = a[NSFontAttributeName]; CGFloat h = f.ascender - f.descender;
    text_at(s, a, NSMakeRect(r.origin.x, NSMidY(r) - h / 2, r.size.width, h + 2));
}

/* ---- the midway's pieces ---- */
static void dusk(NSRect r) {
    NSGradient *g = [[NSGradient alloc] initWithColorsAndLocations:rgb(10, 10, 32), 0.0, rgb(40, 16, 58), 0.45, rgb(86, 26, 62), 0.72, rgb(14, 10, 30), 1.0, nil];
    [g drawInRect:r angle:90];
}
static void glow(NSPoint p, CGFloat rad, NSColor *c, double a) {
    NSGradient *g = [[NSGradient alloc] initWithColorsAndLocations:[NSColor colorWithWhite:1 alpha:a], 0.0, [c colorWithAlphaComponent:0.9 * a], 0.22, [c colorWithAlphaComponent:0], 1.0, nil];
    [g drawFromCenter:p radius:0 toCenter:p radius:rad options:0];
}
static void searchlight(NSPoint foot, double angle, CGFloat len, CGFloat spread) {
    [NSGraphicsContext saveGraphicsState];
    NSBezierPath *b = [NSBezierPath bezierPath];
    NSPoint a = NSMakePoint(foot.x + len * sin(angle - spread), foot.y - len * cos(angle - spread));
    NSPoint c = NSMakePoint(foot.x + len * sin(angle + spread), foot.y - len * cos(angle + spread));
    [b moveToPoint:foot]; [b lineToPoint:a]; [b lineToPoint:c]; [b closePath]; [b addClip];
    NSGradient *g = [[NSGradient alloc] initWithStartingColor:rgba(255, 240, 200, 0.16) endingColor:rgba(180, 190, 255, 0)];
    [g drawFromCenter:foot radius:0 toCenter:foot radius:len options:0];
    [NSGraphicsContext restoreGraphicsState];
}
static void firework(NSPoint c, CGFloat r, unsigned seed) {
    NSArray *cols = @[rgb(255, 120, 90), rgb(255, 214, 90), rgb(120, 200, 255), rgb(190, 130, 255), rgb(120, 240, 150)];
    NSColor *col = cols[seed % cols.count];
    for (int i = 0; i < 30; i++) {
        double a = i * 2 * M_PI / 30 + frand(seed * 7 + i) * 0.2, d = r * (0.75 + 0.25 * frand(seed * 13 + i));
        NSPoint p = NSMakePoint(c.x + d * cos(a), c.y + d * sin(a) + d * d * 0.0012);
        glow(p, 7, col, 0.85);
        NSBezierPath *trail = [NSBezierPath bezierPath]; [trail moveToPoint:NSMakePoint(c.x + d * 0.55 * cos(a), c.y + d * 0.55 * sin(a))]; [trail lineToPoint:p];
        trail.lineWidth = 1.2; [[col colorWithAlphaComponent:0.35] setStroke]; [trail stroke];
    }
    glow(c, r * 0.35, col, 0.25);
}
static void bunting(CGFloat x0, CGFloat x1, CGFloat top, CGFloat sag, int phase) {
    NSArray *flagc = @[rgb(214, 40, 40), rgb(250, 204, 44), rgb(40, 120, 220), rgb(60, 170, 80), rgb(245, 130, 30), rgb(150, 80, 210)];
    NSBezierPath *str = [NSBezierPath bezierPath]; [str moveToPoint:NSMakePoint(x0, top)];
    [str curveToPoint:NSMakePoint(x1, top) controlPoint1:NSMakePoint(x0 + (x1 - x0) / 3, top + 2 * sag) controlPoint2:NSMakePoint(x0 + 2 * (x1 - x0) / 3, top + 2 * sag)];
    str.lineWidth = 1.4; [rgba(242, 236, 220, 0.75) setStroke]; [str stroke];
    int n = (int)((x1 - x0) / 30);
    for (int i = 1; i < n; i++) {
        double u = (double)i / n; CGFloat x = x0 + (x1 - x0) * u, y = top + 3 * sag * u * (1 - u) * 1.0;
        NSBezierPath *f = [NSBezierPath bezierPath]; double tilt = 0.08 * sin(i * 1.7 + phase);
        NSPoint a = NSMakePoint(x - 9, y), b = NSMakePoint(x + 9, y), c = NSMakePoint(x + 22 * sin(tilt), y + 22 * cos(tilt));
        [f moveToPoint:a]; [f lineToPoint:b]; [f lineToPoint:c]; [f closePath];
        [NSGraphicsContext saveGraphicsState]; [shadow_of(0.4, 2, 2) set]; [(NSColor *)flagc[(i + phase) % flagc.count] setFill]; [f fill]; [NSGraphicsContext restoreGraphicsState];
    }
}
static void bulb(NSPoint p, BOOL lit, CGFloat k) {
    if (lit) glow(p, 11 * k, rgb(255, 236, 150), 0.8);
    [rgb(96, 56, 16) setFill]; [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(p.x - 4 * k, p.y - 4 * k, 8 * k, 8 * k)] fill];
    NSGradient *g = [[NSGradient alloc] initWithStartingColor:lit ? NSColor.whiteColor : rgb(170, 130, 70) endingColor:lit ? rgb(255, 236, 150) : rgb(110, 76, 34)];
    [g drawInBezierPath:[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(p.x - 3 * k, p.y - 3 * k, 6 * k, 6 * k)] relativeCenterPosition:NSMakePoint(-0.3, 0.3)];
}
static void bulb_ring(NSRect r, CGFloat step, CGFloat k) {   /* round a rectangle, every third lit (the chase, held still) */
    CGFloat per = 2 * (r.size.width + r.size.height); int n = (int)lround(per / step);
    for (int i = 0; i < n; i++) {
        CGFloat d = per * i / n, x, y;
        if (d < r.size.width) { x = r.origin.x + d; y = r.origin.y; }
        else if ((d -= r.size.width) < r.size.height) { x = NSMaxX(r); y = r.origin.y + d; }
        else if ((d -= r.size.height) < r.size.width) { x = NSMaxX(r) - d; y = NSMaxY(r); }
        else { d -= r.size.width; x = r.origin.x; y = NSMaxY(r) - d; }
        bulb(NSMakePoint(x, y), i % 3 == 0, k);
    }
}
static void star(NSPoint c, CGFloat r, NSColor *col) {
    NSBezierPath *s = [NSBezierPath bezierPath];
    for (int i = 0; i < 10; i++) { double a = -M_PI / 2 + i * M_PI / 5; CGFloat rr = i % 2 ? r * 0.45 : r;
        NSPoint p = NSMakePoint(c.x + rr * cos(a), c.y + rr * sin(a)); if (i) [s lineToPoint:p]; else [s moveToPoint:p]; }
    [s closePath]; [col setFill]; [s fill];
}
static void gold_band(NSBezierPath *p) {
    NSGradient *band = [[NSGradient alloc] initWithColorsAndLocations:rgb(255, 228, 140), 0.0, rgb(232, 172, 54), 0.45, rgb(150, 94, 22), 1.0, nil];
    [band drawInBezierPath:p angle:90];
}
static void lacquer(NSRect in, CGFloat rad) {
    NSBezierPath *ip = [NSBezierPath bezierPathWithRoundedRect:in xRadius:rad yRadius:rad];
    NSGradient *lac = [[NSGradient alloc] initWithColorsAndLocations:rgb(214, 38, 46), 0.0, rgb(168, 20, 32), 0.55, rgb(104, 10, 22), 1.0, nil];
    [lac drawInBezierPath:ip angle:90];
    [NSGraphicsContext saveGraphicsState]; [ip addClip];
    [[NSColor colorWithWhite:1 alpha:0.055] setFill];
    for (CGFloat x = in.origin.x - in.size.height; x < NSMaxX(in); x += 30) {
        NSBezierPath *st = [NSBezierPath bezierPath]; [st moveToPoint:NSMakePoint(x, NSMaxY(in))]; [st lineToPoint:NSMakePoint(x + 14, NSMaxY(in))];
        [st lineToPoint:NSMakePoint(x + 14 + in.size.height, in.origin.y)]; [st lineToPoint:NSMakePoint(x + in.size.height, in.origin.y)]; [st closePath]; [st fill]; }
    NSGradient *sheen = [[NSGradient alloc] initWithStartingColor:[NSColor colorWithWhite:1 alpha:0.20] endingColor:[NSColor colorWithWhite:1 alpha:0]];
    [sheen drawInRect:NSMakeRect(in.origin.x, in.origin.y, in.size.width, in.size.height * 0.3) angle:90];
    [NSGraphicsContext restoreGraphicsState];
}

/* ---- the page's logo ---- */
static int banner(NSString *out) {
    CGFloat W = 1600, H = 500;
    return render(W, H, out, ^(NSRect r) {
        dusk(r);
        searchlight(NSMakePoint(W * 0.12, H + 20), 0.38, 760, 0.07); searchlight(NSMakePoint(W * 0.88, H + 20), -0.42, 760, 0.07);
        searchlight(NSMakePoint(W * 0.30, H + 20), -0.18, 700, 0.05);
        for (int i = 0; i < 70; i++) glow(NSMakePoint(W * frand(i * 3 + 1), H * frand(i * 5 + 2) * 0.9), 1.5 + 2.5 * frand(i * 7), rgb(255, 230, 170), 0.35 + 0.4 * frand(i * 11));
        firework(NSMakePoint(170, 150), 95, 2); firework(NSMakePoint(1440, 120), 105, 4); firework(NSMakePoint(1330, 360), 70, 1); firework(NSMakePoint(260, 390), 60, 3);
        bunting(-10, W * 0.52, 4, 22, 0); bunting(W * 0.48, W + 10, 10, 20, 3);
        NSRect s = NSMakeRect(310, 78, 980, 330);              /* the marquee sign */
        NSBezierPath *outer = [NSBezierPath bezierPathWithRoundedRect:s xRadius:26 yRadius:26];
        [NSGraphicsContext saveGraphicsState]; [shadow_of(0.75, 10, 30) set]; [rgb(46, 8, 16) setFill]; [outer fill]; [NSGraphicsContext restoreGraphicsState];
        gold_band([NSBezierPath bezierPathWithRoundedRect:NSInsetRect(s, 3, 3) xRadius:24 yRadius:24]);
        NSRect in = NSInsetRect(s, 24, 24); lacquer(in, 12);
        NSBezierPath *ib = [NSBezierPath bezierPathWithRoundedRect:in xRadius:12 yRadius:12]; ib.lineWidth = 3; [rgb(70, 8, 16) setStroke]; [ib stroke];
        NSBezierPath *ig = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(in, 7, 7) xRadius:8 yRadius:8]; ig.lineWidth = 1.5; [rgba(255, 220, 120, 0.5) setStroke]; [ig stroke];
        bulb_ring(NSInsetRect(s, 12, 12), 24, 1.25);
        for (int i = 0; i < 2; i++) star(NSMakePoint(i ? NSMaxX(in) - 40 : in.origin.x + 40, in.origin.y + 40), 13, rgb(250, 204, 82));
        text_mid(@"BULLFROG'S THEME PARK, NATIVELY", attrs(caps(19), rgb(255, 226, 150), 7, NSTextAlignmentCenter, shadow_of(0.8, 2, 4)), NSMakeRect(in.origin.x, in.origin.y + 18, in.size.width, 34));
        NSShadow *deep = [NSShadow new]; deep.shadowColor = rgba(60, 0, 10, 0.95); deep.shadowOffset = NSMakeSize(0, -7); deep.shadowBlurRadius = 2;
        NSDictionary *big = attrs(poster(150), rgb(255, 246, 224), 10, NSTextAlignmentCenter, deep);
        NSMutableDictionary *edge = [big mutableCopy]; edge[NSStrokeColorAttributeName] = rgb(176, 118, 36); edge[NSStrokeWidthAttributeName] = @(-3.0);
        text_mid(@"OPENPARK", edge, NSMakeRect(in.origin.x, in.origin.y + 54, in.size.width, 170));
        /* the bottom rule: a gold line, a diamond, and the systems */
        CGFloat ry = NSMaxY(in) - 44;
        NSGradient *rule = [[NSGradient alloc] initWithColorsAndLocations:rgba(250, 204, 82, 0), 0.0, rgba(250, 204, 82, 0.8), 0.5, rgba(250, 204, 82, 0), 1.0, nil];
        [rule drawInRect:NSMakeRect(in.origin.x + 60, ry - 18, in.size.width - 120, 1.5) angle:0];
        text_mid(@"macOS  ·  WINDOWS  ·  LINUX", attrs(caps(17), rgb(255, 236, 190), 6, NSTextAlignmentCenter, shadow_of(0.8, 2, 4)), NSMakeRect(in.origin.x, ry - 8, in.size.width, 30));
    });
}

/* ---- a screenshot framed ---- */
static int card(NSString *shot, NSString *title, NSString *caption, NSString *colour, NSString *over, NSString *out) {
    NSImage *img = [[NSImage alloc] initWithContentsOfFile:shot]; if (!img) { fprintf(stderr, "no image %s\n", shot.UTF8String); return 1; }
    NSImage *top = over ? [[NSImage alloc] initWithContentsOfFile:over] : nil;
    NSBitmapImageRep *rep = (NSBitmapImageRep *)img.representations.firstObject;
    CGFloat iw = rep.pixelsWide, ih = rep.pixelsHigh, W = 1200, frameW = W - 80, picW = frameW - 44, picH = round(picW * ih / iw), H = picH + 44 + 80 + 64;
    NSDictionary *tint = @{@"red": rgb(206, 44, 48), @"blue": rgb(38, 112, 212), @"gold": rgb(232, 164, 26), @"green": rgb(44, 150, 84), @"purple": rgb(128, 74, 196)};
    NSColor *base = tint[colour] ?: tint[@"red"];
    return render(W, H, out, ^(NSRect r) {
        dusk(r);
        for (int i = 0; i < 40; i++) glow(NSMakePoint(W * frand(i * 3 + 9 + title.length), H * frand(i * 5 + 4)), 1.5 + 2 * frand(i * 7), rgb(255, 230, 170), 0.3 + 0.3 * frand(i * 11));
        NSRect f = NSMakeRect(40, 36, frameW, picH + 44);
        [NSGraphicsContext saveGraphicsState]; [shadow_of(0.8, 10, 26) set]; [rgb(36, 18, 10) setFill]; NSRectFill(f); [NSGraphicsContext restoreGraphicsState];
        NSGradient *gilt = [[NSGradient alloc] initWithColorsAndLocations:rgb(255, 232, 150), 0.0, rgb(214, 156, 46), 0.35, rgb(255, 214, 110), 0.6, rgb(140, 86, 20), 1.0, nil];
        [gilt drawInRect:NSInsetRect(f, 1, 1) angle:-60];
        [rgb(80, 46, 12) setStroke]; NSFrameRectWithWidth(NSInsetRect(f, 19.5, 19.5), 1.5);
        NSRect pic = NSInsetRect(f, 22, 22);
        [img drawInRect:pic fromRect:NSZeroRect operation:NSCompositingOperationCopy fraction:1 respectFlipped:YES hints:@{NSImageHintInterpolation: @(NSImageInterpolationHigh)}];
        if (top) [top drawInRect:pic fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1 respectFlipped:YES hints:@{NSImageHintInterpolation: @(NSImageInterpolationHigh)}];
        bulb_ring(NSInsetRect(f, 10, 10), 26, 1.0);
        /* the ribbon: the ticket's colour, notched ends, over the frame's foot */
        NSDictionary *ta = @{NSFontAttributeName: poster(44), NSKernAttributeName: @4}; CGFloat tw = [title.uppercaseString sizeWithAttributes:ta].width;
        CGFloat rw = MIN(frameW - 120, tw + 200), rx = W / 2 - rw / 2, ry = NSMaxY(f) - 26, rh = 64;
        NSBezierPath *rib = [NSBezierPath bezierPath];
        [rib moveToPoint:NSMakePoint(rx - 30, ry + 8)]; [rib lineToPoint:NSMakePoint(rx + rw + 30, ry + 8)]; [rib lineToPoint:NSMakePoint(rx + rw + 10, ry + rh / 2 + 8)];
        [rib lineToPoint:NSMakePoint(rx + rw + 30, ry + rh + 8)]; [rib lineToPoint:NSMakePoint(rx - 30, ry + rh + 8)]; [rib lineToPoint:NSMakePoint(rx - 10, ry + rh / 2 + 8)]; [rib closePath];
        [NSGraphicsContext saveGraphicsState]; [shadow_of(0.6, 3, 6) set]; [[base blendedColorWithFraction:0.45 ofColor:NSColor.blackColor] setFill]; [rib fill]; [NSGraphicsContext restoreGraphicsState];
        NSBezierPath *face = [NSBezierPath bezierPathWithRoundedRect:NSMakeRect(rx, ry, rw, rh) xRadius:6 yRadius:6];
        NSGradient *fg = [[NSGradient alloc] initWithStartingColor:[base blendedColorWithFraction:0.25 ofColor:NSColor.whiteColor] endingColor:[base blendedColorWithFraction:0.3 ofColor:NSColor.blackColor]];
        [NSGraphicsContext saveGraphicsState]; [shadow_of(0.7, 4, 10) set]; [fg drawInBezierPath:face angle:90]; [base setFill]; [NSGraphicsContext restoreGraphicsState];
        [fg drawInBezierPath:face angle:90];
        NSBezierPath *fl = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(NSMakeRect(rx, ry, rw, rh), 5, 5) xRadius:4 yRadius:4]; fl.lineWidth = 1.5; [rgba(255, 247, 226, 0.6) setStroke]; [fl stroke];
        for (int i = 0; i < 2; i++) star(NSMakePoint(i ? rx + rw - 30 : rx + 30, ry + rh / 2), 9, rgb(255, 236, 160));
        text_mid(title.uppercaseString, attrs(poster(44), rgb(255, 247, 226), 4, NSTextAlignmentCenter, shadow_of(0.8, 2, 3)), NSMakeRect(rx + 44, ry, rw - 88, rh));
        text_mid(caption, attrs(ital(25), rgb(255, 240, 214), 0.3, NSTextAlignmentCenter, shadow_of(0.85, 1.5, 3)), NSMakeRect(40, ry + rh + 10, W - 80, 40));
    });
}

int main(int argc, char **argv) {
    @autoreleasepool {
        if (argc == 3 && !strcmp(argv[1], "banner")) return banner(@(argv[2]));
        if ((argc == 7 || argc == 9) && !strcmp(argv[1], "card"))
            return card(@(argv[2]), @(argv[3]), @(argv[4]), @(argv[5]), argc == 9 && !strcmp(argv[7], "--over") ? @(argv[8]) : nil, @(argv[6]));
        fprintf(stderr, "usage: make_readme_art banner <out.png> | card <shot> <title> <caption> <colour> <out.jpg> [--over <png>]\n");
        return 2;
    }
}
