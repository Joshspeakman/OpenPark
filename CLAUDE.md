# Theme Park native recompilation (macOS first; Windows and Linux next)

- Use only the user's owned Theme Park package. Downloads and installed games are read-only.
- Final product: native code on each platform (macOS on Apple Silicon first; Windows and Linux next, docs/PORTING.md, the user's say 2026-10-19), no DOSBox, Wine, Rosetta or CPU interpreter at runtime. Platform code lives in platform/<os>/ and runtime/platform_*.c; runtime/ stays portable C. Unicorn is permitted as a development-only differential oracle.
- Keep package contents, executables, generated C, screenshots, saves and traces under ignored work/ or build/. Never commit game-derived material. Stage explicit paths only. Exception (the user's say, 2026-10-19): the README's in-game screenshots in docs/screenshots/.
- Work locally on nightly. Do not change main, rewrite history, create a remote repository, publish, or install an app without the user's instruction.
- Follow the user's commit identity: Joshspeakman <44591690+Joshspeakman@users.noreply.github.com>. Identify Codex truthfully in commit messages.
- Automated game runs must be bounded, background-only, muted, and use scratch profiles (DS_TEST=1 W32_AUDIO_MUTE=1). Stop only owned test processes. No router, lobby or public-server operations.
- Move deleted artifacts to ~/.Trash; never empty it.
- Existing ports are read-only references. Record reused code provenance; do not carry unrelated game hooks into this port.
- Follow LLM-Decomp-Guides: survey first, differential verification before whole-game claims, explicit traps for unsupported behavior, measured optimization, concise progress updates.
- Maintain docs/PLAN.md with verified results, limitations, next steps and dated decisions. A routine test is not a playable game.
