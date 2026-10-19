# Other editions: what the owner's discs hold beyond GOG's release (2026-10-19)

Agent: Claude Code. Sources are the owner's own discs and floppy, mounted read-only. Comparisons are by
SHA-256 against GOG's package (`config/inputs.json`). Unpacked files stay in the ignored `work/extras/`.

| Source | What it is | Beyond GOG |
|---|---|---|
| PC CD (`THEMECD`, two copies) | the DOS CD release; the second copy (UK) is byte-identical to the first | Nothing. 129 of 133 files are identical to GOG's; the other four are GOG's edits (below). |
| Japanese PC CD (`Mening CD`) | Japanese release: Japanese edition `GAME/MAINCH.EXE`, double-byte fonts | Japanese (kanji and kana) and Simplified Chinese text, redrawn menu and finance screens, a 320×200 installer variant |
| Amiga CD32 (Mindscape 1995, data track 1 of 26) | Amiga AGA release | Its music is CD audio (`cd.device`; `midi.sng` is 51 KB): tracks 2–26, **not yet dumped** |
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
  for the game's tune (`app/main.m` musicTick, `app/audio.m`).
- **Checked:** your test park's Super Spinner (tune 12) played track 13. The recorded output matched the track's
  envelope at 0.96 correlation (another track: 0.16).

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
