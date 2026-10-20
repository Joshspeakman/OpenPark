# Reimagined Merry-Go-Round — 2026-10-20

The local remaster has a new four-cup spinner candidate matching this game's
Merry-Go-Round. The owned source shows orange-backed metal cups on a blue/white
turntable, not a horse carousel. The model uses the six native frame keys from
animation 95 and the actual passenger assignment supplied by the game.

Work remains on `codex/reimagined-full-remaster`. Main, Nightly and installed
applications are unchanged. Derived geometry, authoring, source references and
captures stay under ignored `work/remaster/`. This is unaccepted candidate art;
no partial pack was installed and the full remaster is still unfinished.

## Geometry and movement

The model has 3,204 attribute vertices, 6,080 triangles, six rigid groups and
four passenger attachments. It includes a grounded stepped turntable, blue edge,
rolled pale rim, radial seams and rivets; each cup has a foot ring, shaped metal
bowl, recessed orange seat, thick half-round cushion back, closed inner/outer
surfaces and a front restraint. The same physical dimensions serve every frame
and both projections. The physical platform and cup paths are circular; the
source's hand-drawn projected offsets are reference evidence, not separate
per-frame mesh fitting.

The six keys represent fifteen-degree steps. Cup positions advance around the
platform in one direction while their facing turns the opposite way. Passenger
assignment uses the native rotation phase; after a six-frame wrap the same
guest continues in the next ordinal cup slot. Every guest's 24-step path and
heading has been checked through all four wraps. This establishes the authored
sequence's consistency with native seat dispatch, not that the game advances
the phase at a measured wall-clock cadence.

The eight existing canonical guests gained forty exact rider keys, including
the observed back-facing and mirrored states. Existing seated keys are reused
only when their full authored matrices agree. Geometry, materials and every
previous pose line are unchanged. Each passenger retains their own body, face,
clothes and proportions. No separate generic rider was introduced.

## Tools and verification

The snapshot tool accepts an explicit native ride phase for isolated probes.
A bounded optional orthographic focal setting controls only the inspection
camera. The original fixed framing clipped the edge of this larger platform;
all normal captures were regenerated with one fixed focal value of 128. Every
normal image now has a clear three-pixel border. No asset dimensions were
changed to make the captures fit.

The general ride replay tool now uses the same occupied-pose resolver as the
runtime, retaining source-hidden passengers when their authored poses exist.
It preserves the model cache across samples, records native phase, validates
its numeric arguments and accepts either 64- or 128-cell recorded maps.

- All 312 native frame/phase/occupancy combinations produce 936 sanitized
  captures: two perspectives and one normal projection. Checks cover all eight
  identities, mixed occupants and zero through four guests. Actual native seat
  identities, exact posture keys and visibility metadata agree with expected
  descending/cyclic assignment.
- All four guests have complete 24-step geometric path/heading checks,
  including sixteen guest-specific frame wraps in total.
- The ride itself has 24 sanitized model inspection views. Forty new guest
  posture keys have 160 sanitized individual views. Previous geometry and
  postures compare exactly with their saved pre-change models.
- The earlier native demo recording contains this carousel only in the idle,
  unoccupied state. Sixteen copied states replay into 32 images, all retaining
  frame 4239 and phase zero. This is not running-ride or live timing evidence.
- Nine native sanitizer checks, 21 Python tests, both walk modes and 17,808
  view checks pass. The source-free rotating-ride fixture now explicitly checks
  native guest reassignment across eight phase values. Five malformed replay
  arguments and four malformed snapshot phase arguments reject cleanly.
- All 112 owned source resource hashes remain unchanged. Runtime/native game
  code did not change this turn. No app build, game launch or install was needed
  for these art/tool changes; headed remastered acceptance remains outstanding.

Private evidence is in `rides/carousel.json`, `rides/carousel-verification.json`,
`rides/carousel-idle-verification.json`, `characters/carousel-poses.json` and
`characters/carousel-pose-verification.json` under `work/remaster/`.

## Remaining work

Operating lifecycle observation, actual phase/timing advancement, acceleration,
loading/unloading, special overlays, breakdown/repair states, exact hand contact,
ride-along camera behavior and full visual fidelity still need verification.
Offline normal-projection captures do not establish integration into the live
normal gameplay pack. The remaining ride definitions and all other unfinished
asset/animation families remain within the full remaster scope. This candidate
is not a release fallback or an accepted substitute for missing artwork.
