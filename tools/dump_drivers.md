# HMI sound driver images

The game copies HMI SOS drivers from `HMIMDRV.386`, `HMIDET.386` and
`HMIDRV.386` into DOS memory at run time and calls them. They are compiled
ahead of time like the rest of the game (`tools/build_game.py --loaded-code`),
at the addresses where the game deterministically loads them with GOG's
`sndsetup.inf` (SB16 220h/IRQ 7/DMA 1, General MIDI 330h):

| File in work/ | Load address | Entered | Driver |
|---|---|---|---|
| drv-40034-flat-mpu.bin | 0x40034 | flat | mpu401.com (General MIDI) |
| drv-402c0-flat-det.bin | 0x402c0 | flat | detection driver from HMIDET.386 |
| drv-402c0-{flat,cs}-dig.bin | 0x402c0 | flat init, then own selector | sw1616m.com (SB16 16-bit) |
| drv-409a8-{flat,cs}-mix.bin | 0x409a8 | flat init, then own selector | tr16m.com (software mixer) |

`tools/make_drivers.py` makes the images from the owned package; `tools/build_app.sh` runs it on the first
build (a few minutes; `TP_NO_SOUND=1` skips it):

1. Each driver is cut from its `.386` file (offsets in the tool; the cut bytes are checked against
   `config/drivers.json`, which holds hashes only). The MIDI, detection and digital drivers run as they are.
2. The game is built with the drivers fully guarded and run headless with sound on through the scripted
   start into the first park (`tests/check_macos.sh --park`'s input). The first block whose bytes changed stops
   the run (`TP_DUMP_ON_STOP`); the changed bytes are taken into the images, and the run repeats until it
   finishes without a stop (five rounds with GOG's package: the mixer patches its own instructions, then
   generates its voice routine).
3. The mixer writes one generated routine into each of its 32 voice slots (350 bytes from body offset
   0x1cd1) as voices come into use, the same routine in every slot. The run uses two; the tool checks they
   match and fills all 32 with it.
4. `<image>.mask` marks with 0 the bytes that differ from the file (the bytes a driver rewrites). They are
   excluded from the identity check (the first 32 bytes), and blocks covering them are re-checked on every
   execution.

Checked against images dumped by hand from long play: the same bytes in every byte the build compiles; they
differ only in 36 masked data bytes (sample pointers and volumes).
A different load address or generated routine stops the game with an explicit
message instead of running mismatched code. All images are game-derived and
stay in work/.
