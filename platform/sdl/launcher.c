/* Theme Park's launch window for Windows and Linux: the Mac's "midway at dusk" (platform/macos/launcher.m), the same
 * window drawn the same way. Its rows, tickets, tiles, controls and words are runtime/app_spec.c, which the Mac's reads
 * too; the measurements and colours are docs/parity/launcher.md's (from launcher.m, cited there line by line).
 *
 * The settings view is drawn in software (draw.c) into one canvas at the display's scale, only when something changes
 * (a hover, a focus, a setting, a page); what moves (the park, dusk, searchlights, fireworks, bunting, bulbs, the glint,
 * motes) is midway.c's, sprites the GPU places every frame, as the Mac's Core Animation layers. Settings are
 * settings.ini's (settings.c), written as they change.
 *
 * Fonts (platform/sdl/fonts, beside the program): open-licence stand-ins for the Mac's Futura Condensed ExtraBold,
 * Futura Bold, American Typewriter Bold and Optima. Art (beside the program in art/, else <data>/art/): keyart.jpg
 * and logo.png (GOG's store art, fetched at install time onto this machine only), banner.png (the player's own title
 * screen), your-park.png (the park, saved at quit) and park-shot-*.png (GOG's park screenshots). Without them the
 * window draws its fallbacks, as the Mac's does.
 *
 * Test hooks, as the Mac's: TP_LAUNCHER_SHOT=<png> renders the window once at 2000 x 1400 and returns "don't play";
 * TP_LAUNCHER_OPEN=<page>, TP_LAUNCHER_FOCUS=<n>, TP_LAUNCHER_HOVER=<hit>, TP_LAUNCHER_TIME=<s>, TP_LAUNCHER_NO_ART=1,
 * TP_LAUNCHER_PARK=<png>, TP_LAUNCHER_PROFILE=<s>. */
#include <SDL.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "app.h"
#include "app_spec.h"
#include "draw.h"
#include "midway.h"
#include "platform.h"

/* ---- colours (launcher.m:24-31) ---- */
#define GOLD col(250, 204, 82)
#define GOLD_DK col(176, 118, 36)
#define CREAM col(255, 247, 226)
#define MUTED col(190, 186, 214)
#define NIGHT(a) cola(14, 16, 42, a)
#define WHITE(a) cola(255, 255, 255, a)
#define BLACK(a) cola(0, 0, 0, a)

enum { HIT_PLAY = 1000, HIT_QUIT, HIT_BACK, HIT_SAVES, HIT_UPDATES };
enum { UPD_NONE, UPD_CHECKING, UPD_CURRENT, UPD_AVAILABLE, UPD_ERROR };

/* A font role at a size (points), its extra advance (points), and how far its lines sit lower than the font's own
 * metrics put them (ems): the stand-in faces' metrics differ from the Mac's (Poppins stands taller than Futura). */
typedef struct { Font *f; float size, kern, dy; } Face;
static struct {
    const TpAppSpec *spec;
    float s;                                         /* device pixels per point */
    Font *poster, *caps, *print, *plain, *ital;
    Canvas *cv;                                      /* the settings view, 1000 x 700 points at s */
    int page, focus, hover, drag_row, dirty;
    int result, done;
    Image *logo, *keyart, *poster_img;               /* poster_img: the riders, cut and sized for the well */
    int update_state; char update_latest[64];
    char data_root[1024], res_dir[1024], version[64];
} L;

static const char *G(const char *k) { const char *v = getenv(k); return v && *v ? v : NULL; }

/* ---- type ---- */
/* (dy measured against the Mac's renders, docs/launcher-reference: caps sat 3.6 pt high at 10.5 pt; the others within
 * half a point) */
static Face poster(float n) { return (Face){L.poster, n, 0, 0}; }
static Face caps(float n) { return (Face){L.caps, n, 0, 0.345f}; }
static Face print_f(float n) { return (Face){L.print, n, 0, 0}; }
static Face plain(float n) { return (Face){L.plain, n, 0, 0}; }
static Face ital(float n) { return (Face){L.ital, n, 0, 0}; }
static Face kern(Face f, float k) { f.kern = k; return f; }
/* A line's width in points, with AppKit's kern after every character (the last too: centred text sits kern/2 left). */
static float width_of(Face f, const char *t) {
    if (!f.f || !t || !*t) return 0;
    return text_width(f.f, f.size * L.s, f.kern / f.size, t) / L.s + f.kern;
}
static float line_h(Face f) { return f.f ? (font_ascent(f.f, f.size * L.s) + font_descent(f.f, f.size * L.s)) / L.s : f.size; }
/* Text drawn at (x, top of its line box) in points, with an optional shadow (alpha, offset down, AppKit blur). */
static void text_at(Face f, float x, float top, Col k, const char *t, float sh_a, float sh_dy, float sh_blur) {
    if (!f.f || !t || !*t) return;
    top += f.dy * f.size;
    float s = L.s, asc = font_ascent(f.f, f.size * s), desc = font_descent(f.f, f.size * s);
    if (sh_a > 0) {
        float w = text_width(f.f, f.size * s, f.kern / f.size, t);
        int m = 2;
        Canvas *layer = cv_new((int)ceilf(w) + 2 * m + 2, (int)ceilf(asc + desc) + 2 * m + 2);
        if (layer) {
            cv_text(layer, f.f, f.size * s, m + (x * s - floorf(x * s)), m + asc, f.kern / f.size, k, t);
            float lx = floorf(x * s) - m, ly = floorf(top * s) - m;
            cv_shadow_of(L.cv, layer, lx, ly, 0, sh_dy * s, sh_blur * s, BLACK(sh_a));
            cv_over(L.cv, layer, lx, ly, 1);
            cv_free(layer);
            return;
        }
    }
    cv_text(L.cv, f.f, f.size * s, x * s, top * s + asc, f.kern / f.size, k, t);
}
enum { LEFT, CENTRE, RIGHT };
/* launcher.m label(): one line, vertically centred in r, cut with "…" at its tail if too wide. */
static void label(const char *t, Face f, Col k, Box r, int align, int shadow) {
    if (!t || !*t || !f.f) return;
    char fit[512];
    text_fit(f.f, f.size * L.s, f.kern / f.size, t, (r.w - f.kern) * L.s, fit, sizeof fit);
    float w = width_of(f, fit), h = line_h(f);
    float x = align == LEFT ? r.x : align == CENTRE ? r.x + (r.w - w) / 2 : r.x + r.w - w;
    text_at(f, x, r.y + r.h / 2 - h / 2, k, fit, shadow ? 0.85f : 0, 1.5f, 3);
}
/* launcher.m wrapped(): word-wrapped, top-aligned, lines 2 pt apart, shadowed (0.8, 1, 3); "\n" breaks a line. */
static void wrapped(const char *t, Face f, Col k, Box r, int align) {
    if (!t || !f.f) return;
    float h = line_h(f), y = r.y;
    char line[512] = "";
    const char *p = t;
    while (*p && y + h <= r.y + r.h + 0.5f) {
        /* the longest run of words that fits */
        const char *end = p, *best = NULL;
        for (;;) {
            const char *q = end;
            while (*q && *q != ' ' && *q != '\n') q++;
            char tryl[512]; int n = (int)(q - p); if (n >= (int)sizeof tryl) n = (int)sizeof tryl - 1;
            memcpy(tryl, p, (size_t)n); tryl[n] = 0;
            if (best && width_of(f, tryl) > r.w) break;
            best = q;
            if (!*q || *q == '\n') break;
            end = q + 1;
        }
        int n = (int)(best - p); if (n >= (int)sizeof line) n = (int)sizeof line - 1;
        memcpy(line, p, (size_t)n); line[n] = 0;
        float w = width_of(f, line), x = align == LEFT ? r.x : align == CENTRE ? r.x + (r.w - w) / 2 : r.x + r.w - w;
        text_at(f, x, y, k, line, 0.8f, 1, 3);
        y += h + 2;
        p = best;
        while (*p == ' ' || *p == '\n') p++;
    }
}
static void upper(const char *in, char *out, size_t n) {
    size_t i = 0;
    for (; in[i] && i + 1 < n; i++) out[i] = (char)((unsigned char)in[i] < 0x80 ? toupper((unsigned char)in[i]) : in[i]);
    out[i] = 0;
}

/* ---- shapes in points ---- */
static Box P(Box b) { return box(b.x * L.s, b.y * L.s, b.w * L.s, b.h * L.s); }
static void rrect(Box b, float r, Col k) { cv_rrect(L.cv, P(b), r * L.s, k); }
/* An NSBezierPath stroke: `width` centred on the rounded path b (radius r). */
static void stroke(Box b, float r, float width, Col k) { cv_rrect_line(L.cv, P(inset(b, -width / 2)), (r + width / 2) * L.s, width * L.s, k); }
static void grad(Box b, float r, const Col *c, const float *pos, int n, float angle) { cv_grad_rrect(L.cv, P(b), r * L.s, c, pos, n, angle); }
static void grad2(Box b, float r, Col a, Col z, float angle) { Col c[2] = {a, z}; float p[2] = {0, 1}; grad(b, r, c, p, 2, angle); }
/* shadow(α, dy, blur) under a rounded rect (AppKit's blur radius: sigma = blur / 2). */
static void shadow(Box b, float r, float a, float dy, float blur) { cv_shadow(L.cv, P(box(b.x, b.y + dy, b.w, b.h)), r * L.s, blur * L.s, BLACK(a)); }
static void circle(float x, float y, float rad, Col k) { cv_circle(L.cv, x * L.s, y * L.s, rad * L.s, k); }
static void line(float x0, float y0, float x1, float y1, float w, Col k) { cv_line(L.cv, x0 * L.s, y0 * L.s, x1 * L.s, y1 * L.s, w * L.s, k); }
static void poly(const float *xy, int n, Col k) {
    float t[64]; for (int i = 0; i < 2 * n && i < 64; i++) t[i] = xy[i] * L.s;
    cv_poly(L.cv, t, n, k);
}
static void polyline(const float *xy, int n, float w, int closed, Col k) {
    float t[200]; for (int i = 0; i < 2 * n && i < 200; i++) t[i] = xy[i] * L.s;
    cv_polyline(L.cv, t, n, w * L.s, closed, k);
}
/* A five-point star, first point up (launcher.m:516-521). */
static void star(float cx, float cy, float r, Col k) {
    float xy[20];
    for (int i = 0; i < 10; i++) {
        float a = -3.14159265f / 2 + i * 3.14159265f / 5, rr = i % 2 ? r * 0.45f : r;
        xy[2 * i] = cx + rr * cosf(a); xy[2 * i + 1] = cy + rr * sinf(a);
    }
    poly(xy, 10, k);
}
/* Drawing into a layer of its own (its shadow, its cut-outs), then over the view: `into` swaps the canvas. */
static Canvas *saved_cv;
static Canvas *layer_begin(void) {
    Canvas *c = cv_new(L.cv->w, L.cv->h);
    if (!c) return NULL;
    saved_cv = L.cv; L.cv = c;
    return c;
}
static void layer_end(Canvas *c, float sh_a, float sh_dy, float sh_blur, Col glow_k, float glow_blur) {
    if (!c) return;
    L.cv = saved_cv;
    if (glow_blur > 0) cv_shadow_of(L.cv, c, 0, 0, 0, 0, glow_blur * L.s, glow_k);
    if (sh_a > 0) cv_shadow_of(L.cv, c, 0, 0, 0, sh_dy * L.s, sh_blur * L.s, BLACK(sh_a));
    cv_over(L.cv, c, 0, 0, 1);
    cv_free(c);
}

/* ---- settings ---- */
static const TpRow *row_of(int page, int k) { return L.spec->pages[page].rows[k]; }
static int index_of(const TpRow *r) {
    if (!r || !r->key || !r->n) return 0;
    double v = settings_getf(r->key);
    int found = -1;
    for (int i = 0; i < r->n; i++) if (fabs(r->choices[i].value - v) < 0.05) found = i;   /* (the last within 0.05, as the Mac) */
    return found < 0 ? 0 : found;
}
static const char *label_of(const TpRow *r) { return r->n ? r->choices[index_of(r)].label : r->value; }
static void store(const TpRow *r, int i) {
    double v = r->choices[i].value;
    if (r->kind == TP_ROW_BOOL) settings_set(r->key, v != 0);
    else if (v == floor(v)) settings_set(r->key, (long)v);
    else settings_setf(r->key, v);
}
static int extras_missing(const char *pack) {
    char p[1300]; snprintf(p, sizeof p, "%s/extras/%s/manifest.json", L.data_root, pack);
    struct stat st; return stat(p, &st) != 0;
}
static int tunes_changed(void) {
    char t[464]; settings_gets("RideTunes", t, sizeof t);
    int n = 0; for (char *p = t; *p; p++) if (*p == ':') n++;
    return n;
}
static void summary(int page, char *out, size_t n);
/* A row's value and note as shown (launcher.m valueOf / noteOf). */
static void row_text(const TpRow *r, char *value, size_t vn, const char **note) {
    static char sb[300];
    *note = r->note;
    switch (r->kind) {
    case TP_ROW_PAGE: summary(r->page, value, vn); return;
    case TP_ROW_FILES: snprintf(value, vn, "%s", r->value); return;
    case TP_ROW_RIDETUNES: {
        int n = tunes_changed();
        if (n) { snprintf(value, vn, "%d Changed", n); *note = "→ changes them; ← puts every ride back"; }
        else snprintf(value, vn, "%s", r->value);
        return;
    }
    case TP_ROW_SOUNDBANK: {
        settings_gets("SoundBank", sb, sizeof sb);
        if (!*sb) { snprintf(value, vn, "%s", r->value); return; }
        const char *base = sb + strlen(sb);
        while (base > sb && base[-1] != '/' && base[-1] != '\\') base--;
        snprintf(value, vn, "%s", base);
        char *dot = strrchr(value, '.'); if (dot) *dot = 0;
        *note = "Your SoundFont for the music; ← goes back to built-in";
        return;
    }
    default: {
        int i = index_of(r);
        snprintf(value, vn, "%s", r->choices[i].label);
        *note = r->choices[i].note && *r->choices[i].note ? r->choices[i].note : r->note;
        int dropin = G("TP_IMAGE_DIR") != NULL;
        if (!strcmp(r->key, "MusicType") && i != 0 && extras_missing(i == 1 ? "amiga-cd32" : "3do-usa"))
            *note = dropin ? "Not imported yet: drop your disc's image (.cue) on the app; MIDI plays until then"
                           : "Not imported yet: tools/extras.py <your disc's .cue>; MIDI plays until then";
        if (!strcmp(r->key, "RideVideos") && i != 0 && extras_missing("3do-usa"))
            *note = dropin ? "Not imported yet: drop your 3DO disc's image (.cue) on the app" : "Not imported yet: tools/extras.py <your 3DO disc's .cue>";
    }
    }
}
/* The tickets' and link rows' summaries (launcher.m summary:, :963-974). */
static void summary(int page, char *out, size_t n) {
    switch (page) {
    case TP_PAGE_GAME: snprintf(out, n, "%s · %s", label_of(tp_app_row("Cycles")), label_of(tp_app_row("Language"))); break;
    case TP_PAGE_DISPLAY: snprintf(out, n, "%s · %s", label_of(tp_app_row("StartFullScreen")), label_of(tp_app_row("StartHiRes"))); break;
    case TP_PAGE_SOUND:
        if (!settings_get("Sound")) snprintf(out, n, "Off");
        else snprintf(out, n, "Music %s · Effects %s", label_of(tp_app_row("MusicVolume")), label_of(tp_app_row("SoundVolume")));
        break;
    case TP_PAGE_FREE: snprintf(out, n, "%s · %s", label_of(tp_app_row("FreeRules")), label_of(tp_app_row("FreeMapSize"))); break;
    case TP_PAGE_CONTROLS: snprintf(out, n, "Mouse, keys, controller"); break;
    case TP_PAGE_MUSIC: snprintf(out, n, "%s", label_of(tp_app_row("MusicType"))); break;
    case TP_PAGE_EFFECTS: {
        char a[64], b[64]; const char *ew = label_of(tp_app_row("GrossOutGap")), *till = label_of(tp_app_row("PurchaseGap"));
        snprintf(a, sizeof a, "%s", strncmp(ew, "Every ", 6) ? ew : ew + 6);
        snprintf(b, sizeof b, "%s", strncmp(till, "Every ", 6) ? till : till + 6);
        snprintf(out, n, "Ew %s · Till %s", a, b);
        break;
    }
    default: out[0] = 0;
    }
}

/* ---- layout (docs/parity/launcher.md 1.3, 1.4) ---- */
static const Box SIGN = {44, 30, 584, 166}, LOGO_AREA = {88, 80, 496, 94}, POSTER = {664, 30, 292, 412}, SAVES = {44, 592, 176, 56},
                 UPDATES = {232, 592, 236, 56}, QUIT = {556, 592, 162, 56}, PLAY = {734, 588, 222, 64}, BOARD = {44, 30, 912, 536};
static Box ticket_box(int g) { return box(44 + g * 119.2f, 310, 107.2f, 132); }
static Box tile_box(int k) { return box(44 + k * 231.f, 484, 219, 82); }
static int page_rows(void) { return L.page >= 0 ? L.spec->pages[L.page].n : 0; }
static int compact(void) { return page_rows() > 6; }
static float pitch(void) {
    int n = page_rows(); float avail = L.page == TP_PAGE_FREE ? 364 : 420;
    return n ? fminf(72, floorf(avail / n)) : 72;
}
static Box row_box(int k) { return box(78, 126 + k * pitch(), 844, pitch()); }
static float row_top(void) { return compact() ? 3 : 8; }
static Box arrow_box(int k, int right) { Box r = row_box(k); return box(right ? 878 : 502, r.y + row_top(), 34, 30); }
static Box track_box(int k) { Box r = row_box(k); return box(552, r.y + (compact() ? 34 : 44), 310, 9); }
static Box logo_box(void) {
    if (!L.logo) return LOGO_AREA;
    float k = fminf(LOGO_AREA.w / L.logo->w, LOGO_AREA.h / L.logo->h), w = roundf(L.logo->w * k), h = roundf(L.logo->h * k);
    return box(LOGO_AREA.x + (LOGO_AREA.w - w) / 2, LOGO_AREA.y + (LOGO_AREA.h - h) / 2, w, h);
}

/* ---- pieces (docs/parity/launcher.md 4) ---- */
static void heading(const char *t, float x, float y, float x1) {
    Face f = kern(caps(11), 5);
    text_at(f, x, y - 8, GOLD, t, 0.9f, 1, 3);
    float w = width_of(f, t);
    star(x + w + 10, y + 1, 4.5f, GOLD);
    grad2(box(x + w + 20, y, x1 - (x + w + 20), 1), 0, with_alpha(GOLD, 0.7f), with_alpha(GOLD, 0), 0);
}
static void plate(Box r, float rad, int lit) {
    shadow(r, rad, 0.7f, 4, 12);
    rrect(r, rad, NIGHT(lit ? 0.95f : 0.92f));
    if (lit) grad2(r, rad, with_alpha(GOLD, 0.16f), with_alpha(GOLD, 0.02f), -90);
    stroke(inset(r, 0.5f), rad, lit ? 1.6f : 1.1f, lit ? GOLD : with_alpha(GOLD, 0.75f));
    stroke(inset(r, 4.5f), fmaxf(rad - 4, 2), 1, with_alpha(GOLD, 0.22f));
    float xs[2] = {r.x + 9, r.x + r.w - 9}, ys[2] = {r.y + 9, r.y + r.h - 9};
    for (int i = 0; i < 4; i++) { circle(xs[i & 1], ys[i >> 1], 2.2f, GOLD_DK); circle(xs[i & 1] - 0.1f, ys[i >> 1] - 0.4f, 1.2f, GOLD); }
    if (lit == 2) stroke(inset(r, -3), rad + 3, 2, with_alpha(CREAM, 0.85f));
}
static const Col SIGN_GOLD[3] = {{255 / 255.f, 228 / 255.f, 140 / 255.f, 1}, {232 / 255.f, 172 / 255.f, 54 / 255.f, 1}, {150 / 255.f, 94 / 255.f, 22 / 255.f, 1}};
static const float SIGN_GOLD_AT[3] = {0, 0.45f, 1};
static void draw_sign(void) {
    shadow(SIGN, 18, 0.75f, 6, 18);
    rrect(SIGN, 18, col(46, 8, 16));
    grad(inset(SIGN, 2), 16, SIGN_GOLD, SIGN_GOLD_AT, 3, -90);
    Box in = {58, 44, 556, 138};
    Col lac[3] = {col(214, 38, 46), col(168, 20, 32), col(104, 10, 22)}; float lac_at[3] = {0, 0.55f, 1};
    grad(in, 8, lac, lac_at, 3, -90);
    /* candy stripes and the sheen, inside the panel's rounded corners */
    Canvas *c = layer_begin();
    if (c) {
        for (float x = in.x - in.h; x < in.x + in.w; x += 26) {
            float xy[8] = {x, in.y + in.h, x + 12, in.y + in.h, x + 12 + in.h, in.y, x + in.h, in.y};
            poly(xy, 4, WHITE(0.055f));
        }
        grad2(box(58, 44, 556, 40), 0, WHITE(0.20f), WHITE(0), 90);
        /* (keep only what lies in the panel) */
        Canvas *mask = cv_new(c->w, c->h);
        if (mask) {
            cv_rrect(mask, P(in), 8 * L.s, col(255, 255, 255));
            for (size_t i = 0; i < (size_t)c->w * c->h; i++) { unsigned a = mask->px[i * 4 + 3]; for (int ch = 0; ch < 4; ch++) c->px[i * 4 + ch] = (uint8_t)(c->px[i * 4 + ch] * a / 255); }
            cv_free(mask);
        }
        layer_end(c, 0, 0, 0, (Col){0, 0, 0, 0}, 0);
    }
    stroke(in, 8, 2, col(70, 8, 16));
    stroke(inset(in, 4), 5, 1, cola(255, 220, 120, 0.5f));
    star(80, 66, 7, GOLD); star(592, 66, 7, GOLD);
    label(L.spec->presents, kern(caps(10.5f), 4.2f), col(255, 226, 150), box(58, 56, 556, 20), CENTRE, 1);
    if (L.logo) {
        Box lb = logo_box();
        Image *m = img_scaled(L.logo, box(0, 0, (float)L.logo->w, (float)L.logo->h), (int)lroundf(lb.w * L.s), (int)lroundf(lb.h * L.s));
        if (m) {
            Canvas lay = {m->w, m->h, m->px};
            cv_shadow_of(L.cv, &lay, lb.x * L.s, lb.y * L.s, 0, 4 * L.s, 9 * L.s, BLACK(0.6f));
            cv_image(L.cv, m, lb.x * L.s, lb.y * L.s, 1);
            img_free(m);
        }
    } else label("THEME PARK", kern(poster(64), 3), CREAM, LOGO_AREA, CENTRE, 1);
}
static void draw_poster(void) {
    shadow(POSTER, 0, 0.8f, 8, 20);
    rrect(POSTER, 0, col(36, 18, 10));
    Col gilt[4] = {col(255, 232, 150), col(214, 156, 46), col(255, 214, 110), col(140, 86, 20)}; float gilt_at[4] = {0, 0.35f, 0.6f, 1};
    grad(inset(POSTER, 1), 0, gilt, gilt_at, 4, -60);
    cv_rrect_line(L.cv, P(box(675.5f, 41.5f, 269, 389)), 0, 1.5f * L.s, col(80, 46, 12));
    Box well = {678, 44, 264, 326}, plaque = {678, 370, 264, 56};
    rrect(well, 0, BLACK(1)); rrect(plaque, 0, BLACK(1));
    if (L.poster_img) cv_image(L.cv, L.poster_img, well.x * L.s, well.y * L.s, 1);
    else grad2(well, 0, col(40, 140, 220), col(230, 120, 60), -70);
    grad2(box(678, 300, 264, 70), 0, BLACK(0), BLACK(0.75f), 90);
    /* NOW OPEN: a band turned 45° up to the right about (718, 84), inside the picture */
    float s = L.s;
    int bw = (int)ceilf(200 * s), bh = (int)ceilf(48 * s);
    Canvas *band = cv_new(bw, bh);
    if (band) {
        Canvas *keep = L.cv; L.cv = band;
        /* (local origin at (100, 24) points in the band's canvas) */
        Box b = box(100 - 90, 24 - 14, 180, 28);
        shadow(b, 0, 0.6f, 2, 5);
        rrect(b, 0, col(196, 28, 36));
        rrect(box(10, 24 - 12 - 0.5f, 180, 1), 0, cola(255, 220, 120, 0.9f));
        rrect(box(10, 24 + 11 - 0.5f, 180, 1), 0, cola(255, 220, 120, 0.9f));
        label(L.spec->sash, kern(caps(11), 3.5f), CREAM, box(10, 24 - 10, 180, 20), CENTRE, 0);
        L.cv = keep;
        cv_clip(L.cv, P(well));
        cv_over_turned(L.cv, band, 100 * s, 24 * s, 718 * s, 84 * s, -3.14159265f / 4, 1);
        cv_unclip(L.cv);
        cv_free(band);
    }
    rrect(plaque, 0, NIGHT(0.95f));
    rrect(box(678, 370, 264, 1.5f), 0, with_alpha(GOLD, 0.8f));
    label(L.spec->plaque, kern(caps(13), 4.5f), GOLD, box(678, 377, 264, 22), CENTRE, 1);
    label(L.spec->plaque_sub, kern(ital(12.5f), 0.2f), CREAM, box(684, 400, 252, 18), CENTRE, 1);
}
/* The ticket icons: cream line drawings in a 44 pt box about its centre m (launcher.m icon(), :532-569). */
static void icon(int g, float mx, float my, Col k) {
    if (g == 0) {                                                   /* a Ferris wheel */
        float cx = mx, cy = my - 3, xy[2 * 33];
        for (int i = 0; i <= 32; i++) { float a = i * 6.2831853f / 32; xy[2 * i] = cx + 15 * cosf(a); xy[2 * i + 1] = cy + 15 * sinf(a); }
        polyline(xy, 32, 2.2f, 1, k);
        for (int i = 0; i < 8; i++) {
            float a = i * 3.14159265f / 4, ex = cx + 15 * cosf(a), ey = cy + 15 * sinf(a);
            line(cx, cy, ex, ey, 1.2f, k); circle(ex, ey, 2.6f, k);
        }
        float legs[6] = {mx - 12, my + 20, cx, cy, mx + 12, my + 20};
        polyline(legs, 3, 2.2f, 0, k);
    } else if (g == 1) {                                            /* a 1994 monitor */
        stroke(box(mx - 18, my - 16, 36, 27), 4, 2.2f, k);
        rrect(box(mx - 18 + 5, my - 16 + 4.5f, 26, 18), 3, with_alpha(k, 0.35f));
        float stand[8] = {mx - 5, my + 12, mx - 8, my + 18, mx + 8, my + 18, mx + 5, my + 12};
        polyline(stand, 4, 2.2f, 0, k);
    } else if (g == 2) {                                            /* a speaker */
        float sp[12] = {-16, -6, -9, -6, 0, -14, 0, 14, -9, 6, -16, 6};
        for (int i = 0; i < 6; i++) { sp[2 * i] += mx; sp[2 * i + 1] += my; }
        poly(sp, 6, k);
        for (int r = 10; r <= 20; r += 5) {
            float xy[2 * 17];
            for (int i = 0; i <= 16; i++) { float a = (-40 + 80.f * i / 16) * 3.14159265f / 180; xy[2 * i] = mx + 1 + r * cosf(a); xy[2 * i + 1] = my + r * sinf(a); }
            polyline(xy, 17, 2, 0, k);
        }
    } else if (g == 3) {                                            /* a lemniscate */
        float xy[2 * 65];
        for (int i = 0; i < 65; i++) {
            float t = i * 6.2831853f / 64, d = 1 + sinf(t) * sinf(t);
            xy[2 * i] = mx + 19 * cosf(t) / d; xy[2 * i + 1] = my + 19 * sinf(t) * cosf(t) / d;
        }
        polyline(xy, 64, 3, 1, k);
    } else {                                                        /* a mouse */
        stroke(box(mx - 11, my - 12, 22, 31), 11, 2.2f, k);
        line(mx, my - 12, mx, my - 1, 1.6f, k);
        line(mx - 11, my - 1, mx + 11, my - 1, 1.6f, k);
        float xy[2 * 17];
        for (int i = 0; i <= 16; i++) {                             /* the cord: a cubic */
            float t = i / 16.f, u = 1 - t;
            xy[2 * i] = u * u * u * mx + 3 * u * u * t * mx + 3 * u * t * t * (mx + 8) + t * t * t * (mx + 12);
            xy[2 * i + 1] = u * u * u * (my - 12) + 3 * u * u * t * (my - 20) + 3 * u * t * t * (my - 14) + t * t * t * (my - 20);
        }
        polyline(xy, 17, 1.6f, 0, k);
    }
}
static Col shade(Col c, Col to, float t) { return mix(c, to, t); }
static void dots_line(float x0, float y0, float x1, float y1, float d, Col k) {
    float len = hypotf(x1 - x0, y1 - y0); int n = (int)(len / 6);
    for (int i = 1; i < n; i++) circle(x0 + (x1 - x0) * i / n, y0 + (y1 - y0) * i / n, d / 2, k);
}
static void draw_ticket(int g, int state) {
    const TpTicket *t = &L.spec->tickets[g];
    Box r = ticket_box(g);
    if (state) r.y -= 4;
    float x = r.x, y = r.y, w = r.w, ny = y + 92;
    Col base = col(t->rgb[0], t->rgb[1], t->rgb[2]), white = col(255, 255, 255), black = col(0, 0, 0);
    Canvas *c = layer_begin();
    if (c) {
        rrect(r, 9, base);
        grad2(r, 9, shade(base, white, state ? 0.38f : 0.25f), shade(base, black, 0.30f), -90);
        grad2(box(x, ny, w, r.h - 92), 0, shade(base, black, 0.55f), shade(base, black, 0.70f), -90);
        for (float sx = x - 60; sx < x + w; sx += 14) {
            float xy[8] = {sx, ny, sx + 6, ny, sx + 66, y, sx + 60, y};
            poly(xy, 4, WHITE(0.06f));
        }
        /* (the stripes and the stub stay inside the ticket's rounded corners) */
        Canvas *mask = cv_new(c->w, c->h);
        if (mask) {
            cv_rrect(mask, P(r), 9 * L.s, white);
            for (size_t i = 0; i < (size_t)c->w * c->h; i++) { unsigned a = mask->px[i * 4 + 3]; for (int ch = 0; ch < 4; ch++) c->px[i * 4 + ch] = (uint8_t)(c->px[i * 4 + ch] * a / 255); }
            cv_free(mask);
        }
        stroke(box(x + 5.5f, y + 5.5f, w - 11, 92 - 11), 5, 1, cola(255, 247, 226, 0.55f));
        cv_erase_circle(c, x * L.s, ny * L.s, 7 * L.s);
        cv_erase_circle(c, (x + w) * L.s, ny * L.s, 7 * L.s);
        if (state) layer_end(c, 0, 0, 0, cola(255, 214, 110, 0.85f), 16);
        else layer_end(c, 0.7f, 4, 6, (Col){0, 0, 0, 0}, 0);
    }
    dots_line(x + 8, ny, x + w - 8, ny, 2.6f, cola(255, 247, 226, 0.7f));
    Canvas *ic = layer_begin();
    if (ic) { icon(g, x + w / 2, y + 12 + 22, CREAM); layer_end(ic, 0.55f, 1.5f, 2, (Col){0, 0, 0, 0}, 0); }
    char title[32]; upper(t->title, title, sizeof title);
    label(title, kern(poster(g == 3 ? 17 : 19), 1.2f), CREAM, box(x + 4, y + 62, w - 8, 24), CENTRE, 1);
    char sum[128], sum2[160]; summary(t->page, sum, sizeof sum);
    /* (" × " never breaks: "64 × 64" stays on one line) */
    char *o = sum2; for (const char *p = sum; *p && o < sum2 + sizeof sum2 - 8;) {
        if (!strncmp(p, " \xc3\x97 ", 4)) { memcpy(o, "\xc2\xa0\xc3\x97\xc2\xa0", 6); o += 6; p += 4; } else *o++ = *p++;
    }
    *o = 0;
    wrapped(sum2, print_f(9.5f), cola(255, 247, 226, 0.92f), box(x + 7, y + 98, w - 14, 32), CENTRE);
    if (state == 2) stroke(inset(r, -3), 11, 2, cola(255, 247, 226, 0.9f));
}
static void draw_tile(int k, int lit) {
    const TpRow *row = L.spec->tiles[k];
    Box r = tile_box(k);
    plate(r, 10, lit);
    char t[32]; upper(row->title, t, sizeof t);
    label(t, kern(caps(10.5f), 2.8f), GOLD, box(r.x + 18, r.y + 12, r.w - 80, 16), LEFT, 1);
    int cur = index_of(row);
    for (int j = 0; j < row->n; j++) circle(r.x + r.w - 18 - (row->n - 1 - j) * 9, r.y + 16, 2.75f, j == cur ? GOLD : with_alpha(GOLD, 0.28f));
    label(row->choices[cur].label, kern(poster(25), 0.8f), CREAM, box(r.x + 18, r.y + 28, r.w - 36, 30), LEFT, 1);
    label(row->choices[cur].note, kern(plain(11.5f), 0.1f), MUTED, box(r.x + 18, r.y + 58, r.w - 28, 16), LEFT, 1);
    if (lit) {
        float cy = r.y + r.h / 2 + 4, a[6] = {r.x + 9 + 3, cy - 6, r.x + 9 - 3, cy, r.x + 9 + 3, cy + 6},
              b[6] = {r.x + r.w - 12 - 3, cy - 6, r.x + r.w - 12 + 3, cy, r.x + r.w - 12 - 3, cy + 6};
        polyline(a, 3, 2, 0, GOLD); polyline(b, 3, 2, 0, GOLD);
    }
}
static void draw_button(Box r, const char *title, const char *sub, Col dot, int has_dot, int lit) {
    plate(r, 9, lit);
    if (sub) {
        label(title, kern(caps(13), 3.6f), lit ? GOLD : CREAM, box(r.x, r.y + 10, r.w, 20), CENTRE, 1);
        Face f = kern(plain(11.5f), 0.3f);
        float sw = width_of(f, sub) + (has_dot ? 12 : 0), sx = r.x + r.w / 2 - sw / 2;
        if (has_dot) { circle(sx + 3, r.y + 36, 3, dot); sx += 12; }
        label(sub, f, MUTED, box(sx, r.y + 31, sw + 4, 16), LEFT, 1);
    } else label(title, kern(caps(14), 4), lit ? GOLD : CREAM, inset(box(r.x, r.y, r.w, r.h), 0), CENTRE, 1);
}
static void draw_play(int lit, int focused) {
    Box r = PLAY;
    float nx = 790;
    Canvas *c = layer_begin();
    if (c) {
        rrect(r, 8, col(150, 16, 22));
        Col g1[3] = {col(236, 58, 52), col(186, 24, 32), col(120, 10, 20)}, g2[3] = {col(255, 92, 72), col(210, 34, 38), col(120, 10, 20)};
        float at[3] = {0, 0.55f, 1};
        grad(r, 8, lit ? g2 : g1, at, 3, -90);
        Canvas *m = cv_new(c->w, c->h);
        if (m) {
            cv_rrect(m, P(box(734, 588, 56, 64)), 0, cola(60, 0, 8, 0.35f));
            cv_grad_rrect(m, P(box(734, 588, 222, 28.8f)), 0, (Col[2]){WHITE(0.22f), WHITE(0)}, (float[2]){0, 1}, 2, 90);
            Canvas *mask = cv_new(c->w, c->h);
            if (mask) {
                cv_rrect(mask, P(r), 8 * L.s, col(255, 255, 255));
                for (size_t i = 0; i < (size_t)m->w * m->h; i++) { unsigned a = mask->px[i * 4 + 3]; for (int ch = 0; ch < 4; ch++) m->px[i * 4 + ch] = (uint8_t)(m->px[i * 4 + ch] * a / 255); }
                cv_free(mask);
            }
            cv_over(c, m, 0, 0, 1);
            cv_free(m);
        }
        stroke(inset(r, 5), 5, 1.5f, cola(255, 214, 110, 0.9f));
        cv_erase_circle(c, nx * L.s, r.y * L.s, 8 * L.s);
        cv_erase_circle(c, nx * L.s, (r.y + r.h) * L.s, 8 * L.s);
        if (lit) layer_end(c, 0, 0, 0, cola(255, 120, 90, 0.9f), 18);
        else layer_end(c, 0.8f, 5, 14, (Col){0, 0, 0, 0}, 0);
    }
    dots_line(nx, 597, nx, 643, 2.6f, cola(255, 230, 170, 0.85f));
    label("ADMIT", kern(print_f(10), 0.6f), col(255, 226, 150), box(738, 605, 48, 14), CENTRE, 0);
    label("ONE", kern(print_f(10), 1.4f), col(255, 226, 150), box(738, 621, 48, 14), CENTRE, 0);
    Face f = kern(poster(36), 6);
    float pw = width_of(f, "PLAY"), x = 873 - (pw + 26) / 2, cy = 620;
    Canvas *tri = layer_begin();
    if (tri) { float xy[6] = {x, cy - 11, x + 16, cy, x, cy + 11}; poly(xy, 3, col(255, 222, 110)); layer_end(tri, 0.8f, 2, 4, (Col){0, 0, 0, 0}, 0); }
    text_at(f, x + 26, cy - line_h(f) / 2 - 1, col(255, 222, 110), "PLAY", 0.8f, 2, 4);
    if (focused) stroke(inset(r, -4), 11, 2, cola(255, 247, 226, 0.9f));
}
/* The pennant arrows beside a row's value. */
static void arrow(Box b, int d, int lit) {
    float cx = b.x + b.w / 2, cy = b.y + b.h / 2, xy[6] = {cx + 10 * d, cy, cx - 8 * d, cy - 10, cx - 8 * d, cy + 10};
    Canvas *c = layer_begin();
    if (!c) return;
    poly(xy, 3, lit ? col(255, 236, 160) : col(236, 180, 64));
    polyline(xy, 3, 1.1f, 1, col(110, 50, 14));
    layer_end(c, 0.8f, 2, 3, (Col){0, 0, 0, 0}, 0);
}
static void draw_row(int k) {
    const TpRow *row = row_of(L.page, k);
    Box r = row_box(k);
    float top = row_top();
    int focused = L.focus == k, hover = L.hover == 100 + k || L.hover == 200 + k || L.hover == 400 + k;
    if (focused || hover) {
        Box h = box(68, r.y + 2, 864, pitch() - 4);
        rrect(h, 8, with_alpha(GOLD, focused ? 0.16f : 0.09f));
        if (focused) stroke(h, 8, 1, with_alpha(GOLD, 0.5f));
    }
    char t[64]; upper(row->title, t, sizeof t);
    label(t, kern(caps(13.5f), 2.6f), GOLD, box(86, r.y + top, 340, 30), LEFT, 1);
    char value[320]; const char *note = NULL;
    row_text(row, value, sizeof value, &note);
    label(value, kern(poster(22), 1), CREAM, box(540, r.y + top, 334, 30), CENTRE, 1);
    int link = row->kind == TP_ROW_PAGE || row->kind == TP_ROW_FILES;
    if (row->kind == TP_ROW_SLIDER) {
        Box tb = track_box(k);
        int i = index_of(row), n = row->n;
        float kx = tb.x + tb.w * (n > 1 ? (float)i / (n - 1) : 0);
        rrect(tb, 4.5f, col(34, 30, 64));
        if (kx > tb.x) grad2(box(tb.x, tb.y, kx - tb.x, tb.h), 4.5f, col(255, 228, 140), col(214, 150, 40), -90);
        stroke(tb, 4.5f, 1, with_alpha(GOLD, 0.6f));
        for (int j = 0; j < n; j++) { float tx = tb.x + tb.w * (n > 1 ? (float)j / (n - 1) : 0); rrect(box(tx - 0.5f, tb.y + 2, 1, 5), 0, cola(20, 16, 40, 0.8f)); }
        Canvas *c = layer_begin();
        if (c) {
            circle(kx, tb.y + tb.h / 2, 9, CREAM);
            cv_ring(L.cv, kx * L.s, (tb.y + tb.h / 2) * L.s, 8 * L.s, 2 * L.s, GOLD_DK);
            layer_end(c, 0.7f, 1.5f, 3, (Col){0, 0, 0, 0}, 0);
        }
    } else label(note, kern(plain(13), 0.3f), MUTED, box(470, r.y + (compact() ? 30 : 40), 474, 18), CENTRE, 1);
    if (!link) arrow(arrow_box(k, 0), -1, L.hover == 100 + k);
    arrow(arrow_box(k, 1), 1, L.hover == 200 + k || L.hover == 100 + k || L.hover == 400 + k);
    if (k < page_rows() - 1) {
        Col c[3] = {with_alpha(GOLD, 0), with_alpha(GOLD, 0.35f), with_alpha(GOLD, 0)}; float at[3] = {0, 0.5f, 1};
        grad(box(78, r.y + pitch() - 0.5f, 844, 1), 0, c, at, 3, 0);
    }
}
static void draw_board(void) {
    shadow(BOARD, 16, 0.85f, 8, 22);
    rrect(BOARD, 16, NIGHT(0.94f));
    stroke(inset(BOARD, 1), 15, 2, GOLD);
    stroke(inset(BOARD, 8), 10, 1, with_alpha(GOLD, 0.3f));
    grad(box(68, 52, 864, 56), 10, SIGN_GOLD, SIGN_GOLD_AT, 3, -90);
    grad2(box(73, 57, 854, 46), 6, col(214, 38, 46), col(150, 16, 28), -90);
    star(95, 80, 7, GOLD); star(905, 80, 7, GOLD);
    char t[112], a[48], b[48];                       /* (room for both titles and the dot between) */
    const TpPage *pg = &L.spec->pages[L.page];
    if (pg->parent >= 0) { upper(L.spec->pages[pg->parent].title, a, sizeof a); upper(pg->title, b, sizeof b); snprintf(t, sizeof t, "%s  ·  %s", a, b); }
    else upper(pg->title, t, sizeof t);
    label(t, kern(poster(30), 5), col(255, 226, 130), box(73, 57, 854, 46), CENTRE, 1);
    if (L.page == TP_PAGE_CONTROLS) {
        int n = L.spec->ncontrols; float step = fminf(31, (566 - 24 - 130) / (float)n);
        for (int i = 0; i < n; i++) {
            float y = 130 + step * i;
            if (i % 2 == 0) rrect(box(84, y - 2, 832, step), 5, with_alpha(GOLD, 0.05f));
            char key[64]; upper(L.spec->controls[i][0], key, sizeof key);
            label(key, kern(caps(12.5f), 2), GOLD, box(104, y, 320, step - 4), LEFT, 1);
            label(L.spec->controls[i][1], kern(plain(16), 0.3f), CREAM, box(444, y, 462, step - 4), LEFT, 1);
        }
        return;
    }
    for (int k = 0; k < page_rows(); k++) draw_row(k);
    if (pg->footnote[0]) {
        float y0 = 126 + page_rows() * pitch() + 12;
        for (int i = 0; i < 2 && pg->footnote[i]; i++)
            label(pg->footnote[i], kern(ital(14.5f), 0.2f), MUTED, box(94, y0 + 22 * i, 812, 20), CENTRE, 1);
    }
}
static void draw_view(void) {
    memset(L.cv->px, 0, (size_t)L.cv->w * L.cv->h * 4);
    const TpAppSpec *s = L.spec;
    if (L.page < 0) {
        draw_sign();
        label(s->credits, kern(caps(10.5f), 3.4f), GOLD, box(44, 204, 584, 18), CENTRE, 1);
        wrapped(s->tagline, ital(16.5f), CREAM, box(44, 228, 584, 50), CENTRE);
        heading("ATTRACTIONS", 44, 294, 628);
        int lifted[8] = {0}, nl = 0;
        for (int g = 0; g < s->ntickets; g++) {
            int st = L.focus == g ? 2 : L.hover == 300 + g ? 1 : 0;
            if (st) lifted[nl++] = g; else draw_ticket(g, 0);
        }
        for (int i = 0; i < nl; i++) draw_ticket(lifted[i], L.focus == lifted[i] ? 2 : 1);
        draw_poster();
        heading("QUICK SETTINGS", 44, 468, 956);
        for (int k = 0; k < s->ntiles; k++) draw_tile(k, L.focus == 5 + k ? 2 : L.hover == 500 + k ? 1 : 0);
        const TpRow *files = NULL;
        for (int k = 0; k < s->pages[TP_PAGE_GAME].n; k++) if (row_of(TP_PAGE_GAME, k)->kind == TP_ROW_FILES) files = row_of(TP_PAGE_GAME, k);
        draw_button(SAVES, "SAVED PARKS", files ? files->value : "", (Col){0, 0, 0, 0}, 0, L.focus == 9 ? 2 : L.hover == HIT_SAVES ? 1 : 0);
        char sub[96]; Col dot = col(150, 150, 170);
        switch (L.update_state) {
        case UPD_CHECKING: snprintf(sub, sizeof sub, "Checking…"); break;
        case UPD_CURRENT: snprintf(sub, sizeof sub, "Up to date · %.7s", L.version[0] ? L.version : ""); dot = col(120, 220, 120); break;
        case UPD_AVAILABLE: snprintf(sub, sizeof sub, "New: %.7s · press", L.update_latest); dot = col(255, 190, 60); break;
        case UPD_ERROR: snprintf(sub, sizeof sub, "Couldn't Check"); dot = col(230, 90, 80); break;
        default: snprintf(sub, sizeof sub, "Not Available");
        }
        draw_button(UPDATES, "UPDATES", sub, dot, 1, L.focus == 10 ? 2 : L.hover == HIT_UPDATES ? 1 : 0);
        draw_button(QUIT, "QUIT", NULL, (Col){0, 0, 0, 0}, 0, L.focus == 11 ? 2 : L.hover == HIT_QUIT ? 1 : 0);
        draw_play(L.focus == 12 || L.hover == HIT_PLAY, L.focus == 12);
    } else {
        draw_board();
        draw_button(QUIT, "BACK", NULL, (Col){0, 0, 0, 0}, 0, L.hover == HIT_BACK ? 1 : 0);
        draw_play(L.hover == HIT_PLAY, 0);
    }
    label(L.page < 0 ? s->hint_main : s->hint_page, kern(plain(12), 0.3f), cola(210, 206, 230, 0.75f), box(44, 670, 640, 18), LEFT, 1);
    char v[96]; upper(L.version[0] ? L.version : "dev", v, sizeof v);
    char vv[128]; snprintf(vv, sizeof vv, "NIGHTLY %s", v);
    label(vv, kern(caps(9.5f), 3), with_alpha(GOLD, 0.6f), box(700, 673, 256, 14), RIGHT, 0);
    L.dirty = 0;
}

/* ---- art ---- */
static Image *art(const char *name) {
    if (G("TP_LAUNCHER_NO_ART")) return NULL;
    char p[1300];
    snprintf(p, sizeof p, "%sart/%s", L.res_dir, name);
    Image *m = img_load(p);
    if (!m) { snprintf(p, sizeof p, "%s/art/%s", L.data_root, name); m = img_load(p); }
    return m;
}
/* Only what is opaque enough: GOG's logo trimmed to its pixels with alpha > 24 (launcher.m trimmed()). */
static Image *trimmed(Image *m) {
    if (!m) return NULL;
    int x0 = m->w, y0 = m->h, x1 = -1, y1 = -1;
    for (int y = 0; y < m->h; y++) for (int x = 0; x < m->w; x++)
        if (m->px[((size_t)y * m->w + x) * 4 + 3] > 24) { if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; }
    if (x1 < x0) return m;
    Image *t = img_scaled(m, box((float)x0, (float)y0, (float)(x1 - x0 + 1), (float)(y1 - y0 + 1)), x1 - x0 + 1, y1 - y0 + 1);
    if (t) { img_free(m); return t; }
    return m;
}
/* The title screen's logo from banner.png: (58, 17, 206, 36) of it, 8x with hard pixel edges (launcher.m pixel_crop). */
static Image *banner_logo(void) {
    Image *b = art("banner.png");
    if (!b || b->w < 264 || b->h < 53) { img_free(b); return NULL; }
    Image *m = calloc(1, sizeof *m);
    if (m) { m->w = 206 * 8; m->h = 36 * 8; m->px = malloc((size_t)m->w * m->h * 4); }
    if (!m || !m->px) { if (m) free(m); img_free(b); return NULL; }
    for (int y = 0; y < m->h; y++) for (int x = 0; x < m->w; x++)
        memcpy(m->px + ((size_t)y * m->w + x) * 4, b->px + ((size_t)(17 + y / 8) * b->w + 58 + x / 8) * 4, 4);
    img_free(b);
    return m;
}
static void load_art(void) {
    L.logo = trimmed(art("logo.png"));
    if (!L.logo) L.logo = banner_logo();
    Image *k = art("keyart.jpg");
    if (k && k->w >= 1600) {
        /* the riders at the front of the car (launcher.m:591-592), then filled into the well, top kept */
        Box crop = box(k->w * 0.484f, 0, k->w * 0.25f, k->h * 0.978f);
        float kk = fmaxf(264 / crop.w, 326 / crop.h);
        Box from = box(crop.x + (crop.w - 264 / kk) / 2, crop.y, 264 / kk, 326 / kk);
        L.poster_img = img_scaled(k, from, (int)lroundf(264 * L.s), (int)lroundf(326 * L.s));
    }
    img_free(k);
}

/* ---- input (docs/parity/launcher.md 8) ---- */
static int in_box(Box b, float x, float y) { return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h; }
static int hit(float x, float y) {
    if (in_box(inset(PLAY, -4), x, y)) return HIT_PLAY;
    if (in_box(QUIT, x, y)) return L.page < 0 ? HIT_QUIT : HIT_BACK;
    if (L.page < 0) {
        if (in_box(SAVES, x, y)) return HIT_SAVES;
        if (in_box(UPDATES, x, y)) return HIT_UPDATES;
        for (int g = 0; g < L.spec->ntickets; g++) if (in_box(ticket_box(g), x, y)) return 300 + g;
        for (int k = 0; k < L.spec->ntiles; k++) if (in_box(tile_box(k), x, y)) return 500 + k;
        return -1;
    }
    if (L.page == TP_PAGE_CONTROLS) return -1;
    for (int k = 0; k < page_rows(); k++) {
        const TpRow *r = row_of(L.page, k);
        if (r->kind == TP_ROW_SLIDER) { Box t = track_box(k); if (in_box(box(t.x - 6, t.y - 8, t.w + 12, t.h + 16), x, y)) return 400 + k; }
        if (r->kind != TP_ROW_PAGE && r->kind != TP_ROW_FILES) { Box a = arrow_box(k, 0); if (in_box(inset(a, -6), x, y)) return 100 + k; }
        if (in_box(row_box(k), x, y)) return 200 + k;
    }
    return -1;
}
static void open_saves(void) {
    char p[1300]; snprintf(p, sizeof p, "%s/saves", L.data_root); tp_mkdir(p);
    snprintf(p, sizeof p, "%s/saves/save", L.data_root); tp_mkdir(p);
    char url[1400] = "file:///"; size_t o = strlen(url);
    for (const char *s = p; *s && o < sizeof url - 4; s++) {
        if (*s == '\\') url[o++] = '/';
        else if (*s == ' ') { memcpy(url + o, "%20", 3); o += 3; }
        else url[o++] = *s;
    }
    url[o] = 0;
    if (url[8] == '/') memmove(url + 7, url + 8, strlen(url + 8) + 1);   /* (Linux paths start with their own /) */
    SDL_OpenURL(url);
}
static void open_page(int page) { L.page = page; L.focus = -1; L.hover = -1; L.dirty = 1; }
static void step(int k, int d) {
    const TpRow *r = row_of(L.page, k);
    switch (r->kind) {
    case TP_ROW_PAGE: if (d > 0) open_page(r->page); return;
    case TP_ROW_FILES: if (d > 0) open_saves(); return;
    case TP_ROW_RIDETUNES: if (d < 0) settings_sets("RideTunes", ""); L.dirty = 1; return;   /* (→: the chooser, platform/sdl's own page: to come) */
    case TP_ROW_SOUNDBANK: if (d < 0) settings_sets("SoundBank", ""); L.dirty = 1; return;   /* (→: a file dialog: to come) */
    default: break;
    }
    if (!r->n) return;
    store(r, (index_of(r) + d + r->n) % r->n);
    L.dirty = 1;
}
static void step_tile(int k, int d) { const TpRow *r = L.spec->tiles[k]; store(r, (index_of(r) + d + r->n) % r->n); L.dirty = 1; }
static void slider_to(int k, float x) {
    const TpRow *r = row_of(L.page, k);
    Box t = track_box(k);
    int i = (int)lroundf(fminf(1, fmaxf(0, (x - t.x) / t.w)) * (r->n - 1));
    if (i != index_of(r)) { store(r, i); L.dirty = 1; }
}
static void finish(int play) { L.result = play; L.done = 1; }
static void press(int h, float x, int shift) {
    if (h == HIT_PLAY) { finish(1); return; }
    if (h == HIT_QUIT) { finish(0); return; }
    if (h == HIT_BACK) { int parent = L.spec->pages[L.page].parent; open_page(parent >= 0 ? parent : -1); L.focus = -1; return; }
    if (h == HIT_SAVES) { open_saves(); return; }
    if (h == HIT_UPDATES) return;                                   /* (the updater: to come, docs/PC-HANDOFF.md T4) */
    if (h >= 300 && h < 400) { open_page(L.spec->tickets[h - 300].page); return; }
    if (h >= 500 && h < 600) { int k = h - 500; L.focus = 5 + k; step_tile(k, shift || x < tile_box(k).x + 44 ? -1 : 1); return; }
    if (h >= 400 && h < 500) { L.focus = h - 400; L.drag_row = h - 400; slider_to(h - 400, x); L.dirty = 1; return; }
    if (h >= 100 && h < 200) { L.focus = h - 100; step(h - 100, -1); L.dirty = 1; return; }
    if (h >= 200 && h < 300) { L.focus = h - 200; step(h - 200, 1); L.dirty = 1; return; }
}
/* Main screen focus: 0-4 tickets, 5-8 tiles, 9 Saved Parks, 10 Updates, 11 Quit, 12 Play; rows of 5, 4, 4. */
static void focus_row_of(int f, int *start, int *count) {
    if (f < 5) { *start = 0; *count = 5; } else if (f < 9) { *start = 5; *count = 4; } else { *start = 9; *count = 4; }
}
static int hit_of_item(int f) { return f < 5 ? 300 + f : f < 9 ? 500 + f - 5 : f == 9 ? HIT_SAVES : f == 10 ? HIT_UPDATES : f == 11 ? HIT_QUIT : HIT_PLAY; }
static void key_down(SDL_Keysym ks) {
    SDL_Keycode k = ks.sym; int alt = ks.mod & KMOD_ALT;
    if (L.page >= 0) {
        int n = page_rows();
        switch (k) {
        case SDLK_RETURN: case SDLK_KP_ENTER: finish(1); return;
        case SDLK_ESCAPE: press(HIT_BACK, 0, 0); return;
        case SDLK_UP: if (n) L.focus = L.focus < 0 ? n - 1 : (L.focus + n - 1) % n; L.dirty = 1; return;
        case SDLK_DOWN: if (n) L.focus = L.focus < 0 ? 0 : (L.focus + 1) % n; L.dirty = 1; return;
        case SDLK_LEFT: if (L.focus >= 0 && L.focus < n) step(L.focus, -1); return;
        case SDLK_RIGHT: case SDLK_SPACE: if (L.focus >= 0 && L.focus < n) step(L.focus, 1); return;
        default: return;
        }
    }
    int f = L.focus, start, count;
    switch (k) {
    case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE: if (f < 0) finish(1); else press(hit_of_item(f), 1e9f, 0); return;
    case SDLK_ESCAPE: finish(0); return;
    case SDLK_RIGHT: case SDLK_LEFT: {
        int d = k == SDLK_RIGHT ? 1 : -1;
        if (alt && f >= 5 && f < 9) { step_tile(f - 5, d); return; }
        if (f < 0) L.focus = d > 0 ? 0 : 12;
        else { focus_row_of(f, &start, &count); L.focus = start + (f - start + d + count) % count; }
        L.dirty = 1; return;
    }
    case SDLK_DOWN: case SDLK_UP: {
        int d = k == SDLK_DOWN ? 1 : -1;
        if (f < 0) { L.focus = d > 0 ? 0 : 12; L.dirty = 1; return; }
        focus_row_of(f, &start, &count);
        int row = start == 0 ? 0 : start == 5 ? 1 : 2, nrow = (row + d + 3) % 3, ns = nrow == 0 ? 0 : nrow == 1 ? 5 : 9, nc = nrow == 0 ? 5 : 4;
        int rel = f - start, idx = rel * nc / count; if (idx > nc - 1) idx = nc - 1;
        L.focus = ns + idx; L.dirty = 1; return;
    }
    default: return;
    }
}

/* ---- the window ---- */
static void setup_fonts(void) {
    static const char *names[5] = {"FiraSansExtraCondensed-Black.ttf", "Poppins-Bold.ttf", "Arvo-Bold.ttf", "AlegreyaSans-Regular.ttf", "AlegreyaSans-Italic.ttf"};
    Font **slots[5] = {&L.poster, &L.caps, &L.print, &L.plain, &L.ital};
    for (int i = 0; i < 5; i++) {
        char p[1300]; snprintf(p, sizeof p, "%sfonts/%s", L.res_dir, names[i]);
        *slots[i] = font_load(p);
        if (!*slots[i]) fprintf(stderr, "launcher: no font %s\n", p);
    }
    for (int i = 0; i < 5; i++) if (i != 3) font_set_fallback(*slots[i], L.plain);   /* (Alegreya Sans has every arrow) */
}
static int open_page_named(const char *name) {
    for (int p = 0; p < TP_NPAGES; p++) if (!SDL_strcasecmp(L.spec->pages[p].title, name)) return p;
    return -1;
}
/* The backdrop (launcher.m:594-612): the player's park (<data>/art/your-park.png, saved at quit), else GOG's park
 * screenshots (<data>/art/park-shot-*.png, chosen at install time) composed in two rows and shown soft; *soft says which. */
static int by_name(const void *a, const void *b) { return strcmp(*(const char *const *)a, *(const char *const *)b); }
static Image *park_picture(int *soft) {
    *soft = 0;
    if (G("TP_LAUNCHER_NO_ART")) return NULL;
    char p[1300];
    if (G("TP_LAUNCHER_PARK")) return img_load(G("TP_LAUNCHER_PARK"));
    snprintf(p, sizeof p, "%s/art/your-park.png", L.data_root);
    Image *m = img_load(p);
    if (m && m->w >= 320) return m;
    img_free(m);
    /* GOG's shots, sorted by name, up to four */
    char dir[1100]; snprintf(dir, sizeof dir, "%s/art", L.data_root);
    char *names[16]; int n = 0;
    for (int i = 1; i <= 16 && n < 16; i++) {                     /* (park-shot-1.png .. : the install step numbers them) */
        snprintf(p, sizeof p, "%s/park-shot-%d.png", dir, i);
        struct stat st; if (stat(p, &st) == 0) names[n++] = SDL_strdup(p);
    }
    qsort(names, (size_t)n, sizeof *names, by_name);
    Image *shots[4]; int k = 0;
    for (int i = 0; i < n; i++) { if (k < 4 && (shots[k] = img_load(names[i]))) k++; SDL_free(names[i]); }
    if (!k) return NULL;
    Image *b = midway_park_from_shots((const Image *const *)shots, k);
    for (int i = 0; i < k; i++) img_free(shots[i]);
    *soft = b != NULL;
    return b;
}
/* What the window shows now: midway's back layers, the settings view, midway's front layers, into the renderer's target. */
static void compose(SDL_Renderer *ren, SDL_Texture *view, Midway *mid, double t) {
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    if (mid) midway_draw_back(mid, t);
    SDL_RenderCopy(ren, view, NULL, NULL);
    if (mid) midway_draw_front(mid, t, L.page >= 0);
}
/* The settings view into its texture, in the sprites' blend convention (midway.h: premultiplied, or straight where the
 * renderer refuses custom blend modes). */
static SDL_Texture *upload(Midway *mid, SDL_Texture *view) { return mid ? midway_upload(mid, L.cv->px, L.cv->w, L.cv->h, view) : view; }

int tp_launcher_run(const char *data_root, const char *res_dir, const char *version) {
    memset(&L, 0, sizeof L);
    L.spec = tp_app_spec();
    snprintf(L.data_root, sizeof L.data_root, "%s", data_root);
    snprintf(L.res_dir, sizeof L.res_dir, "%s", res_dir);
    snprintf(L.version, sizeof L.version, "%s", version ? version : "");
    L.page = -1; L.focus = -1; L.hover = -1; L.drag_row = -1; L.dirty = 1;
    const char *shot = G("TP_LAUNCHER_SHOT");
    double t0 = G("TP_LAUNCHER_TIME") ? atof(G("TP_LAUNCHER_TIME")) : MIDWAY_T0;
    if (G("TP_LAUNCHER_OPEN")) { int p = open_page_named(G("TP_LAUNCHER_OPEN")); if (p >= 0) L.page = p; }
    if (G("TP_LAUNCHER_FOCUS")) L.focus = atoi(G("TP_LAUNCHER_FOCUS"));
    if (G("TP_LAUNCHER_HOVER")) L.hover = atoi(G("TP_LAUNCHER_HOVER"));

    /* the window: 1000 x 700 points; on Windows SDL counts pixels (per-monitor DPI awareness), so it is sized by the DPI */
    float dpi_k = 1;
#ifdef _WIN32
    float ddpi, hdpi, vdpi;
    if (!SDL_GetDisplayDPI(0, &ddpi, &hdpi, &vdpi) && hdpi > 0) dpi_k = hdpi / 96.f;
#endif
    int ww = shot ? 1000 : (int)lroundf(1000 * dpi_k), wh = shot ? 700 : (int)lroundf(700 * dpi_k);
    SDL_Window *win = SDL_CreateWindow("Theme Park", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, ww, wh,
                                       SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_HIDDEN);
    if (!win) { fprintf(stderr, "launcher: %s\n", SDL_GetError()); return 1; }
    { char icon[1300]; snprintf(icon, sizeof icon, "%sicon.bmp", res_dir); SDL_Surface *s = SDL_LoadBMP(icon); if (s) { SDL_SetWindowIcon(win, s); SDL_FreeSurface(s); } }
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_TARGETTEXTURE);
    if (!ren) { fprintf(stderr, "launcher: %s\n", SDL_GetError()); SDL_DestroyWindow(win); return 1; }
    int pw, ph; SDL_GetRendererOutputSize(ren, &pw, &ph);
    L.s = shot ? 2.f : pw / 1000.f;
    int cw = (int)lroundf(1000 * L.s), ch = (int)lroundf(700 * L.s);
    L.cv = cv_new(cw, ch);
    setup_fonts();
    load_art();
    int soft;
    Image *park = park_picture(&soft);
    MidwayLayout lay = {SIGN, POSTER, PLAY, logo_box()};
    SDL_Texture *target = shot ? SDL_CreateTexture(ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_TARGET, cw, ch) : NULL;
    if (target) SDL_SetRenderTarget(ren, target);
    Midway *mid = midway_new(ren, L.s, soft ? NULL : park, lay);
    if (mid && soft) midway_set_backdrop(mid, park, 1);
    img_free(park);
    SDL_Texture *view = NULL;
    int still = midway_reduce_motion();

    if (shot) {
        draw_view(); view = upload(mid, view);
        compose(ren, view, mid, t0);
        uint8_t *px = malloc((size_t)cw * ch * 4);
        int rc = 1;
        if (px && !SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_ABGR8888, px, cw * 4)) {
            Canvas out = {cw, ch, px};
            for (size_t i = 0; i < (size_t)cw * ch; i++) px[i * 4 + 3] = 255;
            rc = cv_write_png(&out, shot);
        }
        fprintf(stderr, "launcher shot %s: %s\n", shot, rc ? "failed" : "written");
        free(px);
        L.result = 0;
    } else {
        SDL_ShowWindow(win); SDL_RaiseWindow(win);
        double start = (double)SDL_GetPerformanceCounter() / SDL_GetPerformanceFrequency();
        double profile = G("TP_LAUNCHER_PROFILE") ? atof(G("TP_LAUNCHER_PROFILE")) : 0, longest = 0, total = 0; int ticks = 0;
        while (!L.done) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                float x, y; int w, h; SDL_GetWindowSize(win, &w, &h);
                switch (e.type) {
                case SDL_QUIT: finish(0); break;
                case SDL_WINDOWEVENT: if (e.window.event == SDL_WINDOWEVENT_CLOSE) finish(0); break;
                case SDL_MOUSEMOTION:
                    x = e.motion.x * 1000.f / w; y = e.motion.y * 700.f / h;
                    if (L.drag_row >= 0) slider_to(L.drag_row, x);
                    { int hh = hit(x, y); if (hh != L.hover) { L.hover = hh; L.dirty = 1; } }
                    break;
                case SDL_MOUSEBUTTONDOWN:
                    x = e.button.x * 1000.f / w; y = e.button.y * 700.f / h;
                    if (e.button.button == SDL_BUTTON_LEFT) press(hit(x, y), x, (SDL_GetModState() & KMOD_SHIFT) != 0);
                    else if (e.button.button == SDL_BUTTON_RIGHT) { int hh = hit(x, y); if (hh >= 500 && hh < 600) { L.focus = 5 + hh - 500; step_tile(hh - 500, -1); } }
                    break;
                case SDL_MOUSEBUTTONUP: L.drag_row = -1; break;
                case SDL_KEYDOWN:
                    if ((e.key.keysym.sym == SDLK_q && (e.key.keysym.mod & KMOD_CTRL)) || (e.key.keysym.sym == SDLK_F4 && (e.key.keysym.mod & KMOD_ALT))) finish(0);
                    else key_down(e.key.keysym);
                    break;
                }
            }
            double now = (double)SDL_GetPerformanceCounter() / SDL_GetPerformanceFrequency(), t = still ? t0 : t0 + now - start;
            if (L.dirty) { draw_view(); view = upload(mid, view); }
            compose(ren, view, mid, t);
            SDL_RenderPresent(ren);
            double spent = (double)SDL_GetPerformanceCounter() / SDL_GetPerformanceFrequency() - now;
            total += spent; ticks++; if (spent > longest) longest = spent;
            if (profile > 0 && now - start >= profile) {
                fprintf(stderr, "launcher: %d ticks in %.1f s (%.0f/s), %.2f ms each, longest %.1f ms\n", ticks, now - start, ticks / (now - start),
                        1000 * total / ticks, 1000 * longest);
                finish(0);
            }
        }
    }
    midway_free(mid);
    if (target) SDL_DestroyTexture(target);
    if (view) SDL_DestroyTexture(view);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    cv_free(L.cv); L.cv = NULL;
    img_free(L.logo); img_free(L.poster_img);
    return L.result;
}
