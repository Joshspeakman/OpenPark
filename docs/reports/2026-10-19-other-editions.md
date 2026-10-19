# Other editions: what the owner's discs hold beyond GOG's release (2026-10-19)

Agent: Claude Code. Sources are the owner's own discs and floppy, mounted read-only. Comparisons are by
SHA-256 against GOG's package (`config/inputs.json`). Unpacked files stay in the ignored `work/extras/`.

| Source | What it is | Beyond GOG |
|---|---|---|
| PC CD (`THEMECD`, two copies) | the DOS CD release; the second copy (UK) is byte-identical to the first | Nothing. 129 of 133 files are identical to GOG's; the other four are GOG's edits (below). |
| Japanese PC CD (`Mening CD`) | Japanese release: Japanese edition `GAME/MAINCH.EXE`, double-byte fonts | Japanese (kanji and kana) and Simplified Chinese text, redrawn menu and finance screens, a 320×200 installer variant |
| Amiga CD32 (Mindscape 1995, data track 1 of 26) | Amiga AGA release | Its music is CD audio (`cd.device`; `midi.sng` is 51 KB): tracks 2–26, **not yet dumped** |
| 3DO (USA) | the 3DO release (Opera filesystem) | A recorded soundtrack of the PC tunes; 17 ride videos, two of them (Space Shuttle, Tree House) for rides with no PC video, three longer cuts |
| PC Gamer demo floppy (March 1994) | pre-release DOS demo in an RNC ProPack archive | Pre-release data and texts (below) |

## GOG's edits to the CD release

- `THEME.BAT`, `SETUP.BAT`: `exit` lines, so DOSBox closes with the game.
- `CONFIG/MAIN.EXE` (sound setup): the line it writes into the batch file, `@cd \` → `exit`.
- `GAME/MAIN.EXE`: 31 bytes in two places.
  - A jump displacement at file offset 0x99680.
  - A self-locating stub at 0x99e00 (guest 0x17e000, the end of the game's code) that writes a `jmp` into
    the code before it at run time.
  - The port runs GOG's executable, so it inherits this; the build's verified trace records the rewritten
    variant (`tools/build_app.sh`: "the self-modified allocation variant").

## Japanese CD

- **`GAME/MAINCH.EXE`** (907,687 bytes): a DOS/4GW LE executable like `MAIN.EXE`, with a font renderer.
  - It loads `FONT12.FON` (196,288 bytes), `FONTA12.FON` (3,072) and `FONT16.FON`.
  - The setup offers a 320×400 installer (`MAIN.EXE`) and a 320×200 one (`MAINUK.EXE`).
- **Text:**
  - `LANGJ-0.DAT`: Japanese with kanji (Shift-JIS, 1,304 strings).
  - `LANGV-0.DAT`: Japanese in half-width katakana (1,304 strings, for 320×200).
  - `LANGC-0.DAT`: Simplified Chinese (GBK, 1,292 strings).
  - The Japanese files have 12 more strings than English, so the indices differ.
- **Redrawn screens:** `MMENU-0/1.DAT` and `MSTATE-0.DAT` (10,000–41,000 bytes differ), presumably translated.
- **`RIDEANI.021`:** the same video. The only difference is 11,734 bytes zeroed in the middle, which looks like
  damage on that copy.
- **`SDD53/`:** SciTech Display Doctor 5.3, a third-party VESA driver; not game content.
- **What running it would take:**
  - The host has no VGA sequencer/CRTC support (only the DAC and mode 13h/VESA), so the 320×400 mode is a
    host feature first.
  - As a separate image it would get none of the port's MAIN.EXE features: the view, EXTRAS, 128×128 maps
    and hooks are all pinned to MAIN.EXE's addresses.

## Amiga CD32

- **Files:** `main` (68000), `pintro`, `startup-sequence`, and Amiga-format data: bitplane `.BM` screens, 8SVX
  and its own sample bank (`SNDS-0`), `midi.sng`, a `frog.pak` intro, and `I-0`/`BALPR`/`BSTSPR` sprites.
- **The art:** Amiga-native formats and palettes; not directly usable by the DOS game.
- **The music:** the executable opens `cd.device`, the CD32's audio-track interface. With only a 51 KB music
  file on the data track, the soundtrack is almost certainly the 25 audio tracks. Using it needs a full dump
  of the disc (bin/cue, or the tracks as WAV), then the game-situation → track mapping from `main`.

**The full disc dump (bin/cue, 26 tracks):**
- **The tracks:** 25 audio tracks, 13.6 minutes in all, 4–82 s each.
- **The tune → track table:** `main` plays a CD track at 0x53d40 (cd.device command 0x25, one track); its only
  caller, 0x4d712, takes tune n ≤ 29 to the table at data hunk 6 +0xcd72 and tune n ≥ 30 to track n − 30.
  - Tunes 1–17 (the rides) → 2, 17, 15, 7, 11, 25, 23, 24, 14, 5, 20, 13, 13, 14, 3, 16, 22.
  - Tunes 18–27 → the same track number.
  - Tracks 4, 6, 8, 9, 10 and 12 aren't in the table (tunes ≥ 30, likely menu music).
- **In the port:** `tools/extras.py` imports the tracks; Launcher > Sound > Music Type: Amiga CD32 plays them
  for the game's tune (`platform/macos/main.m` musicTick, `platform/macos/audio.m`).
- **Checked:** your test park's Super Spinner (tune 12) played track 13. The recorded output matched the track's
  envelope at 0.96 correlation (another track: 0.16).

## 3DO (USA)

- **The disc:** one data track (bin/cue, 227 MB) with the 3DO Opera filesystem (`tools/opera.py` lists and
  extracts it): 270 files, 170 MB. The game is `Park` (ARM), with data in `STP Data/` and 3DO system files.
- **A recorded soundtrack:** `System/Audio/aiff/RIDE1..17.AIFC` (SDX2 2:1 compressed, mono, 44.1 kHz).
  - Each is the PC tune of the same number, recorded: lengths match the PC tunes' HMP song times to a second
    (RIDE1 17.5 s / 17 s … RIDE7 67.3 s / 67 s, RIDE11 74.6 s / 75 s).
  - Plus `HEART` (76.5 s), `SHARES` (81.6 s, the stock market?) and `CHEMIST` (68.2 s, research?).
- **17 ride videos and three event videos** (`*.STREAM`: 3DO DataStream, Cinepak 320×200 at 15 fps, SDX2
  sound; `tools/cinepak.py` decodes them):
  - **Two rides the PC has no video for: `SHUTTLE` (Space Shuttle, 480 frames: up the launch tower, a
    "BLAST OFF" display) and `TREE` (Tree House, 481 frames).**
  - **Longer cuts than the PC's:** `SPINNER` 275 frames (PC 25), `TCUPS` 481 (PC's Merry-Go-Round 120),
    `PLANE` 180 (PC 60).
  - The same length as the PC: FLIGHT, HAUNT, MAZE, PARA, PLANET, RACE, ROLER, CASTLE, TUBE, WHEEL; also
    PIRATE 303 (300), SNAKE 402 (418).
  - Events: `TO` (takeover?, 130), `WL` (win/lose?, 336), `BUST` (bankrupt, 94).
- **Other:**
  - 3DO-drawn screens (`NTSC_*.cel`: main, bank, stock, research, negotiation, award…);
  - a 3DO intro (`INTRO.FLC`, 18.5 MB) with its own MIDI files;
  - three languages;
  - a sample save (`ROLL_GAME`).
- **In the port (2026-10-19):** `tools/extras.py` imports the recorded tunes as WAV and keeps the ride streams
  as they are, with their sound as WAV.
  - **Music:** Music Type: 3DO Recorded; tunes 1–17 play RIDEn, and 18 plays HEART.
  - **Ride videos:** Ride Videos (PC / PC + 3DO / 3DO). `qol.c` hooks the game's player at 0x15c04f (mode 2,
    ride), and the app decodes the Cinepak frames itself (`runtime/cinepak.c`, the same as `tools/cinepak.py`
    byte for byte) and shows them in full colour.
  - **Pausing:** the game is paused like a hidden window while a video plays. Blocking the game thread let it
    race to catch up afterwards: 7 in-game months in 40 s; now one week for the 4 s around the video, and 10
    days in the next 5 s, its normal rate.
  - **Checked:** Space Shuttle played in the game; RIDE12 played for the Super Spinner.

## Demo floppy (PC Gamer, March 1994)

- **The archive:** `GAME.EXE` is a PROPACK SFX 2.18 archive of 41 RNC method-1 blocks; `tools/rnc.py` unpacks
  them.
- **What's inside:** a DOS/4GW demo build (701,141 bytes) and pre-release data. Only `MAWAR1`, `MCUP` and
  `DRUM.BNK` are identical to the release; it also has a `MGLASS-0.DAT` the release lacks.
- **Text differences from the release** (`LANG0-0.DAT`, 1,007 strings against 1,169):
  - PINE TREE where the release has LAMP POST (string 64);
  - ONE WAY PATH (later ONE WAY SIGNS);
  - HIPPO MAN (later RHINO MAN);
  - "YOU CAN ONLY PLACE A TUBING HOUSE NEXT TO…";
  - the typo PANET ROCKET.
  - BINGO HIWAY is present, as in the release (a sideshow cut from both).
  - Rival parks CATS, FATS, WALLEY WORLD, STONE PARK, EURO TRASH, WET AND SET, CIPPY SODBURY, FARMYARD FUN, FLIP AND
    FLOP, and different rival names (CHUFF CHIFF, THACKERY, J/V/A/B/C/D STONE).
- **A developer build-info screen** in the demo executable: AUTHOR, COMPILED BY, LATEST CHANGES, ART CONTACT,
  PROGRAM CONTACT, LOGIN DEMO, USERNAME, ILLEGAL USER.

## Candidates for an extras importer (OpenRA-style: the owner's discs, matched by hash)

1. **Amiga CD32 soundtrack:** CD-audio music as an alternative to the MIDI tunes. Waits on the audio tracks.
2. **Japanese and Chinese text:** needs the Japanese executable's renderer, ported or recompiled, plus host
   VGA work.
3. **The 1994 demo:** playable as its own image (the same job as the intro); historical.
