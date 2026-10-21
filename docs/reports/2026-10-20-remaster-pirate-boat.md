# Pirate Boat candidate — 2026-10-20

The owned art depicts a compact rocking wooden boat over a blue-and-white
base. The new local candidate preserves that arrangement, the red/ivory sail,
raised gold ends and eight inward-facing passenger positions. It is isolated
on `codex/reimagined-full-remaster`; Main/Nightly and installed apps are unchanged.
This is candidate artwork, not accepted full-remaster coverage.

## Geometry and motion

The model has 1,871 attribute vertices, 2,276 triangles, two rigid groups and
eight attachment matrices. It includes a closed shaped hull, inner surfaces,
gunwales, rubbing strips, exposed frames, separate deck boards, raised end
decks, upholstered seat pads, wooden backs, restraints, mast collars, curved
cloth panels and sail hems. Interior UVs use a continuous material field rather
than repeating a full wood panel on each small face.

All 50 selected native keys from chains 386, 320 and 383 exist explicitly.
Sixteen distinct hull sprites drive authored rocking angles. The source hull
origins supply lateral displacement; the actual rotated underside sets the
contact height. The base stays fixed at ground level. No per-frame scaling or
separate passenger bob is applied. Seats inherit the entire hull transform.
Angles are an authored reconstruction, not recovered simulation values or proof
that the game's acceleration/deceleration cadence is correct. Live lifecycle
and final visual fidelity remain unverified.

The eight existing canonical guests gain six keys each: two additional seated
poses in both mirror states and two states for source-omitted passengers.
Their previous geometry, materials and every existing pose line remain verbatim.
The new keys use the shared skeleton and a local hip origin; the seat supplies
the rocking angle and facing. They are not separate generic ride characters.

## Native omitted passengers

At steep angles the original ride consumes a seat using sprite 57, which is
absent from the regular substitution table. Appearance zero draws its tiny
source marker; other appearances skip it. Previously the 3D capture either
received an invalid key or no marker for that consumed passenger.

New keys 65444–65447 retain that actual identity with source visibility zero.
The original indexed calls/pixels remain unchanged. Complete-model drawing
requires the exact occupied pose, including these markers; the physical hull
provides occlusion. The independent car marker range remains unchanged.

## Materials and files

The built-in imagegen skill produced a four-quadrant material atlas for amber
hull wood, crimson/ivory canvas, blue/white painted water and golden deck wood.
The prompt requested crisp chunky original-game materials, without objects,
perspective, labels or a rounded toy treatment. The generated bitmap was decoded
and packed into the real mesh texture atlas; it is not a scene-image substitute.

Private project-bound files:

- Model: `work/remaster/rides/models/04-2.rig`.
- Generated image: `work/remaster/art/pirate-materials.png` (1254×1254).
- Exact generation prompt: `work/remaster/art/pirate-materials.prompt.txt`.
- Provenance and decoded pixels: `work/remaster/art/pirate-materials.json` and
  `work/remaster/art/pirate-materials.rgba`.
- Authoring: `work/remaster/rides/authoring/pirate.py` and
  `work/remaster/characters/authoring/pirate_riders.py`.
- Current inventory: `work/remaster/catalog/models-current.json` (115 models).

These derived artifacts stay ignored as required by repository policy. The
private inspection profile links the canonical model and guests; it is not an
installed or releasable partial pack. The complete-pack launcher gate remains
unchanged.

## Verification and limits

- 458 native occupancy probes cover all 50 keys with each of eight uniform
  appearances, every empty frame, and one-to-eight mixed occupants at a steep
  state. Identity/order and exact pose availability pass, including omitted
  source bodies. These are controlled records, not observed live loading.
- 216 captured views include all 50 keys through both actual normal native
  modes, the Visitor renderer and a fixed orthographic inspection camera,
  plus opposite-side views of all sixteen distinct hull positions. Selected
  neutral and extreme images were visually inspected.
- All 50 rigid transforms retain unit scale. Their lowest hull points remain
  supported inside the base, with fixed clearance; all inspection images have
  clear borders at the same camera scale. This is geometry evidence, not a
  complete contact/action or artistic acceptance.
- 900 owned-source comparisons against the independently compiled previous
  native renderer match all 10,778 source events and indexed pixels. Memory is
  unchanged. Both native modes include every source key with all appearances
  and empty seats.
- Twelve remaster native sanitizer checks and 21 Python tests pass, including
  new omitted-ride cases for all appearances and both normal modes. Both walk
  modes, all 17,808 view checks and existing mesh/model/rig/layer checks pass.
- The isolated app builds without installation and passes strict/deep signing
  verification. All 112 owned resource hashes and 106 existing non-guest model
  hashes remain unchanged. Eight guest files gain only pose keys; one ride
  model is added. No game launch or headed capture occurred.

Evidence lives under `work/remaster/rides/pirate/`: `verification.json`,
`geometry.json`, `source-comparison.json`, `integrity.json`, `guest-additions.json`,
captures, source references and test/build logs. The source ledger records this
as an unaccepted candidate. Other ride bodies, special track composites, remaining
art/animations, terrain/effects, non-car ride cameras, full-pack performance and
final headed acceptance still require work. Original-art fallback is not counted
as completion.
