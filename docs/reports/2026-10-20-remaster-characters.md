# Character authoring and source interpretation — 2026-10-20

The full remaster is still in progress. Nothing here is release acceptance,
a review pause, or a partial-art installation.

Eight guest identities have new local articulated candidate geometry under
`work/remaster/characters/models/`. These include distinct hair, garments,
collars, cuffs, hands, shoes, facial features and original height differences.
Children's width/height ratios were corrected independently; they are not
uniformly scaled adults. Sixteen bones articulate each identity.

The local author writes 128 source-keyed walking poses. These are **candidate
poses**, not a claim that the original animation has been matched. Directional
phase alignment, source silhouettes, foot travel, arm bends and body bob still
require corrections. Source walking references cover all 16 guest/staff
identities (256 frames); another 184 references cover shop-action substitutions.
Staff, entertainers, shop operators and remaining actions are not yet complete.

The native sanitized inspector rendered 512 views across the guest poses;
ground minima remain zero. Overscan silhouette diagnostics report mean
intersection-over-union of approximately 0.53–0.60, which is evidence of
remaining differences, not an acceptance score. All 128 poses extend beyond
the old source crop. Expanded replacement-image bounds are therefore required
to avoid clipping the new geometry in the normal view. Do not crop feet away
or move/resize the character independently in every frame to conceal this.

Evidence and ignored authoring live in `work/remaster/characters/`:
`source-poses.json`, `source-identities.png`, `walking/`, `shop-actions/`,
`models.json`, `verification.json`, `silhouette-diagnostics.json`,
`renders/`, `bakes/`, `overscan/` and `authoring/`.

## Corrected table interpretation

A read-only survey of the user's existing translated source under
`theme-park-macos/work/app-release/` verified each recorded table operand
against the bytes at its PC in the copied memory snapshot. File hashes,
instruction contexts and PCs remain in the ignored
`action-code-references.json`; no translated game code was copied into the
tracked repository.

- Guest slot 11 is assigned directly to frame word 10 at 0x14222d,
  0x142363 and 0x143d36. It is a composite-frame reference, not an
  animation-table index. The catalog now separates it from animation refs,
  avoiding false ownership of an unrelated animation.
- Slot 12 adds direction byte 0x12 before the animation lookup at 0x125c11,
  0x125caf and 0x13067a. All four directional references are now recorded.
- Unverified action names remain unverified; appearance is not proof of
  simulation meaning or timing.

The updated catalog has zero malformed references. Three native sanitizer
tests and ten Python tests pass, including direct-frame ownership without an
invented animation edge. No runtime source changed in this checkpoint.
The full release audit remains incomplete with 3,847 blockers and no accepted
replacement assets. No new application was installed and no headed
remastered-art pass is claimed.
