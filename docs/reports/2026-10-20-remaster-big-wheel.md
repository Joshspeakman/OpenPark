# Big Wheel candidate — 2026-10-20

The owned source has a compact six-cabin rotor, gold arms, an indigo central
star, blue canopies and a yellow/ivory base. The new local model preserves that
arrangement and the six native keys from chain 92. Work stays on
`codex/reimagined-full-remaster`; Main/Nightly and installed applications are
unchanged. This is unaccepted candidate art, not a completed remaster.

## Model and passenger movement

The model contains 4,071 attribute vertices, 6,334 triangles, eight rigid groups
and six passenger attachments. Geometry includes a stepped plinth, pale inset
deck, rear support legs/feet/braces, axle, machinery housing, tapered octagonal
arms, reinforced star, hub, cuffs, suspension forks, closed curved canopies,
rimmed lower shells, recessed cabin floors, inner walls, cushions and rails.
Generated painted-metal materials replace the unsuitable reused yellow tile.

One physical wheel has radius 1.55 world units. The six keys advance it in
ten-degree steps; after each frame wrap the native phase moves the actual
guest into the next ordinal slot. All six identities follow continuous 36-step
circuits, including the final return. The cabins remain upright rather than
rotating their passengers upside down. Model dimensions and camera settings
do not vary per frame. This establishes geometric/frame/phase consistency,
not the running game's loading, speed, stop/start or wall-clock cadence.

The existing eight canonical guests gain two keys each: the source's full-body
cabin sitter and its omitted state. Their previous geometry, materials and
every existing pose line are unchanged. Seated legs use the same articulated
body; cabin geometry provides view-dependent occlusion.

The native ride's sprite-31 marker consumes a passenger even when the source
omits their body. New keys 65448–65451 preserve that real occupant separately
from the car's existing sprite-31 keys. A source-hidden flag does not remove the
guest from the 3D scene. Complete rendering still requires every exact pose.

## Refinement and verification

The initial suspension crossed the passenger space. It now connects behind
the cabin, clear of the head and torso. The lower shell initially had a solid
cap at its rim; it now has an actual recessed interior so hips, legs and feet
do not intersect that cap. Positive clearance against the rotor plane, inner
roof, floor and inner-wall envelope is checked for all eight guest appearances.
This is not a general triangle-collision or final contact-animation acceptance.

The previous close ride-inspection camera cropped this tall model. An optional
bounded `TP_SNAPSHOT_RIDE_DISTANCE` now lets the offline tool move back without
rescaling artwork. The final Visitor captures use distance 9, pitch −.17 and the
same eye height for every frame; selected views were visually inspected. All
orthographic images use focal 85 and have clear borders. Four malformed distance
values reject with exit code 2. Existing default camera behavior is unchanged.

- 540 controlled native probes cover six keys, six phases, all eight uniform
  appearances and empty/partial/mixed occupancy. Every expected identity,
  source-hidden state and exact authored pose resolves.
- 150 final captures cover both actual normal native modes, Visitor and fixed
  orthographic inspection at all 36 frame/phase combinations, plus six opposite
  Visitor viewpoints. Earlier close/capped diagnostics are not final evidence.
- Every rigid transform retains unit scale. All cabins remain upright and
  attachments match their cabin matrices. The fixed base reaches ground zero.
- 648 comparisons against the independently compiled previous native renderer
  match all 8,904 source events and indexed pixels; memory remains unchanged.
- Twelve native remaster sanitizer checks and 21 Python tests pass. The omitted
  marker fixture now covers both rides' marker families, all appearances and
  both modes; separate car/ride domains and the upper invalid key are checked.
  Both walk modes, 17,808 view checks and mesh/model/rig/layer tests pass. The
  final added domain assertions pass in `final-snapshot-test.log`.
- The isolated app compiles with installation disabled and passes deep/strict
  signature verification. No app launch or headed test occurred. All 112 owned
  resource hashes and 107 existing non-guest model hashes remain unchanged.
  Eight guest files gain poses; one ride model is added, for 116 candidates.

Private evidence is in `work/remaster/rides/wheel/`: `verification.json`,
`geometry.json`, `source-comparison.json`, `integrity.json`, guest additions,
source references, captures and logs. The final capture log is
`verify-framing.log`; these are offline snapshot probes, not live timing tests.

## Generated materials and remaining work

The built-in imagegen tool created a four-quadrant coating atlas: yellow,
sky-blue, ivory and indigo. The prompt requests clean original-game colours
and restrained coarse paint texture, without objects, wood grain, labels or
rounded toy imagery. The decoded atlas is packed into the actual 3D model.

Private project files:

- Model: `work/remaster/rides/models/04-12.rig`.
- Image: `work/remaster/art/wheel-materials.png` (1254×1254).
- Exact prompt: `work/remaster/art/wheel-materials.prompt.txt`.
- Provenance/decoded input: `work/remaster/art/wheel-materials.json` and
  `work/remaster/art/wheel-materials.rgba`.
- Authoring: `work/remaster/rides/authoring/wheel.py` and
  `work/remaster/characters/authoring/wheel_riders.py`.

Derived art remains ignored per repository policy. The current source ledger
and `work/remaster/catalog/models-current.json` record this unaccepted candidate.
No partial pack is installed or marked complete. Remaining artwork/animations,
special track composites, terrain/effects, non-car ride cameras, full-pack
performance and final headed acceptance remain unfinished. Source-art fallback
does not count toward the user's no-placeholder requirement.
