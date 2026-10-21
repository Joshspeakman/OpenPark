# Maze figure capture — 2026-10-20

The native kind-8 draw decision now reaches both remaster views without
rerolling its selection or advancing the animation again. Metadata event 13
records the signed person selector, selected frame, source offsets,
alternate-table entry and native end-frame suppression. Existing drawing
events retain their order. The capture does not write simulation memory.

Shared scene resolution validates the Maze definition, capacity, kind, body
frame, ride state, linked people and remembered selector. It retains all
linked identities in chain order, including people omitted by the source
image, and separately identifies the selected person and appearance. The
Visitor card stores the same descriptor that the normal pass resolves.
Malformed, duplicate and conflicting figure metadata is rejected.

This is an animation-data implementation. The Maze body, 121 guest overlay
keys, 35 additional peek keys, interior occupancy and complete figure
rendering remain unfinished. Both rendering paths deliberately reject a
body-only replacement while a separate figure is required. No placeholder
model or candidate-count increase was added.

## Multiple Maze correction

The first two-Maze test exposed an existing dependency on draw order. The
renderer initialized its alternate-offset index from simulation memory at
the start of a pass, then allowed an earlier Maze's random choice to alter
the index used by a later Maze's remembered negative selector. Visitor
capture starts each cell separately. The selected offsets therefore differed
between the two views in 42 of 64 owned-data seeds.

Stable per-thing capture now initializes that index from simulation memory
for each kind-8 draw, alongside its existing per-thing random selection.
The same 64 cases have zero view mismatches after the correction. Thirty-four
of their stable-mode source images intentionally differ from the prior
renderer because the incorrect offset is corrected; the other descriptor
differences can share the same offset. The original sequential/oracle mode
keeps native shared draw-order behavior, with all 64 old/new comparisons
matching exactly. This is a scoped stable-capture correction, not a claim
that every source image across every renderer mode is unchanged.

Inspection also confirmed that both native entry points already initialize
the remembered index from memory address 0x1d7be8. The earlier survey's
uncertainty about initialization is resolved; cross-ride leakage during the
pass was the issue found here.

## Verification

- 2,560 source-free remembered-selection cases cover both screen modes,
  sequential and stable random modes, eight appearances, guest/alternate
  selection, four phase positions and both tick cases. All nine alternate
  placements and 320 suppressed endings pass. Actual person IDs remain
  distinct even when the appearances are identical.
- 256 fresh random selections match between normal and Visitor capture.
  Empty rides emit no figure. Native source pixels and simulation memory
  remain unchanged by metadata collection.
- 1,152 two-Maze stable-selection cases cover both screen modes, all nine
  remembered offsets and 64 seeds. The selected frame, person, offset and
  appearance agree across views, without modifying simulation memory.
- Ownership and malformed-input checks reject missing/unlinked people,
  invalid appearances, mismatched remembered selectors, incorrect capacity
  or definition offsets, conflicting riders, high-speed states, repeated
  descriptors, invalid ranges and figure metadata on another object class.
- 2,576 old/new owned-data passes cover all 226 guest/alternate-placement
  keys in both screen modes and both random modes, including 768 initial
  random selections. All 10,344 source events and indexed pixels match in
  these single-Maze cases; 68 terminal frames suppress drawing. Both captures
  agree, all 16 linked guests are retained, and memory is unchanged.
- Thirteen native remaster checks, 21 Python tests, both walk modes, 17,808
  view checks and mesh/model/rig/layer checks pass. The isolated app builds
  and passes signature verification with installation disabled.

All 119 existing candidate models and 112 owned resources retain their
hashes. Main/Nightly and the separate installed app were not changed. The
complete-pack gate remains enabled. No app launch or headed acceptance is
claimed for this metadata work.

Private evidence is under `work/remaster/rides/maze/`: `figure-capture.json`,
`source-comparison.json`, `source-cases.h`, old/new comparison programs,
`multiple.jsonl`, `multiple.log`, `multiple-fixed.json`, test/build logs and
`preservation.json`. The pre-correction multi-Maze reproduction is retained
as historical evidence; its expectation of a mismatch is intentionally no
longer true of the corrected renderer.

Next work remains the actual hedge geometry, all 156 overlay animation keys,
full occupant representation and atomic body/figure rendering in both views.
Live selection timing, entry/exit and other lifecycle states still require
verification. The full game remaster and final headed capture remain open.
