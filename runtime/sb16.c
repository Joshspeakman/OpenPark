/* Sound Blaster 16 at 0x220, IRQ 7, DMA 1 (8-bit) and 5 (16-bit), as GOG's
 * sndsetup.inf configures it, plus the 8237 DMA controllers it reads through.
 * The game's own HMI driver code (compiled ahead of time) programs these
 * ports; samples are pulled from guest memory by the audio thread at the
 * programmed rate, which also raises the end-of-block interrupts.
 *
 * Written for this port from the public SB16/8237 programming documentation;
 * only the behaviour the HMI drivers use is modelled. Unknown DSP commands are
 * logged and ignored. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

extern uint8_t *G_MEM;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

/* ---- 8237 DMA ---- */
typedef struct {
    uint32_t base_addr, base_count, addr, count;  /* addr/count in transfer units (bytes or words) */
    uint8_t page, mode, masked;
} Dma;
static Dma dma[8];
static int flipflop[2];                /* per controller */
static const uint8_t page_port_ch[8] = {0x87, 0x83, 0x81, 0x82, 0x8f, 0x8b, 0x89, 0x8a};

static uint32_t dma_linear(int ch) {
    Dma *d = &dma[ch];
    return ch < 4 ? ((uint32_t)d->page << 16) | (d->addr & 0xffff)
                  : ((uint32_t)(d->page & 0xfe) << 16) | ((d->addr & 0xffff) << 1);
}
/* Read one unit (byte or word) and advance; returns -1 when masked. */
static int dma_read(int ch, uint32_t *value) {
    Dma *d = &dma[ch];
    if (d->masked) return -1;
    uint32_t a = dma_linear(ch);
    if (a + 2 > 0x110000) return -1;
    *value = ch < 4 ? G_MEM[a] : (uint32_t)(G_MEM[a] | G_MEM[a + 1] << 8);
    int down = d->mode & 0x20;
    d->addr = (d->addr + (down ? -1 : 1)) & 0xffff;
    if (d->count-- == 0) {             /* terminal count */
        if (d->mode & 0x10) { d->addr = d->base_addr; d->count = d->base_count; }
        else { d->count = 0xffff; d->masked = 1; }
    }
    return 0;
}
static int dma_port_out_(unsigned port, unsigned v);
static int dma_port_out(unsigned port, unsigned v) {
    static int n; static int on = -1; if (on < 0) on = getenv("TP_DEBUG") != NULL;
    int r = dma_port_out_(port, v);
    if (on && r && n++ < 80) fprintf(stderr, "dma: out %02x=%02x\n", port, v);
    return r;
}
static int dma_port_out_(unsigned port, unsigned v) {
    int ctl, ch, reg;
    if (port < 0x10) { ctl = 0; ch = (port >> 1) & 3; reg = port; }
    else if (port >= 0xc0 && port < 0xe0) { ctl = 1; ch = 4 + ((port - 0xc0) >> 2); reg = (port - 0xc0) >> 1; }
    else {
        for (int c = 0; c < 8; c++) if (page_port_ch[c] == port) { dma[c].page = v; return 1; }
        return 0;
    }
    if (reg < 8) {                     /* address (even) / count (odd) of a channel */
        Dma *d = &dma[ch];
        uint32_t *base = (reg & 1) ? &d->base_count : &d->base_addr;
        *base = flipflop[ctl] ? ((*base & 0xff) | (v & 0xff) << 8) : ((*base & 0xff00) | (v & 0xff));
        if (reg & 1) d->count = *base; else d->addr = *base;
        flipflop[ctl] ^= 1;
        return 1;
    }
    int base_ch = ctl * 4;
    switch (reg) {
    case 8: case 9: return 1;                                  /* command, request */
    case 10: dma[base_ch + (v & 3)].masked = (v >> 2) & 1; return 1;
    case 11: dma[base_ch + (v & 3)].mode = v; return 1;
    case 12: flipflop[ctl] = 0; return 1;
    case 13: for (int c = 0; c < 4; c++) dma[base_ch + c].masked = 1; flipflop[ctl] = 0; return 1;
    case 14: for (int c = 0; c < 4; c++) dma[base_ch + c].masked = 0; return 1;
    case 15: for (int c = 0; c < 4; c++) dma[base_ch + c].masked = (v >> c) & 1; return 1;
    }
    return 0;
}
static int dma_port_in(unsigned port, unsigned *v) {
    int ctl, ch, reg;
    if (port < 0x10) { ctl = 0; ch = (port >> 1) & 3; reg = port; }
    else if (port >= 0xc0 && port < 0xe0) { ctl = 1; ch = 4 + ((port - 0xc0) >> 2); reg = (port - 0xc0) >> 1; }
    else {
        for (int c = 0; c < 8; c++) if (page_port_ch[c] == port) { *v = dma[c].page; return 1; }
        return 0;
    }
    if (reg < 8) {
        static long reads; static int dbg = -1; if (dbg < 0) dbg = getenv("TP_DEBUG") != NULL;
        if (dbg && ++reads % 200 == 1) fprintf(stderr, "dma: position read ch %d reg %d (%ld so far)\n", ch, reg, reads);
        uint32_t x = (reg & 1) ? dma[ch].count : dma[ch].addr;
        *v = flipflop[ctl] ? (x >> 8) & 0xff : x & 0xff;
        flipflop[ctl] ^= 1;
        return 1;
    }
    if (reg == 8) { *v = 0; return 1; }        /* status */
    *v = 0xff; return 1;
}

/* ---- DSP ---- */
static uint8_t outq[64]; static int outq_n;
static uint8_t cmd, args[4]; static int nargs, want;
static int reset_high;
static uint8_t mixer_index, mixer[256];
static uint32_t rate = 22050;
static int time_constant_rate;
static uint32_t block8 = 0x7ff;        /* 0x48 block size */
static int irq8_pending, irq16_pending, irq_raise;
static uint32_t test_reg;
/* active transfer */
static int playing, bits16, stereo, is_signed, autoinit, paused;
static uint32_t block_len, block_left;  /* in samples (frames * channels) */
static double phase;
static float hold_l, hold_r, prev_l, prev_r;   /* the card's last two samples: output between them (linear) */

static void push(uint8_t b) { if (outq_n < 64) outq[outq_n++] = b; }
static void raise_irq(int sixteen) {
    if (sixteen) irq16_pending = 1; else irq8_pending = 1;
    mixer[0x82] = (irq8_pending ? 1 : 0) | (irq16_pending ? 2 : 0);
    irq_raise++;
}

static void dsp_reset(void) {
    outq_n = 0; nargs = want = 0; cmd = 0;
    playing = paused = 0; irq8_pending = irq16_pending = 0; mixer[0x82] = 0;
    push(0xaa);
}

static void start(int sixteen, uint8_t command, uint8_t mode, uint32_t len) {
    bits16 = sixteen;
    autoinit = (command & 0x04) != 0;
    stereo = (mode & 0x20) != 0;
    is_signed = (mode & 0x10) != 0;
    block_len = block_left = len + 1;
    playing = 1; paused = 0; phase = 0;
}

static void dsp_log(void) {
    static int n; static int on = -1; if (on < 0) on = getenv("TP_DEBUG") != NULL;
    if (on && n++ < 60) fprintf(stderr, "sb16: cmd %02x args %02x %02x %02x rate %u\n", cmd, args[0], args[1], args[2], rate);
}
static void dsp_command_done(void) {
    dsp_log();
    switch (cmd) {
    case 0x10: break;                                      /* direct 8-bit sample */
    case 0x14: case 0x91:                                  /* single-cycle 8-bit */
        bits16 = 0; stereo = 0; is_signed = 0; autoinit = 0;
        block_len = block_left = (cmd == 0x14 ? (args[0] | args[1] << 8) : block8) + 1; playing = 1; paused = 0; break;
    case 0x1c: case 0x90:                                  /* auto-init 8-bit */
        bits16 = 0; stereo = (mixer[0x0e] & 2) != 0; is_signed = 0; autoinit = 1;
        block_len = block_left = block8 + 1; playing = 1; paused = 0; break;
    case 0x40: time_constant_rate = 1; rate = 1000000 / (256 - args[0]); break;
    case 0x41: case 0x42: time_constant_rate = 0; rate = (uint32_t)args[0] << 8 | args[1]; break;
    case 0x48: block8 = args[0] | args[1] << 8; break;
    case 0xd0: case 0xd5: paused = 1; break;
    case 0xd4: case 0xd6: paused = 0; break;
    case 0xd9: case 0xda: autoinit = 0; break;
    case 0xd1: case 0xd3: break;                           /* speaker */
    case 0xd8: push(0xff); break;
    case 0xe0: push(~args[0]); break;
    case 0xe1: push(4); push(5); break;                    /* DSP 4.05: Sound Blaster 16 */
    case 0xe3: { const char *c = "COPYRIGHT (C) CREATIVE TECHNOLOGY LTD, 1992."; while (*c) push(*c++); push(0); break; }
    case 0xe4: test_reg = args[0]; break;
    case 0xe8: push(test_reg); break;
    case 0xf2: raise_irq(0); break;
    case 0xf3: raise_irq(1); break;
    default:
        if (cmd >= 0xb0 && cmd <= 0xcf) { start(cmd < 0xc0, cmd, args[0], args[1] | args[2] << 8); break; }
        if (getenv("TP_DEBUG")) fprintf(stderr, "sb16: ignored DSP command %02x\n", cmd);
    }
}
static int arg_count(uint8_t c) {
    if (c >= 0xb0 && c <= 0xcf) return 3;
    switch (c) {
    case 0x10: case 0x40: case 0xe0: case 0xe4: return 1;
    case 0x14: case 0x41: case 0x42: case 0x48: return 2;
    default: return 0;
    }
}
static void dsp_write(uint8_t v) {
    if (want) {
        args[nargs++] = v;
        if (nargs == want) { want = 0; dsp_command_done(); }
        return;
    }
    cmd = v; nargs = 0; want = arg_count(v);
    if (!want) dsp_command_done();
}

/* Ports: returns 1 if handled. */
int sb16_port_in(unsigned port, unsigned *v) {
    int handled = 1;
    pthread_mutex_lock(&lock);
    if (dma_port_in(port, v)) {}
    else switch (port) {
    case 0x224: *v = mixer_index; break;
    case 0x225:
        *v = mixer_index == 0x80 ? (mixer[0x80] ? mixer[0x80] : 0x04) : mixer_index == 0x81 ? (mixer[0x81] ? mixer[0x81] : 0x22) :
             mixer_index == 0x82 ? (irq8_pending | irq16_pending << 1) : mixer[mixer_index];
        break;
    case 0x226: *v = 0xff; break;
    case 0x22a: if (outq_n) { *v = outq[0]; memmove(outq, outq + 1, --outq_n); } else *v = 0xff; break;
    case 0x22c: *v = 0x7f; break;                           /* bit 7 clear: ready for a command */
    case 0x22e: *v = outq_n ? 0xff : 0x7f; irq8_pending = 0; break;
    case 0x22f: *v = 0xff; irq16_pending = 0; break;
    default: handled = port >= 0x220 && port <= 0x22f ? (*v = 0xff, 1) : 0;
    }
    pthread_mutex_unlock(&lock);
    return handled;
}
int sb16_port_out(unsigned port, unsigned v) {
    int handled = 1;
    pthread_mutex_lock(&lock);
    if (dma_port_out(port, v)) {}
    else switch (port) {
    case 0x224: mixer_index = v; break;
    case 0x225:
        if (getenv("TP_DEBUG")) fprintf(stderr, "sb16: mixer %02x=%02x\n", mixer_index, v);
        if (mixer_index == 0) memset(mixer, 0, sizeof mixer); else mixer[mixer_index] = v; break;
    case 0x226:
        if (v & 1) reset_high = 1;
        else if (reset_high) { reset_high = 0; dsp_reset(); }
        break;
    case 0x22c: dsp_write(v); break;
    default: handled = port >= 0x220 && port <= 0x22f;
    }
    pthread_mutex_unlock(&lock);
    return handled;
}

/* IRQ 7 requests raised since the last call (delivered by the CPU thread). */
int sb16_take_irq(void) {
    pthread_mutex_lock(&lock);
    int n = irq_raise; irq_raise = 0;
    pthread_mutex_unlock(&lock);
    return n;
}

/* DMA channels from mixer register 0x81 (as the driver configured them). A
 * 16-bit transfer without a high channel goes over the 8-bit one, a byte at a time. */
static int low_channel(void) { uint8_t m = mixer[0x81]; return m & 1 ? 0 : m & 2 ? 1 : m & 8 ? 3 : 1; }
static int high_channel(void) { uint8_t m = mixer[0x81]; return m & 0x20 ? 5 : m & 0x40 ? 6 : m & 0x80 ? 7 : -1; }
static int read_sample(uint32_t *v) {
    if (!bits16) return dma_read(low_channel(), v);
    int hc = high_channel();
    if (hc >= 0) return dma_read(hc, v);
    uint32_t lo, hi;
    if (dma_read(low_channel(), &lo) || dma_read(low_channel(), &hi)) return -1;
    *v = lo | hi << 8;
    return 0;
}
static int fetch(float *l, float *r) {
    uint32_t a, b = 0;
    if (read_sample(&a)) return -1;
    if (stereo && read_sample(&b)) return -1;
    float fa, fb;
    if (bits16) {
        fa = is_signed ? (int16_t)a / 32768.0f : ((int)a - 32768) / 32768.0f;
        fb = is_signed ? (int16_t)b / 32768.0f : ((int)b - 32768) / 32768.0f;
    } else {
        fa = is_signed ? (int8_t)a / 128.0f : ((int)a - 128) / 128.0f;
        fb = is_signed ? (int8_t)b / 128.0f : ((int)b - 128) / 128.0f;
    }
    *l = fa; *r = stereo ? fb : fa;
    block_left -= stereo ? 2 : 1;
    if ((int32_t)block_left <= 0) {
        raise_irq(bits16);
        if (autoinit) block_left = block_len; else playing = 0;
    }
    return 0;
}

static float peak;
float sb16_peak(void) { float p = peak; peak = 0; return p; }
/* Fill `frames` stereo float frames at `out_rate`; silence when idle. */
void sb16_render(float *out, int frames, double out_rate) {
    pthread_mutex_lock(&lock);
    double step = playing && !paused ? (stereo && time_constant_rate ? rate / 2.0 : rate) / out_rate : 0;
    for (int i = 0; i < frames; i++) {
        if (step > 0) {
            phase += step;
            while (phase >= 1.0 && playing) {
                phase -= 1.0;
                prev_l = hold_l; prev_r = hold_r;
                if (fetch(&hold_l, &hold_r)) { hold_l = hold_r = 0; break; }
            }
            double f = phase < 1.0 ? phase : 1.0;
            out[2 * i] = prev_l + (hold_l - prev_l) * (float)f; out[2 * i + 1] = prev_r + (hold_r - prev_r) * (float)f;
            if (hold_l > peak) peak = hold_l; else if (-hold_l > peak) peak = -hold_l;
        } else out[2 * i] = out[2 * i + 1] = 0;
        if (!playing || paused) step = 0;
    }
    pthread_mutex_unlock(&lock);
}

/* The card plays in the game's time: the CPU thread advances it (sb16_pump, at every chance, by the wall clock)
 * in steps of a fraction of a millisecond, as the hardware's DMA would. The HMI driver reads the DMA position
 * ~700 times a second and mixes just ahead of it; advanced by the audio callback instead, the position jumped
 * ~12 ms at a time, so the card read what the driver had not mixed yet (choppy, harsh) and the driver waited
 * on it. The audio output (sb16_play) plays what the pump made, ~25 ms behind. */
#define RING 16384                     /* output frames */
static float ring[2 * RING]; static unsigned r_head, r_tail; static int primed;
static pthread_mutex_t ring_lock = PTHREAD_MUTEX_INITIALIZER;
static double pumped;
static long underruns;
void sb16_pump(double t) {
    if (pumped == 0 || t < pumped || t - pumped > 0.25) { pumped = t; return; }   /* (a long stall is not replayed) */
    int frames = (int)((t - pumped) * 44100);
    if (frames < 16) return;
    if (frames > 4096) frames = 4096;
    static float tmp[2 * 4096];
    sb16_render(tmp, frames, 44100);
    pumped += frames / 44100.0;
    pthread_mutex_lock(&ring_lock);
    for (int i = 0; i < frames; i++, r_head++) { ring[(r_head % RING) * 2] = tmp[2 * i]; ring[(r_head % RING) * 2 + 1] = tmp[2 * i + 1]; }
    if (r_head - r_tail > RING) r_tail = r_head - RING;
    pthread_mutex_unlock(&ring_lock);
}
void sb16_play(float *out, int frames) {
    pthread_mutex_lock(&ring_lock);
    unsigned avail = r_head - r_tail;
    if (!primed && avail >= 1024) primed = 1;
    if (primed && avail > 4096) r_tail = r_head - 1536;   /* (far behind, the game ran ahead of the device: catch up) */
    int i = 0;
    if (primed) for (; i < frames && r_tail != r_head; i++, r_tail++) { out[2 * i] = ring[(r_tail % RING) * 2]; out[2 * i + 1] = ring[(r_tail % RING) * 2 + 1]; }
    if (i < frames) {                                    /* (ran dry: silence, and fill up again before playing on) */
        if (primed) { primed = 0; underruns++; if (getenv("TP_DEBUG")) fprintf(stderr, "sb16: output ran dry (%ld)\n", underruns); }
        for (; i < frames; i++) out[2 * i] = out[2 * i + 1] = 0;
    }
    pthread_mutex_unlock(&ring_lock);
}

void sb16_reset(void) {
    pthread_mutex_lock(&lock);
    memset(dma, 0, sizeof dma);
    for (int c = 0; c < 8; c++) dma[c].masked = 1;
    flipflop[0] = flipflop[1] = 0;
    memset(mixer, 0, sizeof mixer);
    outq_n = 0; want = nargs = 0; playing = 0; irq_raise = 0; irq8_pending = irq16_pending = 0;
    rate = 22050; block8 = 0x7ff; hold_l = hold_r = prev_l = prev_r = 0;
    pthread_mutex_unlock(&lock);
    pthread_mutex_lock(&ring_lock); r_tail = r_head; primed = 0; pumped = 0; pthread_mutex_unlock(&ring_lock);
}
