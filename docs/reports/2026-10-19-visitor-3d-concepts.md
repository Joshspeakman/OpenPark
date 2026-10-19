# Visitor View 3D asset concepts — 2026-10-19

Requested by the user: inspect native macOS OpenPark Visitor View and illustrate
what replacing its game assets with 3D models could look like, in a separate
branch and install while they work on other branches.

Branch: `codex/visitor-3d-mockups`, based on `01391b3`.
Worktree: `/Users/joshuaspeakman/Projects/openpark-visitor-3d-mockups`.
Current follow-up branch: `codex/visitor-authored-models`, after the user's
rejection of the first implementation's appearance.

## Replacement approach: authored surfaces

The user explicitly rejected the generic shell/extrusion result. It is now
disabled by default. `runtime/walk_model.h` loads bounded, indexed, authored
surface meshes from the isolated profile's `visitor3d` directory. Models use
actual original palette pixels with nearest sampling. Objects without a model,
or with a differently cropped composite, use their original sprites. P still
compares the model mode to original cards. `TP_WALK_SHELL_STUDY=1` is only for
reproducing the rejected study; it is not the new default.

The user accepted the Toy Land direction and requested a solid entrance plus
four additional structures and trees. Toy Land now has 246 vertices / 116
triangles, separate roof slopes, ridge, chimney/pot, solid front facade and bear
badge. The original dark entrance pixels are on a closed plane; all recessed
jamb/interior/threshold geometry was removed. Unseen sides/rears are inferred
from existing motifs and cannot claim original unseen detail.

The local review profile now has eight authored static models:

| Asset | Runtime key | Triangles |
| --- | --- | ---: |
| Toy Land | 10/7 | 116 |
| Haunted House | 4/6 | 767 |
| Gift Shop | 10/9 | 378 |
| Novelty Shop | 10/11 | 64 |
| Coffee Shop | 10/6 | 271 |
| Oak | 255/114 | 972 |
| Palm | 255/111 | 148 |
| Birch | 255/115 | 192 |

Static scenery's original animation ID is its model key. These are separate
surface constructions, not the rejected automatic shells. Coffee Shop has a
solid cup, top swirl, handle, striped canopy and separate tables/parasols;
Haunted House and Gift Shop have separate wings/towers/roofs. Trees use actual
trunks, branches and foliage surfaces. These new models are for review; their
shape, texture density, inferred views and scale are not declared accepted.
No moving ride mechanism, guest or staff rig is implemented.

Local game-derived artifacts (intentionally ignored):

- `work/visitor-3d/authored/toyland.py`, `review_set.py`, `surfaces.py`: local
  authoring sources; `profile/visitor3d/` holds the eight runtime meshes.
- `work/visitor-3d/authored/index.html`: source plus 24-angle turntable per asset,
  built by generic `tools/visitor_model_gallery.py`.
- `authored/toyland-park-solid.png`: actual copied TESTPARK snapshot after the
  doorway fix; `scenery-integration.png` adds three trees in a separate scratch
  memory image, without changing any save. Both are offline native captures.
- `work/reimagined/index.html`: the subsequent second art direction, distinct
  from these original-pixel models; see the separate art-options report.

Validation: both walk modes and 17,808 view checks pass; synthetic mesh/model
checks cover palette texels, depth, clipping, invalid input, changed frames and
incompatible texture fallback under ASan/UBSan. The eight-model gallery produced
200 successful sanitized captures (192 model views and eight source exports).
Requested models must now load and match their source dimensions or the capture
fails: earlier negative root vertices had revealed a silent fallback risk, fixed
by clipping authored ground-touching vertices to z=0. Logs record model triangle
counts, so original cards cannot stand in for the requested gallery model.
The park/scratch-scenery checks resolve all eight runtime keys with matching
source sizes. This verifies routing, not every possible animation/state.

The separate native app rebuilt, installed and passed signature verification.
The fresh headed attempt `work/visitor-3d/headed-6buixn_p` exited -6 in 0.11 s,
with zero windows/captures, consistent with the previously diagnosed macOS
application-registration abort. No headed pass is claimed. The separate app,
profile and bounded headed runner remain available for the user's GUI session.

The sections below document the earlier concepts and rejected prototype.

## Concept deliverables

Local images remain in ignored `work/visitor-3d/`, following CLAUDE.md's rule
for game-derived material. Generated with the built-in imagegen tool, using
the repository's actual Visitor View screenshot and existing park capture as
references. Exact prompts are in `work/visitor-3d/prompts.json`.

- `mockups/01-visitor-view-3d.png`: the existing screenshot's staging reimagined
  with solid buildings, a horizontal maze footprint and grounded foundations.
- `mockups/02-midway-eye-level.png`: a new close-up path-level composition
  between the star-domed attraction and the thatched cottage.
- `mockups/03-asset-model-sheet.png`: proposed cottage, dome, booth, guest,
  hedge modules, bench and path pieces, including side and rear views.
- `mockups/04-original-pixel-3d.png`: revised direction requested by the user:
  coarse original-style pixel textures and details on solid forms. Built-in
  imagegen, exact prompt in `work/visitor-3d/pixel-revision-prompt.txt`.

These are raster concept images, not exported meshes or screenshots of an
implemented renderer. Side/rear details, precise materials, lighting and some
landscaping are artistic proposals. They do not establish performance or exact
game dimensions. The midway image still exaggerates the maze's slope; real
maze geometry should follow the flat cell grid, as the asset sheet shows.

## Recommended direction

User correction (2026-10-19): they like the buildings but require the exact
chunky pixel look of the originals, including exact details. This supersedes
the smooth/painted rendering direction in concepts 01–03. Concept 04 illustrates
the revision, but is generated artwork and is not pixel-exact original art.

For implementation, preserve actual decoded source texels and their palette,
use nearest-neighbour texture sampling, and match geometry to the original
silhouettes. Preserve baked highlights, roof markings, signs, emblems, windows
and wall patterns; do not invent higher-resolution decoration or blur texels.
New shading must not repaint the original palette. Ground remains the original
coarse tile art. Add actual side surfaces and depth without voxelizing every
pixel into a cube. Projecting/reusing source artwork needs careful separation
of surfaces; a sprite already contains perspective, so simply wrapping the
whole sprite around a box will not preserve its geometry correctly.

Exact visible source details can be retained, but unseen backs and sides are
not defined by the original sprites. Reuse source patterns conservatively on
those surfaces and label reconstructions as inferred. Validate any future
models against the actual decoded sprites at a matching reference angle.

## Implementation study and quality gate

The user subsequently authorized implementation across rides, shops, scenery
and animation states, in this branch and separate install, then explicitly
requested a headed captured test. That request authorizes this test's visible
window despite the repository's usual background-only test rule.

Implemented a **provisional single-view reconstruction**, not a complete set of
individually authored 3D assets. `runtime/walk_mesh.h` supplies a portable CPU
triangle renderer with near clipping, perspective-correct nearest sampling,
original palette colours, transparency and a shared depth buffer. `walk.c`
feeds it the current native composite sprites, retaining changing source frames,
riders, signs and other overlays. Shape rules choose shells for structures,
open rides, track/car pieces, trees and ground features. People remain cards.
P switches to the existing card renderer for comparison. The study defaults to
240 render rows and supports the copied 128-cell TESTPARK.

Original visible texels are reused without repainting. This does **not** mean
all original geometry/details are preserved from every angle: inferred hidden
surfaces repeat/stretch source artwork. Animation changes the source composite;
mechanisms are not separately rigged 3D parts.

Verified locally:

- `tests/check_walk.sh`: snapshot, grounding, eye height, movement and memory
  invariants pass in both original and mesh modes; view tests pass 17,808 checks;
  mesh tests pass palette, depth order, alpha, texture-frame changes, side
  thickness and near clipping. AddressSanitizer/UndefinedBehaviorSanitizer used.
- Owned-data inventory: 29 ride, 17 shop/sideshow, 19 scenery/toilet definitions;
  694 animation slots, 615 nonempty chains, 3,919 unique chain frame references.
  This inventories source records, not finished models. Five track rides use
  placed track/car composites rather than a standalone definition sprite.
- Offline sanitizer sweep: 5,704 frame records checked, 3,918 nonempty,
  11,754 front/side/rear renders, no sanitizer failure. One referenced chain
  frame yields no visible composite. This tests isolated frames under a generic
  shape; it does not exercise every real object/state combination.
- `tools/visitor_gallery.py`: 180 source-frame captures for the 60 definitions
  with standalone animation references. The other five are explicitly listed
  as requiring track/car review. `work/visitor-3d/gallery/index.html` is local
  and game-derived; it is not a headed game capture.
- Native headless TESTPARK load ran with copied saves and exercised the new
  renderer. An early sample rendered 33 visible objects/14,598 triangles at
  960x600 in about 8 ms for the mesh pass; this is not a Cocoa frame-rate claim.
- The isolated native Cocoa app builds and passes ad-hoc signature validation.

**Quality has not passed.** Inspected captures show stretched/accordion roof
edges, repeated facades on backs, flat or compressed open rides at side angles,
and inadequate maze relief. Some geometry classification is still generic.
The source-facing pixel texture is useful, but the result does not yet meet the
accepted mockup's solid forms and exact-detail requirement. Do not call the
65-definition inventory a completed 65-model asset set.

Required follow-up: replace generic shells with per-asset surface/part layouts,
separate roof/wall/footprint projections, give the maze real hedge geometry,
and drive ride parts from the simulation's frame/state. Validate original
silhouettes and details at the source view, then all sides and animation states.
Track rides need dedicated track/car scenes. Retain card comparison throughout.

## Headed test

`tools/check_visitor_headed.py` runs only the study bundle, with a new scratch
profile, copied saves, audio muted and a 65-second in-app watchdog plus an
external process timeout. It shows the window, loads TESTPARK, captures five
views and a later animation frame, and captures only its owned window using
`screencapture -l`. It separately records window visibility, window ID and PID;
offline renders cannot satisfy the headed test. Visual approval remains manual.
`tools/Visitor Quality Check.command` invokes the same test from Finder/Terminal.

Latest agent-run attempt: `work/visitor-3d/headed-h1dovp_m/result.json`, exit -6
in 0.10 seconds, zero captures and no completed controls. Crash report
`ThemePark-2026-10-19-152054.ips` shows `SIGABRT` in
`___RegisterApplication_block_invoke` during `+[NSApplication sharedApplication]`,
before the game window or renderer exists. Earlier LaunchServices launch also
failed with -10827. The root cause is not established; no successful headed
launch or screenshot is claimed. User asked to run the prepared test from their
GUI session so this dependency can be checked.

## Isolation

Separate implementation-study install:
`~/Applications/OpenPark Visitor 3D Study.app`.
Bundle ID: `com.joshspeakman.openpark.visitor3dstudy`.
`tools/build_visitor_study.sh` compiles this branch's C/Cocoa code against local
copies of the verified native AOT archives and resources. These inputs are in
ignored `work/visitor-3d/native`; the original build cache is not modified.
The bundle's `TPDataRoot` selects `work/visitor-3d/profile`, independent copies
of game files and saves. Preferences use the distinct bundle identity. The
build script replaces only this exact study identity and moves its prior study
install to Trash. `TP_STUDY_NO_INSTALL=1` builds without installing.

The original Nightly checkout and install were not edited; the original
checkout was rechecked clean. No public uploads or commits of game-derived
material were made. Captures, inventory, snapshots and generated concepts stay
under ignored `work/` or `build/`.
