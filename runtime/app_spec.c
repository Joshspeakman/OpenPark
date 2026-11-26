/* runtime/app_spec.h: the settings, pages, tickets, tiles, controls and defaults (from platform/macos/launcher.m as
 * of 2026-11-25, the "Midway Marquee" launch window). Only here; every platform's app reads it. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "app_spec.h"

#ifdef __APPLE__
#define CMD "⌘"
#define FULL_KEY "⌃⌘F"
#define SYS_PTR "The Mac's"
#define SYNTH_NOTE "The Mac's synthesizer; → picks a SoundFont"
#elif defined(_WIN32)
#define CMD "Ctrl+"
#define FULL_KEY "F11"
#define SYS_PTR "Windows'"
#define SYNTH_NOTE "Windows' own synthesizer; → picks a SoundFont"
#else
#define CMD "Ctrl+"
#define FULL_KEY "F11"
#define SYS_PTR "The system's"
#define SYNTH_NOTE "FluidSynth with the system's SoundFont; → picks another"
#endif
#define N(a) ((int)(sizeof a / sizeof a[0]))

/* ---- Game ---- */
static const TpChoice c_speed[] = {{10000, "Original", "Exactly as GOG's DOSBox runs it"}, {12500, "1.25×", "A quarter faster than GOG's DOSBox"},
    {15000, "1.5×", "Half again as fast as GOG's DOSBox"}, {20000, "2×", "Twice GOG's DOSBox"}, {40000, "4×", "Four times: the seasons fly by"}};
static const TpChoice c_intro[] = {{1, "Play", "The 1994 intro movie; Escape skips it"}, {0, "Skip", "Straight to the title screen"}};
static const TpChoice c_lang[] = {{0, "English", "The game's text and advisors"}, {1, "Deutsch", "Texte und Berater des Spiels"},
    {2, "Italiano", "Testi e consiglieri del gioco"}, {3, "Español", "Textos y asesores del juego"}, {4, "Français", "Textes et conseillers du jeu"}};
static const TpChoice c_pacing[] = {{1, "Steady", "Every frame the same length, in step with the display"},
    {0, "Like DOSBox", "The game's speed follows how busy the screen is, as in 1994"}};
static const TpRow r_speed = {"Game Speed", "Cycles", TP_ROW_CHOICE, N(c_speed), c_speed};
static const TpRow r_intro = {"Intro", "PlayIntro", TP_ROW_BOOL, N(c_intro), c_intro};
static const TpRow r_lang = {"Language", "Language", TP_ROW_CHOICE, N(c_lang), c_lang};
static const TpRow r_pacing = {"Frame Pacing", "SteadyPacing", TP_ROW_BOOL, N(c_pacing), c_pacing};
static const TpRow r_files = {"Saved Parks", NULL, TP_ROW_FILES, 0, NULL, "Your saved parks' folder", "Show in Finder"};

/* ---- Display ---- */
static const TpChoice c_screen[] = {{0, "Window", "A window you can resize; " FULL_KEY " switches"}, {1, "Full Screen", "The whole display; " FULL_KEY " switches"}};
static const TpChoice c_start[] = {{0, "320 × 200", "The game's usual screen; R in the park switches"}, {1, "640 × 480", "The game's SVGA mode: more of the park at once"}};
static const TpChoice c_aspect[] = {{0, "4:3", "The shape a 1994 monitor gave it"}, {1, "Fill the Window", "Stretched to the window's shape"}};
static const TpChoice c_wide[] = {{1, "More Park", "The park fills the window; the panel stays centred"}, {0, "Black Bars", "The game's 4:3 picture, as on a CRT"}};
static const TpChoice c_panel[] = {{0.5, "0.5×", "The panel and the advisor half size, more park in view (wide windows)"},
    {0.75, "0.75×", "The panel and the advisor three quarters size (wide windows)"}, {1.0, "The Game's", "The panel and the advisor as drawn in 1994"},
    {1.25, "1.25×", "The panel and the advisor a quarter larger (wide windows)"}, {1.5, "1.5×", "The panel and the advisor half as large again (wide windows)"},
    {1.75, "1.75×", "The panel and the advisor 1.75 times as large (wide windows)"}, {2.0, "2×", "The panel and the advisor twice as large (wide windows)"}};
static const TpChoice c_pixels[] = {{0, "Sharp", "Square pixels, as the game drew them"}, {1, "Smooth", "Softened as they are scaled up"},
    {2, "CRT", "Soft, with a 1994 monitor's scanlines"}};
static const TpChoice c_pointer[] = {{0, "The Game's", SYS_PTR " pointer hides over the game"}, {1, "Both", SYS_PTR " pointer stays visible too"}};
static const TpChoice c_zoomed[] = {{1, "Native", "Zoomed-out views drawn by the new engine: faster"}, {0, "The Game's", "Zoomed-out views drawn by the game's own code"}};
static const TpChoice c_videos[] = {{0, "PC", "The PC's ride videos only"}, {1, "PC + 3DO", "The PC's, and the 3DO's for Space Shuttle and Tree House"},
    {2, "3DO", "The 3DO's in full colour, the PC's for the rest"}};
static const TpRow r_screen = {"Screen", "StartFullScreen", TP_ROW_BOOL, N(c_screen), c_screen};
static const TpRow r_start = {"Start In", "StartHiRes", TP_ROW_BOOL, N(c_start), c_start};
static const TpRow r_aspect = {"Aspect", "Stretch", TP_ROW_BOOL, N(c_aspect), c_aspect};
static const TpRow r_wide = {"Wide Windows", "Widescreen", TP_ROW_BOOL, N(c_wide), c_wide};
static const TpRow r_panel = {"Panel Size", "PanelScale", TP_ROW_SLIDER, N(c_panel), c_panel};
static const TpRow r_pixels = {"Pixels", "SmoothPixels", TP_ROW_CHOICE, N(c_pixels), c_pixels};
static const TpRow r_pointer = {"Mouse Pointer", "ShowMacCursor", TP_ROW_BOOL, N(c_pointer), c_pointer};
static const TpRow r_zoomed = {"Zoomed Drawing", "NativePasses", TP_ROW_BOOL, N(c_zoomed), c_zoomed};
static const TpRow r_videos = {"Ride Videos", "RideVideos", TP_ROW_CHOICE, N(c_videos), c_videos};

/* ---- Sound, Music, Effects ---- */
#define VOL(n) {n / 10.0, #n "0%", ""}
static const TpChoice c_vol[] = {{0, "Off", ""}, VOL(1), VOL(2), VOL(3), VOL(4), VOL(5), VOL(6), VOL(7), VOL(8), VOL(9), {1.0, "100%", ""}};
static const TpChoice c_sound[] = {{1, "On", "Sound Blaster effects and General MIDI music"}, {0, "Off", "The park is silent"}};
static const TpChoice c_musictype[] = {{0, "General MIDI", "The game's MIDI tunes, as on the PC"},
    {1, "Amiga CD32", "The CD32 disc's own tracks (tools/extras.py imports them)"}, {2, "3DO Recorded", "The 3DO's recordings of the PC tunes (tools/extras.py imports them)"}};
static const TpChoice c_ridemusic[] = {{1, "On", "Running rides on screen play their tunes"}, {0, "Off", "No ride tunes (the game's fanfares still play)"}};
static const TpChoice c_ew[] = {{0, "Off", "Visitors keep their disgust to themselves"}, {0.25, "Quarter", "The \"ew\" at a quarter of the game sounds"},
    {0.5, "Half", "The \"ew\" at half the game sounds"}, {0.75, "Three Quarters", "The \"ew\" at three quarters of the game sounds"},
    {1.0, "Full", "The \"ew\" as loud as the other game sounds"}};
static const TpChoice c_till[] = {{0, "Off", "Visitors pay quietly"}, {0.25, "Quarter", "The till at a quarter of the game sounds"},
    {0.5, "Half", "The till at half the game sounds"}, {0.75, "Three Quarters", "The till at three quarters of the game sounds"},
    {1.0, "Full", "The till as loud as the other game sounds"}};
static const TpChoice c_ewgap[] = {{0, "Every Time", "The \"ew\" each time a visitor sees a mess, as in 1994"}, {5, "Every 5 s", "At most one \"ew\" every 5 seconds"},
    {15, "Every 15 s", "At most one \"ew\" every 15 seconds"}, {30, "Every 30 s", "At most one \"ew\" every 30 seconds"}, {-1, "Off", "Visitors keep their disgust to themselves"}};
static const TpChoice c_tillgap[] = {{0, "Every Time", "The till rings for every purchase on screen"}, {5, "Every 5 s", "At most one till every 5 seconds"},
    {15, "Every 15 s", "At most one till every 15 seconds"}, {30, "Every 30 s", "At most one till every 30 seconds"}, {-1, "Off", "Visitors pay quietly"}};
static TpChoice c_parktune[1 + TP_TUNES];                  /* (filled from the tune tables: tp_app_spec) */
static const TpRow r_sound = {"Sound", "Sound", TP_ROW_BOOL, N(c_sound), c_sound};
static const TpRow r_music = {"Music Volume", "MusicVolume", TP_ROW_SLIDER, N(c_vol), c_vol};
static const TpRow r_effects = {"Effects Volume", "SoundVolume", TP_ROW_SLIDER, N(c_vol), c_vol};
static const TpRow r_musicpage = {"Music Options", NULL, TP_ROW_PAGE, 0, NULL, "Music type, instruments, ride and park tunes", NULL, TP_PAGE_MUSIC};
static const TpRow r_effectpage = {"Effect Options", NULL, TP_ROW_PAGE, 0, NULL, "Game and system sounds, the ew and the till", NULL, TP_PAGE_EFFECTS};
static const TpRow r_musictype = {"Music Type", "MusicType", TP_ROW_CHOICE, N(c_musictype), c_musictype};
static const TpRow r_soundbank = {"Music Instruments", "SoundBank", TP_ROW_SOUNDBANK, 0, NULL, SYNTH_NOTE, "Built-in"};
static const TpRow r_ridemusic = {"Ride Music", "RideMusic", TP_ROW_BOOL, N(c_ridemusic), c_ridemusic};
static const TpRow r_ridetunes = {"Ride Tunes", "RideTunes", TP_ROW_RIDETUNES, 0, NULL, "Each ride plays its own tune; → chooses per ride", "The Game's"};
static const TpRow r_parktune = {"Park Music", "ParkTune", TP_ROW_CHOICE, N(c_parktune), c_parktune};
static const TpRow r_gamesnd = {"Game Sounds", "GameSoundVolume", TP_ROW_SLIDER, N(c_vol), c_vol};
static const TpRow r_syssnd = {"System Sounds", "SystemSoundVolume", TP_ROW_SLIDER, N(c_vol), c_vol};
static const TpRow r_ewvol = {"Ew Volume", "EwVolume", TP_ROW_SLIDER, N(c_ew), c_ew};
static const TpRow r_ewgap = {"Ew How Often", "GrossOutGap", TP_ROW_CHOICE, N(c_ewgap), c_ewgap};
static const TpRow r_tillvol = {"Till Volume", "PurchaseVolume", TP_ROW_SLIDER, N(c_till), c_till};
static const TpRow r_tillgap = {"Till How Often", "PurchaseGap", TP_ROW_CHOICE, N(c_tillgap), c_tillgap};

/* ---- Free Mode (sandbox parks) ---- */
static const TpChoice c_rules[] = {{0, "Sandbox", "The game's sandbox, at the settings you choose"},
    {1, "Endless", "Starts as easy as the first parks and grows to the hardest; research as normal"},
    {2, "True Freeplay", "Every ride and shop researched, and money that never runs out"}};
static const TpChoice c_ramp[] = {{36, "3 Years", "Endless parks reach the hardest in three game years"},
    {60, "5 Years", "Endless parks reach the hardest in five game years"}, {120, "10 Years", "Endless parks reach the hardest in ten game years"}};
static const TpChoice c_visitors[] = {{0, "The Game's", "800 in a 64 × 64 park, 10,000 in a 128 × 128 one"},
    {1000, "1,000", "Up to 1,000, about all a 64 × 64 park can hold"}, {2500, "2,500", "Up to 2,500 in a 128 × 128 park (1,000 in a 64 × 64 one)"},
    {5000, "5,000", "Up to 5,000 in a 128 × 128 park (1,000 in a 64 × 64 one)"}, {10000, "10,000", "Up to 10,000 in a 128 × 128 park (1,000 in a 64 × 64 one)"}};
static const TpChoice c_mapsize[] = {{64, "64 × 64", "The game's park size"}, {128, "128 × 128", "4× the land; buses bring 4× the visitors, who stay twice as long"}};
static const TpChoice c_bus[] = {{0, "The Game's", "20 to 150 a bus, growing with the park"}, {60, "60", "At least 60 visitors a bus"},
    {100, "100", "At least 100 visitors a bus"}, {150, "150", "At least 150 visitors a bus"}, {255, "255", "Up to 255 visitors a bus, the most it can carry"}};
static const TpRow r_rules = {"Rules", "FreeRules", TP_ROW_CHOICE, N(c_rules), c_rules};
static const TpRow r_ramp = {"Endless Ramp", "FreeRamp", TP_ROW_CHOICE, N(c_ramp), c_ramp};
static const TpRow r_visitors = {"Visitors", "FreeVisitors", TP_ROW_CHOICE, N(c_visitors), c_visitors};
static const TpRow r_mapsize = {"Map Size", "FreeMapSize", TP_ROW_CHOICE, N(c_mapsize), c_mapsize};
static const TpRow r_bus = {"Bus Size", "FreeBus", TP_ROW_CHOICE, N(c_bus), c_bus};

/* ---- pages, tickets, tiles ---- */
static const TpRow *const p_game[] = {&r_speed, &r_intro, &r_lang, &r_pacing, &r_files};
static const TpRow *const p_display[] = {&r_screen, &r_start, &r_aspect, &r_wide, &r_panel, &r_pixels, &r_pointer, &r_zoomed, &r_videos};
static const TpRow *const p_sound[] = {&r_sound, &r_music, &r_effects, &r_musicpage, &r_effectpage};
static const TpRow *const p_free[] = {&r_rules, &r_ramp, &r_visitors, &r_mapsize, &r_bus};
static const TpRow *const p_music[] = {&r_musictype, &r_soundbank, &r_ridemusic, &r_ridetunes, &r_parktune};
static const TpRow *const p_effects[] = {&r_gamesnd, &r_syssnd, &r_ewvol, &r_ewgap, &r_tillvol, &r_tillgap};
static const TpPage pages[TP_NPAGES] = {
    {"Game", -1, N(p_game), p_game},
    {"Display", -1, N(p_display), p_display},
    {"Sound", -1, N(p_sound), p_sound},
    {"Free Mode", -1, N(p_free), p_free, {"For new sandbox parks (Sim Level Sandbox); other parks play by the game's rules.",
                                          "Saved parks keep the size and rules they were started with."}},
    {"Controls", -1, 0, NULL},
    {"Music", TP_PAGE_SOUND, N(p_music), p_music},
    {"Effects", TP_PAGE_SOUND, N(p_effects), p_effects}};
static const TpTicket tickets[] = {{"Game", TP_PAGE_GAME, {206, 44, 48}}, {"Display", TP_PAGE_DISPLAY, {38, 112, 212}},
    {"Sound", TP_PAGE_SOUND, {232, 164, 26}}, {"Free Mode", TP_PAGE_FREE, {44, 150, 84}}, {"Controls", TP_PAGE_CONTROLS, {128, 74, 196}}};
static const TpRow *const tiles[] = {&r_screen, &r_start, &r_speed, &r_pixels};

/* ---- the controls table (the launch window's Controls page and the in-game card) ---- */
static const char *const controls[][2] = {
#ifdef __APPLE__
    {"Click", "Select, build, place"}, {"Right- or Control-click", "Cancel, rotate, close panels"},
    {"Pinch or ⌘-scroll", "Zoom the park in and out"}, {"⌘=  ⌘−  ⌘0", "Zoom in, zoom out, actual size"},
    {"Scroll, arrows, drag", "Move the view (middle- or Option-drag)"},
#else
    {"Click", "Select, build, place"}, {"Right-click", "Cancel, rotate, close panels"},
    {"Pinch or Ctrl-scroll", "Zoom the park in and out"}, {"Ctrl+=  Ctrl+−  Ctrl+0", "Zoom in, zoom out, actual size"},
    {"Scroll, arrows, drag", "Move the view (middle- or Alt-drag)"},
#endif
    {"Hold Tab", "Fast forward"}, {"R in the park", "The game's 640 × 480 mode, and back"}, {CMD "E", "Save a picture of the whole park"},
    {"Game controller", "Sticks move; A, B click; shoulders zoom"}, {"Hold View + Menu", "Quit from a game controller (one second)"},
    {"Escape", "Skips the intro"}, {FULL_KEY, "Full screen"}, {CMD "/", "This card, in the game"}, {CMD "Q", "Quit (save in the game first)"}};

/* ---- defaults: what the apps register (keys not here start at their first choice) ---- */
static const TpDefault defaults[] = {
    {"SoundVolume", 1.0}, {"MusicVolume", 0.8}, {"MusicType", 0}, {"RideVideos", 1}, {"Sound", 1, 1}, {"Cycles", 15000}, {"PlayIntro", 1, 1},
    {"Widescreen", 1, 1}, {"NativePasses", 1, 1}, {"GrossOutGap", 0}, {"PurchaseGap", 0}, {"RideMusic", 1, 1}, {"ParkTune", 0},
    {"FreeVisitors", 0}, {"FreeBus", 0}, {"FreeMapSize", 64}, {"FreeRules", 0}, {"FreeRamp", 60}, {"SteadyPacing", 1, 1}, {"PanelScale", 0.75},
    {"GameSoundVolume", 1.0}, {"SystemSoundVolume", 1.0}, {"EwVolume", 1.0}, {"PurchaseVolume", 1.0}};

/* ---- the game's ride types and tunes ---- */
static const char *const ride_types[TP_RIDE_TYPES] = {"Flight Sim", "Tree House", "Pirate Boat", "Plane Flyer", "Maze", "Ghost House",
    "Haunted House", "Rubber Tubing", "Race Car Ride", "Roller Coaster", "Loop the Loop", "Cork Screw", "Big Wheel",
    "Merry-Go-Round", "Bouncy Castle", "Space Shuttle", "Big Dipper", "Water Splash", "Monorail", "Snakes and Ladders",
    "Observation Tower", "Parasol Chairs", "Cowboy Acts", "Clown Acts", "Dolphin Acts", "Medieval Acts", "Super Spinner",
    "Observatory", "Planet Rocket"};
static const int ride_tune[TP_RIDE_TYPES] = {13, 5, 6, 12, 5, 4, 4, 9, 15, 2, 0, 0, 1, 1, 15, 13, 14, 0, 17, 11, 12, 17, 7, 8, 16, 10, 12, 3, 1};
const char *tp_ride_type_name(int i) { return i >= 0 && i < TP_RIDE_TYPES ? ride_types[i] : ""; }
int tp_ride_type_tune(int i) { return i >= 0 && i < TP_RIDE_TYPES ? ride_tune[i] : 0; }
const char *tp_tune_label(int t) {
    static const char *const jingles[5] = {"Heartbeat", "Fanfare", "Takeover", "Winner", "Funeral"};
    static char names[18][16];
    if (t <= 0) return "None";
    if (t > 17) return t <= TP_TUNES ? jingles[t - 18] : "";
    if (!names[t][0]) snprintf(names[t], sizeof names[t], "Ride Tune %d", t);
    return names[t];
}
const char *tp_tune_about(int t) {
    static char about[TP_TUNES + 1][400];
    if (t <= 0) return "Quiet where no ride's music plays";
    if (t > 17) return "One of the game's jingles, played on and on";
    if (!about[t][0]) {
        char *o = about[t]; size_t left = sizeof about[t]; int n = 0;
        for (int i = 0; i < TP_RIDE_TYPES; i++) if (ride_tune[i] == t) {
            int k = snprintf(o, left, "%s%s", n++ ? ", " : "As played by ", ride_types[i]); if (k < 0 || (size_t)k >= left) break; o += k; left -= (size_t)k; }
        if (!n) snprintf(about[t], sizeof about[t], "A tune no ride plays in the game");
    }
    return about[t];
}

const TpAppSpec *tp_app_spec(void) {
    static TpAppSpec s; static int ready;
    if (!ready) {                                   /* (single-threaded first call: the apps' main threads) */
        for (int t = 0; t <= TP_TUNES; t++) c_parktune[t] = (TpChoice){t, tp_tune_label(t), tp_tune_about(t)};
        s = (TpAppSpec){pages, tickets, N(tickets), tiles, N(tiles), controls, N(controls), defaults, N(defaults),
            "BULLFROG PRODUCTIONS PRESENTS",
#ifdef __APPLE__
            "BULLFROG PRODUCTIONS  ·  1994  ·  NATIVE FOR APPLE SILICON",
#elif defined(_WIN32)
            "BULLFROG PRODUCTIONS  ·  1994  ·  NATIVE FOR WINDOWS",
#else
            "BULLFROG PRODUCTIONS  ·  1994  ·  NATIVE FOR LINUX",
#endif
            "Build the rides, hire the staff, set the prices: and keep\nthe crowds screaming for the right reasons.",
            "LOOP THE LOOP", "Hold on tight. Keep your burger down.", "NOW OPEN",
            "Click a ticket or a tile  ·  arrows move  ·  Return chooses  ·  Esc quits",
            "Click the arrows or a row  ·  ↑ ↓ choose  ·  ← → change  ·  Esc goes back"};
        ready = 1;
    }
    return &s;
}
const TpRow *tp_app_row(const char *key) {
    const TpAppSpec *s = tp_app_spec();
    for (int p = 0; p < TP_NPAGES; p++) for (int k = 0; k < s->pages[p].n; k++)
        if (s->pages[p].rows[k]->key && !strcmp(s->pages[p].rows[k]->key, key)) return s->pages[p].rows[k];
    return NULL;
}
int tp_app_choice_index(const TpRow *r, double value) {
    for (int i = 0; r && i < r->n; i++) if (fabs(r->choices[i].value - value) < 0.05) return i;
    return 0;
}
