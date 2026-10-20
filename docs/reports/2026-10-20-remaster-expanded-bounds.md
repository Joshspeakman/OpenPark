# Expanded remaster drawing bounds — 2026-10-20

Character overscan checks found that all 128 current walking candidates
extend beyond their legacy sprite crops. The remaster pack now supports
replacement rectangles independently of the source rectangle. This avoids
cutting off modeled feet or changing an actor's scale/position to fit a crop.
It does not complete the character art or animation work.

`TPRART02` retains the original bank/index, dimensions and decoded-pixel
source hash, then adds signed x/y offsets and replacement width/height.
Pixels remain palette indices with 256 for transparency. Display dimensions
are bounded to 2048 per side, offsets to ±2048, and decoded allocations to
64 MiB across the pack. The previous `TPRART01` format remains supported.

The normal world pass, native terrain/decorations, and Visitor snapshot
capture use the expanded rectangle. The original source rectangle remains
the mirror/flip pivot; transparent padding cannot move a mirrored actor.
Snapshot dirty bounds include the added pixels. Expanded records use bounded
rectangle clipping. Original mode, UI drawing and legacy RLE records retain
the existing native blitter behavior.

The production compiler accepts optional `display_bounds` on a completed
part record: `{x, y, width, height}`, in decoded source-pixel units. The
overhead bake must uniformly scale that rectangle. Source binding still
uses the original dimensions/hash. Alias checks include placement, and
padding unchanged original pixels does not qualify as remastered artwork.
Compilation and installation still require complete release evidence;
this is not a partial-pack release path.

## Evidence

- Native sanitizer tests cover source binding, all four mirror/flip
  combinations, partial clipping at each side, transparent pixels, ghosting,
  colour override, original-mode isolation and malformed bounds/pixels.
  Expanded landscape and thing drawing agree with independently positioned
  expected pixels.
- A source-free native snapshot fixture captures all 64 pixels of an
  expanded 8×8 replacement for a 4×4 source, retaining the shop's native
  model origin.
- Three native sanitizer tests and eleven Python tests pass. Both walk
  modes, 17,808 view checks, and existing mesh/model/rig/layer checks pass.
- Compile-only native ARM64 app build and signature verification pass.
- A 36.54-second muted scratch-profile headed regression exited normally
  after capturing both windows. Both captures were visually inspected:
  populated normal park, intact interface, and occupied Visitor ride scene.
  **This run used Original mode and does not validate Reimagined artwork.**
- All 112 owned DATA files match their pre-run catalog hashes; the test
  process exited. No application was installed.

Evidence: `work/remaster/characters/expanded-bounds-*.log`,
`expanded-bounds-headed.json`, `expanded-bounds-source-integrity.json`,
and `work/remaster/runtime-ifpt9gvf/`.

The full remaster remains incomplete. Expanded bounds do not provide HD
overhead rendering, complete pose fidelity, missing animated composites,
or full-pack performance/visual acceptance. Those requirements remain open.
