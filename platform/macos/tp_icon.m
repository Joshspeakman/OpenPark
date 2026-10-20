// tp_icon <art> <out.png> [--nightly] [--box]: the app icon, made at install time (tools/build_app.sh) from the park
// planet: on GOG's box art (--box, downloaded from GOG's store at install time, the sharper one) or on GOG's icon in
// the player's own package (without its cyan ring), on a sky tile in macOS icon proportions (an 824 tile in 1024,
// corner radius 185), with the band of the other ports' Nightly apps when asked.
// Provenance: tile geometry, shading and the Nightly band follow ~/Projects/freedom-force-macos/recomp/tools/ff_icon.m
// (same author, read-only reference) so the Dock matches the other ports. Build: clang -fobjc-arc -framework AppKit.
#import <AppKit/AppKit.h>

static NSColor *rgb(int r, int g, int b) { return [NSColor colorWithSRGBRed:r / 255.0 green:g / 255.0 blue:b / 255.0 alpha:1]; }

/* The planet: everything outside a soft circle (image pixels, top-down) cleared. */
static NSImage *planet(NSImage *ico, CGFloat cx, CGFloat cy, CGFloat r0, CGFloat r1)
{
    CGImageRef c = [ico CGImageForProposedRect:NULL context:nil hints:nil]; if (!c) return nil;
    size_t w = CGImageGetWidth(c), h = CGImageGetHeight(c); uint8_t *px = calloc(w * h, 4);
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef g = CGBitmapContextCreate(px, w, h, 8, w * 4, cs, (CGBitmapInfo)kCGImageAlphaPremultipliedLast);
    CGContextDrawImage(g, CGRectMake(0, 0, w, h), c);
    for (size_t y = 0; y < h; y++) for (size_t x = 0; x < w; x++) {   /* rows run top-down in the buffer */
        CGFloat d = hypot(x + 0.5 - cx, y + 0.5 - cy), a = d <= r0 ? 1 : d >= r1 ? 0 : (r1 - d) / (r1 - r0);
        for (int i = 0; i < 4; i++) px[(y * w + x) * 4 + i] = (uint8_t)lround(px[(y * w + x) * 4 + i] * a);
    }
    CGImageRef out = CGBitmapContextCreateImage(g);
    NSImage *img = [[NSImage alloc] initWithCGImage:out size:NSMakeSize(w, h)];
    CGImageRelease(out); CGContextRelease(g); CGColorSpaceRelease(cs); free(px);
    return img;
}

int main(int argc, char **argv)
{
    @autoreleasepool {
        if (argc < 3) { fprintf(stderr, "usage: tp_icon <art> <out.png> [--nightly] [--box]\n"); return 2; }
        BOOL nightly = NO, box = NO;
        for (int k = 3; k < argc; k++) { if (!strcmp(argv[k], "--nightly")) nightly = YES; if (!strcmp(argv[k], "--box")) box = YES; }
        NSImage *ico = [[NSImage alloc] initWithContentsOfFile:@(argv[1])];
        if (ico.size.width < 64) { fprintf(stderr, "tp_icon: no icon in %s\n", argv[1]); return 1; }
        CGImageRef ci = [ico CGImageForProposedRect:NULL context:nil hints:nil]; if (!ci) return 1;
        CGFloat iw = CGImageGetWidth(ci), ih = CGImageGetHeight(ci), u = box ? iw / 342 : iw / 256;
        /* the planet's centre and radius: on the 342 x 482 box art, or on the 256-pixel icon */
        CGFloat cx = (box ? 171 : 128) * u, cy = (box ? 284 : 125) * u, r0 = (box ? 150 : 82) * u, r1 = (box ? 161 : 91) * u;
        if (box && ih < 400 * u) return 1;
        NSImage *globe = planet(ico, cx, cy, r0, r1);
        if (!globe) return 1;
        const CGFloat S = 1024;
        NSBitmapImageRep *rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL pixelsWide:(NSInteger)S pixelsHigh:(NSInteger)S bitsPerSample:8 samplesPerPixel:4
            hasAlpha:YES isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:0 bitsPerPixel:0];
        [NSGraphicsContext saveGraphicsState];
        NSGraphicsContext.currentContext = [NSGraphicsContext graphicsContextWithBitmapImageRep:rep];
        NSGraphicsContext.currentContext.imageInterpolation = NSImageInterpolationHigh;
        NSRect tile = NSMakeRect(100, 100, 824, 824);
        NSBezierPath *shape = [NSBezierPath bezierPathWithRoundedRect:tile xRadius:185 yRadius:185];
        NSShadow *drop = [NSShadow new]; drop.shadowColor = [NSColor colorWithWhite:0 alpha:0.45]; drop.shadowOffset = NSMakeSize(0, -10); drop.shadowBlurRadius = 24;
        [NSGraphicsContext saveGraphicsState]; [drop set]; [NSColor.blackColor setFill]; [shape fill]; [NSGraphicsContext restoreGraphicsState];
        [NSGraphicsContext saveGraphicsState]; [shape addClip];

        NSGradient *sky = [[NSGradient alloc] initWithColorsAndLocations:rgb(30, 86, 178), 0.0, rgb(66, 146, 228), 0.55, rgb(150, 206, 250), 1.0, nil];
        [sky drawInRect:tile angle:90];
        CGFloat floor = nightly ? 293 : 100, room = 924 - floor;               /* the planet sits above the band */
        CGFloat d = MIN(room * 0.96, 680);
        NSRect to = NSMakeRect(512 - d / 2, floor + (room - d) / 2 - (nightly ? 6 : 0), d, d);
        NSRect from = NSMakeRect(cx - r1, ih - cy - r1, 2 * r1, 2 * r1);       /* AppKit images run bottom-up */
        NSShadow *s = [NSShadow new]; s.shadowColor = [NSColor colorWithWhite:0 alpha:0.35]; s.shadowOffset = NSMakeSize(0, -12); s.shadowBlurRadius = 28;
        [NSGraphicsContext saveGraphicsState]; [s set];
        [globe drawInRect:to fromRect:from operation:NSCompositingOperationSourceOver fraction:1 respectFlipped:NO hints:@{NSImageHintInterpolation: @(NSImageInterpolationHigh)}];
        [NSGraphicsContext restoreGraphicsState];

        NSGradient *shade = [[NSGradient alloc] initWithColorsAndLocations:[NSColor colorWithWhite:0 alpha:0.0], 0.0, [NSColor colorWithWhite:0 alpha:0.0], 0.7,
                             [NSColor colorWithWhite:1 alpha:0.10], 1.0, nil];
        [shade drawInRect:tile angle:90];                                      /* a little light from above */
        if (nightly) {   /* the band, as on the other ports' Nightly apps */
            NSRect band = NSMakeRect(100, 100, 824, 185);
            NSGradient *fill = [[NSGradient alloc] initWithStartingColor:[NSColor colorWithSRGBRed:0.36 green:0.20 blue:0.78 alpha:1] endingColor:[NSColor colorWithSRGBRed:0.20 green:0.10 blue:0.52 alpha:1]];
            [fill drawInRect:band angle:-90];
            [rgb(8, 8, 14) setFill]; NSRectFill(NSMakeRect(100, 285, 824, 8));
            NSMutableParagraphStyle *ps = [NSMutableParagraphStyle new]; ps.alignment = NSTextAlignmentCenter;
            NSShadow *ts = [NSShadow new]; ts.shadowColor = [NSColor colorWithWhite:0 alpha:0.6]; ts.shadowOffset = NSMakeSize(0, -3); ts.shadowBlurRadius = 6;
            NSDictionary *at = @{NSFontAttributeName: [NSFont systemFontOfSize:104 weight:NSFontWeightHeavy], NSForegroundColorAttributeName: NSColor.whiteColor,
                                 NSParagraphStyleAttributeName: ps, NSKernAttributeName: @8, NSShadowAttributeName: ts};
            NSSize ls = [@"NIGHTLY" sizeWithAttributes:at];
            [@"NIGHTLY" drawInRect:NSMakeRect(100, NSMidY(band) + 12 - ls.height / 2, 824, ls.height) withAttributes:at];
        }
        [NSGraphicsContext restoreGraphicsState];
        [rgb(8, 8, 14) setStroke]; shape.lineWidth = 14; [shape stroke];      /* the outline, and the launcher's yellow keyline */
        NSBezierPath *key = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(tile, 14, 14) xRadius:172 yRadius:172];
        [[rgb(255, 210, 63) colorWithAlphaComponent:0.9] setStroke]; key.lineWidth = 5; [key stroke];
        [NSGraphicsContext restoreGraphicsState];
        if (![[rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:@(argv[2]) atomically:YES]) { fprintf(stderr, "cannot write %s\n", argv[2]); return 1; }
    }
    return 0;
}
