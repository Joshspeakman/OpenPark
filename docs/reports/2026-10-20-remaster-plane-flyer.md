# Plane Flyer candidate — 2026-10-20

The local Plane Flyer candidate preserves the owned source's four red
monoplanes, open cockpits, cowling eyes, wing and tail roundels, propellers,
blue centre pedestal, green deck, pale plinth and rotating gold sun face.
Work remains on `codex/reimagined-full-remaster`. This is unaccepted candidate
art; the full remaster and its live acceptance remain unfinished.

## Geometry and movement

The model has 6,068 attribute vertices, 5,154 triangles, 18 rigid groups and
four passenger attachments. Aircraft have shaped fuselages, recessed cockpit
floors and walls, seat cushions and backs, clipped wing tips, aileron seams,
layered roundels, closed tail fins, rudder hinges, underbody mounting shoes,
shafts and two separate propeller blades. The sun has two sculpted faces,
raised brows, eyelids, cheeks, noses, mouths, a closed rim and eight rays.
Generated enamel coatings provide the red, gold, blue and green materials.

Native chain 321 selects frames 1448–1451. Their four slot phases form a
16-step aircraft circuit, advancing by 22.5 degrees per selected frame. Each
plane follows one fixed-radius orbit with tangent heading and a continuous
authored lift curve. Two fixed-length telescoping arm sections connect each
plane to the pedestal. The sun advances 45 degrees per key, matching the
source's face/oblique/edge/reverse-oblique sequence. Two-sided symmetry closes
that cycle. Two-bladed propellers also have four geometry states.

All eight existing canonical guest meshes already contain the six required
seated keys, including the source-hidden sprite-57 passenger. No guest mesh,
material or pose was changed. Exact native identities remain attached while
geometry supplies occlusion from each viewpoint.

The source samples constrain the silhouette, selected frames and passenger
order. They do not independently establish the precise mechanical lift,
propeller speed or physical dimensions. These remain authored candidates;
the running game's speed transitions, loading, unloading and cadence have
not been accepted.

## Corrections and checks

Initial inspection found the sun facing edge-on in its first key and the
cowling eyes partly below the fuselage surface. Both were corrected. A
triangle-versus-wing-prism check then found one sun ray intersecting the
highest aircraft's wing. Raising the ornament and extending its support
removed that intersection across all four keys.

- 208 actual native occupancy/phase probes cover all frames, all four phases,
  eight uniform appearances and zero through four mixed passengers. Exact
  identities, selected poses and source visibility flags match expectations.
- Four identities follow continuous 16-step position, heading and lift
  circuits, including every phase wrap and the return to the first sample.
- 68 final captures cover both normal renderer modes and Visitor perspective,
  native orthographic and reverse inspection views. Each resolves a complete
  model; normal passes report one model and zero source objects for the ride.
- All 18 groups remain rigid in all four keys. The base reaches ground zero,
  passenger attachments match aircraft transforms, and arm endpoints remain
  connected with at least 0.0948 world units of telescoping overlap.
- All 48 appearance/pose combinations clear the cockpit floor, lower inner
  wall envelope and propeller plane. The sun has no intersection with the
  actual wing prisms. These are specific clearance checks, not a general
  whole-model collision or contact-animation acceptance.
- All 16 orthographic capture borders are clear with one fixed camera setup.
- 288 old/new native source passes match 2,120 draw events, indexed pixels
  and unchanged simulation memory under sanitizer instrumentation.
- The previous 116 candidate models and all 112 owned resources retain their
  hashes. The current private inventory now contains 117 candidates.

Only private art, authoring and evidence plus these tracked notes changed.
Runtime code and the installed app were not changed, so the full runtime
suite and app build were not repeated. The normal/Visitor harnesses were
compiled from the current branch and exercised above. No app was launched
or installed, and no complete-pack gate was bypassed.

## Local evidence and remaining work

Ignored local files:

- `work/remaster/rides/authoring/planes.py` and `planes_verify.py`
- `work/remaster/rides/models/04-3.rig`
- `work/remaster/rides/planes/model.json`, `verification.json`, `geometry.json`
- `work/remaster/rides/planes/source-comparison.json`, `preservation.json`
- `work/remaster/rides/planes/check-geometry.py`, `check-intersections.py`
- `work/remaster/rides/planes/captures/`
- `work/remaster/rides/planes/phase-cycle-ortho.gif`, `phase-cycle-visitor.gif`
- `work/remaster/art/plane-materials.png`, `.rgba`, `.prompt.txt`, `.json`

The GIFs package the unchanged native PNG captures at six frames per second
for inspection. That presentation rate is not measured live game timing.
The PNGs remain the colour and geometry evidence.

Remaining rides and other art families, terrain/effects, speed overlays,
special track/car states, ride cameras, live lifecycle validation, complete
pack creation and final headed captured acceptance are still outstanding.
Main/Nightly, other worktrees and the single separate installed app remain
untouched.
