# Reimagined normal-view scene capture — 2026-10-20

Normal gameplay can now emit complete object scopes and native animation,
passenger, track-component and vehicle metadata without adopting Visitor
View's different entrance/sign behavior. The live normal renderer still uses
its existing per-part drawing path; complete-model painting is not connected
yet. This is groundwork for that integration, not a finished visual mode.

Work is isolated to `codex/reimagined-full-remaster`. Main, Nightly and installed
applications are unchanged. Derived inputs, comparison executables and captures
remain under ignored `work/remaster/normal-scene/` and `build/`.

## Capture behavior

`tp_native_things_metadata()` follows the same native pass traversal and draws
as `tp_native_things()`. It adds object begin/end events, native-selected frame
origins, actual passenger pose markers, track components and car track/bounce
bindings. The event contract is in `runtime/native_scene.h`.

Entrance redraws nest neighboring things inside the entrance's object scope.
Those scopes remain balanced, including static scenery and objects with no
drawn frame. Normal sign clipping and entrance scrolling text remain present.
Visitor cell capture retains its intentional suppression of neighbor redraws
and its offscreen sign text. Entering either capture path leaves a subsequent
ordinary pass unchanged. The stream is game-thread owned and nonreentrant.

`runtime/remaster_capture.h` now collects frame, passenger, component and car
metadata independently of source pixels. Visitor capture uses this same helper;
normal pass consumers can use one instance per nested object. It retains
component bounds, duplicate-attachment rejection, car-track validation and
explicit errors for missing/truncated memory. No animation timer, guessed pose
or artwork substitute was added. Eligibility, model loading and drawing remain
separate from metadata collection.

## Verification

- A source-free sanitized fixture compares 24 ordinary and metadata passes
  across both native screen modes, both RNG modes, both entrance orientations
  and three vertical camera positions. Original events, copied sign strings,
  indexed pixels and all 64 MiB of game memory match. Sixteen passes exercise
  nested entrance redraws. Sign bounds, Visitor exceptions, scope balance,
  metadata reset, duplicate passengers/car bindings and truncated-memory guards
  are checked explicitly.
- A separately compiled copy of the pre-change renderer from commit `00f3d06`
  provides an independent native comparison for two owned memory snapshots.
  Across 144 passes through the recorded 64-cell demo and 760 through the
  expanded 128-cell park, all 40,092 original events and rendered indexed
  pixels match. Game memory remains unchanged. The added stream has 11,812
  balanced object occurrences, 16,276 frame events, 720 passenger events,
  4,168 track components and 62 car origin/track pairs. These are repeated
  occurrences across cameras, not unique assets or animation coverage counts.
- Shared capture is exercised by the existing native origin, shop/ride rider,
  3,408 track-assembly, 1,440 car-movement, 3,008 rider-binding and seat-camera
  checks. Eleven remaster native sanitizer checks and 21 Python tests pass.
- Both Visitor modes, all 17,808 view checks and the mesh/model/rig/layer tests
  pass. Seventy-two occupied carousel captures (six frames, four phases, three
  views) match the preceding images byte-for-byte after the capture refactor.
- The isolated app compiles without installation and passes deep/strict
  signature verification. The existing two unused-variable warnings in
  `view.c` remain. No app launch or headed capture was performed in this step.
- All 112 owned source resource hashes and 114 candidate model hashes are
  unchanged. No artwork was accepted or release gate relaxed.

Evidence includes `demo-comparison.json`, `expanded-comparison.json`,
`verification.json`, `carousel-regression.json`, `capture-tests.log`,
`walk-tests.log` and `build.log` in the private normal-scene folder. The final
header-declaration cleanup is also compiled and exercised by
`final-snapshot-test.log`.

## Next integration work

The normal paint path needs to collect each object's complete source draws,
resolve its model and all attached poses before replacement, and project its
geometry through the native camera into the full-colour surface. Indexed
drawing must retain original picking/state behavior. Nested entrance scopes,
deferred text lifetime, clipping, zoom, world depth, UI/weather/tool priority
and the model cache's owning thread all need explicit handling.

The shared metadata collector does not yet move the eligibility/placement
rules from `walk.c` into a common scene descriptor. That is the next concrete
step before connecting whole-model painting; duplicating those rules for normal
gameplay would allow the two views' passengers or physical track paths to drift.

Full normal-view rendering, remaining artwork and animation families, populated
park performance and final headed remastered quality acceptance remain
unfinished. Existing complete-pack requirements and the user's no-placeholder
rule still govern release.
