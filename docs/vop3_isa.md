# VOP3 (Yamaha YSS236) reference

What the chip does, as the engine runs it: the host-side register map, the step word, the arithmetic, the memory
and the I/O. `src/yss236_core.h` is the executable form of this page (its compiled pass,
`yss236_jit.cpp`, is checked against it pass for pass) and `src/vop3.cpp` / `vop3_regs.h` the
register side. How each rule was found, session by session, with the readings later withdrawn, is
[vop3_research_journal.md](vop3_research_journal.md); a rule's `sNN` tag below names its session there.

Evidence marks: **FW** read from the FS1R, PLG150-AN or AN1x firmware (Ghidra databases in `FS1R_DISASM`);
**CHIP** measured on rgwan's FS1R through the debug monitor; **inferred** where neither.

Related: [vop3_outputs.md](vop3_outputs.md) (the FS1R's VOP3-2 mixer and output cells, from firmware),
[vop3_an_ram.md](vop3_an_ram.md) (the AN boards' table RAM and the AN1x mask ROM), [vop3_blockers.md](vop3_blockers.md)
(what stood between the disassembly and a running model, now mostly closed), `docs/vop3_disasm/` (every program
image disassembled, `python tools/vop3_disasm.py --listings`).

## 1. One chip, four boards

The YSS236 is the effect DSP of the FS1R (VOP3-2, at 0x800000, with a 2^18-word DRAM), its filter (VOP3-1, at
0x800200, no DRAM), and the synthesis engine of the AN1x (a 1 Mbit mask ROM on the RAM pins) and PLG150-AN (a
DRAM the H8 fills with tables at boot). **A rule measured on one applies to all**: no measurement on one chip has
contradicted one on another, and the disassembler and core decode every image with the one set of meanings.

* A program is **512 steps**, run once per sample (24.576 MHz / 512 = 48 kHz on the FS1R). FW: every uploader
  (FS1R `FUN_0000BC8C` / `FUN_000398C2`, PLG150-AN `FUN_0007f87c`, AN1x `FUN_00056028`) writes 512 rows.
* A step is **five 16-bit words** plus a 16-bit **constant** and a **tag byte**. Nothing is computed outside the
  steps: every level, pan, send, delay time and filter coefficient a user can set is a step constant or a slot
  offset (FW, `vop3_outputs.md` section 2).

## 2. Host-side registers

The host writes 16-bit words at `base + 2 * reg` (FS1R `FUN_00039648(reg, v)` for VOP3-2, `FUN_0000B5E2` for
VOP3-1). **The H8 boards' decompilations show byte offsets**: their `| 0x1a`, `| 0x1c`, `| 0x2c` are registers
0x0D, 0x0E, 0x16. Most registers address one of several items (a step, a slot, an LFO, an EG channel, a table
entry) through the **select latch**, register 0. The register file is **write-only** from the CPU: reads return bus
noise, except register 0x21 (and, on the PLG150-AN, 0x22) (CHIP s38n, FW).

| reg | name (`vop3_regs.h`) | indexed by | what a write does | written by | evidence |
|---|---|---|---|---|---|
| 0x00 | `SELECT` | - | the step, slot, LFO, EG or table entry the next indexed write goes to. Bit 15 (`AUTO_INCREMENT`): each program-word or constant write then advances the step | every driver | FW `FUN_000396a6` ORs 0x8000 for a constant burst; CHIP s8 |
| 0x01 | (control) | - | an upload bracket: FS1R VOP3-2 boot writes it, then 0; VOP3-1 `1 = 0x1004` around its upload. Meaning of the bits open | FS1R `FUN_0003a064`, `FUN_0000BC8C` | FW |
| 0x05 | (config) | - | chip configuration, written once at boot: **FS1R 0x1FF4, both AN boards 2**. Writing 2 on a running FS1R silences its whole program (CHIP s40e). Bits open | FS1R `FUN_0003a064`, PLG `FUN_00080a3a`, AN1x 0x56B98 | FW, CHIP |
| 0x06..0x0A | `PROGRAM` - k | step | the step's word k: reg 0x0A is `w0`, 0x09 `w1`, 0x08 `w2`, 0x07 `w3`, 0x06 `w4` (the session-era names `r10 r9 r8 r7 r6`) | all uploaders | FW, CHIP s8 |
| 0x0B | `COEF` | step | the step's constant, signed 1.15 | constant handlers | FW, CHIP s11/s14 |
| 0x0C | `TAG` | step | the step's tag byte: the firmware's write-group id (AN1x 3..7 = voice 0..4, 1 = global); the chip's use of it open | uploaders | FW |
| 0x0D | `SLOT_HIGH` | slot | delay slot offset, bits 17..9 | `FUN_000397f4`, PLG `FUN_0007f87c`, AN1x `FUN_00056028` | FW, CHIP s16/s34 |
| 0x0E | `SLOT_LOW` | slot | delay slot offset, bits 8..0. The 18-bit offset is `0x0D << 9 \| 0x0E` | as 0x0D | FW, CHIP |
| 0x0F | (RAM address high) | - | **host access to the external RAM**: the address of the next data write, high part. The PLG150-AN writes 0 here once before its table load | PLG150-AN `FUN_00081358` only | FW |
| 0x10 | (RAM address low) | - | the low part, also 0 before the load | as 0x0F | FW |
| 0x11 | (RAM data) | - | writes one 16-bit word at the RAM address, which then **advances by one**: 0x6121 consecutive writes fill words 0..0x6120 | as 0x0F | FW (`docs/vop3_an_ram.md`) |
| 0x12 | (RAM write control) | - | written 0 before each data word; meaning open (a strobe or a mode word) | as 0x0F | FW |
| 0x14 | `TRIGGER` | - | the constant loads of every gated block land at once (section 6) | VOP3-1 `FUN_0000BAF6`; never on VOP3-2 | FW |
| 0x16 | `CONFIG` | entry (select i + 1) | a 15-entry table written at boot and around each upload. Entry 0 is a control word whose bit 6 is set while microcode is rewritten (VOP3-2 0x42 / 0x40; VOP3-1 and the AN boards 0x17); the other entries are static and open | FS1R `FUN_000397BA`, `FUN_0000B85E`; PLG `FUN_0007F87C`; AN1x `FUN_00056028` | FW |
| 0x17 | `UPLOADING` | - | 0x23 while a block's program is rewritten, 0 after | `FUN_000398C2`, `FUN_0000BC8C` | FW |
| 0x1B | (config) | - | **0xFC00 at boot on both AN boards**; the FS1R never writes it. Open | PLG `FUN_0008072c`, AN1x `FUN_000568dc` | FW |
| 0x1C | - | - | written 0 right after 0x20; open | `FUN_00039c74` | FW |
| 0x20 | `LFO_RESET` | mask | each set bit n puts LFO n back to its start phase (`LFO_WAVE`'s low 5 bits) | `FUN_00039c74` (the block's 4-bit mask on upload), VOP3-1 `FUN_0000D050` at key-on | FW |
| 0x21 | `EG_STATUS` | - | write: the EG channels that may report; **read**: 0x10 \| the lowest enabled channel whose segment ended, 0 if none | VOP3-1 / AN EG drivers | FW |
| 0x22 | - | - | read and written by the PLG150-AN's EG driver (`FUN_0007a502`, compared with 0x4000); open | PLG only | FW |
| 0x24 | `LFO_SPEED` | LFO | 10-bit speed, **cycles per sample x 2^20** (s41: the effects modulate at the Data List's LFO Freq). Bits 10..11 are kept by the host across speed changes; open | `FUN_00039B24` (`0x36EF54[v]`), VOP3-1 `FUN_0000C130` | FW, CHIP s41 |
| 0x25 | `LFO_WAVE` | LFO | bits 7..5 the wave (0 triangle, 1 saw down, 2 saw up, 3 square, 4 sine, 5 sample and hold); bits 4..0 the start phase, in 32nds of a cycle | `FUN_00039B54`, VOP3-1 `FUN_0000C1AC` | FW, CHIP s41 |
| 0x26 | `LFO_DEPTH` | LFO | the LFO's amplitude, unsigned 1.15 (0x7FFF full: the wave spans the register range, 0..+128 for a unipolar one) | `FUN_00039B84`, VOP3-1 `FUN_0000C20C` | FW, CHIP s41c |
| 0x27 | `LFO_DIRECTION` | LFO | VOP3-2: bit 15 centres the unipolar wave (-0.5..0.5 instead of 0..1, s41g); the effect host writes 0, 0x7FFF and 0xC000, so its low bits are open. VOP3-1 writes 0x4000 / 0xC000 with its saws and 0 otherwise, unmeasured | `FUN_00039BB4`, `FUN_003A9B8A`; VOP3-1 `FUN_0000CA44` | FW |
| 0x28, 0x29, 0x2A | - | entry | 15 entries each, written once at boot: 0x15, 0x15, 0 on both FS1R chips; the AN boards write them in their channel setup. Open | `FUN_00039BE4`..`FUN_00039C44`, PLG `FUN_0006cae8` | FW |
| 0x2B | `EG_RATE` | EG | an EG segment's rate (0 jumps, 255 holds); writing it acknowledges the segment | VOP3-1 `FUN_0000BB30`..; AN EG drivers. Never on VOP3-2 | FW, CHIP (filter EG) |
| 0x2C | `EG_TARGET` | EG | the level at which the segment ends; arms it | as 0x2B | FW |
| 0x2D | `EG_AIM` | EG | the level the segment heads for | as 0x2B | FW |

Registers 0x02..0x04, 0x13, 0x15, 0x18..0x1A, 0x1D..0x1F, 0x23 and 0x2E up are written by no firmware of the
four boards. VOP3-2 on the FS1R never writes 0x14, 0x21, 0x22 or 0x2B..0x2D: its 16 EGs are unused there.

**Uploading a program** (FS1R `FUN_000398C2`, the order matters only for the bracket): `CONFIG` entry 0 = 0x40,
`UPLOADING` = 0x23; per step, `SELECT` = step | 0x8000 then the tag, the five words and the constant; the block's
slot offsets; its four LFOs' four registers; `LFO_RESET` with the block's mask, then 0x1C = 0; `CONFIG` entry 0 back,
`UPLOADING` = 0. **Writing a constant** is `SELECT` = step, `COEF` = value; the firmware keeps a shadow of every
constant (`DAT_0106842C`) and the 192 Hz tick writes through the same select latch with interrupts masked, so a
write from outside that critical section can land on the step the tick last named (CHIP: two takes lost to it).

**The external RAM from the host** is write-only through 0x0F..0x12, and only the PLG150-AN uses it (one 0xC242-byte
table load at boot). The FS1R's VOP3-2 DRAM is never written by its CPU; its programs fill it.

## 3. The step word

Word k is register 0x0A - k. Field names are the core's (`yss236_core::op`); the last column is what
`tools/vop3_disasm.py` and the journal call the same bits.

| word | bits | core field | meaning | journal name |
|---|---|---|---|---|
| w0 (0x0A) | 2:0 | `memop` | delay RAM: 1 write, 2/3 read, 4/6 read one word older, 0 none | `mem` (`r10`) |
| w1 (0x09) | 6:0 | `dr` | r register written (0 = none) | `rA` |
| | 7 | `rcopy` | `dr` takes the previous step's x operand (a z^-1), not the result | `r9[7]` |
| | 11:8 | `tn` | t register written | `path` |
| | 12:11 | `tw` | t write: bit 1 the result, bit 0 alone the step's constant | |
| | 12:8 | `dwr` | nonzero on a class-1 step: it also writes its m cell | |
| | 13 | `wdata` | this step's output is the data of the next RAM write | `r9[13]` |
| | 14 | `tfrac` | t takes the fraction of the result below one delay sample | |
| | 15 | `ctl` | control step (section 6) | |
| w2 (0x08) | 0 | `cmp` | the result sets the compare flag | |
| | 2 | `idxw` | the result, x 256 floored, becomes the delay index, in effect 3 steps later | |
| | 3 | `idxa` | add the delay index to this step's RAM address | |
| | 5 | `tbl` | the RAM access ignores the ring pointer (table address) | `r8[5]` |
| | 12:7 | `dm` | m cell written | `daddr` low |
| | 15:13 | `msrc` | what the m cell takes: 0 serial bank 1, 1 serial bank 2, 2 LFO, 3 EG level, 4/5 RAM capture, 6 the result, 7 the previous step's m operand | `daddr` high |
| w3 (0x07) | 2:0 | `t` | t register read (with `m1` bit 0 the upper eight) | `rsrc` |
| | 4:3 | `m1` | multiplier input 1: 0/1 the constant, 2/3 a t register | `rd-en` |
| | 7:6 | `asel` | adder input: 0 the running value p, 1 r[sr], 2 m[sm], 3 none | `op` bits 1:0 |
| | 8 | `m2m` | multiplier input 2 is m[sm], else r[sr] | `op` bit 2 |
| | 10 | `sub` | subtract the adder input | `sel` |
| | 13:12 | `shift` | the result times 1, 2, 4, 16 | `route` |
| | 15:14 | `clamp` | 0 wrap, 1 saturate, 2 negatives to 0, 3 absolute value | |
| w4 (0x06) | 5:0 | `sm` | m cell read | `wdst` |
| | 6 | `sout` | m[sm] goes to the serial output slot of this step's page | `r6[6]` |
| | 13:7 | `sr` | r register read (0 = none) | `rB` |
| | 15:14 | `mmode` | 0 no product (class 0), 1 constant load (class 1), 2 product (class 2), 3 unity product (class 3) | `class` |

The session-era `op` (3 bits, `w3[8:6]`) is `m2m << 2 | asel`: op 0 `p + k*r`, 1 `r + k*r`, 2 `m + k*r`, 3 `k*r`,
4 `p + k*m`, 5 `r + k*m`, 6 `m + k*m`, 7 `k*m`. That is how the journal's op tables and the core agree.

## 4. Arithmetic

Numbers are fixed point; the core holds them as doubles at these scales (s24, s27):

* r registers and m cells hold **[-128, 128)** (word / 256). The running value **p** holds [-256, 256). The RAM word
  clips at **+-32** (s41k: Vokodrone's feedback node, tapped on the unit, reaches +-11; the s14 +-8 is withdrawn). A class-1 constant `v` loads as `v / 256` (8.8); a class-2/3 constant is a signed 1.15 gain.
* **Class 2**: `y = (g * m2 +- a) * 2^shift`, with `g` the constant (or t[t] when `m1` >= 2), `m2` = m[sm] or
  r[sr], `a` the adder input. **Class 3** is the same with `g` = 1. **Class 1**: `y = (k * 128 +- a) * 2^shift`,
  the constant load plus the adder. **Class 0** writes nothing unless its adder, shift, clamp or sub bits are set,
  when the ALU runs without a product.
* `clamp` then shapes y: 0 wraps at the register range (p at its own +-256), 1..3 saturate at +-128. y becomes p
  and, when `dr` is set, is written to r[dr].
* **Writes land 3 steps later** (`LAT`): a register written at step n reads as the old value at n + 1 and n + 2
  (s24). m cell writes and captures the same (`DLAT`, s30/s31). t registers likewise.
* `rcopy` stores the previous step's x (r[sr] of the step before), the z^-1 the biquads keep their state in (s27).

## 5. Memory and I/O

* **Delay RAM**: 2^18 words, one ring whose pointer steps back once per pass. 128 slots; a step's slot is
  `step >> 2`, its address `pointer + offset[slot] (+ 1 for an "older" read) (+ index when idxa)`. A read transfer
  is a mark on any step from **003** on, any phase (s40f; marks on 000..002 transfer nothing, s40a/b/d). A write
  stores the latched `wdata` value if a marked step set one, else the step's own output (s34).
* **`tbl` (w2[5])** drops the pointer: the access addresses `offset[slot] (+ index)` directly, read or write
  (s40a). The AN boards' every access sets it; that is how their programs read the lookup tables
  (`vop3_an_ram.md`).
* **Captures**: an m cell with `msrc` 4/5 takes the latest RAM read transfer of this pass made **5..11 steps
  earlier**, else keeps its value (s36k, s40c).
* **Inputs**: `msrc` 0/1/3 move the serial input slot (bank 1 / bank 2) or the EG level of the step's page into
  the m cell; a slot is a 64-step page and its half. On the FS1R's VOP3-2 the effect sends also arrive in r
  registers the program reads in place: reverb r[0d]/r[0e], variation r[0f]/r[10], insertion r[11]/r[12] (s35h,
  s36). **Outputs**: `sout` puts m[sm] on the page's serial output slot; the FS1R's main pair is m cells 0x10 / 0x11
  (base steps 0cf / 0d0), the individual pair 0x28 / 0x29 (`vop3_outputs.md`). DAC word = floor to 18 bits of
  value / 4, clipped at +-8 (s27/s28).

## 6. Control steps

A step with `w1[15]` set computes nothing; it decides which of the following steps run. `w3[3:0]` = 8 is a
**gate**: the steps up to `w4[15:8]` (within the same half of the program) are skipped until the host writes
`TRIGGER`, which lands every gated block's constant loads at once (VOP3-1). `w3[3:0]` = 0xD is a **conditional
skip** on the compare flag: `w3[6:4]` 6 runs what follows only if the flag is positive, 4 only if negative, 1
never (a jump).

## 7. LFOs

Sixteen, four per FS1R effect window, by the firmware's tables (`LFOS_*` in `vop3host/rom_tables.cpp`: aux 0..3,
reverb-delay 4..7, variation 8..11, insertion 12..15). A step reads LFO n with `msrc` = 2 into an m cell, where n
is the step's 32-step page (`step >> 5`); the effect programs read theirs at steps 0x07 + 0x20 n (and 0x06, 0x0f
variants), each into the cell its delay or allpass coefficient path reads.

The phase is a 32-bit accumulator stepped by `LFO_SPEED << 12` per sample, so speed s is `s * 48000 / 2^20` Hz.
The value the program sees is `wave(phase) * LFO_DEPTH / 32768 * 128`.

**One rule for both chips, picked by `LFO_WAVE` bit 12.** Without it (every VOP3-2 LFO) the saws and the triangle
are unipolar and `LFO_DIRECTION` 0x8000 centres them (s41c/e/g, CHIP): an LFO
moved into an m cell at a free step and played on the DAC reads

| effect (wave, direction) | unit | engine before s41 |
|---|---|---|
| Chorus, Flanger, Rotary SP, Vokodrone's flanger (saw up, 0) | 0 .. +128, rising | -128 .. +128 |
| Phaser1 (triangle, 0, depth 0x2c58) | 0 .. +44.3 | -44.3 .. +44.3 |
| 2WayRotary (saw up, 0xC000) | -64 .. +64, rising | +128 .. -128, falling |

So saw down is `1 - x`, saw up `x`, the triangle `0 .. 1 .. 0`, and direction 0x8000 subtracts 0.5. The -1..1
waves the engine ran made every chorus, flanger, rotary and phaser modulate at twice its set rate (the delay or
allpass sweep folded through zero); with the measured shapes all eight effects of s41b carry the unit's three
strongest envelope and FM lines, frequencies to the bin. With bit 12 set (the VOP3-1 host boots every
`LFO_WAVE` to 0x1000) the waves are bipolar -1..1 and `LFO_DIRECTION` 0x4000 halves them: `20_fltmod`'s LFO2
envelopes match the unit at 6.1 dB rms read that way and 12.7 dB read unipolar (CAPTURE, inferred: bit 12 is the
only LFO register bit the two hosts set differently, so it is taken as the selector; no probe has flipped it).
Open: 0x4000 without bit 12, 0x8000 with it, and the square, sine and sample-and-hold waves (no measured program
uses them; they stay -1..1).

**Depth.** The delay-index steps were tapped on the unit (s41h, l: Vokodrone 1b4 / 1d3, the variation Flanger
and Chorus 16c / 182): each matches the engine's to 2% of its swing, so the LFO reaches the delay address at the
unit's depth. Rendered the way the rig plays it (performance and sine-voice bulks through the whole device), FM sd,
engine / unit: Vokodrone's flanger 0.0028 / 0.0030, insertion Chorus 0.00185 / 0.00184, 2WayRotary 0.00229 /
0.00232, Rotary SP 0.00169 / 0.00184, variation Phaser1 0.00036 / 0.00037; every envelope p1/p50/p99 within 12%.
The 4.3x Vokodrone excess of s41b was the probe, not the engine: it pushed a 0.25 sine straight into the bus,
hotter than the rig's voice, and drove the feedback node into the +-8 clip (section 4). Over all eight
effects the FM p5..p95 band and the envelope p1/p50/p99 agree with the unit (2WayRotary 977.2..1028.8 Hz against
977.2..1027.2; variation Chorus, on 41a's 40 s hold, an 11.10 Hz FM step against the engine's 11.08). Two things
in the unit takes are not the effect: every s25 take has a one-sample phase glitch every 1.451 s, in every
effect, that reads as an FM spike; and the variation Chorus (0.137 Hz) needs a hold longer than s41b's 4 s.
Vokodrone's flanger taps (s41k: 1d5, 1e9, 1f5) match the unit within 6% over their whole distribution with the
+-32 RAM clip. Left: Vokodrone's notch peaks are 10% under the unit's at |FB| 60 (0.177 / 0.195); at FB -30..+30
they match within 1%.

## 8. EGs

Sixteen, used by VOP3-1 (the filter EG) and the AN boards. A segment: rate 0 jumps to the target and 255 holds;
any other rate approaches the aim, two thirds of the way in `cal::FEG_SEG_S` at rate 0 and in twice that every
`cal::FEG_RATE_OCT`. A segment ends when the level crosses its target; armed within two units of it, it ends
after `cal::FEG_FLAT_S` whatever its rate. What arms a segment, and the chip's own EG step rate, are open (CHIP:
the filter EG takes, `docs/findings.md` 2026-09-23; `vop3.cpp`).

## 9. Open

* Register 5's bits (the AN boards' 2 silences the FS1R program), register 0x1B, 0x0F..0x12's control word 0x12,
  0x16's entries 1..14, 0x28..0x2A, 0x1C, and the LFO speed word's bits 10..11.
* `LFO_DIRECTION`'s low bits, the square, sine and sample-and-hold waves on the effect chip, and VOP3-1's LFO2 reading.
* Vokodrone's flanger notch peaks are 10% low at |FB| 60 (section 7).
* VOP3-1's register bank per channel (the journal's sessions 38q..39): the filter runs on the measured core with
  the firmware's program, and the bank map is what the replay still leans on.
* The tag byte's meaning to the chip, if any.
