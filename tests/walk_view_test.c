/* The drop point of the Visitor View: view.c's tp_view_point_to_world against the cell-to-screen
 * formula the landscape loop follows (checked in walk_test.c), on a synthetic memory: a map point
 * shown at (sx, sy) of the game's own 320x200 view must come back as that map point, for several
 * cameras and view offsets. No game data. Build and run: bash tests/check_walk.sh */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *mem;
uint8_t *tp_host_memory(void) { return mem; }
void tp_host_palette_rgba(uint32_t pal[256]) { for (int i = 0; i < 256; i++) pal[i] = 0xff000000u | (uint32_t)i; }
int tp_host_png_rgba(const char *p, const uint32_t *px, int w, int h) { (void)p; (void)px; (void)w; (void)h; return 0; }
void tp_host_mouse(double fx, double fy, uint32_t b) { (void)fx; (void)fy; (void)b; }
void tp_host_mouse_pos(int *x, int *y) { *x = *y = 0; }
int64_t tp_host_guest_call(uint32_t fn, const uint32_t *a, int n, uint32_t m) { (void)fn; (void)a; (void)n; (void)m; return -1; }
void tp_host_key(uint32_t k) { (void)k; }
void tp_host_key_raw(uint8_t s, int d) { (void)s; (void)d; }
uint32_t tp_get(unsigned i) { (void)i; return 0; }
uint32_t tp_hook_pcs[24]; int tp_hook_which; uint64_t tp_insns, tp_rep;
void tp_hooks_changed(void) {}
int tp_view_point_to_world(double fx, double fy, double *wx, double *wy);
void tp_view_geometry(int *sw, int *sh, int *wh);
void tp_view_enable(int main_game);

static void w16(uint32_t a, int v) { int16_t x = (int16_t)v; memcpy(mem + a, &x, 2); }
static void w32(uint32_t a, uint32_t v) { memcpy(mem + a, &v, 4); }

int main(void) {
    mem = calloc(64u << 20, 1);
    w16(0x1d663a, 48); w16(0x1d6654, 32); w16(0x1de5f6, 1); w32(0x1de5b8, 320); w32(0x1de5c0, 200);
    int sw, sh, wh; tp_view_enable(1); tp_view_geometry(&sw, &sh, &wh);
    int fails = 0, n = 0;
    for (int off = -16; off <= 40; off += 8) for (int cy = 0; cy <= 40; cy += 13) for (int cx = 4; cx <= 50; cx += 9) {
        w16(0x1d6624, off); w16(0x1d667a, cx); w16(0x1d6666, cy);
        double c0 = (0x280 - 48 * ((0x280 - off) / 48)) >> 1;
        for (double X = cx; X < cx + 12; X += 1.37) for (double Y = cy; Y < cy + 10; Y += 0.91) {
            double sx = 24 * (X - cx) + 8 * (Y - cy) + c0, sy = 16 * (Y - cy);
            if (sx < 0 || sx >= sw || sy < 0 || sy >= wh || X >= 64 || Y >= 64) continue;
            double wx, wy; n++;
            if (!tp_view_point_to_world(sx / sw, sy / sh, &wx, &wy) || fabs(wx - X) > 1e-6 || fabs(wy - Y) > 1e-6) {
                if (fails++ < 5) printf("FAIL off %d camera %d,%d: %.2f,%.2f at %.1f,%.1f came back as %.3f,%.3f\n", off, cx, cy, X, Y, sx, sy, wx, wy);
            }
        }
    }
    printf(fails ? "walk view test: %d of %d failed\n" : "walk view test: all %d passed\n", fails ? fails : n, n);
    free(mem);
    return fails != 0;
}
