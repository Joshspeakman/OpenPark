# Reimagined ordinary coaster geometry — 2026-10-20

The local remaster now has authored ordinary track modules for both steel and
wooden coasters. Forty-six models carry 91 exact source pose keys. They remain
unaccepted candidates; the full remaster is unfinished, and no placeholder or
partial pack was installed.

Work remains on `codex/reimagined-full-remaster`. Main and Nightly are untouched.
All derived meshes, authoring, material references, copied simulation records
and rendered evidence stay under ignored `work/remaster/tracks/` and
`work/remaster/vehicles/`. This follow-up changes only the offline replay tool
and documentation in the tracked tree. No application was launched or installed.

## Authored geometry and animation

Twenty steel models cover eighteen ordinary segment shapes and their base and
stack supports, with 65 source pose keys. Fixed modules include paired silver
rails, dark webs, orange spines and crossmembers, column feet, buffer fixtures
and a stop sign. The source's four wind states select explicit connected cloth
panel transforms for the red/blue/yellow flags. Rail and pole geometry stay
fixed. Duplicate stop-frame keys retain the source's identical appearance.

Twenty-six wooden models cover eighteen ordinary shapes and eight distinct
base/stack support references. They include beveled sleepers, longitudinal
members, metal running rails, braced columns and metal stops. Existing generated
wood materials are reused; no source sprite is wrapped around a box. Inspection
found overly bright timber and conspicuously repeated end-grain motifs. The
current models use darker tint and varied smaller material crops. All geometry
uses fixed module coordinates without fitting each source frame independently.

Together the models contain 48,244 attribute vertices and 43,620 triangles.
These totals describe the authored set, not a single displayed park frame.
The normal-projection captures use these same meshes; they are not a complete
normal-gameplay source-part pack.

## Joins and vehicle contact

The recorded native demo park supplies actual segment positions and levels.
All 47 adjoining ordinary steel endpoint pairs and all 60 wooden pairs agree
in position, height and horizontal tangent direction. Slopes use .6 world units
per native 24-unit elevation step, including two-step slopes. These checks do
not establish smooth curvature at every slope transition or complete special
track coverage.

The occupied train recorded in the preceding follow-up runs on wooden family 1;
the stationary train runs on steel family 2. Both layouts' physical elevation
steps match. Their differing native car sprite-offset gradients therefore must
not be treated directly as different physical rail slopes. Those fields place
hand-drawn sprite frames and need source-aware retargeting for a 3D body.

Contextual inspection shows joined modules and cars together. Ordinary wooden
tracks in the selected nearby views resolve to their authored geometry. Special
pieces elsewhere retain their source cards in these private diagnostic views;
this is explicitly incomplete art, not an accepted replacement or a release
fallback. Car-to-rail offsets remain visible at bends and transitions. The next
alignment work must follow the authored physical paths and preserve actual
rider identity/posture, rather than accepting source-frame bounding-box fitting
as proof of 3D contact.

## Verification and performance scope

- All 91 keys have 364 sanitized model inspection images. Ordinary native
  assemblies have 405 perspective/orthographic captures. The contextual checks
  add 22 images. Current model and image hashes agree with the private reports.
- Endpoint checks cover the 107 recorded ordinary joins described above.
  Loop, corkscrew, special train, construction and other exceptional states are
  unfinished; their source tables and composite paths remain separate work.
- The offline vehicle replay tool now starts Visitor View once and forces a
  fresh snapshot for each sampled record set while preserving its model cache.
  Reopening per sample discarded loaded art and hid cache behavior. All 320
  images and 160 evidence rows for the comparison car remain byte-for-byte
  identical. Five malformed argument cases still reject under sanitizers.
- A private optimized 960x600 context benchmark retains that cache. The first
  draw takes 161.958 ms. The 141 stationary warmed samples have 27.553 ms median,
  28.722 ms p95 and 30.135 ms maximum rendering time. This measures copied track
  context and one car, without the full park, normal view, live scheduling or
  gameplay workload. It is not final performance acceptance. Earlier repeated
  cold-load measurements are not representative of steady rendering.
- All 112 owned source resource hashes remain unchanged. No completion ledger
  entries were accepted for this work.

The full remaster continues with physical car contact and turning, special
track composites and all remaining asset/animation families, complete packs for
both views, cache/performance work and headed Reimagined acceptance. The current
96-model cache limit still needs a full populated-park solution; these narrower
contexts cannot establish that it scales to the requested complete asset set.
