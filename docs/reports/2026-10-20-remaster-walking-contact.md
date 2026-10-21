# Walking contact correction — 2026-10-20

The Tree House contact work exposed a shared authoring error in the eight
guest walking rigs and the mechanic and handyman. Ground normalization
included collapsed hidden groups at zero height. Their zero coordinates
could win the minimum-height calculation while the actual shoes remained
above ground. The largest measured error was 0.055246 world units.

All 160 walking keys in these ten candidates now use the actual shoe groups
12 and 15 to find ground contact. Each affected pose receives one uniform
vertical translation across its active groups. Scale, limb rotations,
horizontal placement, frame keys and carried-tool relationships are
unchanged. Hidden groups stay collapsed. The private authoring helper was
corrected as well; Tree House neutral arm substitution now carries the
matching root offset so regenerating it preserves the existing ride poses.

The audit inspected all 272 walking keys across eight guest and nine staff
identities. The other seven staff candidates already had a visible contact
envelope at zero; they were not changed. This envelope result alone does
not establish costume-specific contact fidelity.

Verification:

- All 160 corrected keys have an actual shoe contact at zero and no active
  geometry below ground, within the serialized model precision.
- Exact comparisons preserve geometry, material payloads and all 765
  nonwalking poses in the ten modified files. Only active groups' vertical
  translations changed in their walking keys.
- Reconstructing guest and worker walking poses from the corrected authoring
  code reproduces the serialized candidates within 0.000001. All 40 Tree
  House additions also reproduce without changing their current poses.
- 640 controlled source-frame captures pass: all 160 keys in both native
  normal modes, Visitor perspective and orthographic inspection. All resolve
  the candidate model. All 160 orthographic borders remain clear.
- The other 109 candidate models and all 112 owned resources preserve their
  hashes. The private inventory remains at 119 candidates.

Evidence, exact backups, correction script, authoring reproduction checks,
captures and inspection GIFs are under
`work/remaster/characters/ground-contact/`. GIFs use a six-fps inspection
cadence, not measured game timing. `verification.json`, `changes.json`,
`captures.json` and `preservation.json` record the current results. Prior
capture reports remain historical evidence for their recorded hashes.

No runtime code, app build or installed app changed in this correction.
Main/Nightly remain untouched. This is contact-plane validation for fixed
walking poses, not temporal foot locking, slope handling, live travel speed
or final animation acceptance. The full remaster, complete pack and headed
captured acceptance remain unfinished.
