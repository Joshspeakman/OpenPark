# Maze animation survey — 2026-10-20

The Maze's definition references one static hedge frame in chain 324, but
that frame is not its full animation scope. Native ride kind 8 adds a
separately selected animated figure. The current remaster seat resolver
rejects kind 8, so a body-only model would not complete this ride.

The owned baseline and source animation table identify:

- One hedge/body key, with a 127-by-70 source image and anchor (61, 53).
- A native capacity of 16 and the linked guest chain.
- Eight guest-specific overlay sequences, containing 121 distinct keys.
  None of those keys is currently authored in the canonical guest models.
- Three additional peek sequences containing 35 distinct keys, selected
  through nine entries with different source offsets.

The source shows guests peeking, looking around and tumbling, as well as
small brown and grey figures. These require geometry, expressions and
occlusion, not a static visitor at a guessed hedge position. Detailed
interpretation of the tiny non-guest figures remains unresolved.

`runtime/native_land.c:ride_draw` selects the figure from the actual linked
people when the remembered selector at +0x4d is zero. A positive selector
uses a guest-specific initial sequence or the selected person's current
frame. Negative selection uses an alternate figure sequence and offsets
from the nine-entry table. It advances the frame on the native four-tick
boundary and omits drawing at the marked end of a sequence. The renderer's
remembered alternate index also needs examination for persisted snapshots
and multiple rides; this survey does not establish its lifecycle fidelity.

The extra figure is currently emitted as sprite blits without a dedicated
remaster actor descriptor. It must be captured at the actual native decision
point to preserve selection, frame, offsets and person identity. The normal
and Visitor renderers must consume that same decision; independently choosing
an animation in each view would desynchronize them. Source-pixel and event
comparisons are required before changing this path.

Private evidence is in `work/remaster/rides/maze/`: `source.json`,
`animated-overlays.json`, `survey.json`, the original body export and all
17 overlay strips. The private asset ledger records these additional
requirements. No Maze model, runtime support, acceptance or candidate-count
increase is claimed. Inventory remains 119. Next work is the native overlay
contract, complete hedge geometry, all 156 overlay keys and actual occupancy,
followed by source-equivalence and both-view animation checks.
