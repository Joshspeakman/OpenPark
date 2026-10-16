# Theme Park native macOS plan

Target: the user's GOG `theme_park_enUS_1_0_33010.pkg`, recompiled into native
Apple Silicon code. Ground rules: [CLAUDE.md](../CLAUDE.md).

## Current milestone — 2026-10-16: native app (Claude Code, continuing Codex)

**Theme Park Nightly.app** runs the game as native ARM64 code in a Cocoa
window, with real-time keyboard/mouse input. Title, nickname, main menu, player
setup, the world map, buying land and the park itself run at the original
pace. No DOSBox, Wine, Rosetta, interpreter or Python is in the app process.
There is **no sound yet** and the intro movie (INTRO.EXE) is not played.

What is verified:

- **Whole-program compilation.** `tools/build_game.py` compiles every block
  statically reachable from the entry point, all relocated code pointers, the
  survey's call targets and the verified trace: 30,143 blocks, O(1) dispatch.
  Block extents follow QEMU/Unicorn's translation-block rules so block-count
  scheduling matches the oracle. Unknown targets stop with their address.
- **x86 oracle, scripted path.** `tests/check_macos.sh ... --park` verifies the
  whole-program library: eight million blocks, 13,678 DOS/BIOS and 6,882
  scheduling boundaries, golden first-park frame and profile.
- **Native C host.** `runtime/host.c` (C port of `tools/dos.py`) in
  deterministic mode reproduces the golden first-park frame and profile
  byte for byte; inside the app the menu and setup frames hash identically to
  the verified milestones.
- **Off-path code.** `tools/spotcheck.py` replays C-host checkpoints from
  random-click play through the compiled blocks and Unicorn in lockstep
  (exact arrival-count synchronisation). 160 of 160 checkpoints match.

Pacing: the game's loop is not timer-paced; idle timeouts, the attract montage
and the simulation advance per pass. GOG's release pins DOSBox at
`cycles=fixed 10000`, so the host holds the executed x86 instruction rate at
10 million per second (`TP_CYCLES`, thousands per second) by sleeping. Without
it the nickname screen timed out to the demo in about a second. The PIT runs
at the game's programmed 72.8 Hz.

Known problems (see "Next"):

- Long random-click fuzzing reached a division fault at 0x1452cf (two seeds,
  after staff hiring/negotiation screens) and a "STAFF NEGOTIATION" screen with
  a garbled background. Not yet shown to be a translation fault or a game bug.
- One fuzz seed read past the 64 MiB guest space while drawing a corrupt
  cursor sprite on the park-status screen. Guest memory is now a 4 GiB
  reservation, so this reads zeros instead of crashing; the cause is open.
- No audio (HMI SOS drivers), no intro playback, no VESA modes, no launcher
  window yet. Save/load through the app has not been exercised.

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
| 6 | Native input and simulation pacing; new park, build operations, save/load | Real-time input and 10M-instruction/s governor; park runs; path tool opens | Save/reload and build-operation acceptance remain |
| 7 | Intro playback and HMI audio through native output | Not started | Large/uncertain; driver ABI and music formats (verify) |
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

1. Decide the fuzz faults: replay the deterministic fuzz scripts
   (`work/headless-build/detfuzz-*.txt`) through `tools/verify_startup.py
   --library` to find the first divergence, or show the original faults too.
2. Save/load from the app, then a long real-time soak.
3. HMI SOS audio: identify the driver ABI the game calls and replace it with
   native Core Audio output (sound effects, then music).
4. A launch window in the other ports' layout; INTRO.EXE (separate image).

## Done so far

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
