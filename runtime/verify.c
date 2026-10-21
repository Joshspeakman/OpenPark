#include <string.h>
#include <stdatomic.h>
/* Development-only bounded native block host. No x86 interpreter or decoder. */
#include "rt.h"
#include <stdlib.h>
#include <setjmp.h>
#include <sys/mman.h>
#include <stdio.h>

uint8_t *G_MEM;
uint32_t tp_seg[6], tp_base[8192];
static Ctx state;
static jmp_buf trap;
static char error[256];
static uint32_t interrupt_number;
static uint32_t blocks_run;
uint32_t tp_blocks(void) { return blocks_run; }
static int (*io_callback)(int, unsigned, unsigned, unsigned);
void tp_io_callback(int (*fn)(int,unsigned,unsigned,unsigned)) { io_callback = fn; }
uint32_t tp_io(Ctx *c, int output, unsigned port, unsigned size, unsigned value) {
    int result = io_callback ? io_callback(output,port&65535,size,value) : -1;
    if (result < 0) {
        snprintf(error,sizeof error,"port I/O failure: %x",port);
        longjmp(trap,1);
    }
    return (uint32_t)result;
}
void tp_set_segment(unsigned n, unsigned selector) {
    selector &= 65535;
    if ((selector & 7) || (n == 3 && tp_base[selector >> 3])) {
        snprintf(error, sizeof error, "unsupported segment %u=%x", n, selector);
        longjmp(trap, 1);
    }
    tp_seg[n] = selector;
}
void rt_unhandled(Ctx *c, uint32_t pc, const char *why) {
    snprintf(error, sizeof error, "%08x: %s", pc, why);
    longjmp(trap, 1);
}
void tp_interrupt(Ctx *c, uint32_t number) { interrupt_number = number; }
void rt_bus_lock(void) {}
void rt_bus_unlock(void) {}
void rt_raise_cpu_exception(Ctx *c, uint32_t pc, uint32_t code) {
    rt_unhandled(c, pc, "CPU exception");
}
void rt_exception(Ctx *c, uint32_t pc, uint32_t code) {
    rt_unhandled(c, pc, "CPU exception");
}
void rt_div_fault(Ctx *c, uint32_t pc) { rt_unhandled(c, pc, "division fault"); }
void rt_cpuid(Ctx *c, uint32_t *a, uint32_t *b, uint32_t *x, uint32_t *d) {
    rt_unhandled(c, c->tc, "CPUID");
}
/* x87 80-bit load (same model as the pinned reference runtime's rt.c). */
double rt_f80_load(const uint8_t *M, uint32_t a) {
    uint64_t mant; uint16_t se; memcpy(&mant, M + a, 8); memcpy(&se, M + a + 8, 2);
    int e = se & 0x7fff; double s = (se & 0x8000) ? -1.0 : 1.0;
    if (e == 0 && mant == 0) return s * 0.0;
    if (e == 0x7fff) return (mant << 1) ? NAN : s * INFINITY;
    return s * ldexp((double)mant, e - 16383 - 63);
}
/* Guest memory is the first 64 MiB of a 4 GiB reservation: every 32-bit
 * address is mapped, so a stray access past the DOS extender's memory reads
 * zeros instead of crashing the app (pages are committed only when touched). */
/* x87 state/transcendental helpers: unused by the game; stop explicitly. */
void rt_frstor(Ctx *c, uint32_t a) { (void)a; rt_unhandled(c, c->tc, "x87 frstor"); }
void rt_fnsave(Ctx *c, uint32_t a) { (void)a; rt_unhandled(c, c->tc, "x87 fnsave"); }
void rt_fldenv(Ctx *c, uint32_t a) { (void)a; rt_unhandled(c, c->tc, "x87 fldenv"); }
void rt_fnstenv(Ctx *c, uint32_t a) { (void)a; rt_unhandled(c, c->tc, "x87 fnstenv"); }
void rt_fxam(Ctx *c) { rt_unhandled(c, c->tc, "x87 fxam"); }
void rt_fprem(Ctx *c, int ieee) { (void)ieee; rt_unhandled(c, c->tc, "x87 fprem"); }
void rt_f80_store(uint8_t *M, uint32_t a, double v) { (void)M; (void)a; (void)v; rt_unhandled(&state, state.tc, "x87 80-bit store"); }
double rt_x87_1(int op, double x) { (void)op; (void)x; rt_unhandled(&state, state.tc, "x87 transcendental"); }
double rt_x87_2(int op, double y, double x) { (void)op; (void)y; (void)x; rt_unhandled(&state, state.tc, "x87 transcendental"); }
void *tp_memory(void) {
    if (!G_MEM) {
        void *p = mmap(0, 1ull << 32, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0);
        /* (every 32-bit address must read: a smaller region in its place faulted far from the cause) */
        if (p == MAP_FAILED) { fprintf(stderr, "cannot reserve the 4 GiB guest address space (ulimit -v? vm.overcommit_memory=2?)\n"); exit(2); }
        G_MEM = p;
        /* Tests: TP_GUARD faults any access past the extender's 64 MiB. */
        if (p != MAP_FAILED && getenv("TP_GUARD"))
            mprotect(G_MEM + (64u << 20), (1ull << 32) - (64u << 20), PROT_NONE);
    }
    return G_MEM;
}
void tp_dispatch_init(void);
int tp_dispatch(Ctx *c);
/* The compiled image in use: MAIN.EXE's, or another linked with a prefix (INTRO.EXE). */
static int (*dispatch_fn)(Ctx *) = tp_dispatch;
static void (*dispatch_init_fn)(void) = tp_dispatch_init;
void tp_select_code(int (*dispatch)(Ctx *), void (*init)(void)) { dispatch_fn = dispatch; dispatch_init_fn = init; }
void tp_reset(void) {
    dispatch_init_fn();
    memset(&state, 0, sizeof state);
    state.fcw = 0x37f;
    memset(tp_seg, 0, sizeof tp_seg);
    memset(tp_base, 0, sizeof tp_base);
}
uint32_t tp_get(unsigned i) {
    if (i < 8) return ((uint32_t *)&state)[i];
    if (i == 8) return flags_get(&state.f);
    if (i == 9) return state.tc;
    return tp_seg[i - 10];
}
void tp_set(unsigned i, uint32_t value) {
    if (i < 8) ((uint32_t *)&state)[i] = value;
    else if (i == 8) flags_set(&state.f, value, 0xffffffffu);
    else if (i == 9) state.tc = value;
    else tp_seg[i - 10] = value;
}
void tp_descriptor(unsigned selector, unsigned base) { tp_base[selector >> 3] = base; }
const char *tp_error(void) { return error; }
int tp_dispatch(Ctx *c);
/* The host learns when execution reaches one address (the far-callback
 * epilogue) without changing block scheduling. */
uint64_t tp_insns, tp_rep;   /* executed x86 instructions; REP repetitions among them */
/* Development breakpoint (TP_BREAK=hex address): prints registers. */
uint32_t tp_break_pc = 0xffffffffu; int tp_break_hits;
uint32_t tp_break_list[32]; int tp_break_count[32], tp_break_n;
/* Optional record of dispatched block addresses (tools/spotcheck.py). */
uint32_t *tp_trace_buf; uint32_t tp_trace_cap, tp_trace_n;
/* Block coverage (development, TP_COVERAGE=1): one bit per guest address 0x100000..0x1fffff, set when a block
 * starts there; tp_coverage_dump ORs it into a file, so runs add up (the port audit, docs/reports). */
uint8_t *tp_cov;
void tp_coverage_dump(const char *path) {
    if (!tp_cov) return;
    static uint8_t old[0x20000]; FILE *f = fopen(path, "rb"); size_t n = f ? fread(old, 1, sizeof old, f) : 0; if (f) fclose(f);
    for (size_t i = 0; i < n; i++) tp_cov[i] |= old[i];
    f = fopen(path, "wb"); if (f) { fwrite(tp_cov, 1, 0x20000, f); fclose(f); }
}
uint32_t tp_watch_pc = 0xffffffffu;
volatile int tp_watch_hit;
/* The host's hooks: tp_run returns -3 on arriving at one of tp_hook_pcs (once
 * per arrival, tp_hook_which says which; resuming runs the block), e.g. the
 * native park view (view.c) around the game's per-frame render. */
uint32_t tp_hook_pcs[32];   /* 0..11 view.c, 12..31 qol.c; all 0xffffffff until set (tp_hooks_init) */
static int hook_lo = 0x7fffffff, hook_hi = -1;
__attribute__((constructor)) static void tp_hooks_init(void) { for (int h = 0; h < 32; h++) tp_hook_pcs[h] = 0xffffffffu; }   /* hooks lie in [lo, hi]: one compare in the common case */
static uint8_t hook_bits[4096];                   /* a hook may be at addresses with these low 12 bits */
/* tp_hook_pcs may be changed from any thread (the app's zoom, pan, poster, Visitor View); the table the game
 * thread tests on every block is only rebuilt by the game thread, at the start of a run, so it never sees it
 * half built (issue #2). Hooks are handled between runs, so a change made there applies at once. */
static _Atomic int hooks_dirty;
void tp_hooks_changed(void) { atomic_store_explicit(&hooks_dirty, 1, memory_order_release); }
static void hooks_rebuild(void) {
    hook_lo = 0x7fffffff; hook_hi = -1; memset(hook_bits, 0, sizeof hook_bits);
    for (int h = 0; h < 32; h++) if (tp_hook_pcs[h] != 0xffffffffu) {
        hook_bits[tp_hook_pcs[h] & 4095] = 1;
        if ((int)tp_hook_pcs[h] < hook_lo) hook_lo = (int)tp_hook_pcs[h];
        if ((int)tp_hook_pcs[h] > hook_hi) hook_hi = (int)tp_hook_pcs[h];
    }
}
int tp_hook_which;
int tp_hook_armed = 1;   /* the host clears it after handling a hook (its own guest calls re-arm it) */
int tp_run(uint32_t budget) {
    if (atomic_exchange_explicit(&hooks_dirty, 0, memory_order_acquire)) hooks_rebuild();
    interrupt_number = 0;
    blocks_run = 0;
    static int cam_watch = -1; if (cam_watch < 0) cam_watch = getenv("TP_CAM_WATCH") != NULL;   /* development: game code moving the camera */
    static uint16_t cam_x, cam_y; static int cam_logged;
    if (cam_watch) { memcpy(&cam_x, G_MEM + 0x1d667a, 2); memcpy(&cam_y, G_MEM + 0x1d6666, 2); }   /* (the host's own writes are not the game's) */
    if (setjmp(trap)) return -1;
    while (budget--) {
        int hooked = 0;
        if (hook_bits[state.tc & 4095] && (int)state.tc >= hook_lo && (int)state.tc <= hook_hi)
            for (int h = 0; h < 32; h++) if (state.tc == tp_hook_pcs[h]) { hooked = 1; tp_hook_which = h; }
        if (hooked) { if (tp_hook_armed) { tp_hook_armed = 0; return -3; } }
        else tp_hook_armed = 1;
        if (state.tc == tp_watch_pc) tp_watch_hit = 1;
        if (tp_break_n) for (int b = 0; b < tp_break_n; b++) if (state.tc == tp_break_list[b] && tp_break_count[b]++ < 3)
            fprintf(stderr, "hit %08x\n", state.tc);
        if (state.tc == tp_break_pc && tp_break_hits < 20) {
            tp_break_hits++;
            fprintf(stderr, "break %08x eax=%08x ebx=%08x ecx=%08x edx=%08x esi=%08x edi=%08x esp=%08x [esp]=%08x [esp+4]=%08x\n", state.tc,
                    state.eax, state.ebx, state.ecx, state.edx, state.esi, state.edi, state.esp, *(uint32_t *)(G_MEM + state.esp), *(uint32_t *)(G_MEM + state.esp + 4));
            const char *m = getenv("TP_BREAK_MEM");
            if (m) { uint32_t a = (uint32_t)strtoul(m, NULL, 16); fprintf(stderr, "  [%08x]:", a);
                     for (int k = 0; k < 32; k++) fprintf(stderr, " %02x", G_MEM[a + k]); fprintf(stderr, "\n"); }
        }
        if (tp_trace_buf && tp_trace_n < tp_trace_cap) tp_trace_buf[tp_trace_n++] = state.tc;
        if (tp_cov && state.tc - 0x100000u < 0x100000u) tp_cov[(state.tc - 0x100000u) >> 3] |= (uint8_t)(1u << (state.tc & 7));
        uint32_t ran_tc = state.tc;
        if (!dispatch_fn(&state)) return -2;
        blocks_run++;
        if (cam_watch) { uint16_t x, y; memcpy(&x, G_MEM + 0x1d667a, 2); memcpy(&y, G_MEM + 0x1d6666, 2);
            if ((x != cam_x || y != cam_y) && cam_logged++ < 2000)
            {   fprintf(stderr, "camera %d,%d -> %d,%d by block %06x, code addresses on the stack:", cam_x, cam_y, x, y, ran_tc);
                for (int k = 0; k < 64; k++) { uint32_t v = *(uint32_t *)(G_MEM + state.esp + 4 * k); if (v >= 0x100000 && v < 0x1a0000) fprintf(stderr, " %06x", v); }
                fprintf(stderr, "\n"); }
            cam_x = x; cam_y = y; }
        if (interrupt_number) return (int)interrupt_number;
    }
    return 0;
}
