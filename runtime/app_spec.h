/* The app's settings and launch window as data, one copy for every platform's app (platform/macos/launcher.m and
 * main.m, platform/sdl/): the pages and their rows (title, settings key, choices with their labels and notes), the
 * tickets on the launch window, its quick-settings tiles, the controls table (the launcher's page and the in-game card),
 * the settings' defaults and the window's words. A label, note, choice or default changed here changes on every system.
 * Keys are the Mac's user-defaults keys and the SDL app's settings.ini keys alike. Strings that name keys differ per
 * system (⌘ on the Mac, Ctrl elsewhere): app_spec.c chooses them at compile time. Plain C11, no allocation. */
#ifndef TP_APP_SPEC_H
#define TP_APP_SPEC_H

typedef struct { double value; const char *label, *note; } TpChoice;

enum {
    TP_ROW_CHOICE,      /* one of `choices`, stored as a number */
    TP_ROW_BOOL,        /* one of `choices` (values 1 and 0), stored as a boolean */
    TP_ROW_SLIDER,      /* one of `choices` on a track (volumes, panel size); `note` under it if any */
    TP_ROW_PAGE,        /* opens page `page`; its value is that page's summary, its note `note` */
    TP_ROW_FILES,       /* opens the saved parks' folder: `value` and `note` */
    TP_ROW_RIDETUNES,   /* per ride type tunes (key RideTunes: {type index: tune}): `value` when none set, `note` */
    TP_ROW_SOUNDBANK,   /* a SoundFont path or none (key SoundBank): `value` when none, `note` */
};
typedef struct {
    const char *title, *key;        /* key: NULL for pages and the folder */
    int kind;
    int n; const TpChoice *choices; /* CHOICE, BOOL, SLIDER */
    const char *note, *value;       /* PAGE, FILES, RIDETUNES, SOUNDBANK (and a SLIDER's note when its choices have none) */
    int page;                       /* PAGE: the page it opens */
} TpRow;

enum { TP_PAGE_GAME, TP_PAGE_DISPLAY, TP_PAGE_SOUND, TP_PAGE_FREE, TP_PAGE_CONTROLS, TP_PAGE_MUSIC, TP_PAGE_EFFECTS, TP_NPAGES };
typedef struct { const char *title; int parent; int n; const TpRow *const *rows; const char *footnote[2]; } TpPage;
typedef struct { const char *title; int page; unsigned char rgb[3]; } TpTicket;   /* the launch window's ride tickets */
typedef struct { const char *key; double value; int is_bool; } TpDefault;

typedef struct {
    const TpPage *pages;            /* TP_NPAGES; TP_PAGE_CONTROLS has no rows: the controls table */
    const TpTicket *tickets; int ntickets;
    const TpRow *const *tiles; int ntiles;
    const char *const (*controls)[2]; int ncontrols;
    const TpDefault *defaults; int ndefaults;
    /* the launch window's words */
    const char *presents, *credits, *tagline, *plaque, *plaque_sub, *sash, *hint_main, *hint_page;
} TpAppSpec;

const TpAppSpec *tp_app_spec(void);
/* Find a row by its key (first match), or NULL. */
const TpRow *tp_app_row(const char *key);
/* The index of `value` among a row's choices (nearest within 0.05, else 0: as unset). */
int tp_app_choice_index(const TpRow *r, double value);
/* The game's ride types and tunes (the type table at 0x1cced0): names for the Ride Tunes chooser and the Park Music
 * row. Tunes 1-17 are ride tunes, 18-22 the game's jingles; 0 none. */
enum { TP_RIDE_TYPES = 29, TP_TUNES = 22 };
const char *tp_ride_type_name(int i);
int tp_ride_type_tune(int i);
const char *tp_tune_label(int t);      /* "Ride Tune 3", "Fanfare", "None" */
const char *tp_tune_about(int t);      /* which rides play it */
#endif
