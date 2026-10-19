# Port audit: what runs, what was never exercised, what the developers left unfinished (2026-10-18)

Agent: Claude Code. Inputs: the static survey (`work/main-survey.json`, `work/survey/all.txt`), a new block-coverage
map (below), live memory of the running game, and the owned package's data files. Outputs of the audit scripts stay
in `work/audit-*` (game-derived; not committed).

## The short answer

This port is a whole-image static recompilation of `GAME/MAIN.EXE`, so no game code was left out. Every function the survey found
(1,812, of which 1,221 are game code below 0x17e000 and 591 are the Watcom C library and the HMI sound drivers) is
compiled to native ARM64. "Missed" can only mean three different things, and they are kept apart here:

1. **Host services the game calls that the port does not provide.** The host stops with
   `unsupported int ...` rather than guessing. No such call was reached in any session, and the static list below
   shows that the unhandled ones sit in C-library paths the game does not use.
2. **Code that was compiled but never executed in the test sessions.** It is ported but unverified: 626 game functions
   (about half) did not run in the sessions so far. These sessions were sandbox play, a 128x128 park, Sim and Business
   modes, new games, catalogues and the business screens.
3. **Features the original developers left unfinished.** Found by evidence in the code and data; each is graded
   *confirmed* (seen running or proven by the code path) or *static* (from the code alone).

## 1. Code and host coverage

- **Decoding:** 97.5% of the code object's bytes decode as instructions. What is left is eight runs of 24 bytes or
  more (1,071 bytes): the largest is 557 zero bytes of padding, and the rest are jump tables and switch data. There
  is no undecoded block of code.
- **Survey "islands":** functions with no direct caller turned out to be switch-case labels and jump tables that the
  survey listed as functions (for example 0x11ca0f, 0x11ce36, 0x13cce1). They are not dead code. The `int 0x13` that
  the survey shows at 0x13ccfa is a jump table read as code, so there is no disk-BIOS code.
- **Interrupts and ports** (`work/audit-services.txt`): the game's direct DOS, DPMI, mouse, keyboard, video and VESA
  calls are all handled by `runtime/host.c`, and so are the sound card's ports (sb16.c). The Watcom `int386` helper
  at 0x18a068 holds one `int` for every vector, so which of them it uses only shows at run time; none has stopped
  the port.
- **Unhandled DOS calls, all in library paths:**
  - 21h/41h delete, reached only through `fopen`'s internals; the game's own `fopen` of `sndsetup.inf` is read-only.
  - 21h/4Bh exec and 4Eh/4Fh find-first/next: only from library startup and spawn code; not run.
  - 21h/4Ah resize and 21h/01h read with echo: not run.
- **The other two executables:** `INTRO.EXE` is compiled and runs as the intro. `CONFIG/MAIN.EXE` (the sound setup
  program) is not needed: the launcher replaces it, and `sndsetup.inf` is supplied.

**Dynamic coverage** (`TP_COVERAGE=1`, ctl `coverage <file>`, which adds into the file across runs;
`work/audit_cov.py` reports by function):

| | ran | of |
|---|---:|---:|
| game functions | 595 | 1,221 |
| library and driver functions | 240 | 591 |

The 626 game functions that did not run (`work/audit-cats.json`) split into two groups:
- **255 are called from code that ran,** so they are reachable but not exercised.
- **371 are called only from code that also did not run.** 128 of these lead back to reachable code; the other 243
  are the switch-case and table "islands" described above.

What has not been exercised yet:
- a full game year in Sim and Business modes (auctions, takeovers, bankruptcy, winning);
- the stock market screens;
- the world map and moving to a new park;
- research completing for each item;
- every ride's breakdown and explosion;
- weather;
- the 640x480 mode screens;
- loading and saving every slot.

Extending the corpus is the next step before calling any of this verified.

## 2. Unfinished or cut features (evidence)

| Feature | Evidence | Grade |
|---|---|---|
| **Fireworks have no sound** | The main loop calls the firework routine (0x16d508) while park flag +0x77 is set, which is the FIREWORK DISPLAY menu's tick at 0x1cf257. It starts bank-0 sample 27 at launch, and its 1,000-particle system (0x16d55d: arrays at 0x1d8ba8 and 0x1d9b4c) starts 27 again at each burst. Sample 27 is `NULL.RAW`, a silent placeholder. | static, strong |
| **Placeholder sound slots** | Bank 0 has nine `NULL.RAW` entries (23, 24, 27, 28, 29, 33, 34, 36, 46). Only 27 is started with a constant (the fireworks); the others may have been planned sounds. | static |
| **A removed developer menu** | The menu text holds a full set of items that no menu points to: PAUSE, SCROLL ONLY, VERSION, TURBO MODE, MISCELLANEOUS, RAIN LEFT/RIGHT, PALETTE DARKER/LIGHTER/CHANGE/RESET, FORCE YEAR END, COM PLAYERS, GAME SPEED FASTER/SLOWER, CHEATS (MORE MONEY, LESS MONEY), DEBUG, GENERAL, 3DO TEST, WEATHER NEXT TYPE, PARK NAMING, MOVE PANEL, DELETE PEOPLE, LOAD SAVED GAME, SAVE CURRENT GAME (menu texts 14..69). There are no hidden drop-downs; the menus were unlinked, and some handlers survive as keys (next rows). "3DO TEST" points to the 3DO port. | confirmed (texts, menus) |
| **Cheat mode (HORZA)** | The player name is checked against an encoded table at 0x1b8c58 (each letter shifted left by its position), which decodes to HORZA. A match sets 0x1d6682, and keys 8, F8, C and Z then do more (key table 0x113cda, handlers 0x113cf9). | confirmed (code) |
| **MAKE THINGS TINY** (Business mode DISPLAY menu, Shift-T) | A toggle at 0x1d7762 sets 0x1d6640 = 3, 0x1d6642 = 4 and 0x1d667c = 8: a reduced-scale view. It sits next to the developer items and has no help text. Not seen on screen yet (a negotiation popped up over the test). | static |
| **Ride animations for only 15 of 29 rides** | `RIDEANI.nnn` exists for 0, 2, 3, 4, 6, 7, 8, 9, 12, 13, 14, 19, 21, 26 and 28. The player (0x15c04f) remaps ride 16 to animation 9 by hand, and a missing file is skipped without a message. | confirmed (files, code) |
| **A command queue for computer or network players** | Many keys don't act directly: they post action codes to a per-player queue (0x1d441f + player x 6). "COM PLAYERS" in the cut menu and the "%s HAS JUST CHEATED" messages fit an unfinished multiplayer or computer-player layer. | static |
| GAME (DISPLAY menu, options case 5) | Not unfinished: it clears the screen state (0x1d6652, 0x1d6670) and returns to the park view. | confirmed |
| Menu items with no function | Not unfinished: each fires its keyboard shortcut instead (+0x13 scancode, +0x14 modifier: F5..F7, O, F, Shift-S, Ctrl-S, M, Shift-T and so on). | confirmed |

The second sound bank (`SNDS1`: barks, propellers, magic, parachute and the father and boy voices) is the intro's,
not unused game sound.

## 3. Modding map (for an OpenThemePark engine)

Addresses are this port's guest addresses (the file's plus 0x100000). The data files are the owned package's.
- **Things:**
  - A pool of 0xc8-byte records at `*[0x1d3b10]`; the last record is at `[0x1d3b78]`.
  - Fields: +7 x, +9 y, +0xc kind, +0x16 type offset into the ride table, +0x20 counter, +0x2e wear (explodes past
    twice the type's +0xa8), +0x3a cycles.
  - The pool size is variable (bigmap.c; 12,000 in 128x128 parks).
- **Map:**
  - Cells of 4 bytes at `[0x1d3b08]`, with a stride of 64 or 128 (`tp_map_w`).
  - Cell flags come from `0x1b44d9 + 4 * kind`.
  - Camera at 0x1d667a (x) and 0x1d6666 (y); 0x1d666e is the redraw flag; the zoom is at 0x1d663a.
- **Rides:** 29 definitions of 0xc8 bytes at 0x1cced0 (+0xb7 the tune, +0xc5 researched, +0xa8 the wear limit).
- **Shops:** 17 definitions of 0x6b bytes at 0x1d3c62 + 0x22 (+0x44 researched).
- **Park records:**
  - The player's park is at `[0x1d3818]`; +0x18 means park open, +0x77 fireworks on, +0x63f the entrance cell.
  - The player record is at `[0x1d3814]`; +0x5ed is the month.
  - The level is at 0x1d66f1 (0 sandbox, 1 sim, 2 business).
- **Menus:**
  - The top bar is at 0x1b53b6 (word pairs: drop-down and text).
  - Drop-downs start at 0x1b53c6 + n x 0x354, with items of 0x15 bytes: submenu, text, kind (1 tick, 2 line),
    tick pointer, function, argument, key.
  - Texts are in the table at `[0x1d7bcc]`; menu text n is string n + 392 of `LANG0-0.DAT`.
  - Clicking calls the function with its argument from the main loop (0x1122b2). The options function at 0x150ccd
    has 14 cases.
  - The port adds its own drop-downs from 32666 on (qol.c), which shows that the menus can be extended.
- **Keys:** 32 scancodes at 0x113cda, with the jump table at 0x113cf9. Some act directly; others post codes to the
  queue at 0x1d441f.
- **Sound:**
  - `SNDSb-v.DAT` with `.TAB` entries of 32 bytes: name[18], offset, 0, length. Variant v is 0 (8-bit 11 kHz),
    1 (8-bit 22 kHz) or 2 (16-bit 44.1 kHz, the one this port loads).
  - Game sample n is entry n. The game plays a length 16 bytes short of the table's.
  - Effects start through 0x180fe0 (interface and events) and 0x180c80 (the park), and the HMI start-sample call is
    at 0x18943f: the volume word is at record +0x10 and the size at +8.
  - Music: `MUSICb-v` banks of HMP tunes; play_tune is at 0x180a20.
- **Ride animations:** `data/rideani.%03d`, played by 0x15c04f (mode 2; mode 1 plays `wingame.dat`).
- **Saves:** `<name>.g0`/`.gy` hold the park; `.GD` is the profile. This port adds `.big` (128x128: stride and
  things) and `.free` (Free Mode rules) beside a save.
- **Tunables** the host can change at run time are listed in `tools/build_game.py` (TUNABLES, for example the
  visitor cap), and map-geometry sites in `config/mapgeo.json` (636 constants made variable for 128x128 parks).

## Next steps

1. Widen the coverage corpus: a scripted year in Sim and Business modes, the stock market, the world map and
   relocation, breakdowns, weather and 640x480. Re-run `work/audit_cov.py` until the not-exercised list is only
   error paths.
2. Unfinished features worth finishing in the port or an engine:
   - fireworks sound (put a real sample in slot 27);
   - the developer menu (link the surviving handlers into EXTRAS: force year end, weather, palette, more money);
   - ride animations for the 14 rides without one.
3. Confirm MAKE THINGS TINY and the cheat-mode keys on screen, and document what each does.
