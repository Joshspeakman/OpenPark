# Reimagined occupied attachments and shop clearance — 2026-10-20

Occupied guests now require their exact authored 3D pose even when the original
sprite was invisible. Native visibility remains unchanged metadata. The current
view's depth test and authored surfaces determine which parts of a passenger
can be seen. An empty seat still has no guest; a missing authored pose rejects
the composite instead of silently deleting its occupant.

This corrects the earlier candidate policy documented in the rider, customer
and vehicle reports. Those reports describe the previous implementation, not
acceptance of disappearing people. Work remains on the isolated
`codex/reimagined-full-remaster` branch. Main, Nightly and installed applications
are unchanged. All derived art, authoring and captured evidence stay in ignored
`work/`. No artwork was accepted, installed or presented as the complete remaster.

## Occupant behavior and authored poses

A shared resolver now handles occupied rigs for Visitor rendering, ordinary-car
ride-along cameras, orthographic inspection and recorded vehicle replay. Every
occupied attachment must have the exact identity and source-keyed posture.
This avoids different visibility rules in the different projections. The native
source renderer and its visibility/identity metadata are unchanged.

The Monorail provides direct evidence for projection-dependent omission. Frames
4298 and 4506 contain a sprite-31 marker that consumes an actual passenger but
draws no sprite; side frames 4299 and 4505 show both seats. Eight explicit
65440 poses retain those inward-facing seated guests, using their existing
walking character geometry. The source's other flip keys remain unsupported
until their specific artwork is authored; there is no generic seated fallback.

A missing neutral Bouncy Castle pose for identity 3 now uses that guest's actual
standing body between its existing bounce postures. Twenty-eight shop keys fill
fourteen empty identity-specific table entries and fourteen identity-zero-only
exceptions. These use the matching observed gesture transferred through the
canonical skeleton at the target character's own scale. Clothes, hair, face and
body geometry remain those of the actual guest. The authoring records identify
the observed donor slot and character; this is an explicit authored posture,
not a runtime fallback. These inferred missing states remain visually unaccepted.

The eight guest rigs now contain 674 keys, including the 37 additions. Geometry,
materials and every preceding pose line are unchanged. Private evidence records
that the earlier occupied-vehicle captures still use identical geometry and
poses after the later shop-only additions; reconstruction without those 28
unrelated keys reproduces the captured model hashes exactly.

## Physical customer placement

The restored poses exposed an existing problem: the source-projected customer
anchors for some midway booths placed people inside the counter. The former
constant depths came from source sprite foot positions and were not collision
constraints for the reconstructed building.

The five shop candidates now choose customer depth from the actual authored
front face plus the most forward body/head/leg surface across their mapped
customer postures. All 1,640 mapped cases retain at least .025 world units of
clearance for those checked surfaces. Hands are excluded because they can
legitimately reach toward the booth. This is a front-plane check, not arbitrary
mesh collision detection or proof of correct hand/prop contact.

Only the customer attachment X/Y positions change. Horizontal source-projection
alignment is retained by compensating X for the depth change; native-derived
jump Z is unchanged. The customer therefore stands on the correct side without
being lifted to preserve an ambiguous source-projection foot location. Normal
projection vertical position consequently changes. Shop geometry, all other
bone transforms and character proportions remain unchanged. Front and normal
views confirm that the formerly embedded customer now stands outside the
counter; the reverse solid wall naturally hides the customer.

## Verification

- Nine native sanitizer checks and 21 Python tests pass, including explicit
  source-hidden occupied seats, missing exact poses, invalid visibility metadata
  and both occupied eye anchors. Both walk modes and 17,808 view checks pass.
- Compile-only native app build and signature verification pass. The two existing
  unused-variable warnings remain. No application launch or installation was
  performed this turn; final headed acceptance is still outstanding.
- Thirty-seven new postures have 148 sanitized individual inspection views.
  All prior character geometry/materials/pose data compare exactly with the
  saved pre-change candidates.
- All 416 occupied vehicle identity/frame combinations produce 1,248 sanitized
  perspective/orthographic captures. Monorail side and end views show occupants
  being obscured by seats/canopy rather than removed by source visibility.
- Eleven castle frames have 55 perspective captures with the restored guest and
  mixed occupants. Source-selected keys and actual identities remain checked.
- All 1,640 mapped occupied shop states resolve with exact native identity,
  pose and source-visibility metadata. These images precede the physical depth
  correction and are retained as binding evidence, not final placement evidence.
- After the depth correction, numerical checks cover all 1,640 customer cases
  and prove unchanged jump Z, building geometry and non-customer transforms.
  Fifty-six new reverse/orthographic images cover the 28 restored shop actions;
  an additional front view verifies the previously embedded adult customer.
- All 112 owned source resource hashes remain unchanged. No native source
  rendering code changed in this follow-up.

Private evidence is in `vehicles/occlusion-verification.json`,
`vehicles/occlusion-final-state.json`, `characters/occlusion-models.json`,
`characters/occlusion-pose-verification.json`, `rides/occlusion-verification.json`,
`shops/occlusion-customer-verification.json`, `shops/customer-space-verification.json`
and `shops/customer-space-views.json` under `work/remaster/`.

All these remain candidate artwork. Full ride/action coverage, hand and prop
contact, complete normal-gameplay integration, special tracks and cameras,
other missing source states, detailed visual review, live animation timing and
full-park performance are unfinished. The diagnostic source-art fallback is
not accepted as satisfying the user's no-placeholder rule.
