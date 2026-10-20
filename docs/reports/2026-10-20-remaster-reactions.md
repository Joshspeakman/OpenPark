# Guest reaction authoring and native placement — 2026-10-20

The full Reimagined remaster remains in progress. This is an implementation
checkpoint, not release acceptance or a partial-art installation.

## Runtime

Guest rigs now use the actual native single-frame draw key and origin for
ordinary and reaction states. Different sprite crops do not move the model.
Native bench/elevated frame offsets become model height rather than ground
translation. Invalid identities, carried items, thought/selection overlays
and substituted multi-part riders still require their complete bindings.
An exact authored pose must exist before a rig can render.

The offline snapshot tool accepts `guest FRAME VARIANT yaw [state SUBTYPE]`.
Guest inspection uses a fixed camera across identities and reaction frames,
and records the native origin, height and eligibility. It does not change
simulation state in the running application.

## Local artwork

Eight guest candidates now have 55-bone articulation for head, jaw, facial
features, alternate skin surfaces, hands and props. The first two identities
have individually authored candidates for their eight reaction-table chains:
121 and 105 source keys respectively, alongside the existing 128 walking
keys. The poses include enlarged heads/hands, the watch gesture, pointing,
green-face changes, sleep glyphs, thumbs-up, shock and tears.

These performances are not copied between identities by sequence index.
The second guest points with the opposite hand, covers her face and uses a
folded handkerchief. The original frame keys and repeated visual states are
retained. Runtime cadence is still owned by the game; no new animation clock
was added.

Review corrected the pointing direction, inward-crossed spread legs,
disconnected mouth/jaw motion, mirrored sleep glyphs, sleeveless clothing and
neck attachment. Source-derived geometry, authoring, references and renders
remain ignored under `work/remaster/characters/`. These are still **candidates**:
source silhouettes, motion fidelity, hand/face intersections, remaining guest
actions and identities, staff, actual riders and other asset families need work.
The source dispatch of reaction slots 7/8 remains unverified; their native
replays explicitly identify the plain-frame fixture used.

## Verification

- Three native sanitizer tests and eleven Python tests pass. New fixtures
  cover 24 guest states in both screen modes, independent source crops,
  overlay guards and vertical offsets. The synthetic bench record retains
  the renderer's unsupported-class diagnostic; it is not bench-art coverage.
- Both walk modes, 17,808 view checks and existing mesh/model/rig/layer tests
  pass.
- Native ARM64 compile/signature passes with installation disabled.
- A 36.61-second muted scratch-profile headed **Original-mode** regression
  captured both views, was visually inspected, and exited cleanly. Evidence:
  `work/remaster/runtime-g_mtwnbz/`. This does not validate remastered artwork.
- All 112 source DATA hashes are unchanged.

Authoring evidence includes `models.json`, `verification.json`,
`native-verification.json`, `action-source-poses.json`,
`action-code-references.json`, `reaction-tests.log`, `reaction-build.log`,
`reaction-headed.json` and `reaction-source-integrity.json`. Native replay
reports distinguish frame-draw checks from live timing/animation acceptance.

No new application was installed. The completion manifest still has no
accepted replacements; the full release gate remains closed. Full-colour,
higher-resolution normal-view rendering also remains unfinished.
