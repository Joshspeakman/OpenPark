# Method and reference provenance

Read the user's private [LLM-Decomp-Guides](https://github.com/Joshspeakman/LLM-Decomp-Guides):
README, general working practices, Dungeon Siege 1 overview and chapters 02
(recompiler), 03 (Win32), 04 (graphics), 09 (verification), 12 (pitfalls), and the
Dungeon Siege 2 springboard. Applied its survey-first method, original-vs-native
differential testing, scratch-only outputs, and explicit limits on claims.

Existing ports reviewed read-only:

- Freedom Force: executable analysis/lifting, the CPU context and flags, test
  harness, background/muted runs, and overlay architecture. `prove_native.py`
  and the build import its lifter and include its runtime headers. Since
  2026-10-19 (issue #8) the six files used are vendored unchanged in
  `vendor/freedom-force-recomp/recomp/` (both projects are the user's own);
  `config/reference.json` pins the source commit and their hashes, checked
  before every use (`flow_targets.py`, imported lazily by `lift.py`, was missing
  from the pins and is now included). All game hooks and overrides are empty in this
  proof; the lifter's game-specific command-line build is not invoked.
  The DOS adapter now also reuses only its generic instruction emitters. It
  replaces collection/dispatch, segment accesses, DOS boundaries and string
  handling; no Win32 service implementation or game hook is imported into the
  native product. Python bytecode writes into reference directories are disabled.
- Delta Force: Watcom conventions, code discovery, self-modifying renderer
  treatment, paletted/software framebuffer handling, and platform-service reuse.
  Those findings are leads for Theme Park, not proof that it has the same engine.
- Brutal Half-Life: bounded routine/startup proofs, original-data preservation,
  explicit unsupported behavior and honest milestone acceptance criteria.

LE parsing is newly written here using the structures and constants documented
by [Open Watcom's exeflat.h](https://github.com/open-watcom/open-watcom-v2/blob/master/bld/watcom/h/exeflat.h).
That reference is downloaded only under ignored `work/reference/`. No game code
or original assets are included in project sources. Synthetic parser fixtures
are constructed by the tests.

The owned package and three executable identities are in `config/inputs.json`.
Unpacking archive data does not run the package's installation scripts. The
bundled DOSBox configuration was read as evidence of the shipped platform;
neither DOSBox nor the GOG launcher was launched.

Additional primary references and synthetic host choices are recorded in
[DOS-CONTRACT.md](DOS-CONTRACT.md). The optional mouse extension is treated as
unavailable, not as successful hardware emulation. No external implementation
source was copied for DOS services, memory allocation, VGA presentation or input.
