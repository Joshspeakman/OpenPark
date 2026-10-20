# Reimagined scenery work — 2026-10-20

The full autonomous remaster and no-placeholder requirements remain in force.
This is an internal production checkpoint, not a vertical-slice delivery,
approval request, complete pack or playable remaster.

## Authored local candidates

The 19 scenery definitions have new canonical geometry in
`work/remaster/scenery/models/`, authored by the local
`work/remaster/scenery/authoring/author.py`. The meshes include folded palm
fronds, branching trunks and roots, tiered canopies, fruit and rose blossoms,
fluted blue/white pots, three framed lanterns, crossed fences and hedge,
lake edging, detailed toilet buildings, and the four-bowl fountain.

The original frames determine identities and proportions. For example, the
Palm Tree has multiple trunks, Tropical Bush is a single leaning palm, the
Oak Tree has a tiered silhouette, and the item named Castle Wall is depicted
as crossed tan rails. Geometry is authored around the source frame origin.
Ground contact remains z=0. Unseen sides are interpretations and still require
final visual review.

Generated bitmap UV materials come from built-in image generation, with exact
prompts and provenance under `work/remaster/art/`. They are new materials,
not enlarged original sprites. Material scale was corrected after early
canopy renders repeated too many tiny leaves. Fence geometry was inset within
its grass cell to retain actual post height. The outhouse now has closed rear
roof/wall geometry and bright orange wood instead of dark brown.

The fountain shares its solid structure across three source-keyed water
states. Each state has distinct water geometry; the native selected frame
chooses the pose. There is no independent animation clock. The fourth, small
rear bowl and dark spindle were added after inspecting all original frames.

There are 21 source poses and 22 native sprite-part bakes. The lamp's two
source rectangles and the fountain's changing source height have separate
bakes from the same canonical geometry. No candidate has been marked as
accepted in the full release ledger, and no partial pack is installed.

## Runtime and authoring changes

- Native draw metadata records the selected frame and original frame origin.
  Static and animated scenery snapshots use this for canonical model
  placement, independently of transparent crop bounds.
- `TPRMAP1` provides sorted, bounded class/frame aliases to shared model
  identities. The render thread loads it; capture does not mutate model caches.
- `TPRIG2` triangle colours tint generated textures for camera-independent
  authored shading. The rasterizer restores the caller's material state.
- `tools/remaster_inspect.c` renders every source pose from four fixed cameras
  with a neutral measurement floor. The camera remains fixed throughout an
  animation, so changes in bounds do not produce misleading zoom.
- `tools/remaster_texture.swift` decodes images to portable straight RGBA.
  The initial vertical-flip error was corrected and checked against the atlas.
- `tools/remaster_bake.c` can isolate a bone group while keeping the full
  model's pose and origin. Synthetic tests verify that disjoint part bakes
  combine to exactly the full bake.
- PNG writers now preserve the IEND checksum; generated inspection images
  are checked chunk by chunk.

## Verification

`work/remaster/scenery/verification.json` records 84 ASan/UBSan perspective
renders, 21 offline native scene replays and 22 nonempty part bakes. Every
scene replay logs one Reimagined rig. This exercises actual snapshot/model
binding and rasterization, but is not a headed game animation run.

`tests/check_remaster.sh` passes native source-replacement, textured raster,
binding parsing and snapshot tests, plus eight Python tests. The new snapshot
fixture verifies stable scenery origins despite different sprite crops and
the native advance-before-draw animation rule without writing game state.
Existing walk tests pass in both modes, including all 17,808 view checks and
mesh/model/rig/layer sanitizer tests.

The compile-only native app build and code-signature check pass. Existing
SDK deprecation and unrelated unused-variable warnings remain.
`work/remaster/runtime-w17j5zg3/` contains a 36.51-second bounded, muted,
background-window run in Original mode. Both owned-window captures were
inspected: populated park and occupied Visitor scene. Exit 0, complete control
acknowledgements, and no surviving test process. All 112 DATA hashes remain
unchanged. Installed applications, Main and Nightly were not modified.

## Remaining work

The full remaster remains unfinished. Scenery still needs final silhouette,
material repetition, source-detail and scale checks, including high-resolution
source variants and original resource aliases. Ground-tile geometry and
fences need continuity checks when repeated across cells. Offline frames do
not establish live animation timing or a busy park's performance.

The full normal-view replacement pack, HD overhead rendering, production model
installation, all rides/shops/custom tracks, guests/staff/entertainers,
expressions/items/seated identities and effects remain required. The normal
baker still targets the native palette and source-part dimensions.

Final QA must distinguish complete authored content staged for testing from
a release that has passed all visual and integration checks; installation
must remain gated on the latter. No original-art fallback, concept,
engineering fixture or incomplete model counts as completed remaster art.
