# Porting to other platforms

macOS on Apple Silicon is the first platform and the only finished one. Windows and Linux come next. This
page is the contract a new platform implements, and what is already portable.

| | macOS (Apple Silicon) | Linux | Windows |
|---|---|---|---|
| Recompiled game code (`tools/build_game.py`) | done | portable C, x86-64 and arm64 | portable C |
| Runtime: DOS host, Sound Blaster model, view, Visitor View, extras (`runtime/`) | done | compiles (CI); `runtime/platform_posix.c` | needs `platform_windows.c`, and pthreads or a shim |
| Platform layer (`runtime/platform.h`) | `platform/macos/platform_macos.c` | `runtime/platform_posix.c` (untested in play) | to write |
| App: window, sound output, input, launcher (`platform/<os>/`) | `platform/macos/` (Cocoa, Core Audio) | to write | to write |
| Build of the app | `tools/build_app.sh` | to write | to write |
| Owned-package extraction (`tools/prepare.py`) | GOG's Mac `.pkg` | GOG's Windows installer: to write | GOG's Windows installer: to write |
| Extras importer (`tools/extras.py`) | done | portable Python, except mounting `.iso`/`.img` (`hdiutil`) | portable, except mounting |

CI (`.github/workflows/ci.yml`) runs everything that needs no game files on Ubuntu and macOS:
- the parser and host tests;
- the CPU model against Unicorn;
- `tests/check_portable.sh`: every runtime file compiles for arm64 and x86-64, with platform-only calls as
  errors;
- the Visitor View tests.

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

  It uses three platform functions (below), and nothing else from the operating system beyond the C library.
- **The Python tools:** they need Python 3.9+, Capstone and Unicorn (`requirements.txt`).

## `runtime/platform.h`

| Function | What it does | macOS | POSIX |
|---|---|---|---|
| `tp_plat_realtime_thread(period)` | asks for the game thread to be woken on time | Mach time-constraint policy | `SCHED_FIFO` if allowed |
| `tp_plat_sleep(seconds)` | a precise short sleep | `mach_wait_until` | `clock_nanosleep` |
| `tp_plat_parallel_for(n, ctx, fn)` | the compositor's row bands across the cores | `dispatch_apply_f` | serial for now |

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

- **Linux:** an app (SDL2 is the likely choice for the window, sound and input), its build script, and
  `prepare.py` for GOG's Windows installer (InnoSetup: `innoextract`, or a Python reader).
- **Windows:** the same, plus `platform_windows.c` (`QueryPerformanceCounter`, waitable timers, a thread pool)
  and pthreads (MinGW provides them; with MSVC, a shim).
- **The development checks:** the Unicorn oracle in `tests/check_macos.sh` is architecture-neutral in
  principle; the script still names macOS and links the macOS platform file.
- **Scaling:** `tp_plat_parallel_for` is serial on POSIX; a thread pool would bring the compositor's speed up
  to macOS's on large views.
