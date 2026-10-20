# Reimagined vehicle paths — 2026-10-20

Cars in the private ordinary-track candidate now follow authored physical rails.
The native simulation still selects the track module, fraction, direction, source
frame, rider identities and shake. Explicit model contact pivots and track paths
replace sprite-projection offsets for 3D placement. Riders inherit the resulting
parent transform, including pitch, turning and native shake.

Work stays on `codex/reimagined-full-remaster`. Main, Nightly and the installed
study are unchanged. All derived routes, models, authoring, copied native states
and images stay in ignored `work/`. No artwork was accepted or partial pack
installed. The full remaster remains unfinished.

## Implementation

`TPRMOTION1` stores bounded, ordered body/root bindings and ordinary module
positions/tangents, base height and per-level elevation. Loading rejects malformed,
nonfinite, duplicate, truncated, trailing and degenerate input. A car's native
fractional position selects the closest authored XY path point; its native
heading chooses travel direction. The explicit contact pivot removes the source
frame's root fitting before applying physical placement. The original source
pixels, drawing order and random-number consumption remain unchanged.

The private route set has eighteen wooden, eighteen steel and ten Monorail
shapes. Wooden and steel elevation increments both use .6 world units, independently
of their different source sprite-offset slopes. Contact heights use each authored
rail top. Both car models have explicit root/contact bindings. Unknown or invalid
paths do not manufacture a replacement vehicle; the private diagnostic renderer
retains source art, and the replay tool rejects a missing path with exit 4 and
no substitute images. This diagnostic behavior does not satisfy release coverage.

## Evidence

- Eight native sanitizer checks and 21 Python tests pass. Motion tests cover
  8,192 contact/attachment combinations, source-root removal, affine composition,
  slopes, bidirectional turns and malformed paths. Car checks retain 1,440
  movement cases and 3,008 rider bindings. A subsequent targeted sanitizer run
  checks 64 stable random seeds twice each and observes all four native shake
  values without altering repeated-capture state.
- Both walk modes and 17,808 view checks pass. Compile-only native app build and
  signature verification pass; the two existing unused-variable warnings remain.
- All 1,248 before/after native probes preserve source pixels, dimensions and
  anchors. All 112 owned resource hashes remain unchanged.
- Four cars replay 640 previously recorded native states into 1,280 images in
  perspective and orthographic projections. Rider identity, linked/visible counts
  and native origins agree. Each car includes nineteen occupied samples and
  occupied-to-empty-to-newly-boarded transitions. These reuse a prior bounded
  native capture; they are not a new live remastered gameplay run.
- Independent 3D distance checks put all 640 authored contact roots on their
  selected rails within 1.848727976960625e-6 world units. This verifies root
  contact, not each wheel, smoothness across every transition or full animation
  timing. Twenty additional images include nearby ordinary track context.
- The 36.84-second bounded, muted Original-mode app run exits normally and
  completes its control script. Both macOS window captures fail with “could not
  create image from window.” Internal images exist, but no headed visual pass
  or remastered-art acceptance is claimed.

Private evidence is recorded in `tracks/motion-contact-verification.json`,
`tracks/motion-context-verification.json`, `vehicles/path-motion-verification.json`,
`vehicles/motion-differential.json` and `vehicles/missing-motion/result.json` under
`work/remaster/`. Test/build logs are in the corresponding tracks folder.

## Remaining work

The ride-along camera still follows legacy container position/height. It needs
exact guest-to-seat identity and authored eye anchors composed with the same
vehicle transform. Source-hidden riders currently remain omitted; reverse 3D
views need explicit authored poses and geometric occlusion. Neither problem is
accepted as finished behavior.

The ordinary XY path format does not handle vertical loops, corkscrews or other
special assemblies. Individual wheel contact, complete transition coverage,
normal-gameplay integration of the full artwork, the 96-model cache limit and
full-park performance remain open. Offline orthographic images are not proof
that the live normal gameplay pack is complete. Headed Reimagined acceptance
must be repeated once the complete pack and its animations are ready.
