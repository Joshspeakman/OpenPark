/* The controls card over the game (Ctrl+/ or the control file's "controls"): the Mac's TPControlsCard (main.m:943-987)
 * drawn with draw.c. The shared controls table (runtime/app_spec.c, the launch window's Controls page) on a dark card over
 * the dimmed game, which waits while it is up (main.c refresh_hold). The Mac's system font is Poppins Bold for its heavy
 * lines and Alegreya Sans for the rest (platform/sdl/fonts). */
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app.h"
#include "app_spec.h"
#include "draw.h"

static Font *heavy, *regular;
/* (Poppins carries more room above its capitals than Alegreya Sans: its lines a little higher, so the gold keys sit
 * level with their descriptions as the Mac's one system font has them) */
static float poppins_dy = -0.08f;

/* Text with its line box's top at y (points), kern after every character, as NSString drawInRect: lays it out;
 * align 0 left, 1 centred in w. Cut with an ellipsis if wider than w. */
static void text(Canvas *c, float s, Font *f, float size, float kern, float x, float y, float w, int align, Col k, const char *t) {
    if (!f || !t) return;
    char fit[300];
    text_fit(f, size * s, kern / size, t, w * s, fit, sizeof fit);
    float tw = text_width(f, size * s, kern / size, fit) / s + kern, dy = f == heavy ? poppins_dy * size : 0;
    float x0 = align ? x + (w - tw) / 2 : x;
    cv_text(c, f, size * s, x0 * s, (y + dy) * s + font_ascent(f, size * s), kern / size, k, fit);
}

uint8_t *tp_card_render(int pw, int ph, float s, const char *fonts_dir) {
    if (!heavy) {
        char p[1200];
        snprintf(p, sizeof p, "%sfonts/Poppins-Bold.ttf", fonts_dir); heavy = font_load(p);
        snprintf(p, sizeof p, "%sfonts/AlegreyaSans-Regular.ttf", fonts_dir); regular = font_load(p);
        font_set_fallback(heavy, regular);
    }
    Canvas *c = cv_new(pw, ph);
    if (!c) return NULL;
    cv_fill(c, cola(0, 0, 0, 0.62f));
    const TpAppSpec *spec = tp_app_spec();
    float vw = pw / s, vh = ph / s;                  /* the window in points */
    float W = fminf(vw - 40, 760), rowH = 30, H = 96 + rowH * spec->ncontrols + 44;
    float k = fminf(1, (vh - 40) / H);               /* (a small window: the card smaller) */
    float ks = s * k, ox = (vw - W * k) / 2 * s, oy = (vh - H * k) / 2 * s;
    /* the card in its own canvas at the card's scale, then over the dimmed game */
    Canvas *card = cv_new((int)ceilf(W * ks) + 4, (int)ceilf(H * ks) + 4);
    if (card) {
        Box b = box(2, 2, W * ks, H * ks);
        cv_rrect(card, b, 18 * ks, cola(51, 28, 13, 0.96f));
        cv_rrect_line(card, inset(b, -1.5f * ks), 19.5f * ks, 3 * ks, col(237, 179, 64));
        Col gold = col(255, 204, 77), cream = col(255, 245, 219);
        float split = W * 0.42f, o = 2 / ks;          /* (o: the canvas's margin, in card points) */
        text(card, ks, heavy, 28, 3, o, o + 24, W, 1, gold, "CONTROLS");
        for (int i = 0; i < spec->ncontrols; i++) {
            float y = 84 + rowH * i;
            char key[96]; size_t j = 0;
            for (const char *q = spec->controls[i][0]; *q && j + 1 < sizeof key; q++) key[j++] = (char)((unsigned char)*q < 0x80 ? toupper((unsigned char)*q) : *q);
            key[j] = 0;
            text(card, ks, heavy, 15, 1, o + 36, o + y + 4, split - 44, 0, gold, key);
            text(card, ks, regular, 16, 0, o + split, o + y + 3, W - split - 30, 0, cream, spec->controls[i][1]);
        }
        text(card, ks, regular, 13, 0, o, o + H - 38, W, 1, with_alpha(cream, 0.6f), "Any key or click goes back to the game");
        cv_over(c, card, ox - 2, oy - 2, 1);
        cv_free(card);
    }
    uint8_t *px = c->px;
    free(c);                                         /* (the pixels are the caller's) */
    return px;
}
