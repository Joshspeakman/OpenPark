# Theme Park native macOS plan

Target: the user's GOG `theme_park_enUS_1_0_33010.pkg`, recompiled into native
Apple Silicon code. Ground rules: [CLAUDE.md](../CLAUDE.md).

## Current milestone — 2026-10-16 (evening): sound, intro, launcher, save/load

**Theme Park Nightly.app** runs INTRO.EXE and MAIN.EXE as native ARM64 code
in a Cocoa window with a launch window, keyboard/mouse, Sound Blaster 16
digital sound and General MIDI music, at GOG's DOSBox pace. No DOSBox, Wine,
Rosetta, CPU interpreter or Python runs in the app.

What is verified, and how:

- **Main game code** (30,143 blocks + loaded drivers): the scripted first-park
  run matches the x86 oracle for 8M blocks with golden frame/profile hashes
  (`tests/check_macos.sh ... --park`, green on 611a66a); two deterministic
  random-click runs matched the oracle in full lockstep for 120M blocks each.
- **Off-path code from real-time play**: `tools/spotcheck.py` replays C-host
  checkpoints through compiled code and Unicorn. 160 random-play checkpoints,
  108 timer-interrupt entries and 90 Sound Blaster interrupt entries (1.8M
  blocks of HMI driver and mixer code) match.
- **Sound**: the HMI SOS drivers the game copies into DOS memory (MPU-401,
  detection, SB16 `sw1616m`, software mixer `tr16m` with its generated
  routine pool) are compiled ahead of time at their deterministic load
  addresses (`tools/dump_drivers.md`); blocks over bytes the drivers rewrite
  are re-checked on every run. `runtime/sb16.c` emulates the DSP, mixer and
  8237 DMA (the driver sends 16-bit samples over DMA 1) and IRQ 7;
  `app/audio.m` plays them through AVAudioEngine and MIDI through Apple's DLS
  synth. Measured: IRQ 7 at 5.5/s (44,100 Hz / 8,000-sample blocks), effect
  and ambience levels 0.2–0.9 in the park, MIDI 27–153 bytes/s in the intro.
  The hardware model itself is checked only by these measurements, not
  against real hardware; **nobody has listened to it yet**. The main game sent
  no MIDI in the parts of play exercised (its park audio is digital).
- **Intro**: INTRO.EXE traced (no self-modifying code in 30M blocks) and
  compiled with an `intro_` prefix; plays with sound, Escape skips (its own
  INT 9 handler reading port 0x60).
- **Save/load**: a random-play run writes a 304,668-byte `save/TEST.gy`;
  a new session's "Continue game" reads it and restores the park. (Before the
  BX handle fix below, saves were written empty.)
- **Soak**: four 2-minute sound-on random-play runs at 4x speed (≈1.27B blocks
  each) and earlier deterministic seeds: no stops.

Fixes found today by these checks: DOS handle functions must use BX only
(Watcom passes junk in EBX's top half: sound setup and saves silently failed);
blocks ending at LES/LDS never reached a successor; far CALL/JMP through
memory were compiled as near; block extents now follow QEMU's rules so
deterministic scheduling matches the oracle; PIC mask/keyboard IRQ for the
intro; DPMI descriptor, vector and lock services for HMI.

Known limits:

- Random right-click play can make the original code read outside memory
  (oracle-confirmed for one case; game behaviour); the app maps a 4 GiB guest
  space so this reads zeros, as DOSBox tolerates it.
- The early division fault at 0x1452cf has not recurred since the block-rule
  fix; not explained.
- Driver images are game-derived files in work/, made by the procedure in
  `tools/dump_drivers.md`; a different sound configuration needs new images.
- No VESA/SVGA mode; the game runs in 320x200 as GOG's default does.
- The NSEvent key table, window mouse mapping, on-screen drawing and audio
  output have only been exercised by automated tests, not by a person.

## Confirmed input survey

The package is 104,934,252 bytes, SHA-256
`942da3010a657c07621eb19c6f2dacbb71ce6f06c07f62914cb8f422e1305650`.
It contains a DOS release wrapped for Mac: `GAME/MAIN.EXE`, `GAME/INTRO.EXE`,
`CONFIG/MAIN.EXE`, DOS4GW and HMI audio-driver files. The game executables are
32-bit LE images behind MZ stubs, with their LE headers at file offset 0x28b8.
No Windows DLL import layer is present in these three images.

| Image | Bytes | Entry at LE object bases | Internal offset fixups | Function candidates | Static code-byte coverage |
|---|---:|---:|---:|---:|---:|
| GAME/MAIN.EXE | 841,223 | 0x7e2cc | 19,746 | 1,812 | 97.53% |
| GAME/INTRO.EXE | 179,303 | 0x11554 | 3,479 | 518 | 86.67% |
| CONFIG/MAIN.EXE | 90,758 | 0x15450 | 2,007 | 300 | 90.06% |

All recorded fixups in these images are internal 32-bit offset fixups. The LE
reader maps initialized pages and zero-filled tails, and applies these fixups.
Offsets may represent negative biases modulo 2^32; rejecting every offset beyond
an object's size incorrectly rejects the actual main executable. This address
arithmetic now has a synthetic regression test.

Main has three objects: code at 0x10000 (0x84deb bytes), writable storage at
0xa0000 (0x110), and data/BSS/stack at 0xb0000 (0x32340). These are analysis
addresses from the file, not a verified DPMI process layout or physical VGA map.

**Coverage is provisional.** The survey starts from the entry and relocation
references into code, then follows direct control flow. Some references may be
jump-table entries or embedded data. Suspicious disassemblies exist; a high
byte-coverage number does not validate all those instruction boundaries. Main's
first pass has 153,273 decoded instructions, 96 indirect-call sites and 57
indirect jumps. DOS/DPMI interrupts and port I/O are visible, but the exact set of
services required by live startup still needs a dynamic trace. Do not implement
every apparent service found in a speculative decode.

## Phases

| Phase | Deliverable / expected effect | State | Effort and confidence |
|---|---|---|---|
| 0 | Owned input pins, scratch extraction, ground rules and source-only project | Done locally | Small; high |
| 1 | Bounded LE mapping/fixups, executable census | Initial version done | Small for observed files; medium for arbitrary LE files |
| 2 | ARM64 routine proofs using the shared lifter | Two integer/memory slices pass | Small initial proof; broad CPU coverage still needed |
| 3 | Verified startup analysis and differential trace to first DOS service | Passed through player details | Synthetic DOS4G launch contract; original-vs-native CPU agreement |
| 4 | DOS/DPMI services: memory, process environment, files, time and callbacks | Observed startup services implemented | Keyboard, timer and far mouse callbacks tested |
| 5 | Framebuffer/palette presentation in a native window, then menus | Cocoa window (CoreAnimation, 4:3, full screen) | VESA modes remain |
| 6 | Native input and simulation pacing; new park, build operations, save/load | Real-time input, governor, save/continue verified | Build operations covered by fuzzing only |
| 7 | Intro playback and HMI audio through native output | Done: intro, SB16 effects, General MIDI | Hardware model checked by measurement only |
| 8 | Soak, original-behavior comparisons, launcher and packaging | App builds and installs (user authorised 2026-10-16); fuzz + checkpoint oracle | Launcher window, fuzz faults above |

The DOS adapter implements interrupts, port I/O, selector state, segmented string
operations and interrupt/far returns without invoking the shared lifter's game
hooks. Traced basic blocks compile ahead of time. Self-modified block variants
are selected by byte guards; an uncompiled variant stops explicitly. No runtime
decoder or CPU interpreter is used by the native runner.

## Initial slice verification and limits (superseded by startup evidence above)

- Seven synthetic LE tests pass, including every truncated prefix of the fixture,
  invalid bounds/objects, BSS, source lists, relative relocations, negative target
  bias and explicit pending selector fixups. No game bytes are in the fixtures.
- Fresh extraction was reproduced after preserving the first extraction under
  `work/*-first-inspection`; package scripts were not executed.
- All three actual executable images parse with zero pending offset fixups.
- Native proof: 0x28656 updates a 16-bit field; 0x87790 sign-extends four 16-bit
  inputs, stores them and their pairwise sums. These are behavior descriptions,
  not recovered original symbol names. Each ran 1,000 cases including integer
  boundaries and deterministic random values, **2,000 cases, zero mismatches**.
- Comparison covers all eight integer registers, defined arithmetic flags and
  DF, both writable LE objects, scratch memory and stack. The generated code is
  an ARM64 Mach-O library; `otool -L` shows only libSystem as a dependency.
- Both sides use the new LE loader's relocated bytes. This proves the selected
  lifted instruction behavior under that layout, not independent equivalence
  of the loader to DOS4GW. Startup/segment/bootstrap equivalence is still needed.
- No x87, OS call, hardware I/O, framebuffer, audio, whole-game startup, game
  loop or save compatibility has been verified. No game window was opened.

Evidence (local/ignored): `work/main-survey.json`, `work/intro-survey.json`,
`work/config-survey.json`, `work/native-proof/result.json`,
`work/native-proof/proof.dylib`, `work/check.log`.

## Next concrete step

1. The user's first session: keyboard, mouse, full screen, sound balance.
2. Regenerate driver images automatically (script the dump/mask procedure).
3. SVGA mode (the game's "toggle screen mode"), and a longer real-time soak.

## Done so far

- 2026-10-16 evening (Claude Code): sound (HMI drivers compiled, SB16/MPU-401
  emulation, Core Audio), intro, launch window, save/load verified, DOS handle
  fix, lifter fixes, interrupt-entry oracle checks, soak. Reinstalled the app.

- 2026-10-16 (Claude Code, continuing Codex after its usage limit): whole-program
  build; native C DOS host with deterministic and real-time modes; Cocoa app,
  self-test mode and build/install script; CPU-speed governor; checkpoint
  oracle and fuzzing. Installed `~/Applications/Theme Park Nightly.app` at the
  user's request (data `~/Games/ThemeParkNative`).

- 2026-10-16 (Codex, input/scheduling): added far mouse callbacks, full caller
  restoration, POPFD interrupt-flag restoration and independent scheduling
  quanta. Fixed the release-event deadlock in modal dialogs by scheduling input
  outside keyboard polling. Each run now gets a fresh, non-overlapping scratch
  profile. Native/original runs match through land purchase and the first park,
  including eight million blocks, exact frame output and setup-profile bytes.
  The complete `tests/check_macos.sh ... --park` check passes. Build-operation
  and full park-save compatibility are still unverified.

- 2026-10-16 (Codex, continuation): implemented the observed DOS/DPMI host and
  static DOS lifter adapter. Fixed selector width/stack behavior, F2 MOVSD repeat
  decoding, self-modifying setup allocation control flow, file-handle reuse and
  allocator resizing. Verified title, nickname input, main menu and player setup
  against x86, including timer ISR/far-return chaining and framebuffer equality.
  Added a native-only runner with no oracle/disassembler imports. All assets,
  compiled game blocks, traces, frames and scratch files stay ignored locally.

- 2026-10-16 (Codex): located and inspected the owned package; read the requested
  guide and reference ports. Established that this is a DOS/LE target. Added a
  bounded LE reader, synthetic tests, reproducible pinned extraction and static
  surveys. Reused the pinned Freedom Force lifter/header implementation in a
  local proof; two Theme Park routines now execute as ARM64 and pass 2,000
  original-vs-native cases. Existing projects and Downloads were not modified.
  No remote repository, public upload, installed app, launcher or release was
  created. Remaining runtime work is listed above.
