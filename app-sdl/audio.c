/* Sound output: the emulated Sound Blaster 16 (runtime/sb16.c) through SDL audio, and the MPU-401 MIDI bytes
 * through a General MIDI synthesizer: FluidSynth with a SoundFont (Linux; Windows when built with it and given
 * one), else Windows' own synthesizer (winmm). Effects and music go out through a look-ahead peak limiter, as the
 * Mac app's AUPeakLimiter does (app/audio.m): together they can pass full scale, and the limiter holds the sum
 * under it instead of the output clipping. TP_SOUND_RAW=1: no limiter. */
#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "host.h"
#include "app.h"
#ifdef TP_FLUIDSYNTH
#include <fluidsynth.h>
#endif
#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#endif

#define RATE 44100
static SDL_AudioDeviceID dev;
static volatile float sfx_volume = 1.0f, music_volume = 0.8f;
static int muted, raw;
static FILE *rec, *rec_out;

/* ---- the synthesizer ---- */
#ifdef TP_FLUIDSYNTH
static fluid_settings_t *fl_settings;
static fluid_synth_t *fl;
static int fluid_open(void) {
    char bank[512]; const char *e = getenv("TP_SOUNDBANK");
    if (e && *e) snprintf(bank, sizeof bank, "%s", e); else settings_gets("SoundBank", bank, sizeof bank);
    static const char *system_banks[] = {"/usr/share/sounds/sf2/FluidR3_GM.sf2", "/usr/share/soundfonts/FluidR3_GM.sf2",
        "/usr/share/soundfonts/default.sf2", "/usr/share/sounds/sf2/default-GM.sf2", "/usr/share/sounds/sf2/TimGM6mb.sf2",
        "/usr/share/soundfonts/FluidR3_GM2-2.sf2", "/usr/local/share/soundfonts/default.sf2", NULL};
    const char *use = *bank ? bank : NULL;
#ifndef _WIN32
    for (int i = 0; !use && system_banks[i]; i++) { FILE *f = fopen(system_banks[i], "rb"); if (f) { fclose(f); use = system_banks[i]; } }
#endif
    if (!use) { fprintf(stderr, "audio: no SoundFont for the music (settings.ini SoundBank, or install fluid-soundfont-gm)\n"); return 0; }
    fl_settings = new_fluid_settings();
    fluid_settings_setnum(fl_settings, "synth.sample-rate", RATE);
    fluid_settings_setnum(fl_settings, "synth.gain", 0.5);
    fl = new_fluid_synth(fl_settings);
    if (!fl || fluid_synth_sfload(fl, use, 1) == FLUID_FAILED) {
        fprintf(stderr, "audio: SoundFont %s could not be loaded\n", use);
        if (fl) delete_fluid_synth(fl);
        delete_fluid_settings(fl_settings); fl = NULL; fl_settings = NULL;
        return 0;
    }
    fprintf(stderr, "audio: General MIDI from FluidSynth with %s\n", use);
    return 1;
}
#endif
#ifdef _WIN32
static HMIDIOUT winmm;
static int cc7[16];                 /* each channel's volume as the game set it: scaled by the music volume */
static void winmm_volume(void) {
    for (int ch = 0; ch < 16; ch++)
        midiOutShortMsg(winmm, (DWORD)(0xb0 | ch) | 7u << 8 | (DWORD)lroundf(cc7[ch] * music_volume) << 16);
}
#endif
static int synth_on;
static void synth_short(uint8_t st, uint8_t d1, uint8_t d2) {
#ifdef TP_FLUIDSYNTH
    if (fl) {
        int ch = st & 15;
        switch (st & 0xf0) {
        case 0x80: fluid_synth_noteoff(fl, ch, d1); break;
        case 0x90: fluid_synth_noteon(fl, ch, d1, d2); break;
        case 0xa0: fluid_synth_key_pressure(fl, ch, d1, d2); break;
        case 0xb0: fluid_synth_cc(fl, ch, d1, d2); break;
        case 0xc0: fluid_synth_program_change(fl, ch, d1); break;
        case 0xd0: fluid_synth_channel_pressure(fl, ch, d1); break;
        case 0xe0: fluid_synth_pitch_bend(fl, ch, d1 | d2 << 7); break;
        }
        return;
    }
#endif
#ifdef _WIN32
    if (winmm) {
        if ((st & 0xf0) == 0xb0 && d1 == 7) { cc7[st & 15] = d2; d2 = (uint8_t)lroundf(d2 * music_volume); }
        midiOutShortMsg(winmm, st | (DWORD)d1 << 8 | (DWORD)d2 << 16);
    }
#endif
    (void)st; (void)d1; (void)d2;
}
static void synth_sysex(const uint8_t *b, int n) {     /* F0 .. F7 */
#ifdef TP_FLUIDSYNTH
    if (fl) { if (n > 2) fluid_synth_sysex(fl, (const char *)b + 1, n - 2, NULL, NULL, NULL, 0); return; }
#endif
#ifdef _WIN32
    if (winmm) {
        static char buf[1024]; if (n > (int)sizeof buf) return;
        memcpy(buf, b, (size_t)n);
        MIDIHDR h; memset(&h, 0, sizeof h); h.lpData = buf; h.dwBufferLength = (DWORD)n;
        if (midiOutPrepareHeader(winmm, &h, sizeof h) != MMSYSERR_NOERROR) return;
        if (midiOutLongMsg(winmm, &h, sizeof h) == MMSYSERR_NOERROR)
            for (int i = 0; i < 200 && !(h.dwFlags & MHDR_DONE); i++) Sleep(1);
        midiOutUnprepareHeader(winmm, &h, sizeof h);
    }
#endif
    (void)b; (void)n;
}

/* MIDI byte stream -> messages (running status; SysEx passed whole), as app/audio.m. */
static uint8_t msg[3], status_byte; static int have, need;
static uint8_t sysex[1024]; static int sysex_n = -1;
static int data_len(uint8_t s) {
    switch (s & 0xf0) { case 0xc0: case 0xd0: return 1; case 0xf0: return s == 0xf2 ? 2 : s == 0xf1 || s == 0xf3 ? 1 : 0; default: return 2; }
}
void tp_audio_midi(uint8_t b) {
    if (!synth_on) return;
    if (b >= 0xf8) return;                                  /* real-time messages */
    if (sysex_n >= 0) {
        if (sysex_n < (int)sizeof sysex) sysex[sysex_n++] = b;
        if (b == 0xf7) { synth_sysex(sysex, sysex_n); sysex_n = -1; }
        return;
    }
    if (b == 0xf0) { sysex[0] = b; sysex_n = 1; return; }
    if (b & 0x80) { status_byte = b; have = 0; need = data_len(b); if (!need) status_byte = 0; return; }
    if (!status_byte) return;
    msg[have++] = b;
    if (have < need) return;
    synth_short(status_byte, msg[0], need > 1 ? msg[1] : 0);
    have = 0;
}
void tp_audio_midi_reset(void) {
    sysex_n = -1; status_byte = 0; have = 0;
    if (!synth_on) return;
    for (int ch = 0; ch < 16; ch++) {
        synth_short((uint8_t)(0xb0 | ch), 120, 0);   /* all sound off */
        synth_short((uint8_t)(0xb0 | ch), 123, 0);   /* all notes off */
        synth_short((uint8_t)(0xb0 | ch), 121, 0);   /* reset controllers */
    }
}

/* ---- the limiter: 5 ms look-ahead, 80 ms release, ceiling 0.98 ---- */
#define LOOK 220
static float delay_line[LOOK][2], gains[LOOK]; static int dpos; static float gain = 1;
static void limit(float *buf, int frames) {
    static float attack, release;
    if (!attack) { attack = 1.0f - expf(-5.0f / LOOK); release = 1.0f - expf(-1.0f / (0.08f * RATE)); for (int i = 0; i < LOOK; i++) gains[i] = 1; }
    const float ceiling = 0.98f;
    for (int i = 0; i < frames; i++) {
        float l = buf[2 * i], r = buf[2 * i + 1], peak = fmaxf(fabsf(l), fabsf(r));
        gains[dpos] = peak > ceiling ? ceiling / peak : 1.0f;
        float want = 1.0f; for (int k = 0; k < LOOK; k++) if (gains[k] < want) want = gains[k];
        gain += (want - gain) * (want < gain ? attack : release);
        float dl = delay_line[dpos][0], dr = delay_line[dpos][1];
        delay_line[dpos][0] = l; delay_line[dpos][1] = r;
        dpos = (dpos + 1) % LOOK;
        dl *= gain; dr *= gain;
        buf[2 * i] = dl > 1 ? 1 : dl < -1 ? -1 : dl;
        buf[2 * i + 1] = dr > 1 ? 1 : dr < -1 ? -1 : dr;
    }
}

static void render(void *ud, Uint8 *stream, int len) {
    (void)ud;
    float *out = (float *)stream; int frames = len / (int)(2 * sizeof(float));
    static float buf[2 * 4096], mus[2 * 4096];
    for (int done = 0; done < frames; ) {
        int n = frames - done > 4096 ? 4096 : frames - done;
        sb16_play(buf, n);                     /* (made in the game's time by sb16_pump) */
        if (rec) { fwrite(buf, sizeof(float), 2 * (size_t)n, rec); fflush(rec); }
        float sv = sfx_volume, mv = music_volume;
        for (int i = 0; i < 2 * n; i++) buf[i] *= sv;
#ifdef TP_FLUIDSYNTH
        if (fl) { fluid_synth_write_float(fl, n, mus, 0, 2, mus, 1, 2); for (int i = 0; i < 2 * n; i++) buf[i] += mus[i] * mv; }
#endif
        (void)mus; (void)mv;
        if (!raw) limit(buf, n);
        if (rec_out) { fwrite(buf, sizeof(float), 2 * (size_t)n, rec_out); fflush(rec_out); }
        if (muted) memset(buf, 0, sizeof(float) * 2 * (size_t)n);    /* (automated tests: the whole path, silent) */
        memcpy(out + 2 * done, buf, sizeof(float) * 2 * (size_t)n);
        done += n;
    }
}

void tp_audio_set_volumes(float sfx, float music) {
    sfx_volume = sfx; music_volume = music;
#ifdef _WIN32
    if (winmm) winmm_volume();
#endif
}

int tp_audio_start(void) {
    if (dev) return 0;
    muted = getenv("W32_AUDIO_MUTE") || getenv("TP_MUTE");
    raw = getenv("TP_SOUND_RAW") != NULL;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO)) { fprintf(stderr, "audio: %s\n", SDL_GetError()); return -1; }
    SDL_AudioSpec want = {0}, got;
    want.freq = RATE; want.format = AUDIO_F32SYS; want.channels = 2; want.samples = 1024; want.callback = render;
    dev = SDL_OpenAudioDevice(NULL, 0, &want, &got, 0);       /* (SDL converts to what the device takes) */
    if (!dev) { fprintf(stderr, "audio: %s\n", SDL_GetError()); return -1; }
    const char *p;
    if ((p = getenv("TP_AUDIO_REC"))) rec = fopen(p, "wb");          /* (development: the card's output, 44.1 kHz stereo float) */
    if ((p = getenv("TP_AUDIO_REC_OUT"))) rec_out = fopen(p, "wb");  /* (what goes out) */
    const char *how = "off";
#ifdef TP_FLUIDSYNTH
    if (fluid_open()) how = "FluidSynth";
#endif
#ifdef _WIN32
    if (!synth_on && strcmp(how, "FluidSynth")) {
        for (int ch = 0; ch < 16; ch++) cc7[ch] = 100;
        if (midiOutOpen(&winmm, MIDI_MAPPER, 0, 0, CALLBACK_NULL) == MMSYSERR_NOERROR) { how = "Windows (winmm)"; winmm_volume(); }
        else winmm = NULL;
    }
#endif
    synth_on = strcmp(how, "off") != 0;
    SDL_PauseAudioDevice(dev, 0);
    tp_host_audio_attached(1);
    fprintf(stderr, "audio: output %d Hz (device %d Hz), General MIDI synth %s\n", RATE, got.freq, how);
    return 0;
}

void tp_audio_stop(void) {
    if (!dev) return;
    SDL_CloseAudioDevice(dev); dev = 0;
    synth_on = 0;
#ifdef TP_FLUIDSYNTH
    if (fl) { delete_fluid_synth(fl); delete_fluid_settings(fl_settings); fl = NULL; fl_settings = NULL; }
#endif
#ifdef _WIN32
    if (winmm) { midiOutReset(winmm); midiOutClose(winmm); winmm = NULL; }
#endif
}
