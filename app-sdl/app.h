/* Theme Park for Linux and Windows: the SDL2 platform layer around the recompiled game code and the native DOS
 * host (runtime/host.c). It does for these systems what app/ (Cocoa) does on macOS. */
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
int settings_has(const char *key);

/* audio.c: the emulated Sound Blaster 16 and the General MIDI music through SDL audio. */
int tp_audio_start(void);
void tp_audio_stop(void);
void tp_audio_set_volumes(float sfx, float music);
#endif
