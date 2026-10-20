# Reimagined animation study — 2026-10-19

The accepted angular concept now has a structural asset/animation map and a
small running native animation prototype. This is not the completed replacement
pack or a claim that every animation has passed visual review.

## Review artifacts

All owned-game-derived data stays in ignored `work/reimagined/`:

- `motion-review/index.html`: paired running captures, pose controls, observed
  states, limitations and links to the accepted concepts.
- `animation-map/index.html` / `catalog.json`: 81 asset/guest records, both
  frame-table families, resource hashes and per-asset motion checks.
- `live-review/native-comparison.gif`: 120 paired captures, six seconds at the
  requested 20 fps, from a 37-second native run with copied saves. Its resized,
  quantized GIF is for review; the paired PNGs preserve exact rendered pixels.
- `live-review/trace.csv` / `observed.json`: 104,775 changed records across 43
  asset/raw groups. At-most-50-Hz observation can miss intermediate changes.
- `live-review/capture-validation.json`: 120 valid pairs, all with changed art,
  74 unique Reimagined frames, no headed pass.
- `headed-cg1qnczz/result.json`: macOS launch exited -6 in 0.11 seconds, before
  any window or capture. The separate app must still pass a visible-window test.

## Coverage and unresolved work

| Family | Structurally inventoried | Runtime replacement in this study |
|---|---:|---|
| Rides | 29 definitions | Original art; no finished Reimagined rigs |
| Shops | 17 definitions | Original art |
| Scenery | 19 definitions | Original art |
| Staff/entertainers | 8 definitions | Original art; activity semantics pending |
| Guests | 8 variant tables and seated sprite tables | Two walking rigs; other activities retain original art |
| Animation tables | 694 M and 686 MD slots | Exact source frame lookup for supported rig poses |
| Primary composites | 5,704 nonzero frame IDs exported | 3,918 nonempty; 1,786 empty |
| DATA resources | 112 files, including 15 ride FLICs | Original resource paths preserved |

Linked-frame enumeration records termination, loops, unreferenced records and
rider-marker elements. It does not assign invented action names to unknown
states. Guest slot zero is a base animation plus four compass directions;
native state 8 and position deltas verified N/E/S/W direction mapping. Secondary
family image extraction, track assemblies, effects/UI semantic mapping and all
individual action/transition labels remain incomplete.

The asset checklists cover opening/closing, starting/running/stopping, riders,
speed effects, breakdown/repair, service/purchases, items, gestures, entertainer
routines, placement and composite overlays. Applicability and completion must
be checked for each asset; the checklist does not assert that every item exists
for every object. First-person FLIC videos have their own native player and
timing, separate from park sprite poses.

## Runtime behavior

`TP_ART_MODE=reimagined` selects local `TP_DATA/reimagined/CC-TYPE.rig` files in
Visitor View. `TPRIG1` stores bounded vertices, flat face colors, bone indices,
strictly sorted source frame keys, bone matrices and optional seat matrices.
Frame 65535 is the canonical seated pose. Geometry/colors are shared between
walking and sitting. Render-thread code reads copied snapshot metadata rather
than simulation memory.

Two prototypes cover the blue and green guest examples: seven bones, 264
triangles and 17 poses each (16 walking keys and one seated key). Their pose
selection follows the game's current frame, including holds. There is no
wall-clock sine wave or added interpolation. The current limb poses, depth and
proportions are authored approximations requiring source-silhouette comparison.
Seated fixtures do not prove boarding, unloading or occupied 3D ride behavior.

Walking with carried items, selection markers and other activity states keeps
the original complete composite. Missing poses/models also fall back. This
preserves the original behavior but produces an unfinished visual change when
a guest enters an unconverted activity. Full state coverage is needed before
calling the animation seamless.

Seat ordering mirrors native descending slot consumption, capacity, phase
rotation and empty seats. Rendering requires every occupied seat's guest rig
and pose before replacing a ride. Unsupported kinds and composites fall back.
Tests verify this plumbing, not actual authored ride choreography.

## Isolation and verification

Branch: `codex/reimagined-animation-study` in the existing isolated worktree.
Install: `~/Applications/OpenPark Reimagined Study.app`, bundle identity
`com.joshspeakman.openpark.reimaginedstudy`. It uses its own profile and copied
saves; game resources link to the already-isolated owned package copy.
Faithful Study remains a different app/profile. The user's active Nightly
checkout and application were not edited by this work.

Build: `bash tools/build_visitor_study.sh --reimagined`.
Visible-window check: `python3 tools/check_visitor_headed.py --reimagined`.
The explicit user request authorizes this bounded headed study test.

Passed: native ARM64 build and signature verification; both existing walk modes;
17,808 view checks; mesh, model and new rig tests under ASan/UBSan; Reimagined
fallback on the synthetic park; three synthetic animation-graph tests; 136
native pose/angle renders; review JavaScript syntax; paired capture/GIF checks.
The first running capture revealed speckled depth fighting between coplanar
original cards. A relative depth tolerance fixes ownership instability without
hiding genuinely nearer surfaces; a synthetic one-ULP depth/triangulation
regression verifies it. The final capture was regenerated after this fix.
Its repeated frames mean the GIF's nominal playback rate is not a measured
native application frame rate or a completed smoothness/performance check.
H.264 encoding was unavailable in this environment, so ImageIO packaged the
comparison as a GIF. This does not affect the source PNGs or native execution.

Still required: faithful stride/silhouette matching; all other guest actions;
same-identity riders in real 3D rides; ride cycle transitions and speeds;
staff/entertainer routines; shop/scenery effects; overhead integration; launcher
selection; and the blocked visible-window quality review. A full animation or
whole-game quality pass is not claimed.
