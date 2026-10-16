# How the FS1R's VOP3-2 reaches the DACs

Where the FS1R's effect mix happens and which steps drive its outputs, read from the v1.20 firmware and the shipped programs. Field names are the core's (`vop3_isa.md` section 3; appendix A maps the disassembler's). All of it is **FW** unless marked.

## Summary

* **There is no mixer in the register file.** Every level, pan, send, return and output switch is a step constant. The registers above 0x13 are LFOs, EGs and static configuration (section 1).
* **The tone generators mix the parts.** Part sends and dry levels are set in the YMP706s, and VOP3-2 receives finished buses on its serial inputs (section 2).
* **Four m cells carry the outputs**: 0x10 / 0x11 the main pair, 0x28 / 0x29 the individual pair. The steps that write them carry the performance volume and the Individual Output switch (section 3).
* **A step with `sout` sends m[sm] to the serial output slot of its page.** The shipped program sends exactly those four cells (section 4). The core implements this rule; which board pin each slot drives is not read from anything.

## 1. The registers above 0x13

The FS1R writes VOP3-2 through `FUN_00039648(reg, v)` at `0x800000 + 2 * reg` and VOP3-1 through `FUN_0000B5E2` at `0x800200 + 2 * reg`. Every writer of the upper registers on both chips:

| reg | VOP3-2 writer | VOP3-1 writer | called from | entries | contents |
|---|---|---|---|---|---|
| 0x16 | `FUN_000397BA` | `FUN_0000B85E` | VOP3-2: boot `FUN_0003A064`, each upload `FUN_000398C2`. VOP3-1: boot `FUN_0000BC8C` only | 15 | VOP3-2 from 0x37101A: `42 44 1C 00 42 27 00 00 00 00 00 00 00 00 01`. VOP3-1 from 0x37861A: `17 12 00 .. 00 0B 40` |
| 0x17 | `FUN_000398C2` | `FUN_0000BC8C` | upload paths | 1 | 0x23 during an upload, 0 after |
| 0x1C, 0x20 | `FUN_00039C74` | `FUN_0000BAC2` | `FUN_000398C2`; VOP3-1 `FUN_0000D050` | 1 | 0x20 = a 16-bit LFO mask, then 0x1C = 0 |
| 0x24 | `FUN_00039B24` | `FUN_0000B8EC` | `FUN_003A3070`, `FUN_003A9DD0`, `FUN_000398C2`; VOP3-1 `FUN_0000C130` | 16 | LFO speed (`0x36EF54[v]`, VOP3-1 `0x374A04[v]`), bits 10..11 kept |
| 0x25 | `FUN_00039B54` | `FUN_0000B932`, `FUN_0000C182` | `FUN_003A9B8A`; VOP3-1 `FUN_0000C1AC`, `FUN_0000D050` | 16 | LFO wave in bits 5..7 |
| 0x26 | `FUN_00039B84` | `FUN_0000B970` | `FUN_003A3128`; VOP3-1 `FUN_0000C20C` | 16 | LFO depth, `v * 0x8000 / 127` |
| 0x27 | `FUN_00039BB4` | `FUN_0000B9AE` | `FUN_003A9B8A`; VOP3-1 `FUN_0000CA44` | 16 | VOP3-2 0 / 0x7FFF; VOP3-1 0x4000 / 0xC000 / 0 |
| 0x28, 0x29, 0x2A | `FUN_00039BE4`, `FUN_00039C14`, `FUN_00039C44` | `FUN_0000BA08`, `FUN_0000BA46`, `FUN_0000BA84` | boot only | 15 | 0x15, 0x15, 0 |
| 0x21, 0x2B..0x2D | none | `FUN_0000BB30`..`FUN_0000BC42` | `FUN_0000B010`, `FUN_0000CB6C`, `FUN_0000D050`, `FUN_0000D7B0` | 16 | the filter EG |
| 0x14 | none | `FUN_0000BAF6` | `FUN_0000C468` | 1 | the gate trigger |

**0x24..0x27 are the LFOs, four per effect block.** `FUN_000398C2` writes four LFO indices per block, from tables in EPROM: reverb `0x35E178` = 0..3, ins1 `0x35E304` = 4..7, ins2 = 8..11, variation `0x35E264` = 12..15. The block's `LFO_RESET` mask is the same four bits (0x000F, 0x00F0, 0x0F00, 0xF000). `FUN_0039B09C` loads each block's four LFOs from the effect type's coefficient row, and `FUN_003A9B8A` flips the wave and 0x27 on a sign test, which switches the modulation direction. On VOP3-1 the same registers carry the filter's LFO2, and `FUN_0000D050` resets LFO `1 << ch` at key-on.

**0x16 is a configuration table, not a set of bus levels.** Only boot and the upload bracket write it: `FUN_000398C2` sets entry 0 to 0x40 and 0x17 to 0x23 before rewriting a block, then entry 0 back to 0x42 and 0x17 to 0. No user parameter reaches it, and most entries are zero on a chip whose outputs are live. The AN boards write the same 15-word table (their `| 0x2c` is the byte offset of register 0x16), with entry 0 = 0x17 as on VOP3-1.

**0x28..0x2A** are written once at boot on both FS1R chips and never again.

## 2. The mix: step constants

The firmware keeps a shadow of every VOP3-2 constant at `DAT_0106842C + 2 * step` and sends it through `FUN_00039652` / `FUN_000396A6` to `COEF`. Every reference to that array, by step:

| steps | written by | performance parameter (Data List address) | role |
|---|---|---|---|
| 000..00B | `FUN_0003A2EC` cases 2, 5, 10..13 | reverb, variation, insertion pan (01 29, 01 2C, 01 30) | pan pairs from the quarter-sine law at 0x3719D0 (`[0x80 - pan]`, `[pan]`) |
| 00C..019, 046..055, 089..096, 0D9..0E7 | `FUN_0003BB30`, `FUN_0003BA2C`, `FUN_0003BA8E`, `FUN_0003A2EC`; step lists 0x35DFA0..0x35DFFF | insertion level, insertion to reverb and to variation, the returns, variation to reverb (01 2A..01 33) | the send and return chains |
| 0CF, 0D0 | `FUN_0003A2EC` case 14 | performance volume (byte 0x10) | gain on the main pair |
| 10A..10D | `FUN_0003BBBA(mode)`, step list 0x3721D4 | Individual Output (byte 0x14: 0 off, 1 pre insertion, 2 post insertion) | off: all 0; pre: 10A, 10C = 0x0B50; post: 10B, 10D = 0x0B50 |
| 0E9, 0EB | `FUN_0003C9C4` (boot), `FUN_0003CBBC` (diagnostic, 0x4000) | none | the output gains of test image 1 |

**Part sends never touch VOP3-2.** `FUN_00019362` cases 0x11..0x14 and `FUN_0001FF90` / `FUN_0001FFC6` write the part-to-effect sends and dry level to the YMP706 (events 0x222..0x224 through `FUN_000224F8`, table 0x35C006). With a part's Insertion switch on, its reverb and variation sends are muted at the tone generator. So the tone generators deliver mixed dry and send buses, and VOP3-2 adds the effects, returns and output gains.

**The reverb window always holds `reverb_00`.** `DAT_0106840E`, the reverb program index `FUN_00039C8A` reads, has one writer in normal use, `FUN_0003AAC8`, which sets it to 0. That is why `FUN_0003BBBA` can patch fixed steps 10A..10D inside the reverb window: in `reverb_00` they are the individual-out block.

## 3. The output steps

The assembled program (base, `reverb_00`, `ins1_00`, `ins2_00`, `variation_00`), in the disassembler's notation:

```
0cf  y = k*r[1d]          m[10] = y     main L, k = performance volume
0d0  y = k*r[29]          m[11] = y     main R
10a  y = k*r[07]                        individual L, pre insertion (k = 0x0B50 or 0)
10b  y = s + k*r[11]      m[28] = y     individual L, post insertion
10c  y = k*r[08]                        individual R, pre insertion
10d  y = s + k*r[12]      m[29] = y     individual R, post insertion
```

m cells 0x10, 0x11, 0x28 and 0x29 are written at shift x16 and read by no other step. Base writes 0x10 / 0x11 and every `reverb_0x` image writes 0x28 / 0x29, so every combination of programs has all four. `r[11]` and `r[12]` are the insertion's send registers (`vop3_isa.md` section 5).

## 4. From cell to serial output

A step with `sout` (`w4[6]`) puts m[sm] on the serial output slot of its page. The shipped program has four such steps, sending exactly the four output cells:

| step | sends | output slot | pair |
|---|---|---|---|
| 002 | m[10] | 0 | main L |
| 10e | m[11] | 8 | main R |
| 045 | m[28] | 2 | individual L |
| 156 | m[29] | 10 | individual R |

`sout` is rare in every image, and each image's set matches what the chip has to drive:

| image | `sout` steps (step: cell) |
|---|---|
| FS1R VOP3-2 test images 0 and 1 | 002: 0c, 042: 0c, 102: 0b, 142: 0b |
| FS1R VOP3-2, assembled | 002: 10, 045: 28, 10e: 11, 156: 29 |
| FS1R VOP3-1 filter | 16 steps, one per filter channel |
| PLG150-AN modes 0..2 | 004: 1a, 041 / 043: 20, 082 / 083: 22 |
| AN1x voice | 004: 03, 046: 1b, 085: 07, 100: 2e, 141: 2f, 182: 07 |

Sixteen on the filter, which returns sixteen channels to the tone generator, and four on the effects chip, which drives two stereo DACs. On the AN boards the sent cells are the ones the voice's final mixer writes (PLG150-AN base 1fc..1ff, AN1x voice 1fb..1ff).

**Evidence.** On test image 1 (**CHIP**, session 11, recorded off the main DAC): clearing 002 or 102 changes the output, clearing 042 or 142 does not, and clearing the steps that compute into the cells (0e9 / 0eb) does not either while their constants still set the gain. So the `sout` step is what puts a cell on the port. The core runs the shipped programs on this rule and the main pair plays.

## 5. Open

* Which board pin each output slot drives. The board connects SDO1 and SDO3 to the DACs; nothing in the firmware names pins, and the individual pair has not been recorded on the unit.
* The meaning of `CONFIG` entries 1..14 and of 0x28..0x2A.
