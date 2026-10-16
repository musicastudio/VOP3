# VOP3 (Yamaha YSS236) reference

The YSS236-F (XT013A00) "VOP3" as a programmer sees it: the host registers, the step word, the arithmetic, the memory, the I/O, the LFOs and the EGs. `src/yss236_core.h` implements this page and `src/vop3.cpp` / `src/vop3_regs.h` the register side; where this page and the code disagree, the code is what was checked against the unit.

Each rule rests on firmware (**FW**, the FS1R, PLG150-AN and AN1x drivers) or on measurement of a real FS1R (**CHIP**). The register table marks which. How each rule was found, and what was withdrawn along the way, is in [vop3_research_journal.md](vop3_research_journal.md); appendix B lists the firmware routines behind each register.

Related: [vop3_outputs.md](vop3_outputs.md) (the FS1R's VOP3-2 mixer and output cells), [vop3_an_ram.md](vop3_an_ram.md) (the AN boards' table RAM and the AN1x mask ROM), `docs/vop3_disasm/` (every program image disassembled).

## 1. Overview

The YSS236 is one part on four boards: the FS1R's effect DSP (VOP3-2, with a 2^18-word DRAM) and filter (VOP3-1, no DRAM), the AN1x's synthesis engine (a 1 Mbit mask ROM on the RAM pins) and the PLG150-AN's (a DRAM the H8 fills with tables at boot). The same rules decode every image from all four.

* A program is **512 steps**, run once per sample: 24.576 MHz / 512 = 48 kHz.
* A step is **five 16-bit words**, a 16-bit **constant** and a **tag byte**.
* Nothing runs outside the steps. Every level, pan, send, delay time and filter coefficient is a step constant or a delay slot offset, so a host changes sound by writing constants and offsets, not by rewriting the program.
* State: the running value `p`, 128 r registers, 64 m cells, 16 t registers (multiplier gains), 128 delay slots into the delay RAM, 16 LFOs, 16 EGs, two banks of 16 serial input slots and 16 serial output slots.

## 2. Host registers

The host writes 16-bit words at `base + 2 * reg`. Decompiled H8 drivers (the AN boards) show the byte offset, so their `0x1a`, `0x1c`, `0x2c` are registers 0x0D, 0x0E, 0x16.

**The select latch.** Most registers act on one of several items (a step, a slot, an LFO, an EG, a table entry), chosen by the last write to `SELECT`. With bit 15 (`AUTO_INCREMENT`) set, each following write advances the step by one.

**Reads.** The register file is write-only. Reads return bus noise, except `EG_STATUS` (0x21) and, on the PLG150-AN, 0x22.

| reg | name | indexed by | a write | evidence |
|---|---|---|---|---|
| 0x00 | `SELECT` | | the step, slot, LFO, EG or table entry the next indexed write goes to; bit 15 auto-increments | FW, CHIP |
| 0x01 | | | upload control, written around an upload; the bits are unknown | FW |
| 0x05 | | | chip configuration, written once at boot (FS1R 0x1FF4, AN boards 2). Writing 2 on a running FS1R silences its program | FW, CHIP |
| 0x06..0x0A | `PROGRAM - k` | step | word k of the step: 0x0A is `w0`, 0x09 `w1`, 0x08 `w2`, 0x07 `w3`, 0x06 `w4` | FW, CHIP |
| 0x0B | `COEF` | step | the step's constant | FW, CHIP |
| 0x0C | `TAG` | step | the step's tag byte. Firmware uses it as a write-group id (AN1x 3..7 = voice 0..4, 1 = global); the chip's use of it is unknown | FW |
| 0x0D | `SLOT_HIGH` | slot | delay slot offset, bits 17..9 | FW, CHIP |
| 0x0E | `SLOT_LOW` | slot | delay slot offset, bits 8..0. The offset is `SLOT_HIGH << 9 \| SLOT_LOW` | FW, CHIP |
| 0x0F | | | external RAM write address, high part | FW |
| 0x10 | | | external RAM write address, low part | FW |
| 0x11 | | | writes one word at the RAM address, which then advances by one | FW |
| 0x12 | | | written 0 before each RAM data word; meaning unknown | FW |
| 0x14 | `TRIGGER` | | the constant loads of every gated block land at once (section 6) | FW |
| 0x16 | `CONFIG` | entry, `SELECT` = i + 1 | a 15-entry table. Entry 0 is a control word with bit 6 set while microcode is rewritten (VOP3-2 0x40 during, 0x42 after; VOP3-1 and the AN boards 0x17). Entries 1..14 are static and unknown | FW |
| 0x17 | `UPLOADING` | | 0x23 while a program is rewritten, 0 after | FW |
| 0x1B | | | 0xFC00 at boot on the AN boards; the FS1R never writes it | FW |
| 0x1C | | | written 0 after `LFO_RESET` | FW |
| 0x20 | `LFO_RESET` | mask | each set bit n returns LFO n to its start phase | FW |
| 0x21 | `EG_STATUS` | | write: the EGs allowed to report. Read: `0x10 \| n` for the lowest enabled EG n whose segment ended, 0 if none | FW |
| 0x22 | | | read and written by the PLG150-AN's EG driver only | FW |
| 0x24 | `LFO_SPEED` | LFO | speed, 10 bits, in cycles per sample x 2^20. Hosts preserve bits 10..11 | FW, CHIP |
| 0x25 | `LFO_WAVE` | LFO | bits 7..5 the wave (0 triangle, 1 saw down, 2 saw up, 3 square, 4 sine, 5 sample and hold); bits 4..0 the start phase in 32nds of a cycle; bit 12 bipolar (section 7) | FW, CHIP |
| 0x26 | `LFO_DEPTH` | LFO | amplitude, unsigned 1.15 | FW, CHIP |
| 0x27 | `LFO_DIRECTION` | LFO | offset and scale of the wave (section 7) | FW, CHIP |
| 0x28..0x2A | | entry | 15 entries each, written once at boot (0x15, 0x15, 0 on the FS1R) | FW |
| 0x2B | `EG_RATE` | EG | segment rate: 0 jumps, 255 holds. Writing it acknowledges the segment | FW, CHIP |
| 0x2C | `EG_TARGET` | EG | the level at which the segment ends; arms it | FW |
| 0x2D | `EG_AIM` | EG | the level the segment heads for | FW |

No firmware writes 0x02..0x04, 0x13, 0x15, 0x18..0x1A, 0x1D..0x1F, 0x23 or 0x2E and up. The FS1R's VOP3-2 never writes 0x14, 0x21, 0x22 or 0x2B..0x2D.

The core models `SELECT`, the program, constant, tag and slot registers, `TRIGGER`, the LFOs and the EGs. It ignores the rest: the configuration registers, the upload bracket and host access to the external RAM.

### Uploading a program

The FS1R's sequence, of which only the bracket order matters:

1. `CONFIG` entry 0 = 0x40, `UPLOADING` = 0x23.
2. Per step: `SELECT` = step | 0x8000, then the tag, the five words and the constant.
3. The block's slot offsets, then its LFOs' four registers each.
4. `LFO_RESET` with the block's LFO mask, then 0x1C = 0.
5. `CONFIG` entry 0 back to its running value, `UPLOADING` = 0.

### Writing a constant

`SELECT` = step, then `COEF` = value. Both writes must be atomic with respect to any other writer: the FS1R updates constants from a 192 Hz tick through the same latch, and a write that interleaves with it lands on the wrong step.

### External RAM from the host

Write-only, through 0x0F..0x12, and only the PLG150-AN uses it (one table load at boot, `vop3_an_ram.md`). The FS1R's CPU never writes its VOP3-2 DRAM; the programs fill it.

## 3. The step word

Word k is written to register 0x0A - k. The field names are `yss236_core::op`'s.

| word | bits | field | meaning |
|---|---|---|---|
| w0 | 2:0 | `memop` | delay RAM access: 0 none, 1 write, 2/3 read, 4/6 read one word older |
| w1 | 6:0 | `dr` | r register written, 0 for none |
| | 7 | `rcopy` | `dr` takes the previous step's r operand (a z^-1) instead of the result |
| | 11:8 | `tn` | t register written |
| | 12:11 | `tw` | t write: bit 1 the result, bit 0 alone the step's constant |
| | 12:8 | `dwr` | nonzero on a class-1 step: it also writes its m cell |
| | 13 | `wdata` | the result becomes the data of the next delay RAM write |
| | 14 | `tfrac` | t takes the fraction of the result below one delay sample |
| | 15 | `ctl` | control step (section 6) |
| w2 | 0 | `cmp` | the result sets the compare flag |
| | 2 | `idxw` | the result x 256, floored, becomes the delay index, in effect 3 steps later |
| | 3 | `idxa` | add the delay index to this step's RAM address |
| | 5 | `tbl` | the RAM access ignores the ring pointer (a table read or write) |
| | 12:7 | `dm` | m cell written, 0 for none |
| | 15:13 | `msrc` | what the m cell takes: 0 serial bank 1, 1 serial bank 2, 2 LFO, 3 EG level, 4/5 RAM capture, 6 the result, 7 the previous step's m operand |
| w3 | 2:0 | `t` | t register read; `m1` bit 0 selects the upper eight |
| | 4:3 | `m1` | multiplier input 1: 0/1 the constant, 2/3 a t register |
| | 7:6 | `asel` | adder input: 0 the running value p, 1 r[sr], 2 m[sm], 3 none |
| | 8 | `m2m` | multiplier input 2: 1 m[sm], 0 r[sr] |
| | 10 | `sub` | subtract the adder input instead of adding it |
| | 13:12 | `shift` | scale the result by 1, 2, 4 or 16 |
| | 15:14 | `clamp` | result mode: 0 wrap, 1 saturate, 2 negatives to 0, 3 absolute value |
| w4 | 5:0 | `sm` | m cell read |
| | 6 | `sout` | m[sm] goes to the serial output slot of this step's page |
| | 13:7 | `sr` | r register read, 0 for none |
| | 15:14 | `mmode` | the step's class: 0 no product, 1 constant load, 2 product, 3 unity product |

Bits 8:6 of w3 together are the disassembler's `op`: `m2m << 2 | asel`. Op 0 is `p + k*r`, 1 `r + k*r`, 2 `m + k*r`, 3 `k*r`, 4 `p + k*m`, 5 `r + k*m`, 6 `m + k*m`, 7 `k*m`.

## 4. Arithmetic

Values are fixed point. The core holds them as doubles at these scales:

| quantity | range |
|---|---|
| r registers, m cells | [-128, 128), the word / 256 |
| running value p | [-256, 256) |
| delay RAM word | clipped at +-32 |
| class-1 constant | `v / 256` (8.8) |
| class-2/3 constant | signed 1.15 gain |
| DAC word | `floor(value / 4)` to 18 bits, clipped at +-8 |

**Classes**, with `a` the adder input (negated when `sub` is set) and `s = 2^shift`:

* **Class 0**: no product. When any of `asel`, `shift`, `clamp` or `sub` is set, `y = a * s`; otherwise the step only moves data.
* **Class 1**: constant load, `y = (k * 128 + a) * s`.
* **Class 2**: `y = (g * m2 + a) * s`, with `g` the constant (or `t[t]` when `m1` >= 2) and `m2` = m[sm] or r[sr].
* **Class 3**: as class 2 with `g` = 1.

**Result.** `clamp` shapes y: mode 0 wraps at the register range (p at its own +-256), modes 1..3 saturate at +-128. y becomes p and, when `dr` is set, is written to r[dr].

**Latency.** A write lands 3 steps after the step that issues it: an r register written at step n still reads the old value at n + 1 and n + 2. m cell writes, captures and t registers land the same way. Programs depend on this, so a host model must keep it.

**z^-1.** With `rcopy`, r[dr] takes the previous step's r operand. The biquads keep their state this way.

## 5. Memory and I/O

**Delay RAM.** 2^18 words in one ring whose pointer steps back once per pass. There are 128 slots, and a step's slot is `step >> 2`. The address is `pointer + offset[slot]`, plus 1 for an "older" read, plus the delay index when `idxa` is set. Steps 000..002 never transfer.

* A **read** pushes the word into the pass's transfer history.
* A **write** stores the value latched by the last `wdata` step, if one is pending, else the step's own result.
* **`tbl`** drops the pointer, so the access addresses `offset[slot]` (plus the index) directly. Every AN board access sets it; that is how their programs read lookup tables.

**Captures.** An m cell written with `msrc` 4 or 5 takes the latest RAM read made 5 to 11 steps earlier in the same pass, and keeps its value if there was none.

**Inputs.** `msrc` 0, 1 and 3 move serial input bank 1, bank 2 or the EG level of the step's slot into the m cell. A slot is a 64-step page and its half, so `in[source][page * 2 + half]`. On the FS1R's VOP3-2 the effect sends also arrive in r registers the program reads in place: reverb r[0d]/r[0e], variation r[0f]/r[10], insertion r[11]/r[12].

**Outputs.** `sout` puts m[sm] on the serial output slot of the step's page. On the FS1R the main stereo pair is m cells 0x10 / 0x11 and the individual pair 0x28 / 0x29 (`vop3_outputs.md`).

## 6. Control steps

A step with `w1[15]` set computes nothing; it decides which of the following steps run, up to the target step `w4[15:8]` within the same half of the program.

* **Gate**, `w3[3:0]` = 8: the steps up to the target are skipped until the host writes `TRIGGER`, which lands the constant loads of every gated block at once. VOP3-1 uses this to load a voice's filter constants in one go.
* **Conditional skip**, `w3[3:0]` = 0xD, on the compare flag set by the last `cmp` step: `w3[6:4]` = 6 runs the block only if the flag is positive, 4 only if negative, 1 never (a jump).

## 7. LFOs

Sixteen. A step with `msrc` = 2 moves LFO n into its m cell, where n is the step's 32-step page, `step >> 5`. The FS1R's effect host gives each effect window four: aux 0..3, reverb 4..7, variation 8..11, insertion 12..15.

**Phase.** A 32-bit accumulator advanced by `LFO_SPEED << 12` per sample, so speed s runs at `s * 48000 / 2^20` Hz. `LFO_RESET` loads the start phase from `LFO_WAVE` bits 4..0.

**Value.** The program sees `wave(phase) * LFO_DEPTH / 32768 * 128`. `LFO_WAVE` bit 12 selects the range of the triangle and saws:

| | bit 12 clear (unipolar, every VOP3-2 LFO) | bit 12 set (bipolar, VOP3-1) |
|---|---|---|
| saw down | `1 - x` | `1 - 2x` |
| saw up | `x` | `2x - 1` |
| triangle | 0 .. 1 .. 0 | 0 .. 1 .. -1 .. 0 |
| `LFO_DIRECTION` | 0x8000 subtracts 0.5 | 0x4000 halves |

With x the phase in [0, 1). Square, sine and sample and hold are -1..1 either way. The unipolar rule is measured on the unit (a chorus LFO reads 0..+128, a 2WayRotary with direction 0xC000 -64..+64); the bipolar rule is fitted to the filter's LFO2 and inferred to hang on bit 12, the only LFO bit the two FS1R hosts set differently.

## 8. EGs

Sixteen, used by VOP3-1 (the filter EG) and the AN boards. A segment is set by `EG_RATE`, `EG_TARGET` and `EG_AIM`:

* Rate 0 jumps to the target; rate 255 holds.
* Any other rate approaches the aim exponentially, covering two thirds of the way in `FEG_SEG_S * 2^(rate / FEG_RATE_OCT)` seconds (1.33e-4 s, 15.5; `Vop3` in `vop3.h`).
* The segment ends when the level crosses its target, sets its bit for `EG_STATUS`, and stops there.
* A segment armed within two units of its target ends after `FEG_FLAT_S` (0.78 s) whatever its rate.

The EG level reaches the program as serial input source 3, scaled so the level's ±512 spans the register range ±128.

## 9. Open

* What arms an EG segment, and the chip's own EG step rate (the core steps them once per sample).
* Register 0x05's bits, 0x01, 0x12, 0x1B, 0x1C, 0x28..0x2A, `CONFIG` entries 1..14, and `LFO_SPEED` bits 10..11.
* `LFO_DIRECTION`'s low bits, 0x4000 without bit 12 and 0x8000 with it, and the square, sine and sample-and-hold waves, which no measured program uses.
* The scale of the delay index load (`idxw`).
* VOP3-1's per-channel register banking.
* What the tag byte means to the chip, if anything.
* Vokodrone's flanger notch peaks run 10% under the unit's at |FB| 60; they match within 1% at -30..+30.

## Appendix A. Disassembler names

`tools/vop3_disasm.py` and the research journal name words and fields as they were first probed. The mapping:

| here | disassembler / journal |
|---|---|
| w0..w4 | `r10`, `r9`, `r8`, `r7`, `r6` |
| `memop` | `mem` |
| `dr`, `sr` | `rA`, `rB` |
| `rcopy`, `wdata` | `r9[7]`, `r9[13]` |
| `tn` | `path` |
| `tbl` | `r8[5]` |
| `dm`, `msrc` | `daddr` low, high |
| `t`, `m1` | `rsrc`, `rd-en` |
| `asel`, `m2m` | `op` bits 1:0, bit 2 |
| `sub` | `sel` |
| `shift` | `route` |
| `sm` | `wdst` |
| `sout` | `r6[6]` |
| `mmode` | `class` |

## Appendix B. Firmware sources

The routines each register was read from, by board. Addresses are in the v1.20 FS1R EPROM, the PLG150-AN ROM and the AN1x v1.04 image.

| reg | FS1R | PLG150-AN | AN1x |
|---|---|---|---|
| write routine | `FUN_00039648` (VOP3-2), `FUN_0000B5E2` (VOP3-1) | | |
| program upload | `FUN_000398C2` (VOP3-2), `FUN_0000BC8C` (VOP3-1), `FUN_0003C9C4` (boot image) | `FUN_0007F87C` | `FUN_00056028` |
| 0x00 auto-increment | `FUN_000396A6` | | |
| 0x01, 0x05 | `FUN_0003A064`, `FUN_0000BC8C` | `FUN_00080A3A` | 0x56B98 |
| 0x0D, 0x0E | `FUN_000397F4` | `FUN_0007F87C` | `FUN_00056028` |
| 0x0F..0x12 | | `FUN_00081358` | |
| 0x14 | `FUN_0000BAF6` (VOP3-1) | | |
| 0x16 | `FUN_000397BA`, `FUN_0000B85E` | `FUN_0007F87C`, `FUN_0007FD1A`, `FUN_00080B70` | `FUN_00056028`, `FUN_00056162`, `FUN_00056C4C` |
| 0x1B | | `FUN_0008072C` | `FUN_000568DC` |
| 0x1C, 0x20 | `FUN_00039C74`, `FUN_0000D050` (VOP3-1, at key-on) | | |
| 0x22 | | `FUN_0007A502` | |
| 0x24 | `FUN_00039B24` (table 0x36EF54), `FUN_0000C130` (VOP3-1) | | |
| 0x25 | `FUN_00039B54`, `FUN_0000C1AC` (VOP3-1) | | |
| 0x26 | `FUN_00039B84`, `FUN_0000C20C` (VOP3-1) | | |
| 0x27 | `FUN_00039BB4`, `FUN_003A9B8A`, `FUN_0000CA44` (VOP3-1) | | |
| 0x28..0x2A | `FUN_00039BE4`..`FUN_00039C44` | `FUN_0006CAE8` | |
| 0x2B..0x2D | `FUN_0000BB30` and following (VOP3-1) | EG driver | EG driver |
| constant shadow | `DAT_0106842C` | | |
