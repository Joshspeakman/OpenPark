# Reimagined shared placement — 2026-10-20

Native frame/attachment capture now resolves through one view-independent
scene descriptor. Visitor View uses it for canonical model eligibility,
placement, occupied poses, modular tracks and car state. Normal-pass consumers
can resolve the same descriptor without building or cropping a source card.
The live normal renderer is not connected to complete-model painting yet.

Work remains on `codex/reimagined-full-remaster`; Main, Nightly and installed
applications are unchanged. No artwork or release acceptance changed.

## Implementation

`runtime/remaster_scene.h` extracts the canonical-model rules previously held
inside Visitor card construction. It receives read-only memory, captured native
metadata and an explicit projection reference. It returns the model class/type,
selected frame, ground placement and separate height, exact passenger markers,
native entity identities, physical-car inputs and placed track components.
It does not load artwork, infer animation, draw pixels or modify simulation.

Normal passes use a common camera reference; Visitor capture uses a per-cell
scratch-image reference. The projection includes the current thing's cell
origin so elevated guest states preserve their height in both contexts. Track
and car height remain separated from ground coordinates. Invalid/truncated
contexts, malformed frame keys and oversized assemblies reject before use.

Ride seat rotation now carries native entity IDs alongside appearance variants.
Two same-looking guests remain distinct as the native phase advances. Shop
attachment occurrences also retain the actual linked customer ID. Repeated
markers for one customer retain that same identity; this does not invent extra
customers or establish final contact/animation quality. Non-car ride-along
cameras still need implementation; this descriptor change does not supply them.

The canonical vehicle-state type and native seat-order helper moved to shared
headers. Legacy card silhouettes and Faithful layer rules remain in Visitor
presentation. Model loading, complete attachment resolution and rendering remain
separate; descriptor eligibility is not proof that an authored model exists.

## Evidence

- The synthetic normal-pass fixture compares descriptors produced from normal
  camera coordinates with those from the equivalent per-cell scratch capture.
  Model keys, poses, entity identities, body placement, heights and every track
  component agree. Existing exact native-position tests continue to pass.
- In two owned recorded parks, 7,838 eligible object occurrences resolve equally
  through both coordinate systems across 904 passes. This is repeated camera
  coverage, not a count of unique assets. All 40,092 original draw events and
  indexed pixels still match the independently compiled pre-change renderer;
  simulation memory remains unchanged.
- New rider assertions cover actual entity rotation, two guests sharing one
  appearance, empty-seat identity clearing, and shop customer identity across
  both native modes, 23 source slots, eight variants and four flips.
- Eleven native remaster sanitizer checks and 21 Python tests pass, including
  the existing 3,408 track assemblies, 1,440 car movements, 3,008 rider bindings
  and exact seat-camera cases. Both Visitor modes, all 17,808 view checks and
  mesh/model/rig/layer checks pass.
- All 72 occupied carousel captures (six frames, four phases, three views)
  match the previous images byte-for-byte. This is a rendering regression,
  not final animation fidelity or normal gameplay integration acceptance.
- The isolated app compiles with installation disabled and passes deep/strict
  signature verification. The existing two unused-variable warnings in
  `view.c` remain. No app launch or headed capture occurred in this step.
- All 112 owned resource hashes and 114 candidate model hashes are unchanged.

Private evidence is under `work/remaster/placement/`, including the two park
comparison JSON files, `verification.json`, `carousel-regression.json`, test
logs and `build.log`. The final added customer/empty-seat identity assertions
also pass in `final-snapshot-test.log`.

## Remaining integration

The normal paint layer must use these descriptors to resolve every model and
attachment before replacing its source draws. It still needs actual geometry
projection into `TPRemasterPaint`, world depth, pass clipping, zoom, nested
entrance ordering, deferred text handling and explicit cache ownership. Original
indexed drawing must continue serving picking and game state, with UI/weather
and construction tools composed correctly.

Remaining artwork/animation families, other ride cameras, complete normal-view
rendering, populated-park performance and final headed remastered acceptance
remain unfinished. The complete-pack release gate and no-placeholder rule are
unchanged; this is not a releasable partial art mode.
