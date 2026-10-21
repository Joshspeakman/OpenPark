# Flight Simulator candidate — 2026-10-20

The local candidate reproduces the owned source's closed silver cabin, yellow
nose, two dark front windows, short side windows, red/black bumper, orange
side fins, coil spring and shallow red plinth. It uses all six native keys in
chain 311: 1350, 1351, 175, 1353, 176 and 177. This remains unaccepted candidate
art on `codex/reimagined-full-remaster`, not a completed remaster.

## Cabin, spring and occupants

The model contains 1,972 attribute vertices, 2,678 triangles, 51 transform
groups and five passenger attachments. The cabin has a tapered octagonal
shell, chamfered roof, inset opaque windows and raised frames, side door seams,
latches, sills, closed fins, marker lamps, underside mount, interior floor,
five seats and restraints. Generated coatings provide neutral silver, red,
gold and dark rubber material tiles.

The three-turn spring uses 49 connected cross-section rings. Each ring keeps
its wire radius while following the changing centreline; adjacent strips use
the same joint transform, avoiding gaps between independently moving pieces.
Its bottom endpoint stays fixed to the base and its top endpoint follows the
cabin mount. Cabin pitch and heading follow the six selected source keys,
including the matching reverse-swing keys. Dimensions are constant.

The original opaque cabin has no passenger sprite markers, despite holding
five linked guests. Previously that left an occupied remaster model without
the required passenger poses. Shared scene resolution now retains those
actual guests using an explicit authored enclosed-sitting key, 65452. This
is deliberately distinct from a source-selected sprite pose. It applies only
to the exact Flight Simulator definition, capacity, kind and six known keys,
and rejects unexpected native rider metadata or malformed occupancy.

Eight new keys extend the existing canonical guest rigs by reusing their
full seated posture. Every previous geometry/material payload and pose line
is unchanged. Cabin transforms carry the actual guests and seats together;
the closed exterior supplies occlusion in both renderers.

## Refinement and verification

The first capture had an overly tall spring, a cream-coloured roof and a
side-facing cabin. The final candidate has a shorter connected spring,
shallower plinth, newly generated neutral silver coating and revised cabin
heading/pitch. These are authored reconstructions from the source samples,
not recovered mechanical measurements or accepted live timing.

- 648 source-free scene tests cover both projections, all six frame keys,
  zero through five passengers, all eight appearances and mixed identities.
  They verify actual person IDs, exact enclosed poses, unchanged inputs and
  rejection of malformed capacity, definition offset, kind, variant,
  over-capacity chains and conflicting source passenger metadata.
- 168 actual native occupancy/phase probes resolve exact occupants and poses.
  Source image checksums remain identical for each frame across occupancies;
  the Flight Simulator's seat order does not rotate with the phase counter.
- 360 final captures cover both normal modes, Visitor front/rear/side views
  and native orthographic inspection. All resolve the complete ride model.
- 324 occupied-versus-empty image comparisons are pixel-identical. All guest
  bodies remain present inside the opaque shell without bleeding through it.
- All 40 guest/seat combinations have positive clearance against the actual
  tapered cabin skin and floor. The minimum measured clearance is 0.00114
  world units. This is a static seated-envelope check, not contact-animation
  or general whole-model collision acceptance.
- All six states retain rigid transform bases, a grounded plinth, exact seat
  attachments, constant wire radius and fixed/moving spring endpoints.
  Nonadjacent spring-turn clearance exceeds 0.0148 world units, and the cabin
  remains at least 0.236 world units above ground. Six inspection borders are
  clear with fixed camera settings.
- 648 old/new native passes across both modes and three ride substates match
  1,944 source events and indexed pixels, with unchanged simulation memory.
- Twelve remaster native checks and 21 Python tests pass. Both walk modes,
  17,808 view checks and mesh/model/rig/layer checks pass. The isolated app
  compiles and passes signature verification with installation disabled.

The complete-pack requirement remains enabled. No app was launched or
installed. Main/Nightly, other worktrees and the existing separate install
were not changed. The private inventory contains 118 candidates; all 109
other models and 112 owned resources retain their hashes, while the eight
guest files have only the documented pose addition.

## Local evidence and remaining work

Ignored local files:

- `work/remaster/rides/authoring/flight_sim.py`, `flight_verify.py`
- `work/remaster/characters/authoring/flight_riders.py`
- `work/remaster/rides/models/04-0.rig`
- `work/remaster/rides/flight-sim/model.json`, `verification.json`, `geometry.json`
- `work/remaster/rides/flight-sim/source-comparison.json`, `preservation.json`
- `work/remaster/rides/flight-sim/guest-additions.json`, `previous-guests/`
- `work/remaster/rides/flight-sim/captures/`
- `work/remaster/rides/flight-sim/phase-cycle-ortho.gif`, `phase-cycle-visitor.gif`
- `work/remaster/art/flight-materials.png`, `.rgba`, `.prompt.txt`, `.json`

The GIFs package unchanged renderer captures at six frames per second for
inspection; that rate is not measured game timing. Runtime start/stop,
loading/unloading, high-speed shake and overlay states remain unaccepted.
Remaining art families, terrain/effects, special track states, ride cameras,
complete pack creation and final headed captured acceptance are unfinished.
