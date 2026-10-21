# Tree House candidate — 2026-10-20

The isolated Reimagined candidate now covers all 36 native Tree House keys:
eight in chain 355 and 28 in chain 385. It includes the wooden lift, fluted
trunk and roots, entrance room, balcony, lookout, layered foliage, elephant
and wheel, and two canopy peek sequences. This is unaccepted candidate art,
not a completed remaster or a playable lifecycle acceptance.

## Geometry and motion

The model has 7,374 attribute vertices, 5,846 triangles, 22 transform groups
and one passenger attachment. Geometry keeps constant dimensions across all
keys. The lift follows the source's four elevations, retaining its upper
platform while the source temporarily omits the lift sprite. Its rope stays
connected to the branch and moving basket. The elephant has articulated
legs, head, ears and connected trunk sections, with blink states selected
from the source's four elephant sprites. Brown and grey canopy figures use
the exact source appearance sequences; their detailed anatomy and mechanics
are authored interpretations of small pixel references.

Generated bark, leaf and wood tiles use continuous material coordinates.
A quieter neutral grey material replaces the first noisy elephant skin.
Lookout floor and foliage were adjusted to support the visitor's feet;
root bottoms and moving elephant soles were corrected to their ground/deck
planes. UV bounds remain strict after a leaf extension exposed an invalid
coordinate during iteration.

The actual linked visitor follows the source's lift and walking markers.
Four additional step/walk keys and an authored crouching key extend each of
the eight canonical guest rigs: 40 new poses, with all prior materials,
geometry and pose lines preserved. Their sole origins are measured against
actual shoe geometry, excluding collapsed hidden transform groups.

Shared scene resolution now retains the real occupant in the 23 Tree House
frames without a native guest marker. This only applies to the verified
definition, kind, capacity and keys, and does not overwrite native rider
metadata. The crouched visitor stays on the lookout floor. This is an
authored interior posture, not recovered stair traversal. Source omission
does not guarantee invisibility from every new camera: parts of the visitor
can be seen above or between lookout staves. Final occlusion and fidelity
remain unaccepted.

## Verification

- 736 new source-free scene cases cover both projections, 23 unmarked keys,
  empty/occupied states and eight appearances. Actual identity, pose and
  source visibility pass, with guards for visible keys and malformed data.
- 648 actual native occupancy/phase probes resolve the exact identities and
  keys, preserving source visibility and frame checksums across phases.
- 328 final captures cover all keys in both normal modes and Visitor front,
  rear, side and orthographic views, plus representative states for all
  eight appearances. All resolve the complete candidate model.
- 36 geometry states retain grounded roots, lift heights, connected rope
  endpoints, elephant sole clearance and matching blink/peek groups.
- 288 guest/frame checks verify sole alignment and support footprints.
  Lookout bobbing intentionally raises the source-selected guest above its
  floor. These checks do not prove general body/branch collision clearance.
- All 36 fixed-camera orthographic borders remain clear.
- 648 old/new native passes match 5,752 source draw events and indexed pixels,
  with unchanged simulation memory.
- Twelve native remaster checks, 21 Python tests, both walk modes, 17,808
  view checks and mesh/model/rig/layer checks pass. The isolated app compiles
  and passes signature verification with installation disabled.

The current private inventory contains 119 candidates. All 110 previous
noncharacter models and all 112 owned resources retain their hashes. The
eight guest files contain only the documented pose additions. Main/Nightly
and the existing separate install were not changed. The complete-pack gate
remains enabled; no app launch or headed acceptance is claimed.

## Evidence and remaining work

Ignored local evidence is under `work/remaster/rides/tree-house/`: model,
verification, geometry, border, preservation, guest-addition and source-
comparison JSON; native reference exports; captured PNGs; and lift/lookout
GIFs for orthographic and Visitor views. GIFs package unchanged captures at
six frames per second for inspection, not measured game timing. Authoring
is in `work/remaster/rides/authoring/tree_house.py`, `tree_verify.py` and
`work/remaster/characters/authoring/tree_riders.py`. The model is
`work/remaster/rides/models/04-1.rig`; material provenance is recorded under
`work/remaster/art/tree-materials.*`.

The shoe-origin correction exposed a broader audit need in canonical
walking poses: collapsed hidden groups can select a fictitious ground plane.
This change only normalizes the five new Tree House poses per guest; it does
not claim every existing walking pose is grounded. Live entry, boarding,
stair travel, timing, exit, breakdown and repair remain unfinished, along
with other art families, terrain/effects, special track states, ride cameras,
complete pack assembly and final headed captured acceptance.
