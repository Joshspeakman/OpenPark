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
| **Ride videos for only 15 of 29 rides** | `RIDEANI.nnn` (first-person ride videos) exists for 0, 2, 3, 4, 6, 7, 8, 9, 12, 13, 14, 19, 21, 26 and 28. The player (0x15c04f) remaps ride 16 to video 9 by hand, and a missing file is skipped without a message. Six real rides never got one (see *Ride videos and cut objects*). | confirmed (files, code) |
| **A command queue for computer or network players** | Many keys don't act directly: they post action codes to a per-player queue (0x1d441f + player x 6). "COM PLAYERS" in the cut menu and the "%s HAS JUST CHEATED" messages fit an unfinished multiplayer or computer-player layer. | static |
| GAME (DISPLAY menu, options case 5) | Not unfinished: it clears the screen state (0x1d6652, 0x1d6670) and returns to the park view. | confirmed |
| Menu items with no function | Not unfinished: each fires its keyboard shortcut instead (+0x13 scancode, +0x14 modifier: F5..F7, O, F, Shift-S, Ctrl-S, M, Shift-T and so on). | confirmed |

The second sound bank (`SNDS1`: barks, propellers, magic, parachute and the father and boy voices) is the intro's,
not unused game sound.

### Ride videos and cut objects (2026-10-19)

**What `RIDEANI.nnn` is.** Each file is a 320x200 FLIC video (magic 0xAF12 at +4, frame count at +6; a 16-byte
header instead of FLIC's 128; standard chunk types 4, 7, 12 and 15) that shows the ride from a seat. All 15 were
decoded and each shows its own ride: 0 Flight Sim, 2 Pirate Boat, 3 Plane Flyer, 4 Maze, 6 Haunted House, 7 Rubber
Tubing, 8 Race Car Ride, 9 Roller Coaster, 12 Big Wheel, 13 Merry-Go-Round (teacups), 14 Bouncy Castle, 19 Snakes and
Ladders, 21 Parasol Chairs, 26 Super Spinner (25 frames) and 28 Planet Rocket. None shows a ride that is not in the
game. The number is the ride's index in the ride table (`(thing+0x16)/0xc8`).
- **Checked in the game (2026-10-19):** the Super Spinner's video plays from its panel and returns to the park.
  During a video the widescreen side strips keep the last park picture; they should be black (open, cosmetic).
- **Speed (fixed 2026-10-19):** between frames the player waits for 2 or 8 hundredths on the DOS clock
  (int 21h/2Ch, 0x16123d; 2 in this mode). DOS derives that clock from the 18.2 Hz BIOS tick, so a wait lasts a
  whole tick: 18.2 frames a second. The host answered to the hundredth, and the videos ran at about 50. Now the
  clock steps at the tick, as in DOS and DOSBox. Measured through the same player (the win video, mode 1): 101
  frames in 2 s before, 36 after.
- **The intro is unaffected:** it never reads the DOS clock. It paces by a 120 Hz timer event registered with the
  HMI driver (0x112e3d(120, 0x110010), counter 0x140a82 against a per-segment limit at 0x140a4c set by the video's
  script). Measured: 1,202 events in 10 s.
- **How it is played:** button action 14 (jump table 0x13b787, handler 0x13c086). On the ride panel (screen 0x14)
  it calls the player 0x15c04f in mode 2 with the selected ride. On the other panels the same action centres the
  camera on the selected thing.
- **Music:** the player takes each video's tune from a two-byte-per-ride table at 0x1ba0ff. The table has entries
  for exactly the 15 rides that have files and zeros for the rest. So the 14 missing videos were never planned in
  the shipped code; they are not files missing from this copy.
- **The 14 without a video:**
  - 3 are track pieces, not rides: Loop the Loop, Cork Screw and Water Splash.
  - 4 are shows: the Cowboy, Clown, Dolphin and Medieval acts.
  - Big Dipper (16) reuses the Roller Coaster's video by the hard-coded remap.
  - That leaves six real rides that never got one: Tree House, Ghost House, Space Shuttle, Monorail, Observation
    Tower and Observatory.

**The id-to-name rule.** Every ride, shop and scenery record has an object id at +0x22, and the text in
`LANGn-0.DAT` is laid out so that **object id k is named by string k + 26**. This holds for all 65 defined objects:
29 rides at 0x1cced0 (0xc8 bytes), 17 shops and sideshows at 0x1d3c62 (0x6b bytes) and 19 scenery and toilet items at
0x1cca92 (0x38 bytes). The tables' templates in the executable hold the same counts: 29 ride records at 0x1b16f8 and 17 shop records at
0x1b2ff8, each followed by zeros. Ids
that have a name but no record are objects that were cut:

| id | Name (all five languages) | Group, from its neighbours | Evidence |
|---|---|---|---|
| 0x19 | DIVE BOMBER (STURZBOMBER, PAZZO BOMBARDIERE, ...) | ride, between Snakes and Ladders (0x18) and Observation Tower (0x1b) | no record; no code tests the id |
| 0x1a | PLANET BOMBER | ride, as above | no record; no code tests the id |
| 0x1c | (slot reused for the UI text TIME REMAINING) | ride, between Observation Tower and Flight Sim | name replaced, so nothing is left |
| 0x1f | SALTY DOG | food stall, before Pokey Cola, Mr. Whippy and the Big Time stalls (0x20..0x23) | no shop record |
| 0x145 | BINGO HIGHWAY (BINGO HIWAY, BINGO LAND) | sideshow, after Tin Can Alley (0x143) | no shop record |

Dive Bomber and Planet Bomber also appear in the old ride-name run (strings 44..56) that runs in id order from
Merry-Go-Round (0x12) to Tree House (0x1e). They were translated into all four other languages, so they were cut
after localisation began. Game code tests ride ids by constant only for 0x14, 0x64, 0xaf, 0xb0 and 0x108, so
nothing in the code is waiting for the cut ids.

**Animations no ride or shop record uses** (decoded from `MSTA/MFRA/MELE-0.ANI`: start frame per animation; 8-byte
frames {first element, w, h, flags, next}; 10-byte elements {sprite x 6, x, y, flags, next}; 694 animations over the
3,301 sprites of `MSPR-0`). Every sprite that has art belongs to some animation; 785 sprite slots are empty (0x0),
which is art taken out of the bank with its slot kept. Of the ride-sized animations that no ride or shop record names:
- **Alternate animations of five existing rides: 610..617.** They are built only from those rides' own sprites:
  Pirate Boat, Space Shuttle, Snakes and Ladders, Ghost House and Tree House. They match those rides' sizes but use
  different frame sequences, and no reference to them was found. They may be extra states that were dropped; they
  add no new ride.
- **Copies of existing sideshows:**
  - 671..673 are frame-for-frame copies of the Duck Shoot set (581, 582, 586);
  - 577 is a second Coconut Shy picture (the same art as 568, with different frames);
  - 585 is an 18-frame Race Track loop beside the 17- and 28-frame ones.
- **Used, not cut:**
  - 618..621 (sprites 992..995), a gold-roofed booth with striped pillars seen from four sides, is the Race Car
    Ride's station: it shows in that ride's panel. No ride or shop record names it, which makes it a clear example
    of the caveat below.
  - 319, 543 and 544 (and the pieces 317, 318, 545, 546) are the park entrance's gate arm and ticket kiosk. Function
    0x118bc0 places them from a 6-byte layout table at 0x1b4ccc, relative to the entrance cell.
  - The four show stages in full (393, 394, 400, 410).
  - Buses (176..212, the bus-upgrade event).
  - The pieces that look like track-ride and path parts: 137..153, 498..503 (race cars) and 432..433 (one-way
    arrows).
  - Seven sheets of every guest pose in one frame each (603..609, probably developer reference sheets).
- **Caveat:** absence from the records does not prove an animation unused. The code also picks animations by
  constants and through other tables (302 sites read the start table), and only the ride-sized animations were
  checked one by one.

No art was found for any of the four cut objects. To bring one back, an engine needs a new record with the cut id
in the matching table (the id already has a name in all five languages) and new art.

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
- **Ride videos:** `data/rideani.%03d` (FLIC), played by 0x15c04f (mode 2; mode 1 plays `wingame.dat`); tune per ride at 0x1ba0ff.
- **Park animations:** `MSTA-0.ANI` (start frame per animation, pointer [0x1d3b38]), `MFRA-0.ANI`, `MELE-0.ANI`, sprites `MSPR-0`.
- **Scenery and toilets:** 19 records of 0x38 bytes at 0x1cca92 (+0 object id, +2 animation).
- **Object names:** object id k is `LANGn-0.DAT` string k + 26.
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
