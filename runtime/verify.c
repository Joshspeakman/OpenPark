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
void *tp_memory(void) {
    if (!G_MEM) {
        void *p = mmap(0, 1ull << 32, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0);
        G_MEM = p == MAP_FAILED ? calloc(1, 64u * 1024u * 1024u) : p;
        /* Tests: TP_GUARD faults any access past the extender's 64 MiB. */
        if (p != MAP_FAILED && getenv("TP_GUARD"))
            mprotect(G_MEM + (64u << 20), (1ull << 32) - (64u << 20), PROT_NONE);
    }
    return G_MEM;
}
void tp_dispatch_init(void);
void tp_reset(void) {
    tp_dispatch_init();
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
uint64_t tp_insns;
/* Optional record of dispatched block addresses (tools/spotcheck.py). */
uint32_t *tp_trace_buf; uint32_t tp_trace_cap, tp_trace_n;
uint32_t tp_watch_pc = 0xffffffffu;
volatile int tp_watch_hit;
int tp_run(uint32_t budget) {
    interrupt_number = 0;
    blocks_run = 0;
    if (setjmp(trap)) return -1;
    while (budget--) {
        if (state.tc == tp_watch_pc) tp_watch_hit = 1;
        if (tp_trace_buf && tp_trace_n < tp_trace_cap) tp_trace_buf[tp_trace_n++] = state.tc;
        if (!tp_dispatch(&state)) return -2;
        blocks_run++;
        if (interrupt_number) return (int)interrupt_number;
    }
    return 0;
}
