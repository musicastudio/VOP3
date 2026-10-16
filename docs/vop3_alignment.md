# VOP3 <-> MEG alignment, first pass (2026-09-30)

Inputs: PLG150-AN VOP3 AN program `docs/vop3_an/mode0.bin` (230 steps, 0x004..0x0E9, from
`tools/extract_plg150an_vop3.py`) and the EX5 MEG AN program at EX5_TG1.bin 0x3C97D4 (254 steps,
`tools/meg_disasm.py`). Same synthesis engine, two chips.

## PLG150-AN driver facts (Ghidra project PLG150AN, H8/3002 at h8s:BE:32:advanced)

- VOP3 register window at 0x400000, 16-bit registers at `base + 2*reg`: same register numbers as the
  FS1R's `FUN_0000B5E2` (`docs/vop3_microcode.md`). The FS1R's VOP3-1 upload is therefore the same
  format; this is not a different chip mode.
- `FUN_0007fd1a` clears 0x200 steps (regs 0xC, 0xB, 10..6), 0x80 slot entries (regs 0x1A, 0x1C) and
  15 bus levels (reg 0x2C).
- `FUN_0007f87c(image, base, flag)` uploads `u16 first, u16 count, count x 8 words` (order
  r0C, slot, r0B, r10, r9, r8, r7, r6). When `r10 & 7 != 0` the step also writes reg 0x1A =
  `(s8)slot_lo >> 1` and reg 0x1C = `slot & 0x1FF` at register-0 address `step >> 2`.
  **So `r10 & 7` marks a delay-memory access and the memory slot is `step / 4`.** On the FS1R
  VOP3-1 `r10` is 0 in all 512 steps (no DRAM on IC31), consistent.
- Images: `0xBB9F0` base (57 steps + bus levels), `0xBDA12/0xBE894/0xBF716` = voice modes 0/1/2
  (identical shape), `0xBB16E` boot blank; `FUN_0007f9e6` patches from lists at `0xC0598`, `0xC19D2`.

## Structural match with the MEG program

| | MEG (EX5) | VOP3 (PLG150-AN mode0) |
|---|---|---|
| memory-access granularity | every 3rd step (pc % 3 == 0) | every 4th step (`step >> 2` is the slot) |
| memory ops | 43 | 45 |
| dense read run at the start | 24 reads, pc 0x00..0x45 | 18 slots, steps 0x11..0x55, then sparser |
| writes at the end | 11 writes, pc 0x9F..0xE4 | 15 slots with slot word `0x2020`, steps 0x97..0xDC |
| read variants | `mem_r`, `mem_1r`, `@` absolute | slot words `0x0000`, `0x1010`, `0x1020`, `0x1C20` |
| per-step byte table | 8 RAM mappings, selected by pc/12 | reg 0xC byte, values 0..7 (45 non-zero) |

The tail of both programs is the same shape: a block of writes back to delay memory after the voice
mixer. In the MEG listing those writes (0x9F..0xE4) sit between the two 5-tap filter sections
(0x8C..0x95 and 0xBB..0xC4) and the output stage (0xFA..0xFD); expect the VOP3 filter at
~0x8C..0xC0 too.

## Bit-field statistics (mode0 vs FS1R VOP3-1 program_0)

Per-register bit-population counts line up: r10 is 0/2 on the AN, 0 on the FS1R; r8 low byte is dead
on the FS1R (0 in 512 steps) and live on the AN; r7 bits 11 and 5 are 0 in both; r6 bit 15 is
set in 90% of steps on both (an "instruction valid" or "MAC enable" bit is the obvious reading).

## Next

1. Decode `r10/r9/r8/r7/r6` field by field against MEG semantics: start from the 15 write slots
   (0x97..0xDC, slot word 0x2020) and the MEG's `mem_w` steps, which pair 1:1 by order.
2. `r0B` (coefficient) is 0 in most AN steps but the MEG uses a constant almost everywhere -> the VOP3
   constant must live inside r9..r6 (immediate field). Look for 1.15 values like 0x4000, 0x7FFF,
   0xC000 in r6 (present: `4000`, `8003`, `803F`, `BFDA`).
3. Diff `mode0/1/2`: the steps that change are the voice-type-dependent ones (poly/layer/AN+FDSP).

Mode images: mode1 differs from mode0 in 155 of 230 steps, mode2 in 229 — they are separate
programs (re-scheduled), not patches; align each to the MEG independently, mode0 first.
