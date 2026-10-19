# Theme Park native first-park report — 2026-10-16

The project is this repository (`theme-park-macos`), on local
`nightly`. Freedom Force is a pinned, read-only tool reference. No launcher from
another project was opened. No remote, upload, installation or publication was
created. Downloaded originals and game data remain outside git.

## Visible result

Statically compiled ARM64 game code now reaches the first park after title,
nickname input, setup, mouse-driven country selection and buying UK land.
The entrance, toolbar and running date render from the game code. The
native/original park PNG hash is
`ad3273fc886d14342f9b0aa5ebc364f1f448a10c1dcdaafbc65bd0dee59ed470`.
The earlier setup form's hash is
`6aed2e603efbdbcebf07f5846c72c383a56c764ef3dda7126d25a9b04de1d345`.
The main menu's hash is
`71d5e24d8a093a2b089c3265cb2d60e2097996ed27966cd5d4c1b76086536416`.

Local evidence:

- `work/check-macos-park.log`: complete extended macOS check, green.
- `work/check-native/result.json`: 3,627 compiled blocks; 8,000,000 executions;
  13,678 DOS/BIOS and 6,882 scheduling comparisons; matching frames and files.
- `work/check-native/native-frame.png`: verified first park.
- `work/check-native-only/result.json`: identical park, profile bytes and block
  count with `python3 -S`; no Unicorn or Capstone imported.
- `config/milestones.json`: golden hashes only; no game pixels/data in git.

## Fixes justified by failures

- Relocated the image above 1 MiB so its LE object bases cannot collide with VGA.
- Added real segment-selector state and bases; fixed 32-bit segment push values
  and truncation when loading a selector from a 32-bit register.
- Found Capstone 5 drops F2 from both mnemonic and prefix metadata for MOVSD.
  Decode the raw legacy prefixes for these string instructions. The
  synthetic corpus compares zero/nonzero counts, direction, overlap and nonflat
  DS/ES, plus selector load/push/pop, against x86.
- The game patches a one-time allocation call into a jump. Compile observed
  variants ahead of time and guard them against memory bytes. This prevents
  repeated allocations without replacing the game routine or adding an interpreter.
- Reuse the lowest free DOS file handle. Monotonic handles exceeded Watcom's
  table, causing later graphics assets to fail. The regression opens/closes
  the same file 100 times and checks concurrent handles too.
- Added coalescing DOS/DPMI memory arenas, transactional resize failure, scratch
  file overlays, timer IRQ delivery and far/interrupt returns. Unknown services
  and code variants stop with an address and reason.
- Added far mouse callback delivery with saved caller registers/selectors/flags.
  Modal loops do not always poll BIOS keyboard services, so releases scheduled
  only at keyboard polls could never arrive. Added execution quanta and
  independently scheduled mouse input; normalized Unicorn REP iterations to
  each native block's complete string operation and compared counts/state at
  every yield. POPFD now restores IF for DOS code.
- Made scratch profiles unique for every run, rejected overlapping game/scratch
  roots, and compared saved file hashes. The generated setup profile
  `save/TEST.GD` matches with SHA-256
  `efcfd83c68e45211a97a7378aafb3125d78c68676d4f4a86ff37bb9e5a6515b9`.

## Verification and limits

17 synthetic parser/host tests, 124 synthetic instruction comparisons and 2,000
game routine cases pass. Startup compares eight integer registers, segment
selectors and IF/DF; CPU-written pages at each boundary; all 64 MiB periodically
and on completion; port I/O sequences; and framebuffer/palette output. Arbitrary
undefined arithmetic flags are not compared at DOS boundaries. x87 equivalence
has not been established. The library is ARM64 and links only libSystem.

The oracle and native runs share the DOS host. This proves the lifted execution
under that contract, not fidelity of the contract to every DOS4GW/BIOS behavior.
The host is still Python development code; timing is deliberately accelerated
and deterministic. There is no Cocoa window, finished launcher, real-time
input/pacing, build-operation acceptance, full park save/reload validation,
VESA validation, intro playback or audio support yet. The matched file is a
setup profile, not proof of full park-save compatibility.

## Decisions and next work

Used a scratch player named `TEST`, default setup values and UK land solely for
automated testing. These are not saved user preferences. Kept the downloaded installer and
other projects untouched, kept all generated artifacts local/ignored, and made
no publication or installation decision for the user.

Next: native window/event presentation, PIT-rate timing, build operations and
full park save/reload tests, then replace development platform services with the
native application host. Entering a park is not completion of the port.
