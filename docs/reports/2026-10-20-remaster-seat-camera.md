# Reimagined exact-seat camera — 2026-10-20

The ordinary vehicle camera now has an explicit path from the native guest ID
to its car, seat, authored posture and eye anchor. It does not use appearance
variant as personal identity. This fixes the shared linked-chain case where a
later car's passenger could be assigned to the first car.

The change stays on `codex/reimagined-full-remaster`. No installation or accepted
artwork was produced. Main and Nightly remain untouched. Generated anchors,
character authoring and captured evidence stay under ignored `work/remaster/`.

## Behavior

Snapshots retain entity IDs only for validated native seat occurrences, including
explicit source-omitted markers. The exact mapping corrects the legacy container
lookup. Reimagined street-view rendering resolves one unambiguous car/seat for
the followed guest, verifies the visible occupant poses, and composes the eye's
head-bone transform with the guest pose, seat and physical car placement. The
camera inherits car pitch and direction even when stationary. User head/look
controls remain available. The followed character is omitted from their own
first-person image when this exact camera resolves; other riders remain visible.

The `TPREYES1` sidecar specifies sorted model identity, head bone, eye point and
forward direction. Its bounded loader rejects malformed, duplicate, nonfinite,
zero-direction, trailing and truncated input. Missing, ambiguous or hidden-head
bindings do not invent a camera anchor. Eight private anchors use the actual
canonical pupils' midpoint and each character's existing scale. No character
meshes were changed. The helper loads assets only on the app/render thread.

The offline vehicle replay tool's optional `--seat-view` mode retains nearby
track context for eye-level images and records exact guest ID, appearance,
pose, position and gaze in `seats.csv`. Ordinary perspective/orthographic output
remains available for the same recorded state.

## Verification and limits

Nine native sanitizer checks and 21 Python tests pass. New tests distinguish
same-looking guests in different seats and reject duplicate identity bindings,
missing other occupants' poses, missing anchors and singular head transforms.
They check scale and uphill eye/gaze composition. Native-car tests check exact
IDs, omitted-seat IDs and a linked chain spanning two actual cars. Both walk
modes and all 17,808 view checks pass. Compile-only native build/signature pass,
with the existing two unused-variable warnings.

Four recorded cars have nineteen occupied states each. All 76 eye records match
the native guest ID and appearance; their gaze vectors are unit length. There
are 76 contextual first-person captures. All 1,280 existing non-follow images
remain byte-identical to the preceding physical-path replay. Current private
model/anchor hashes and evidence are in `vehicles/eye-verification.json`.
The samples reuse the earlier 10 Hz native capture, so they do not establish
live camera timing, smoothness or comfort.

Visual inspection of downhill, level-before-climb and steep uphill samples
confirms that the camera changes with the actual car pitch. It also exposes
abrupt authored track transitions: one steep upward view contains only sky, and
the preceding level view faces the rising track. These are not accepted as a
finished ride experience. This diagnostic scene also still contains original
terrain/special-piece content. Camera math is not full visual-quality proof.

A bounded, muted background Original-mode app run completes normally in 37.04
seconds. Both macOS window captures fail with “could not create image from
window,” despite visible owned-window metadata. Internal captures exist in
`work/remaster/runtime-2bk04qqz/`; no headed visual pass is claimed. This run
checks shared application code and cannot accept remastered artwork.

Remaining work includes source-hidden riders in reverse views, all other ride
families' exact-seat cameras, diorama following, bank/roll and special-track
paths, authored transition refinement, live timing and final headed acceptance.
The complete normal/Visitor artwork packs and full-park performance are still
unfinished. No partial pack or diagnostic source-art fallback is accepted as
fulfilling the user's no-placeholder requirement.
