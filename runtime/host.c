/* Native DOS/DPMI platform host: C port of tools/dos.py, plus a real-time
 * mode for the app. Deterministic mode reproduces the Python host's scripted
 * scheduling exactly so the verified first-park frame and profile can be
 * checked against config/milestones.json. Unknown services stop with a
 * message; nothing succeeds silently. */
#include "host.h"
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
#include <CommonCrypto/CommonDigest.h>

/* compiled-code runtime (runtime/verify.c) */
void *tp_memory(void);
void tp_reset(void);
uint32_t tp_get(unsigned i);
void tp_set(unsigned i, uint32_t v);
void tp_descriptor(unsigned selector, unsigned base);
int tp_run(uint32_t budget);
uint32_t tp_blocks(void);
const char *tp_error(void);
void tp_io_callback(int (*fn)(int, unsigned, unsigned, unsigned));
extern uint32_t tp_watch_pc;
extern volatile int tp_watch_hit;

enum { EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI, EFLAGS, EIP, CS, DS, ES, SS, FS, GS };
#define MEMORY (64u * 1024u * 1024u)
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
typedef struct { uint32_t ptr, size; } Lin;
static Lin linear_allocs[8192]; static int nlinear;
static uint32_t next_selector, heap;
static uint32_t free_selectors[4096]; static int nfree_selectors;
static uint32_t vec_sel[256], vec_pc[256]; static uint8_t vec_set[256];
static int handles_fd[20]; static int handles_writable[20];
static uint64_t ticks;
static uint32_t video_mode;
static uint8_t port_val[65536]; static uint8_t port_known[65536];
static uint8_t palette[768]; static uint32_t dac_index;
static uint32_t poll_count, irq_count;
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

/* ---- ports ---- */
static int vretrace(void) {
    if (deterministic) { ticks++; return ticks & 1 ? 8 : 0; }
    double t = now_s() * 70.086;               /* VGA mode 13h refresh */
    return (t - (double)(uint64_t)t) > 0.9 ? 8 : 0;
}
static int port_in(unsigned port, unsigned size) {
    if (size != 1) stop("unsupported port input width at %x", port);
    if (port_known[port]) return port_val[port];
    if (port == 0x3da) return vretrace();
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
    else if (port == 0x3c8) dac_index = value * 3;
    else if (port == 0x3c9) { palette[dac_index] = value & 63; dac_index = (dac_index + 1) % 768; }
    else stop("unsupported OUT %04x=%x", port, value);
}
static int io_cb(int output, unsigned port, unsigned size, unsigned value) {
    jmp_buf saved; memcpy(saved, stop_jb, sizeof saved);
    if (setjmp(stop_jb)) { memcpy(stop_jb, saved, sizeof saved); stopped = 1; return -1; }
    int r = 0;
    if (output) port_out(port, size, value); else r = port_in(port, size);
    memcpy(stop_jb, saved, sizeof saved);
    return r;
}

/* ---- interrupts into guest code ---- */
static int timer_installed(void) { return !(vec_sel[8] == 8 && vec_pc[8] == 0xf000); }
static void timer_irq(void) {
    if (!(G(EFLAGS) & 0x200) || !timer_installed()) return;
    uint32_t sp = G(ESP) - 12;
    put32(sp, G(EIP)); put32(sp + 4, G(CS)); put32(sp + 8, G(EFLAGS));
    S(ESP, sp); S(CS, vec_sel[8]); S(EIP, vec_pc[8]);
    S(EFLAGS, G(EFLAGS) & ~0x300u);
    irq_count++;
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
static void realtime_events(void) {
    if (in_callback && tp_watch_hit) in_callback = 0;
    if (!(G(EFLAGS) & 0x200)) return;
    double t = now_s();
    double period = pit_divisor / 1193182.0;
    if (t >= timer_next && timer_installed()) {
        if (t - timer_next > 0.25) timer_next = t;        /* do not replay a long stall */
        timer_next += period;
        timer_irq();
    }
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
        queue_mouse(script.block_clicks[block_click_at].x, script.block_clicks[block_click_at].y, 1);
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
    struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm; localtime_r(&ts.tv_sec, &tm);
    S(ECX, (tm.tm_hour << 8) | tm.tm_min);
    S(EDX, (tm.tm_sec << 8) | (uint32_t)(ts.tv_nsec / 10000000));
}

static void interrupt(uint32_t number) {
    uint32_t a = G(EAX), b = G(EBX), c = G(ECX), d = G(EDX);
    uint32_t ax = a & 65535, ah = (a >> 8) & 255;
    char name[512], path[1024];
    if (number == 0x21) {
        if (ah == 0xb) { S(EAX, (a & ~255u) | (keys_available() ? 255 : 0)); return; }
        if (ah == 7 || ah == 8) { uint32_t k = wait_key(); S(EAX, (a & ~255u) | (k & 255)); return; }
        if (ah == 0x30) { S(EAX, 0x1606); S(EBX, 0); S(ECX, 0); return; }
        if (ax == 0xff00 && (d & 65535) == 0x78) { S(EAX, (a & ~255u) | 1); return; }
        if (ah == 0x2c) { dos_time(a); return; }
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
            close(handles_fd[b]); handles_fd[b] = -1; carry(0); return;
        }
        if (ah == 0x3f) {
            if (b >= 20 || handles_fd[b] < 0) { carry(6); return; }
            uint32_t at = address(DS, d);
            if ((uint64_t)at + c > MEMORY) stop("DOS read outside memory");
            ssize_t got = 0, r;
            while ((uint32_t)got < c && (r = read(handles_fd[b], mem + at + got, c - got)) > 0) got += r;
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
            S(ES, vec_set[n] ? vec_sel[n] : 8); S(EBX, vec_set[n] ? vec_pc[n] : 0); return;
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
    if (number == 0x33) {
        if (ax == 0x2f) return;
        if (ax == 0) { S(EAX, 0xffff); S(EBX, 3); return; }
        if (ax == 1 || ax == 2) { mouse.visible = ax == 1; return; }
        if (ax == 3) { S(EBX, mouse.buttons); S(ECX, mouse.x); S(EDX, mouse.y); return; }
        if (ax == 4) { mouse.x = c & 65535; mouse.y = d & 65535; return; }
        if (ax == 7) { mouse.rx0 = c & 65535; mouse.rx1 = d & 65535; return; }
        if (ax == 8) { mouse.ry0 = c & 65535; mouse.ry1 = d & 65535; return; }
        if (ax == 0xc) { mouse.has_cb = 1; mouse.cb_mask = c & 65535; mouse.cb_sel = G(ES); mouse.cb_pc = d; return; }
    }
    if (number == 0x10) {
        if (ah == 0xf) { S(EAX, (a & 0xffff0000u) | 0x5000 | video_mode); S(EBX, b & ~0xff00u); return; }
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

int tp_host_init(const char *game_dir, const char *overlay_dir, int det, const TpScript *s) {
    mem = tp_memory();
    memset(mem, 0, MEMORY);
    tp_reset();
    deterministic = det;
    if (s) script = *s;
    snprintf(game_root, sizeof game_root, "%s", game_dir);
    snprintf(overlay_root, sizeof overlay_root, "%s", overlay_dir);
    mkdir(overlay_root, 0755);
    for (int i = 0; i < 20; i++) handles_fd[i] = -1;
    arena_init(&linear_arena, 0x400000, MEMORY, 4096);
    arena_init(&conventional, 0x40000, 0x90000, 16);
    next_selector = 0x100; heap = 0x400000;
    vec_sel[8] = 8; vec_pc[8] = 0xf000; vec_set[8] = 1;
    port_val[0x21] = port_val[0xa1] = port_val[0x3c6] = 0xff;
    port_known[0x21] = port_known[0xa1] = port_known[0x3c6] = 1;
    video_mode = 3;
    mouse.x = 320; mouse.y = 240; mouse.rx1 = 639; mouse.ry1 = 479;
    tp_io_callback(io_cb);
    char img[1100]; snprintf(img, sizeof img, "%s/MAIN.TPIMG", game_dir);
    const char *env_img = getenv("TP_IMAGE");
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
    mwrite(PSP + 0x80, "\0\r", 2);
    static const char env[] = "PATH=C:\\GAME\0NO87=1\0\0\x01\x00" "C:\\GAME\\MAIN.EXE";
    mwrite(ENV, env, sizeof env);       /* includes the final NUL */
    for (int r = EAX; r <= EDI; r++) S(r, r == ESP ? stack : 0);
    S(EFLAGS, 0x202);
    S(CS, 8); S(DS, 16); S(ES, 24); S(SS, 16); S(FS, 16); S(GS, 0);
    S(EIP, entry);
    tp_watch_pc = 0xf010;
    rt_start = now_s(); timer_next = rt_start;
    snprintf(status, sizeof status, "running");
    return 0;
}

int tp_host_run(uint32_t quantum, uint32_t blocks_limit, double ms) {
    if (stopped) return -1;
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
        int n = tp_run(budget);
        if (stopped) { snprintf(status + strlen(status), sizeof status - strlen(status), " (port I/O)"); return -1; }
        if (n < 0 || (n == 0 && !quantum)) {
            snprintf(status, sizeof status, "native %d at %08x: %s", n, G(EIP), n == -2 ? "no compiled block" : tp_error());
            stopped = 1; return -1;
        }
        if (n) interrupt((uint32_t)n);
        advance_blocks(tp_blocks());
        if (!deterministic) {
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
        uint8_t md[32]; CC_SHA256(buf, (CC_LONG)len, md); free(buf);
        for (int i = 0; i < 32; i++) sprintf(hex + i * 2, "%02x", md[i]);
    }
    return 1;
}

void tp_host_key(uint32_t key) {
    pthread_mutex_lock(&input_lock);
    if (nkeys < 64) keys[nkeys++] = key;
    pthread_mutex_unlock(&input_lock);
}
void tp_host_shift(uint32_t flags) { shift_flags = flags; }
static void push_mouse(int x, int y, uint32_t buttons) {
    pthread_mutex_lock(&input_lock);
    int next = (mq_tail + 1) % 256;
    if (next != mq_head) { mouse_q[mq_tail] = (MouseEv){x, y, buttons}; mq_tail = next; }
    pthread_mutex_unlock(&input_lock);
}
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
void tp_host_mouse(double fx, double fy, uint32_t buttons) {
    int x0 = mouse.rx0, x1 = mouse.rx1, y0 = mouse.ry0, y1 = mouse.ry1;
    /* The game's range is in the driver's 640x400 space for mode 13h. */
    int x = clampi((int)(fx * 640.0), x0, x1), y = clampi((int)(fy * 400.0), y0, y1);
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
void tp_host_quit(void) { quit_requested = 1; }

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
        CC_SHA256_CTX h; CC_SHA256_Init(&h); uint8_t buf[65536]; size_t k;
        while ((k = fread(buf, 1, sizeof buf, f))) CC_SHA256_Update(&h, buf, (CC_LONG)k);
        fclose(f);
        uint8_t md[32]; CC_SHA256_Final(md, &h); char hex[65];
        for (int j = 0; j < 32; j++) sprintf(hex + j * 2, "%02x", md[j]);
        fn(r, hex, ctx);
    }
}
void tp_host_list_saved(void (*fn)(const char *, const char *, void *), void *ctx) { list_dir(overlay_root, "", fn, ctx); }
