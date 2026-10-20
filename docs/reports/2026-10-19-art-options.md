# Two art directions — 2026-10-19

Later direction: the user's full autonomous Reimagined request supersedes the
slice/review acceptance sequence below. UI is exempt, all world assets and
animations are mandatory, and no placeholders or concept-review pauses are
allowed. Current work and truthful completion status:
[full remaster](2026-10-19-full-remaster.md).

The user requested two distinct options: the current faithful conversion of
original sprites to 3D, and **Reimagined**, a faithful cartoon remaster of all
game art. Reimagined must cover both the original overhead game and Visitor
View, eventually with a launcher choice. Work remains on the isolated study
branch/install, without modifying Nightly or its game data.

## Direction and current deliverables

### User correction: angular forms and original proportions

The user rejected the first Reimagined sheets as too soft, round and cute.
Their correction supersedes that art direction: preserve the original chunky,
over-exaggerated proportions, silhouettes and character identities. Use hard
planes and sharp forms where the source calls for them, without turning every
pixel into a cube. Do not enlarge eyes, inflate heads/limbs, soften architecture
or replace original expressions/clothing with generic modern mascot features.

Riders must use the **same canonical guest mesh, face, hair, clothing, palette
variant and proportions as walking guests**. Boarding changes pose and attaches
the guest to a seat; it must not replace that guest with a separate passenger
design. This is an implementation requirement for all rides and both camera
views. The concept sheet illustrates the requirement but cannot verify shared
geometry or animation behavior.

Three revised sheets and their exact prompts are local:

- `work/reimagined/revision-02/01-characters.png`: the two guest examples and
  eight staff/entertainer identities, using the original sprite contact sheet.
- `02-riders.png`: those green/blue guest designs walking and seated, with
  passengers in the six-cabin Big Wheel and four-cup Merry-Go-Round.
- `03-buildings.png`: Toy Land, Haunted House, Coffee Shop, Oak, Palm and Birch;
  a follow-up edit closes entrance surfaces and adjusts squat proportions.

All were produced with the built-in imagegen tool. The current gallery is
`work/reimagined/index.html`, with original references and prompt links.
`index-v1.html` preserves the rejected first direction, explicitly marked
superseded. Revision 02 covers 16 definition identities and two guest examples;
coverage marks older unrevised concepts accordingly. Neither revision is a
completed model pack. Exact original proportions, detail identity and pose
consistency still require source-aligned model checks and user visual review.

| Option | Art treatment | Current implementation |
| --- | --- | --- |
| Faithful 3D | Original palette texels, nearest sampling, authored solid forms, preserved visible motifs | Eight static Visitor View review models; overhead art unchanged |
| Reimagined | Angular solid 3D forms, original exaggerated proportions, original character identities, shared walking/riding guests | Three revised concept sheets; first five sheets superseded; no runtime models or rigs yet |

The original artwork remains a supported baseline. Reimagined is an additional
option, not a replacement for Faithful 3D. Source pixels cannot reveal hidden
surfaces; reconstructed backs, material treatment and topology remain proposals.
Entrances should be solid dark surfaces wherever an interior is not needed.

Earlier review artifacts, all ignored as game-derived material (the five
first-direction sheets below are superseded by revision 02):

- `work/visitor-3d/authored/index.html`: eight real model turntables using the
  native Visitor renderer, with original sprites beside them.
- `work/reimagined/index.html`: Reimagined review gallery with five concept
  sheets and links to the exact generation/edit prompts.
- `work/reimagined/concepts/01-buildings.png`: Toy Land, Haunted House, Coffee
  Shop and Gift Shop. A correction removes an invented spoon and closes doors.
- `02-rides.png`: Pirate Boat, six-gondola Big Wheel, and the actual game's
  cup-style Merry-Go-Round, including proposed alternate motion poses.
- `03-people.png`: two guest examples and the eight loaded staff/entertainer
  designs, including chicken, teddy, rhino, squid and strongman costumes.
- `04-landscape.png`: ten landscape/prop examples, matched to extracted frames.
- `05-two-views.png`: overhead/Visitor View composition using the same proposed
  asset family. This is an illustration, not matched camera test evidence.

Generated using the **built-in imagegen tool**, following the imagegen skill.
Original references were exported through the native decoder from copied game
memory. Native sprite contact sheets preserve those decoded pixels. Final
images and prompt text are saved in the workspace; nothing depends solely on
the tool's generated-image cache. No CLI/API image generation or external
publication was used.

Image generation supplies reference art and can supply texture work. It does
not export usable meshes, UVs, skeletons, collision, LODs or executable animation
clips. None of these concepts should be installed as a fake 3D replacement.

## Full conversion scope and tracking

`work/reimagined/coverage.json` and its HTML view list all 65 inventoried object
definitions: 29 rides, 17 shops/sideshows, 19 scenery/toilet definitions. Eight
staff definitions were additionally read from the copied loaded table. Of
these 73 records, 25 are represented in the concept sheets; this is concept
coverage only. All Reimagined mesh, rig and renderer-readiness flags remain
false. Two guest examples are additional and do not exhaust guest variants.

The source survey found 694 animation slots, 615 nonempty chains, 3,919 unique
chain-frame references, 3,301 sprite records and 244 ground records. These are
source counts, not a promise that every record is a distinct model or animation.

Additional work must explicitly cover:

- Five track rides' placed track/car/train/rider composites, all orientations,
  joins, entrances, exits, stations and overlays.
- Guest variants and activity/direction states; staff jobs and entertainer
  performances; seated riders and carried items.
- Ground, paths, queues, terrain transitions, water and edge tiles; landscape
  combinations; signs, park entrances, litter, balloons, umbrellas and effects.
- Working/idle/broken ride states and repairs where present in source data.
  Derive actual state mappings from the game rather than inventing a checklist
  of states that may not exist.
- Interface artwork/icons and other non-world art: inventory separately before
  claiming that every original game asset has been remastered.

Names are not sufficient evidence of geometry. For example, the inspected
Merry-Go-Round is a cup platform, not a horse carousel. One loaded scenery label
reads Castle Wall while its selected source looks like tan rails; confirm that
definition/frame mapping against live placement before final asset acceptance.

## Planned implementation

Use stable art-mode identifiers `original`, `faithful3d`, `reimagined`, with
separate asset roots and shared simulation state. Future launcher labels should
include **Faithful 3D** and **Reimagined**, preserving access to the original art.
There is no functional Reimagined launcher choice yet: selecting a concept sheet
would not constitute implementing an art mode.

For Reimagined, author a canonical model per identity, with its own texture
atlas/materials, anchors, footprint, bounds and named moving parts. The current
Faithful loader samples the original sprite and requires matching dimensions;
it is not a general Reimagined material or animation pipeline. That renderer
needs a deliberate extension rather than baking generated pictures onto boxes.

Use the same canonical assets and animation state for the overhead and Visitor
cameras. An overhead replacement pass must preserve placement, occlusion,
picking, signs and user interface behavior. If overhead sprites are baked from
3D models during migration, they must be generated from those same models and
poses, not separately reimagined artwork with drifting details. The final choice
between a direct 3D overhead pass and model-derived sprite rendering remains an
implementation decision, not something these mockups have verified.

Bind animation to existing game states/timing: wheel rotation and upright
gondolas, boat swing, cup/platform motion, rider seat attachments, walking and
staff/entertainer actions. A second painted pose is not a rig. Test each model
from front, side, rear and overhead, including representative moving states.

## Acceptance sequence

1. Review the distinct visual directions and correct identity/detail drift in
   the reference sheets. Avoid scaling unreviewed errors across the catalog.
2. Build one complete Reimagined slice: building, moving ride, tree/prop, guest
   and entertainer; one shared art set rendered through both camera paths.
3. Verify footprint, texel/detail readability, scene scale, geometry occlusion,
   state synchronization and frame cost in the isolated native app.
4. Expand by asset family, tracking concepts, meshes, rigs, overhead integration,
   Visitor integration and validation independently. Preserve clearly recorded
   original-art fallback until each replacement is ready.
5. Add the real launcher choice when its renderer and asset pack work. Perform
   captured headed tests from the GUI session before calling either option done.

Current verification is limited to the Faithful work: eight-model native
turntables, compatible runtime routing, sanitizer checks, build and signature.
The latest headed launch still aborts before a window exists. Reimagined has
not passed an in-game perspective, animation or performance test.
