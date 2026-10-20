# Correct staff identities and directional coverage — 2026-10-20

The earlier catalog started staff records at the job-kind field and read the
next record's name and animations. It therefore omitted Shark Man, assigned
Mechanic the native ID 0 instead of 1, and shifted every other staff ID.
There are nine staff identities, not eight.

The corrected table begins at `0x1d35c0`, with 54-byte records, a 32-byte name
at offset zero, animation fields at `+0x26/+0x28/+0x2a`, job kind at `+0x2c`,
and a zero `+0x22` word terminating the table. Native instructions at
`0x14f729` select the record from entity byte `0x59`. The lookups at
`0x128729` and `0x132366` add direction byte `0x12` to the first two
animation groups; both groups now include all four directional references.
The `+0x2a` lookup at `0x132fa2` is retained as its own animation reference.
Activity names remain unverified.

The native IDs are now Shark Man 0, Mechanic 1, Handyman 2, Chicken Man 3,
Teddy Man 4, Rhino Man 5, Squid Man 6, Strong Man 7 and Guard 8.
No staff models had been authored against the previous IDs.

Verification:

- The table reader has bounded, source-free tests for the first/last records,
  all direction references, the actual sentinel field and missing terminators.
  All fourteen Python remaster tests pass.
- Instruction operands were checked against the copied owned executable.
  Local evidence: `work/remaster/characters/staff-code-references.json`.
- The rebuilt catalog contains 82 assets, nine staff identities and zero
  malformed references. The full audit remains incomplete with 3,848 blockers
  and no accepted replacements.
- Regenerated references contain 17 guest/staff identities and 272 walking
  frames, plus the unchanged 184 shop-action poses.
- A cross-check of 5,157 previously captured native staff samples confirms
  additional observed frames beyond the definition groups: mechanic frames
  in source chain 204, and handyman frames in chains 196–198. These remain
  explicit mapping work, not inferred completed job animations. Evidence:
  `work/remaster/characters/staff-observed-actions.json`.

This corrects the inventory used for authoring and acceptance. It does not
add replacement staff artwork or claim complete staff animation coverage.
All owned data, reference sheets and instruction extracts remain ignored
under `work/`. No application was installed.
