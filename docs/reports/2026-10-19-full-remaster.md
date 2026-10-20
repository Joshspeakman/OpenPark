# Reimagined full remaster — work in progress, 2026-10-19

The user's latest scope supersedes the previous study/review sequence: remaster
all world artwork and animations in both the normal park and Visitor views,
with UI elements exempt. Preserve the original exaggerated proportions,
identities and character. No generic primitive substitutes, soft rounded toy
style, placeholders, recolours counted as replacements, partial release, or
pauses for concept approval. Walking and seated guests must remain the same
characters, with expressions, carried items, work and entertainer routines.
Custom track rides, vehicle assemblies and their passengers are mandatory.

Work is isolated in `codex/reimagined-full-remaster`. Neither `main` nor
`nightly` is edited. The working tree is
`/Users/joshuaspeakman/Projects/openpark-visitor-3d-mockups`. Future installation
reuses **OpenPark Reimagined Study.app** and its existing bundle identifier;
this task has not installed another application. `work/remaster/profile` has
copied saves and links to the already isolated owned game copy.

## Implemented engineering

- The existing launcher's Display panel has an Art Style row, with Original,
  Faithful 3D and Reimagined. Selection is resolved after the launcher closes,
  persisted in the isolated bundle's defaults, and shared by the renderers.
  Explicit test environment settings take precedence. Launcher structure and
  artwork are retained.
- The remaster build and launcher reject a missing or inconsistent complete-pack
  receipt. `--remaster` installation is blocked until that receipt exists.
  Compile-only builds remain available for engineering verification. There is
  no production partial-pack flag.
- `TPRIG2` extends the existing bounded native rig format with UV coordinates
  and full-colour RGBA textures. Geometry, bones and source-keyed poses are
  shared between perspective rendering and a new orthographic bake tool.
  Alpha cutouts participate in the existing depth buffer.
- `tools/remaster_bake.c` uses the native rasterizer and model loader to produce
  a pose in the park's shear projection. Authored anchors and pixel scale are
  explicit inputs. It exports raw RGBA; it does not guess geometry or animation.
- `TPRART01` carries palette-quantized bakes for original world-part IDs.
  Ground, native park things and Visitor composite capture share the same
  replacement lookup. Source dimensions and decoded-pixel fingerprints must
  match; source memory, animation links and UI palettes are never rewritten.
  Native clipping, mirror/flip, ghost and one-colour semantics are preserved.
- `tools/remaster_catalog.py` inventories orphan as well as referenced records,
  seated substitution tables, both animation families, videos and unresolved
  resources. `tools/remaster_pack.py` refuses a release when the audit is
  incomplete or a bake simply reproduces the original pixels. Conflicting
  replacements for aliases of the same source data are rejected.

The pack compiler currently produces the baked world-part payload. Installing
and validating all canonical model bindings remains required; evidence of
that is an explicit integration gate. The tool does not generate missing models
or infer unverified state semantics.

## Inventory and evidence

The owned source snapshot contains 81 definition/guest records, 3,545 sprite
and ground records (2,759 nonempty), 11,408 non-sentinel frame records across M
and MD, and 1,380 animation slots. The table traversal finds zero malformed
references. These counts are not finished asset or animation coverage.

The ledger separately requires semantic state/transition evidence for every
definition, normal and Visitor capture evidence for every nonempty animation,
and the following world channels: five custom-track families; trains;
seated guests; ride overlays; items and expressions; entrances; animated
scenery; world signs; litter and cleanup; weather streaks, particles and
lightning; and construction previews. Unknown resources remain unresolved
instead of being silently classified as exempt UI.

Local evidence, all ignored:

- `work/remaster/catalog/ledger.json` and `audit.json`: complete source ledger,
  with **zero accepted remastered replacements** at this stage.
- `work/remaster/release-gate-test.json`: coverage, pack compilation and
  installation all reject the incomplete work; no release payload was created.
- `work/remaster/walk-tests-final.log`: both walk modes, 17,808 view checks,
  and existing mesh/model/rig/layer tests pass under ASan/UBSan.
- `work/remaster/remaster-tests-final.log`: new native tests cover source
  matching, mode isolation, unchanged source memory, clipping, mirroring,
  flipping, ghost/colour paths, malformed packs, RGBA cutout depth, shared poses,
  orthographic scale and bounded texture/UV loading. Six synthetic Python
  tests cover missing evidence, partial sequences, original/concept/placeholder
  treatment, changed evidence, unknown resources and bake encoding.
- `work/remaster/build-final.log`: native ARM64 compile and code-signature
  verification. Existing unrelated unused-variable warnings remain in view.c.
- `work/remaster/launcher-display.png` and `launcher-test.json`: the launcher
  renders and exits successfully in this environment. The prior AppKit launch
  restriction no longer blocks this launcher check.
- `work/remaster/runtime-i_regza2/`: a 37.28-second muted native run with copied
  saves, Original mode, and two captured windows belonging to the test PID.
  The normal view shows the populated test park; Visitor View shows the
  occupied castle and surrounding original artwork. Both captures were
  inspected. Exit 0 and complete control acknowledgements; no process remains.
  This verifies shared-code execution, not new-art quality or animation coverage.
- `work/remaster/source-integrity.json`: all 112 owned DATA resource hashes
  still match the source ledger after the native tests.

Two material sheets were generated with built-in `image_gen.imagegen`.
`work/remaster/art/materials-v1.png` is rejected for realistic surface noise.
`materials-v2.png` is a candidate with flatter, angular motifs; it still needs
UV/seam and in-game checks and is not installed or counted as a completed
asset. Exact prompts and provenance are beside the files. No original pixel
upscale, concept sheet, or synthetic test fixture has been installed as art.

## Unfinished work and practical limits

This is **not a finished graphics remaster**. The canonical world model pack,
all guest/staff/entertainer actions and expressions, seated identities across
every ride, procedural custom-track geometry, world effects and first-person
ride videos are still outstanding. The existing two primitive guest studies
are not accepted production art and are absent from the new profile.

The normal-view bake path currently retains the native palette and part
dimensions. It does not provide an HD overhead framebuffer. A model's
orthographic bake is not proof of its in-game placement or correct hidden
surfaces. Static source matching does not prove animation timing, and passing
synthetic raster tests does not prove an entire park's visual quality.
RGBA model materials currently use binary alpha cutouts, not translucent
surface blending. The ground bank also contains raised fences, walls and
other geometry: treating every ground-bank record as a flat material would
lose those details and is not an acceptable conversion.

Runtime source lookup remains a conservative linear scan with source-content
verification for candidate matches; full-pack CPU/memory cost has not been
benchmarked. Exceptional native drawing paths and complete model eligibility
still need coverage. Development fallback paths remain in the renderer but
cannot satisfy the release audit. The new profile contains no partial pack.

`tools/check_remaster_runtime.py` exercises the cloned native application with
muted, scratch-profile, background-only headed captures of both perspectives.
Its default Original mode verifies the shared code, not remastered art.
Reimagined mode requires a complete pack. No full remastered quality or
animation pass is claimed.
