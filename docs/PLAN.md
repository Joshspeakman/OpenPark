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

## Engine rebuild (CorsixTH-style) — plan and state, 2026-10-17

The user wants the engine rebuilt the way CorsixTH rebuilt Theme Hospital, starting
with real-time zoom. Approach: keep the verified recompiled game as the reference
and replace subsystems natively one at a time, each checked against the original,
until the original code is no longer needed for that part.

Stage 1 (done, runtime/view.c): the host owns camera and zoom. The park is drawn by
the game's own renderer called as a service: `render_park(cam_x, cam_y, rows)` at
0x11b837 is run by the host (`tp_host_guest_call`) once per area ("pass"), and the
passes are assembled into one 1984x1058 picture of the whole park (320x200 scale).
At 1x nothing changes (no hooks are set). Facts it rests on, all measured:
- Hooks (verify.c `tp_hook_pcs`): 0x113b9a (the game's per-frame render call),
  0x113beb (its return), 0x180210 (back buffer to screen, before the cursor).
- Camera: x [0x1d667a], y [0x1d6666] (game keeps x 4..51, y 0..51); landscape
  cache dirty flag [0x1d666e]. A camera step moves the picture by (24, 0) in x and
  (8, 16) in y (registration of passes, exact match). Park rows 0..177; panel below.
- Side effects: drawing advances things' animation frames (offset 10 of the
  200-byte records at [0x1d3b10], every 16th tick). The game's globals and data
  blocks (pointer table 0x1d3b00..0x1d3bbf) are saved before the passes and put
  back after; a full 64 MiB memory diff over busy demo frames then shows no change.
- A pass costs about 0.56 ms; the whole park is 63 passes. Visible passes run
  within 12 ms per frame (round robin beyond that) and do not count as game time.
- Interface over the park (windows, date, messages) = back buffer at 0x180210
  minus the park as rendered; shown at 1x over the zoomed picture.
- Input: a click in the zoomed park moves the game's camera so the point is in its
  real view, then goes to the game there (checked: path tool selected through the
  panel, path laid at a point outside the game's own view).
How the addresses were found: block profile of a running park (TP_PROFILE ->
tools/survey_callgraph.py), function disassembly from a checkpoint
(TP_SHOT_CKPT -> tools/survey_disasm.py), sprite banks located by content
(tools/sprites.py decodes them), the renderer reached through the MBLK descriptor.

Done since (2026-10-17): the game's cursor and tool previews in the zoomed picture
(the park layer is taken after 0x14de1a, which draws park-anchored extras; the date
and advisor-button rectangles stay interface); clicks on the game's windows go
straight through; the 640x480 SVGA mode (R in the park; host VESA 101h through
DPMI 0300h and INT 10h 4F02h/4F05h, 64 KiB banked window). Measured: the art is the
same size in both modes (camera steps (24, 0) and (8, 16) in either), 640x480 just
shows more; pass row count comes from the game's row height [0x1d6654].

Stage 3 started (2026-10-17 night): runtime/native_land.c draws the landscape
layer natively (the loop 0x11b8fe..0x11bd47, the plain/mirrored blitter
0x15ed21, composite frames 0x1209b5 and the cell decorations), from the game's
own data in memory. TP_NATIVE_CHECK compares it with the game's landscape cache
after every pass: 0 differing pixels over 2,200 passes of the scripted park,
1,400 of the busy demo park (decorations included) and 600 in 640x480.
Learned: the game keeps sprite sizes doubled in its tables and works in a
640-wide space; 320x200 halves positions and sizes in the blitter; 640x480
uses a smaller row height ([0x1d6654]), so the art is the same size in both.

Stage 3b (done 2026-10-17): things (rides, shops, people), runtime/native_land.c
`tp_native_things` (the call list) and runtime/native_draw.c (the blitter core
0x15ed21 with its clipping quirks, and the small font 0x180e88/0x180f34).
Checked two ways (TP_NATIVE_THINGS): draw calls — the host logs, during each
game pass, the game's calls to the per-cell function 0x11c22a, the blitter
core, the small font and the one-colour glyph blit 0x111bd5, and compares them
cell by cell with the native list made from the state the game draws from;
and pixels — the screen as the game's things loop starts and ends is
snapshotted and the native list drawn over the first must give the second,
byte for byte. The renderer changes state as it draws (animation steps, a
shared animation 0x1d77a4, the last bystander pose 0x1d7be8, people's
counters, ride sign scroll counters, its random generator 0x118b92); the
native code leaves the state alone and models those. All classes are native
(2 people, 4 rides with the ride renderer 0x169840, 6/0x1c ride signs, 8
staff, 0x0a, 0x0e ride cars, 0x12 stacked rides and trains 0x11feb5, 0x14
entrance, 0x18, static scenery); scaled drawing (FRAME_MODE 8: 0x166dfa,
0x167182) and the selected-ride highlight 0x164940 stay the game's. Results,
0 differ: demo park 320x200 30,921 of 30,921 cells and 1,600 fully native
passes (64,209 blits) pixel-exact; 640x480 67,462 cells, 3,400 passes; a new
game with the mouse over the park 3,826 of 3,826 cells. Classes 4/0x0a/0x12
trains/people sub-kinds were ported by a Claude Code subagent; paths the runs
never reached (class 0x0a sub-kind 4, people sub-kinds 0x32/0x36, the car
frame switch to word 0x90, kind 9 rides) are transcribed but unverified.

Native passes (view.c `native_pass`, TP_NATIVE_PASS 0 off / 1 on, default /
2 check): a pass is drawn natively (landscape from the cache's current
contents, copied to the screen, things in the clip window learned from the
first game pass of each screen mode) unless something above is not native;
then it is the game's pass as before. Game state is saved only before the
first game pass of a frame. Check mode: 1,600 (demo) and 3,600 (640x480)
native passes against the game's, 0 differing pixels. The busy demo park
now draws every pass natively, about ten times cheaper per pass.
Found on the way: the renderer also picks the thing under the mouse
(0x120ff5, gated by [0x1de5ac]) and tests clicks ([0x1de5a0]); on a click it
can open that thing's window (0x135f21). Game passes draw with another camera,
so they now turn both off. Game passes also triggered the renderer's sound
effects for what they drew (0x180fe0); native passes do not.

Later: the native renderer replaces passes; widescreen, hi-res UI, simulation.

## Next concrete step

1. The user's first session: keyboard, mouse, full screen, sound balance.
2. Regenerate driver images automatically (script the dump/mask procedure).
3. SVGA mode (the game's "toggle screen mode"), and a longer real-time soak.

## Quality of life and free mode (runtime/qol.c) — 2026-10-17

Host hooks at game functions (hook slots 12..23; view.c has 0..11; slots now
24, with a 4096-entry pre-check so the per-block test stays cheap), set from
the launcher's new Tweaks and Free Mode pages through the environment:
- **Grossed-out sounds** (Tweaks; TP_EW_GAP): the "ew" is SICK.RAW (bank 0,
  sample 13), played only at 0x122fc5 through 0x180c80 when a visitor reacts to
  a mess in its cell (0x122e22). Every time / at most once per 5, 15 or 30 s /
  off; skipped calls return to the caller at entry. Demo park, 60 s: 11 of 11
  as the game; gap 5: 7 played, 3 skipped; off: 9 of 9 skipped.
- **Ride music** (Tweaks; TP_RIDE_MUSIC, TP_RIDE_TUNES, TP_PARK_TUNE): tunes are
  MIDI (MUSIC0: RIDE1..17.HMP, then jingles). The park renderer picks the
  music ride while drawing (first running ride with a tune on screen, or the
  one under the pointer; [0x1d3ae8], state [0x1d6632]) and at its end plays
  its type's tune (type table 0x1cced0, 29 records of 0xc8, tune byte +0xb7;
  play_tune 0x180a20 returns to 0x11c1c7) or stops (0x180990 from
  0x11c183/0x11c193). The hook at play_tune changes or silences the tune per
  ride type (off redirects to stop); the park tune plays where no ride's music
  does (at stop, and at block 0x11c198 when no ride was seen). Checked in the
  demo park: type 12 tune 1 → 3, → none; park tune 19 becomes current in a
  park with no running rides. Sound and music calls inside the view's game
  passes (other cameras) are now dropped.
- **Free mode** (sandbox parks only, sim level byte 0x1d66f1: 0 sandbox, 1 sim,
  2 full; found by diffing three new parks): visitors cap — the immediate 800
  at 0x167ff6 (0x167cd4, how many leave an arriving bus) is now a variable,
  tp_tune_max_people, made by tools/build_game.py's tunables step (exact
  instruction match; defined in dispatch.c); up to 1000 (the things pool,
  1200 records with rides, staff and cars, gives visitors ~1,100 slots). Bus
  size — the park record's +0x48 raised to the setting (up to 255) as a bus
  arrives (the game's 20..150 by level, table 0x1b78cc). Sandbox park, bus 150:
  ~460 visitors within the test. Larger parks are not possible as a setting:
  the map is a fixed 64x64 grid (4-byte cells at [0x1d3b08]) already walled
  to its edges by 0x118bc0 with hard-coded offsets; a bigger grid means ~350
  map references, the minimap, saves and our renderer.
- **Weather in widescreen**: the weather is drawn in screen space after the
  park (0x147606 rain, colour 0xc0..0xc5 streaks, and storms with lightning;
  0x147a27 drifting particles), flags [0x1b14cc] (bit 0 rain, 1 particles, 2
  storm). view.c captures what each draws (entry snapshot, diff at return) and
  the compositor repeats the rain and particles across the areas beside the
  4:3 column; the storm's palette flash already covers the window. Lightning
  bolts stay where the game draws them, in the column. TP_DEV_WEATHER forces
  a kind for tests.

## Zoomed/widescreen view: coherent frames (2026-10-17)

The player saw garbled sprites when hovering rides or the entrance and the bus
breaking up as it left. Cause: the game draws its frame 100–180 times a second
but the passes ran at most 30 times, and every game frame's real view was
copied into the picture. Things moving across the real view's edges were drawn
from different moments (walkers, the bus, thought bubbles), and hovering moves
the camera (so the edges move over rides and the entrance). Now the real frame
is captured only on frames that had passes (all from one game moment), passes
run up to 60 times a second (~3.5 ms each), the app publishes only those
frames (the game's cursor is still tracked every frame), pointer events that
would move the camera wait until the frame is captured, and a frame whose
camera changed while drawn (the game's own scrolling) is left out.
Checked in the real app window with a 1,000-visitor park (TP_VIEW_VERIFY:
every visible piece of each published picture against fresh native passes
of the same moment; TP_VIEW_OLD restores the old behaviour for comparison):
old, 273 of 1,200 frames had a piece off by more than 3,000 pixels and 4,600
to 7,400 stale pixels a frame; new, none after the first frame. Window
captures (screencapture of the game's window) confirm.

## Zoomed/widescreen view: no popping (2026-10-17, later)

The player saw rides clip in and out while circling the pointer. Measured with
TP_VIEW_TRUST (native passes one camera step apart, same state, compared):
the renderer draws an object from the cell at its back, so a ride whose cell is
above a view's top row is not drawn at all though it hangs ~64 rows down into
the view; things of rows below the drawn rows are cut at the bottom; and
objects of cells beside the view are cut up to ~60 px in. The old trusted part
of a pass (16 px in, rows 0..112) and the game's own frame (copied over the
park, its edges moving with the camera, which follows the pointer) were inside
those zones. Now native passes are drawn taller (192 more rows into a taller
buffer), only rows 80..PH-112 and 64 px in from the sides are used, and the park
picture is native passes only (they draw everything the game's frame did,
thought bubbles included); the game's frame is still used for interface, tool
previews and weather. Also: weather pixels are no longer taken for interface
(rain streaks and lightning were, with park pixels between them, drawn unzoomed),
the cursor capture accepts only pointer-sized changes (bigger ones dragged park
patches to the pointer), and never-drawn visible passes are drawn at once.
Neighbouring passes now differ by under 200 px (walkers) in their trusted
parts; pass cost ~6 ms a frame at 1x, ~9 ms zoomed out. Checked in the real app
window: slow pointer circles at 1x and 0.5x, 1,000 visitors, forced storm.

## Pointer, full screen and the top of the view (2026-10-17, later)

The player reported pop-in near the top, a broken menu bar at the top and a
jumpy pointer. In full screen (real pointer moved with CGEvents, screen captured):
- the app hid the Mac pointer over the whole view, so with the menu bar revealed
  there was no arrow, only the game's cursor drawn under the title bar; and edge
  scrolling at the top moved the park while the pointer went for the menu bar;
- the game's cursor was found by diffing the screen against the back buffer
  around a stale mouse position: moving, it often missed (no cursor that frame)
  or caught park pixels (drawn at the pointer, unzoomed);
- in the date and advisor rectangles, whatever the game's frame had that the
  passes' park did not was shown as interface, including that frame's missing
  rides (it does not draw those anchored above its top): pop-in at the top.
Now the cursor is read from the game (0x17d2dd: [0x1becb4] shown, image
[0x1de590] 16 x 15 rows [0x1de594] apart, 0xfe transparent, corner at the mouse)
and shown as a Mac cursor at the view's scale (capped at 64 points, the largest
macOS shows), moving at the display's rate; over the menu bar macOS shows its
arrow. The panel is published without the game's cursor. No edge scrolling at
the top. The date rectangle uses the passes comparison only when the date was
drawn with the park (otherwise lost). Screen captures: popping blocks while
circling near the top 409 → 74 in 40 frames (the rest is the pointer itself);
TP_VIEW_VERIFY 0 bad frames.

## The game's top bar, and flashing chunks (2026-10-17, later)

- Top bar: with the pointer at the screen's top edge the game draws a bar across
  the top (light blue, 8 rows, under the date) and scrolls its camera up. The
  park view never let the pointer reach the game's top (it moves the camera to
  keep the pointer inside the real view), and the game draws the bar before the
  extras, so it counted as park. Now the layout's top 2 rows of the screen
  column pass the pointer straight to the game (and the bar's 8 rows while it is
  open), the camera is held where it was while the pointer is there (the game's
  scrolling undone before its render), and the bar's rows are interface while
  open. Checked headless and in full screen with the real pointer (the macOS
  menu bar still slides down above it).
- Flashing chunks: passes were refreshed within a 12 ms budget a frame; in a
  full-screen zoomed-out view more passes are visible than fit, so some showed
  their area from earlier frames and jumped when refreshed. Now every visible
  pass is refreshed every frame, and passes are cheaper: the landscape (0.6 of
  0.62 ms a pass) skips sprites wholly outside the used part (TX0..TX1 x
  PT..PB): 0.26 ms. TP_NATIVE_PASS=2 now compares the rows where the game's
  pass is complete (PT..RB: its pass lacks things of rows below it, which the
  taller native pass has): 0 differing pixels, demo park 320x200 and 640x480
  and the test park.

Also (same day): frames were captured twice when an interrupt returned the game
to AFTER_RENDER (a capture after the pointer had moved the camera: ~1 frame in
30 dropped while hovering); now once per frame (0 dropped). The lost first click
after a menu opened: the view stayed "live" for 0.5 s after the park stopped
being drawn, so the window kept showing the park and clicks went through the
park mapping to the wrong place; now it switches to the game's screen from the
second present without the park (a click on a menu's tick buys at once).

Also: a thing drawn by two passes (a ride car across their seam) took its
random choices (an overloaded ride's car jitter, 0x11d... classE; a show's
bystanders) from the game's generator as it ran through each pass, so the two
halves could differ; for display each thing now draws them from a seed of its
own per frame (TP_NATIVE_PASS=2 and the draw-call checks keep the game's
sequence). TP_VIEW_SEAMS (neighbouring passes compared where both are
complete): 0 differences, test park and demo park.

Guest-free test (the player's request: crowds hid problems from the screen-capture
counter): a copy of the test park with the gates closed until it was empty
(work/playtest/data-empty), paused, full screen, slow real-pointer circles at
zoom 1, 0.6, 0.4 and 0.25, screen captured: 0 popping blocks. It found one more
flash: the game presents up to 2 frames without drawing the park in normal play
(after a camera move), and the menu rule above switched to the game's screen
after 2, so for a frame the window showed the game's own 4:3 screen. Now the
switch waits 150 ms without the park (still long before anyone can click).
Unpaused, the only change flagged was a ride sign scrolling its name.

## Player report batch (2026-10-17, night)

- Advisor portrait background changing with the pointer: interface was found by
  diffing the screen against the captured park, so interface pixels equal to the
  park under them showed the zoomed park through (the portrait's edges, text).
  Now, once the park is captured, its pixels in the back buffer are set to a
  marker colour (0xcc: unused by the interface, and its row of the blend table
  [0x1ba2f8] gives only colours the interface never uses); what is drawn over
  them is interface whatever its colours, and translucent interface (bubble
  shading) is recomputed over the real park. The park is put back for the
  screen at PRESENT. The gap filling heuristic is gone.
- The game's top menus (PARK, OPTIONS, DISPLAY and drop-downs): they open with
  the pointer on the top bar; [0x1b14d2..5] say which is open (0xff closed),
  0x1b571a its rectangle (x0, y0, x1, y1, mouse units). While open, the
  pointer goes to the game anywhere in the park column and the drop-down's
  rectangle is interface. Checked in full screen with the real pointer.
- Full screen: the window goes borderless over the display with the Dock and
  menu bar hidden (as the other ports), not macOS's full-screen space (whose
  menu and title bar slide down at the top edge); the picture stays below the
  notch.
- Edge scrolling at the top again and diagonally at corners.
- Debug overlays (View menu ⌥⌘F frame rate, ⌥⌘G section grid; remembered):
  screen, park and game frame rates and drawing time; each pass's complete
  part outlined and labelled with its camera, and the pointer's canvas
  coordinates and camera.
- Speed and pacing: at Original speed the game ran 61 frames a second at 1x but
  85 zoomed out: the host's work between the game's instructions (the view's
  passes) shortened the game's waits on the wall clock. Two changes: (1) the
  retrace, timer interrupt and DOS time follow emulated time (instructions at
  the governed rate) instead of the wall clock: 61 at every zoom; (2) Steady
  pacing (default; Launcher Tweaks: Frame Pacing Steady / Like DOSBox): park
  frames are released at exactly 60 a second at Original (in proportion for the
  faster speeds) by the game thread (a Mach real-time thread waiting with
  mach_wait_until), the instruction governor standing aside meanwhile. Frame
  starts are exactly 16.67 ms apart. The app presents on the display's
  refresh (CADisplayLink), each frame at the refresh after 0.7 of a frame from
  its start: shown 16.4..17.0 ms apart on a 120 Hz display. Composing is
  parallel (bands across the cores) straight into IOSurfaces the layer shows
  (no copy): 8 ms + 11 ms upload → 0.8 ms. Every visible pass is drawn every
  frame. Hitches remain only at the moment of a zoom change.

## Visitor View (experimental) — 2026-10-18

Decision (2026-10-18): the user asked for a first-person "walk your park" mode like Street View
(drop in anywhere, walk at a visitor's height with everything at the right scale, buy things,
ride rides, a ride's video while riding) and, after a survey of options, to build it fully on an
experimental branch (`claude/focused-volta-nsneko`, Claude Code in a cloud session). Chosen: one
pseudo-3D renderer for both an eye-level view and a raised "diorama" view (V switches), from the
game's own state and art; interaction through real visitors (ride along) rather than a pretend
one, because buying and riding are the visitor AI's and faking them would put made-up state into
the simulation.

**What it is** (runtime/walk.c; app: View > Visitor View, ⇧⌘V, then click where to stand):
- Floor: the game's ground, un-sheared. The landscape is a plain shear (cell corner at
  tw (X - cx) + tw/3 (Y - cy) + C0, th (Y - cy); C0 = 0x280 - tw ((0x280 - [0x1d6624]) / tw), from
  0x11b8fe), so each cell's block and decorations, drawn by the native landscape code
  (`tp_native_cell_ground`), become one square of a 64x64-cell texture. Updated per changed cell.
- Things: each drawn by the native things code into a picture of its own (`tp_native_cell_things`
  marks each thing's start, kind 6; the entrance does not redraw neighbours; sign text is kept):
  the game's art with animation, riders, carried items, thoughts. A picture is a card facing the
  camera with its foot on the ground: people and other things placed by bytes 6/8 at their exact
  place; others at the middle of the picture's bottom edge, moved back half an estimated footprint.
- Scale: one for everything (an art pixel is 1/tws of a cell across, aspect/tws up; 320x200's
  pixels are 1.2 times as tall). The eye is at 0.92 of the median visitor picture's height.
  TP_WALK_SCALE multiplies it all if calibration says so.
- Walking: WASD/arrows, Q/E turn, drag (grab) or two-finger scroll to look, Shift runs. Rides
  (class 4, 0x12) and scenery block their estimated footprint; N walks through.
- Visitors: F follows the nearest visitor (you see what they see, where they go, what they buy),
  Space hops to another one ahead, R joins someone riding the nearest ride ahead. Riding is read
  from the rider chains (word 0x47 from class 4 rides and 0x0e cars, 0x0a's one rider, 0x12
  trains' cars through word 0x7a). On a car the camera rides the car (lifted by its stack level);
  on other rides it turns with the ride. If `~/Games/ThemeParkNative/rides/<type>.mp4|mov|m4v`
  (type 0..28, or the ride's name) exists it plays instead, looping, until the ride ends.
- The game's camera follows the walker, so ride music and sounds are those around you.
- Survey aid: TP_WALK_WATCH=1 logs every byte of the followed visitor's record (and its ride's)
  that changes, with the game tick.

**Verified** (tests/check_walk.sh, any C compiler, ASan/UBSan, synthetic memory, no game data):
the snapshot changes no game memory but the camera; the floor's every cell has its own colour
(all 4,096 cells, 9 samples each) and follows changes in both buffers; the cell-to-screen formula
matches the transcribed landscape loop for view offsets -24..40; the drop-point mapping
(`tp_view_point_to_world`, view.c) returns 17,808 points of 24 cameras and 8 offsets exactly; a
visitor's card stands at its place with its foot at the art's; the render puts a visitor ahead in
the middle at the right height and a tree to the north-east right of the middle, and the floor
under the view is the right cell; a tree blocks the way; riding along finds the ride's type and
name. Cost on this (x86 cloud) machine, 1,000 visitors and 300 trees: snapshot 1.8 ms (2.8 ms
with a changed cell), drawing 720x450 4.2 ms.

**Not verified — nothing here has met the real game.** The session had no game files and no
macOS: app/main.m (menu, keys, drop click, status line, ride video) was written but not
compiled; the build scripts include walk.c but were not run. Unknown until it runs on the Mac:
whether real ground blocks are flat parallelograms (raised water edges would be cut), the right
height scale (the art is drawn looking down ~40°, so cards may look squat or tall), how well the
footprint guess fits real rides, whether 0x47 chains on real rides are riders only, the ride-type
lookup for cars, performance with the real art on Apple Silicon.

**Limits, by design until more is reverse-engineered**:
- One art direction: rides and shops are cards turning to face you (their backs are their
  fronts); the art's own ground under a ride stands up with the card. Diorama view keeps near
  the art's own angle, where this shows least.
- You cannot steer a visitor, queue or buy yourself: the visitor AI (where it picks its way at a
  cell, how it chooses a shop or ride, its cash) is not known. Free walking is an onlooker.
- No ride videos were found or checked in the GOG data; the generated seat view is the default.

**To test on the Mac** (scratch profile, muted, bounded, as CLAUDE.md requires):
`bash tests/check_walk.sh`; then build with tools/build_app.sh and either (a) the app with
TP_CONTROL: `walk -1 0` (the middle of the game's view), `walkshot PATH`, `walkcmd 1` (follow;
2 hop, 3 ride along, 4 view, 5 noclip), `walkhold FWD STRAFE TURN SECONDS`, `walkteleport X Y
YAW`, `walkstop`; or (b) tp-headless `--realtime S --rt-keys` with `T:w:X:Y` (X < 0: the view's
middle), `T:p` follow, `T:r` ride along, `T:o` view, `T:n` noclip, `T:t:X:Y:YAW`,
`T:i:FWD:STRAFE:TURN:SECONDS`, `T:g:NAME` (out/NAME.png, 960x600, with the walker's state logged).

**On the Mac (2026-10-18, merged into nightly):** compiled first time; tests/check_walk.sh passes
(with stubs for nightly's pacing); in the real test park, headless and in the full-screen app, the
floor un-shears correctly (paths, flower beds, fence), visitors, shops and rides stand as cards at
a plausible scale, walking, turning, diorama view and following a visitor work, and leaving returns
to the park view (60 frames a second). Not yet checked: riding along, ride videos, footprints of
large rides, the pointer drop point in the app.

**Planted on the ground (2026-10-18, the player: "make buildings feel planted to the ground
better")**: in the demo park (walkshots from the south, north, east, the path) rides and shops
stood where their pictures' bottoms are: rides (class 4/0x0a, placed by bytes 6/8) at their
footprint's front edge, others at the middle; seen from any other side their own ground showed in
front of them, as if they floated. A big thing now keeps its footprint (middle, half its depth:
about the picture's width in cells, rounded, up to 6) and its card stands on the footprint's edge
nearest the camera (to the square's edge, corners a little short), sorted by that point; the
walker's blocked cells are the footprint itself (they were centred on the front edge). Contact
shadows: the floor darkens under each thing (8 samples a cell, smoothed): a big thing's footprint
with a soft rim, a visitor's feet, a soft blob for others; riders none. TP_WALK_PARTS logs one
snapshot's sprites and cards (development). The art's own ground under a building still stands up
with the card (the pictures are one sprite each; nothing marks which pixels are ground).

**Next steps**: (1) run it in a park and look (scale, footprints, flicker, the drop point);
(2) list GAME/ and THEME.CD/ for any ride animations; (3) the visitor AI with TP_WALK_WATCH and
TP_PROFILE: the heading/state/target bytes and the function choosing a visitor's next cell, then
a hook there to steer the followed visitor (Street View arrows at path junctions), which gives
real buying and queueing; (4) which cells are paths (for walking on paths only).

## Larger maps (sandbox parks), 2026-10-18

Goal: 128x128 parks in the non-campaign mode (campaign stays 64x64).

Facts (survey of MAIN.EXE):
- The park is a grid of 4-byte cells at [0x1d3b08], addressed by a cell index
  x + y * 64 (words; 128x128 = 16384 still fits). Things keep positions as 8.8
  words at +6/+8 (cell bytes +7/+9): 128 cells fit, 256 would not.
- The map is the *MAP memory block (size at 0x1b127c) and is saved through the
  save list ((pointer, size) pairs from 0x1b1515; the map's size at 0x1b1521).
- Geometry lives in ~570 instructions: x/y splits (idiv 0x40, the cdq/shl/sbb/
  sar 6 signed division), y << 6 + x, neighbours +-0x40 and +-0x100 bytes,
  bounds 0x3e..0x40, edge rows near the bottom (the bus road, the entrance at
  (32, row 64) - 4 rows), absolute cells off the map base, three "linear walks"
  that step a pointer 4 bytes a cell assuming rows back to back, a memset of
  0x4000, and a random cell (rand % 0x1000).
- And in data: neighbour lists (0x1b4920, 0x1b4b2a, 0x1b8c3e, 0x1b8fbe), steps
  (0x1b8f44), footprints of the 17 shops (0x6b records at 0x1b301a) and 29 rides
  (0xc8 records at 0x1b16f8), and the entrance template (0x1b4cca).

Machinery:
- config/mapgeo.json: the instructions, found by idiom scripts over the survey
  listing and reviewed by hand (the rejected look-alikes are listed too: money,
  sound, cell byte-3 direction bits, screen pixels). tools/build_game.py rewrites
  each in the generated C to read tp_map_w (row stride), tp_map_shift, tp_map_ext
  (playable size) or tp_map_bytes: immediates, shift counts, displacements (rows
  near the bottom edge, column 63, absolute cells kept near their edge or the
  middle column), and appended statements (the random cell; the row skip of the
  linear walks).
- runtime/bigmap.c sets the variables and re-encodes the data tables
  (TP_MAP_W, TP_MAP_EXT; TP_MAP_ALLOC for tests).

Verification so far:
- No-op (64): the patched build reproduces the original exactly (deterministic
  fuzz scenario, 60 hashed shots) and the check suite's golden first park.
- Test layout (stride 128, playable 64, same allocation): identical to the
  64-stride run over 60M blocks of seeded clicks (park set-up, building, walls,
  trees, the bus, the renderer), REP counts included. Tools: TP_DET_CKPT
  (checkpoints at each shot), TP_DET_TRACE (block trace of one interval),
  work-only write-watch build (work/dbg). The fuzz does not open the park, so
  visitors are not covered by it; they are tested in a big park by play.
- Note: a 0x10000 *MAP block at stride 64 departs from the original after 22M
  blocks (less free memory for the game's caches); campaign parks keep 0x4000.

Stage 2 (extent 128, 2026-10-18):
- More sites: the camera (its clamp, centring on the middle column 0x20, the
  tutorial's positions), the entrance gap in the bottom fence (x 31..33), the
  entrance area's rows in 0x159xxx-0x15bxxx, a map-row walker's end
  (map + 0x4100) and the map's memset (the whole block). Rules: x near 0 or y
  near 0 stay absolute; near 63 they count from the far edge; x near 32 counts
  from the middle column. Not geometry (checked): 0x39c5 / 0x39c1 passed to
  0x1349eb are a mask of cell types; 0x3cb0 at 0x113dfb a marker.
- Our code: native_land.c (cells by tp_map_w, bounds by tp_map_ext), view.c
  (camera range E-11, canvas up to 4400x2400, 4096 passes), walk.c (tables for
  128, loops over the extent). Native passes 0 differ at 64 and at 128.
- Which parks: Free Mode > Map Size (FreeMapSize, TP_BIG_MAPS) gives the map
  block 128x128 room at start-up. The game builds a new park (0x119eee, hooked)
  before the player picks the sim level, so it is built 64x64 and, while it is
  being set up, a choice of Sandbox rebuilds it 128x128 (the game's 0x118bc0
  removes the entrance and builds the border and the entrance again; the bus is
  moved and relinked). The first park frame or a load ends the setting up.
  Other parks stay 64x64 and save in the game's own format.
- Saves: a 128x128 park's save has a marker beside it (<save>.big, written when
  the map chunk written is 0x10000 bytes); a read of the map chunk switches to
  the layout of the save (and refuses a 128x128 save without the setting).
  Checked in the app: a new sandbox park at 128 (log, picture), its autosave
  (0xC000 bytes more, marker) loads back at 128; the 64x64 demo park loads at
  64 with the setting on.
- Visual QA in the full-screen app (2026-10-18 night): a new 128x128 sandbox
  park with everything researched (test command "unlockall": the runtime
  tables' flags, rides +0xc5, shops +0x44) and money ("poke32 money": the
  player's money is the *COMPUTER block +0x0a, not the park record): all 29
  rides and 17 shops chosen by list position and placed over the whole map;
  every one that the game accepted draws correctly (roller coasters and the
  monorail need track: the game says so), zoomed out the whole park and its
  east border show. Fixed on the way: the game plays from copies of the ride
  and shop definitions (0x1cced0, 0x1d3c62), whose footprints were 64-wide.
- A 128x128 save can always be loaded: the app gives the map room for it when
  any save has its marker (TP_BIG_ROOM), whatever the setting.
- Menus and hotkeys (zoomed view, the demo park): Park, Options and Display
  open and draw cleanly; F5 / F6 / F7 lists, M world map, O park open, Shift Z
  money work. Not confirmed: D (research) and Shift S (stock screen) did
  nothing from the test script (the game's frame shows no screen opening).
- View fixes: under a top menu the marker is not used (the menu's edge and
  shadow darken through another table: over the marker they came out
  magenta; the camera is held meanwhile, so the frame difference is exact);
  blend patches touching the park or one or two pixels thin are shadows over
  the park, not window colours; the date box keeps only the pixels the date
  drew (stale pixels a frame 23 -> 2 in the picture verify).
- Downward scrolling (2026-10-18, the player: "scrolling downward jumps and goes too fast"):
  with the pointer on the screen's bottom row (the panel's bottom edge) the game scrolled its own
  camera a row every frame, and the zoomed view re-centred on it each time (the jump and the
  speed; the top bar already held the camera, and the sides never reach the game's edge). With
  the view on, the game's pointer is now kept two pixels off the screen's edges and the app pans
  at the panel's outer edge at its own steady rate. Also for 128 parks: the view kept the game's
  camera within the 64 park's range (rows/columns to 51, so the pointer was pinned to the game
  view's edge below that) — now tp_map_ext - 13; and the game's own scroll limits 0x1143c5 /
  0x11443b (mov ebx, 0x40) are map-geometry sites (615). Checked in the app (TP_VIEW_DEBUG):
  pointer on the bottom row, no camera moves (before: 51 -> 52 -> 53 -> 54 with re-centres);
  camera reaches row 64 on a 128 park.

## Fence gaps in 128x128 parks; the black pointer (2026-10-18)

The player: large gaps in the fences of large sandbox parks; the hand pointer black at a new game.
- Fences: the border routine 0x118bc0 still had three 64x64 numbers: the side fences' loop (0x119221, rows
  1..55: the sides stopped there, 65 rows short) and the bottom row's entrance gap (0x11901a / 0x119021,
  columns 30..34: a hole of five). Map-geometry sites now (tp_map_ext - 8, tp_map_ext / 2 -/+ 2; sites 634).
  Parks made before keep their holes in their saves: a loaded 128x128 park gets the game's rule again on its
  empty border cells at its first frame (bigmap.c repair_border). Checked: a new park's sides 119/119 rows,
  the bottom row open only at the entrance; the old TEST park, 133 pieces filled.
- Pointer: the app rebuilt its copy of the game's pointer only when its shape changed (view.c's serial),
  so one made while a new game faded its palette in stayed black. It now also rebuilds when the pointer's
  colours change (main.m: a hash of its pixels): 30 rebuilds during the fade, none after.

## EXTRAS: this port's options in the game's own top bar (2026-10-18)

The player asked for the new options that make sense mid-game in the top menu, in a section of their own,
in the game's own bar. The bar is data (survey of 0x14fd85 update, 0x150723 draw, 0x150092 click):
- the list of top menus, {drop-down, text} words at 0x1b53b6 ended by text -1 (it sits just before the
  first drop-down, so it cannot grow in place: its two uses, 0x14fdf2 and 0x150723, read tp_menu_bar, a
  build site);
- drop-downs of 0x354 bytes at 0x1b53c6 + n * 0x354 (the array is full: 3 and 4 are Load and Save, 5 an
  OPTIONS submenu, then other data): rectangle, +0xa the item under the pointer, from +0xc items of 0x15
  bytes: submenu (-1), text, kind (bit 0 a tick from the byte at +5, bit 1 a line under it), +5 tick byte,
  +9 another, +0xd function and +0x11 its word (set into 0x1d3828 / 0x1d6622, called by the main loop at
  0x1122b2), or +0x13 / +0x14 a key pressed instead (0x113bf3); 0x1b14d4 the open drop-down;
- texts: words into the table [0x1d7bcc] (239 entries, made once by 0x12e3f9 and never freed).
Done (qol.c, host.c): the linear arena keeps a 64 KiB hole at 0x1c40000 (n must be a signed word: below
~0x1c55000); there qol.c builds, at the park's first tick, the game's list plus EXTRAS, its drop-down
(n = 32666, at 0x1c4004e) and a copy of the string table with the new texts after the game's (re-made if
the game ever makes another). Items call the options function 0x150ccd with 0x7700 + k (its switch has
0..13; anything else does nothing), taken by a hook (H_OPTIONS; the hook table is now 32). Items, all
taking effect at once: Ride Music, Park Music, Fewer Ew Sounds | Steady Frame Pacing | Smooth Pixels,
CRT Lines, Mac Pointer; their ticks follow the settings every 8 ticks; choices are saved to the app's
settings (main.m bridge), except in test runs (DS_TEST: in memory; a test once switched the player's
Park Music on through the shared settings domain, put back). view.c's open drop-down rectangle now
follows 0x1b14d4. Checked in the player's park: EXTRAS beside DISPLAY in the game's style, ticks right,
choosing toggles (park music on, ride music off), check suite and view tests pass. TP_NO_EXTRAS=1 leaves
the bar as the game's.

## Game windows stay widescreen (2026-10-18)

The player: opening the ride or shop catalogue switched the picture from widescreen to 4:3 (a jerk each
way). With a full-screen game window the game stops drawing the park; after 150 ms view.c reports the
view not live and the app used to lay out the game's 4:3 screen (stretched or boxed). Now, in the wide
view, the layout stays: the last park picture at the sides, the game's screen in the column its interface
takes in the park view (the panel's), every frame, the pointer mapped into that column (main.m, present:
menuWide). Checked full screen with the player's park: the shop catalogue opens in the column with the
park either side, its close button works, the park view returns. Not handled: Panel Size above 1 (the
window's panel is drawn at 1x), and 640x480 screens (they fall back to the old way).

## Ride music: no more tunes stepping on each other (2026-10-18)

The player: ride music steps on each other. The game plays the tune of a music ride in its own view
(play_tune 0x180a20, which returns at once for the tune already playing; stop 0x180990 when no music
ride is seen). In the wide view the game's camera follows the pointer, so moving it over the park
stopped, restarted and swapped tunes many times a second (TP_SOUND_LOG "music: now playing": 18 changes
in 20 s of pointer sweeps). Now a playing ride tune stays until its ride has been out of the game's view
for 3 s (qol.c, TP_MUSIC_HOLD; both the stop and a request for another tune wait): the same sweeps, 1
change (the tune starting). Ambient ATMOS2.RAW (bank 0 sample 21, every ~0.5 s) is the game's.

## Bouncy castle riders (2026-10-18)

The player: no one bouncing on the bouncy castle (other rides perhaps too). The ride renderer 0x169840
chooses how riders are drawn by the ride type's byte 0xb3: 0 and 1 (seats, turning), 5 and 7 (seats),
8 (the maze: people at random places), 9 (the bouncy castle): if word 0x32 is clear, no riders, else as
5 and 7 (0x169a59 -> 0x169a6f); 2, 3, 4, 6 none. The native port had turned 9 without word 0x32 into 2
but never sent 9 with it down the seat path: the castle stayed empty. Fixed (one condition); 3, 4 and
6 draw no riders in the game either (tubing and race cars, coasters, the tower family: their cars are
things of their own). Checked in the player's park: riders on the castle in the app as in the game's
frame. (Found by tracing: the "bouncer" first suspected was the maze's, drawn correctly.)

## Top bar: no magenta edge (2026-10-18)

The player: a pink line at the bottom of the menu bar at the top. Seen full screen with the pointer on the
top edge (the blue bar, before a menu drops): its dark bottom row is drawn by darkening what is under it
through another table (as the menus' edges, PLAN above), so over the marker it came out as colour 0xff,
magenta (TP_TOPBAR_DEBUG showed row 7: screen ff, park 00). The marker is now off while the pointer is on the
top bar, as under open menus. Checked full screen: a dark edge, as with the view off.

## Sound: the card in the game's time (2026-10-18)

The player (nightly, their own park, sound on): some effects choppy or distorted; the game slows for a
moment when some effects (the "ew") play.
- The HMI driver reads the DMA count (channel 1, register 3) about 480 times a second: two bytes per
  240 Hz timer tick (TP_DEBUG logs it), mixing a little ahead of the play position each tick (~4 ms).
  The card used to be advanced by the audio callback, ~512 output frames (~12 ms) at a time: the
  position jumped past what the driver had mixed, so the card played unmixed or stale data.
- Now the CPU thread advances the card (sb16_pump, realtime_events and the pacing wait, which sleeps a
  millisecond at a time) by the wall clock into a ring; the audio callback plays from it (sb16_play,
  ~25 ms behind; fills up again after running dry; skips ahead after a stall over ~93 ms). The card's
  samples are interpolated linearly to 44.1 kHz instead of held. Headless runs use the same pump.
- Still coarse: a native pass (~3.5 ms, on the CPU thread) holds the card meanwhile, at the edge of the
  driver's mix-ahead. If crackles remain, look there first.
- Checked (the player's save, sound and music on at their request): no output underruns in 60 s, park
  ticks steady (worst gap ~28 ms), check suite and view tests pass. TP_AUDIO_REC=<file> records the card's
  output (44.1 kHz stereo float).
- The slowdown: reproduced with GrossOutGap 0 (the player's 30 s gap skipped most "ew"s): park ticks
  stopped for 0.4-1.1 s after some "ew"s. Measured per tick gap (TP_SOUND_LOG: game code, governor,
  pacing, view lock, passes): ~97% was the frame-pacing wait, with the timer interrupt at 100-150 a second
  instead of 240. Then the game runs short frames (one that waits on its timer counters while the effect
  starts; the HMI driver mixes on every tick), and the timer interrupts, delivered between instructions,
  got two or three a frame between the 16.7 ms frame waits. Now a frame that finds the timer more than a
  period overdue is not paced (tp_host_timer_behind). Checked twice, 19 "ew"s each: no park tick gap
  over 60 ms after loading, timer 240 a second, 60 paced frames a second. (Not the governor, not the view
  lock, not the passes: each measured; a short-slice run loop did not help.) TP_STALL_PROFILE=<file>
  writes the blocks of the first tick gap over 200 ms.

## Up to 10,000 visitors in 128x128 parks, 2026-10-18

The player: "i would like the guest limit on the bigger maps to be closer to 10,000 and we will
need to adjust a few things just in case the guests are not staying long enough to reach the
entire park in the new sizes".

Survey (MAIN.EXE):
- Visitors, rides, staff and cars share the things pool (*THING, descriptor 0x1b1323: 0xc8-byte
  records at [0x1d3b10]). The game's size is 1200. It is allocated at 0x1107fe (and again at 0x110842
  after freeing something), starting at 1200 records and trying ten fewer each time the heap is short.
  The size then appears only in [0x1d3b78] (-> the last record, set at 0x112a34, add 0x3a8b8), which every
  loop over things runs to (~40 compares), in the new park's clearing (0x119f38, push 0x3a980), and in
  the visitor allocator (0x12845a): it searches up to the last record - 0x4e20, keeping the last 100
  for rides and the like. The save list saves the block whole (its entry's size is 0x1b1529).
  No other descriptor and no other static table is sized by it. Indices are words (some read signed),
  so up to 32767. 0x190ed6 (idiv 0x4b0) is unrelated fixed-point arithmetic.
- Arrivals: 0x167cd4 returns how many get off a bus (at most the bus size, a byte: <= 255), from the
  park's appeal; the caller (0x1283a6) creates them. HORZA with bus 255 and no cap held ~180.
- Staying: a visitor is ready to go home once (any of) 4000 ticks have gone since its clock (+0x75,
  reset with its tiredness by sub-kind 4 at 0x167343), or its tiredness (+0x82, rising as it walks,
  weather makes it worse) is over 8000 or at least 2000 x the park record's +0x82 (byte), or the park is
  closed (+0x18). The predicate is inlined five times (0x12287c, 0x12308d, 0x1231bd, 0x12386b, 0x1240a4
  for the time; 0x1231a4, 0x12408b for tiredness; 0x1279eb and 0x167c3c give the tired thought). When
  ready, a visitor turns for the gate as it next passes one (0x1e/0x40, then 0x22: 0x12698e walks to the
  bus, 0x126a91 counts it out). Visitor sub-kinds dispatch through the key table 0x121723 (0x25 keys).

Done (bigmap.c, qol.c, config/mapgeo.json):
- Five map-geometry sites: tp_things_alloc (1107fe, 110842), tp_things_n (112a34: (n-1)*0xc8),
  tp_things_bytes (119f38), tp_things_spare (128473). With the Map Size setting on, the pool is
  allocated for 12,000 records at start-up. A 128x128 park uses all of them, keeps n/25 records spare
  instead of 100, and saves them all (0x1b1529); its marker is now "128 n" (an old "128" marker
  means 1200). After the read, the rest of the pool is cleared and comes into play, so a park saved
  before this change gets the full pool too. Every other park uses 1200 records and saves exactly as
  the game does.
- Visitors: in a 128x128 park the cap is 10,000 (the Visitors setting stays for 64x64 parks, up to
  1,000), and a return hook at 0x1283dc multiplies each bus's arrivals by 4 (TP_FREE_ARRIVALS),
  up to the room under the cap.
- Staying: in a 128x128 park the time is 8000 ticks and the tiredness 16000 (twice the walk across;
  tp_stay_ticks, tp_stay_tired, nine sites). The 2000-per-attraction rule is the game's (with fewer
  than eight attractions it comes before the tiredness limit). Sites now 629.
- walk.c follows the pool (its last record) instead of a fixed 1200.
- Checked (scratch profile, muted): new 128 park allocates 12,000; the TEST 128 park with an old "128"
  marker loads with 1200 records and grows to 12,000 (save list 0x249f00); its year-end autosave is
  2,513,820 bytes with "128 12000" and reloads; HORZA (64) in the same kind of session: 1200, 0x3a980.
  HORZA with TP_THINGS=12000 and TP_FREE_ARRIVALS=60 / 100: one bus brought 2,580 / 4,300; the game
  ran with 2,600 / 4,477 visitors (records past 2,600 / 4,500) at ~90 ticks a second either way
  (as with 180), CPU 61-91%, no faults. Check suite and walk tests pass.
- Not checked: a real 128 sandbox park with rides (how near 10,000 it gets with x4 arrivals; the
  bus count park +0x47 / player +0x5ee still shows the game's number), visitors actually reaching
  the far side with the doubled limits, and 10,000 at once. The cap compares the game's in-park count
  (+0x1f: visitors through the gate), so it is approximate; the pool (12,000 - 480 spare) is the
  hard ceiling. The herd tests swung in and out together (one bus of thousands); not seen at x4.
- TP_THINGS=n (development) gives every park n records; a 64x64 park's saves still hold 1200, cut short
  (the year-end autosave overwrote the HORZA test copy once: restored from a backup), so scratch copies only.

## Endless and True Freeplay (sandbox parks), 2026-10-18

Decision (2026-10-18): the player asked for an endless mode for sandbox parks that "start[s] out at
the difficulty of basically the first couple of levels and then ramp[s] up to the hardest as you
play", with research as normal, and a "true freeplay where everything is unlocked with infinite
money". Launcher > Free Mode > Rules: Sandbox (as before) / Endless / True Freeplay, and Endless
Ramp: 3, 5 (default) or 10 game years. Campaign (Sim, Full) parks are never touched.

Survey (memory diffs on the player-details screen; reads of each byte in the build):
- the screen's choices: 0x1d66f1 sim level, 0x1d66f2 park visitors (0 happy .. 2 fussy),
  0x1d66f5 start level (0 easy .. 2 hard; the screen's default 1), 0x1d66f6 opponents level;
- what they do: start money 0x1b7f7c[start level] (200,000 / 150,000 / 100,000 -> 0x1d6718, then
  the player's block, *0x1d3b00 + 0x0a); a park being bought (0x16e8xx) writes its record
  (0x1cf1e0 + 0x250 n, the player's at [0x1d3818]): +0 = 200 + 50 fussy (a visitor's +0x54 is
  compared with it, 0x132c95), +2 = -200 + 50 fussy (changes in play: not touched), +4 = 100 + 40 (2
  - start), +6 = 25 + 15 (2 - start), +8 = 104 + 20 (2 - fussy); a new visitor's +0x54 =
  (2 - fussy) * 50 (0x128534). The bus table 0x1b78cc is indexed by the park's bus upgrades (+0x49),
  not a difficulty. The month: the player record's +0x5ed ([0x1d3814], 0..11); no year counter
  was found, so Endless counts months itself.
- Research needs nothing: sandbox researches as the other levels do (only the QA scripts unlocked).

runtime/freemode.c: a new park's rules (or a loaded one's) are settled at its first frame (view.c's
FRAME_RENDER, held on until then: the setting-up screens come after the park is first built);
then every park tick (a hook on 0x1139ad, the return from the tick's call at 0x1139a8: a block the
dispatcher enters, where 0x1139a8 itself is reached by a jump inside the block function):
- Endless: at the start Easy's money and the record's values for difficulty 0; each month
  d = 2 x months / ramp (to 2): the record's values in proportion, the start/opponents bytes
  rounded, and the visitors byte dithered between its neighbours each tick, so visitors arrive on
  average as fussy as d;
- True Freeplay: every ride's and shop's researched flag (the runtime tables, as the QA unlock) and
  the money topped up to 5,000,000 whenever it falls below 1,000,000.
The details screen's bytes are the player's for the next game: Endless keeps what they were at its
start (a loaded Endless park: the screen's defaults, Happy / Medium / Easy) and puts them back when a
new park is started or another park loaded (checked: ramped to 2/2/2, Park > Restart, back to the
player's 0/1/0) — the game itself re-initialises only the start level (0x11a0b7).
A park's rules, months and ramp are kept beside its save (<save>.free, "rules months ramp"; host.c,
with the 128x128 marker), so a loaded park keeps its own rules whatever the Launcher says.

Checked in the app (muted, scratch data, TP_FREE_LOG): a new Endless park starts with 200,000 and
the record 200/180/55/144; seven game months later (3-year ramp) d = 0.39 and the record is
219/164/49/136; the autosave at the year end wrote "1 11"; continued with the Launcher on Sandbox
it loaded as Endless at month 11. True Freeplay: money 5,000,000, the Flight Sim (late research)
in the ride list at once. tests/check_macos.sh --park passes.
Not known: what +4/+6/+8 govern in play (the game's own easy..hard values are interpolated).

## Advisor only while talking; Panel Size (2026-10-18)

- The advisor's face stayed after his bubble went: the gap rule refilled his
  pixels from last frame's interface. Gaps are now filled only within 2 px of
  interface the game drew this frame. Checked in the full-screen app: the face
  goes with the bubble.
- Once he has spoken the game keeps drawing his face for good (checked with the
  view off). A view rule hiding it without a bubble was made and taken out (the
  player: the game's own Options > Advisor, unchecked, removes him outright).
- Game windows were speckled and their insets black (the installed 0fa4694):
  the two colours a blend over the marker gives are also window colours. Each
  patch of them is now settled by what it touches: the bubble's white (text,
  darkened), other interface (kept as is), only park (blended over the park).
  The ride window now matches the game's own screen pixel for pixel.
- Panel Size (launcher Tweaks; View > Larger / Smaller Panel, ⌘] / ⌘[;
  default PanelScale; TP_UI_SCALE in headless runs): the panel and the
  advisor's band (60 rows above it) drawn 1.25 to 2 times larger, anchored at
  the bottom middle, never wider than the view; pointer events there map back
  to the game's screen. While a game window crosses the band (more than 1/8 of
  its top row is interface), the panel is shown at its own size so the window
  fits. In a 4:3 window there is no room, so it stays at its own size.
  Checked: real clicks on the enlarged buttons in the full-screen app, picture
  verify 0 bad frames at 1 and 1.5, native passes 0 differ.

## Speech bubble and date corruption (2026-10-18)

Moving the pointer, the advisor's bubble showed visitors through its text and
the date had park specks. Found in the app (real pointer, screen captures, logs):
- The bubble text is drawn as a darkening blend over what is under it, not as a
  colour. Over the marker that gives two colours (0x6c/0x6e) that many sources
  share; it was redone over the park at that point (a visitor's shirt) instead
  of over the bubble's white. Now: over the brightest non-blended interface
  pixel within 2 px, taking the darkest result among the possible sources:
  black text, as in the game.
- A gap rule filled missing interface from the game's park picture (visitors);
  it now uses the last frame's interface only where it was interface.
- The date: drawn with the park at times, so things could cover it, and only
  some frames captured it. Its box (from the extras) is kept; every frame its
  pixels come from the screen, and anything not a date colour from the last
  clean capture.
- Frames where the game skips drawing the park (AFTER_SKIP) keep the last park
  capture instead of taking last frame's picture (interface and all) as park.
- The marker is no longer lifted around the tool preview and weather (it was
  not always put back: frames fell back to the old difference method).

## Reused from decomp.wtf (2026-10-17)

The user forked i12bp8/decomp.wtf (MIT, discontinued 2026-10-15) and asked what
helps here. It is a matching-decompilation orchestrator for console games (C
that the original compiler turns into identical bytes; N64, PS1/2, GameCube,
Wii, GBA, DS; Linux/Windows only): its period-compiler toolchains, byte scorer,
permuter and hand-written-assembly signs (PowerPC/MIPS register rules) do not
apply to a Watcom-built DOS game recompiled statically. One idea does: ranking
look-alike functions by instruction sequence (its similarity.py, after coddog).
Adapted as tools/survey_similar.py over survey_disasm's listing: `--at ADDR`
ranks a function's look-alikes, `--groups` lists families of near-identical
functions (MAIN.EXE: 45 families, 109 of 1,812 functions at 0.92, 30+
instructions). Checked against known pairs: the small font 0x180e88 / 0x180f34
(320x200 / 640x480) 0.94; the blit wrappers 0x15ec9c / 0x15ecd4 identical. Use
it before porting a game routine natively, to port its variants with it.

## Done so far

- 2026-10-17 (Claude Code, headed playtest; the user saw "visual instability,
  especially mousing over the entrance"): the app under live control
  (TP_CONTROL=<file>: key/type/raw/shift, mouse/vmouse/click/vclick, zoom/
  center/pan/size/cycles, shot/vshot, sleep, ack; work-only client and
  scripts in work/playtest, scratch data, muted, launcher skipped) built a
  sandbox park with the HORZA cheats (Shift+C money, Alt+Z rides, Ctrl+Z
  shops, Shift+Z scenery: they work through the app's BIOS shift flags):
  paths, eight rides and shows (five kinds), three shops, scenery, two guards, then opened it ('O')
  to visitors. Zoomed-view bugs found and fixed (view.c):
  - the real frame was copied whole into the canvas, but like a pass its sides
    and bottom miss objects whose cells are outside it (the entrance's arch
    when its cell is just below): as the pointer moved the camera, the arch
    and other tall things flickered. Now only its complete part (TX0..TX1,
    rows above TY1) is copied; passes draw the rest — the reported bug;
  - the tool preview (0x1149a4 from the interface 0x1145fb: a ride's or
    shop's ghost, path and scenery previews) is drawn after the park is
    captured, so it showed unzoomed at the screen's scale. Its pixels now go
    into the park picture (snapshot at its entry, diff at its return), and
    while one shows the pointer is kept low and central in the game's view so
    the ghost is not cut by the view's top;
  - drags (paths, queues) bent into L and U shapes: the camera moved under the
    game while the button was down or as it went down. No camera moves now on
    a button change or during a drag (past the view's edge the game scrolls
    itself, as at 1x), and the app does not recentre on a drag's scrolling;
  - the date (and advisor buttons) are sometimes drawn with the park, so they
    could be stamped into the canvas or vanish from the screen; they are left
    out of the real-frame copy, always redrawn by passes, and anything there
    that differs from the passes' park is interface;
  - interface pixels equal to the park pixel under them (text over a road, a
    speech bubble's white over white flowers) showed the zoomed park through
    them: gaps up to 4 px with interface on both sides are interface.
  Checks during the playtest (TP_NATIVE_PASS=2 + TP_NATIVE_THINGS in the app):
  88,600 native passes against the game's, 0 differing pixels; 125,022 cells'
  draw calls, 0 differ, nothing not native. Demo park headless at 0.161 /
  0.4 / wide 1x: 5,600 passes checked, 0 differ.
  Not done: queues for the built rides (the game calls them rails; the
  entrance's front tile was not found), so no one rode them — riders were
  covered by the demo park; track rides not built; the first click after a
  menu opens was sometimes lost in the app session (never headless, events
  all reach the game's callback: unexplained); weather (rain, lightning) is
  drawn in screen space by the game, so it does not zoom and covers only the
  4:3 column in widescreen.

- 2026-10-17 night (Claude Code, the user away; budget: under 40% of tokens):
  widescreen park, Tab fast forward, start in 640x480, pause when hidden, Save
  Park Picture, language choice, SoundFont music, drag pan, CRT scanlines, game
  controllers; the view no longer slows the game (landscape cache restored,
  passes throttled; measured equal game-day rate); native landscape layer
  (stage 3 start), pixel-exact against the game. Stage 3b things (section
  above): all classes native, checked by draw calls and pixels; the zoomed
  view now draws natively (native passes; launcher Display → Zoomed Drawing
  switches back to the game's passes); passes no longer pick under the
  mouse or take clicks. Two 300 s fuzz soaks (seeds 3, 7) with the
  wide view at 0.6: no stops, state unchanged by passes at all 40 checks each.

- 2026-10-17 (Claude Code): zoom shows the game's cursor and tool previews, game
  windows take clicks while zoomed, and the game's 640x480 mode works (R in the
  park; VESA in the host) with zoom in both modes. Launcher Controls page lists
  the zoom keys. `--park` check green.

- 2026-10-17 (Claude Code): real-time zoom, engine rebuild stage 1 (runtime/view.c,
  section above). Pinch / ⌘-scroll / ⌘= ⌘- ⌘0 zoom, scroll or arrows pan, edge pan;
  1x is the game's own view. Checked headless and with the installed app's
  background self-test (zoom out, tool through the panel, click in the zoomed park,
  back to 1x); `--park` check green. Not yet played by hand.

- 2026-10-17 (Claude Code): the user found "slow" about 20% too fast. Cause: the
  governor counted a REP string instruction once, where GOG's DOSBox (normal core,
  cycles=fixed 10000) charges every MOVS/STOS/LODS repetition (core_normal/
  string.h; SCAS/CMPS are free) plus an I/O delay per port access. Repetitions are
  ~48% of a running park's count, so old "Original" ran ~1.9x GOG's speed:
  measured over 90 s of the scripted park, 18 game months old vs 9.6 counted the
  DOSBox way. Counting now matches DOSBox; the launcher's speeds are Original
  (exact), 1.25x, 1.5x, 2x, 4x, and a saved old Original moves to 1.5x once
  (about 20% slower than before, the user's estimate). `--park` check green.

- 2026-10-16 night (Claude Code): the user asked for the game's artwork from
  ScreenScraper or Steam and a launcher like Dungeon Siege's. Theme Park is not
  on Steam, and ScreenScraper's API needs developer credentials, so the art
  comes from GOG's public store API (the store this copy is from), downloaded at
  install time into `~/Games/ThemeParkNative/art` (key art, logo, box art; not
  committed). The launch window now follows Dungeon Siege's layout: key art
  banner with the logo and "Native Edition for macOS", a framed box of main
  choices with a line under each value, big group buttons with summaries, a
  group as a panel over the window, Quit/Play. Dungeon Siege's stone and wood
  become the title screen's grass and fairground-bulb signs. The icon uses
  the box art's planet (sharper than the 256 px .ico). Checked by rendering
  every page and the icon; clicks were not made by a person.

- 2026-10-16 late (Claude Code): the user said the launcher and icon looked bad.
  Decision: restyle both to the other ports' shared look instead of the busy
  park backdrop. The launch window now follows Freedom Force/Brutal Half-Life
  (banner with the title screen's logo box scaled in whole steps, faint
  big-wheel motif, gold labels, pop-up menu fields, outlined group buttons,
  Quit/Play, build line). The icon is made by `tools/tp_icon.m` (geometry and
  Nightly band from Freedom Force's `ff_icon.m`): GOG's park planet without
  its ring on a sky tile, fed to iconutil from 1024 px instead of a 256 px
  upscale. The 16 s demo-park render is gone from the install. Checked by
  rendering every launcher page and the icon at 1024/128/32 px.

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
