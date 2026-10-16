# Theme Park native startup report — 2026-10-16

The project is `/Users/joshuaspeakman/Projects/theme-park-macos`, on local
`nightly`. Freedom Force is a pinned, read-only tool reference. No launcher from
another project was opened. No remote, upload, installation or publication was
created. Downloaded originals and game data remain outside git.

## Visible result

Statically compiled ARM64 game code reaches the original title, nickname entry,
main menu and new-park player-details form. The last screen's native/original
PNG hash is
`6aed2e603efbdbcebf07f5846c72c383a56c764ef3dda7126d25a9b04de1d345`.
The main menu's hash is
`71d5e24d8a093a2b089c3265cb2d60e2097996ed27966cd5d4c1b76086536416`.

Local evidence:

- `work/check-macos.log`: complete standard check, green.
- `work/check-native/result.json`: 12,699 service comparisons through main menu.
- `work/native-newpark/result.json`: 12,818 service comparisons, 2,518 compiled
  blocks, player-details screen and matching pixels/palette.
- `work/native-newpark/native-frame.png`: verified setup screen.
- `work/native-only/result.json`: same screen/hash with `python3 -S`, no Unicorn
  or Capstone imported, only statically compiled ARM64 game execution.

## Fixes justified by failures

- Relocated the image above 1 MiB so its LE object bases cannot collide with VGA.
- Added real segment-selector state and bases; fixed 32-bit segment push values
  and truncation when loading a selector from a 32-bit register.
- Found Capstone 5 drops F2 from both mnemonic and prefix metadata for MOVSD.
  Decode the raw legacy prefixes for these string instructions. A 100-case
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

## Verification and limits

15 synthetic parser/host tests, 100 synthetic instruction comparisons and 2,000
game routine cases pass. Startup compares eight integer registers, segment
selectors and IF/DF; CPU-written pages at each boundary; all 64 MiB periodically
and on completion; port I/O sequences; and framebuffer/palette output. Arbitrary
undefined arithmetic flags are not compared at DOS boundaries. x87 equivalence
has not been established. The library is ARM64 and links only libSystem.

The oracle and native runs share the DOS host. This proves the lifted execution
under that contract, not fidelity of the contract to every DOS4GW/BIOS behavior.
The host is still Python development code; timing is deliberately accelerated
and deterministic. There is no Cocoa window, finished launcher, gameplay/save
acceptance, VESA validation, intro playback or audio support yet.

## Decisions and next work

Used a scratch player named `TEST` and default setup values solely for automated
testing. These are not saved user preferences. Kept the downloaded installer and
other projects untouched, kept all generated artifacts local/ignored, and made
no publication or installation decision for the user.

Next: deliver the registered mouse callback to accept setup, reach a park, test
build/save/reload behavior, and replace test timing/platform services with the
native application host. A rendered setup screen is not completion of the port.
