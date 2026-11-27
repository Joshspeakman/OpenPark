/* Stand-ins for the recompiled game (tools/build_game.py's generated dispatch.c and its tables), so the app and the
 * runtime can be compiled and linked where there are no game files: the CI (tests/check_sdl.sh). Never run as a game:
 * the dispatchers find no code. Values are the generated ones for the 64x64 park (runtime/bigmap.c). */
#include <stdint.h>
struct Ctx;
uint32_t tp_map_w = 64, tp_map_shift = 6, tp_map_ext = 64, tp_map_bytes = 0x4000;
uint32_t tp_things_alloc = 1200, tp_things_n = 1200, tp_things_bytes = 0x3a980, tp_things_spare = 0x4e20;
uint32_t tp_stay_ticks = 4000, tp_stay_tired = 8000, tp_tune_max_people = 0x320, tp_menu_bar;
void tp_dispatch_init(void) {}
int tp_dispatch(struct Ctx *c) { (void)c; return -1; }
void intro_dispatch_init(void) {}
int intro_dispatch(struct Ctx *c) { (void)c; return -1; }
