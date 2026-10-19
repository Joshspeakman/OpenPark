/* Native DOS/DPMI platform host: C port of tools/dos.py, plus a real-time
 * mode for the app. Deterministic mode reproduces the Python host's scripted
 * scheduling exactly so the verified first-park frame and profile can be
 * checked against config/milestones.json. Unknown services stop with a
 * message; nothing succeeds silently. */
#include "host.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <pthread.h>
#include <sys/stat.h>
#include <zlib.h>
#include "sha256.h"

/* compiled-code runtime (runtime/verify.c) */
void *tp_memory(void);
void tp_reset(void);
uint32_t tp_get(unsigned i);
void tp_set(unsigned i, uint32_t v);
void tp_descriptor(unsigned selector, unsigned base);
int tp_run(uint32_t budget);
void tp_host_palette_rgba(uint32_t pal[256]);
void tp_view_hook(void);    /* view.c */
void tp_view_poll(void);    /* view.c: queued pointer events */
uint32_t tp_blocks(void);
const char *tp_error(void);
void tp_io_callback(int (*fn)(int, unsigned, unsigned, unsigned));
extern uint32_t tp_watch_pc;
extern volatile int tp_watch_hit;
extern uint64_t tp_insns;

enum { EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI, EFLAGS, EIP, CS, DS, ES, SS, FS, GS };
#define MEMORY (64u * 1024u * 1024u)
#define TP_EXTRAS_AREA 0x1c40000u   /* (qol.c) */
#define TP_EXTRAS_SIZE 0x10000u
#define GDT 0x1000u
#define PSP 0x2000u
#define ENV 0x3000u
#define G(r) tp_get(r)
#define S(r, v) tp_set(r, (uint32_t)(v))

static uint8_t *mem;
static int deterministic;
static char game_root[1024], overlay_root[1024];
static char status[512];
static int exit_code = -1, stopped;
static TpScript script;
static int key_at, click_at, block_click_at;

/* ---- arenas (dos.py Arena) ---- */
typedef struct { uint32_t ptr, len; } Span;
typedef struct { Span free[4096]; int nfree; Span used[8192]; int nused; uint32_t align; } Arena;
static Arena linear_arena, conventional;

static void arena_init(Arena *a, uint32_t start, uint32_t end, uint32_t align) {
    memset(a, 0, sizeof *a); a->free[0] = (Span){start, end - start}; a->nfree = 1; a->align = align;
}
static int used_index(Arena *a, uint32_t ptr) {
    for (int i = 0; i < a->nused; i++) if (a->used[i].ptr == ptr) return i;
    return -1;
}
static void used_set(Arena *a, uint32_t ptr, uint32_t len) {
    int i = used_index(a, ptr);
    if (i < 0) { i = a->nused++; }
    a->used[i] = (Span){ptr, len};
}
static uint32_t used_pop(Arena *a, uint32_t ptr) {
    int i = used_index(a, ptr); uint32_t n = a->used[i].len;
    a->used[i] = a->used[--a->nused];
    return n;
}
static uint32_t round_up(Arena *a, uint32_t n) { return (n + a->align - 1) & ~(a->align - 1); }
static int64_t arena_allocate(Arena *a, uint32_t size) {
    if (!size) return -1;
    size = round_up(a, size);
    for (int i = 0; i < a->nfree; i++) {
        if (a->free[i].len >= size) {
            uint32_t ptr = a->free[i].ptr;
            if (a->free[i].len > size) a->free[i] = (Span){ptr + size, a->free[i].len - size};
            else { memmove(&a->free[i], &a->free[i + 1], (a->nfree - i - 1) * sizeof(Span)); a->nfree--; }
            used_set(a, ptr, size);
            return ptr;
        }
    }
    return -1;
}
static int span_cmp(const void *x, const void *y) {
    uint32_t a = ((const Span *)x)->ptr, b = ((const Span *)y)->ptr; return a < b ? -1 : a > b;
}
static void arena_release(Arena *a, uint32_t ptr) {
    uint32_t size = used_pop(a, ptr);
    a->free[a->nfree++] = (Span){ptr, size};
    qsort(a->free, a->nfree, sizeof(Span), span_cmp);
    int n = 0;
    for (int i = 0; i < a->nfree; i++) {
        if (n && a->free[n - 1].ptr + a->free[n - 1].len == a->free[i].ptr) a->free[n - 1].len += a->free[i].len;
        else a->free[n++] = a->free[i];
    }
    a->nfree = n;
}
static uint32_t arena_largest(Arena *a) {
    uint32_t m = 0; for (int i = 0; i < a->nfree; i++) if (a->free[i].len > m) m = a->free[i].len; return m;
}
static int64_t arena_resize(Arena *a, uint32_t ptr, uint32_t size) {
    uint32_t old = a->used[used_index(a, ptr)].len;
    if (!size) return -1;
    size = round_up(a, size);
    if (size <= old) {
        used_set(a, ptr, size);
        if (size < old) { used_set(a, ptr + size, old - size); arena_release(a, ptr + size); }
        return ptr;
    }
    for (int i = 0; i < a->nfree; i++) {
        if (a->free[i].ptr == ptr + old && a->free[i].len >= size - old) {
            uint32_t left = a->free[i].len - (size - old);
            if (left) a->free[i] = (Span){ptr + size, left};
            else { memmove(&a->free[i], &a->free[i + 1], (a->nfree - i - 1) * sizeof(Span)); a->nfree--; }
            used_set(a, ptr, size);
            return ptr;
        }
    }
    int64_t r = arena_allocate(a, size);
    if (r >= 0) arena_release(a, ptr);
    return r;
}

/* ---- machine state ---- */
static uint32_t bases[4096 * 2];        /* selector -> base (selector index = sel) */
static uint8_t has_base[65536];
static uint32_t alloc_size[65536];      /* DOS selector -> size */
static uint8_t is_alloc_sel[65536];
static uint8_t code_sel[65536];
typedef struct { uint32_t ptr, size; } Lin;
static Lin linear_allocs[8192]; static int nlinear;
static uint32_t next_selector, heap;
static uint32_t free_selectors[4096]; static int nfree_selectors;
static uint32_t vec_sel[256], vec_pc[256]; static uint8_t vec_set[256];
static int handles_fd[20] = {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1}; static int handles_writable[20];
/* Larger maps (bigmap.c): a save holding a 128x128 park has a marker beside it (<save>.big). */
static char handles_path[20][1024]; static int handles_big[20], things_grow;
/* A park save's own record of what the port adds (issue #5): appended after the game's data when the file that
 * held the map is closed. The game reads exactly the sizes in its save list, so the trailer is ignored by it;
 * copied or renamed, the save still says it is 128x128 and keeps its Free Mode rules. The marker files beside a
 * save (.big, .free) stay for older saves and the launcher's room check; the trailer wins when present. */
typedef struct { uint16_t w; uint32_t things; int8_t rules; uint16_t months, ramp; } SaveTrailer;
#define TRAILER_N 20
static void save_trailer_write(const char *path, int w, uint32_t things) {
    int mo = 0, ra = 0, rules; { int tp_free_state(int *, int *); rules = tp_free_state(&mo, &ra); }
    uint8_t t[TRAILER_N] = {0};
    t[0] = (uint8_t)w; t[1] = (uint8_t)(w >> 8); memcpy(t + 2, &things, 4); t[6] = (uint8_t)rules;
    t[8] = (uint8_t)mo; t[9] = (uint8_t)(mo >> 8); t[10] = (uint8_t)ra; t[11] = (uint8_t)(ra >> 8);
    memcpy(t + 16, "TPX1", 4);
    int fd = open(path, O_WRONLY | O_APPEND); if (fd < 0) return;
    if (write(fd, t, TRAILER_N) != TRAILER_N) fprintf(stderr, "save: trailer not written to %s\n", path);
    close(fd);
}
static int save_trailer_read(int fd, SaveTrailer *tr) {
    struct stat st; uint8_t t[TRAILER_N];   /* (fstat, not lseek: the game's position in the file stays) */
    if (fstat(fd, &st)) return 0;
    off_t end = st.st_size;
    if (end < TRAILER_N || pread(fd, t, TRAILER_N, end - TRAILER_N) != TRAILER_N || memcmp(t + 16, "TPX1", 4)) return 0;
    tr->w = (uint16_t)(t[0] | t[1] << 8); memcpy(&tr->things, t + 2, 4); tr->rules = (int8_t)t[6];
    tr->months = (uint16_t)(t[8] | t[9] << 8); tr->ramp = (uint16_t)(t[10] | t[11] << 8);
    return (tr->w == 64 || tr->w == 128) && tr->rules >= 0 && tr->rules <= 2;
}
int tp_bigmap_alloc(void); void tp_bigmap_set(int w, int ext); extern uint32_t tp_map_w;
int tp_bigmap_things(int n); int tp_bigmap_things_room(void); extern uint32_t tp_things_n;   /* (its things pool: "128 n" in the marker; the save list's entry
                                                                                                   * gives the read its size) */
static uint64_t ticks;
static uint32_t video_mode;
static uint8_t port_val[65536]; static uint8_t port_known[65536];
static uint8_t palette[768]; static uint32_t dac_index;
static uint32_t poll_count, irq_count, time_calls, retrace_reads, int_calls;
static uint64_t blocks;
static int64_t release_due = -1;
static uint32_t pit_divisor = 65536; static int pit_low = -1;

/* mouse */
static struct { int x, y; uint32_t buttons; int visible; int has_cb; uint32_t cb_mask, cb_sel, cb_pc;
                int rx0, rx1, ry0, ry1; } mouse;
static int pending_mouse; static uint32_t pm_event; static int pm_dx, pm_dy;
static int in_callback;

/* input queues (real time) */
static pthread_mutex_t input_lock = PTHREAD_MUTEX_INITIALIZER;
static uint32_t keys[64]; static int nkeys;
static uint8_t raw_keys[64]; static int nraw;      /* make/break scancodes for an INT 9 handler */
static uint8_t port60;
static uint32_t shift_flags;
typedef struct { int x, y; uint32_t buttons; } MouseEv;
static MouseEv mouse_q[256]; static int mq_head, mq_tail;
static volatile int quit_requested;
static double rt_start, timer_next;

static double now_s(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

static void stop(const char *fmt, ...) __attribute__((format(printf, 1, 2), noreturn));
#include <stdarg.h>
#include <setjmp.h>
static jmp_buf stop_jb;
static void stop(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); vsnprintf(status, sizeof status, fmt, ap); va_end(ap);
    longjmp(stop_jb, 1);
}

static void mwrite(uint32_t a, const void *d, uint32_t n) {
    if ((uint64_t)a + n > MEMORY) stop("host write outside memory");
    memcpy(mem + a, d, n);
}
static void put16(uint32_t a, uint16_t v) { mwrite(a, &v, 2); }
static void put32(uint32_t a, uint32_t v) { mwrite(a, &v, 4); }
static uint32_t get32(uint32_t a) { uint32_t v; if ((uint64_t)a + 4 > MEMORY) stop("host read outside memory"); memcpy(&v, mem + a, 4); return v; }

static void descriptor(uint32_t sel, uint32_t base, uint8_t access) {
    uint32_t limit = 0xfffff; uint8_t flags = 0xc;
    uint8_t d[8] = { limit & 255, (limit >> 8) & 255, base & 255, (base >> 8) & 255, (base >> 16) & 255,
                     access | 1, ((limit >> 16) & 15) | (flags << 4), base >> 24 };
    mwrite(GDT + sel, d, 8);
}
static void set_base(uint32_t sel, uint32_t base) { bases[sel] = base; has_base[sel] = 1; tp_descriptor(sel, base); }

static void carry(uint32_t error) {
    S(EFLAGS, (G(EFLAGS) & ~1u) | (error ? 1 : 0));
    if (error) S(EAX, error);
}

/* ---- sound hardware: MPU-401 (UART mode) ----
 * The game's HMI MPU-401 driver writes MIDI bytes to 0x330. The app plays them
 * through macOS's General MIDI synthesizer (tp_audio_midi, weak here). */
__attribute__((weak)) void tp_audio_midi(uint8_t byte) { (void)byte; }
/* Silence the synth: on MPU-401 reset and when a program starts (the intro can
 * be stopped mid-note by Escape; the game would otherwise inherit its notes). */
__attribute__((weak)) void tp_audio_midi_reset(void) {}
static uint8_t mpu_queue[16]; static int mpu_n; static uint32_t midi_bytes;
static void mpu_ack(void) { if (mpu_n < 16) mpu_queue[mpu_n++] = 0xfe; }
static int mpu_in(unsigned port) {
    if (port == 0x331) return mpu_n ? 0x00 : 0x80;     /* bit 7 clear: data ready; bit 6 clear: can write */
    if (!mpu_n) return 0xfe;
    int v = mpu_queue[0]; memmove(mpu_queue, mpu_queue + 1, --mpu_n); return v;
}
static void mpu_out(unsigned port, unsigned value) {
    if (port == 0x331) {
        if (value == 0xff) tp_audio_midi_reset();
        if (value == 0xff || value == 0x3f) mpu_ack();
        return;
    }
    midi_bytes++;
    tp_audio_midi((uint8_t)value);
}

int sb16_port_in(unsigned port, unsigned *v);
int sb16_port_out(unsigned port, unsigned v);
int sb16_take_irq(void);
void sb16_render(float *out, int frames, double out_rate);
void sb16_reset(void);
static int audio_attached;
void tp_host_audio_attached(int on) { audio_attached = on; }
static int floating_port(unsigned port) {     /* ISA I/O with no device behind it */
    if (port >= 0x200 && port < 0x400) {
        if (getenv("TP_DEBUG")) fprintf(stderr, "port %04x: no device\n", port);
        return 1;
    }
    return 0;
}

/* The emulated clock: time as the game experiences it, from the instructions it executed at the governed
 * rate (as DOSBox's PIC time follows its cycles). The retrace, the timer interrupt and the DOS time follow
 * it, not the wall clock: so work the host does between the game's instructions (the park view's drawing)
 * does not shorten the game's waits, and the game paces itself the same whatever is drawn. The governor
 * keeps it in step with real time. */
extern uint64_t tp_insns;
static double gov_rate;
static double emu_t; static uint64_t emu_last; static int emu_on = -1;
volatile double tp_frame_paced_at;                       /* steady pacing: when a park frame was last released (view.c) */
double tp_dbg_run, tp_dbg_gov;                           /* (development: qol.c's tick gaps) */
static int frame_paced(void) { return tp_frame_paced_at && now_s() - tp_frame_paced_at < 0.1; }
static double emu_now(void) {
    if (emu_on < 0) emu_on = !getenv("TP_WALL_CLOCK");
    if (frame_paced()) { emu_t = now_s(); emu_last = tp_insns; return emu_t; }   /* (frames are paced: time is real time) */
    if (!emu_on || gov_rate <= 0) return now_s();
    if (!emu_t) { emu_t = now_s(); emu_last = tp_insns; }
    uint64_t i = tp_insns; if (i > emu_last) emu_t += (double)(i - emu_last) / gov_rate;
    emu_last = i;
    return emu_t;
}

/* ---- ports ---- */
static int vretrace(void) {
    if (deterministic) { ticks++; return ticks & 1 ? 8 : 0; }
    double t = emu_now() * 70.086;             /* VGA mode 13h refresh */
    return (t - (double)(uint64_t)t) > 0.9 ? 8 : 0;
}
static int port_in(unsigned port, unsigned size) {
    if (size != 1) stop("unsupported port input width at %x", port);
    if (port_known[port]) return port_val[port];
    if (port == 0x3da) { retrace_reads++; return vretrace(); }
    if (port == 0x330 || port == 0x331) return mpu_in(port);
    if (port == 0x60) return port60;                    /* keyboard controller data */
    if (port == 0x61) return port_val[0x61];
    if (port == 0x64) return 0x14;                      /* status: no data pending, system flag */
    { unsigned v; if (sb16_port_in(port, &v)) return v; }
    if (floating_port(port)) return 0xff;
    stop("unsupported IN %04x", port);
}
static void port_out(unsigned port, unsigned size, unsigned value) {
    if (size != 1) stop("unsupported port output width at %x", port);
    if (port == 0x21 || port == 0xa1 || port == 0x3c6) { port_val[port] = value; port_known[port] = 1; }
    else if (port == 0x43) { pit_low = -1; }
    else if (port == 0x40) {
        if (pit_low < 0) pit_low = value & 255;
        else { pit_divisor = pit_low | (value & 255) << 8; if (!pit_divisor) pit_divisor = 65536; pit_low = -1; }
    }
    else if ((port == 0x20 || port == 0xa0) && value == 0x20) {}
    else if (port == 0x330 || port == 0x331) mpu_out(port, value);
    else if (port == 0x61) port_val[0x61] = value;      /* keyboard acknowledge strobe */
    else if (port == 0x3c8) dac_index = value * 3;
    else if (port == 0x3c9) { palette[dac_index] = value & 63; dac_index = (dac_index + 1) % 768; }
    else if (sb16_port_out(port, value)) {}
    else if (floating_port(port)) {}
    else stop("unsupported OUT %04x=%x", port, value);
}
/* DOSBox charges every port access an I/O delay in cycles (CPU_CycleMax/1024 for
 * a read, /1365 for a write: 9 and 7 at GOG's 10000); the governor does the same. */
extern uint64_t tp_insns, tp_rep;
static uint64_t io_cost;
static double gov_rate;   /* the governor's instruction rate (below) */
static int io_cb(int output, unsigned port, unsigned size, unsigned value) {
    if (gov_rate > 0) { unsigned d = (unsigned)(gov_rate / 1000.0 / (output ? 1365 : 1024)); tp_insns += d; io_cost += d; }
    jmp_buf saved; memcpy(saved, stop_jb, sizeof saved);
    if (setjmp(stop_jb)) { memcpy(stop_jb, saved, sizeof saved); stopped = 1; return -1; }
    int r = 0;
    if (output) port_out(port, size, value); else r = port_in(port, size);
    memcpy(stop_jb, saved, sizeof saved);
    return r;
}

/* ---- interrupts into guest code ---- */
static int timer_installed(void) { return !(vec_sel[8] == 8 && vec_pc[8] == 0xf000); }
int tp_host_checkpoint(const char *path);
static void hw_interrupt(uint32_t vector) {
    uint32_t sp = G(ESP) - 12;
    put32(sp, G(EIP)); put32(sp + 4, G(CS)); put32(sp + 8, G(EFLAGS));
    S(ESP, sp); S(CS, vec_sel[vector]); S(EIP, vec_pc[vector]);
    S(EFLAGS, G(EFLAGS) & ~0x300u);
    /* Tests: checkpoints at interrupt entry let tools/spotcheck.py replay
     * the handlers (HMI driver code) against the x86 oracle. */
    static int every = -1, n, saved;
    if (every < 0) every = getenv("TP_CKPT_IRQ_EVERY") ? atoi(getenv("TP_CKPT_IRQ_EVERY")) : 0;
    if (every && (!getenv("TP_CKPT_VECTOR") || strtoul(getenv("TP_CKPT_VECTOR"), NULL, 16) == vector) && ++n % every == 0 && saved < 200) {
        char p[256]; snprintf(p, sizeof p, "%s/irq-%02x-%03d.ckpt", getenv("TP_CKPT_DIR") ? getenv("TP_CKPT_DIR") : ".", vector, saved++);
        tp_host_checkpoint(p);
    }
}
static void timer_irq(void) {
    if (!(G(EFLAGS) & 0x200) || !timer_installed()) return;
    hw_interrupt(8);
    irq_count++;
}
static int sb_irq_pending; static uint32_t sb_irqs_delivered;
/* IRQ 1 to a program's own INT 9 handler (the intro reads port 0x60). */
static int keyboard_irq(void) {
    if (!vec_set[9] || (vec_sel[9] == 8 && vec_pc[9] == 0xf000)) return 0;
    if (!(G(EFLAGS) & 0x200) || (port_val[0x21] & 0x02)) return 0;
    pthread_mutex_lock(&input_lock);
    int have = nraw > 0;
    if (have) { port60 = raw_keys[0]; memmove(raw_keys, raw_keys + 1, --nraw); }
    pthread_mutex_unlock(&input_lock);
    if (have) hw_interrupt(9);
    return have;
}
/* Sound Blaster IRQ 7 (vector 0x0f), honouring the PIC mask. */
static int sb_irq(void) {
    sb_irq_pending += sb16_take_irq();
    if (!sb_irq_pending || !(G(EFLAGS) & 0x200) || (port_val[0x21] & 0x80) || !vec_set[0x0f]) return 0;
    sb_irq_pending--;
    hw_interrupt(0x0f);
    return 1;
}

static void queue_mouse(int x, int y, uint32_t buttons) {
    uint32_t event = (x != mouse.x || y != mouse.y) ? 1 : 0;
    static const uint32_t bits[3][3] = {{1, 2, 4}, {2, 8, 16}, {4, 32, 64}};
    for (int i = 0; i < 3; i++)
        if ((mouse.buttons ^ buttons) & bits[i][0]) event |= (buttons & bits[i][0]) ? bits[i][1] : bits[i][2];
    int dx = x - mouse.x, dy = y - mouse.y;
    mouse.x = x; mouse.y = y; mouse.buttons = buttons;
    pending_mouse = 1; pm_event = event; pm_dx = dx; pm_dy = dy;
}

static void mouse_callback(void) {
    if (!pending_mouse) return;
    pending_mouse = 0;
    if (getenv("TP_MOUSE_DEBUG")) fprintf(stderr, "mouse event %#x buttons %u at %d,%d mask %#x%s\n", pm_event, mouse.buttons, mouse.x, mouse.y, mouse.cb_mask,
                                        !mouse.has_cb || !(pm_event & mouse.cb_mask) ? " DROPPED" : "");
    if (!mouse.has_cb || !(pm_event & mouse.cb_mask)) return;
    if (!has_base[mouse.cb_sel] || bases[mouse.cb_sel] != 0) stop("nonflat mouse callback selector");
    uint32_t saved[16] = {0xf010, G(CS), G(GS), G(FS), G(ES), G(DS), G(EDI), G(ESI), G(EBP), G(ESP), G(EBX), G(EDX),
                          G(ECX), G(EAX), G(EFLAGS), G(EIP)};
    uint32_t sp = G(ESP) - 64;
    mwrite(sp, saved, 64);
    S(EAX, pm_event); S(EBX, mouse.buttons); S(ECX, mouse.x); S(EDX, mouse.y);
    S(ESI, (uint32_t)pm_dx); S(EDI, (uint32_t)pm_dy); S(ESP, sp); S(CS, 8); S(EIP, mouse.cb_pc);
    S(EFLAGS, G(EFLAGS) & ~0x400u);
    if (!deterministic) { in_callback = 1; tp_watch_hit = 0; }
}

/* Real-time: deliver queued input and due timer ticks at a block boundary. */
static void debug_stats(void) {
    static double last; static uint32_t lirq, lpoll, ltime, lret, lint; static uint64_t lblocks;
    static int on = -1; if (on < 0) on = getenv("TP_DEBUG") != NULL;
    if (!on) return;
    double t = now_s(); if (t - last < 1.0) return;
    static uint64_t lins, lrep, lio;
    fprintf(stderr, "Mins/s=%.2f (rep %.2f io %.2f) time/s=%u 3da/s=%u ints/s=%u ", (tp_insns - lins) / 1e6, (tp_rep - lrep) / 1e6, (io_cost - lio) / 1e6,
            time_calls - ltime, retrace_reads - lret, int_calls - lint);
    lins = tp_insns; lrep = tp_rep; lio = io_cost;
    ltime = time_calls; lret = retrace_reads; lint = int_calls;
    { static uint32_t ls; float sb16_peak(void); fprintf(stderr, "sbirq/s=%u pend=%d peak=%.3f midi/s=%u ", sb_irqs_delivered - ls, sb_irq_pending, sb16_peak(), midi_bytes); ls = sb_irqs_delivered; midi_bytes = 0; }
    { double sb16_pump_stats(long *jumps, long *clipped); long jumps, clipped; double worst = sb16_pump_stats(&jumps, &clipped);
      double tp_host_card_hold_max(void);
      fprintf(stderr, "card: step max %.1f ms, over 5 ms x%ld, hold max %.1f ms, full-scale samples %ld ", worst * 1e3, jumps, tp_host_card_hold_max() * 1e3, clipped); }
    fprintf(stderr, "t=%.1f irq/s=%u polls/s=%u Mblocks/s=%.1f pit=%u vec8=%x:%x mode=%x eip=%08x keys=%d cb=%d\n",
            t - rt_start, irq_count - lirq, poll_count - lpoll, (blocks - lblocks) / 1e6, pit_divisor,
            vec_sel[8], vec_pc[8], video_mode, G(EIP), nkeys, in_callback);
    last = t; lirq = irq_count; lpoll = poll_count; lblocks = blocks;
}
/* The timer interrupt is more than a period overdue (view.c's pacing: a frame should not sleep then). */
int tp_host_timer_behind(void) {
    if (deterministic || !timer_installed()) return 0;
    return emu_now() - timer_next > pit_divisor / 1193182.0;
}
/* The sound card's clock (sb16_pump): the wall clock, but never more than a timer period past the last timer tick
 * delivered. The HMI driver mixes only a few milliseconds ahead of the DMA position, in its 240 Hz timer handler;
 * while this thread cannot deliver ticks (a pass or a native pass in the view's hook, the pacing wait, the
 * governor's sleep, a long file read) the card used to run on by the wall clock and, when the ticks resumed, had
 * read past what was mixed: stale or unmixed buffer data, heard as a crackle or a broken effect whenever a frame ran
 * long with a sound playing (#10). Now each delivered tick lets the card on by one period (card_allow), so it
 * advances in lockstep with the mixer whatever the clocks (emu_now is the wall clock while frames are paced and
 * the instruction count's time otherwise: holding by either clock alone let the card outrun the mixer in the menus
 * and while loading). It never runs ahead of real time, and after a stall longer than the ring's lead it goes on from now (sb16_skip_to): the stall is not replayed. The output ring's
 * lead (sb16_play) covers the hold. Deterministic runs: the wall clock, as before. */
static double card_allow, card_hold_max;   /* (card_hold_max: the longest the card waited for a tick, TP_DEBUG's card: line) */
static void card_tick(double period) {
    double t = now_s(), sb16_lead_seconds(void);
    if (card_allow && t - card_allow > card_hold_max) card_hold_max = t - card_allow;
    /* After a stall longer than the output ring's lead (or at the start): on from now, the stall not replayed. (A
     * shorter threshold than the lead let a stall run the ring dry and then replayed it, leaving the effects that
     * much later until the next dry-out.) */
    if (card_allow < t - sb16_lead_seconds()) { void sb16_skip_to(double t); card_allow = t; sb16_skip_to(t); }
    card_allow += period;                              /* (the card may play a period past this tick before its handler has mixed:
                                                        * safe because the driver mixes further ahead than one period) */
    if (card_allow > t + period) card_allow = t + period;
}
double tp_host_card_hold_max(void) { double h = card_hold_max; card_hold_max = 0; return h; }
double tp_host_card_clock(void) {
    double t = now_s();
    static int wall = -1; if (wall < 0) wall = getenv("TP_CARD_WALLCLOCK") != NULL;   /* (development: the old clock, for A/B) */
    if (wall || deterministic || !timer_installed() || card_allow <= 0) return t;
    return t < card_allow ? t : card_allow;
}
static void realtime_events(void) {
    debug_stats();
    tp_view_poll();
    if (in_callback && tp_watch_hit) in_callback = 0;
    { void sb16_pump(double t); sb16_pump(tp_host_card_clock()); }   /* the card's DMA, in small steps in the game's time (sb16.c) */
    if (!(G(EFLAGS) & 0x200)) return;
    double t = now_s(), te = emu_now();
    double period = pit_divisor / 1193182.0;
    if (te >= timer_next && timer_installed()) {
        if (te - timer_next > 0.25) timer_next = te;      /* do not replay a long stall */
        timer_next += period;
        card_tick(period);                               /* (the card may play on a period: tp_host_card_clock) */
        timer_irq();
    } else if (sb_irq()) { sb_irqs_delivered++; return; }
    else if (keyboard_irq()) return;
    if (!in_callback) {
        pthread_mutex_lock(&input_lock);
        if (!pending_mouse && mq_head != mq_tail) {
            MouseEv e = mouse_q[mq_head]; mq_head = (mq_head + 1) % 256;
            /* coalesce pure movement */
            while (mq_head != mq_tail && mouse_q[mq_head].buttons == e.buttons && e.buttons == mouse.buttons) {
                e = mouse_q[mq_head]; mq_head = (mq_head + 1) % 256;
            }
            queue_mouse(e.x, e.y, e.buttons);
        }
        pthread_mutex_unlock(&input_lock);
        if (pending_mouse && (G(EFLAGS) & 0x200)) mouse_callback();
    }
}

static void poll_keyboard(void) {
    poll_count++;
    if (!deterministic) return;
    while (key_at < script.nkeys && script.keys[key_at].at <= poll_count) keys[nkeys++] = script.keys[key_at++].key;
    while (click_at < script.nclicks && script.clicks[click_at].at <= poll_count) {
        queue_mouse(script.clicks[click_at].x, script.clicks[click_at].y, 1);
        release_due = blocks + 50000;
        click_at++;
    }
}
static void advance_blocks(uint32_t count) {
    blocks += count;
    if (!deterministic) return;
    if (release_due >= 0 && blocks >= (uint64_t)release_due && (G(EFLAGS) & 0x200)) {
        release_due = -1;
        queue_mouse(mouse.x, mouse.y, 0);
        mouse_callback();
    }
    if (block_click_at < script.nblock_clicks && blocks >= script.block_clicks[block_click_at].at && (G(EFLAGS) & 0x200)) {
        queue_mouse(script.block_clicks[block_click_at].x, script.block_clicks[block_click_at].y,
                    script.block_clicks[block_click_at].buttons ? script.block_clicks[block_click_at].buttons : 1);
        block_click_at++;
        release_due = blocks + 50000;
        mouse_callback();
    }
}

static int keys_available(void) {
    pthread_mutex_lock(&input_lock); int n = nkeys; pthread_mutex_unlock(&input_lock); return n;
}
static uint32_t key_peek(void) { pthread_mutex_lock(&input_lock); uint32_t k = keys[0]; pthread_mutex_unlock(&input_lock); return k; }
static uint32_t key_pop(void) {
    pthread_mutex_lock(&input_lock);
    uint32_t k = keys[0]; memmove(keys, keys + 1, (--nkeys) * sizeof keys[0]);
    pthread_mutex_unlock(&input_lock);
    return k;
}
static uint32_t wait_key(void) {
    if (deterministic && !nkeys) stop("waiting for keyboard input");
    while (!keys_available()) {
        if (quit_requested) stop("quit");
        usleep(2000);
    }
    return key_pop();
}

/* ---- files ---- */
static void cstring(uint32_t a, char *out, size_t n) {
    for (size_t i = 0; i < 4096; i++) {
        if (a + i >= MEMORY) break;
        uint8_t b = mem[a + i];
        if (i + 1 >= n) stop("DOS string too long");
        out[i] = (char)b;
        if (!b) return;
    }
    stop("unterminated DOS string");
}
static int is_dir(const char *p) { struct stat st; return !stat(p, &st) && S_ISDIR(st.st_mode); }
static int is_file(const char *p) { struct stat st; return !stat(p, &st) && S_ISREG(st.st_mode); }
static int exists(const char *p) { struct stat st; return !stat(p, &st); }

/* Split a DOS path into components (dos.py DOS.path). */
static int dos_parts(const char *name, char parts[16][64]) {
    char buf[512]; snprintf(buf, sizeof buf, "%s", name);
    for (char *p = buf; *p; p++) if (*p == '\\') *p = '/';
    char *s = buf;
    if (strlen(s) > 1 && s[1] == ':') s += 2;
    int n = 0;
    for (char *tok = strtok(s, "/"); tok; tok = strtok(NULL, "/")) {
        if (!*tok || !strcmp(tok, ".")) continue;
        if (!strcmp(tok, "..")) stop("unsafe DOS path: %s", name);
        if (n >= 16) stop("DOS path too deep");
        snprintf(parts[n++], 64, "%s", tok);
    }
    if (n && !strcasecmp(parts[0], "GAME")) { memmove(parts[0], parts[1], (n - 1) * 64); n--; }
    if (!n) stop("unsafe DOS path: %s", name);
    return n;
}
static void walk(const char *root, char parts[16][64], int n, char *out, size_t size) {
    snprintf(out, size, "%s", root);
    for (int i = 0; i < n; i++) {
        char match[256] = {0};
        DIR *d = is_dir(out) ? opendir(out) : NULL;
        if (d) {
            struct dirent *e;
            while ((e = readdir(d))) if (!strcasecmp(e->d_name, parts[i])) { snprintf(match, sizeof match, "%s", e->d_name); break; }
            closedir(d);
        }
        size_t len = strlen(out);
        snprintf(out + len, size - len, "/%s", *match ? match : parts[i]);
    }
}
/* Resolve: overlay first, then the game; otherwise a new path in the overlay. */
static int resolve(const char *name, char *out, size_t size) {
    char parts[16][64]; int n = dos_parts(name, parts);
    walk(overlay_root, parts, n, out, size);
    if (is_file(out)) return 1;
    walk(game_root, parts, n, out, size);
    if (is_file(out)) return 2;
    walk(overlay_root, parts, n, out, size);
    return 0;
}
static void mkparents(const char *path) {
    char buf[1024]; snprintf(buf, sizeof buf, "%s", path);
    for (char *p = buf + strlen(overlay_root) + 1; *p; p++)
        if (*p == '/') { *p = 0; mkdir(buf, 0755); *p = '/'; }
}
static void overlay_path(const char *name, char *out, size_t size) {
    char parts[16][64]; int n = dos_parts(name, parts);
    char tmp[1024];
    int where = resolve(name, tmp, sizeof tmp);
    if (where == 2) {          /* map the game's file to the same relative path in the overlay */
        snprintf(out, size, "%s%s", overlay_root, tmp + strlen(game_root));
    } else snprintf(out, size, "%s", tmp);
    (void)n;
    mkparents(out);
}
static int copy_file(const char *from, const char *to) {
    int in = open(from, O_RDONLY); if (in < 0) return -1;
    int out = open(to, O_WRONLY | O_CREAT | O_TRUNC, 0644); if (out < 0) { close(in); return -1; }
    char buf[65536]; ssize_t r;
    while ((r = read(in, buf, sizeof buf)) > 0) if (write(out, buf, r) != r) { r = -1; break; }
    close(in); close(out); return r < 0 ? -1 : 0;
}
static int available_handle(void) { for (int h = 5; h < 20; h++) if (handles_fd[h] < 0) return h; return -1; }
static uint32_t address(int seg, uint32_t offset) {
    uint32_t sel = G(seg);
    if (!has_base[sel]) stop("unknown selector %x", sel);
    return bases[sel] + offset;
}

static void allocate_dos(uint32_t paragraphs, int *ok, uint32_t *ptr_out, uint32_t *sel_out) {
    int64_t ptr = arena_allocate(&conventional, paragraphs * 16);
    *ok = 0;
    if (ptr < 0) { S(EBX, arena_largest(&conventional) / 16); carry(8); return; }
    uint32_t sel = nfree_selectors ? free_selectors[--nfree_selectors] : (next_selector += 8) - 8;
    if (sel >= 4096) stop("descriptor table exhausted");
    set_base(sel, (uint32_t)ptr);
    alloc_size[sel] = conventional.used[used_index(&conventional, (uint32_t)ptr)].len; is_alloc_sel[sel] = 1;
    descriptor(sel, (uint32_t)ptr, 0x92);
    memset(mem + ptr, 0, alloc_size[sel]);
    carry(0);
    *ok = 1; *ptr_out = (uint32_t)ptr; *sel_out = sel;
}
static int lin_index(uint32_t ptr) { for (int i = 0; i < nlinear; i++) if (linear_allocs[i].ptr == ptr) return i; return -1; }

static void dos_time(uint32_t a) {
    (void)a;
    if (deterministic) {
        ticks++;
        uint64_t t = ticks;
        S(ECX, (12 << 8) | ((t / 6000) % 60));
        S(EDX, (((t / 100) % 60) << 8) | (t % 100));
        return;
    }
    /* DOS reads the time from the BIOS tick count, so it steps at 18.2 Hz (DOSBox the same): the ride videos'
     * frame wait (0x16123d: 2 or 8 hundredths) lasts a whole tick or two. A clock exact to the hundredth played
     * them about three times too fast. */
    struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm; localtime_r(&ts.tv_sec, &tm);
    double day = tm.tm_hour * 3600.0 + tm.tm_min * 60.0 + tm.tm_sec + ts.tv_nsec / 1e9;
    double tick = 65536.0 / 1193180.0;
    uint32_t cs = (uint32_t)(floor(day / tick) * tick * 100.0);
    S(ECX, ((cs / 360000) << 8) | (cs / 6000 % 60));
    S(EDX, ((cs / 100 % 60) << 8) | (cs % 100));
}

/* CPU-speed governor. Theme Park's main loop is not timer-paced: screen
 * timeouts, the attract montage and the simulation advance per loop pass, so
 * the original ran at the speed of the PC. GOG's release pins DOSBox at
 * cycles=fixed 10000 (about ten million instructions a second); natively the
 * loop runs some sixty times faster. Hold the executed x86 instruction rate at
 * TP_CYCLES thousand per second (default 10000) by sleeping when ahead,
 * counting as DOSBox's normal core does: one per instruction, one per MOVS/
 * STOS/LODS repetition (generated code adds them) and an I/O delay per port
 * access (io_cb). REP repetitions are about half of a running park's count. */
extern uint64_t tp_insns;
static double gov_t0; static uint64_t gov_i0;

void tp_host_set_cycles(double per_ms) { gov_rate = per_ms * 1000.0; gov_t0 = 0; }
double tp_host_cycles(void) { return gov_rate / 1000.0; }
static void govern(void) {
    if (gov_rate <= 0) return;
    if (frame_paced()) { gov_t0 = 0; return; }           /* park frames are released at a steady rate instead (view.c) */
    double t = now_s();
    if (!gov_t0) { gov_t0 = t; gov_i0 = tp_insns; return; }
    double ahead = (double)(tp_insns - gov_i0) / gov_rate - (t - gov_t0);
    if (getenv("TP_GOV_DEBUG")) { static double l; static uint64_t li; static int resets; if (ahead < -0.1) resets++;
        if (t - l >= 1) { if (l) fprintf(stderr, "gov: %.2f M insns/s (target %.2f), ahead %.1f ms, resets %d\n", (tp_insns - li) / (t - l) / 1e6, gov_rate / 1e6, ahead * 1e3, resets); l = t; li = tp_insns; resets = 0; } }
    if (ahead < -0.1) { gov_t0 = t; gov_i0 = tp_insns; return; }   /* never burst to catch up */
    if (ahead > 0.001) {
        struct timespec ts = {0, (long)(ahead * 1e9)};
        double g0 = now_s(); nanosleep(&ts, NULL); tp_dbg_gov += now_s() - g0;
    }
}

/* ---- VESA 640x480x256 (mode 101h), the game's SVGA mode ([R] in the park) ----
 * What the game asks for, as GOG's DOSBox (svga_s3) answers it: controller
 * and mode information through DPMI 0300h (real-mode INT 10h 4F00h/4F01h, the
 * window granularity is read from the mode information), INT 10h 4F02h to set
 * the mode and 4F05h to move the 64 KiB window at A0000h over the 640x480
 * picture. The picture lives in svga_fb; the window is copied in and out of it
 * on each bank switch, and flushed before a frame is read. */
#define SVGA_W 640
#define SVGA_H 480
#define SVGA_BYTES (SVGA_W * SVGA_H)
static uint8_t svga_fb[SVGA_BYTES];
static int svga_bank;
static uint32_t bank_bytes(int bank) { uint32_t off = (uint32_t)bank * 65536u; return off >= SVGA_BYTES ? 0 : SVGA_BYTES - off < 65536 ? SVGA_BYTES - off : 65536; }
static void svga_flush(void) { memcpy(svga_fb + svga_bank * 65536u, mem + 0xa0000, bank_bytes(svga_bank)); }
static void svga_set_bank(int bank) {
    if (bank < 0 || bank > 15) return;
    svga_flush(); svga_bank = bank;
    memcpy(mem + 0xa0000, svga_fb + bank * 65536u, bank_bytes(bank));
}
static void vbe_controller_info(uint32_t at) {          /* 4F00h: VbeInfoBlock (VESA 1.2) */
    uint8_t b[512] = {0};
    memcpy(b, "VESA", 4); b[4] = 2; b[5] = 1;            /* version 1.2 */
    uint16_t seg = (uint16_t)(at >> 4), off = (uint16_t)(at & 15);
    uint16_t oem = off + 0x100, modes = off + 0x120;
    memcpy(b + 6, &oem, 2); memcpy(b + 8, &seg, 2);       /* OEM string far pointer */
    memcpy(b + 14, &modes, 2); memcpy(b + 16, &seg, 2);   /* mode list far pointer */
    uint16_t mem64k = 16; memcpy(b + 18, &mem64k, 2);     /* 1 MiB */
    memcpy(b + 0x100, "Theme Park native SVGA", 23);
    uint16_t list[2] = {0x101, 0xffff}; memcpy(b + 0x120, list, 4);
    mwrite(at, b, sizeof b);
}
static int vbe_mode_info(uint32_t at, uint32_t mode) {  /* 4F01h: ModeInfoBlock */
    if ((mode & 0x1ff) != 0x101) return 0;
    uint8_t b[256] = {0};
    uint16_t attr = 0x9b; memcpy(b, &attr, 2);            /* supported, colour, graphics, extended info */
    b[2] = 7; b[3] = 0;                                   /* window A: exists, readable, writable */
    uint16_t gran = 64, size = 64, seg = 0xa000, pitch = SVGA_W, w = SVGA_W, h = SVGA_H;
    memcpy(b + 4, &gran, 2); memcpy(b + 6, &size, 2); memcpy(b + 8, &seg, 2);
    memcpy(b + 16, &pitch, 2); memcpy(b + 18, &w, 2); memcpy(b + 20, &h, 2);
    b[22] = 8; b[23] = 16; b[24] = 1; b[25] = 8; b[26] = 1; b[27] = 4; b[28] = 0; b[29] = 0; b[30] = 1;
    mwrite(at, b, sizeof b);
    return 1;
}
static int vbe_set_mode(uint32_t bx) {
    if ((bx & 0x1ff) != 0x101) return 0;
    video_mode = 0x101; svga_bank = 0;
    if (!(bx & 0x8000)) { memset(svga_fb, 0, sizeof svga_fb); memset(mem + 0xa0000, 0, 65536); }
    return 1;
}
/* INT 10h AX=4Fxx, from protected mode (r: the caller's registers, written back). */
static void vbe_call(uint32_t *ax, uint32_t *bx, uint32_t *dx, uint32_t buf) {
    uint32_t f = *ax & 0xff;
    uint32_t ok = 0x004f, fail = 0x014f;
    if (f == 0x00) { vbe_controller_info(buf); *ax = ok; }
    else if (f == 0x01) *ax = vbe_mode_info(buf, *bx & 0xffff) ? ok : fail;   /* bx carries CX for 4F01h */
    else if (f == 0x02) *ax = vbe_set_mode(*bx) ? ok : fail;
    else if (f == 0x03) { *bx = video_mode == 0x101 ? 0x101 : video_mode; *ax = ok; }
    else if (f == 0x05) {
        if (((*bx >> 8) & 255) == 0) { if ((*bx & 255) == 0) svga_set_bank((int)(*dx & 0xffff)); *ax = ok; }
        else { *dx = (uint32_t)svga_bank; *ax = ok; }
    }
    else *ax = fail;
}

static void interrupt(uint32_t number) {
    int_calls++;
    static int trace_ints = -1; if (trace_ints < 0) trace_ints = getenv("TP_TRACE_INTS") ? atoi(getenv("TP_TRACE_INTS")) : 0;
    if (trace_ints && number != 0x16 && !(number == 0x21 && ((G(EAX) >> 8) & 255) == 0x2c)) {
        trace_ints--;
        fprintf(stderr, "int %02x eax=%08x ebx=%08x ecx=%08x edx=%08x at %08x\n", number, G(EAX), G(EBX), G(ECX), G(EDX), G(EIP));
    }
    uint32_t a = G(EAX), b = G(EBX), c = G(ECX), d = G(EDX);
    uint32_t ax = a & 65535, ah = (a >> 8) & 255;
    char name[512], path[1024];
    if (number == 0x21) {
        /* Handle functions take the handle in BX only (the upper half of EBX
         * can hold anything); counts and offsets are 32-bit under DOS/4GW. */
        if (ah == 0x3e || ah == 0x3f || ah == 0x40 || ah == 0x42 || ax == 0x4400) b &= 0xffff;
        if (ah == 0xb) { S(EAX, (a & ~255u) | (keys_available() ? 255 : 0)); return; }
        if (ah == 7 || ah == 8) { uint32_t k = wait_key(); S(EAX, (a & ~255u) | (k & 255)); return; }
        if (ah == 0x30) { S(EAX, 0x1606); S(EBX, 0); S(ECX, 0); return; }
        if (ax == 0xff00 && (d & 65535) == 0x78) { S(EAX, (a & ~255u) | 1); return; }
        if (ah == 0x2c) { time_calls++; dos_time(a); return; }
        if (ah == 0x2a) {
            if (deterministic) { S(EAX, (a & ~255u) | 5); S(ECX, 2026); S(EDX, 0xa10); return; }
            time_t t = time(NULL); struct tm tm; localtime_r(&t, &tm);
            S(EAX, (a & ~255u) | tm.tm_wday); S(ECX, tm.tm_year + 1900); S(EDX, ((tm.tm_mon + 1) << 8) | tm.tm_mday); return;
        }
        if (ah == 0x48) { int ok; uint32_t p, s; allocate_dos(b & 65535, &ok, &p, &s); if (ok) S(EAX, s); return; }
        if (ah == 0x49) {
            uint32_t sel = G(ES);
            if (!is_alloc_sel[sel]) { carry(9); return; }
            arena_release(&conventional, bases[sel]); is_alloc_sel[sel] = 0;
            free_selectors[nfree_selectors++] = sel; carry(0); return;
        }
        if (ah == 0x3d) {
            cstring(address(DS, d), name, sizeof name);
            int where = resolve(name, path, sizeof path);
            if (getenv("TP_DEBUG")) fprintf(stderr, "open %s mode %u -> %s\n", name, a & 255, where ? path : "(missing)");
            if (!where) { carry(2); return; }
            if (a & 3 && where == 2) {
                char target[1024]; snprintf(target, sizeof target, "%s%s", overlay_root, path + strlen(game_root));
                mkparents(target);
                if (copy_file(path, target)) stop("cannot copy %s to overlay", name);
                snprintf(path, sizeof path, "%s", target);
            }
            int h = available_handle();
            if (h < 0) { carry(4); return; }
            int fd = open(path, (a & 3) ? O_RDWR : O_RDONLY);
            if (fd < 0) { carry(5); return; }
            handles_fd[h] = fd; handles_writable[h] = (a & 3) != 0;
            snprintf(handles_path[h], sizeof handles_path[h], "%s", path); handles_big[h] = -1;
            S(EAX, h); carry(0); return;
        }
        if (ah == 0x3c) {
            cstring(address(DS, d), name, sizeof name);
            int h = available_handle();
            if (h < 0) { carry(4); return; }
            overlay_path(name, path, sizeof path);
            int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) stop("cannot create %s", path);
            handles_fd[h] = fd; handles_writable[h] = 1;
            snprintf(handles_path[h], sizeof handles_path[h], "%s", path); handles_big[h] = -1;
            S(EAX, h); carry(0); return;
        }
        if (ax == 0x4300) {
            cstring(address(DS, d), name, sizeof name);
            char parts[16][64]; int n = dos_parts(name, parts);
            walk(overlay_root, parts, n, path, sizeof path);
            if (!exists(path)) walk(game_root, parts, n, path, sizeof path);
            if (!exists(path)) { carry(2); return; }
            S(ECX, is_dir(path) ? 0x10 : 0x20); carry(0); return;
        }
        if (ah == 0x3e) {
            if (b >= 20 || handles_fd[b] < 0) { carry(6); return; }
            close(handles_fd[b]); handles_fd[b] = -1;
            if (handles_big[b] >= 0) {                    /* a park's map was written: mark (or unmark) a 128x128 one */
                save_trailer_write(handles_path[b], handles_big[b] ? 128 : 64, tp_things_n);   /* (in the save itself, issue #5) */
                char mk[1040]; snprintf(mk, sizeof mk, "%s.big", handles_path[b]);
                if (handles_big[b]) { FILE *f = fopen(mk, "w"); if (f) { fprintf(f, "128 %u\n", tp_things_n); fclose(f); } } else unlink(mk);
                { void tp_free_saved(const char *); tp_free_saved(handles_path[b]); }   /* (and its Free Mode rules: freemode.c) */
            }
            carry(0); return;
        }
        if (ah == 0x3f) {
            if (b >= 20 || handles_fd[b] < 0) { carry(6); return; }
            uint32_t at = address(DS, d);
            if ((uint64_t)at + c > MEMORY) stop("DOS read outside memory");
            { uint32_t mp; memcpy(&mp, mem + 0x1d3b08, 4);   /* a park's map: the layout of the park being read (bigmap.c) */
              if (mp && at == mp && (c == 0x4000 || c == 0x10000)) {
                  char mk[1040]; snprintf(mk, sizeof mk, "%s.big", handles_path[b]);
                  SaveTrailer tr; int has_tr = save_trailer_read(handles_fd[b], &tr);   /* (the save's own record wins over the marker files) */
                  int big = has_tr ? tr.w == 128 : exists(mk), want = big ? 0x10000 : 0x4000;
                  if (big && tp_bigmap_alloc() < 128) { fprintf(stderr, "bigmap: %s holds a 128x128 park (turn on the larger map setting)\n", handles_path[b]); carry(5); return; }
                  { void tp_bigmap_loaded(void); tp_bigmap_loaded(); void tp_free_loaded_rules(const char *, int, int, int);
                    tp_free_loaded_rules(handles_path[b], has_tr ? tr.rules : -1, has_tr ? tr.months : 0, has_tr ? tr.ramp : 0); }
                  if ((int)c != want || (int)tp_map_w != (big ? 128 : 64)) { tp_bigmap_set(big ? 128 : 64, big ? 128 : 64); c = (uint32_t)want; }
                  if (big) {                              /* its things: as many as were saved (1200 before the pool grew) */
                      int w = 0, n = 1200;
                      if (has_tr) n = (int)tr.things;
                      else { FILE *f = fopen(mk, "r"); if (f) { if (fscanf(f, "%d %d", &w, &n) < 2) n = 1200; fclose(f); } }
                      if (n > tp_bigmap_things_room()) { fprintf(stderr, "bigmap: %s holds %d things, room for %d\n", handles_path[b], n, tp_bigmap_things_room()); carry(5); return; }
                      tp_bigmap_things(n); things_grow = 1;
                  }
              } }
            ssize_t got = 0, r;
            while ((uint32_t)got < c && (r = read(handles_fd[b], mem + at + got, c - got)) > 0) got += r;
            { void tp_sound_bank_read(const char *, long, uint8_t *, long);   /* (a sound bank: its samples' ends cleaned, sb16.c) */
              if (got > 0) tp_sound_bank_read(handles_path[b], (long)lseek(handles_fd[b], 0, SEEK_CUR) - got, mem + at, got); }
            if (things_grow) { uint32_t tp; memcpy(&tp, mem + 0x1d3b10, 4);   /* a 128x128 park's things read: the rest of the pool, empty, in play */
                if (tp && at == tp) { uint32_t room = (uint32_t)tp_bigmap_things_room() * 0xc8u;
                    if ((uint32_t)got < room) memset(mem + at + got, 0, room - (uint32_t)got);
                    tp_bigmap_things(tp_bigmap_things_room()); things_grow = 0; } }
            if (getenv("TP_DEBUG_READ")) { uint32_t mp; memcpy(&mp, mem + 0x1d3b08, 4);   /* (development: reads into the map) */
                if (mp && at < mp + 0x10000 && at + c > mp) fprintf(stderr, "read into the map: handle %u, +%#x, %u bytes (got %zd)\n", b, at - mp, c, got); }
            S(EAX, (uint32_t)got); carry(0); return;
        }
        if (ah == 0x42) {
            if (b >= 20 || handles_fd[b] < 0) { carry(6); return; }
            int32_t offset = (int32_t)(((c & 65535) << 16) | (d & 65535));
            int whence = (a & 255) == 0 ? SEEK_SET : (a & 255) == 1 ? SEEK_CUR : SEEK_END;
            off_t pos = lseek(handles_fd[b], offset, whence);
            if (pos < 0) stop("seek failed");
            S(EAX, pos & 65535); S(EDX, (uint32_t)pos >> 16); carry(0); return;
        }
        if (ax == 0x4400) {
            if (b < 5) { S(EDX, b == 0 ? 0x80d3 : 0x80d2); carry(0); return; }
            if (b < 20 && handles_fd[b] >= 0) { S(EDX, 0); carry(0); return; }
            carry(6); return;
        }
        if (ah == 0x35) {
            uint32_t n = a & 255;
            /* Unset vectors point at an IRET, so handlers can chain to them. */
            S(ES, vec_set[n] ? vec_sel[n] : 8); S(EBX, vec_set[n] ? vec_pc[n] : 0xf000); return;
        }
        if (ah == 0x25) { uint32_t n = a & 255; vec_sel[n] = G(DS); vec_pc[n] = d; vec_set[n] = 1; return; }
        if (ah == 0x40 && (b == 1 || b == 2)) {
            fwrite(mem + address(DS, d), 1, c, stderr);
            S(EAX, c); carry(0); return;
        }
        if (ah == 0x40) {
            if (b >= 20 || handles_fd[b] < 0) { carry(6); return; }
            if (!handles_writable[b]) { carry(5); return; }
            uint32_t count = 0;
            if (c) {
                { uint32_t mp; memcpy(&mp, mem + 0x1d3b08, 4);
                  if (mp && address(DS, d) == mp && (c == 0x4000 || c == 0x10000)) handles_big[b] = c == 0x10000; }
                ssize_t w = write(handles_fd[b], mem + address(DS, d), c);
                if (w < 0) stop("write failed: %s", strerror(errno));
                count = (uint32_t)w;
            } else {
                off_t pos = lseek(handles_fd[b], 0, SEEK_CUR);
                if (ftruncate(handles_fd[b], pos)) stop("truncate failed");
            }
            S(EAX, count); carry(0); return;
        }
        if (ah == 0x4c) { exit_code = a & 255; snprintf(status, sizeof status, "DOS exit %d", exit_code); longjmp(stop_jb, 2); }
    }
    if (number == 0x2f && ax == 0x1600) { S(EAX, a & ~255u); return; }   /* Windows enhanced mode: not present */
    if (number == 0x33) {
        if (ax == 0x2f) return;
        if (ax == 0x0f || ax == 0x1a || ax == 0x1d) return;   /* mickey ratio, sensitivity, display page: no effect here */
        if (ax == 0) { S(EAX, 0xffff); S(EBX, 3); return; }
        if (ax == 1 || ax == 2) { mouse.visible = ax == 1; return; }
        if (ax == 3) { S(EBX, mouse.buttons); S(ECX, mouse.x); S(EDX, mouse.y); return; }
        if (ax == 4) { mouse.x = c & 65535; mouse.y = d & 65535; return; }
        if (ax == 7) { mouse.rx0 = c & 65535; mouse.rx1 = d & 65535; return; }
        if (ax == 8) { mouse.ry0 = c & 65535; mouse.ry1 = d & 65535; return; }
        if (ax == 0xc) { mouse.has_cb = 1; mouse.cb_mask = c & 65535; mouse.cb_sel = G(ES); mouse.cb_pc = d; return; }
    }
    if (number == 0x10) {
        if (ah == 0x4f) {                         /* VESA BIOS extensions */
            uint32_t r_ax = ax, r_bx = (ax & 0xff) == 1 ? (c & 0xffff) : (b & 0xffff), r_dx = d & 0xffff;
            vbe_call(&r_ax, &r_bx, &r_dx, ((ax & 0xff) <= 1) ? address(ES, G(EDI)) : 0);
            S(EAX, (a & 0xffff0000u) | r_ax);
            if ((ax & 0xff) == 3) S(EBX, (b & 0xffff0000u) | r_bx);
            if ((ax & 0xff) == 5) S(EDX, (d & 0xffff0000u) | r_dx);
            return;
        }
        if (ah == 0xf) { S(EAX, (a & 0xffff0000u) | 0x5000 | (video_mode & 0x7f)); S(EBX, b & ~0xff00u); return; }
        if (ah == 0) {
            video_mode = a & 127;
            if (video_mode != 3 && video_mode != 0x13) stop("unsupported VGA mode %x", video_mode);
            if (!(a & 128)) memset(mem + 0xa0000, 0, 65536);
            return;
        }
    }
    if (number == 0x16) {
        if (ah == 2 || ah == 0x12) { S(EAX, (a & 0xffff0000u) | (deterministic ? 0 : shift_flags & 0xffff)); return; }
        if (ah == 1 || ah == 0x11) {
            poll_keyboard();
            int have = keys_available();
            S(EFLAGS, (G(EFLAGS) & ~0x40u) | (have ? 0 : 0x40));
            if (have) S(EAX, (a & 0xffff0000u) | key_peek());
            if (deterministic) { timer_irq(); mouse_callback(); }
            return;
        }
        if (ah == 0 || ah == 0x10) { uint32_t k = wait_key(); S(EAX, (a & 0xffff0000u) | k); return; }
    }
    if (number == 0x31) {
        /* HMI's timer shutdown sends INT 21h function 2508h to INT 31h; a DPMI
         * host answers an unknown function with CF=1, AX=8001h. */
        if ((ax & 0xff00) == 0x2500) { carry(0x8001); return; }
        /* Lock/unlock linear region or real-mode region: nothing pages here. */
        if (ax >= 0x600 && ax <= 0x603) { carry(0); return; }
        if (ax == 0x0000) {                      /* allocate CX descriptors (contiguous) */
            uint32_t n = c & 65535, first = next_selector;
            if (!n || first + 8 * n >= 4096) { carry(0x8011); return; }
            for (uint32_t k = 0; k < n; k++) { set_base(first + 8 * k, 0); descriptor(first + 8 * k, 0, 0x92); }
            next_selector += 8 * n;
            S(EAX, (a & 0xffff0000u) | first); carry(0); return;
        }
        if (ax == 0x0001) { carry(0); return; }  /* free descriptor: kept mapped */
        if (ax == 0x0003) { S(EAX, (a & 0xffff0000u) | 8); carry(0); return; }
        if (ax == 0x0007) {
            uint32_t sel = b & 65535;
            if (!has_base[sel] || sel < 0x100) { carry(0x8022); return; }
            uint32_t base = ((c & 65535) << 16) | (d & 65535);
            set_base(sel, base); descriptor(sel, base, code_sel[sel] ? 0x9a : 0x92); carry(0); return;
        }
        if (ax == 0x0008) { if (!has_base[b & 65535]) { carry(0x8022); return; } carry(0); return; }
        if (ax == 0x0009) {
            uint32_t sel = b & 65535;
            if (!has_base[sel]) { carry(0x8022); return; }
            code_sel[sel] = (c & 8) != 0;
            descriptor(sel, bases[sel], code_sel[sel] ? 0x9a : 0x92); carry(0); return;
        }
        if (ax == 0x000a) {
            uint32_t sel = b & 65535, alias = next_selector;
            if (!has_base[sel] || alias >= 4096) { carry(0x8022); return; }
            next_selector += 8; set_base(alias, bases[sel]); descriptor(alias, bases[sel], 0x92);
            S(EAX, (a & 0xffff0000u) | alias); carry(0); return;
        }
        if (ax == 0x0204) {
            uint32_t n = b & 255;
            S(ECX, (c & 0xffff0000u) | (vec_set[n] ? vec_sel[n] : 8)); S(EDX, vec_set[n] ? vec_pc[n] : 0xf000); carry(0); return;
        }
        if (ax == 0x0205) {
            uint32_t n = b & 255;
            vec_sel[n] = c & 65535; vec_pc[n] = d; vec_set[n] = 1; carry(0); return;
        }
        if (ax == 0x0200) { S(ECX, (c & 0xffff0000u) | 0xf000); S(EDX, (d & 0xffff0000u) | 0xff53); carry(0); return; }
        if (ax == 0x0201) { carry(0); return; }  /* real-mode vectors: no real mode here */
        if (ax == 0x0300 && (b & 255) == 0x10) {  /* simulate a real-mode INT 10h: the VESA calls (above) */
            uint32_t rc = address(ES, G(EDI));       /* the real-mode call structure */
            uint32_t r_ax = get32(rc + 0x1c) & 0xffff, r_bx = get32(rc + 0x10) & 0xffff, r_cx = get32(rc + 0x18) & 0xffff;
            uint32_t r_dx = get32(rc + 0x14) & 0xffff, r_di = get32(rc + 0x00) & 0xffff;
            uint32_t es = mem[rc + 0x22] | mem[rc + 0x23] << 8;
            if ((r_ax >> 8) != 0x4f) { carry(0x8001); return; }
            uint32_t fn = r_ax & 0xff, pbx = fn == 1 ? r_cx : r_bx;
            vbe_call(&r_ax, &pbx, &r_dx, es * 16 + r_di);
            put32(rc + 0x1c, (get32(rc + 0x1c) & 0xffff0000u) | r_ax);
            if (fn == 3) put32(rc + 0x10, (get32(rc + 0x10) & 0xffff0000u) | pbx);
            put32(rc + 0x14, (get32(rc + 0x14) & 0xffff0000u) | r_dx);
            carry(0); return;
        }
        if (ax == 0x0400) { S(EAX, 0x005a); S(EBX, 3); S(ECX, (c & 0xffffff00u) | 4); S(EDX, 0x0870); carry(0); return; }
        if (ax == 6) {
            uint32_t sel = b & 65535;
            if (!has_base[sel]) { carry(0x8022); return; }
            S(ECX, bases[sel] >> 16); S(EDX, bases[sel] & 65535); carry(0); return;
        }
        if (ax == 0x101) {
            uint32_t sel = d & 65535;
            if (!is_alloc_sel[sel]) { carry(0x8022); return; }
            arena_release(&conventional, bases[sel]); is_alloc_sel[sel] = 0;
            free_selectors[nfree_selectors++] = sel; carry(0); return;
        }
        if (ax == 0x100) {
            int ok; uint32_t p, s; allocate_dos(b & 65535, &ok, &p, &s);
            if (ok) { S(EAX, p >> 4); S(EDX, s); }
            return;
        }
        if (ax == 0x501) {
            uint32_t size = ((b & 65535) << 16) | (c & 65535);
            if (!size) { carry(0x8021); return; }
            size = (size + 4095) & ~4095u;
            int64_t ptr = arena_allocate(&linear_arena, size);
            if (ptr < 0) { carry(0x8012); return; }
            if ((uint32_t)ptr + size > heap) heap = (uint32_t)ptr + size;
            if (nlinear >= 8192) stop("too many linear allocations");
            linear_allocs[nlinear++] = (Lin){(uint32_t)ptr, size};
            memset(mem + ptr, 0, size);
            S(EBX, ptr >> 16); S(ECX, ptr & 65535); S(ESI, ptr >> 16); S(EDI, ptr & 65535);
            carry(0); return;
        }
        if (ax == 0x502) {
            uint32_t handle = ((G(ESI) & 65535) << 16) | (G(EDI) & 65535);
            int i = lin_index(handle);
            if (i < 0) { carry(0x8023); return; }
            linear_allocs[i] = linear_allocs[--nlinear];
            arena_release(&linear_arena, handle);
            carry(0); return;
        }
        if (ax == 0x503) {
            uint32_t handle = ((G(ESI) & 65535) << 16) | (G(EDI) & 65535);
            uint32_t size = ((b & 65535) << 16) | (c & 65535);
            int i = lin_index(handle);
            if (i < 0 || !size) { carry(i < 0 ? 0x8023 : 0x8021); return; }
            size = (size + 4095) & ~4095u;
            uint32_t old = linear_allocs[i].size;
            int64_t ptr = arena_resize(&linear_arena, handle, size);
            if (ptr < 0) { carry(0x8012); return; }
            uint32_t keep = size < old ? size : old;
            memmove(mem + ptr, mem + handle, keep);
            if (size > old) memset(mem + ptr + old, 0, size - old);
            linear_allocs[i] = (Lin){(uint32_t)ptr, size};
            if ((uint32_t)ptr + size > heap) heap = (uint32_t)ptr + size;
            S(EBX, ptr >> 16); S(ECX, ptr & 65535); S(ESI, ptr >> 16); S(EDI, ptr & 65535);
            carry(0); return;
        }
    }
    stop("unsupported int %02x ax=%04x bx=%08x cx=%08x dx=%08x at %08x", number, ax, b, c, d, G(EIP));
}

/* ---- image (tools/export_image.py) ---- */
static int load_image(const char *path, uint32_t *entry, uint32_t *stack) {
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(status, sizeof status, "cannot open %s", path); return -1; }
    char magic[8]; uint32_t hdr[3];
    if (fread(magic, 1, 8, f) != 8 || memcmp(magic, "TPIMG001", 8) || fread(hdr, 4, 3, f) != 3) {
        fclose(f); snprintf(status, sizeof status, "bad image %s", path); return -1;
    }
    *entry = hdr[0]; *stack = hdr[1];
    for (uint32_t i = 0; i < hdr[2]; i++) {
        uint32_t o[2];
        if (fread(o, 4, 2, f) != 2 || (uint64_t)o[0] + o[1] > MEMORY || fread(mem + o[0], 1, o[1], f) != o[1]) {
            fclose(f); snprintf(status, sizeof status, "truncated image"); return -1;
        }
    }
    fclose(f);
    return 0;
}

static char cmd_args[128];
void tp_host_set_args(const char *args) { snprintf(cmd_args, sizeof cmd_args, "%s", args ? args : ""); }
/* The image to load (the app switches from INTRO.EXE to MAIN.EXE on the game thread: no setenv there, issue #3);
 * empty: TP_IMAGE, then the caller's. */
static char image_path[1024];
void tp_host_set_image(const char *path) { snprintf(image_path, sizeof image_path, "%s", path ? path : ""); }
int tp_host_init(const char *game_dir, const char *overlay_dir, int det, const TpScript *s) {
    mem = tp_memory();
    memset(mem, 0, MEMORY);
    tp_reset();
    deterministic = det;
    if (s) script = *s;
    snprintf(game_root, sizeof game_root, "%s", game_dir);
    snprintf(overlay_root, sizeof overlay_root, "%s", overlay_dir);
    mkdir(overlay_root, 0755);
    for (int i = 0; i < 20; i++) { if (handles_fd[i] >= 0) close(handles_fd[i]); handles_fd[i] = -1; }
    /* Full reset: the app restarts the game in-process (THEME.BAT errorlevel >= 10). */
    memset(bases, 0, sizeof bases); memset(has_base, 0, sizeof has_base);
    memset(alloc_size, 0, sizeof alloc_size); memset(is_alloc_sel, 0, sizeof is_alloc_sel); memset(code_sel, 0, sizeof code_sel);
    nlinear = nfree_selectors = 0;
    memset(vec_sel, 0, sizeof vec_sel); memset(vec_pc, 0, sizeof vec_pc); memset(vec_set, 0, sizeof vec_set);
    memset(port_val, 0, sizeof port_val); memset(port_known, 0, sizeof port_known);
    memset(palette, 0, sizeof palette); dac_index = 0;
    ticks = 0; poll_count = irq_count = 0; blocks = 0; release_due = -1;
    pit_divisor = 65536; pit_low = -1; card_allow = 0; card_hold_max = 0;
    memset(&mouse, 0, sizeof mouse); pending_mouse = 0; in_callback = 0;
    pthread_mutex_lock(&input_lock); nkeys = 0; nraw = 0; mq_head = mq_tail = 0; pthread_mutex_unlock(&input_lock);
    port60 = 0;
    key_at = click_at = block_click_at = 0;
    tp_audio_midi_reset();
    stopped = 0; exit_code = -1; mpu_n = 0; sb16_reset(); sb_irq_pending = 0;
    arena_init(&linear_arena, 0x400000, MEMORY, 4096);
    /* (a hole the game is never given: qol.c's EXTRAS menu lives there. Its drop-down is found as 0x1b53c6 +
     * n * 0x354 with n a signed word, so it must lie below ~0x1c55000) */
    linear_arena.free[0] = (Span){0x400000, TP_EXTRAS_AREA - 0x400000};
    linear_arena.free[1] = (Span){TP_EXTRAS_AREA + TP_EXTRAS_SIZE, MEMORY - (TP_EXTRAS_AREA + TP_EXTRAS_SIZE)}; linear_arena.nfree = 2;
    memset(mem + TP_EXTRAS_AREA, 0, TP_EXTRAS_SIZE);
    arena_init(&conventional, 0x40000, 0x90000, 16);
    next_selector = 0x100; heap = 0x400000;
    vec_sel[8] = 8; vec_pc[8] = 0xf000; vec_set[8] = 1;
    port_val[0x21] = port_val[0xa1] = port_val[0x3c6] = 0xff;
    port_known[0x21] = port_known[0xa1] = port_known[0x3c6] = 1;
    port_known[0x61] = 1;
    /* Real time: the PIC mask a BIOS leaves (timer, keyboard and cascade
     * unmasked). The deterministic model keeps the verified 0xff. */
    if (!det) port_val[0x21] = 0xb8;
    video_mode = 3;
    mouse.x = 320; mouse.y = 240; mouse.rx1 = 639; mouse.ry1 = 479;
    tp_io_callback(io_cb);
    char img[1100]; snprintf(img, sizeof img, "%s/MAIN.TPIMG", game_dir);
    const char *env_img = image_path[0] ? image_path : getenv("TP_IMAGE");
    uint32_t entry, stack;
    if (load_image(env_img ? env_img : img, &entry, &stack)) return -1;
    memset(mem + GDT, 0, 4096);
    mem[0xf000] = 0xcf;
    static const uint8_t epilogue[] = {0x0f, 0xa9, 0x0f, 0xa1, 0x07, 0x1f, 0x61, 0x9d, 0xc3};
    memcpy(mem + 0xf010, epilogue, sizeof epilogue);
    if (setjmp(stop_jb)) return -1;
    set_base(8, 0); set_base(16, 0); set_base(24, PSP); set_base(32, ENV);
    descriptor(8, 0, 0x9a); descriptor(16, 0, 0x92); descriptor(24, PSP, 0x92); descriptor(32, ENV, 0x92);
    put16(PSP + 0x2c, 32);
    {   /* the command tail: empty, as the verified runs; tp_host_set_args (e.g. the launcher's language, " -l1") */
        size_t n = strlen(cmd_args); if (n > 126) n = 126;
        uint8_t tail[128]; tail[0] = (uint8_t)n; memcpy(tail + 1, cmd_args, n); tail[n + 1] = '\r';
        mwrite(PSP + 0x80, tail, (uint32_t)n + 2);
    }
    static const char env[] = "PATH=C:\\GAME\0NO87=1\0\0\x01\x00" "C:\\GAME\\MAIN.EXE";
    mwrite(ENV, env, sizeof env);       /* includes the final NUL */
    for (int r = EAX; r <= EDI; r++) S(r, r == ESP ? stack : 0);
    S(EFLAGS, 0x202);
    S(CS, 8); S(DS, 16); S(ES, 24); S(SS, 16); S(FS, 16); S(GS, 0);
    S(EIP, entry);
    tp_watch_pc = 0xf010;
    { extern uint8_t *tp_cov; if (getenv("TP_COVERAGE") && !tp_cov) tp_cov = calloc(0x20000, 1); }   /* (verify.c: the port audit's block coverage) */
    { extern uint32_t tp_break_pc; const char *b = getenv("TP_BREAK"); if (b) tp_break_pc = (uint32_t)strtoul(b, NULL, 16); }
    { extern uint32_t tp_break_list[32]; extern int tp_break_n; const char *b = getenv("TP_BREAKS");
      for (char *e; b && *b && tp_break_n < 32; b = *e ? e + 1 : e) tp_break_list[tp_break_n++] = (uint32_t)strtoul(b, &e, 16); }
    if (!gov_rate) { const char *e = getenv("TP_CYCLES"); tp_host_set_cycles(e ? atof(e) : 10000); }
    gov_t0 = 0;
    rt_start = now_s(); timer_next = emu_now();
    snprintf(status, sizeof status, "running");
    return 0;
}

/* Calls a game function from the host (cdecl arguments) and runs it to its
 * return, servicing DOS/BIOS interrupts as usual but no hardware interrupts.
 * Registers are restored afterwards. Returns EAX, or -1 if it did not return. */
#define GUEST_RETURN 0xf0f0u      /* no block there: the run stops on arriving */
int64_t tp_host_guest_call(uint32_t fn, const uint32_t *args, int nargs, uint32_t max_blocks) {
    uint32_t saved[16]; for (int i = 0; i < 16; i++) saved[i] = G(i);
    uint32_t sp = (G(ESP) - 4096) & ~15u;
    for (int i = nargs - 1; i >= 0; i--) { sp -= 4; put32(sp, args[i]); }
    sp -= 4; put32(sp, GUEST_RETURN);
    S(ESP, sp); S(EIP, fn);
    int64_t result = -1;
    jmp_buf outer; memcpy(outer, stop_jb, sizeof outer);
    int js = setjmp(stop_jb);
    if (js) {   /* a service stopped inside the call: the run stops too and says so (issue #4); -2, not a budget overrun's -1 */
        size_t l = strlen(status); snprintf(status + l, sizeof status - l, " (in a host call to %06x)", fn);
        stopped = 1; result = -2;
    } else {
        uint32_t ran = 0;
        while (ran < max_blocks) {
            int n = tp_run(20000); ran += 20000;
            if (n == -2 && G(EIP) == GUEST_RETURN) { result = G(EAX); break; }
            if (n == -3) { extern int tp_hook_armed, tp_hook_which; void tp_view_nested_hook(void), tp_qol_hook(int nested);
                if (tp_hook_which >= 12) tp_qol_hook(1); else tp_view_nested_hook(); tp_hook_armed = 0; continue; }   /* hooks inside host calls (view.c logging; qol.c) */
            if (n < 0) break;
            if (n) interrupt((uint32_t)n);
        }
    }
    memcpy(stop_jb, outer, sizeof outer);
    for (int i = 0; i < 16; i++) S(i, saved[i]);
    return result;
}
uint8_t *tp_host_memory(void) { return mem; }
int tp_host_is_realtime(void) { return !deterministic; }

static volatile int paused;
void tp_host_pause(int on) { paused = on; }       /* the app is hidden or minimised: the game waits */
int tp_host_run(uint32_t quantum, uint32_t blocks_limit, double ms) {
    if (stopped) return -1;
    if (paused && !deterministic) {                /* the timer skips the stall on resume (realtime_events) */
        struct timespec ts = {0, 20000000}; nanosleep(&ts, NULL); gov_t0 = 0;
        return quit_requested ? (snprintf(status, sizeof status, "quit"), stopped = 1, 1) : 0;
    }
    double deadline = now_s() + ms / 1000.0;
    int r = setjmp(stop_jb);
    if (r) { stopped = 1; return r == 2 ? 1 : -1; }
    for (;;) {
        if (quit_requested) { snprintf(status, sizeof status, "quit"); stopped = 1; return 1; }
        uint32_t budget = quantum;
        if (blocks_limit) {
            if (blocks >= blocks_limit) return 0;
            if (blocks_limit - blocks < budget) budget = (uint32_t)(blocks_limit - blocks);
        }
        double tr0 = now_s(); int n = tp_run(budget); tp_dbg_run += now_s() - tr0;
        if (n == -3) { extern int tp_hook_armed, tp_hook_which; void tp_qol_hook(int nested);
                      if (tp_hook_which >= 12) tp_qol_hook(0); else tp_view_hook(); tp_hook_armed = 0; n = 0; }   /* view.c's points, or qol.c's */
        if (stopped) { snprintf(status + strlen(status), sizeof status - strlen(status), " (port I/O)"); return -1; }
        if (n < 0 || (n == 0 && !quantum)) {
            snprintf(status, sizeof status, "native %d at %08x: %s", n, G(EIP), n == -2 ? "no compiled block" : tp_error());
            stopped = 1; return -1;
        }
        if (n) interrupt((uint32_t)n);
        advance_blocks(tp_blocks());
        if (!deterministic) {
            govern();
            realtime_events();
            if (now_s() >= deadline) return 0;
        }
    }
}

const char *tp_host_status(void) { return status; }
int tp_host_exit_code(void) { return exit_code; }
uint64_t tp_host_blocks(void) { return blocks; }

int tp_host_frame_rgba(uint32_t *out) {
    if (video_mode != 0x13) return 0;
    uint32_t pal[256]; uint8_t mask = port_val[0x3c6];
    for (int i = 0; i < 256; i++) {
        uint8_t r = palette[i * 3], g = palette[i * 3 + 1], b = palette[i * 3 + 2];
        r = (r << 2) | (r >> 4); g = (g << 2) | (g >> 4); b = (b << 2) | (b >> 4);
        pal[i] = 0xff000000u | (b << 16) | (g << 8) | r;
    }
    const uint8_t *px = mem + 0xa0000;
    for (int i = 0; i < 64000; i++) out[i] = pal[px[i] & mask];
    return 1;
}

/* The screen's pixels in the current mode, for the emulation thread (view.c). */
const uint8_t *tp_host_screen(void) {
    if (video_mode == 0x101) { svga_flush(); return svga_fb; }
    return mem + 0xa0000;
}
/* The frame in either mode: 320x200 (mode 13h) or 640x480 (VESA 101h). */
int tp_host_frame_size(int *w, int *h) {
    if (video_mode == 0x13) { *w = 320; *h = 200; return 1; }
    if (video_mode == 0x101) { *w = SVGA_W; *h = SVGA_H; return 1; }
    return 0;
}
int tp_host_frame_rgba_any(uint32_t *out, int max_pixels) {
    if (video_mode == 0x13) return max_pixels >= 64000 ? tp_host_frame_rgba(out) : 0;
    if (video_mode != 0x101 || max_pixels < SVGA_BYTES) return 0;
    uint32_t pal[256]; tp_host_palette_rgba(pal);
    int bank = svga_bank; uint32_t lo = bank * 65536u, hi = lo + bank_bytes(bank);
    for (uint32_t i = 0; i < SVGA_BYTES; i++) out[i] = pal[i >= lo && i < hi ? mem[0xa0000 + i - lo] : svga_fb[i]];
    return 1;
}

static void be32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static void chunk(FILE *f, const char *kind, const uint8_t *data, uint32_t n) {
    uint8_t h[8]; be32(h, n); memcpy(h + 4, kind, 4); fwrite(h, 1, 8, f);
    if (n) fwrite(data, 1, n, f);
    uLong crc = crc32(0, (const Bytef *)kind, 4); if (n) crc = crc32(crc, data, n);
    uint8_t t[4]; be32(t, (uint32_t)crc); fwrite(t, 1, 4, f);
}
/* Same encoding as dos.py DOS.frame (zlib default level) so hashes compare. */
int tp_host_png(const char *path, char hex[65]) {
    if (video_mode != 0x13) return 0;
    static uint32_t rgba[64000]; tp_host_frame_rgba(rgba);
    static uint8_t rows[200 * 961];
    for (int y = 0, k = 0; y < 200; y++) {
        rows[k++] = 0;
        for (int x = 0; x < 320; x++) { uint32_t v = rgba[y * 320 + x]; rows[k++] = v; rows[k++] = v >> 8; rows[k++] = v >> 16; }
    }
    uLongf n = compressBound(sizeof rows); uint8_t *z = malloc(n);
    compress(z, &n, rows, sizeof rows);
    FILE *f = fopen(path, "wb"); if (!f) { free(z); return 0; }
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    uint8_t ihdr[13]; be32(ihdr, 320); be32(ihdr + 4, 200); ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = ihdr[11] = ihdr[12] = 0;
    chunk(f, "IHDR", ihdr, 13); chunk(f, "IDAT", z, (uint32_t)n); chunk(f, "IEND", NULL, 0);
    fclose(f); free(z);
    if (hex) {
        FILE *g = fopen(path, "rb"); fseek(g, 0, SEEK_END); long len = ftell(g); fseek(g, 0, SEEK_SET);
        uint8_t *buf = malloc(len); fread(buf, 1, len, g); fclose(g);
        uint8_t md[32]; tp_sha256(buf, len, md); free(buf);
        for (int i = 0; i < 32; i++) sprintf(hex + i * 2, "%02x", md[i]);
    }
    return 1;
}

/* The current palette as RGBA words (the view's composite uses it). */
void tp_host_palette_rgba(uint32_t pal[256]) {
    uint8_t mask = port_val[0x3c6] ? port_val[0x3c6] : 0xff;
    for (int i = 0; i < 256; i++) {
        int j = i & mask;
        uint8_t r = palette[j * 3], g = palette[j * 3 + 1], b = palette[j * 3 + 2];
        r = (r << 2) | (r >> 4); g = (g << 2) | (g >> 4); b = (b << 2) | (b >> 4);
        pal[i] = 0xff000000u | (b << 16) | (g << 8) | r;
    }
}
/* An RGBA image as PNG (development output). */
int tp_host_png_rgba(const char *path, const uint32_t *px, int w, int h) {
    size_t rowlen = (size_t)w * 3 + 1, len = rowlen * h;
    uint8_t *rows = malloc(len);
    for (int y = 0; y < h; y++) {
        uint8_t *r = rows + y * rowlen; *r++ = 0;
        for (int x = 0; x < w; x++) { uint32_t v = px[(size_t)y * w + x]; *r++ = v; *r++ = v >> 8; *r++ = v >> 16; }
    }
    uLongf n = compressBound(len); uint8_t *z = malloc(n);
    compress2(z, &n, rows, len, 3); free(rows);
    FILE *f = fopen(path, "wb"); if (!f) { free(z); return 0; }
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    uint8_t ihdr[13]; be32(ihdr, w); be32(ihdr + 4, h); ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = ihdr[11] = ihdr[12] = 0;
    chunk(f, "IHDR", ihdr, 13); chunk(f, "IDAT", z, (uint32_t)n); chunk(f, "IEND", NULL, 0);
    fclose(f); free(z); return 1;
}

void tp_host_key(uint32_t key) {
    pthread_mutex_lock(&input_lock);
    if (nkeys < 64) keys[nkeys++] = key;
    pthread_mutex_unlock(&input_lock);
}
void tp_host_shift(uint32_t flags) { shift_flags = flags; }
void tp_host_key_raw(uint8_t scan, int down) {
    pthread_mutex_lock(&input_lock);
    if (nraw < 64) raw_keys[nraw++] = down ? scan : (scan | 0x80);
    pthread_mutex_unlock(&input_lock);
}
static void push_mouse(int x, int y, uint32_t buttons) {
    pthread_mutex_lock(&input_lock);
    int next = (mq_tail + 1) % 256;
    if (next != mq_head) { mouse_q[mq_tail] = (MouseEv){x, y, buttons}; mq_tail = next; }
    pthread_mutex_unlock(&input_lock);
}
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
void tp_host_mouse(double fx, double fy, uint32_t buttons) {
    int x0 = mouse.rx0, x1 = mouse.rx1, y0 = mouse.ry0, y1 = mouse.ry1;
    /* The game's range is in the driver's 640x400 space for mode 13h, 640x480 in VESA 101h. */
    int x = clampi((int)(fx * 640.0), x0, x1), y = clampi((int)(fy * (video_mode == 0x101 ? 480.0 : 400.0)), y0, y1);
    push_mouse(x, y, buttons);
}
static double acc_x, acc_y;
void tp_host_mouse_delta(double dx, double dy, uint32_t buttons) {
    pthread_mutex_lock(&input_lock);
    int bx = mq_head != mq_tail ? mouse_q[(mq_tail + 255) % 256].x : mouse.x;
    int by = mq_head != mq_tail ? mouse_q[(mq_tail + 255) % 256].y : mouse.y;
    pthread_mutex_unlock(&input_lock);
    acc_x += dx * 2; acc_y += dy * 2;
    int ix = (int)acc_x, iy = (int)acc_y; acc_x -= ix; acc_y -= iy;
    push_mouse(clampi(bx + ix, mouse.rx0, mouse.rx1), clampi(by + iy, mouse.ry0, mouse.ry1), buttons);
}
void tp_host_mouse_pos(int *x, int *y) { *x = mouse.x; *y = mouse.y; }   /* the driver's 640x400 space */
void tp_host_quit(void) { quit_requested = 1; }
void tp_host_set_realtime(void) { deterministic = 0; rt_start = now_s(); timer_next = emu_now(); gov_t0 = 0; }

static void list_dir(const char *dir, const char *rel, void (*fn)(const char *, const char *, void *), void *ctx) {
    DIR *d = opendir(dir); if (!d) return;
    struct dirent *e;
    char names[256][256]; int n = 0;
    while ((e = readdir(d)) && n < 256) if (e->d_name[0] != '.') snprintf(names[n++], 256, "%s", e->d_name);
    closedir(d);
    qsort(names, n, 256, (int (*)(const void *, const void *))strcmp);
    for (int i = 0; i < n; i++) {
        char full[1024], r[1024];
        snprintf(full, sizeof full, "%s/%s", dir, names[i]);
        snprintf(r, sizeof r, "%s%s%s", rel, *rel ? "/" : "", names[i]);
        if (is_dir(full)) { list_dir(full, r, fn, ctx); continue; }
        FILE *f = fopen(full, "rb"); if (!f) continue;
        TpSha256 h; tp_sha256_init(&h); uint8_t buf[65536]; size_t k;
        while ((k = fread(buf, 1, sizeof buf, f))) tp_sha256_update(&h, buf, k);
        fclose(f);
        uint8_t md[32]; tp_sha256_final(&h, md); char hex[65];
        for (int j = 0; j < 32; j++) sprintf(hex + j * 2, "%02x", md[j]);
        fn(r, hex, ctx);
    }
}
/* Checkpoint for tools/spotcheck.py: "TPCKPT01", 16 registers (EAX..GS order of
 * tp_get), selector bases (count, then selector/base pairs), then zlib memory. */
int tp_host_checkpoint(const char *path) {
    FILE *f = fopen(path, "wb"); if (!f) return -1;
    fwrite("TPCKPT01", 1, 8, f);
    uint32_t regs[16]; for (int i = 0; i < 16; i++) regs[i] = G(i);
    fwrite(regs, 4, 16, f);
    uint32_t n = 0; for (uint32_t s = 0; s < 65536; s++) if (has_base[s]) n++;
    fwrite(&n, 4, 1, f);
    for (uint32_t s = 0; s < 65536; s++) if (has_base[s]) { uint32_t pair[2] = {s, bases[s]}; fwrite(pair, 4, 2, f); }
    uLongf zn = compressBound(MEMORY); uint8_t *z = malloc(zn);
    compress2(z, &zn, mem, MEMORY, 1);
    uint64_t len = zn; fwrite(&len, 8, 1, f); fwrite(z, 1, zn, f); free(z);
    fclose(f); return 0;
}
void tp_host_list_saved(void (*fn)(const char *, const char *, void *), void *ctx) { list_dir(overlay_root, "", fn, ctx); }
