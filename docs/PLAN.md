# Theme Park native macOS plan

Target: the user's GOG `theme_park_enUS_1_0_33010.pkg`, recompiled into native
Apple Silicon code. Ground rules: [CLAUDE.md](../CLAUDE.md).

## Current verified milestone — 2026-10-16

Native startup now reaches the title, nickname entry, main menu and the new-park
player-details form. **2,518 compiled blocks, 12,818 matched DOS/BIOS service
boundaries**, matching port traffic and a byte-identical 320×200 framebuffer.
A separate native-only run with `python3 -S` reaches the same form and frame
hash without importing Unicorn or Capstone (0.215 seconds for this accelerated
test on this machine; not a real-time performance claim).

The standard menu check passes 15 synthetic LE/DOS tests, 100 synthetic CPU cases,
2,000 original routine comparisons, and 12,699 startup service comparisons.
The ARM64 library links only libSystem. Test runs are headless, muted and scratch
only. The project and generated outputs are entirely in theme-park-macos; no
Freedom Force launcher is used.

The game is **not yet a finished playable port**. The current host is Python
development infrastructure. Mouse callback delivery, real PIT-rate scheduling,
native window/input, gameplay/build operations, save/reload and audio have not
passed acceptance. The DOS model is shared by the oracle/native harness and
is not independently verified against DOS4GW. See [DOS contract](DOS-CONTRACT.md)
and [progress report](reports/2026-10-16-native-startup.md).

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
| 4 | DOS/DPMI services: memory, process environment, files, time and callbacks | Observed startup services implemented | Keyboard and timer callbacks tested; mouse delivery pending |
| 5 | Framebuffer/palette presentation through a background Metal host, then menus | Headless VGA frames/menu pass | Native window/Metal and VESA remain |
| 6 | Native input and simulation pacing; new park, build operations, save/load | Scripted keyboard reaches setup form | Mouse, real-time pacing, gameplay/save acceptance remain |
| 7 | Intro playback and HMI audio through native output | Not started | Large/uncertain; driver ABI and music formats (verify) |
| 8 | Soak, original-behavior comparisons, launcher and packaging | Not started | After playable milestone; no installation authorized yet |

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

Deliver mouse events through the registered guest callback, accept the player
details and reach the first park. Continue differential verification through
each new path. Replace synthetic timing with PIT-rate scheduling and move the
development platform services into the native app host before packaging.

## Done so far

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
