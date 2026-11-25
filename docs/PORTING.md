# Porting OpenPark to other platforms

macOS on Apple Silicon is the first platform and the only finished one. Windows and Linux come next. This
page is the contract a new platform implements, and what is already portable.

| | macOS (Apple Silicon) | Linux | Windows |
|---|---|---|---|
| Recompiled game code (`tools/build_game.py`) | done | portable C, x86-64 and arm64 | portable C |
| Runtime: DOS host, Sound Blaster model, view, Visitor View, extras (`runtime/`) | done | compiles (CI) | cross-compiles (MinGW-w64); `runtime/platform.h` file shims |
| Platform layer (`runtime/platform.h`) | `platform/macos/platform_macos.c` | `runtime/platform_posix.c` + `runtime/platform_pool.c` | `platform/windows/platform_windows.c` + `runtime/platform_pool.c` (branch `pc`) |
| App: window, sound output, input (`platform/<os>/`) | `platform/macos/` (Cocoa, Core Audio) | `platform/sdl/` (SDL2; branch `pc`): CI-built, run on the Mac with the real game | `platform/sdl/` (branch `pc`): CI-built (MSYS2), not run on Windows |
| Launch window | `platform/macos/launcher.m` | to write (settings.ini for now) | to write (settings.ini for now) |
| Build of the app | `tools/build_app.sh` | `tools/build_linux.sh` (branch `pc`, not run with the game) | `tools/build_windows.sh` (branch `pc`, not run) |
| Owned-package extraction (`tools/prepare.py`) | GOG's Mac `.pkg` (xar) | the same `.pkg` through `tools/pkgextract.py` | the same `.pkg` through `tools/pkgextract.py`; GOG's Windows installer: to write |
| Extras importer (`tools/extras.py`) | done | portable Python, except mounting `.iso`/`.img` (`hdiutil`) | portable, except mounting |

Branch `pc` and its state: `docs/PC-HANDOFF.md`.

CI (`.github/workflows/ci.yml`) runs everything that needs no game files on Ubuntu and macOS:
- the parser and host tests;
- the CPU model against Unicorn;
- `tests/check_portable.sh`: every runtime file compiles for arm64 and x86-64, with platform-only calls as
  errors;
- the Visitor View tests;
- `tests/check_sdl.sh` (branch `pc`): the SDL2 app built on Ubuntu (clang and gcc) and Windows (MSYS2 CLANG64)
  against stand-ins for the game (`tests/stub_game.c`), and stopped cleanly without game files.

## What is already portable

- **The recompiled game:** plain C over the CPU model in `vendor/freedom-force-recomp/recomp/runtime/rt.h`.
  The only architecture-specific part is the x87 rounding mode, which has arm64 and x86-64 branches. The tools
  pick the compiler flags for the machine they run on (`tools/toolchain.py`; `CC` overrides the compiler).
- **`runtime/`:** C11 with GNU extensions and pthreads. It contains:
  - the DOS/DPMI host;
  - the Sound Blaster 16 and DMA model;
  - the zoomable park view and its native drawing;
  - Visitor View, Free Mode and the larger maps;
  - the EXTRAS menu;
  - the 3DO video decoder (`runtime/cinepak.c`).

  It uses four platform functions (below), and nothing else from the operating system beyond the C library.
- **The Python tools:** they need Python 3.9+, Capstone and Unicorn (`requirements.txt`).

## `runtime/platform.h`

| Function | What it does | macOS | POSIX |
|---|---|---|---|
| `tp_plat_realtime_thread(period)` | asks for the game thread to be woken on time | Mach time-constraint policy | `SCHED_RR` with `TP_RT_SCHED=1` (Windows: HIGHEST + MMCSS) |
| `tp_plat_game_thread()` | marks the game thread interactive | QoS user-interactive | nothing |
| `tp_plat_sleep(seconds)` | a precise short sleep | `mach_wait_until` | `clock_nanosleep` |
| `tp_plat_parallel_for(n, ctx, fn)` | the compositor's row bands across the cores | `dispatch_apply_f` | a thread pool (`runtime/platform_pool.c`) |

## What a platform's app provides (`platform/macos/` is the reference)

1. **Run the game:**
   - load the image (`tp_host_set_image`, the exported `MAIN.TPIMG` and `INTRO.TPIMG`);
   - start the host (`tp_host_init`);
   - call `tp_host_run` on a game thread until it ends;
   - restart when the game exits with errorlevel 10 or more, as `THEME.BAT` does.
2. **Present:**
   - in the park, the composed view from `runtime/view.c`;
   - elsewhere, the 8-bit screen and its palette from the host;
   - a window with a 4:3 or a wide aspect.
3. **Sound:**
   - pull 44.1 kHz stereo float from `sb16_play`;
   - send MPU-401 MIDI bytes (`tp_audio_midi`) to a General MIDI synthesiser;
   - play the extras' WAV tracks for CD and 3DO music, and the 3DO ride videos' sound;
   - put a peak limiter on the output.
4. **Input:** keyboard scancodes and mouse moves and buttons into the host (`tp_host_key`, `tp_host_mouse`), the
   view's zoom and pan, and Visitor View's movement.
5. **Settings and the launcher:** the settings the host and `runtime/qol.c` read (`tp_extras_get/set`, the
   environment the app sets before the game starts), and the data root (`~/Games/ThemeParkNative` on macOS).
6. **Video:**
   - the 3DO ride videos: frames from `runtime/cinepak.c`, shown over the paused game when `runtime/qol.c`
     asks (`tp_ride_video`);
   - Visitor View's ride-along clips, a separate path (on macOS through AVFoundation).

## Open items

See `docs/PC-HANDOFF.md` (branch `pc`): the proof with the game on x86-64 (`tests/check_native.sh --park`), a played
session on each system, then the app-layer parity list (launch window, extras' music and videos, quick exit and the
controls card, the audit's app-layer fixes).
