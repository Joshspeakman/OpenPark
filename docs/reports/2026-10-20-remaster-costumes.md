# Remaining staff costume candidates — 2026-10-20

Local Reimagined authoring now has distinct canonical models for all nine
staff identities. This adds Chicken Man, Teddy Man, Rhino Man and Squid Man
to the five earlier candidates. It does not complete their action routines
or accept the staff category for release.

Teddy Man uses separate costume-head, jaw, torso, arm, foot, pogo-shaft,
spring and peg transforms. The sixteen original travel keys supply measured
head displacement, spring length and projected ground-tip position. The
six-pixel compression is retained without fitting each pose to its crop.
The telescoping shaft remains overlapped; transformed wrist/grip and
foot/peg contact checks pass for every key. Visual inspection led to rebuilt
arm volumes and a mouth recess that exposes the human face. The distinct
performance sequence, which adds stronger head squash and hand waves,
remains unfinished.

Chicken Man has a checkered orange shell, articulated wings, hooked crest,
long tail, beak and separate clawed feet. The native frame offsets drive the
body bob. Inspection caught a claw extending slightly below its sole plane;
the local foot geometry was corrected. The leg tops now remain inside the
costume at the highest authored bob.

Rhino Man has a projecting muzzle, curved horns, pointed ears, costume eyes,
wrapped yellow scarf and tails, layered breast contours and broad toes.
Chicken and rhino use one shared scale per model, solving front and profile
rest-mesh widths together rather than scaling individual frames.

Squid Man has a spotted shell, costume eyes, visible human face, black top
tendrils and eight tentacles with three connected segments each. Its body
bob is bound to the original separate tendril-part Y offsets. The terminal
contact pads remain on the floor as upper segments bend. Spot boundaries
were refined on the same coarse body faces; extra paint subdivisions do not
round the underlying silhouette.

## Evidence and remaining scope

- The four additions contain 751 vertices / 1,319 triangles (chicken),
  1,113 / 1,936 (teddy), 706 / 1,182 (rhino), and 2,201 / 4,122 (squid).
- All nine current staff models total 179 authored keys: 144 travel keys,
  sixteen handyman mower keys and nineteen mechanic break keys. Current
  models have 716 sanitized model inspection views and 716 native perspective
  captures. Exact model and capture hashes were checked when combining the
  existing evidence; unchanged models were not repeatedly rendered for every
  costume edit. Local record:
  `work/remaster/characters/staff-verification-current.json`.
- These checks establish native identity/key selection, model loading,
  geometry bounds and the stated attachment invariants. They do not establish
  complete animation fidelity, live pacing, all job transitions, or integrated
  HD normal-view rendering. Source proportions and motion still need review
  at that scope. The static native fixtures retain original terrain.
- A new private action-reference export covers 44 known staff chains and
  371 referenced keys without decoding errors. It includes definition-table
  references and the additionally observed mower/break chains. It is not
  proof that every dynamic dispatch or trigger is mapped. Evidence:
  `work/remaster/characters/staff-action-source-poses.json` and
  `work/remaster/characters/staff-actions/mapping-report.json`.
- All 112 source DATA hashes remain unchanged. No runtime code, installed
  application, main branch or nightly branch changed in this authoring pass.

The models, local authoring scripts, source sheets and captures remain ignored
under `work/`. No release-completeness record or accepted replacement was
added. Remaining staff performances, repair/work/special states and the rest
of the full-game remaster continue.
