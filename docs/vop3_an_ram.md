# The VOP3's external RAM on the AN boards, and the AN1x mask ROM

The PLG150-AN hangs a DRAM off its VOP3 and the AN1x hangs a 1 Mbit mask ROM off the same pins. This note reads what the PLG150-AN's H8 writes into its RAM, what the two voice programs read out of it, and rebuilds the AN1x mask ROM from both. `python tools/vop3_an_ram.py` prints every number below and asserts the checks; `--probe` runs both programs through `yss236_core` (`tools/vop3_an_ram.cpp`). The tables and the mask ROM are read from firmware; how the chip addresses them was measured on rgwan's FS1R in session 40 (below), with `--check` replaying those takes.

## What the PLG150-AN uploads

`FUN_00081358`, called once from the boot sequence `FUN_00080a3a` after the microcode is in, writes 0 to VOP3 registers 0x0F and 0x10 (the RAM address, high and low), then for each little-endian word of the 0xC242 bytes at ROM 0xC245C writes 0 to register 0x12 and the word to register 0x11. The address auto-increments, so RAM words 0x0000..0x6120 hold the image (`docs/vop3_an/ram.bin`, as the chip receives it). The H8 drivers name registers by byte offset (base + 2 x reg, as `FUN_0000B5E2` on the FS1R), so the decompilation shows these as `| 0x1e`, `| 0x20`, `| 0x22`, `| 0x24`. No other PLG150-AN function writes registers 0x0F..0x12, and no AN1x function does at all. It is a one-time table load: nothing writes the RAM afterwards, from the CPU or from the program.

The image is nine tables, word offsets:

- `0000..0FFF` exp, `-16384 * 2^(i/4096)`, one octave
- `1000..101F` powers of two, -1 .. -32768, each twice
- `1020..181F` decay, `32767 * 2^(-i/256)` over eight octaves
- `1820..281F` sine, one cycle of 4096
- `2820..381F` the same sine again
- `3820..481F` an odd periodic shape, four periods of 1024, peak 1552
- `4820..5820` a soft clipper, +-18022 over 4097 words, odd about 5020
- `5821..5929` zeros, then 1 .. 16384 in powers of two, then a run of 32767
- `592A..6120` a reciprocal, 29127 falling to 128

## What the programs read

The RAM is addressed per delay slot, as on the FS1R's VOP3-2: slot = step >> 2, and the slot's offset is 9 + 9 bits in registers 0x0D/0x0E. The PLG150-AN uploader `FUN_0007f87c` writes `w1 >> 9` to 0x0D and `w1 & 0x1FF` to 0x0E, so the 18-bit offset is `w1` itself; the AN1x uploader `FUN_00056028` writes its slot pair to 0x0D then 0x0E. Both appear as `| 0x1a` / `| 0x1c` in the decompilations, which is why the docs called them registers 0x1A/0x1C until now: they are the FS1R's 0x0D/0x0E. The same reading settles `docs/vop3_outputs.md`'s open question: the AN drivers' 15-word table at `| 0x2c` is register 0x16, the FS1R's, not a second register. `tools/extract_an1x_vop3.py` paired the AN1x's slot words wrongly until this note (it took one half by step parity); `docs/vop3_an1x/voice.bin` is regenerated with the 18-bit offset in the slot column, in the PLG150-AN's packing.

With the patch lists of `FUN_0007f9e6` applied (0xC0598 / 0xC0C56 / 0xC1314 per mode on steps 105..162, 0xC19D2 on 163..1F6):

- PLG150-AN mode 0: 95 reads, 0 writes, offsets 00000 01010 01020 01C20 02020 05020
- PLG150-AN mode 1: 85 reads, 0 writes, offsets 00000 01010 01C20 02020 04020 05020 05921
- PLG150-AN mode 2: 85 reads, 0 writes, offsets 00000 01010 02020 02420 02576 025BA 02728 02820 02980 05020 058A1
- AN1x voice: 95 reads, 0 writes, offsets 00000 01010 01020 03420 03820 04820

Every offset lands inside one of the tables above, mostly at its centre (02020 is the sine's half-cycle, 05020 the clipper's zero, 01010 the middle of the pow2 pairs), which is where a signed index into a lookup belongs. No AN program has a single `mem 1` write. So on these boards the "delay memory" is a table ROM, which is why the AN1x can wire a mask ROM where the PLG150-AN has DRAM.

**Every RAM access on both AN boards sets `r8` bit 5 (`w2[5]`, `tbl` in `yss236_core.h`), and no access in any FS1R program does** (FS1R VOP3-2 marks carry `r8` low bits 0x00, 0x01, 0x03, 0x08, 0x40, 0x44, 0x48, 0x4C). Session 40 measured what it does: the access, read or write, addresses the RAM at the slot's offset without the ring pointer (next section).

## The AN1x mask ROM

PLG150-AN mode 0 and the AN1x voice are the same voice program with the same step count and the same read pattern: six offsets each, and the readers pair one to one.

- 00000 <-> 00000, 20 / 20 readers (exp)
- 01010 <-> 01010, 20 / 20 (pow2)
- 01020 <-> 01020, 5 / 5 (decay)
- 01C20 <-> 03420, 5 / 5 (sine)
- 02020 <-> 03820, 25 / 25 (sine)
- 05020 <-> 04820, 20 / 20, the same twenty steps (clipper)

Where the offset is the same, the table is at the same place; where it moved, the AN1x layout is shifted. The AN1x CPU ROM settles which: a big-endian copy of the tables sits at **0xE496C**, referenced from code at 0x6CD4C, and against the PLG150-AN image it holds

- exp at +0000, 4096 of 4096 words equal
- pow2 at +1000, the same sixteen powers, each once instead of twice
- decay at +1010, 0x2000 words at `2^(-i/1024)`: the PLG150-AN's table is exactly every fourth word of it (2048 of 2048)
- sine at +3010, 4096 of 4096 equal
- clipper at +4010, 4097 of 4097 equal
- a second clipper at +5011, +-4506, which the PLG150-AN does not carry

Laid in at the offsets the AN1x reads, it fits: the AN1x reads the sine at 3420 and 3820 where the CPU copy's sine starts at 3010 + 0x10 = 3020 (PLG: 1C20 and 2020 against a start of 1820, the same 0x400 and 0x800 in), and the clipper at 4820 where the copy's clipper centre is 4020 + 0x800. So `docs/vop3_an1x/maskrom.bin` (64 Ki words, little-endian, as `ram.bin`) is:

- `0000..0FFF` exp (PLG = CPU copy)
- `1000..101F` pow2 doubled, from the PLG image, read at 1010. The CPU copy has them single, so the mask ROM's own layout here is inferred from the PLG150-AN, and the copy's 0x10 shift of everything after is consistent with the doubled pairs taking 0x20
- `1020..301F` decay at the AN1x's 4x resolution, from the CPU copy
- `3020..401F` sine
- `4020..5020` clipper +-18022
- `5021..6021` clipper +-4506, the next table in the CPU copy. No AN1x step reads it, so its place was inferred, wrongly (below)
- `6022..FFFF` unknown, zero at the time of writing

**Checked against the ROM.** rgwan dumped the AN1x's VOP3 mask ROM (FS1R.unlock `dmps/an1x_vop3.bin`, 2026-10-15, now `docs/vop3_an1x/maskrom.bin`, 64 Ki words little-endian). Against it the rebuild is **word for word equal over 0000..5020**, every table the AN1x voice reads, the doubled pow2 included (`tools/vop3_an_ram.py` asserts it). The one guess was wrong: from 5021 the ROM holds eleven more soft clippers, 4097 words each, the same shape as the +-18022 one at 4020 with peaks 18432, 18842, 19251, 19661, 20480, 21299, 22938, 17613, 17203, 16794, then 0xFFFF fill from F02C. The CPU copy's +-4506 clipper is not in the ROM. No AN1x step reads those clippers at the shipped offsets, so a drive or character parameter must move a slot offset onto them at run time; which parameter is open.

## Session 40: the rules, measured on the FS1R

Seven takes on rgwan's FS1R over the s25 runner (`FS1R.unlock/captures/2026-10-15-*-s25`, probe lists `docs/vop3_ship/40a..40g.probes.json`, all generated by `tools/vop3_an_ram.py --rig a..g`). The rig is Hall1 under the s36 click: L taps the input r[0d], R taps d[2c], a free cell that step 008 captures. As shipped, 008 captures step 003's read of slot 0 (offset 29998), which hears step 037's write to slot 0x0d (offset 28652), so R is the click 1346 passes late. `--score <dir>` prints R's bursts after the click. All seven are read off the digital output to the sample.

**`r8[5]` drops the ring pointer, on reads and writes.** (40a, 40b, 40d)

- `rd5` (read 003 with bit 5): silent. `wr5` (write 037 with bit 5): R arrives 6425 late, not 1346. `same` (slot 0's offset set equal to slot 0x0d's, both on the ring): R arrives 5079 late. So a ring read at the writer's own offset is not "now", and neither half alone gives an echo at the expected lag.
- `same5` / `abs60` / `abs20` / `c60ph3` (equal offsets, bit 5 on both): **R arrives with L, 0 passes late**, 1513 samples long, the whole click. Both steps address word 28652 directly.
- `abs60_off1` (read one word above the write): silent. The address is exact, not a region.
- `abs28` and `abs68` (the AN boards' own mark bytes 0x28 and 0x68): R with L, as `abs60`. Bits 6 and 3 do not matter here; bit 5 does.
- `b6clr` (shipped read with `r8` low 0x00 instead of 0x40): as shipped. Bit 6 is not an enable.

**RAM marks transfer on every phase; steps 000..002 transfer nothing.** (40a-40d, 40f, 40g)

- 40f moves Hall1's own phase-1 read at 0c9 (slots 0x31/0x32 set to offset 29998, capture at 0ce) to 0c8 (phase 0), 0c7 (phase 3), 0c6 (phase 2) and 0c5 (phase 1): **all five land at 1346**, the shipped echo. With no read, the capture holds and R comes 18442 later from another path. So s16's "only phase 3" and s17's "and phase 1" were the capture window below, not a phase rule.
- 40a/b/d move the slot-0 read word to steps 000, 001 and 002: silent on all three phases, with the bare class-0 read word, with the shipped word and as an absolute read. The same word on 003 works. So the first three steps of the program make no transfer.
- 40g, on a shipped program: insertion program 13 (performance 70's type 26) carries phase-0 reads at 1a4, 1a8, 1ac, 1b0. Clearing 1a4's mark, or all four, **silences the insertion**; clearing 1b0's leaves it as shipped. A phase-0 read in a shipped FS1R program is live.

**A capture takes a transfer 5..11 steps back, not 3..11.** (40c, 40d)

Capturing at 004, 005, 006 or 007 after the read at 003 (1, 2, 3, 4 steps) holds; at 009 and 00a (6, 7) it takes 003's read at 1346; at 00c, 00d, 00e (9, 10, 11) it takes 007's slot-1 read instead (2066). The s36k window of 3..11 never saw a capture 3 or 4 steps after its read on the shipped programs, so it was not tested at that end.

**Register 5.** Both AN boards write register 5 = 2 and register 0x1B = 0xFC00 at boot (PLG150-AN `FUN_00080a3a`, AN1x code at 0x56B98); the FS1R writes register 5 = 0x1FF4 (`FUN_0003a064`). 40e wrote register 5 = 2 through the firmware's `FUN_00039648` on the FS1R: the chip's output went to digital zero for every probe until 0x1FF4 was written back. So register 5 is a chip-wide configuration word whose AN value does not run the FS1R's program; what its bits select is open, and it is the most likely place a RAM width or table mode is set.

**All 49 timed probes.** `python tools/vop3_an_ram.py --check` replays every probe of 40a-40d and 40f through `tools/vop3_interp.py`: 46 of 49 match the unit's first R burst to 2 samples under the new rules, against 27 of 49 under the old. The three misses are `wr5`, `same` and `f_none`, the probes whose answer depends on a read at the writer's own ring offset (6425 and 5079 late) and on another path's tail (18442); none of them is an AN-board access.

## The rules in the model

`tools/vop3_interp.py` (`TABLE`, `PHASES`, `XFER_FROM`, `CAPMIN`) and `src/yss236_core.h` with its compiled pass (`addr`, `XFER_FROM`, `CAPMIN`) carry all three, unconditionally. The checks:

- `vop3_interp.py` self-check 398 / 398, `vop3_ship.py` 26k 18 / 18 and 27a 23 / 23, as before. The s36k tap replay (8 shipped steps tapped on the click, `2026-10-02-093836-s25`) gives the same error on every segment under both rule sets.
- `tools/vop3_disasm.py` prints `table[n]` for an `r8[5]` access, `mem(step < 3: no xfer)` for the dead first steps, and a capture's source as the step range 5..11 back; the listings in `docs/vop3_disasm/` are regenerated. `docs/vop3_2_params.md` quotes older listing text and was not regenerated (its generator writes it unchanged).
- `vop3_jit_check`: 229,600 passes, compiled and interpreted identical.
- `tools/vop3_an_ram.py --probe` through the C++ core: PLG150-AN mode 0 and the AN1x voice, 456,000 reads each, **100% inside the table image, every offset transferred**, including the soft clipper the old rule never read.
- The whole engine, the 384 factory performances rendered 3 s each from the tree before and after: 379 bit-identical; the five that move (29 Power Key, 70 Relaxxx, 88 Metallic, 94 Hard Pulse, 298 Power Key) all load insertion program 13, whose phase-0 reads at 1a4..1b0 the old rule dropped and 40g shows are live. They differ by -13 to -19 dB of peak from the first 0.3 s on, with rms within 4%.

The probe drives the serial inputs, not the hardware inputs the AN voice reads its pitch from, so every index stayed at 0 or 1: how far the index moves within a table is still unmeasured.

## What is still open

- Which AN1x parameter moves a slot offset onto the clippers at 5021..F02A (the dump has them, no shipped offset reads them).
- Register 5's bits, and register 0x1B, which only the AN boards write.
- A read at the writer's own ring offset (`wr5`, `same`): 6425 and 5079 passes, not 0 and not 2^18.
