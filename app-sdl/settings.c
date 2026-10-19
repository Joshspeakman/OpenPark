/* The app's settings: <data>/settings.ini, "Key = value" lines. The keys and defaults are the Mac app's (its user
 * defaults), so the game's EXTRAS menu (qol.c) and the C modules read the same names on every system. Test runs
 * (DS_TEST) keep their changes in memory: the player's file is never written by them. */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app.h"

typedef struct { char key[48]; char value[464]; } Entry;
static Entry entries[128]; static int n_entries;
static char file[1024];
static SDL_mutex *lock;

/* What each setting does, written into a new settings.ini. */
static const struct { const char *key, *value, *note; } defaults[] = {
    {"Cycles", "15000", "game speed: emulated thousands of instructions a second (15000 = the original; EXTRAS menu)"},
    {"PlayIntro", "1", "play the intro"},
    {"Language", "0", "0 English, 1 French, 2 German, 3 Italian, 4 Spanish (the game's own -l option)"},
    {"StartFullScreen", "0", "start in full screen (F11 or Alt+Enter switches)"},
    {"StartHiRes", "0", "start the park in the game's 640x480 mode (R in the park switches)"},
    {"Widescreen", "1", "a wider window shows more of the park"},
    {"Stretch", "0", "stretch the game's 4:3 picture to the window"},
    {"SmoothPixels", "0", "0 sharp pixels, 1 smooth, 2 CRT scanlines (EXTRAS menu)"},
    {"ShowMacCursor", "0", "show the system pointer over the game instead of the game's own"},
    {"PanelScale", "0.75", "size of the panel in the widescreen park view, 0.5 .. 2 (EXTRAS menu)"},
    {"NativePasses", "1", "zoomed drawing: 1 native park passes, 0 the game's"},
    {"SteadyPacing", "1", "release park frames at a steady rate (EXTRAS menu)"},
    {"Sound", "1", "sound on"},
    {"SoundVolume", "1.0", "effects, 0 .. 1"},
    {"MusicVolume", "0.8", "music, 0 .. 1 (EXTRAS menu)"},
    {"GameSoundVolume", "1.0", "park effects, 0 .. 1 (EXTRAS menu)"},
    {"SystemSoundVolume", "1.0", "interface sounds, 0 .. 1 (EXTRAS menu)"},
    {"EwVolume", "1.0", "\"ew\" from sick visitors, 0 .. 1 (EXTRAS menu)"},
    {"PurchaseVolume", "1.0", "purchase sounds, 0 .. 1 (EXTRAS menu)"},
    {"GrossOutGap", "0", "seconds at least between \"ew\" sounds (0: as the game)"},
    {"PurchaseGap", "0", "seconds at least between purchase sounds (0: as the game)"},
    {"RideMusic", "1", "rides play their tunes"},
    {"ParkTune", "0", "a park tune (0: none)"},
    {"RideTunes", "", "a tune per ride type: type:tune,type:tune"},
    {"SoundBank", "", "a General MIDI SoundFont (.sf2) for the music (Linux: else the system's default)"},
    {"FreeMapSize", "64", "Free Mode map size, 64 or 128"},
    {"FreeRules", "0", "Free Mode rules: 0 sandbox, 1 endless (difficulty rises month by month), 2 true freeplay"},
    {"FreeRamp", "60", "endless: months until the difficulty is full"},
    {"FreeVisitors", "0", "Free Mode: visitors arriving (0: as the game)"},
    {"FreeBus", "0", "Free Mode: the visitors' bus (0: as the game)"},
    {"WalkRows", "450", "Visitor View: rows drawn (150 .. 1200)"},
    {"DebugFPS", "0", "show frame rates (Ctrl+Alt+F)"},
    {"DebugGrid", "0", "show the park view's section grid (Ctrl+Alt+G)"},
};

static Entry *find(const char *key) {
    for (int i = 0; i < n_entries; i++) if (!strcmp(entries[i].key, key)) return &entries[i];
    return NULL;
}
static void put(const char *key, const char *value) {
    Entry *e = find(key);
    if (!e) { if (n_entries == (int)(sizeof entries / sizeof *entries)) return; e = &entries[n_entries++]; snprintf(e->key, sizeof e->key, "%s", key); }
    snprintf(e->value, sizeof e->value, "%s", value);
}
static char *trim(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0;
    return s;
}
/* The whole file again, the notes kept, every setting in it. */
static void save(void) {
    if (!*file || getenv("DS_TEST")) return;
    char tmp[1040]; snprintf(tmp, sizeof tmp, "%s.new", file);
    FILE *f = fopen(tmp, "w"); if (!f) return;
    fprintf(f, "# Theme Park settings (Key = value). Read when the game starts; the EXTRAS menu writes some of them.\n");
    for (int i = 0; i < n_entries; i++) {
        const char *note = NULL;
        for (size_t d = 0; d < sizeof defaults / sizeof *defaults; d++) if (!strcmp(defaults[d].key, entries[i].key)) note = defaults[d].note;
        if (note) fprintf(f, "\n# %s\n", note);
        fprintf(f, "%s = %s\n", entries[i].key, entries[i].value);
    }
    fclose(f);
    remove(file);          /* (Windows: rename does not replace) */
    rename(tmp, file);
}
void settings_load(const char *path) {
    if (!lock) lock = SDL_CreateMutex();
    SDL_LockMutex(lock);
    snprintf(file, sizeof file, "%s", path ? path : "");
    n_entries = 0;
    for (size_t d = 0; d < sizeof defaults / sizeof *defaults; d++) put(defaults[d].key, defaults[d].value);
    FILE *f = *file ? fopen(file, "r") : NULL;
    int existed = f != NULL;
    if (f) {
        char line[600];
        while (fgets(line, sizeof line, f)) {
            char *s = trim(line), *eq = strchr(s, '=');
            if (!*s || *s == '#' || *s == ';' || *s == '[' || !eq) continue;
            *eq = 0; put(trim(s), trim(eq + 1));
        }
        fclose(f);
    }
    if (!existed) save();
    SDL_UnlockMutex(lock);
}
const char *settings_gets(const char *key, char *buf, int n) {
    SDL_LockMutex(lock); Entry *e = find(key); snprintf(buf, (size_t)n, "%s", e ? e->value : ""); SDL_UnlockMutex(lock);
    return buf;
}
int settings_has(const char *key) { char b[8]; settings_gets(key, b, sizeof b); return *b != 0; }
long settings_get(const char *key) { char b[64]; return (long)strtod(settings_gets(key, b, sizeof b), NULL); }
double settings_getf(const char *key) { char b[64]; return strtod(settings_gets(key, b, sizeof b), NULL); }
void settings_set(const char *key, long value) {
    char v[32]; snprintf(v, sizeof v, "%ld", value);
    SDL_LockMutex(lock); put(key, v); save(); SDL_UnlockMutex(lock);
}
void settings_setf(const char *key, double value) {
    char v[32]; snprintf(v, sizeof v, "%g", value);
    SDL_LockMutex(lock); put(key, v); save(); SDL_UnlockMutex(lock);
}
