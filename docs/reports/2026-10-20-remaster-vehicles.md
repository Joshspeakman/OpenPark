# Reimagined native vehicles — 2026-10-20

Coaster and Monorail snapshot cards now carry the native-selected frame,
ground origin, vertical displacement and actual rider substitutions. Guests
inherit their vehicle's lift through the attachment matrix. The full remaster
is unfinished; these models and seated poses are unaccepted candidates.

All work remains on `codex/reimagined-full-remaster`. Main, Nightly and the
installed study application are unchanged. The existing isolated bundle was
compiled and signed without installation. Source-derived art, authoring,
fixtures and captures remain ignored under `work/remaster/vehicles/`,
`work/remaster/characters/` and `build/`.

## Runtime behavior

The original car drawing path retains its substitution order and pixel output.
Metadata describes each actual seat occurrence, including Monorail's omitted
sprite-31 marker, which consumes a linked guest but deliberately draws no body.
Invisible omission keys cannot become visible replacement poses. A linked guest
that the original does not draw creates no additional attachment. Unknown
markers, invalid identities, malformed chains, unsupported scaled modes,
missing track origins and mismatched frames reject replacement eligibility.

The renderer resolves the exact car frame and guest posture before replacing
a card. Normal-view inspection and Visitor drawing use the same meshes and
seat transforms. This offline orthographic path is not a finished gameplay
sprite pack. Elevated train composites and entrance trains remain separate
unfinished paths.

The snapshot inspection tool adds bounded native car fixtures with family,
segment, direction, elevation, fractional position and ordered guest identities.
Its digests expose actual frame and ground/height metadata. The model inspector
places its floor below negative local geometry so seated legs and track
undersides remain visible during inspection.

## Local artwork

A canonical red/yellow coaster body has 20 source poses, 224 vertices and 356
triangles. A blue/yellow Monorail cabin has four poses, 308 vertices and 394
triangles. They include recessed seating, restraints or benches, faceted body
panels, wheels/guide structure and canopy details. Hidden surfaces are inferred.
Body dimensions stay fixed across poses; source anchoring adjusts X/Z without
fitting an invented ground depth. The Monorail benches face inward. Inspection
corrected canopy intersections and bench spacing.

The eight existing guest bodies gain 88 seated poses, including the source's
mirrored steep-car poses. Articulated hips, knees, feet and arms belong to the
same models used by walking guests. Existing shop geometry, materials and all
172 shop poses remain byte-for-byte unchanged in the corresponding model
sections. No placeholder posture is accepted for missing animation.

The coaster model currently derives pitch from the second elevated family.
Shared sprite keys do not establish identical physical slopes in other
families. Family-specific orientation, track contact, complete occupancy,
proportions and live transitions must be resolved before accepting this art.

## Verification

- Seven sanitizer checks pass, including 1,440 native vehicle movement cases
  and 3,008 rider bindings, both source screen modes, omitted markers,
  malformed chains and inherited attachment lift. Twenty-one Python tests pass.
- Both walk modes, 17,808 view checks and mesh/model/rig/layer checks pass.
- All 1,248 owned-source before/after probes preserve pixels, dimensions and
  anchors, with exact expected rider and height metadata. These are copied
  state probes, not live animation timing evidence.
- The 416 occupied combinations have 1,248 captures in two perspective angles
  and one orthographic projection. The 88 seated guest poses have 352 separate
  inspections and 88 orthographic bakes. Local reports hash current artifacts.
- All 112 owned source resource hashes remain unchanged.
- Native compile and signature verification pass. The bounded headed run in
  `work/remaster/runtime-cjyff0uw/` exits normally after 36.51 seconds. Both
  actual application windows were captured and inspected. This checks shared
  Original-mode rendering, not remastered-art acceptance.

The remaining work includes live vehicle motion, all track families and ground
track systems, source-part packs for normal gameplay, the rest of the asset and
animation inventory, and complete headed visual/performance acceptance. No
completion ledger entry was added for these candidates.


## Recorded native motion follow-up

A bounded, muted Original-mode native run in a scratch profile observes the
owned demo park for 58.06 seconds. It exits normally and records 160 complete
thing-pool samples at nominal 10 Hz after the demo starts. This is native
simulation evidence, not fabricated movement between selected poses.

`tools/remaster_vehicle_replay.c` reads those records with strict arguments
and replays four operating coaster cars through their actual track records.
Because changing map-cell heads were not recorded, the tool rebuilds isolated
cell lists from the sampled car and track coordinates. It leaves guest chains,
terrain, source selection fields and fractional positions intact. It then
isolates the car for rendering; missing track artwork is not substituted by
invented geometry. The camera follows the car and its height, so this is pose
and binding inspection, not whole-park presentation or camera-motion evidence.

All 640 car states resolve exact vehicle and rider poses. The resulting 1,280
images cover both projections with identical attachment lift. Each car has
nineteen occupied samples, transitions to empty, and later boards new guests.
Linked counts, first drawn guest identities and quantized ground origins agree
with the captured records. The selected frames number nine, nine, six and nine
for the four cars. Five malformed argument cases reject cleanly under the
sanitized executable. Artifact and input hashes are in the private
`work/remaster/vehicles/motion-verification.json`.

Representative rising/steep, occupied and empty frames were inspected. This
proves neither every intermediate motion at display cadence nor all animation
states. Live Reimagined timing, complete train context, track contact and art
quality remain outstanding. No installed application or runtime was changed
by this follow-up, and no artwork was accepted into the completion ledger.
