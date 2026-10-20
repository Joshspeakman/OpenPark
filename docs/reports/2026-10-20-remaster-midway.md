# Reimagined midway shop candidates — 2026-10-20

The full autonomous remaster remains incomplete. This is internal production
evidence, not an accepted art pack or a playable remaster. Work stays on
`codex/reimagined-full-remaster`; Main, Nightly and the installed app are
unchanged. No application was installed or launched in this work.

## Geometry and source poses

Five local candidates add Coconut Shy, Race Track, Duck Shoot, Gun Shoot and
Tin Can Alley to the existing twelve shop candidates. The five models contain
12,579 vertices and 18,409 triangles in total. Their 280 M-family source keys
were found by matching each shop's base sprite across composite frames:

| Shop | Source keys | Keys containing customer-marked parts |
| --- | ---: | ---: |
| Coconut Shy | 69 | 43 |
| Race Track | 64 | 45 |
| Duck Shoot | 80 | 56 |
| Gun Shoot | 34 | 29 |
| Tin Can Alley | 33 | 32 |

The definition chains alone contain only 32 keys. Base-sprite matching expands
the source inventory; it does not prove every frame's live trigger or pacing.
The five Gun Shoot default-chain keys have identical source appearances; no
invented recoil loop was added to those keys.

Each non-customer source part occurrence has an independent rigid group.
Native frame keys choose exact part offsets, visibility and alternate geometry
for shopkeepers, coconuts, toy racers, ducks, tins and wooden target panels.
There is no independent animation clock or sinusoidal substitute. Customer
parts are recorded separately and are **not modeled or accepted by this work**.
The existing runtime guard continues to reject occupied base-only shop rigs.

Initial front and side renders exposed excessive booth depth, floating parts,
below-ground backboards and intersecting roof undersides. Architecture now
uses measured source projection landmarks with explicit physical depths,
closed side/back structures and tiled roof undersides. Separate roof ornaments
and support stems were repositioned after side inspection. Coconut cups and
striped posts use the target source positions. All five use canonical geometry
for both normal-projection bakes and perspective views.

These are still candidates. Shape/material fidelity, ornamental asymmetry,
shopkeeper bodies below the original cropped busts, target-panel articulation,
source-part occlusion, architecture proportions and live animation timing need
further review. Render success and a grounded bounding box are insufficient
for visual acceptance. There are no new completion entries or release assets.

## Customer mapping

A separate local map joins all 23 shop substitution slots to eight guest
identities (184 bindings), retaining each frame's offsets and size corrections.
Across these five shop bases, 205 frames contain customer-marked parts.

Ten occurrences use sprites 58 or 65, which are absent from the substitution
table. Inspection shows standing and arms-raised forms of the first guest.
The existing native `classA` renderer draws such an original part for identity
zero, skips it for other identities when no table entry matches, and skips all
marked parts when no customer is linked. The map records this exception instead
of inventing a slot or silently dropping the source occurrence. Runtime
reachability of those frames still needs evidence. The first twelve shop base
sprites have no marked-customer occurrences in this particular source search;
that does not establish complete shop/customer behavior.

## Evidence and remaining work

Local authoring and evidence remain ignored under `work/remaster/shops/`:
`authoring/midway.py`, `authoring/midway_structure.py`, `midway-models.json`,
`midway-renders/`, `midway-native/`, `midway-bakes/`,
`midway-verification.json`, `customer-map.json` and their logs. Source imagery,
geometry and scripts derived from owned assets are not committed.

Current-model verification covers 280 poses, 1,120 sanitized fixed-camera
inspection images, 1,120 sanitized native unoccupied shop-path captures and
280 padded orthographic bakes. Every native capture logs a Reimagined rig;
all pose bounds touch z=0 without extending below it. PNG checksums, model and
capture hashes, nonempty bakes and clear overscan borders are recorded. These
are offline checks, not headed gameplay or occupied-state acceptance.
All 112 owned DATA resource hashes remain unchanged.

No runtime code changed, so the prior runtime test/build results are not
presented as new checks here. Remaining work includes actual shop-customer
geometry and native attachments, source-specific hand/face performances,
normal-view part-pack production, integrated timing/occlusion checks and all
remaining ride, track, terrain, scenery, character, effect and animation art.
The complete-release gate stays closed; final headed captures remain required.
