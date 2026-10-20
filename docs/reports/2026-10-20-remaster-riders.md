# Native rider poses and ride placement — 2026-10-20

The complete Reimagined remaster remains in progress. No asset acceptance,
complete animation coverage or new application installation is claimed.

## Runtime

Native ride capture now records the substituted guest identity, source pose,
mirror/flip flags and explicit visibility for each seat independently of
Faithful layers. Reimagined uses that exact pose key; the previous common
`65535` seated pose is no longer a fallback. Unknown keys, mismatched identities,
missing occupants and duplicate seat metadata reject the replacement.

Standard rider keys reserve 65024–65279 in four groups of 64, with 47 valid
slots per group. Alternate keys reserve 65280–65295 in four groups of four,
with two valid slots per group. These are attachment poses, not composite
frame indices or animation timers. Source-defined empty substitutions stay
empty. A ride's authored attachment matrix supplies placement and orientation.

Eligible single-frame rides now use the native composite origin, independently
of sprite cropping and occupancy. Overlays and unsupported draw dispatches
still require their own complete bindings. The existing regular ride kinds
0/1/5/7/9 are covered by this metadata path; vehicle/train dispatches and maze
bystanders remain unfinished. Ghost flag 4 is not accepted by this key format.
Two duplicate source-table offsets resolve to the first native table match.

The offline snapshot tool adds `ride FRAME TYPE yaw [guests IDENTITIES]`.
It creates isolated native things in a copied memory snapshot, reports the
actual seat metadata, and uses a fixed ride camera across frame keys.
It does not run or retime the simulation.

## Local artwork

The owned rider tables have 392 entries across eight identities: 47 standard
and two alternate entries per identity. Their reference sheets remain local.
They include standing, seated, tumbling and slide-integrated art; they cannot
all be represented by one generic sitting pose.

The eight existing guest meshes now have 23 candidate bouncy-castle attachment
poses, covering neutral, raised-arm and crouched states where the source has
visible art. Identity 3's neutral substitution is intentionally empty.
The compact landing brace was corrected after an overly wide first pass.
These use the walking characters' actual geometry and costumes.

A local castle candidate has 7,170 vertices, 7,868 triangles, ten body bones,
four rider attachments and eleven native frame keys. Its fixed foundation,
towers, bed and entry are separate geometry. Rider jump displacement comes
from source foot baselines; no independent animation clock was added.
Geometry, proportions, material use and contact still require visual review.

The built-in image-generation tool produced a dedicated vinyl atlas after
the earlier generic material read as roof/masonry on the inflated surfaces.
Saved asset: `work/remaster/art/castle-vinyl.png`; exact prompt:
`work/remaster/art/castle-vinyl-prompt.txt`; provenance and SHA-256:
`work/remaster/art/castle-vinyl-provenance.json`. All derived artwork,
authoring scripts, models and captures remain ignored under `work/remaster/`.

## Verification

- Three native sanitizer tests and eleven Python tests pass. Fixtures cover
  actual native rider substitutions without a Faithful layer, alternate and
  mirrored pose keys, explicit empty poses, different poses for the same
  identity, crop-independent placement, empty rides and both screen modes.
- Both walk modes, 17,808 view checks and mesh/model/rig/layer tests pass.
- Native ARM64 compile/signature passes with installation disabled.
- A 36.76-second muted scratch-profile headed **Original-mode** regression
  captured both views, was visually inspected and exited cleanly. Evidence:
  `work/remaster/runtime-m2ut32dm/`. This checks shared code, not new artwork.
- Offline sanitized native castle replay produced 407 captures: eleven
  frame keys, eight identities at four angles, plus zero-to-four occupancy
  cases. Every capture confirms a Reimagined rig and the actual seat order.
  Evidence: `work/remaster/rides/native-verification.json`.
- All 112 source DATA hashes remain unchanged.

These counts are technical checks, not accepted visual or live animation
coverage. Guest actions, staff/operators, remaining rides and track assemblies,
full normal-view remaster rendering, effects/resources and complete runtime
coverage remain unfinished. The completion manifest has no accepted assets;
the full-release installation gate remains closed.

## Captured-state follow-up

Replaying the previously captured native thing arrays exposed two eligibility
errors that the synthetic occupied fixture did not cover. Bouncy-castle
loading can have linked guests while native dispatch intentionally omits
rider substitutions; the body now remains eligible with empty attachments.
The stored overlay key can also remain nonzero while inactive. Eligibility
now follows the native speed threshold and actual drawn-frame count instead
of rejecting that inactive key.

`tools/remaster_ride_replay.c` validates the actual captured seat bindings,
renders the isolated ride through Visitor View, and draws the same assembly
with the orthographic baker's projection. All 80 captured states replay,
covering eleven frame keys and eight samples with linked guests but no
attached rider geometry. The native origin remains fixed at
55.979168,110.937500. The sequence matches the original captured frame list.
The overhead view prompted a reduction in raised bed seams.

Evidence: `work/remaster/rides/replay/`, `replay-verification.json`,
`replay.log`, `loading-tests.log`, `loading-walk-tests.log` and
`loading-build.log`. The report hashes the actual model and capture files.
Three sanitizer tests, eleven Python tests, both walk modes, 17,808 view
checks and native compile/signature pass after the fixes.

These are replays of saved things, not fresh headed captures, measured
display pacing, full loading choreography or an integrated high-resolution
normal-view renderer. The surrounding terrain is still original artwork.
No new application was installed.
