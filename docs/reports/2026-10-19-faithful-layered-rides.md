# Faithful layered rides: Bouncy Castle vertical slice

2026-10-19, Codex. User direction: reassess the floating sprite-overlay approach
and build detailed, layered, animated assets with coherent scale/perspective
that actually run in the native game. Work is isolated on
`codex/faithful-layered-rides` in `openpark-visitor-3d-mockups`.

## The change

The earlier preview kept almost the whole ride on one flat front surface. The
native placement estimate also used the current composite width to estimate
footprint depth. A changing animation crop could therefore move an object's
estimated center. A fixed authored footprint now determines the layered ride's
world anchor and collision cells independently of its sprite dimensions.

The ride is a collection of actual textured 3D surfaces: foundation, folded
entry steps, bounce bed channels, rear/side walls, crenellations, four segmented
towers, collars and pointed red caps. Original indexed palette texels provide
the materials, with nearest sampling. A shared depth buffer gives these
surfaces and the riders consistent perspective and occlusion.

The generic local asset format, `TPLAYER1`, contains:

- Bounded indexed texture, textured vertices and triangle indices.
- Rigid vertex groups with per-frame affine transforms.
- Poses keyed by the native simulation's original frame IDs.
- Independent rider foot transforms indexed by native rider-element order.
- A stable world offset and collision footprint.

`runtime/walk_layer.h` validates and loads the assets from
`TP_DATA/faithful/04-<ride-type>.layer`. The first walk start loads the ride cache;
it then stays immutable for snapshot/render thread safety. Missing assets,
unknown poses, unsupported states or invalid rider slots retain original art.
The first implementation preloads ride assets; shop/scenery integration is a
later extension of the same surface format.

## Motion and people

`native_land.c` brackets its existing substituted rider blits with Visitor View
metadata. It preserves the ordinary drawing operations. Visitor View copies
each rider's exact sprite into its snapshot alongside the complete composite.
Empty native rider poses stay absent. Native rider ordering and variants are
preserved, including loading/unloading; no generic replacement guests are added.

The original frame selects the structural pose and rider foot transforms.
There is no free-running animation clock. In this asset, rider vertical offsets
are measured from original element foot markers; floor/tower deformation is
authored against those source phases. It is not recovered original 3D motion.
Closed skirts connect the moving floor channels to the foundation. Rider feet
follow their local floor height plus the measured jump displacement.
Rider pixel dimensions use the same world conversion as walking sprites:
`1 / tws` horizontally and `aspect / tws` vertically. The captured park uses
`tws=24`, `aspect=1.2`.

Riders remain camera-facing original cutouts at independent 3D locations. This
preserves faces, outfits and animation poses, but also means they present the
same art from behind. Full 3D guest rigs and directional rider art remain open.

## Current asset and evidence

All art, source exports, authoring code, meshes and captures stay ignored under
`work/faithful-layered/`. The review page is `index.html` there.

| Property | Verified result |
| --- | --- |
| Structure | 5,458 vertices; 2,720 triangles |
| Articulation | Ten groups, eleven original frame keys, four rider transforms |
| Grounding | Every pose has minimum z=0; all geometry stays within its authored footprint |
| Placement | Identical center across all 80 captured animation states |
| Source preservation | All 400 motion source PNGs match the previous native composite exports byte for byte |
| Inspection | 80 samples × five cameras plus 24 occupied orbit angles under ASan/UBSan |
| Occupied bounce | Four separate riders in every one of the 45 selected bounce samples |
| Running game | 51-second native run; 140 sequence PNGs from two cameras, layered ride reported in every logged render |

`tools/visitor_layer_review.c` replays captured native thing arrays through the
same snapshot and layered rendering path used by the game. It fails if the
requested ride lacks an eligible model/pose. These inspection renders isolate
the ride for visibility. The final `live-final/front/` and `live-final/quarter/` captures retain
the running park, surrounding guests and staff. Neither is a headed recording.

`tests/walk_layer_test.c` uses synthetic data to check bounded loading, invalid
palette/header values, known/unknown pose selection, rider-slot validity,
occlusion behind a wall, empty seats and source-keyed rider foot translation.
The complete walk test script passes both modes, all 17,808 view assertions and
the existing sanitized mesh/model/rig tests. Source composite comparisons also
exercise the new native rider metadata without changing the original pixels.

## Install and limits

`tools/build_visitor_study.sh --faithful-layered` builds and installs
`~/Applications/OpenPark Faithful Layered Study.app`, identity
`com.joshspeakman.openpark.faithfullayeredstudy`, with a separate profile and
copied saves. Signature verification passes. Earlier Faithful/Reimagined apps
and the user's Nightly working tree are unchanged.

The headed test still exits with SIGABRT before creating a window. The local
crash report reaches `_RegisterApplication` and `NSApplication` initialization
before game rendering. A Launch Services attempt also fails, reporting
`kLSNoExecutableErr`; the signed executable exists. These failures remain
unresolved and are not counted as a visual pass.

This is a reusable runtime path demonstrated by one ride, not completed
conversion of the game. The structure's unseen surfaces, footprint and squash
are inferred/authored; exact original 3D proportions cannot be recovered from
one sprite view. The foundation and rider scale are consistent in world space,
but overall visual acceptance still needs user review. Dynamic lighting,
directional/full-3D riders, weather material behavior and every ride's special
effects remain further work.

## How to grow the feature

Use the native asset/animation catalog to author each ride as independently
moving structural groups with explicit ground anchors and attachment points.
Preserve original pixel materials and map every native state to a reviewed
pose. Validate construction, idle, loading, running, unloading and exceptional
states separately; preserve complete original fallbacks for uncovered states.

For each asset, require source/3D comparisons, occupied motion, an orbit,
grounding/footprint checks, rider identity and occlusion checks, plus captures
from the running game. Establish these contracts before broad conversion or a
launcher option, so additional art cannot silently introduce placement drift,
wrong timing or invented occupants.
