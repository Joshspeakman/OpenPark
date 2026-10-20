# Source-aware placement previews

Codex, 2026-10-20, `codex/reimagined-full-remaster`.

The normal-view construction renderer now captures source-bound full-colour
sprite draws during placement. Previously, the companion surface received only
the game's final indexed pixel differences. That lost detail and could miss a
draw entirely where its resulting palette value equalled the existing pixel.
This change does not accept artwork or enable an incomplete release.

## Drawing and isolation

During the game's placement routine, the view temporarily borrows its spare
hook slot for the original sprite-blitter entry. Coordinates, dimensions,
source, flags, current clip window and solid colour come from that actual draw.
The documented core disassembly confirms that positions and dimensions are
still unhalved at entry and that solid colour is the byte at entry ESP+4.

Source-verified RGBA art is painted into the current visible-region companion.
The original sprite's indexed writes are predicted in a private framebuffer,
using the existing native blitter. At the next sprite, actual framebuffer
changes not explained by that prediction are painted in order: for example,
outlines, labels or another uncaptured operation. This preserves fine texels
instead of covering them with their own palette result. The capture never
changes the game's sprite bank, framebuffer, animation state or placement
selection. Screen furniture is excluded from delta reconciliation.

The hook is restored at routine return, cancellation, mode changes and game
disable. A missed return is cancelled at the next frame. Invalid framebuffer,
clip or prediction state invalidates the companion frame. The full-colour
image helper shares clipping and source-relative pivots with ordinary world
drawing. There is no persistent draw-command allocation: prediction uses one
bounded 640×480-byte buffer.

Original mode does not enable this hook unless the explicit development trace
`TP_VIEW_TOOL_TRACE` is set. That trace observes calls without substituting art.
The complete-pack launcher/build gate is unchanged.

## Evidence

Five native sanitizer checks and eighteen Python tests pass. Added checks
verify unchanged palette pixels retaining new texels, ghost blending against
full-colour content, private prediction of original pixels, later primitive
overlays, subsequent sprite occlusion, excluded furniture/panel rows, invalid
geometry and live hook restoration/framebuffer changes. Both Visitor modes,
17,808 view-coordinate checks and existing mesh/model/rig/layer tests pass.
Compile-only build and code-signature verification pass.

A bounded, muted, scratch-profile Original-mode run completed ride, shop and
scenery placement controls and exited normally after 50.00 seconds. Its trace
contains 128 sprite draws. Comparing encoded dimensions and source offsets
against the local ledger gives a consistent sprite-bank base for all three:

| Placement context | Source part | Source frames | Draws |
| --- | --- | --- | ---: |
| Race Car Ride construction preview | MSPR-0:994 | M/MD:5652 | 65 |
| Arcade shop | MSPR-0:975 | M:5147 | 19 |
| Rose Bush scenery | MSPR-0:1009 | M:1000 / MD:290 | 44 |

The construction component currently has no asset owner in the ledger, but
was already required as a nonempty source part. This observation establishes
one placement use; it does not establish all uses or a complete custom-track
construction sequence. The three observed draws used plain flags; ghost
coverage in this change comes from synthetic tests, not this game trace.

Internal composed previews were inspected. A capture of the reported visible
owned window again failed with macOS `could not create image from window`.
This is **not a headed capture pass or live remastered-art acceptance**. The
earlier exploratory run ended normally at its 90-second bound; its scenery
trace is retained separately. Both test processes are gone.

All 112 source DATA hashes remain unchanged. Evidence remains ignored in
`work/remaster/preview-runtime-oo5hrwzt/`, `preview-runtime-evidence.json`,
`preview-source-integrity.json`, `preview-checks.log`,
`preview-walk-checks.log` and `preview-build.log`. Authoring/control scripts and
captures stay ignored; no application was installed.

## Remaining work

Real remastered preview artwork, larger/multipart and custom-track assemblies,
all invalid-placement states, both-mode live placement and busy-park performance
still require acceptance. The observed construction component needs authored
art. Uncaptured operations currently retain their indexed appearance until
their own remastered rendering is implemented. Effects/lighting and whole-park
export also remain unfinished, alongside the full art/animation workload.
