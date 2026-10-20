# Reimagined shop customers — 2026-10-20

Native shop capture now records the actual customer identity, source pose and
visibility for every marked component. Reimagined can attach that same guest's
canonical model to the shop's exact native frame. Missing guest geometry or an
attachment matrix still rejects the composite; no generic person is inserted.
The full remaster remains incomplete and no new art is accepted for release.
Work remains on `codex/reimagined-full-remaster`. Main, Nightly and the installed
application are unchanged; the existing study build was rebuilt without install.

## Binding behavior

The native shop renderer uses a distinct 23-slot substitution table. New keys
`65312 + flip * 32 + slot` preserve its posture identities independently from
ride poses. Slots 23 and 24 describe the observed identity-zero-only source
parts 58 and 65. Their ten source occurrences draw for identity zero and remain
invisible for the other identities, matching the native renderer. Unknown
marked parts or unsupported flags reject the model instead of guessing a pose.

Multiple marked parts retain separate attachment slots even when they refer
to the same guest. An intentionally empty substituted sprite records a known,
invisible pose. A linked customer with no drawn marked parts contributes no
body. Customer pointers, class, identity and attachment keys are checked;
extra/status frames and scaled frame mode still require separate support.

The five midway candidates now provide per-frame attachment matrices. Source
part centers and bottoms determine the projected attachment origins. Each
shop uses a fixed depth based on its lowest customer source foot position;
native frame offsets supply the raised positions. These physical depths are
an authored reconstruction, not recovered collision or skeleton data.

The snapshot tool accepts `shop FRAME TYPE YAW customer ID`. Its optional
`TP_SNAPSHOT_CAPTURE_DIGEST` mode records source-card checksums and attachment
metadata without a perspective render. `TP_SNAPSHOT_ORTHOGRAPHIC` renders the
same native-selected models, poses and matrices in the park projection.
That is offline geometry evidence, not integration of a completed normal-view
art pack into gameplay.

## Candidate customer artwork

The existing eight guest models now contain 172 additional nonempty shop-pose
candidates: 170 table substitutions and two identity-zero exceptions. Fourteen
empty table entries remain absent. Total local guest keys increase to 549.
The new poses use the walking characters' existing geometry, clothes, hair,
faces, limbs and shoes. No separate stock customer model was introduced.

Local authoring includes throwing preparation/release and recovery, raised-arm
celebrations, wide-legged jumps, crouches, aiming and neutral stances. Hands
use authored source-space landmarks refined against nearby skin pixels;
legs and shoes remain articulated, and contact is solved without independently
resizing the character for each frame. Only the unflipped poses observed in
this inventory are authored; the key format and guards also support flips.

These are **unaccepted candidates**. Landmark inference, body/limb proportions,
hand and prop contact, facial performance, aiming details, transitions and
live pacing still need source review and refinement. Offline pose coverage
is not proof that the game's animation character and charm have been matched.
The shopkeepers' cropped lower bodies and remaining booth/ornament quality
also remain production work.

## Verification

- Five native sanitizer checks and eighteen Python tests pass. The expanded
  snapshot fixture covers all eight identities, all 23 slots, four flip states,
  both display modes, empty poses, exceptions, multiple components, insufficient
  attachment capacity, invalid records and composite guards. Its final added
  multi-component cases were rerun under ASan/UBSan.
- Both Visitor modes, 17,808 view checks and existing mesh/model/rig/layer
  checks pass. Compile-only native app build and signature pass.
- All 1,640 mapped occupied source states (205 frames × eight identities) have
  identical source-card dimensions, anchors and pixel checksums before/after
  the native renderer change. Every new attachment identity, key and visibility
  matches the source map. There are 1,351 visible and 289 invisible occurrences.
- Current guest models have 688 sanitized fixed-camera inspection images and
  172 normal-projection bakes for the new poses. Current occupied shop models
  have 1,640 sanitized native perspective captures, each drawing a Reimagined
  rig, plus 560 offline orthographic captures covering every observed
  shop/pose/identity combination. Evidence includes model/capture hashes.
- A muted, scratch-profile, background-window **Original-mode** regression ran
  for 36.77 seconds, captured normal and Visitor windows, completed its script
  and exited zero. Both actual window captures were visually inspected. This
  validates shared-runtime regression only, not remastered-art acceptance.
- All 112 source DATA resource hashes remain unchanged. No application was
  installed, and no partial-art release gate was bypassed.

Owned art, authoring, model files and captures remain ignored. Evidence is in
`work/remaster/shops/customer-{differential,verification,orthographic}.json`,
`customer-{tests,snapshot-final,walk,build,runtime}.log`,
`customer-source-integrity.json`, and
`work/remaster/characters/shop-verification.json`. The headed run is
`work/remaster/runtime-_ozs8wew/`. New customer matrices/poses change model
hashes; earlier midway/guest reports describe their earlier artifact versions.

Remaining work includes visual refinement and live timing for these customers,
normal-view source-part pack production, all remaining guests/actions/riders,
rides and custom track systems, world surfaces, scenery, props and effects.
The complete release gate stays closed and full remastered headed acceptance
remains required.
