# Normal gameplay complete-model rendering — 2026-10-20

Work is isolated on `codex/reimagined-full-remaster`. Main/Nightly and the
installed study app are unchanged. This connects existing candidate models to
the normal renderer; it does not complete or accept any artwork family.

## Implementation

`runtime/remaster_world.h` consumes the shared native scene descriptors within
the normal colour pass. The complete body, selected frame and every occupied
guest pose resolve before any assembly pixels are written. Modular tracks
likewise require every component. Passengers retain the canonical walking
identity and exact native pose, including source-hidden occupants. Ordinary
cars use the shared physical route transform and inherit it into their seats.

Models share a world depth buffer. Flat terrain rejects buried geometry while
a small bias permits coplanar authored surfaces. The rasterizer now supports
an explicit output stride, exclusive scissor and normal-world orthographic
depth bias; existing zero defaults retain the Visitor/baker behavior.

The original native indexed pass still runs. It supplies picking, interface
extraction and existing bookkeeping. Model/source event replay uses a separate
indexed buffer, preserving source order, ghost operations and copied sign text.
Events wait until a complete root subtree is available, including nested
entrance redraws. Missing models keep the existing source presentation.
Malformed scopes, oversized event collections and allocation failures invalidate
the colour pass rather than publishing a partial result.

The normal view owns its model cache on the game thread, separately from the
Visitor render thread. Colour publication and the existing UI/weather/tool
composition path remain in place. The launcher and complete-pack gate are
unchanged; the partial candidate profile is only used by offline inspection.

## Verification

- Twelve native remaster sanitizer checks and 21 Python tests pass. New checks
  cover atomic failure for missing final passengers/components, source-hidden
  occupied poses, terrain occlusion, geometry draw-order independence, nested
  source replay, callback text lifetime and malformed/overdeep scopes.
- Both native modes draw a complete synthetic model into the normal colour
  surface with unchanged indexed output and matching private indexed replay.
  Original/Faithful mode isolation and colour-surface clipping pass. The
  synthetic triangle is an engineering fixture, never installed artwork.
- Padded colour strides, untouched depth outside the scissor and the expanded
  world depth bias pass. Both Visitor modes, 17,808 view checks and the existing
  mesh/model/rig/layer checks pass.
- Forty-eight distinct images cover the candidate spinner's six native frame
  keys and four phases in both normal modes, using the actual `view.c`
  `native_pass` implementation. Each resolves one complete ride and all four
  attached guests. Visual inspection includes a source-art comparison. The
  initial off-centre diagnostic crossed the pass scissor; the centred captures
  fit inside the trusted region. No model dimensions were changed.
- Six adjacent-pass comparisons agree at every shared pixel: 11,114,496 pixels
  in total. This checks stitching/camera translation for the occupied candidate,
  not every asset or a fully populated park.
- The isolated app builds with installation disabled and passes deep/strict
  signature verification. Existing platform deprecation and unused-variable
  warnings remain. All 112 source resources and 114 candidate models retain
  their recorded hashes. No new app install or game launch occurred.

Private evidence is in `work/remaster/normal-render/`: `capture.c`,
`capture-results.json`, `carousel-sequence/`, `check-overlap.py`,
`overlap-results.json`, `verification.json`, test logs and `build.log`.
The captures replay a copied snapshot with controlled ride records. They are
not live simulation timing, headed captures or final animation acceptance.

## Remaining work

Unresolved objects still use source/baked-part drawing. Those pixels do not
contribute physical geometry depth, so mixed source/model overlap is not a
completed rendering solution. Full authored coverage is required; falling back
to original art does not satisfy the user's no-placeholder rule. Flat native
terrain also remains source art in this inspection profile.

Remaining artwork and animation families, terrain/effects, special track/train
composites, non-car ride cameras, full-pack memory/performance and final headed
acceptance remain unfinished. Separate normal/Visitor caches can duplicate
model payload and still need a measured full-pack budget. No partial pack is
marked complete or exposed by bypassing the launcher gate.
