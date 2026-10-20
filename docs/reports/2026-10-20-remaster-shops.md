# Reimagined shop production checkpoint — 2026-10-20

The autonomous full remaster remains incomplete. This records internal work,
not a slice delivery, approval request, accepted pack or playable remaster.
Main and Nightly remain untouched. No application was installed.

Twelve shop base states now have local canonical textured geometry under
`work/remaster/shops/models/`, produced by
`work/remaster/shops/authoring/author.py`. The models total 19,188 vertices
and 27,974 triangles. They include the food sculptures, striped umbrellas,
serving counters, building roofs, windows, relief signs and trim. Authored
entrances have solid dark backing. The same geometry produces orthographic
bakes and perspective captures.

Review led to corrections to umbrella counts/colours, the arcade canopy,
building platforms, the coffee face/awning, the Toy Land sign and thatch,
and the Tudor roofs. These are still candidates. Further source-fidelity
work is necessary: architectural proportions and asymmetry, material density,
food surface details, ornaments, entrances and unseen sides. Neither successful
loading nor a nonempty render constitutes visual acceptance.

New bitmap materials were generated with the built-in image tool and copied
to `work/remaster/art/shop-materials.png`. The exact prompt and provenance are
in `shop-materials-prompt.txt` and `shop-materials-provenance.json` beside it.
Texture conversion/atlas packing changes the format, not the artwork.
Source-derived meshes, authoring and captures remain ignored and uncommitted.

## Native binding and coverage

The native class-0x0a shop renderer now emits base-frame origin metadata
through its manual composite loop. Visitor models use that origin rather
than the cropped sprite bottom. Base-model eligibility rejects linked
customers, additional overlay/status frames and invalid record offsets;
incomplete base geometry must not consume those composites. Those states
still need their complete replacement bindings before release.

The offline snapshot tool can exercise the actual shop renderer with
`shop FRAME TYPE yaw`, using a private memory copy. It is not a headed game
test and does not claim occupied-state coverage.

Character mapping found the distinct native shop-customer substitution table:
23 sprite slots per guest identity, eight identities, 184 bindings. The
animation catalog and remaster ledger now record these alongside ride poses.
A separate mandatory `shop_customer_substitution` channel prevents this
runtime path being overlooked in release checks. The refreshed inventory
has zero malformed source references and 3,847 completion blockers.
Accepted replacement coverage remains zero.

## Verification

- 48 sanitized native shop-path replays, each logging one Reimagined rig.
- 48 sanitized fixed-camera model inspections and 12 nonempty orthographic
  bakes; all model minimum ground bounds are z=0.
- Native tests cover changing sprite crops, both shop subtypes, both display
  modes, customers, overlays/status marks and invalid record offsets.
- Three native sanitizer tests and nine Python tests pass.
- Both walk modes, 17,808 view checks, and mesh/model/rig/layer tests pass.
- Compile-only native ARM64 app build and signing pass; no installation.

Evidence is in `work/remaster/shops/verification.json`, `renders/`, `bakes/`,
`remaster-tests.log`, `walk-tests.log` and `build.log`. No headed test of the
new full artwork is claimed.

Next production work includes the shared character identities/actions and
the five animated midway shops, followed by the remaining ride, track,
terrain, prop, effect and animation families. Live occupied-state validation,
HD overhead rendering, full-pack integration and final headed captures remain
required. The full-pack installation gate remains closed.
