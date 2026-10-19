/* Visitor View (runtime/walk.c) on a synthetic park: no game data. The memory is laid out as the
 * game's (the addresses native_land.c reads), with made-up art: every cell's ground block is one
 * colour, a visitor, a tree and a ride with a rider. Checks:
 *   - the walk snapshot does not change game memory (except the game's camera, which it moves);
 *   - the floor: every cell's texels are that cell's colour (the shear undone);
 *   - the cell-to-screen formula the walker uses (camera centring) against the transcribed
 *     landscape loop (tp_native_landscape) for several view offsets;
 *   - a visitor's card stands at its place (cell + bytes 6 and 8) with its foot at the art's;
 *   - the renderer: the visitor ahead in the middle, the tree to the east on the right, the
 *     floor under the eye the right cell's colour;
 *   - a tree blocks the way; riding along finds the ride and its name.
 * Build and run: bash tests/check_walk.sh */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static uint8_t *mem;
uint8_t *tp_host_memory(void) { return mem; }
static uint32_t pal_rgba(int i) { return 0xff000000u | (uint32_t)((i * 7) & 255) << 16 | (uint32_t)(255 - i) << 8 | (uint32_t)i; }
void tp_host_palette_rgba(uint32_t pal[256]) { for (int i = 0; i < 256; i++) pal[i] = pal_rgba(i); }
uint32_t tp_hook_pcs[32];
uint32_t tp_map_w = 64, tp_map_ext = 64;   /* (the game's park size; bigmap.c) */
void tp_hooks_changed(void) {}

int tp_walk_start(double wx, double wy);
void tp_walk_frame(void);
void tp_walk_input(double fwd, double strafe, double turn, double look_yaw, double look_pitch, int run, double dt);
void tp_walk_command(int cmd);
void tp_walk_teleport(double x, double y, double yaw);
int tp_walk_render(uint32_t *out, int ow, int oh);
int tp_walk_ride(int *type, char *name, int n);
int tp_walk_floor_at(double x, double y);
int tp_walk_card_of(int idx, float *x, float *y, int *w, int *h, int *ax, int *ay);
void tp_walk_position(double *x, double *y);
int tp_native_landscape(const uint8_t *mem, int cx0, int cy0, int rows, uint8_t *buf, int pitch, int w, int h);

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
static void w8(uint32_t a, int v) { mem[a] = (uint8_t)v; }
static void w16(uint32_t a, int v) { int16_t x = (int16_t)v; memcpy(mem + a, &x, 2); }
static void w32(uint32_t a, uint32_t v) { memcpy(mem + a, &v, 4); }
static int r16(uint32_t a) { int16_t x; memcpy(&x, mem + a, 2); return x; }
static int colour(int x, int y) { return 1 + (x * 7 + y * 13) % 240; }

#define MAP 0x300000u
#define TABLE 0x310000u
#define BLOCKS 0x320000u
#define THINGS 0x400000u
#define FRAMES 0x500000u
#define ELEMS 0x510000u
#define SPRITES 0x520000u
#define SPRDATA 0x530000u
#define DECO 0x540000u
#define C_VISITOR 251
#define C_TREE 252

static uint32_t solid(uint32_t at, int w, int h, int c) {   /* an RLE sprite of w x h of colour c */
    for (int y = 0; y < h; y++) { w8(at++, w); for (int x = 0; x < w; x++) w8(at++, c); w8(at++, 0); }
    return at;
}
static void setup(int view_off) {
    mem = calloc(64u << 20, 1);
    w16(0x1d663a, 48); w16(0x1d6654, 32); w16(0x1d6624, view_off); w16(0x1de5f6, 1);
    w32(0x1d3b08, MAP); w32(0x1d3b04, MAP + 0x4000); w32(0x1d3b90, TABLE);
    w32(0x1d3b10, THINGS); w32(0x1d3b80, FRAMES); w32(0x1d3b7c, ELEMS); w32(0x1d3bb8, SPRITES); w32(0x1d3b38, DECO);
    w32(0x1ba2f8, 0x600000);
    /* ground blocks: entry k is a 24 x 16 parallelogram (each row half a pixel right of the one above) of colour k */
    uint32_t at = BLOCKS;
    for (int k = 1; k < 256; k++) {
        w32(TABLE + k * 6, at); w8(TABLE + k * 6 + 4, 64); w8(TABLE + k * 6 + 5, 32);
        for (int j = 0; j < 16; j++) {
            if (j / 2) w8(at++, -(j / 2));
            w8(at++, 24); for (int i = 0; i < 24; i++) w8(at++, k);
            w8(at++, 0);
        }
    }
    for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) w8(MAP + (y * 64 + x) * 4 + 2, colour(x, y) - 1);
    /* a visitor (thing 5, class 2) at cell 20,30, a quarter across and half down: 8 x 20 pixels, foot at its place */
    uint32_t t = THINGS + 5 * 0xc8;
    w8(t + 4, 2); w8(t + 6, 64); w8(t + 7, 20); w8(t + 8, 128); w8(t + 9, 30); w16(t + 10, 3); w8(t + 0xc, 2);
    w16(MAP + (30 * 64 + 20) * 4, 5);
    w16(FRAMES + 3 * 8, 1);                                       /* frame 3: element 1 */
    w16(ELEMS + 10, 6); w16(ELEMS + 12, -8); w16(ELEMS + 14, -40); /* sprite entry 1, 4 left, 20 up (game units) */
    w32(SPRITES + 6, SPRDATA); w8(SPRITES + 10, 16); w8(SPRITES + 11, 40);
    at = solid(SPRDATA, 8, 20, C_VISITOR);
    /* a tree (static scenery -1) at cell 22,28: 16 x 24 pixels */
    w16(MAP + (28 * 64 + 22) * 4, -1);
    w16(0x1b44c1 + 2, 2); w16(DECO + 4, 4);                       /* static kind 1 -> deco 2 -> frame 4 */
    w16(FRAMES + 4 * 8, 2); w16(ELEMS + 20, 12); w16(ELEMS + 22, 0); w16(ELEMS + 24, -48);
    w32(SPRITES + 12, at); w8(SPRITES + 16, 32); w8(SPRITES + 17, 48);
    at = solid(at, 16, 24, C_TREE);
    /* a ride (thing 7, class 4, type 3 "Teacups") at cell 40,40 with a rider (thing 8, class 2) */
    t = THINGS + 7 * 0xc8;
    w8(t + 4, 4); w8(t + 6, 128); w8(t + 7, 40); w8(t + 8, 128); w8(t + 9, 40); w8(t + 0xc, 2); w16(t + 0x16, 3 * 0xc8); w16(t + 0x47, 8);
    w16(MAP + (40 * 64 + 40) * 4, 7);
    w8(0x1cced0 + 3 * 0xc8 + 0xa8, 1);                            /* (not running: nothing drawn) */
    memcpy(mem + 0x1cced0 + 3 * 0xc8, "Teacups", 8);
    t = THINGS + 8 * 0xc8; w8(t + 4, 2); w8(t + 7, 41); w8(t + 9, 44);
}

int main(void) {
    setenv("TP_WALK_NOCAM", "", 0); unsetenv("TP_WALK_NOCAM");

    /* the cell-to-screen formula against the landscape loop (several view offsets) */
    for (int off = -24; off <= 40; off += 8) {
        setup(off);
        static uint8_t buf[320 * 300];
        int cx0 = 10, cy0 = 12, tw = 48, th = 32, n = 0, bad = 0;
        memset(buf, 0, sizeof buf);
        tp_native_landscape(mem, cx0, cy0, 16, buf, 320, 320, 300);
        double c0 = (0x280 - tw * ((0x280 - off) / tw)) >> 1;
        for (int Y = cy0; Y < cy0 + 14; Y++) for (int X = 0; X < 64; X++) {
            double u = 0.5, v = 0.5;
            double sx = 24 * (X - cx0 + u) + 8 * (Y - cy0 + v) + c0, sy = 16 * (Y - cy0 + v);
            if (sx < 2 || sx >= 318 || sy < 0 || sy >= 300) continue;
            int got = buf[(int)sy * 320 + (int)sx];
            if (!got && sx < (off > 0 ? off / 2 : 0) + 24) continue;   /* (the loop leaves out cells left of the view's offset) */
            n++; if (got != colour(X, Y)) { bad++; if (getenv("WALK_TEST_DEBUG")) printf("off %d cell %d,%d at %.0f,%.0f: %d\n", off, X, Y, sx, sy, buf[(int)sy * 320 + (int)sx]); }
        }
        CHECK(n > 100 && bad == 0, "view offset %d: %d of %d cell centres not where the formula puts them", off, bad, n);
        (void)th; free(mem);
    }

    setup(0);
    uint8_t *before = malloc(64u << 20); memcpy(before, mem, 64u << 20);
    CHECK(tp_walk_start(20.25, 34.5), "start");
    tp_walk_frame();
    long changed = 0;
    for (uint32_t a = 0; a < (64u << 20); a++) if (mem[a] != before[a] && !(a >= 0x1d667a && a < 0x1d667c) && !(a >= 0x1d6666 && a < 0x1d6668) && !(a >= 0x1d666e && a < 0x1d6670)) changed++;
    CHECK(changed == 0, "the snapshot changed %ld bytes of game memory", changed);
    {   /* the game's camera puts the walker in the middle of its view */
        int cx = r16(0x1d667a), cy = r16(0x1d6666);
        double sx = 24 * (20.25 - cx) + 8 * (34.5 - cy) + ((0x280 - 48 * (0x280 / 48)) >> 1), sy = 16 * (34.5 - cy);
        CHECK(fabs(sx - 160) <= 24 && fabs(sy - 89) <= 16, "camera %d,%d puts the walker at %.0f,%.0f", cx, cy, sx, sy);
    }

    /* floor */
    int fbad = 0;
    for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++)
        for (int k = 0; k < 9; k++) {
            double u = 0.12 + 0.38 * (k % 3), v = 0.12 + 0.38 * (k / 3);
            if (tp_walk_floor_at(x + u, y + v) != colour(x, y)) fbad++;
        }
    CHECK(fbad == 0, "floor: %d of %d samples not their cell's colour", fbad, 64 * 64 * 9);

    /* a cell's ground changes (a path laid): the floor follows, in both of its buffers */
    for (int k = 0; k < 3; k++) {
        w8(MAP + (10 * 64 + 12) * 4 + 2, 200 + k);
        usleep(20000); tp_walk_frame();
        CHECK(tp_walk_floor_at(12.5, 10.5) == 201 + k && tp_walk_floor_at(13.5, 10.5) == colour(13, 10),
              "after change %d the floor shows %d (want %d), its neighbour %d", k, tp_walk_floor_at(12.5, 10.5), 201 + k, tp_walk_floor_at(13.5, 10.5));
    }
    w8(MAP + (10 * 64 + 12) * 4 + 2, colour(12, 10) - 1); usleep(20000); tp_walk_frame();
    memcpy(before + MAP, mem + MAP, 0x4000);

    /* the visitor's card */
    float cx, cy; int w, h, ax, ay;
    CHECK(tp_walk_card_of(5, &cx, &cy, &w, &h, &ax, &ay), "no card for the visitor");
    CHECK(fabs(cx - 20.25) < 1e-4 && fabs(cy - 30.5) < 1e-4, "visitor card at %.3f,%.3f (want 20.25,30.5)", cx, cy);
    CHECK(w == 8 && h == 20 && ax == 4 && ay == 20, "visitor card %dx%d foot %d,%d (want 8x20 foot 4,20)", w, h, ax, ay);
    CHECK(tp_walk_card_of(-1, &cx, &cy, &w, &h, &ax, &ay) && w == 16 && h == 24 && (int)cx == 22 && (int)cy == 28, "tree card %.2f,%.2f %dx%d", cx, cy, w, h);

    /* the view: looking north from 4 cells south of the visitor */
    tp_walk_teleport(20.25, 34.5, -M_PI / 2);
    tp_walk_input(0, 0, 0, 0, 0, 0, 0.016);
    static uint32_t img[320 * 200];
    CHECK(tp_walk_render(img, 320, 200), "render");
    double sv = 0, st = 0; int nv = 0, nt = 0, vy0 = 999, vy1 = -1;
    for (int y = 0; y < 200; y++) for (int x = 0; x < 320; x++) {
        if (img[y * 320 + x] == pal_rgba(C_VISITOR)) { sv += x; nv++; if (y < vy0) vy0 = y; if (y > vy1) vy1 = y; }
        if (img[y * 320 + x] == pal_rgba(C_TREE)) { st += x; nt++; }
    }
    CHECK(nv > 20 && fabs(sv / nv - 160) < 4, "visitor ahead: %d pixels, centre x %.1f", nv, nv ? sv / nv : -1);
    CHECK(nt > 20 && st / nt > 190, "tree to the north-east, right of the middle: %d pixels, centre x %.1f", nt, nt ? st / nt : -1);
    /* the eye is at a visitor's height: the visitor's top is a little above the horizon (row 100) */
    CHECK(vy0 < 100 && vy0 > 90 && vy1 > 100, "visitor rows %d..%d around the horizon", vy0, vy1);
    CHECK(img[199 * 320 + 160] == pal_rgba(colour(20, 32)), "floor under the view: %08x, want cell 20,32's %08x", img[199 * 320 + 160], pal_rgba(colour(20, 32)));
    {   FILE *f = fopen(getenv("WALK_TEST_PPM") ? getenv("WALK_TEST_PPM") : "/dev/null", "wb");
        if (f) { fprintf(f, "P6 320 200 255\n"); for (int i = 0; i < 320 * 200; i++) { uint32_t c = img[i]; fputc(c & 255, f); fputc((c >> 8) & 255, f); fputc((c >> 16) & 255, f); } fclose(f); } }

    /* the tree blocks the way north */
    tp_walk_teleport(22.5, 30.5, -M_PI / 2);
    for (int i = 0; i < 100; i++) tp_walk_input(1, 0, 0, 0, 0, 0, 0.05);
    double px, py; tp_walk_position(&px, &py);
    CHECK(py > 28.95 && py < 30, "walked into the tree's cell: %.2f,%.2f", px, py);

    /* riding along */
    tp_walk_teleport(40.5, 43, -M_PI / 2);
    tp_walk_command(3);
    usleep(20000); tp_walk_frame(); tp_walk_input(0, 0, 0, 0, 0, 0, 0.016);
    int type = -9; char name[40] = "";
    CHECK(tp_walk_ride(&type, name, sizeof name) && type == 3 && !strcmp(name, "Teacups"), "riding: type %d name '%s'", type, name);
    changed = 0;
    for (uint32_t a = 0; a < (64u << 20); a++) if (mem[a] != before[a] && !(a >= 0x1d667a && a < 0x1d667c) && !(a >= 0x1d6666 && a < 0x1d6668) && !(a >= 0x1d666e && a < 0x1d6670)) changed++;
    CHECK(changed == 0, "walking changed %ld bytes of game memory", changed);

    /* a 128x128 park: the walker, its walking and the game camera reach the far half (issue #1) */
    tp_map_w = tp_map_ext = 128;
    CHECK(tp_walk_start(100.5, 110.5), "start in a 128x128 park");
    usleep(20000); tp_walk_frame(); tp_walk_input(0, 0, 0, 0, 0, 0, 0.016);
    tp_walk_position(&px, &py);
    CHECK(fabs(px - 100.5) < 1e-6 && fabs(py - 110.5) < 1e-6, "128x128: dropped at 100.5,110.5, walker at %.2f,%.2f", px, py);
    CHECK(r16(0x1d667a) > 51 && r16(0x1d6666) > 51, "128x128: game camera %d,%d held to the 64x64 park's limits", r16(0x1d667a), r16(0x1d6666));
    tp_walk_teleport(126.5, 100.5, 0);
    for (int i = 0; i < 60; i++) tp_walk_input(1, 0, 0, 0, 0, 0, 0.05);
    tp_walk_position(&px, &py);
    CHECK(px > 126.5 && px <= 127.8 + 1e-9, "128x128: walking east from 126.5 ended at %.2f (want past it, at most 127.8)", px);
    tp_map_w = tp_map_ext = 64;

    free(before); free(mem);
    printf(fails ? "walk test: %d failed\n" : "walk test: all passed\n", fails);
    return fails != 0;
}
