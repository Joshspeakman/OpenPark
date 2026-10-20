# Mapped staff routine candidates — 2026-10-20

The isolated Reimagined worktree now has candidate poses for every key in
the current explicit staff action inventory: 371 action keys across 44 known
chains, plus 144 travel keys, across nine identities. This adds 336 keys to
the previous checkpoint. It does not establish complete staff dispatch
coverage or accept the artwork for release.

The source sheets now use one scale and a fixed native origin throughout
each sequence. Previously, fitting each drawing separately made wider arm
poses appear to squash the body. The underlying source pixels, anchors and
manifest hash did not change. The corrected sheets show Strong Man flexing
and pointing without an invented crouch. They also preserve Rhino Man's
actual airborne offsets, which would disappear if every frame were aligned
to its lowest pixel.

## Authored motions and corrections

- Teddy Man adds the twenty-key pogo performance and thirty-two grounded
  head-glance keys. The resting pose holds the shorter stick at his side.
  Performance poses explicitly release waving hands and kicking feet; the
  compressed mouth hides the actor face and teeth while the upper mask
  translates intact.
- Shark Man has a closed raised-fin volume with the source's bent outline
  and tip motion. Stretching the pointed travel fin produced a narrow blade
  in the normal projection; that approach was replaced. Chicken Man's dance
  uses connected upper/lower wings, distinct kicks and measured body bob.
- Rhino Man keeps the four repeated-drawing take-off positions at their
  distinct heights, then turns and compresses on landing. Bounds checks
  explicitly expect the airborne heights instead of requiring every pose
  to touch the floor. Squid Man's six dance drawings use connected tentacle
  segments, alternating curls/kicks and measured top-tendril displacement.
- Strong Man adds flexing, pointing and the wide-eye state, with a wider
  planted stance and the original diagonal singlet strap. Guard saluting
  includes the raised arm and blink. Their idle loops turn the head while
  keeping the rest of the pose still. The other costume head-glance loops
  retain their own body, scarf, crest and tentacle behavior.
- Mechanic setup adds case placement/opening and cloth deployment. Carrying
  and opening now use the same case body and hinged lid. Four connected
  checked-cloth panels unfold and fall. Pixel comparison proves the upper
  body in keys 509–512 is identical: the cloth is released while both hands
  remain at the waist. This corrected an overextended reaching arm.

These poses still need source-proportion refinement and full live motion
review. The list describes implemented candidates, not final art approval.

## Current evidence

- All nine current model files have 2,060 sanitized native perspective draws
  and 2,060 model inspection views. Native capture selection covers each
  exact authored key at four camera angles. The combined record verifies
  the current model and image hashes, retaining unchanged handyman evidence:
  `work/remaster/characters/staff-verification-current.json`.
- All 515 keys render in the canonical normal projection without clipping.
  Fixed-origin comparisons pair each source action drawing with its model
  projection. This is offline rendering, not integrated HD normal gameplay:
  `work/remaster/characters/staff-orthographic-verification-current.json`.
- The changed mechanic model was rerun through the eighty recorded actor
  samples in both projections. Both workers' 160 sampled identities, states,
  frames, directions and subtile positions match the native records. Current
  model hashes and all 320 replay capture hashes check. This remains an
  isolated-actor replay with reconstructed map heads, not full-park or live
  timing evidence: `work/remaster/characters/staff-replay-verification.json`.
- The explicit inventory contains no missing or duplicate authored keys.
  It includes definition-table references and previously observed worker
  chains; additional dynamic dispatches and trigger semantics remain open:
  `work/remaster/characters/staff-action-coverage.json`.
- All 112 source DATA hashes remain unchanged. There was no runtime code
  change, application installation, or modification to main or nightly.

Models, authoring scripts, source references and captures remain ignored
under `work/`. No accepted replacement or release-completeness entry was
added. Remaining staff trigger/quality work, other NPC states, all remaining
world assets and both-view integration continue under the full-remaster goal.
