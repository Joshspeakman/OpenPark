# Development DOS contract

This host runs the owned DOS/4GW executable under a synthetic, deterministic
DOS environment. It is shared by the x86 oracle and ARM64 tests, so agreement
between them verifies lifted CPU behavior, not independent DOS compatibility.

The three LE objects are relocated together by +0x100000, including the entry,
stack and internal fixups. This keeps the VGA aperture at 0xa0000 separate from
the file's object addresses. The 64 MiB test address space contains a GDT at
0x1000, PSP at 0x2000, environment at 0x3000, conventional allocations between
0x40000 and 0x90000, and linear allocations from 0x400000. The code/data selectors
are flat; PSP and environment selectors have their own bases. A synthetic IRET
stub at 0xf000 supplies the default timer chain target.

Startup follows Watcom's DOS4G branch: DOS version, INT21/FF00 identification,
ES pointing to the PSP, and PSP:2c containing the environment selector. The test
environment sets PATH to C:\\GAME and NO87=1 for Watcom's no-coprocessor startup
configuration; x87 compatibility is not claimed. These
choices follow the observed game startup and the corresponding
[Open Watcom startup implementation](https://github.com/open-watcom/open-watcom-v2/blob/master/bld/clib/startup/a/cstrt386.asm).
They are not a byte-for-byte recreation of DOS4GW's process bootstrap.

Implemented services are those reached by startup: DOS file open/close/read,
seek, attributes, create/write, console output, handle information, date/time,
vectors and conventional memory; DPMI linear allocation/free/resize,
conventional allocation/free and selector-base queries; mouse registration,
ranges, position and far callback delivery; BIOS keyboard polling; VGA mode 13h,
DAC palette and mask;
PIC acknowledgement and recorded PIT programming. Unknown operations stop.
The optional mouse hardware-reset extension explicitly remains unavailable
(unchanged AX), matching the unsupported-extension behavior in
[CuteMouse's dispatcher](https://github.com/FDOS/mouse/blob/master/ctmouse.asm).
No audio driver is loaded or pretended to work.

Memory arenas align allocations and coalesce released spans. Failed resizes
preserve the old allocation; growing in place does not relocate into an earlier
hole. DOS file handles reuse the lowest available slot, which is essential for
Watcom's small file table. DOS paths are case-insensitive within the game root;
parent traversal and escaping symlinks fail. Writable opens copy an original
file into the scratch overlay. Creates and writes only target the overlay.
Game and scratch roots may not overlap. Each harness run creates new scratch
directories; verification also compares hashes of the resulting files.

Time is deliberately synthetic for reproducible tests. Keyboard scripts deliver
scan/ASCII pairs at numbered polls. One timer IRQ is delivered at each idle BIOS
keyboard poll after installation, with the real guest IRQ handler and IRET/far
return executing. Optional 1,000-block quanta provide scheduling points even
inside loops that do not call DOS. Unicorn's per-iteration REP blocks are
normalized to the complete string operation executed by the compiled block.
Both machines compare block counts and state at those points. Scripted clicks
release after 50,000 blocks, independently of keyboard polling. Additional
clicks can be scheduled at absolute block counts for modal dialogs. The guest
mouse handler runs through a far-call trampoline that restores the caller's
registers, selectors and flags. Synthetic CPU tests cover that restoration.

This is deterministic test timing, not accurate real-time pacing. PIT-rate
scheduling, native event capture/presentation, VESA, audio and a production
native platform host remain integration work.

The DPMI memory API was checked against the
[DPMI specification hosted by DJGPP](https://www.delorie.com/djgpp/doc/dpmi/api/310501.html).
Primary source downloads are kept under ignored work/reference; none are
copied into project sources.
