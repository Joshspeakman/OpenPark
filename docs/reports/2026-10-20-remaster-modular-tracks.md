# Reimagined modular tracks — 2026-10-20

Visitor View can now assemble authored track components at the original ground
origin with separate native heights. Bases, repeated supports and animated
tops resolve together: any missing model or exact pose rejects the whole
replacement before drawing. Train composites remain excluded until their cars
and actual riders have complete bindings. The full remaster remains unfinished;
no artwork is accepted for release.

Work stays on `codex/reimagined-full-remaster`. Main, Nightly and the installed
application are unchanged. The existing study build was compiled and signed
without installation. All derived records, art, fixtures and captures stay
under ignored `work/remaster/tracks/` and `build/`.

## Native assembly and source inventory

Snapshot-only metadata records the selected frame, track family, ground origin
and vertical displacement without changing the original draw order. Heights
normalize the two source screen modes into canonical world units. The bounded
snapshot pool rejects oversized assemblies, corrupt underflowed stacks and
special scaled modes. Each assembly requires all native frames, no unmatched
rider metadata and models without unbound attachment seats.

The catalog now reads 55 segment records across three elevated track families.
References propagate to six definitions: Roller Coaster, Loop the Loop, Cork
Screw, Big Dipper, Water Splash and Monorail. They include cap definitions,
base/support references, directional vehicle references, the wooden train's
foreground component and Monorail's separate construction ground frame.
Raw unknown fields remain explicitly unresolved. References do not prove that
every combination is a valid gameplay state, or establish live timing.

Ground tubing/racing surfaces follow a separate map-block path. Their ownership,
geometry and vehicle composites still need dedicated work. Elevated train
states, slopes, loops, construction states and missing-cap behavior also need
their full artwork and semantic bindings.

The offline snapshot tool adds bounded `track FRAME FAMILY YAW segment SHAPE
LEVEL` fixtures. Digests include component origins. Orthographic inspection uses
the same selected models and heights as the perspective renderer; it does not
produce or install a finished normal-view art pack.

## Local Monorail candidates

Eleven local models cover the striped support and ten ordinary top-piece
references. The geometry uses fixed tile endpoints, chamfered white/gray rail
sections, dark side bands, red undersides, diagonal red/white/blue support paint,
flared feet and approach-facing stop signs with distinct backs. It is authored
geometry with inferred hidden surfaces, not recovered original 3D data.

Inspection corrected reversed surface lighting, open cut ends and a support
that initially protruded into the rail. The current support is shortened for
the assembled condition; a cap-absent support must still receive its full
separate geometry before acceptance. The construction ground, vehicles and
riders are not represented by these candidates. Proportions and stop fixtures
still need closer source comparison in both game projections.

A private sixteen-cell loop exercises four corners and two straight directions.
All sixteen endpoint pairs agree in world position and tangent. Three native
perspective captures show the joined assembly from above, the side and visitor
height. The two wider views draw all sixteen assemblies; the closer view clips
one outside its field of view, and every rendered assembly uses its model.
This is an offline fixture, not a live ride or a gameplay acceptance test.

Current candidate verification records model and image hashes for 44 individual
model views, 40 native perspective assemblies, ten native orthographic assemblies
and three loop views in `monorail-verification.json`. The geometry remains
unaccepted; no completion ledger entry was added.

## Verification

- Six sanitizer checks pass, including 3,408 track assembly cases covering both
  screen modes, support heights, crop independence, terrain, tick-selected tops,
  bounded storage, train exclusion and all-or-nothing drawing. Twenty-one Python
  tests pass, including malformed track tables and reference ownership.
- Both walk modes, all 17,808 view checks and mesh/model/rig/layer checks pass.
- Before/after native comparison of 378 owned-source component probes preserves
  all source pixels, cropped dimensions and anchors. Every captured component
  retains its shared ground origin. These table probes do not assert valid
  live simulation states.
- Native compile and signature verification pass. The bounded Original-mode
  headed run exits normally after 36.62 seconds and captures both actual app
  windows. `work/remaster/runtime-yt8oq271/` contains the result and inspected
  normal/Visitor screenshots. This verifies shared-code regression only.
- All 112 owned source-resource hashes are unchanged. The refreshed exhaustive
  ledger still reports 3,848 blockers and no accepted remaster completion.

Next work remains the full remaster: refine/complete modular art, bind actual
cars and riders, preserve live motion and transitions, cover ground track
systems and the remaining asset families, produce both-view art packs and pass
integrated headed visual, timing and performance acceptance. The 96-model cache
and bounded component pool also need measurement against a complete busy park.
