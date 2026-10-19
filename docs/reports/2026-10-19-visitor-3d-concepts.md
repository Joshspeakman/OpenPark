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

Toy Land is the first actual authored proof: 302 vertices / 144 triangles,
two sloping roof planes, a stone ridge, separate chimney and pot, a front
gable, a physically open doorway with recess/jambs, wall panels and a shallow
solid bear badge. UVs were laid out against the owned 100x77 source frame 4021.
Roof samples avoid the fascia which occludes the thatch in the source image.
Source texture dimensions are checked before model rendering; the live shop's
different world anchor does not shift its texture. Side/rear architecture is
inferred and reuses existing window, timber and thatch patterns. These surfaces
cannot claim original unseen details. Only this one asset is modeled; rides and
other assets have not been converted to authored geometry or animation rigs.

Local game-derived artifacts (intentionally ignored):

- `work/visitor-3d/authored/toyland.py`: authored surface/UV construction.
- `work/visitor-3d/profile/visitor3d/0a-7.mesh`: the runtime model.
- `work/visitor-3d/authored/index.html`: a 24-angle offline turntable with links
  to the source pixels, front/side/rear views and actual TESTPARK snapshot.

Validation: both existing walk modes and 17,808 view checks pass under
sanitizers; the new synthetic model test covers bounded loading, invalid
indices/nonfinite input, palette-exact sampling, texture-frame changes and
fallback for incompatible texture dimensions. The native study app rebuilt
and its signature verified. The updated headed test copies the mesh into its
scratch profile and visits Toy Land from four directions. Its attempt in
`work/visitor-3d/headed-sqectaq9` exited -6 in 0.11 seconds before any window
or capture, consistent with the previously diagnosed macOS registration abort.
No headed pass or user acceptance of this new visual direction is claimed.

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
