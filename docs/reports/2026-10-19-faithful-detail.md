# Faithful geometry detail and original-sprite overlay studies

2026-10-19, Codex. Branch: `codex/faithful-geometry-detail`, isolated worktree
`openpark-visitor-3d-mockups`. The user's Nightly checkout and previous study
installs were not modified. All owned-art meshes, authoring scripts, source
exports and captures remain ignored under `work/faithful-detail/`.

## Geometry changes

| Asset | Before triangles | After triangles | Changes |
| --- | ---: | ---: | --- |
| Toy Land | 116 | 980 | Folded thatch bundles, thicker eaves, stepped ridge, raised fascia, window trim, solid threshold |
| Haunted House | 767 | 1,247 | Corrected tower/lower-roof overlap, window surrounds, sills, roof edges, corner trim, steps |
| Gift Shop | 378 | 912 | Cornices, floor bands, tower collars, facade trim, porch edge |
| Coffee Shop | 271 | 1,271 | Cup rim/foot ring, rebuilt handle, parasol ribs/valances, bench and table supports |
| Palm | 148 | 1,542 | Individual folded leaflets, midribs, bent segmented trunk, collars and crown |

Nearest-sampled original palette texels provide every surface color. Entrances
remain closed surfaces with the original dark entrance pixels. No smooth
replacement artwork or new animation clock is introduced. Novelty Shop, Oak and
Birch retain their previous meshes. This is five static refinements, not full
ride/shop/scenery conversion or animation acceptance.

## Overlay question

Two Toy Land experiments use separate profiles:

- `overlay-profile` / `overlay-review/index.html`: entire original sprite on a
  fixed transparent plane just ahead of the model. The original front image is
  preserved, but quarter views expose a second chimney and side views reveal
  the thin panel. This is useful evidence of the limitation, not a final design.
- `facade-profile` / `facade-review/index.html`: only the front facade artwork
  mapped onto shallow surfaces matching the gable outline. Roof and chimney
  remain geometric. This preserves the sign/windows/entrance together but
  covers some raised trim. It still needs user review at oblique angles.

Both experiments use the actual native textured-model renderer, not an image
composite. Each contains 48 strict before/after captures over 24 angles. Neither
experimental overlay replaces the installed detail model.

## Evidence and install

`review/index.html` contains original sprite exports, synchronized before/after
turntables, copied-park comparisons and five captures from a running native
headless game. `tools/visitor_model_review.py` requires strict authored-model
loading, records model/image SHA-256 values and fails on incompatible sprites.

- 240 geometry captures and four park captures pass ASan/UBSan. Park inspection
  confirms all five structure mappings; a separate fixture with added trees
  confirms all eight model mappings and matching composite dimensions.
- All 240 logged hashes match the current before/after meshes. The previous
  Faithful profile's eight meshes match the saved before copies byte for byte.
- `tests/check_walk.sh` passes both walk modes, 17,808 view checks, and sanitized
  mesh/model/rig tests. Python, shell and review JavaScript syntax checks pass.
- A newly linked native executable completes a bounded, muted 39-second game
  run with copied scratch saves; all five requested Visitor View captures report
  `drawn=1`, with zero Reimagined rigs. The Haunted House frame is partly blocked
  by a guest, so isolated turntables provide the useful building inspection.
- `OpenPark Faithful Detail Study.app` is installed in `~/Applications`, with
  bundle identity `com.joshspeakman.openpark.faithfuldetailstudy` and its own
  `work/faithful-detail/profile`. Build and code-signature verification pass.
  Its explicit Faithful bundle setting overrides inherited Reimagined mode.
- Headed attempt `headed-fyc5ikvp` exits -6 after 0.11 seconds without a window
  or capture. No headed or overall visual quality pass is claimed.

Hidden sides/rears remain inferred. Reused source regions still stretch or
repeat on some surfaces; the Coffee Shop entrance front remains a particularly
weak mapping. More triangles alone do not fix those UV/layout issues. Next work
should use the overlay comparisons to choose a front-surface treatment, refine
source alignment and unseen surfaces, and resolve the headed-launch failure
before claiming playable visual acceptance.
