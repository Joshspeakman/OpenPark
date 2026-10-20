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
#include <math.h>
#include <strings.h>

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

static float peak; static long clipped_samples;
float sb16_peak(void) { float p = peak; peak = 0; return p; }
/* The output stage: the game's effects are 8-bit samples of 11 to 22 kHz that the HMI driver stretches to 44.1 kHz
 * by holding each value (harsh, gritty highs), and it clips where they add up. qol.c starts every effect at a quarter
 * of its volume (four at full volume fit in the driver's 16-bit sum); here that is given back in floating point and
 * a gentle low-pass (2nd order, 9 kHz) takes the stair-steps' edge off. Nothing clips here: whatever passes full
 * scale meets the app's look-ahead peak limiter (audio.m). TP_SOUND_RAW=1: the card's output as it was. */
#define OUT_GAIN 2.0f                   /* (qol.c starts every effect at a quarter, SFX_HEADROOM: one at full volume peaks at half
                                           scale here, so effects, ambience and music together seldom reach the limiter) */
static int raw_out = -1; static double lp_rate; static float lp_b0, lp_b1, lp_b2, lp_a1, lp_a2, lp_x1[2], lp_x2[2], lp_y1[2], lp_y2[2];
static float shape(float x, int ch) {
    if (raw_out) return x;
    x *= OUT_GAIN;
    float y = lp_b0 * x + lp_b1 * lp_x1[ch] + lp_b2 * lp_x2[ch] - lp_a1 * lp_y1[ch] - lp_a2 * lp_y2[ch];
    lp_x2[ch] = lp_x1[ch]; lp_x1[ch] = x; lp_y2[ch] = lp_y1[ch]; lp_y1[ch] = y;
    return y;                                       /* (past full scale: the app's peak limiter, audio.m) */
}
static void shape_setup(double out_rate) {
    if (raw_out < 0) raw_out = getenv("TP_SOUND_RAW") != NULL;
    if (out_rate == lp_rate) return;
    lp_rate = out_rate; double w = 2 * M_PI * 9000.0 / out_rate, c = cos(w), al = sin(w) / (2 * 0.7071), a0 = 1 + al;
    lp_b0 = (float)((1 - c) / 2 / a0); lp_b1 = (float)((1 - c) / a0); lp_b2 = lp_b0; lp_a1 = (float)(-2 * c / a0); lp_a2 = (float)((1 - al) / a0);
}
/* Fill `frames` stereo float frames at `out_rate`; silence when idle. */
void sb16_render(float *out, int frames, double out_rate) {
    pthread_mutex_lock(&lock);
    shape_setup(out_rate);
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
            out[2 * i] = shape(prev_l + (hold_l - prev_l) * (float)f, 0); out[2 * i + 1] = shape(prev_r + (hold_r - prev_r) * (float)f, 1);
            if (hold_l > peak) peak = hold_l; else if (-hold_l > peak) peak = -hold_l;
            if (hold_l >= 0.999f || hold_l <= -0.999f) clipped_samples++;   /* (the driver's own sum clipped) */
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
static long underruns, pump_jumps; static double pump_worst;
/* How far the output runs behind the card (frames): the card is held while the game thread cannot deliver timer
 * ticks (host.c tp_host_card_clock), for a frame's passes and its pacing wait (TP_DEBUG's card: line, hold max: the
 * longest measured); the lead covers that.
 * TP_AUDIO_LEAD=<ms> (default 60): latency against gaps. */
static unsigned ring_lead(void);
double sb16_lead_seconds(void) { return ring_lead() / 44100.0; }
static unsigned ring_lead(void) {
    static unsigned lead; if (!lead) { const char *e = getenv("TP_AUDIO_LEAD"); double ms = e ? atof(e) : 60; if (ms < 10) ms = 10; if (ms > 300) ms = 300; lead = (unsigned)(ms * 44.1); }
    return lead;
}
/* Development (TP_DEBUG): the largest step the card took in one pump, how many steps passed 5 ms (the driver's
 * mix-ahead is of that order), and the card samples at full scale since the last call. */
double sb16_pump_stats(long *jumps, long *clipped) {
    double w = pump_worst; pump_worst = 0; *jumps = pump_jumps; pump_jumps = 0;
    pthread_mutex_lock(&lock); *clipped = clipped_samples; clipped_samples = 0; pthread_mutex_unlock(&lock);
    return w;
}
/* After a stall the card goes on from t without rendering the time in between (host.c card_tick). */
void sb16_skip_to(double t) { pumped = t; }
void sb16_pump(double t) {
    if (pumped && t < pumped && pumped - t < 0.25) return;    /* (held: the card's clock waits at an overdue tick, host.c) */
    if (pumped == 0 || t < pumped || t - pumped > 0.25) { if (pumped && getenv("TP_RING_LOG")) fprintf(stderr, "sb16: stall of %.0f ms not replayed\n", (t - pumped) * 1e3); pumped = t; return; }   /* (a long stall is not replayed) */
    int frames = (int)((t - pumped) * 44100);
    if (frames < 16) return;
    if (frames > 4096) frames = 4096;
    if (frames / 44100.0 > pump_worst) pump_worst = frames / 44100.0;
    if (frames > 220) pump_jumps++;
    static float tmp[2 * 4096];
    sb16_render(tmp, frames, 44100);
    pumped += frames / 44100.0;
    pthread_mutex_lock(&ring_lock);
    for (int i = 0; i < frames; i++, r_head++) { ring[(r_head % RING) * 2] = tmp[2 * i]; ring[(r_head % RING) * 2 + 1] = tmp[2 * i + 1]; }
    if (r_head - r_tail > RING) r_tail = r_head - RING;
    pthread_mutex_unlock(&ring_lock);
}
long sb16_underruns(void) { return underruns; }
void sb16_play(float *out, int frames) {
    pthread_mutex_lock(&ring_lock);
    unsigned avail = r_head - r_tail, lead = ring_lead();
    if (lead < 2u * (unsigned)frames) lead = 2u * (unsigned)frames;   /* (an output with large buffers, Bluetooth: under two callbacks it stutters) */
    if (!primed && avail >= lead) primed = 1;
    static int rlog = -1; if (rlog < 0) rlog = getenv("TP_RING_LOG") != NULL;
    if (primed && avail > 2 * lead + 1024) { if (rlog) fprintf(stderr, "sb16: skipped ahead %u frames\n", avail - lead); r_tail = r_head - lead; }   /* (far behind, the game ran ahead of the device: catch up) */
    int i = 0;
    if (primed) for (; i < frames && r_tail != r_head; i++, r_tail++) { out[2 * i] = ring[(r_tail % RING) * 2]; out[2 * i + 1] = ring[(r_tail % RING) * 2 + 1]; }
    if (i < frames) {                                    /* (ran dry: silence, and fill up again before playing on) */
        if (primed) { primed = 0; underruns++; if (getenv("TP_DEBUG") || rlog) fprintf(stderr, "sb16: output ran dry (%ld)\n", underruns); }
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

/* The game's sound banks (SNDSb-v.DAT; table SNDSb-v.TAB: 32-byte entries, name[18], offset, 0, length; v 0..2:
 * 8-bit unsigned at 11 or 22 kHz, 16-bit signed at 44.1 kHz) end many samples with their file padding: zeros
 * (8-bit: full negative), or other bytes, full-scale steps heard as a crack at the sound's end (BUTTON3, ROLBLD,
 * DIG, ...); others stop dead (HAMMER). As the game reads a bank, each sample whose end is read gets a clean end: from
 * the first full-scale step in its last 16 bytes, silence, and the 5 ms before that faded out. TP_SOUND_RAW=1:
 * as the files are. Called by host.c after each DOS read. */
void tp_sound_bank_read(const char *path, long pos, uint8_t *buf, long n) {
    static int raw = -1; if (raw < 0) raw = getenv("TP_SOUND_RAW") != NULL;
    if (raw || n < 32 || !path) return;
    const char *b = strrchr(path, '/'); b = b ? b + 1 : path;
    size_t bl = strlen(b);
    if (bl < 11 || strncasecmp(b, "SNDS", 4) || strcasecmp(b + bl - 4, ".DAT") || b[5] != '-') return;
    int v = b[6] - '0', bps = v == 2 ? 2 : 1, rate = v == 2 ? 44100 : v == 1 ? 22050 : 11025;
    static struct { char path[1024]; int n; uint32_t off[160], len[160]; } tab[6]; static int ntab;
    int t = 0; while (t < ntab && strcmp(tab[t].path, path)) t++;
    if (t == ntab) {                                      /* (its table, once) */
        if (ntab == 6) return;
        char tp[1024]; snprintf(tp, sizeof tp, "%s", path); memcpy(tp + strlen(tp) - 3, b[bl - 3] == 'd' ? "tab" : "TAB", 3);
        FILE *f = fopen(tp, "rb"); if (!f) return;
        uint8_t e[32]; int k = 0;
        while (k < 160 && fread(e, 1, 32, f) == 32) { memcpy(&tab[t].off[k], e + 18, 4); memcpy(&tab[t].len[k], e + 26, 4); k++; }
        fclose(f); snprintf(tab[t].path, sizeof tab[t].path, "%s", path); tab[t].n = k; ntab++;
    }
    int fade = (int)(rate * 0.005), done = 0, junk = 0;
    for (int k = 1; k < tab[t].n; k++) {                  /* (entry 0: the whole bank) */
        long s = tab[t].off[k], len = tab[t].len[k], e = s + len, N = len / bps;
        if (N < 2 * fade + 32 || e - (fade + 32) * bps < pos || e > pos + n) continue;   /* (its end in this read) */
        long T = N - (fade + 32);                         /* (only its last fade + 32 samples are touched: based there, not */
        if (s + T * bps < pos) continue;                  /*  before buf where the sample began in an earlier read) */
        uint8_t *p = buf + (s + T * bps - pos);           /* sample i at p[(i - T) * bps] */
        #define SV(i) (bps == 2 ? (int)(int16_t)(p[2 * ((i) - T)] | p[2 * ((i) - T) + 1] << 8) : ((int)p[(i) - T] - 128) << 8)
        long cut = N;
        for (long i = N - 16 / bps; i < N; i++) if (abs(SV(i) - SV(i - 1)) > 16000) { cut = i; break; }
        done++; junk += cut < N;
        for (long i = cut - fade; i < N; i++) {
            int x = i < cut ? (int)((long)SV(i) * (cut - i) / fade) : 0;
            if (bps == 2) { p[2 * (i - T)] = (uint8_t)x; p[2 * (i - T) + 1] = (uint8_t)(x >> 8); } else p[i - T] = (uint8_t)((x >> 8) + 128);
        }
        #undef SV
    }
    if (done && getenv("TP_SOUND_LOG")) fprintf(stderr, "sound bank %s: %d samples' ends cleaned (%d with padding)\n", b, done, junk);
}
