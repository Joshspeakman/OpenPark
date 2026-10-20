# Native staff poses and worker animation candidates — 2026-10-20

Staff models now bind to the actual native staff identity, body frame and
frame origin. The nine identities use entity byte `0x59`; a selected worker,
job marker or other additional native frame remains ineligible for a
body-only replacement. Changing the source crop does not reposition the rig.

`tools/visitor_snapshot.c` can isolate a staff frame and native state.
Source-free fixtures cover all nine identities, 27 ordinary states, two
source crops and both native drawing modes, plus invalid identities and
selection/job/special overlays. This establishes drawing eligibility, not
completed animation coverage.

The ignored local authoring now contains two worker candidates:

- Mechanic: helmet with raised ribs, overalls, boots, articulated face and
  hands, carried toolbox, and a separately modelled ground cloth/open case.
  Sixteen walking keys and nineteen break keys use the same worker geometry.
  The break poses include the hand-to-mouth motion and asymmetric chewing;
  repeated keys are matched by source pixels and anchor.
- Handyman: cap, overalls, bag/strap, boots and a mower with shaped deck,
  motor grille, bent handle, cable and four articulated treaded wheels.
  Sixteen walking and sixteen mowing keys retain the source direction and
  reused body-part phase. One direction shifts the source body sideways by
  two native units; that displacement is preserved and the hands reconnect
  to the handle.

The candidates have 1,165 vertices / 1,864 triangles for the mechanic and
1,366 / 2,191 for the handyman. Neither is accepted replacement coverage.
Other staff identities, repair/cleaning routines, special composite states,
and final proportion and animation review remain unfinished.

## Verification and limits

- All 67 authored keys have four sanitized model inspections and four native
  perspective captures: 268 of each. Model checks confirm ground bounds;
  native captures confirm exact identity/key selection and rig rendering.
  Evidence: `work/remaster/characters/staff-verification.json`.
- `tools/remaster_actor_replay.c` replays 80 recorded states each for a
  mechanic and a mowing handyman. Each state produces Visitor and
  orthographic images from the same geometry, giving 320 captures. All
  nineteen mechanic break keys and four handyman direction-one mower keys
  occur in these recordings. Frame, identity, state and entity coordinates
  match the recorded records.
- The recording contains changing thing records, but only an initial map.
  The replay therefore reconstructs the isolated actor's cell head from its
  recorded coordinates. Keeping stale map heads initially exposed a false
  position error; the corrected replay checks the native origin against the
  recorded position within pixel rounding. Terrain is retained, other actors
  are omitted, and the camera follows at a constant offset. This is offline
  pose/placement evidence, not a complete park replay or live pacing test.
  Evidence: `work/remaster/characters/staff-replay-verification.json`.
- Three C sanitizer tests, fourteen Python tests, both walk modes, 17,808
  view checks and the existing mesh/model/rig/layer checks pass. The isolated
  compile-only native build and signature verification pass.
- A muted, bounded background-window **Original-mode** regression captured
  both gameplay views, was visually inspected and exited normally after
  38.73 seconds. It verifies shared runtime behavior, not replacement art.
  Evidence: `work/remaster/characters/staff-headed.json`.
- All 112 source DATA hashes remain unchanged.

The normal-projection replay is not an integrated HD normal-gameplay pass.
The full remaster, remaining asset/animation authoring and final live art
acceptance remain open. No app was installed; no release-completeness record
was created. All owned-source references, model files and captures remain
ignored under `work/`.
