# Reimagined full-colour normal-view rendering

Codex, 2026-10-20, `codex/reimagined-full-remaster`.

Normal world drawing can now retain the full-colour, higher-resolution bake
instead of displaying only its nearest palette preview. This is renderer work,
not completed or accepted remastered artwork. No application was installed.

## Format and draw order

`TPRART03` preserves source identity, decoded-content hash and expanded logical
bounds. Each record adds a uniform texel density (1–8), a logical indexed
preview and the original RGBA bake. Texture sides are limited to 2,048 texels;
decoded previews and RGBA planes have separate 64 MiB and 512 MiB budgets.
Versions 1 and 2 remain readable. Production pack compilation still requires
the complete audit and publishes the hashed completion receipt last.

The indexed preview remains available for game/UI bookkeeping. A new optional
companion target draws full-colour terrain and sprite parts in the existing
native command sequence. Later legacy pixels and world text cover earlier
replacement texels normally. A replacement's indexed preview cannot overwrite
its own fine detail. Source-relative mirror/flip pivots, clip-window origins,
expanded bounds, binary cutouts and native flag precedence are retained.
Full-colour ghost blending is an RGB average, distinct from palette-table
blending in the legacy path.

Normal-view rendering allocates a visible region with an eight-logical-pixel
margin, initially at four texels per logical pixel. Density decreases for large
regions to stay within 16,777,216 pixels (64 MiB) per surface. Work/published
surfaces swap under the existing presentation lock; there is no whole-park
four-times-density allocation. Only trusted pass rectangles update each
region. Failed native passes invalidate the full-colour frame. Mode/park-size
changes invalidate both surfaces; panning outside the published region uses
the indexed canvas until the next coherent frame.

The compositor samples that region beneath the existing UI and weather layers
and converts channel order for the macOS BGRA surface. Reimagined enables the
native compositor even at 1× zoom in a 4:3 window. The existing launcher and
its art selection remain intact. `native_draw.h` now shares the target layout
between normal and Visitor drawing rather than duplicating its declaration.

## Verification

- Five native ASan/UBSan checks pass: source-bound art, textured meshes, RGBA
  drawing, snapshot bindings and normal-view integration. New checks cover
  physical texel preservation, fractional/non-square scaling, flips, alpha
  thresholds, output stride/clip, layering with legacy sprites/text, terrain,
  visible-region bounds/memory, both native modes, UI/weather priority and BGRA.
- Eighteen Python tests pass, including V3 packing and rejecting a nearest
  enlargement of unchanged original pixels while retaining genuinely new
  detail between palette-preview samples.
- Both Visitor modes, 17,808 view-coordinate checks and existing mesh/model/
  rig/layer tests pass. The new view fixture exposed an old test-only clock
  stub with the wrong signature; both fixtures now match the runtime API.
- Compile-only native build and strict code-signature verification pass.
- A bounded, muted, scratch-profile **Original-mode** run exited normally in
  37.17 seconds and completed both capture controls. Its internal normal and
  Visitor PNGs were inspected. macOS `screencapture` failed for both reported
  visible owned windows with `could not create image from window`; this is
  **not a successful headed capture or a remastered-art quality pass**.
- All 112 source DATA hashes remain unchanged. No main/nightly change and no
  installation. Local evidence remains ignored under `work/remaster/`:
  `rgba-integration-checks.log`, `rgba-integration-walk.log`,
  `rgba-integration-build.log`, `rgba-source-integrity.json`, `rgba-headed.json`
  and `runtime-cpnrd4hp/`.

## Remaining work

Construction previews currently retain the game's indexed delta pixels in
the companion surface; they need full-colour source-aware capture. Existing
weather/UI separation is preserved, but new effect art and full-colour
palette-driven lighting/fades still need treatment. Whole-park picture export
continues to use the indexed canvas. Large replacement extents need seam and
culling verification against real animated assets, and performance needs a
busy remastered-park measurement.

This does not accept any assets, complete the full animation inventory, install
a partial release, or bypass the complete-art gate. The prior staff, guest,
shop, scenery and castle models remain local candidates. All remaining world
art, animations, construction/custom-track assemblies and integrated visual
acceptance continue under the autonomous full-remaster goal.
