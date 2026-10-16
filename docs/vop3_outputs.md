# How VOP3-2 reaches the DACs

Blocker 2 of `docs/vop3_blockers.md` asks which steps of the shipped VOP3-2 program feed the main and individual outputs, and by what mechanism. This note answers it from the FS1R firmware (`FS1R_GHIDRA_ANALYSIS/decomp.db`, v1.20), the PLG150-AN and AN1x decompilations, the session 11 and 16 captures in FS1R.unlock, and a static scan of every image in `docs/vop3_disasm/`. Nothing here was measured for this note; each claim names the function or capture it rests on, and the last section says how strong each is.

## Summary

- **There is no 15-bus mixer in the register file.** Registers 0x16 and 0x28..0x2A are 15-entry tables written once at boot (0x16 also around a program upload) and never by a parameter handler. Registers 0x24..0x27 are 16-entry and belong to the chip's LFOs, not to buses. Every level, pan, send, return and output switch the user can set is a **step constant** (register 0xB) in the microcode.
- **The output words are four cells: `d[10]`/`d[11]` for the main pair (SDO1) and `d[28]`/`d[29]` for the individual pair (SDO3).** `0cf`/`0d0` write `d[10]`/`d[11]` at route 3 and carry the performance volume; `10a`..`10d` write `d[28]`/`d[29]` at route 3 and carry the Individual Output switch. Both are read straight off the firmware.
- **A step with `r6[6]` set names `w[n]` as a serial output slot**, with the port and channel picked by the step's 64-step page. Test image 1 declares `w[0c]` at `002`/`042` and `w[0b]` at `102`/`142`; the shipped program declares `w[10]` at `002`, `w[28]` at `045`, `w[11]` at `10e` and `w[29]` at `156`, the same four indices as the output cells. This is the proposed mechanism, and it is inferred.
- **Why the interpreter hears nothing:** the cells are fed by `r[1d]`, `r[29]` (main) and `r[07]`, `r[08]`, `r[11]`, `r[12]` (individual), which no step of the decoded program writes. Driving those registers by hand puts signal on all four cells in the interpreter; audio on the input port never reaches them. The output path is therefore not missing; the path *into* those registers is, and that is blocker 4 (`w[]` never read).

## 1. The per-index registers, on both FS1R chips

`FUN_00039648(reg, val)` writes VOP3-2 at `0x800000 + 2*reg`; `FUN_0000B5E2` writes VOP3-1 at `0x800200 + 2*reg`. Every caller of both was enumerated. The registers above 0x13 and every function that writes them:

| reg | VOP3-2 writer | VOP3-1 writer | callers | count, index | contents |
|---|---|---|---|---|---|
| 0x16 | `FUN_000397BA` (addr = i + 1 in reg 0) | `FUN_0000B85E` | VOP3-2: `FUN_0003A064` boot, `FUN_000398C2` around each upload. VOP3-1: `FUN_0000BC8C` boot only | 15 | VOP3-2 from EPROM 0x37101A: `42 44 1C 00 42 27 00 00 00 00 00 00 00 00 01`; VOP3-1 from 0x37861A: `17 12 00 .. 00 0B 40` |
| 0x17 | `FUN_000398C2` | `FUN_0000BC8C` | upload paths | 1 | 0x23 during an upload, 0 after |
| 0x1C, 0x20 | `FUN_00039C74` | `FUN_0000BAC2` | `FUN_000398C2`; VOP3-1 `FUN_0000D050` | 1 | reg 0x20 = a 16-bit channel mask, then reg 0x1C = 0 |
| 0x24 | `FUN_00039B24` | `FUN_0000B8EC` | `FUN_003A3070`, `FUN_003A9DD0`, `FUN_000398C2`; VOP3-1 `FUN_0000C130` | 16 | LFO speed word (`0x36EF54[v]`, VOP3-1 `LFO2SPD 0x374A04[v]`), bits 10..11 kept |
| 0x25 | `FUN_00039B54` | `FUN_0000B932`, `FUN_0000C182` | `FUN_003A9B8A`; VOP3-1 `FUN_0000C1AC`, `FUN_0000D050` | 16 | waveform in bits 5..7 (VOP3-1 type 0..6 to 0x00..0xE0) |
| 0x26 | `FUN_00039B84` | `FUN_0000B970` | `FUN_003A3128`; VOP3-1 `FUN_0000C20C` | 16 | depth, `v * 0x8000 / 127`, 0x7FFF at 127 |
| 0x27 | `FUN_00039BB4` | `FUN_0000B9AE` | `FUN_003A9B8A`; VOP3-1 `FUN_0000CA44` | 16 | 0 / 0x7FFF on VOP3-2; 0x4000 / 0xC000 / 0 on VOP3-1 |
| 0x28 | `FUN_00039BE4` | `FUN_0000BA08` | boot only, both chips | 15 | 0x15 |
| 0x29 | `FUN_00039C14` | `FUN_0000BA46` | boot only, both chips | 15 | 0x15 |
| 0x2A | `FUN_00039C44` | `FUN_0000BA84` | boot only, both chips | 15 | 0 |
| 0x2B..0x2D, 0x21 | none | `FUN_0000BB30`..`FUN_0000BC42` | `FUN_0000B010`, `FUN_0000CB6C`, `FUN_0000D050`, `FUN_0000D7B0` | 16 | VOP3-1 filter EG segment (rate, end, asymptote, start), `docs/findings.md` 2026-09-23 |
| 0x14 | none | `FUN_0000BAF6` | `FUN_0000C468` | 1 | VOP3-1 only |

VOP3-2 never writes 0x2B..0x2D, 0x21 or 0x14, and no function in the FS1R firmware writes register 0x2C to VOP3-2.

**Registers 0x24..0x27 are the LFO channels, not buses.** `FUN_000398C2` calls `FUN_00039B24/54/84/B4` for four indices per effect block, from tables in EPROM: reverb `0x35E178` = 0..3, ins1 `0x35E304` = 4..7, ins2 = 8..11 (literal `"\b\t\n\v"`), variation `0x35E264` = 12..15. The block's mask for `FUN_00039C74` is the same four bits: 0x000F, 0x00F0, 0x0F00, 0xF000. On VOP3-1 the same registers carry filter LFO2 (speed `LFO2SPD`, waveform `FUN_0000C1AC`), and `FUN_0000D050` resets channel bit `1 << ch` through 0x20 / 0x1C on key-on (`FUN_0020338C` is a left shift). So 0x20 / 0x1C is an LFO phase reset by mask, and VOP3-2 has 16 LFOs, four per effect block. `FUN_0039B09C` loads each block's four 0x24..0x27 words with the type's coefficient row; `FUN_003A9B8A` flips the waveform bits 0x20/0x40 and the 0x27 word (0 / 0x7FFF) on a sign test, which is a modulation-direction switch, not a level.

**Register 0x16 is not a level.** It is written by boot and by the upload bracket only: `FUN_000398C2` sets entry 0 to 0x40 and register 0x17 to 0x23 before rewriting a block's steps, then entry 0 back to 0x42 and 0x17 to 0. `FUN_0000BC8C` brackets VOP3-1's upload the same way (0x40, then 0x17). No user parameter reaches it, and most entries are zero on a chip whose outputs are live, so "per-bus level" in `docs/research.md` section 8 and `docs/vop3_microcode.md` is a misreading. Entry 0 is a control word whose bit 6 is held while microcode is rewritten; the rest is static configuration whose meaning is not read anywhere in the firmware. The PLG150-AN uploads the same 15-word shape to register **0x2C**, not 0x16: `FUN_0007F87C` writes them when its flag is 1, `FUN_0007FD1A` clears them, `FUN_00080B70` writes 0x17 to entries 1 and 2, and every image carries `0017 000A x11 0000 0000 0000`. The AN1x does the same (`FUN_00056028`, `FUN_00056162`, `FUN_00056C4C`). Entry 0 = 0x17 on VOP3-1 and on both AN boards, so this is one per-chip table. The two numbers are one register: the AN drivers write by byte offset (base + 2 x reg), so `| 0x2c` is register 0x16, as their `| 0x18` is the tag register 0xC (`docs/vop3_an_ram.md`).

**0x28 / 0x29 / 0x2A** are written once at boot to 0x15, 0x15 and 0 for 15 indices on both FS1R chips and never again. They are configuration, not a mixer.

## 2. Where the mixing actually is: step constants

The firmware keeps a shadow of every VOP3-2 step constant at `DAT_0106842C + 2*step` and ships it through `FUN_00039652` / `FUN_000396A6` to register 0xB. Searching every reference to that array by step gives the mixer:

| step(s) | written by | performance byte (Data List address) | role |
|---|---|---|---|
| `000`..`00B` | `FUN_0003A2EC` cases 2, 5, 10, 11, 12, 13 | reverb, variation, insertion pans (01 29, 01 2C, 01 30) | pairs from the quarter-sine pan law at `0x3719D0` (`[0x80 - pan]`, `[pan]`) |
| `00C`..`019`, `046`..`055`, `089`..`096`, `0D9`..`0E7` | `FUN_0003BB30`, `FUN_0003BA2C`, `FUN_0003BA8E`, `FUN_0003A2EC`, step lists at `0x35DFA0`..`0x35DFFF` | insertion level, ins to reverb, ins to variation, returns, variation to reverb (01 2A..01 33) | the send and return summing chains (which step carries which parameter is not split out here) |
| `0CF`, `0D0` | `FUN_0003A2EC` case 14 (step list `0x35DFBC`, `0D0` via `+0x1E0`) | `DAT_01068DD8` = performance byte 0x10, **performance volume** (`FUN_000224F8` case 0x22, `FUN_0003AC88`) | master gain on the main pair |
| `10A`, `10B`, `10C`, `10D` | `FUN_0003BBBA(mode)`, step list `0x3721D4` = `10A 10C 10B 10D` | performance byte 0x14, **individual out** (0 off, 1 pre ins, 2 post ins; `FUN_000224F8`, `FUN_0003AC88`) | off: all 0; pre ins: `10A`, `10C` = 0x0B50; post ins: `10B`, `10D` = 0x0B50 |
| `0E9`, `0EB` | `FUN_0003C9C4` (boot), `FUN_0003CBBC` (diagnostic, 0x4000) via `0x3721FC` | none | the output gains of test image 1 |

Part-to-effect sends and the part dry level do not touch VOP3-2 at all. `FUN_00019362` cases 0x11..0x14 and `FUN_0001FF90`/`FUN_0001FFC6` write them to the YMP706 (events 0x222..0x224 through `FUN_000224F8`/`FUN_0002251E`, `SENDTAB 0x35C006`), and with the part's Insertion switch on the reverb and variation sends are forced to 0xFF (muted) at the tone generator. So the tone generators already mix every part into FS1B's dry and send buses, as `docs/research.md` 2.0.1 has it, and VOP3-2 receives finished buses on SI0..SI3.

The reverb program is always `reverb_00` in normal use: `DAT_0106840E`, the reverb program index `FUN_00039C8A` reads, has one writer, `FUN_0003AAC8`, which sets it to 0 (only the forced path in `FUN_003A3308` uses 3). That is why `FUN_0003BBBA` can patch fixed step numbers `10A..10D` inside the reverb window: in `reverb_00` they are the individual-out block.

## 3. The output steps of the shipped program

Assembled program (base, `reverb_00`, `ins1_00`, `ins2_00`, `variation_00`):

```
0cf  route=3 op=3 class=2  y = k*r[1d]               d[10] = y     main L: k = performance volume
0d0  route=3 op=3 class=2  y = k*r[29]               d[11] = y     main R
10a  route=0 op=3 class=2  y = k*r[07]                             individual, pre ins (k = 0x0B50 or 0)
10b  route=3 op=0 class=2  y = s + k*r[11]           d[28] = y     individual L, post ins
10c  route=0 op=3 class=2  y = k*r[08]                             individual, pre ins
10d  route=3 op=0 class=2  y = s + k*r[12]           d[29] = y     individual R, post ins

002  r6 = 86d0  class 2, r6[6]  -> w[10]                           page 0
045  r6 = bde8  class 2, r6[6]  -> w[28]                           page 1
10e  r6 = 0051  class 0, r6[6]  -> w[11]                           page 4
156  r6 = ae69  class 2, r6[6]  -> w[29]                           page 5
```

`d[10]`, `d[11]`, `d[28]`, `d[29]` are written and **never read by any op-7 step** in the assembled program, the only route-3 `d[]` writes with no reader. All four `reverb_0x` programs write `d[28]`/`d[29]` (types 1 and 3 at route 0 or class 0, type 2 by capture), and base writes `d[10]`/`d[11]`, so every combination has the four cells.

Test image 1 has the same two layers:

```
002  r6 = 004c  class 0, r6[6]  -> w[0c]     page 0      042  same, page 1
102  r6 = 004b  class 0, r6[6]  -> w[0b]     page 4      142  same, page 5
0e9  op 7 route 1  y = k*d[0b] -> w[0b]      right
0eb  op 7 route 1  y = k*d[0c] -> w[0c]      left
```

The session 11 nop walk (FS1R.unlock `captures/2026-10-01-12/README.md`, recorded off the main DAC) found: clearing `002` makes both channels identical, so does clearing `102`; clearing `042`, `142` changes nothing; clearing `0e9`/`0eb` changes nothing while their constants still set the gain. That is the step that declares a slot mattering, and the step that computes into it not mattering once its word is gone, which only reads one way: the port takes `w[n]` (or cell n), and a step only loads it.

**Proposed mechanism.** A step with `r6[6]` set declares slot n = `r6[5:0]` as a serial output; `(step >> 6) & 3` picks the port and `step >> 8` the channel. Page 0 / page 4 are the main pair's left and right, page 1 / page 5 the individual pair's. Test image 1 declares the same two slots on both ports, so it plays the same audio on both DACs, and `042`/`142` are inert in a recording that taps the main DAC only. The shipped program declares `w[10]` (page 0) and `w[11]` (page 4) for the main pair and `w[28]` (page 1) and `w[29]` (page 5) for the individual pair, exactly the indices of the four unread `d[]` cells the master-volume and individual-out steps write. The value on the port is cell n at the end of the pass; whether that is `d[n]`, `w[n]`, or one file seen two ways is open, since in both images the declared `w` index equals the written `d` index.

## 4. Static scan across every image

`/tmp/vop3_outscan.py` (scratch, not committed) runs `fields()` over every FS1R VOP3-2 image, the PLG150-AN and the AN1x.

**Twins of `0e9`/`0eb`** (class 2, op 7, daddr `0x180|n`). Shipped FS1R VOP3-2: four in total, none in base or reverb: `variation_08 1fe` (route 2, `d[6c] -> w[38]`), `variation_09 1b9`, `variation_12 1f5`, `ins1_10 149` (and `ins2_10`). Each sits inside an effect and feeds its own chain, so op 7 reading `d[]` is the chip's generic "play a cell" and not an output stage. The AN images use it heavily (PLG150-AN mode 0/1 15 steps, AN1x 16), as tap reads in the voice. The output role of `0e9`/`0eb` is particular to the test image.

**`r6[6]`** is rare everywhere and marks a short list of slots:

| image | `r6[6]` steps (step: slot) |
|---|---|
| test image 0 and 1 | `002: w0c`, `042: w0c`, `102: w0b`, `142: w0b` |
| assembled VOP3-2 | `002: w10`, `045: w28`, `10e: w11`, `156: w29` |
| reverb 0..3 | `10e` or `10f: w11`, in every type |
| ins1 | `156: w29` in types 0 and 8 only; ins2: none |
| VOP3-1 filter v0 | 16 steps, one per filter channel: `003, 028, 054, 064, 083, 0a3, 0d0, 0f0, 103, 123, 14c, 16c, 180, 1a8, 1c8, 1e8` |
| PLG150-AN mode 0/1/2 | `004: w1a`, `041`/`043: w20`, `082`/`083: w22` |
| AN1x voice | `004: w03`, `046: w1b`, `085: w07`, `100: w2e`, `141: w2f`, `182: w07` |

Sixteen on VOP3-1, a chip that returns sixteen filtered channels to FS1B (SO3/SO7), and four on VOP3-2, a chip that drives two stereo DACs, is the count the output reading predicts. On the AN programs the declared slots line up with the cells the voice's final mixer writes: PLG150-AN base `1fc..1ff` write `d[1e] d[20] d[18] d[22]` at route 1 with k = 0x7FFF, and the AN1x voice ends `1fb..1ff` writing `d[03] d[2e] d[1b] d[2f]` at route 1 (k 0x7FFF), with `w[03]`, `w[1b]`, `w[2e]`, `w[2f]` among its `r6[6]` slots. Those are the AN programs' output steps by the same rule.

`docs/vop3_research_journal.md` section 2 marked `r6[6]` as inert ("`-`") because flipping it on a compute step did nothing; the nop walk above is the only time a step carrying it was removed.

`r7[15:14]` is 1..3 on hundreds of steps in every program and does not sit at program ends, so it is not an output marker. `route` alone is not either: route 3 is common, and the four output cells are told apart by having no reader, not by their route.

## 5. Why the interpreter still gives silence

With the assembled program in `tools/vop3_interp.py` (every constant 0.5, as `tools/vop3_gaps.py`) and the four cells watched over 2048 passes:

- an impulse on the input port: `d[10]`, `d[11]`, `d[28]`, `d[29]` all stay 0;
- `r[09..12]` held at 0.1: `d[28]` = `d[29]` = 0.05, the main pair 0;
- `r[1d]`, `r[29]` held at 0.1: `d[10]` = `d[11]` = 0.05.

So the output steps work as decoded, and the gap is upstream. The main pair reads `r[1d]` and `r[29]`, the individual pair `r[07]`, `r[08]` (pre ins) and `r[11]`, `r[12]` (post ins), and the pan stage `000..00B` reads `r[0b..12]`. No class-2 step writes `r[]`, and the class-1 loads touch only `r[01]`, `r[02]`, `r[79..7f]`. These are where the effect blocks and the bus inputs must land. Either they are hardware inputs (SI0..SI3 and the chip's own returns), or `w[n]` is visible as `r[n]` across a pass boundary or through the `rd-en` read, which the stride test rules out only for `r[40|n]`. Nothing here decides between them. This is blocker 4, and it, not an output port, is what stops the shipped program from sounding in the interpreter.

## 6. Evidence strength

| claim | strength | basis |
|---|---|---|
| 0x16, 0x28..0x2A are static 15-entry tables, not levels | strong | every writer in both FS1R drivers enumerated; no parameter path reaches them |
| 0x24..0x27 / 0x20 / 0x1C are 16 LFO channels, 4 per effect block | strong | index tables `0x35E178`, `0x35E264`, `0x35E304`, `"\b\t\n\v"`; masks in `FUN_000398C2`; VOP3-1 uses them for LFO2 |
| `0CF`/`0D0` are the master gain, `10A..10D` the individual-out switch | strong | `FUN_0003A2EC` case 14, `FUN_0003BBBA`, performance bytes 0x10 and 0x14 matched to the Data List |
| reverb program is always `reverb_00` | strong | `DAT_0106840E` has one writer, `FUN_0003AAC8`, = 0 |
| `d[10]`/`d[11]` reach SDO1 and `d[28]`/`d[29]` SDO3 | medium | the gains above plus unread route-3 cells; never probed on the unit |
| `r6[6]` declares the output slot, port by page | medium-weak | one nop walk on test image 1 plus the structural match in five images; the page-to-port rule rests on test image 1 and the shipped layout only |
| which SO pin each page drives | open | the board says SDO1/SDO3; nothing in the firmware names pins |

## 7. Next capture

One session on the shipped program closes the medium rows: with a reverb-free performance holding a note, clear `0cf` (main left should go, or freeze), then `002`, then `10e`; set Individual Output to post ins and record the slave DAC while clearing `10b` and `156`. A second run moves `002`'s `r6[6]` to `003` and to `042` to test the page rule directly.
