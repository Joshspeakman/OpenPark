# Reimagined model capacity — 2026-10-20

The model cache no longer stops after 96 lookups. Previously, missing assets
consumed that same allowance, so a varied park could prevent later valid
models, track components or passengers from loading. The current private
candidate library already contains 114 distinct canonical models.

Work remains on `codex/reimagined-full-remaster`. Main, Nightly and installed
applications are unchanged. No artwork became accepted through this change.

## Implementation

The cache now uses a sparse direct index over the model format's existing
class/type domain: 256 classes and 32,768 types. Each 256-type page allocates
only when first needed. Individually allocated models and their poses retain
stable addresses until reset, including while another passenger or component
loads. Missing or malformed files use an explicit negative-cache marker;
they cannot exhaust a resident-model count. Lookups do not scan prior models.

The cache remains owned by the render thread. Reset frees model data and index
pages, then clears aliases, motion bindings and eye anchors as before. There
is no eviction while assemblies hold pointers. Loaded geometry remains in
memory for the session; this change does not establish a full-pack memory
budget or solve cold-loading stalls. Allocation failure still returns failure.

The standalone baker and model inspection tool now own their input model
directly instead of borrowing slot zero from the runtime cache. Camera and
track resolver fixtures retain their original assertions with the new storage.

## Evidence

- A new source-free sanitizer regression loads 513 resident models after
  4,096 failed lookups, checks 512 frame aliases, holds a live model and pose
  through all loads, and compares its pixels. It also checks invalid keys,
  malformed-file caching, reset/reload and repeated reset.
- A private sanitized audit loads all 114 current candidate models together
  after the same 4,096 misses. Geometry and complete pose matrices match
  independent file loads. First/last poses in two projections produce 456
  identical pixel comparisons. Of those, 452 contain geometry; four perspective
  comparisons of two tall support models lie outside the fixed diagnostic
  camera. This is cache consistency evidence, not framing or visual acceptance.
- The audit holds 49,544,224 bytes of model payload and 317,440 bytes of index
  storage, excluding allocator overhead and renderer/snapshot memory. One
  instrumented run takes 1.369 seconds to load and 11.449 ms for 1.14 million
  warm lookups. These are local microbenchmarks, not game frame pacing.
- Ten remaster native sanitizer checks and 21 Python tests pass. Both Visitor
  modes, all 17,808 view checks and the mesh/model/rig/layer checks pass.
- The standalone inspection tool produces 24 carousel captures byte-identical
  to the previous inspection. All 114 candidate model hashes and 112 owned
  source resource hashes remain unchanged.
- The separate app compiles and passes deep/strict signature verification with
  `TP_STUDY_NO_INSTALL=1`. No app launch or installation occurred. The two
  existing unused-variable warnings in `view.c` remain.

Private evidence and the candidate symlink profile are under
`work/remaster/cache/`; suite/build logs are `work/remaster/cache-tests.log`,
`cache-walk-tests.log` and `cache-build.log`. This profile is an offline audit
input, not an installed or complete remaster pack.

## Normal gameplay investigation and remaining work

Normal gameplay currently paints per-source-sprite `world.art` replacements
through `native_pass()` and `TPRemasterPaint`. Visitor View and the offline
orthographic tool instead resolve complete models, exact occupied poses and
physical car paths. Those offline images do not establish live normal-view
integration. A full assembly path still needs shared native state extraction,
ordered/physical occlusion, camera placement, clipping, zoom, and preservation
of UI, weather, tools and original picking behavior.

Simply enabling Visitor snapshot capture for normal gameplay is insufficient:
its current capture radius is centered on the walker, uses separate floor
caches and source-card scratch buffers, and its active path updates the native
camera. Likewise, native thing marks currently also change entrance recursion
and sign-text behavior. A normal capture path must separate metadata emission
from those Visitor-specific behaviors. Cache pointers must remain confined to
their owning thread or protected through an explicit shared lifecycle.

The remaining asset/animation families, complete normal gameplay rendering,
full-park performance and final headed remastered acceptance are unfinished.
The complete-pack release gate remains in force; this cache change neither
adds placeholder artwork nor makes an incomplete pack releasable.
