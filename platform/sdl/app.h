/* Theme Park for Linux and Windows: the SDL2 platform layer around the recompiled game code and the native DOS
 * host (runtime/host.c). It does for these systems what platform/macos/ (Cocoa) does on macOS. */
#ifndef TP_APP_H
#define TP_APP_H
#include <stdint.h>

/* settings.c: <data>/settings.ini, the same keys as the Mac app's user defaults (thread-safe). */
void settings_load(const char *path);
long settings_get(const char *key);
double settings_getf(const char *key);
const char *settings_gets(const char *key, char *buf, int n);     /* "" when unset */
void settings_set(const char *key, long value);
void settings_setf(const char *key, double value);
void settings_sets(const char *key, const char *value);            /* (SoundBank, RideTunes) */
int settings_has(const char *key);

/* launcher.c: the launch window (the Mac's launcher.m), before the game's window. 1: play, 0: quit. res_dir: the
 * program's folder (with fonts/ and art/, ending in a separator); version: the build's commit, shown and checked. */
int tp_launcher_run(const char *data_root, const char *res_dir, const char *version);

/* card.c: the controls card (the Mac's TPControlsCard) as premultiplied RGBA, pw x ph pixels at scale s (pixels per
 * point); the caller frees it. res_dir as tp_launcher_run's. */
uint8_t *tp_card_render(int pw, int ph, float s, const char *res_dir);

/* audio.c: the emulated Sound Blaster 16 and the General MIDI music through SDL audio. */
int tp_audio_start(void);
void tp_audio_stop(void);
void tp_audio_set_volumes(float sfx, float music);
#endif
