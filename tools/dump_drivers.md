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

Images are dumped from a C-host checkpoint at first entry
(`TP_DUMP_ON_STOP=x.ckpt` on the headless runner, then
`tools/extract_region.py x.ckpt ADDR SIZE out.bin`). `<image>.mask` marks with
0 the bytes the driver rewrites after loading (pre/post dumps differ); those
are excluded from the identity check, and blocks covering them are re-checked
on every execution. The mixer writes 32 identical 350-byte copies of one
generated routine at body offset 0x1cd1; its image has all 32 slots filled.
A different load address or generated routine stops the game with an explicit
message instead of running mismatched code. All images are game-derived and
stay in work/.
