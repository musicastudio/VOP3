# VOP3 (YSS236) research journal

The measurement history of the VOP3 instruction set, sessions 8 to 41: what each rig session probed, what it
found, and the readings later sessions withdrew. It was `docs/vop3_isa.md` until 2026-10-15; that file is now the
reference (what the chip does, as the engine runs it), and this one keeps how it was found. Nothing here is
deleted or corrected after the fact: a withdrawn reading stays where it was written, followed by the session that
withdrew it.

**Field names below are the session-era ones** (`r6 r9 r8 r7 r10` words, `class`, `rA`/`rB`, `op`, `route`,
`wdst`, `daddr`, `d[]`, `w[]`), as `tools/vop3_disasm.py` still prints them. `docs/vop3_isa.md` appendix A maps
each to the field the core decodes. Where the two disagree (for example session 15's "`r7[11]` rectifies", which
the core reads as `w3[15:14]` = 3), the core is what the unit was last checked against.

Later sessions, written after this file was split off, are at the end: 40 (`docs/vop3_an_ram.md`) and 41.

---

# VOP3 (Yamaha YSS236) instruction set, as measured

Reference for `tools/vop3_disasm.py` and `tools/vop3_interp.py`. Every claim names its evidence:
**FW** = read from the FS1R, PLG150-AN or AN1x firmware; **CHIP** = measured on rgwan's FS1R through the
debug monitor (FS1R.unlock `captures/2026-09-30-9`, `-10`; per-take tables in those READMEs). Anything
not marked is an inference and says so. The disassembler's output over every image we have is in
`docs/vop3_disasm/`: FS1R VOP3-1 (filter, 2 variants), FS1R VOP3-2 (base + 4 reverb + 16 variation +
2x12 insertion + test images 0 and 1), PLG150-AN (3 voice modes + base), AN1x (voice + boot, parameter-labelled).
Each line gives the raw fields, then the step as its measured operation (`y = s + k*(r[31] + r[30])`,
`dram[34] = y`, `d[0b] <- xfer(slot 36, else 37)`, ...); `python tools/vop3_disasm.py --listings` regenerates them.

## One chip

**VOP3-1 and VOP3-2 are the same chip; a result measured on either applies to both** (and to the PLG150-AN's
and AN1x's VOP3). No measurement on one contradicts one on the other. Where this doc gives a field two
descriptions (`path`, `r9[12]`, op table columns "VOP3-1 filter" vs "VOP3-2"), they describe the same
behaviour in two contexts: a step inside the FS1R filter's feedback loop, where a change closes, opens or
runs the loop away, and a step in VOP3-2's feed-forward test chain, where the same change reads out as plain
arithmetic. The one board difference is external: the FS1R's VOP3-1 has no DRAM attached, so its programs
carry no `mem` marks. The disassembler and interpreter decode every image with the one set of meanings.

## 1. The machine

* A program is **512 steps**, run in order once per sample. FW: every uploader (FS1R `FUN_0000BC8C`,
  PLG150-AN `FUN_0007f87c`, AN1x `FUN_00056028`) writes 512 rows.
* A step is **five 16-bit words**, chip registers 10, 9, 8, 7, 6 (`r10 r9 r8 r7 r6` here), plus a
  **constant** in register 0xB and a **tag byte** in register 0xC, all addressed by the step number in
  register 0. FW: all three uploaders. The AN1x's image packs the tag into the `r10` word
  (`r10 | tag<<4`), so it is part of the instruction, not a side table.
* **Register 0 is an address latch shared with everything.** The FS1R's 192 Hz tick writes cutoff
  constants through it with interrupts masked (`FUN_0000B6A4`); a step written from outside that
  critical section lands on whatever step the tick last named. CHIP: two takes lost to exactly this.
  The only safe write is the firmware's own patch path from the CPU shadow (`FUN_0000B600`).
* State the word addresses: a **register file `r[0..0x7f]`** (7-bit addresses in `r9`/`r6`), a
  **write area `w[0..0x3f]`** (6-bit addresses in `r6`/`r7`, class-2 results, read back through `rsrc`),
  **data memory `d[0..0x1ff]`** (9-bit address in `r8`, one entry per step: the constant table seen as
  memory, used for lookup tables), and on chips with DRAM a **delay memory** by slot (`r10[2:0]` marks
  the access, slot = step >> 2, an 18-bit offset in registers 0x0D/0x0E per slot at upload; the AN drivers'
  `| 0x1a` / `| 0x1c` are the byte offsets of the same two registers). FW: AN uploaders, FS1R VOP3-2. On the
  AN boards the same port reads **lookup tables**: the PLG150-AN's H8 loads 0x6121 words into it at boot
  (`FUN_00081358`, registers 0x0F..0x12), the AN1x wires a mask ROM there, no AN program writes it, and every
  AN access sets `r8[5]`, which no FS1R access does. CHIP (session 40): `r8[5]` makes the access, read or write, address the RAM at the slot offset without the ring pointer; marks transfer on every phase from step 3 on; a capture takes a transfer 5..11 steps back (`docs/vop3_an_ram.md`).
* **`r[]` is one 7-bit space laid out per voice, and `w[]` is not its upper half.** FW
  (`tools/vop3_verify.py`): FS1R filter channel c of group g reads exactly the register the same-position
  channel of group 0 reads plus 24g, or a group-shared one (444/444 reads), so group 2 reads `r[3b..4d]`
  where group 0 reads `r[0b..1d]`, straight through 0x40; the AN1x reads `r[1 + 5k + v]` for voice v with one
  step shape per k for k = 0..12 (`r[01..41]`). A "97% of reads at or above 0x40 name a `w[]` slot some
  step writes" count also holds, but it is a coincidence of two dense files and the stride test overrides
  it: `w[3e]` (the cutoff MAC's slot) is not `r[7e]`. Below 0x40 on the AN1x nothing writes `r[01..3f]`
  (no driver path either: the only sub-0x40 targets are class-1 loads to `r[00]` and the boot register
  block `0x24..0x2a`), so they are hardware inputs: the serial ports and internal generators fill them, the
  program reads them. On the FS1R the class-1 control word of channel c (`100`: k=8000 mutes) lands in
  `r[6c]` and nothing reads it: the hardware consumes it. `r[]` is the chip's per-voice I/O window plus the
  class-1 constants; `w[]` is the program's own scratch.
* The FS1R filter runs **four channels interleaved in a 124-step group**: channel c owns steps
  `base+3c..base+3c+2` (gain, mode, cutoff constant) and every step `base+0x0c+c+4n`; four groups from
  0x010 cover sixteen channels. CHIP: clearing any of a channel's 31 steps changes its output,
  clearing any other step of the group does not.
* **The register file is banked per filter channel** (inferred): clearing channel 4's cutoff load
  `08e` closes channel 4 although `012`, `097`, `10a`, `186` load the same `r[61]` with the same word
  in the same pass; the bank must follow the step's position. `tools/vop3_interp.py` models it as
  sixteen banks selected by the interleave.
* A filter type change does not re-upload: the firmware patches steps into the shadow and uploads
  those (`FUN_0000D050` -> `FUN_0000C6C0` -> `FUN_0000B600`), muting the channel around it via
  register 0x2B. CHIP: LPF24 patches two steps per channel (`r7 40d1 -> 50d1`, `r6 low bits`).

## 2. The word

```
r6  [15:14] class   [13:7] rB              [6] -      [5:0] wdst
r9  [15] mod        [14:13] -   [12] ?     [11:8] path   [7] mod   [6:0] rA / load destination
r7  [15:14] -       [13:12] route   [11] ?   [10] sel   [9] mod   [8:6] op   [5] -   [4] rd-en   [3:0] rsrc
r8  [15:7] daddr    [6:0] mmode
r10 [15:3] ?        [2:0] mem
```
`-` = inert on every step it was flipped on (CHIP). `?` = never probed.

| field | role | evidence |
|---|---|---|
| `class` `r6[15:14]` | **1** constant load; **2** compute; **0** no output from this step; **3** loop runs away | CHIP: 1->0 and 2->0 silence the step's contribution, 2->3 runaway; on class 1 every other bit of r6 is inert |
| `rA` `r9[6:0]` | class 2: source register A, **added to `r[rB]`** when rB is set, ignored when rB is 0 (sessions 14/15); class 1: **destination register** of the constant | CHIP: VOP3-1 exact-match on every bit; VOP3-2 `x = r[rA] + r[rB]` (session 15) |
| `rB` `r6[13:7]` | class 2: **source register B**: `x = r[rB]` (+ `r[rA]`); 0 = the running value | CHIP: VOP3-1 exact-match; VOP3-2 sessions 13-15 |
| `path` `r9[11:8]` | exact-match per data path on VOP3-1's feedback loop (1..0xb across a group); inert on a feed-forward step | CHIP: VOP3-1 all 15 other values close the filter; VOP3-2 `0d3` 0..15 gain 1.000 (session 12) |
| `op` `r7[8:6]` | **3-bit operation**, section 3 | CHIP: swept 0..7 on two steps |
| `sel` `r7[10]` | **negates the running value** `s` (op 0 `-s + k*x`, op 4 `-s`; ops 1..3 unaffected) | CHIP: VOP3-2 session 12; VOP3-1's opened filter / constant output are the same sign flip inside the loop |
| `route` `r7[13:12]` | **scale of what the step writes out** (DRAM / bus): 3 = 1, 2 = 1/4, 1 = 1/8, 0 = 1/16; the value handed to the next step is not scaled; op 7 on route 3 gives 0 | CHIP: VOP3-2 sessions 12/14 |
| `rd-en` `r7[4]`, `rsrc` `r7[3:0]` | **with rB set: drops the gain `k`** (`y = x`); `w[rsrc]` never enters the arithmetic; with rB clear: inert | CHIP: VOP3-2 sessions 12/14; VOP3-1 `0f8` (session 9) |
| `wdst` `r6[5:0]` | **write slot** `w[wdst]` of the step's result; `rsrc` reads the same slots | CHIP: exact-match, 55/63 alternatives run away, bit-3 neighbours retune |
| `mod` `r9[7]`, `r9[15]`, `r7[9]` | modifiers: `r7[9]` ignored on the MAC probed; `r9[7]` and `r9[15]` share one signature (partial loss) | CHIP |
| `abs` `r7[11]` | **rectify: `y = abs(y)`** | CHIP: VOP3-2 session 15 (0.9997 x abs(output), residual -51 dB) |
| `r9[12]` | inert on a feed-forward step; one flip opened VOP3-1's filter | CHIP: VOP3-2 session 15 (gain 1.000, -62 dB); VOP3-1 session 8 |
| `daddr` `r8[15:7]` | 9-bit data-memory address. On the FS1R filter: lookup-table pointers into other steps' constants. On VOP3-2: `d[]` cell `n` = bits 6:0; bit 8 set addresses `d[]`: bit 7 = 1 the step writes `d[n]` (its route-scaled output; op 7 reads it), bit 7 = 0 **captures** into `d[n]` the DRAM transfer of slot s - 2 (else s - 1, else it holds) | FW: table indices; CHIP: VOP3-2 session 16 (`d[10a]` / 0e8 captures freeze, 0e0 with slot 36 empty takes slot 37, R == L bit-exact) |
| `mmode` `r8[6:0]` | inert on every step tried (VOP3-2 sessions 12 and 16, the DRAM read and write included) | CHIP |
| `mem` `r10[2:0]` | **delay-memory access, only on steps 3 mod 4, slot = step >> 2:** 1 = write the step's route-scaled output; 2, 3 = read the word written N passes ago (N = this slot's offset - the writing slot's); 4, 6 = one pass older; 0 = none. Offsets (reg 0 = slot, 0xd/0xe = 18-bit offset) index one 2^18-word ring whose pointer steps once per pass | CHIP: VOP3-2 session 16, lag = N to the sample for N = 0..16385, both-offsets shift inert, 2^14..2^16 + 17 no alias, off 0x3fff1 reads the other line 2^17 - 15 back; FW: test image 1 |
| constant (reg 0xB) | class 2: signed 1.15 **gain**; `rd-en` with rB set drops it (and `w[rsrc]` does not enter), with rB clear it stays (session 14). Class 1: the loaded value, 8.8 fixed point (`r = v/256`, session 14) | FW: the firmware patches k into class-2 steps on all three machines (FS1R gain `x120`, mode `x7c`, resonance A/B, type; AN1x mixer levels `x67`, pitch, PW, edge, sync; 69 of 69 AN1x handler targets and 112/112 FS1R targets are class 2 with `rd-en` clear), and a live constant on an `rd-en` step is rare: FS1R 0 of 240, AN1x 20 of 276 (7%, against 86% of the others), PLG150-AN 24% against 43%. So the read replaces the constant on the FS1R and nearly always on the AN; where both are present (the AN's 0x35c3 mixer group) the step has four sources and the MEG's `const * reg + reg` form is the reading. CHIP: session 9's 13 values on `0f8` changed nothing, and `0f8` is an `rd-en` step, so the two agree; the class-1 mute above 0x8000 stands. CHIP (VOP3-2, session 11): on the output step and the DRAM writer the constant is a signed 1.15 **gain** (linear, 0x4000 = half scale, sign follows k); with a zero input every op 0..7 and every k gives exact digital zero, so it is never an addend |
| tag (reg 0xC) | write-group id the firmware uses to patch steps by parameter: 3..7 = voice 0..4 on the AN1x, 1 = global; 0/1 on the FS1R filter | FW |

## 3. Operations (`op` = `r7[8:6]`)

| op | on the cutoff MAC (`098`: rA=62 rB=61 -> w[3e]) | at the output tap (session 10) | MEG counterpart | VOP3-2 `0d3`, measured gain on real audio (session 11 `held`; s = running value, k signed 1.15) |
|---|---|---|---|---|
| 0 | runaway | passes rA | `p =s ... + p` accumulate | **y = s + k*s** (accumulate the product) |
| 1 | closed | floor (~0.02): does not pass rA; 100% carry a read | `p = m` / `p = r` forward move | **y = k*s** |
| 2 | **corner follows rA x rB** | passes rA | `p = c * r` multiply | **y = k*s** |
| 3 | closed | passes rA | `p = (c<<8) + (p>>15)` mul-acc | **y = k*s** |
| 4 | closed | passes rA | mul-acc with memory | **y = s**, k ignored |
| 5 | runaway | floor (~0.02): does not pass rA; lowest read rate | `p = c * r` fresh product (discards accumulator) | s shifted out: output is the sign only ({-2^-17, 0}), k ignored |
| 6 | closed | passes rA; 100% carry a read | accumulate-forward | 0 |
| 7 | closed | passes rA | shifted mul-acc | 0 (op 7 starts a chain; the running value is not its source) |

**Session 12 (VOP3-2, `captures/2026-10-01-0414-s12/README.md`) closes:** the output word is 18 bits
(LSB 2^-17); the pipeline lag is 2 samples; `sel` negates the running-value term (op 0 `-s + g*x`, op 4
`-s`, ops 1..3 unchanged); `route` scales the step's output 3 : 2 : 1 : 0 = 1 : 1/4 : 1/8 : 1/16; op 7
reads the input at any step (session 11's zero at `0d3` was on route 3); `path`, `r8[6:0]`, `r10` and the
DRAM offset registers are inert on a step of a program without a delay line.

**Session 13 (`captures/2026-10-01-0441-s13/README.md`) closes the operand files:** rA/rB address `r[]`,
never `w[]` (w[n] loaded with 0.5 x input reads the same through rB = n or 40|n as with nothing
loaded; `w[]` is reached only through `rd-en`/`rsrc`). A class-1 load lands in `r[]` and op 1 reads it
through rB (`r[30]` = 0x4000 -> a constant, zero-variance output). (Its "live `r[]`" reading is withdrawn by
session 15, below.)

**Session 14 (`captures/2026-10-01-0454-s14/README.md`), class-1 loads as known operands:** a class-1 load
is 8.8 fixed point (`r = v / 256`); the running value / DRAM word clips at +-8 (seen at two output gains);
the running value carries from step to step unscaled by route (op 0 `s + k*x` gives the same 4.000 at
`0d1`, `0d2` or `0d3`); op 1 reading `r[rB]` gives `x + k*x`; `rd-en` with rB set drops `k` and never
brings `w[rsrc]` in (`x` alone, 4.000 for w[0e] live or w[0f] unwritten), with rB clear `k` stays (session
12): VOP3-1's `0f8` (session 9) and VOP3-2 agree. op 0 ignores a loaded `r[rA]` alone.

**Session 15 (`captures/2026-10-01-0509-s15/README.md`) closes the operand and the remaining fields:**
with rA and rB both set the operand is the **sum** `x = r[rA] + r[rB]` (r[30] = 1, r[31] = 0.5/1/2, k = 0.5:
ops 0, 2 store 0.75/1.00/1.50, op 3 1.00, op 1 3.00); op 1 with rB is `x + k*x` (k = 0xc000/0/0x4000 store
0.5/1.0/1.5); op 0 ignores rA alone, loaded or not; `r7[11]` rectifies (`abs(y)`, 0.9997, residual -51 dB);
`r9[12]` is inert. Unloaded `r[]` registers hold constants left by earlier programs (96/127 zero, the rest
fixed values, none carries audio). A step's result reaches the next step in the same pass: there is no
write delay; when the chain head is missing the next step reads the value the previous step in program
order left (the other chain's end), which is what sessions 12/13 saw as a "live" register.

**On a step with no feedback the arithmetic is readable** (last column; VOP3-2's DRAM writer `0d3`, every
fit a pure gain of the same signal, residual equal to the reference's): the constant only multiplies, op 0
adds the product to the running value, ops 1..3 replace it with the product, op 4 passes it, op 5 shifts it
out. With the note off every op and k gives exact zero (take 2), so no op loads or adds k. Whether ops 0..3
multiply the running value or rA/rB is not split yet (on `0d3` they are the same signal).

**On VOP3-1 the op's arithmetic cannot be read from one probed step.**
Session 10 used the output stage's op-1 move as a DC voltmeter on a silenced channel; the tap is
AC-coupled, so `dc` read converter offset (-2e-5) on all 122 segments and the DC plan is dead. What the
level *did* show: the output passes `rA` alone, identically for ops {0,2,3,4,6,7}; ops 1 and 5 drop to a
fixed floor (they start/forward without passing the probed operand); `rB`, the read source, `path` (bar
the reserved slot 3), and every remaining `r7/r9/r10/r8` bit are all inert on a pass step. The op only
manifests through the recursive filter loop, which the FS1R never exposes. The MEG column is the close
(see section 7): the same operation *mix and ordering*, on a chip whose ISA MAME decodes to explicit
arithmetic. The FS1R program uses ops {0, 2, 3, 4, 5, 7} on class-2 steps, the AN1x {0..7}.

## 4. The FS1R filter's cutoff path, channel 0, read off the chip

```
08c  class 2  op 5  -> w[13]  rB=7f                  gain term: cleared, the loop runs away (+83 dB HF)
08d  class 2  op 4  route 1 -> w[05]                 mode: cleared, the resonance peak moves to 80-160 Hz
08e  class 1  r[61] = k                              staged cutoff constant (log frequency, tick-driven)
098  class 2  op 2  path 1  rA=62 rB=61 -> w[3e]     the MAC whose result the corner follows
09c  class 2  op 2  path 1  rA=62 rB=62 -> w[3d]  rd w[1]
0a8  class 2  op 2s        rA=62 rB=61 -> w[3f]      the resonance peak (cleared: -19 dB at 320-1280 Hz only)
0c4..0c7, 0d4..0d7                                   the two biquad sections: a state write (silence when
                                                     cleared), a feedback term (+19/+24 dB blow-up), a runaway
0e8  op 5  rA=1b rB=1b -> w[17] rd w[1]              state write (silence when cleared)
0f0  op 1  sel  rB=1c -> w[23] rd w[1]               output stage, bus A (-16 dB flat when cleared)
0f8  op 1  sel  rB=1d          rd w[1]               output stage, bus B (-16 dB flat when cleared)
100  class 1  r[1e] = k (k=8000)                     control word: a negative value mutes the output stages
```
Channel c adds c to every step address from 0x098 on and 3c to the first three, with its own registers.
The cutoff-byte-to-corner law is `docs/filter.md` (`15_filter`).

## 5. AN1x parameter labels

`docs/an1x_param_map.md` (from the scene-parameter table at `0xCFCEC`) names, for each of 56 knobs,
the steps whose constant it rewrites; `docs/vop3_disasm/an1x_voice.txt` carries them. Cutoff is a
class-1 load followed by a `6000 0004 3040 4000` step (class 1, `r9[14:13]` = 3, route 3, op 1, sel; 20 per
AN1x voice program, 7 on the PLG150-AN, after every sync/cutoff load: a log->linear conversion, as the EX5
MEG program does with an exp table); VCO pitch 4096/octave with a key term; mixer levels `level*0x67`.

## 6. Interpreter

`tools/vop3_interp.py` runs a program with the arithmetic measured on VOP3-2 (sections 3 and 8; the same
chip as VOP3-1). Class 1: `r[rA] = v/256` (8.8). Class 2: gain `g` = `k` (signed 1.15), 0 when `rd-en` and
rB are both set; running value `s` (`-s` with `sel`), one register passed step to step in program order;
operand `x` = `s` when rB = 0, `r[rB]` when only rB is set, `r[rA] + r[rB]` when both are; op 0 `s + g*x`,
op 1 `g*x` (`+ x` when rB is set), ops 2/3 `g*x`, op 4 `s`, op 5 sign of `s`, op 6 zero, op 7 `g x` the
chip's input (0 on route 3); `r7[11]` takes `|y|`; every result clips at +-8. `wdst` writes into `w[]`;
route 0 of an op-1 step goes onto the output bus. Its self-check reproduces session 11's `held` table and
`dc dram` silence, session 12's `sel` gains, session 14's eight stored values, session 15's sum, op-1 and
`r7[11]` results, and session 8's cutoff load (clearing it zeroes the MAC) and per-channel step ownership.
The delay memory is modelled from session 16: `r10` read/write on steps 3 mod 4, slot offsets into one
2^18-word ring stepping once per pass, route-scaled writes, `d[]` captures of slot s - 2; its self-check
reproduces test image 1's lags (N - 1 against image 0 for N = 0, 1, 2, 17), the moved read that plays the
left channel bit-exact, and the other line reached 2^17 - 15 back. Not modelled: the 2-sample output lag and
the 18-bit output word (fixed properties of the DAC path, not of a program).

## 7. Model check against the EX5 MEG (the close for the op arithmetic)

The FS1R VOP3 and the EX5 SWP30 MEG run the *same* AN algorithm; MAME decodes the MEG to explicit
arithmetic (`docs/ex5_meg_an.txt`, the AN program, 254 steps), so it is value-transparent where the
FS1R is not. `tools/vop3_meg_check.py` classifies every step of both into one vocabulary
(multiply / accumulate / load-constant / move / memory / lookup) and compares:

* **Operation mix matches.** MEG: 82% multiply-class, 4% load-constant, 13% memory. VOP3 AN1x voice:
  63% MAC-class, 7% load-constant, 19% memory. Both are MAC-dominated with a small constant-load
  population and a comparable memory fraction.
* **Step count is ~2x** (VOP3 512 vs MEG 254): the FS1R runs the engine at a higher internal rate.
* **The op-1/op-5 pairing reconciles.** Session 10 found ops 1 and 5 do not pass the probed operand at
  the output tap. In the VOP3 program op 1 and op 6 carry a read 100% of the time (operand-forward /
  accumulate, MEG's `p = m`), while op 5 is the largest op with the *lowest* read rate (a fresh product
  `p = c * r` that discards the running accumulator, MEG's most common op). So op 5's "does not pass the
  preloaded a" is exactly a new-product-start, and op 1's is a forward-move: both consistent with the MEG.

This validates the operation field as a genuine ALU-op selector with the MEG's operation semantics,
without needing the FS1R accumulator to be observable. Reproduce: `python tools/vop3_meg_check.py`.

**Dataflow check (`tools/vop3_verify.py`).** The MEG listing passes the trivial test (every `r`/`t` read has
a writer in the program, 260/260); the VOP3 images are tested for what their firmware and layout prove:
the constant is live on class-2 steps and dead on `rd-en` steps, `r[]` has the per-voice stride above
(FS1R 444/444, AN1x 13/13), which is what pins `w[]` as a separate file. An earlier reading of `w[n]` as
`r[0x40 | n]` passed a writer-coverage count at 97% and was wrong; the stride test is the one to trust.

## 8. Open

**The decode does not yet run a shipped program** (full list: `docs/vop3_blockers.md`). Every field above is measured on a feed-forward test chain, and `tools/vop3_gaps.py` runs the shipped programs through the interpreter and asserts three gaps that block a C++ rewrite from the decode:

* **VOP3-1 has no memory.** An impulse on any register filter channel 0 reads and does not load leaves in the same pass with no tail, so the filter as decoded has no pole. Routing class-2 results back into `r[n]` or `r[40|n]`, same pass or next, changes nothing. The state path is undecoded; `path` (exact-match on the feedback loop, inert feed-forward) and class 3 are the candidates.
* **VOP3-2 reaches no output.** Base image plus reverb 0, variation 0 and insertion 0/0, audio on the input port, every constant 0.5: zero non-zero output samples in 4096. Only test image 1 names its output steps (0e9/0eb); which steps of the shipped program feed the DAC is unknown.
* **Class 3 is not decoded** and carries the reverb: 16 of reverb 0's 29 live steps, 27 of the assembled VOP3-2 program, 32 of VOP3-1.

The per-type constants VOP3-2 runs with are not in the images either: `FUN_00039652` writes register 0xB from `DAT_0106842c`, which the effect parameter handlers fill.

Session 16 (`captures/2026-10-01-0535-s16`, test image 1) closed the delay memory: the `mem` and `daddr`
rows of section 2 and the interpreter's DRAM model are its measurements. Recorded but not modelled, because
no shipped program uses them: a capture step at 0dc..0de or 0e5 (R == L at lag 0 / +1 on test image 1) and
0e0 turned into a `d[18b]` step write (0e9 at 0.42 x L).

**Sessions 17-19 (remote rig, 2026-10-01; folders on the rig under `~/FS1R.unlock/captures/2026-10-01-17*`):**

* **op 0 with rA and rB both set, below route 3: rA is a destination, not an operand.** x = r[rB], y = s + k x,
  and r[rA] = y (scaled x16 by route, as the d[]/DRAM writes are: route 0 x1, route 1 x2). With rA = rB this
  is a recursive integrator: r[32] settles to s / (1 - k) at route 0 (s = 0.375 / 0.5, k = 0..0.25: 0.375 /
  0.3999 / 0.4285 / 0.5 and 0.5 / 0.5333 / 0.5714 / 0.6666, all to 1e-4) and 2s / (1 - 2k) at route 1
  (s18: s = 0.25, k = 0.25 / 0.125: 1.0 / 0.667). The
  register keeps its value across passes: this is the chip's state, the comb's damping pole (base 0a5) and the
  missing VOP3-1 filter memory. Route 3 still sums (session 15). Interpreter: in, with its self-check.
* **Class 3 is class 2 at unity gain with rA as destination.** k is ignored; op 0 gives s + r[rB], op 1 2 r[rB]
  (x + x), ops 2/3 r[rB], op 4 s, op 5 r[rB] (not sgn); r[rA] and the running value both take y (s19,
  s = 0.5, r[33] = 0.25, all six exact). Clearing the reverb window's 16 class-3 steps silences the reverb
  (s17 `c3_off`, R exact zero). Interpreter: in.
* **rd-en with rB set: the gain is a 16-entry file G[rsrc], not k and not zero.** Op 2, x = 1.0, k 0.5:
  G = 0.99988, 0.10144, -0.07422, 0, -0.33203, 0, 0.99988, 0, -0.50757, 0, 0, 0, -1, -1, 0, 0 (rsrc 0..f),
  linear in x (x = 0.5 halves it), the same on ops 0/2/3; op 1 adds x (1.10144). A step writing w[02] in the
  same pass does not change G[2], so G is not this pass's w[]. Its writer is open (blocker 4 narrowed):
  the values are what the effect programs left. Interpreter: the measured table, as a constant.
* **Class 1 with rB set adds:** r[rA] = k/256 + r[rB], r[rB] unchanged (2.0 + 0.5 = 2.5, both r7 forms).
* **Comb 1 (Hall1, time 0.8 s) on the unit:** period 4974 samples = read slot 22 - write slot 2a (+1000 on
  either moves it by 1000, slot 28 inert), echo ratio 0.412 = the loop 2 (x3 + x4 z^-1) / (1 - 2 p z^-1) of
  0a3/0a4/0a5's words at its peak (`tools/vop3_comb_check.py`, five segments). Zeroing 0a9's word kills the
  comb (0a9 is the input gain).
* **Off-phase DRAM marks are live:** the write moved to phase 2 still writes; to phases 1 / 0 it changes the
  line (gain 1.1, lag 0); a read on phase 1 also reads. r10 = 5/6/7 on the write and 5/7 on the read are
  not the shipped meanings (uncorrelated / unchanged).
* **Output path (s17 `walk`, Hall1, noise held):** the dry note (L, part panned hard left) drops 25-35 dB
  only when 002, 0e0, 0e7 or 0ea is cleared; the reverb (R) goes to exact zero only when 0d5 or 0d9 is cleared.
  Clearing 12d left R dead for the rest of the take (state latched at zero), so steps after 12d were not
  measured on R, and the note clipped L near 0 dBFS. Needs a re-walk at a lower level with a state reset.
* **EQ frequency:** the dumped coefficients evaluated at 48 kHz fit the measured responses better than at
  44.1 kHz on all three (1.37 / 1.45 / 1.38 dB rms against 1.42 / 1.55 / 1.60), the measured peaks sitting
  at 539 / 1131 / 2150 Hz for words designed at 500 / 1000 / 2000 Hz at 44.1 kHz. Leaning 48 kHz; the
  fit is not decisive.

**Sessions 20-23 (remote rig): VOP3-2's buses, and a reset that needs no power-cycle.**

* **Output:** the left / right DAC words are r[09] / r[0a] after the last step (a class-1 load of 0.25 into
  either at the free step 10f reads 0.5 x 1/16 on that channel only; DAC = r / 8). The base program writes them
  last at 0e7 / 0df (op 0, route 2, rA the destination).
* **Inputs (s22, all 127 registers read into L per bus configuration):** the reverb send pair arrives in
  r[03] / r[04] (and r[57..5c]), the variation send pair in r[05] / r[06] (and r[67..6c]), the insertion pair
  in r[07] / r[08] (and r[43..4e]); each at the same level (-49.5 dB on a -42 dB send). The dry part reaches
  the output already mixed: with the effects off, r[09] / r[0a] and the output-EQ chain r[23..2a] carry it at
  the DAC level, 1-3 samples ahead. So the chip's audio inputs are register writes by the hardware before the
  pass, which is where the "read, never written" registers of the program come from.
* **Reset:** FUN_0003AAC8 (the boot's VOP3-2 sequence: init, every effect handler, programs, unmute) plus
  all-notes-off clears latched state; FUN_0003A064 + FUN_0003A110 alone leave the chip muted.
  FS1R.unlock `tools/vop2_reset.py`.
* **End-to-end (s23):** the effect input r[03] (or r[05]) and the output r[0a] recorded together for reverb
  0/1/9/13/16 and variation 1/9, with the constants, offsets and selectors dumped. The interpreter fed that
  input does not reproduce the output yet: the reverb program saturates (+-8) inside the comb section and
  r[0a] stays 0. That run is the acceptance test the C++ rewrite needs; it is not passed.

**Session 24 (remote rig, rounds 1-4, 80 DC probes; `docs/vop3_probes.json`, `tools/vop3_probe.py check`).** One
rule set reproduces 79 of them and session 11's op table; it replaces the session 18/19 readings above (they were
the special cases rA = rB and route 0 of it):

* x = r[rB]; **rB = 0 names r[0], the chip input**, not the running value (rB 0 on a silent input gives 0 on
  ops 1/2/3; op 0 then passes s).
* y = op(s, k, x), then **y is scaled by the route, x1 / x2 / x4 / x16 (route 0..3)**, and s = y.
* **rA != 0 writes r[rA] = y** on every class-2/3 step, any route (route 3 included: the session 15 "sum"
  was this write plus the next step's read).
* Class 3 is class 2 with k = 1. Class 1 loads r[rA] = k/256 plus r[rB] (rB set) or plus s (rB 0), scaled by
  the route, and s takes the value.
* Open: one probe (`G5_op6_r30_r`, class 1 r7 = 0x1180, the shipped op-6 load form) stores -8 where the rule
  gives +8; and nothing tried writes the rd-en gain file G[] (op-6 loads with wdst 5/7, a plain load and a
  class-2 step with wdst 5: G[5] stays 0).

**End-to-end (`tools/vop3_e2e.py`, session 23's recorded input):** still 0 on r[0a] for var1, rev1, rev9; the
insertion-1 window saturates within one pass (its steps lean on G[12] / G[13] and the op-6 loads, both open).

**Sessions 24-27 (remote rig): number formats, latency, z^-1 registers, the real DAC taps.**
Measured on the unit; every rule is asserted by `tools/vop3_interp.py` (349 probes in `docs/vop3_probes.json`)
or `tools/vop3_ship.py` (shipped program, `docs/vop3_ship/`, 41 of 41).

* Registers hold [-128, 128) (word / 256); the running value s is [-256, 256). r7[15:14] picks the overflow
  rule: 0 wraps, 1 saturates, 2 clamps negatives to 0, 3 takes |y|.
* A register written at step n is readable from step n + 3; steps n + 1, n + 2 see the old value.
* Class 1: r[rA] = k/256 + s (rB does not replace s), times the route.
* Op 5 with rB set (class 2): max(r[rB], 0), k only gates; the comb input steps use it.
* rd-en gain file G[rsrc]: a fixed table (same on a blank image and under the insertion program; no step writes it).
* rA with bit 7 set (word 1 bit 7) is a z^-1 write: the register gets the previous step's x operand (r[rB] of
  the step before), not this step's result; after a step with rB = 0 it gets its own x. The base program's
  biquads (066-074, 0b6-0c4) keep their delay state this way.
* d[] holds at least +-32 (it is not clipped to the DRAM word's +-8).
* The DAC plays d[0x10] (L) and d[0x11] (R), written by base steps 0cf / 0d0 (route 3, x16); unit readout =
  d / 4 in the s25 units (DAC full scale = d 32). Session 22's "r[09] / r[0a] after the last step" was only
  what the base program reads into them. Steps 06a / 0ba are where the output biquads read r09 / r0a.
* Input registers change mid-pass: r0e tapped at 0x010 and at 0x110+ differ by one sample (boundary between
  0x108 and 0x110), r0d by two; the external (VOP3-1 to VOP3-2) bus writes land partway through the pass.
* Still open: probes that break the output biquad loop (06a-06e, 0b9-0bb) settle at +-8 on the unit with a
  sign the model does not reproduce (unstable loops, history-dependent); the end-to-end replay needs a fresh
  paired take using the tap method above and the mid-pass input timing.

**Session 28 (remote rig): step responses on the shipped programs, sample-exact.**
`tools/vop3_step.py`, `tools/vop3_seq.py`; kept-patch probes (s25 runner `keep`), data in FS1R.unlock s28 folders.

* Base program inputs (steps 000-007) re-pointed at a loaded register and stepped 0 -> 1/16: the dry path of
  every shipped program (none, Hall1, variation 1, Hall 9) matches the model to the bit: DAC word 18-bit,
  value floor(d / 4 x 2^14) / 2^14 (readout units), R leaves the chip in the pass the input changes, L one pass
  later (`Interp.dac()`).
* 24 register taps (0cf re-pointed) on Hall1 with the step held: all match the model.
* The reverb tail does NOT appear in the model: on the unit, L (tapped at r77/r78/r1d/r29) starts moving
  1346 / 1473 samples after the step (r37/r3c: 5112 / 5619), with exact first samples 0.000977, 0.002319,
  0.002869, 0.003052. The model's delay memory stays empty: its comb feed is op 7 / op 4 steps with rB = 0
  (0x2a-0x2e, 0xa3-0xb3, 0xd2-0xd3) whose source is not modelled. Probes so far:
  - op 7 / op 4 source is not d[] when daddr bit 8 is clear, not w[r6 slot], not the register x (rounds 13-15,
    all read 0 on test image 0, where no DRAM transfer runs).
  - Hypotheses tried against the 1346-sample onset and rejected: op 7 = k x previous step's x (right sample
    values, but no delay: tail at sample 0); op 7 / op 4 = k x the DRAM transfer of slot s - 2 / s - 1 (no
    tail at all, the memory is never written).
  - Next: a probe on the shipped program that writes a known value into one comb's DRAM write step and reads
    the op 7 chain's result through 0cf, to find which transfer op 7 multiplies.

**Session 29 (remote rig): the shipped Hall1 step by step; op 7's operand; class-1 d[] writes.**
Shipped Hall1 with every DRAM write cut (r10 = 0) and the inputs held at 1/16, each of its 380 live steps tapped
in turn to d[10] (daddr 190, the left DAC): **378 / 380 match the model** (`docs/vop3_ship/29c.*`). Rules added:

* **Op 7's operand is d[r6[5:0]]** (the step's `f6c` field), not daddr: probe D_o7_10 (f6c 0x10) reads the
  left's d[10]; daddr 18d with f6c 0 reads 0 (s24 round 14). Test image 0's own op-7 steps (0d0 f6c 01, 0d4 f6c
  05) read the audio input, so the input arrives in d[1] / d[5] (`INPUT_D`, test-image layout only).
* **Class 1 with r9[12:8] set writes d[n]** for daddr 0x180|n (the 8 Hall1 taps on its `1c7c` / `1d01` / `1c7e`
  loads); plain, op-6 and op-1 loads write nothing there.
* Memory probes need test image 1 (`{"image": 1}` in the s24 runner) or a shipped program; on test image 0 no
  DRAM transfer is observable (every round-16/17 memory probe read 0 on both images, so the transfer path of
  hand-built steps is still not understood; image 1 as shipped (J_ship) does round-trip a value).

Still open, blocking the reverb tail (the model's delay memory stays empty on Hall1, the unit's L tail starts
1346 samples after the step: read slot 0 (offset 29998) minus write slot 0xd (28652), moved 1:1 by slot 0xd's
and slot 0's offsets (346 / 1614 after +-1000), so the tail is the delay line written at slot 0xd (step 0x37,
op 4 rB 0, mem 1) and read at slot 0 (step 0x03)):
* what the comb write steps (op 4 / op 5 / op 7 with rB = 0 and mem 1) carry: tapping them on the unit gives 0
  with memory writes cut, while tapping 0x29 / 0x34 (op 5 rB 34 / 31, k 0) with memory live gives a rising
  non-zero value (0.0065, 0.0156, 0.0191, 0.0208 readout) that the model's op-5 gate (k 0 -> 0) does not produce.

**Session 30 (remote rig): the reverb tail starts in the model, to the sample.**
Hall1 step response, input 0 -> 1/16 (s28 takes; s29a offset-moved takes; s30a: sixteen d[] cells tapped through
an op-7 step at 0cf). Rules added to `tools/vop3_interp.py`, all against unit data:

* **Op 5 with rB: max(r[rB], 0) + k x d[f6c]** (the comb write: feedback plus input gain); k does not gate
  (K_o5_k0 on image 1 writes 0.25 with k 0). **Op 4: s + k x d[f6c]** (the FIR taps).
* **Captures land three steps late** (`CAPLAT = 3`): with it the L tail of every Hall1 take starts on the
  unit's sample (1346 as shipped, 346 with slot 0xd + 1000; taps r77 / r1d / 02b / 02c / 02e / 036 / 037 / 03f
  match their first ~100 tail samples to the LSB).
* **DRAM reads and writes also run on phase 1** (step & 3 == 1; the shipped comb writes 0a9..0b5, 0d1).
* **Op 7 writes d[daddr]** like the other ops, except onto its own source cell.
* **d[] has 64 cells: the cell is daddr & 0x3f** (Hall1's comb outputs at daddr 1e2..1e7 land in d[22..27];
  their onsets 5587 / 4880 / 4200 / 3763 match the model within one sample).

Measured and not yet matched (`docs/vop3_ship/30a.probes.json`, takes in FS1R.unlock s30 folders):
* tail amplitude: d[3e] (op-4 FIR at 04c-04f) is 0.46 x the model from its first sample; the rest of the tail
  follows it, so the late tail (beyond ~1490 samples on L, ~1487 on R) drifts by 1-30 LSB.
* d[26], d[27], d[39], d[3c] stay 0 in the model but carry the tail on the unit (onsets 3066, 5080, 3066, 1487);
  d[17] moves in the model (614) and stays 0 on the unit.
* o0p1000 (slot 0 + 1000): unit 1614, model 1997.

**Session 31: d[] write timing, DRAM regions, the upper d[] bank.** Checked against the s30a d[]-cell taps and
s31a step taps (steps 040, 041, 04c..04f with memory live; `docs/vop3_ship/31a.probes.json`).

* **A step's d[] write lands 3 steps later in the same pass** (`DLAT = 3`), not at pass end. This was the
  0.46 "gain" on d[3e]: with it d[3e] matches the unit to 1 LSB over its first 400 samples (it was 0.46 x).
  Steps 040..04f themselves match to 1 LSB.
* **mmode bits 3:2 select a DRAM region** (`REGION`, size assumed 2^16; Hall1 0x1d3 / 0x177 carry 0x44 / 0x48).
  Without it 0x1d3 overwrote 0xd1's comb line. d[3c] now starts on the unit's sample (1487).
* **Class-0 steps with mem 1 write the running value** (Hall1 0x153, the 1452-sample line read at 0xa3).
* **Steps >= 0x100 use the upper 64 d[] cells** (`DHI`): d[17] (written by 0x11c) stays 0 on the unit; now in
  the model too.
* Regression: 384 / 384 s24 probes, s28a dry takes bit-exact.

Open:
* **daddr bit 6 (0x1e2..0x1e7, the comb outputs):** onsets match (d22..d25 to the sample), level is ~4-5.6 x the
  model and not a plain gain; a lag -1 fit gives y = 3.99 x(read at 0xa3, one step earlier), i.e. close to a x4 copy
  of the DRAM read. Four simple variants (4 x previous operand, 4 x running value, now / pass end) do not fit.
* d[26], d[27], d[39] still silent in the model (unit onsets 3066, 5080, 3066).

**Session 32: daddr bit 6 (the Hall1 comb-output writes 0x1e2..0x1e7).** Probes edit step 0a4 / 0a3 of shipped
Hall1 and tap d[22] (`docs/vop3_ship/32a.probes.json`, `32b.probes.json`). Capture check: the takes carry 18
effective bits, left-justified (low 14 bits zero in all 587k non-zero samples of the s31 take, no dither).

* With bit 6 set and k != 0, the step writes d[n] = the previous step's operand (d[f6c] of 0a3), not its own
  result: d[22] = 3.987 x the op-7 read, lag 1, residue 1 LSB (`base`). The op and the value of k do not
  matter (k 0x0800 / 0x4000 / 0xc000 and op 0 all give the same take). k = 0 gives the plain write
  (`k0`: d[22] = 0a4's result, 1 LSB), as does clearing bit 6 (`b6off`, 1 LSB).
* Now in the model: d[22] matches 1 LSB for its first 9 samples, then drifts.

Measured, not explained:
* Step 0a3 with k = 0: the unit's d[22] is 0, the model's is not. So the written value does depend on
  0a3's k after all. The 3.99 x operand fit is a coincidence of this k, or the value is 0a3's product times
  a fixed gain (0.17834 x 22.36).
* 0a3 k = 0x4000: d[22] alternates +8 / -8 every sample on the unit (a sign-flipping loop through d[38]).
* 0a4 k = 0x7fff: d[22] = -8 constant.
* d[23..27], d[38..3f] drift after 4..40 samples; d[26], d[27], d[39] stay silent in the model.

**Session 33: bit-6 rule settled, Hall1 step taps with memory live.** (`docs/vop3_ship/33a.probes.json`,
`33c.probes.json`; s32a / s33b probes ran with `keep`, so each probe carries the earlier patches: the s32
"0a3 k = 0 silences d[22]" result was 0a4 k = 0 plus 0a3 k = 0, not a dependence on 0a3's k.)

* **daddr bit 6 writes d[n] = the previous step's operand d[f6c]**, one pass later, whatever this step's op,
  k or the previous step's k: 0a3 k = 0x0400 / 0x0b6a / 0x16d4 / 0x2000 / 0xe92c all give the identical d[22]
  take (gain 3.987 on the model's d[38] read, residue 1 LSB). Without bit 6 it is the plain result write.
* **Class-0 steps with mem 1 write nothing** (s31's "running value" rule withdrawn; it zeroed 0xd1's line).
  d[26] and d[39] now start on the unit's sample (3066).
* 28 Hall1 step taps with memory live (01d..08f): 27 match to 1 LSB over 2400 samples; 08f drifts at 2389.
* d[] cells: d16, d17, d22 exact over 6000 samples; the others start on time and match for 1060 samples,
  then miss a negative step (d3c at 2549 = 1487 + 1062: the 0x57 -> 0x33 line, written with 0 in the model).
* Step-response takes (s28a, 8000 samples): Hall1 exact to sample 2088, Hall9 to 1905; tail rms error 1.3x
  (Hall1) and 1.1x (Hall9) the unit's tail rms.

Open: what 0x57 / 0x157 / 0x19f (op 0, k 0, mem 1, rB 78 / 7a) write into DRAM. Writing r[rB] makes the tail
worse (`B13X`, rejected). Swapping the capture source order (`CAPORDER`) breaks 0..613: rejected.


**Session 34: all 128 slot offsets; r9[13] is the DRAM write-data select.** (`tools/vop3_board.py` scored every
dynamic take on disk through the C++ core of the time, `src/vop3_core.h`. Since 2026-10-13 the core is
`src/yss236_core.h`; the old core and the tools that drove it are gone.)

* **The chip has 128 delay slots.** FUN_000397F4's slot lists run to 127 (0x35E23C, 0x35E398) and the area table
  at 0x3720A4 has 128 bytes; every dump before s34 read 64. The rig dump (FS1R.unlock
  `captures/2026-10-02-035714-s25`, Hall1 and Hall9) gives the upper 64 with their area bases (0x10000 / 0x20000 /
  0x30000) and the lower 64 identical to s28. s31's mmode `REGION` guess was standing in for those bases; the model
  now indexes `offs[slot & 0x7f]` and the guess applies only to a 64-slot dump.
* **r9[13] on a step makes its output the data of the next DRAM write; a writer carrying r9[13] writes its own.**
  This replaces s30's "op 5 with rB loads the write latch" (every shipped op-5 comb step carries r9[13]) and s33's
  "class-0 mem-1 steps write nothing": Hall1's 0x153 / 0x19b / 0x1d3 / 0x1eb are class 0 and write the value the
  preceding r9[13] step latched (0x146, 0x18e, 0x1cc, 0x1ea). Unmarked writers (0x57, 0x9b) write their own result
  after a marked step's write has consumed the latch. Census over every VOP3-2 window: r9[13] on 178 steps, 109 of
  them on a mem-1 step.
* Scoreboard, 114 step-up segments (s28 and s30-s33 takes): Hall1 step response exact (1 LSB) to sample 5131 L /
  5002 R (was 2088 / 2228), Hall9 5657 / 5656 (was 1905 / 2039); 102 segments clean past 5000. Python self-check
  384 / 384.

Open: every Hall1 take still misses at 5002 on R and the segments of the s33 08c-08f / 054-05b round (FS1R.unlock
`2026-10-02-024751-s25`) at 3065 / 3066; ua4k0800 (0a4 k = 0x0800 after 0a4 = 0x7fff drove the loop to the clip)
does not recover in the model the way the unit does, which points at the clip / overflow rule inside the comb loop.

**Session 35: op 5 passes negatives; the real input registers; first noise replay.** (FS1R.unlock s25/s24 folders
`2026-10-02-050244` .. `071415`; probe sets `docs/vop3_ship/35a`-`35k`.)

* **Op 5 with rB is r[rB] + k d[f6c], no clamp.** s24's `max(r[rB], 0)` came from its operand load running in
  mode 2 (r7 = 0x8000 clamps negatives): with the same load in mode 1 the step stores -1/256 .. -64/256 exactly
  (`M5_*`, 7 probes, r9[13] set or clear; `N5_*` re-measure the mode-2 zeros). Both are asserted in
  `docs/vop3_probes.json` (398 / 398). Effect on the shipped Hall1 / Hall9 step responses: exact (1 LSB) to
  7312 / 7452 samples (was 5131 / 5657), and to the full 6000-sample window on 154 of 176 step-up segments.
* **The comb's 5002 miss was this clamp, not the delay line.** Moving slot 0x15's offset by +-500 (s35a) moved the
  unit's and the model's first difference from the reference by the same amount, so 0x57's line and its data
  were right; the remaining combs went negative at 4129 / 4826 / 5263 and the clamp cut them.
* **VOP3-2's effect inputs are r[0d] / r[0e] (reverb send) and r[0f] / r[10] (variation send)** (s35h: the left
  DAC re-pointed at each of r[0b..12] with noise on each bus). No step writes them; base steps 002-005 read them
  in place. The two reverb registers carry the same mono send, r[0e] one pass ahead of r[0d]. Session 22's
  r[03..08] were registers the program computes from these.
* **First noise replay (`tools/vop3_replay.py`, take s35j, 0.5 s of noise after a 2000-sample warm-up):** Hall1
  fed the recorded r[0d] (r[0e] one pass later) correlates 0.795 with the unit's right output, 3.8 dB low;
  Hall9 0.505, 2.9 dB low. Variation 1 is not a valid replay yet (its two inputs are uncorrelated stereo and the
  take recorded one). The step responses agree to 7300 samples, so the replay's gap is the input level / timing
  within the pass and the long-tail rules below, not the early reverb.
* Withdrawn: s30's "op 5 with rB loads the DRAM write latch" (s34, r9[13]) and s24's op-5 clamp (above).

Open:
* **Op 7 at route 3:** the shipped 09b (rA 15, mem 1, f6c 3a) taps exact zero on the unit, as do 09b with rA or
  mem cleared and with both cleared; a d[] tap at 0cf (op 7, route 3, f6c 3c) passes, and the same tap with rA
  set reads -1 LSB, with mem 1 starts 4670 samples late (s35k). No single field explains all of them yet.
* The comb-loop overflow: 0a4 k = 0x7fff drives the loop to the clip on the unit (sign-flipping at full scale)
  and the model pins at +8; recovery after k is restored differs.

**Session 36 (in progress): insertion input, mono reverb pair.** (FS1R.unlock `2026-10-02-073531-s25`, `-075023-s25`.)

* The insertion send lands in r[11] / r[12] (r[12] the same signal as r[0x0f+..] taps, corr 1.000 with the right
  output's input); r[01] / r[02] read large offsets (+6.5 / +5.2 readout) under the insertion load. Base steps
  000-007 scale r[0b..12] in place (op 3, k 0x5a82 on the live pairs), so "read, never written" missed them: they
  are rA = rB steps.
* With the part panned hard left, the reverb pair is r[0e] = 0.1532 x r[0d] two passes later (resid 1.6%);
  the variation pair is r[10] = 0.5 x r[0f] same pass (resid 0.1%). These are the send-level pan law, not chip
  rules.
* rev1L replay (exact mono input, both registers driven): corr 0.75, level -5.3 dB. Program step 0x14f writes
  r[0d] (and 0x155 r[0e]) every pass, so when in the pass the hardware's write lands decides what step 002 reads;
  that timing is the next measurement.
* d[] cells read but written by no step (01, 02, 03, 05, 06, 07, 17, 28 and the upper 44/48/4c): only d[03] and
  d[28] carry signal under variation 1, d[02] and d[05] under insertion 1, r[04]-high (d[44]) under reverb. These
  are the hardware's per-bus d[] inputs; not yet in the model.

**Session 36d: click replay of shipped Hall1 / Hall9 against the unit.** (FS1R.unlock `2026-10-02-082621-s25`; the
s25 runner gained a per-probe `retrig`, a fresh note after the patch, so each tap segment holds the same click,
repeatable to the LSB through the early reverb.)

* **The input tap reads r[0d] after steps 002/003 scale it in place** (op 3, rA = rB, k 0x5a82 = 0.7071), so the
  replay must inject tap / 0.7071. That was the whole 3 dB of the s35 noise replays.
* **r[0e] carries the same mono send two passes after r[0d].** Shifts tried (0..2 / -1..3): only (0, 2) holds
  past the first echoes (first >30 LSB at 4324 vs 1880).
* Result (`python tools/vop3_replay.py`, asserted): Hall1 exact to sample 1888 then corr 0.978 over 0.4 s, level
  -0.2 dB; Hall9 exact to 2236, corr 0.971, -0.6 dB. The step responses agree further (7312 / 7452) because a DC
  step never exercises the modulated taps: a 256-sample local fit puts the click tail 0.75-1.1 samples late in
  the model, the signature of a modulated delay read (VOP3-2's LFO registers 0x24-0x27, which the model has none
  of; the depth words read 0x7fff for blocks 1-3 on the unit).
* d[44] (seen live under reverb) is only read by the class-3 parameter smoothers at 0f0-106, which the model
  treats as pass-throughs: not an audio input.

Next: the LFO. Registers 0x24 (speed, 0x36EF54[v]), 0x25 (waveform bits 5-7), 0x26 (depth), 0x27 (direction) per
LFO channel, four per effect block; which step field reads an LFO and how it moves a DRAM read is unmeasured.

**Session 36e-f: input timing settled by tapping shipped read steps.** (FS1R.unlock `2026-10-02-084908-s25`,
`-085439-s25`.) Each segment holds L = r[0d] (0cf re-pointed) and R = one shipped DRAM read step tapped into d[11];
the replay fed the segment's own L must reproduce R. With r[0d] = tap[p + 1] / 0.7071 and r[0e] = tap[p + 2] / 0.7071
(the 0cf tap reads r[0d] a pass after it holds the value the program sees), every tapped read step (003, 013, 017,
01b, 0a3) matches the unit to 1 LSB over 9000 samples, so the DRAM line timing, slot offsets and write rules on the
input lines are right. The replay check (`tools/vop3_replay.py`) moves to Hall1 exact to 1927 samples.

**Session 36g-k: capture source is step distance, not slot; Hall1 / Hall9 click replays exact to rounding.**
(FS1R.unlock `2026-10-02-090236-s25`, `-092531-s25`, `-093836-s25`, `-103008-s25`.)

* Method: one shipped step at a time re-pointed into d[11] (R), with r[0d] on L, under the s36d click; the replay
  fed L must reproduce R. 30 of the first 32 taps (02a..088) and 0a3..0b5 exact over 12000 samples; the first
  divergences cluster on paths through d[38] (0d2 reads it) and d[27] (0d3), both fed by capture 0ce.
* **Rule (adopted):** a capture (daddr 0x100|n) at step st takes the latest DRAM transfer of this pass made 3..11
  steps earlier, else holds. s16's "slot s-2, else s-1" picked 0c5 (a class-0 read of slot 0x31, offset 0x10000)
  for 0ce; the unit takes 0c9's read. Discriminator: d[12] at 0be reads exact 0 on the unit (s36k) as the rule
  predicts; the alternative "slots parked at >= 0x10000 make no transfer" predicts a 7766 LSB peak there.
* Result: Hall1 click replay error rms 63 -> 0.6 LSB (40000 samples, max 5 LSB, corr 0.999996, +0.01 dB); Hall9
  114 -> 0.9 LSB (max 6, corr 0.999998). `tools/vop3_replay.py` asserts max error <= 8 LSB. Self-check 398/398.
* Board: 2 takes better (u3c, ud27), 3 worse: us098 / d77 by 1 LSB of rounding, d3c by 15 LSB late in a
  segment that follows u3c (u3c's 14 LSB miss moves to d3c: the same tail, now on the other side of a keep). OPEN.
* Noise replays (s35j / s36a) stay at corr 0.67..0.89: those takes start mid-stream with the unit's DRAM full of
  earlier noise, which the replay cannot know. The click takes start from silence and are the exactness test.
* Remaining at <= 6 LSB: rounding (output / accumulator LSB conventions), first at Hall1 1927 / Hall9 2236.

**Session 37: the effect input pair is per algorithm; every VOP3-2 type replayed from a click.** (FS1R.unlock
`2026-10-02-104635-s25`, `-140637-s25`.) Take: one config per algorithm, each with `_ia` / `_ib` re-pointing the
0cf tap at r[0d] / r[0e], `_ir` at the driven register, all under a fresh click per segment.

* `rev3c` Room1 / `rev8c` Plate: r[0d] drives the output (err rms 1.8 / 1.3 LSB, corr 1.00000 over 16000 samples).
* `rev13c` Delay LCR / `var20c` Delay LCR: **the mono send pair arrives on r[0d] and r[0e] only for the
  algorithm that reads it there; Delay LCR reads r[0e]** (a single register, driven by the second tap) — feeding
  r[0d] gives exact 0, r[0e] gives corr 1.0000 / 2 LSB. Variation types read r[0f] / r[10].
* The Delay algorithms are **not clean** in a multi-config take: their output equals their own segment's R for
  the first ~5100 samples and holds a constant after (the delay ring is 2^18 words at 48 kHz, so an earlier
  config's tail is still in it). Replaying them needs a silence lead-in and a single config per take; a rig round
  is queued. Under those terms `rev13c` is exact (corr 1.0000).
* Register-level rule: **which register the hardware drives depends on the algorithm**, and the base program's
  own writers (steps 0xda-0x0e7 for the delay mix, 0x13e/0x140 re-pointed at r[0d]/r[0e] for the delay programs)
  move the input on. The tap is the way to ask: re-point 0cf at each register in turn (s36). `_ia` / `_ib` /
  `_ir` on a per-algorithm config is the shape of every future replay round.

**Session 37c: the mono send feeds both registers of the pair; the delay algorithms are exact.** (FS1R.unlock
`2026-10-02-142901-s25`.) Each delay config captured alone, after a silence segment, with the taps re-pointed per
register: `rev13e` Delay LCR and `var20e` Delay LCR both reproduce the unit to 2.3-2.8 LSB (corr 0.99999, gain
0.9999, lag 0) over 14000 samples, when r[0d]/r[0e] and r[0f]/r[10] are fed the same recorded send — feeding only
the second register of the pair is enough (the two registers carry the same send; s36's "r[0e] two passes later"
is step 003's in-place scale, not a different signal). The earlier zero-output and constant-hold readings were
the take's own delay ring: a Delay config captured after other configs still holds their tail (2^18 words at
48 kHz), so delay algorithms need a single config per take with a silence lead — both now measured.
* `rev7e` (reverb 7 Stage2, the base program) still misses: model rms 99/165 against the unit's 236/238, corr
  0.83/0.94 at gains 2.0/1.35 (OPEN — the stage program's own input path differs from Hall1/Plate's).

**Session 37d: VOP3-1's program structure, as far as it goes without the rig.** (`docs/vop3/program_0.bin`, 512
steps; class 2 = 416, class 1 = 57, class 0 = 7, class 3 = 32.)

* It is a repetition of one 20-step filter block writing its own register bank: **block A at 0x059 writes
  r[02..0x18]**, **block B at 0x14d writes r[0x31..0x48]**, **block C at 0x1c9 writes r[0x49..0x60]**, and the
  upper half (0x100 up) repeats the pattern for the channel's own copy. Blocks are not translations of each
  other, so register numbers carry the block's identity, not its position.
* Each block opens with an 8-step header (e.g. 0x059-0x060) that writes the **parameter file r[0x65], r[0x66],
  r[0x68], r[0x69], r[0x6b], r[0x6c]** with class-2 op-0 steps (rA = the destination, one running value passed
  step to step), then runs the two biquad sections.
* **r[0x64], r[0x67], r[0x6a] are read as `f6c` operands - the filter coefficients - and never written by any
  step.** The candidate voice-audio registers are the block heads r[07]/[08]/[09], r[13]/[14]/[15],
  r[31..35], r[49..4d], each read by op 0 / op 3 / op 5 steps and never written. No step is an op-1 route-0
  step, so the DAC path is not how the filtered voice leaves; the output stages 0f0 / 0f8 are the candidates.
* This is where the rig takes over: re-point a probe step into d[10] as in s36 and drive r[07], r[13], r[31],
  r[49] in turn. `Vop3Filter` now carries the map, the `param()` write (FUN_0000B5E2) and the OPEN list.

**Session 38a: driving VOP3-1 registers from the monitor under a running note is silent.** (FS1R.unlock
`2026-10-02-145907-s25`; `docs/vop3_ship/38a.probes.json`.) A filter part with the operator silent (`level=0`,
so the note itself contributes nothing) and the 8 candidate registers r[07]/[13]/[31]/[49]/[64]/[67]/[6a]/[0d]
held at +-0x4000 for 0.35 s each through the firmware's own write (FUN_0000B5E2, the s25 runner's new `regs`
field): every segment reads the same -56.6 dBFS floor as the reference, +-0.1 dB (0.5 s, 48 kHz). So
* the write does not take effect under a running note (the firmware stages VOP3-1 through register 0 and 11,
  and a direct poke may need the chip idle), or
* none of these registers is the sounding channel's input.
The next step is the same sweep on a **silent** chip (no note: the register writes are then unambiguously the
only signal, and a write that appears at the output proves the write path), then the register probe under a note
once the path is proven.

**Session 38b: a silent filter part makes no sound at all, so the register write path is still unproven.**
(FS1R.unlock `2026-10-02-150543-s25`; `docs/vop3_ship/38b.probes.json`.) 13 registers written with 0x4000 and
back to 0 on a silent chip (voice off): every segment is exact digital silence (L and R max 0.0). A filter part
whose operator is silent produces nothing even with the program running, so this cannot separate "the write
does not land" from "the program passes nothing"; s38c (audible note, coefficient cells r[64]/[67]/[6a] and the
lane heads swept) is the deciding round.

**Session 38c: writing the filter's coefficient cells does not change the response.** (FS1R.unlock
`2026-10-02-151144-s25`; `docs/vop3_ship/38c.probes.json`.) An audible filter part, 28 segments: r[0x64],
r[0x67], r[0x6a] (the cells the program reads as `f6c` coefficient operands) and the lane heads r[07]/[13]/
[31]/[49] each written 0x0000/0x4000/0x8000/0xC000. Every segment reads within +-1 dB of the reference level
(-64.4 dBFS) and within a few dB per band (noise). So the monitor's direct register write either does not
reach the chip while it is running the voice, or those registers are not in the sounding channel's path.
*The harness point first:* the filter part as configured here only produces -64 dBFS, which is too little
headroom for a register-difference test; the next round raises the source (operator level, filter gain, or
the cutoff) until the reference reads near -20 dBFS, then repeats this sweep. `regs` / `filter` in the s25
runner are the interface (FS1R.unlock, `fs1r_capture_session25.py`).

**Session 38d: the monitor's VOP3-1 register write does not reach the running chip.** (FS1R.unlock
`2026-10-02-152257-s25`; `docs/vop3_ship/38d.probes.json`.) The s38c sweep with the source at full (operator
level 127, cutoff 64, note 36), so the reference reads -34.9 dBFS: all 28 segments land within +-0.1 dB of the
reference overall and every band difference is noise (the largest, 6.4 dB in the 20-40 Hz band, also appears at
repeat writes of the same value). Writing r[0x64], r[0x67], r[0x6a] - read by the program as `f6c` coefficient
operands, so they must change the filter if they land - and the lane heads r[07]/[13]/[31]/[49] leaves the
response untouched.

**Conclusion: FUN_0000B5E2 does not reach the running chip's register file from the monitor.** That is why
every VOP3-1 probe so far reads as nothing, and it is the blocker for the whole VOP3-1 input-register question
(and for the s8 `bits` runs, which write program steps, not registers - those do land, so the *program* path is
live and the *register* path is not). Candidates: the write needs the chip idle / a status handshake
(`DAT_010683E0` is VOP3-2's busy; VOP3-1's may be elsewhere), or the routine's register 0/11 staging (the
`FUN_0000B6A4` pair) is what the chip actually samples.

**Session 38e: VOP3-1's coefficient port is registers 0 and 11, and the audio path is still OPEN.**
(`FUN_0000B6A4` decompile + `docs/vop3/program_0.bin`.) The firmware's own coefficient write is a *pair*:
register 0 takes the coefficient **slot** number and register 11 takes the **value** (`FUN_0000C36C` stages
`0x01068F78[slot]` then calls `FUN_0000B6A4(1, ptr)`), which is why the boot program opens with steps 000-008
reading r[0]. A monitor poke of a *coefficient slot* through `FUN_0000B5E2` therefore writes the slot number
where the value belongs - which is exactly the historical "the panel lit up" failure. So r[0]/r[11] are the
coefficient port, not audio, and every s38 register sweep that wrote r[0]/[11] was writing the wrong thing.
Still OPEN: which register carries the voice audio. `docs/vop3/program_0.bin` never reads r[0]/r[11] as operands,
so the audio must sit in a cell the program reads and never writes; `tools/vop3_core_check.py` now asserts that
`Vop3Filter` loads and runs the measured program, so the next rig round starts from a wired module.

**Session 38f: VOP3-1's head is a table setup; the filter's parameter cells are the f6c 0x11/0x14/0x17/0x1a.**
(`docs/vop3/program_0.bin` 0x000-0x01f.) Steps 000-00e write d[] cells (0x18, 0x59, 0xd7, 0x1bf/0x1be/0x1bd,
0x83) and load r[01..05] - the table the firmware patches at note-on (FUN_0000D050 changes a channel's type
and gains in the live shadow, not the image). From 0x010 on, the filter proper: class-2 op 5 reading
`f6c` 0x11 / 0x14 / 0x17 / 0x1a (the per-voice parameter cells) storing into d[] 0x01/0x02/0x03/0x04, then
class-1 loads r[61]/r[62]/r[63] and the two biquad sections (0x1c+). So the parameter cells a rig probe can
sweep with visible effect are `f6c` 0x11/0x14/0x17/0x1a, with r[61]/[62]/[63] the cutoff/resonance/gain
constants - a much smaller candidate set than the 128-register sweep. Still OPEN: the audio input register,
which no step writes and (per s38d) the monitor cannot reach while the chip runs.

**Session 38g: VOP3-1's output is the SUM BUS, and 33 registers feed it (four lane groups).**
(`docs/vop3/program_0.bin`.) The boot program has **no d[10]/d[11] write at all** (the s38 DAC sweep found zero
registers reaching the DAC cells), but it does have **48 class-2 op-1 route-0 steps**, which is the model's sum
bus. Driving each of the 128 registers and reading that bus instead:

    0x09 0x0a 0x0b   0x15 0x16 0x17   0x1b 0x1c 0x1d    (the first lane, x4 offsets)
    0x21 0x22 0x23   0x2d 0x2e 0x2f   0x33 0x34 0x35
    0x39 0x3a 0x3b   0x45 0x46 0x47   0x4b 0x4c 0x4d
    0x51 0x52 0x53   0x5d 0x5e 0x5f
    (+ lane groups at +0x18 and +0x30): 33 registers in **four lanes of a three-register group plus the
    filter-state cells**. Each lane is one filter channel's input/output trio, so the voice's audio enters
    through a lane register and the filtered result leaves through a later one in the same lane.

That is the candidate set the rig needs, and it is small: the next round drives lane 0's r[09]/r[0a]/r[0b] (and
the +0x18/+0x30 equivalents) with the loud filter part already in hand. Note the ordering rule: **on VOP3-1 the
output is the sum bus, not the DAC cells** - the model's `bus` (op-1 route 0) is the right output there.

**Session 38i: the VOP3-1 register window is gated by register 1, and the channel map is known.**
(`FUN_0000BC8C` / `FUN_0000B600` decompiles, `FILT_STEPS` 0x374E44 / `BLOCK_TAB` 0x374F2E.)

* **The window bit.** `FUN_0000B5E2(1, 0x1004)` **opens** VOP3-1's register window and `FUN_0000B5E2(1, 0)`
  closes it; `FUN_0000BC8C` brackets its whole upload that way, and `FUN_0000B600` writes a step as
  `FUN_0000B5E2(0, slot)` then `FUN_0000B5E2(10 - k, word)` (k = 0..4). All of s38a-38h wrote registers with the
  window **closed**, which is why 128 registers and 33 lane cells all read as nothing. Every future VOP3-1
  probe must open the window first (FS1R.unlock `fs1r_capture_session25.py` now does it around `regs`).
* **The channel map** (what the note lands on, so a probe writes the right lane):
  ch0 0x70, ch1-3 0x90, ch4-5 0xf0, ch6-7 0x110, ch8-11 0x170, ch12-15 0x10 - the group whose cutoff step the
  table names. A channel's lane trio is at the group's own cells, so a probe reads `FILT_STEPS[channel]` and
  `BLOCK_TAB[channel]` and writes that group.

**Session 38j: with the register window opened, VOP3-1 still does not take a monitor register write.**
(FS1R.unlock `2026-10-02-154946-s25`; `docs/vop3_ship/38j.probes.json`.) Same loud filter part as s38d; each probe
opens the window (`FUN_0000B5E2(1, 0x1004)`), writes one lane cell (r[09]..r[1d], the s38g lane trios) 0x4000,
closes it. Every segment is within +-0.1 dB of the reference; every band difference is at or below the -3.8 dB
bass-bin transient that the reference and `aud_ref_end` show too. So **register 1 alone does not gate it**: the
monitor's write does not reach a running chip's VOP3-1 register file even with the window opened, though the
same routine *does* write the program steps (s8's `bits`/`nop` runs move the filter), and the boot upload
(`FUN_0000BC8C`) uses the identical call.

**Where this leaves VOP3-1 (blocker, with the specific ask).** Everything about the chip that can be read from
the firmware is now in `Vop3Filter`: the 512-step program, the block/lane structure, the sum-bus output, the
coefficient port, the channel map, the window bit. What is missing is which register carries the voice's audio
in. To get it, from inside the unit rather than from the monitor:
* breakpoint `FUN_0000B5E2` while a note sounds and read the caller's register/value arguments; or
* break at `FUN_0000B600` (the program upload) and see whether it is called when a voice arrives (if it is, the
  program image is the CPU's `0x01069978`, which is readable from the monitor); or
* note the arguments the firmware's own coefficient write (`FUN_0000C36C` -> `FUN_0000B6A4`) passes for a *known*
  coefficient, and compare with the monitor's call.

**Session 38k: baseline board and the remaining VOP3-2 misses.** Board (`tools/vop3_board.py`, 200 takes, 185
step-up segments): **sum of first-bad L 1014120, R 1046334** with the s36k capture rule in. Remaining, both
named: (1) every Hall1 take still misses on R at 5002, and the s33 08c-08f / 054-05b segments at 3065/3066;
(2) `ua4k0800` (0a4 k = 0x7fff then 0x0800) does not recover in the model the way the unit does - the clip /
overflow rule inside the comb loop, the last open VOP3-2 arithmetic question. Everything else that s36 settled
(Hall1/Hall9 click replays, the Delay LCR pair, the capture source) is locked behind `tools/vop3_replay.py`'s
assertion, so a regression in any of them fails the check.

**Session 38l: the comb's overflow is a WRAP at the route-scaled range, not the +-8 clip.** (FS1R.unlock
`2026-10-02-021742-s25`, the `ua4k*` takes.) Driving Hall1's comb to the clip (0a4 k = 0x7fff, then reducing it)
the unit's output sits at exactly +-131071 / -131072 of the 16384-unit readout - i.e. **+-8 full scale at the
DAC, d[] +-32** - and alternates sign sample to sample, which is a wrap, not a clamp (the model pins at +8 and
never alternates). The comb's steps: 0a5 is class-2 op 0 rA = rB = r37, mode 1 (saturate), route 1 (x2),
k = 0x001d; 0a4 is op 4 route 0 mode 0 (wrap) writing d[0x22]; 0a9 is op 5 rB = r37. So the running value inside
the comb reaches +-256 through route 1 and wraps there, while the model applies the step's mode-1 saturation
first. The rule to settle: **the result's mode and the route scale interact - a route-1 (x2) result wraps at
+-256 rather than saturating at +-128**, which is invisible on every non-overflowing probe (all 200 takes
without the clip) and only shows on `ua4k*`. Fixing it in `tools/vop3_interp.py`'s `mode()` / `acc_mode()` and
re-running the board (`sum first-bad L 1014120 / R 1046334`) is the next model step; it must not move any other
take's first-bad sample.

**Session 38l (cont.): neither wrap location fixes the ua4k* takes - the divergence is carried-in state.**
Tried the wrap at the route-scaled result (limited to +-2*RANGE on a route-1 step) and at the d[] write
(+-DSAT): both leave every `ua4k*` take byte-identical to the clamp model, and the interp self-check stays
398/398. So the divergence at sample 0/1 of those takes is not a per-step overflow rule - the `z*` probe that
drives the loop to the clip leaves the unit in a state the model does not reproduce, and the next segment reads
it. That is a probe-design problem (the model cannot be seeded with the unit's carried state from a
clip-driving segment), not a rule to fit: leave the clamp as measured for the non-overflowing 200 takes and
record the ua4k* set as not decidable by this pair of segments. A cleaner probe would drive the loop to a
*known* state (a short burst then silence) so both sides start from the same place.

**Session 38m: the VOP3-1 window readback proves the monitor write never lands (chip-select, not audio).**
(rig, one-off `vop1_rw.py` / `vop1_read.py`.) Read-only + one-register writes:

* **The window reads back.** `peek(0x00800200, 128, 2)` returns VOP3-1's register file: on an idle unit it is
  almost entirely 0, with only r[1] = 9 and r[0x43] = 0xe9 non-zero; a note moves r[1] 9 -> 0xa and r[0x43]
  0xe9 -> 0xea (so r[1] is a write counter, not data). **No register ever holds the voice audio** - the audio
  does not enter through a register. (Also: the CPU shadow at 0x01069978 does not change under a note, so it is
  the boot image, not a live copy; and VOP3-1 has no live copy of its program at all.)
* **The write never lands.** `FUN_0000B5E2(0x0b, 0x1234)` on an idle unit, read back at 0x800200 + 2*0x0b:
  0x0000. Same for r[09] and r[01]; the firmware's own window-open write `(1, 0x1004)` also reads back 0. So
  s38a-38j's negatives were never about audio or the window bit: **the monitor's write to 0x00800200 does not
  reach VOP3-1's register file at all.**
* **Why:** every register write that *has* worked in this project (s8/s11 `write_step` / `write_coef`, the test
  images) pokes **0x00800000 - VOP3-2**. Nothing has ever written 0x00800200 successfully. The firmware writes
  it, and the note-on handler sets the per-channel **chip-select flag** before its first event (docs elsewhere),
  so the CS2 decode for 0x00800200 depends on that selection being made. Candidate next measurement: read the
  per-channel struct's chip-select cell before and after a note, then set it before a monitor write and read
  0x800200 back again.
* The s38g model result stands on its own (which registers *would* reach the sum bus, if written), and the
  in-unit breakpoint route is still available - but this readback is cheaper and it also settles what a
  breakpoint would have told us: the value written is not the problem, the write path is.

**Session 38m RETRACTED / 38n: the VOP3-1 window is write-only; the readback was bus noise, and the write path
works.** (rig one-offs `vop1_noise.py`, `vop1_read.py`.)

* `docs/protocol.md` says it outright and I had skipped it: "0x00800100 - the LED and LCD-contrast latch,
  **write only** ... write-only devices read back as **bus noise**, so peeking 0xC00000 or 0x800100 tells you
  nothing." `captures/sweep.py` says the same of VOP3-1's block: "VOP3-1's block is write-only, so the check is
  the firmware's echo instead."
* Measured: 0x800100 reads a **floating** pattern (0x644d, 0xfffe, 0xfffe -> 0x644d moving between reads);
  0xC00000 reads 0x4dff / 0xffff moving; **0x800200 reads all zeros with an occasional 0xd**. All three are
  write-only; the VOP3-1 window's "r[1] = 9 -> 0xa, r[0x43] = 0xe9" in s38m was an artifact of the floating bus,
  not a register. So s38m's "the write never lands" and "no register holds audio" are **withdrawn**.
* **What is established instead:** session 8's `bits` / `nop` / `filtcoef` runs wrote program steps through
  `FUN_0000B5E2` and moved the filter hard - `probe_check.csv` shows **62 of 96 segments moving >50 Hz** in
  spectral centroid, up to +11 dB. So the monitor's write path into VOP3-1 is live and audible. The s38
  register-write negatives therefore stay unexplained by any write-path failure; the open question is back to
  the honest one - which register (or which non-register path) carries the audio - and it is only answerable
  with audio, next to a **positive control** in the same take.

**Session 38p: the complete list of registers the firmware writes into VOP33-1 - and no audio register exists.**
(`FS1R_GHIDRA_ANALYSIS` decompiles: every function that calls `FUN_0000B5E2`, i.e. every write into
0x800200 + 2*reg.)

The firmware writes exactly these VOP3-1 registers, and knows nothing else:
* `r[0]` - the step address (the program port).
* `r[10] r[9] r[8] r[7] r[6]` - a step's five words (`FUN_0000B600`, the program upload).
* `r[0xb]` - a coefficient value (`FUN_0000B6A4`, paired with `r[0]` = the slot).
* `r[0xc]` - a per-step tag byte (`FUN_0000B788` / `FUN_0000B7EE`).
* `r[0x16]` - the 15 bus values (`FUN_0000B85E`).
* `r[0x24..0x27]`, `r[0x28]`, `r[0x29]`, `r[0x2a]`, `r[0x1c]`, `r[0x20]`, `r[0x21]`, `r[0x14]` - the LFO and EG
  words and their control cells (`FUN_0000B8EC` .. `FUN_0000BAC2`, `FUN_0000BAF6`, `FUN_0000BC42`).
* `r[0x2b]`, `r[0x2c]`, `r[0x2d]` - the filter EG rate / level / depth words.
* `r[1]`, `r[0x17]`, `r[0x21]` - control (the window bit and the two words `FUN_0000BC8C` sets).

**There is no audio register.** Every register the firmware writes is a parameter, a program word, a tag, or a
control bit. So the voice's audio does not arrive in VOP3-1 through the register file at all: it must come
through the chip's own input path, which on VOP3-2 is the audio-bearing `d[]` cells (`INPUT_D = (1, 5)` in the
interpreter) and the op-7 "chip input". That is why s38a-38j's register sweeps were silent - they were aimed at
something that does not exist - and it is a **confirmation of the model's existing mechanism**, not a gap.
*Consequence:* the register-sweep line of investigation is closed for good. The remaining VOP3-1 question is
narrower: which `d[]` cell (or op-7 route) the voice enters on, and how the filtered result leaves - and the
filter's actual behaviour is already testable against the unit by feeding the interpreter the note's audio in
the input cells and comparing the sum bus to the recording, with no register in the loop.

**Session 38q: VOP3-1's live constant table dumped, and the bank map is the remaining bug.**
(rig `vop1_coefs.py`; FS1R.unlock take `2026-09-30-10` already on disk.)

* **The staged constant table reads back** (`DAT_01068F78`, 512 x u16, RAM not write-only): one note through a
  filter channel and the table holds **149 nonzero words** where the EPROM image has 132 - the extra are exactly
  the per-channel constants the firmware patches at note-on. Saved as `docs/vop3/coefficients_live_0.bin`, with
  the CPU program copy as `docs/vop3/shadow_program_0.bin` (the shadow is the boot image, unchanged by a note).
  This is what `Vop3Filter` should run with: the model's zero constants were the first reason its output was
  silent.
* **The chain runs** once the real constants and the per-channel banks are in: with `bank_of = fs1r_bank` and
  r[0x7f] driven, steps 0x10c and 0x114 produce 0.6 and 15.15 where they were 0 before.
* **What is still wrong is the bank assignment, not the arithmetic.** The output stage 0x174 (bank 8 per
  `fs1r_bank`) reads r[0x35], which step 0x175 writes in bank **9**; r[0x3a] is written in bank 11, r[0x34] in
  bank 9. On the chip a channel's steps must share one bank, so `fs1r_bank` (documented INFERRED, never measured)
  is off by the group, and the group at 0x108 belongs to channel 4 whose own steps are interleaved across it.
  Four variants (as-is, shift-4, group->channel, all-in-one-bank) all stay silent downstream, so the formula
  needs a measurement, not another guess: the cleanest is VOP3-2's (session 16) - move one register between two
  banks and see which step's read changes.
* `tools/vop3_filt_replay.py` is in place: it loads the full 512-step program and the live constants, takes the
  recorded input (`op5s1`, the output stage's op changed to 5 = the chip input) and the filtered output (`ref`)
  from the same session-9 take, and reports model-vs-unit rms, gain, correlation and error. It is the harness the
  bank map can be settled with, and it needs no further rig setup.

**Session 38r: what VOP3-1's replay needs, and the measured facts it rests on.** (rig dumps; take
`2026-09-30-10`.)

Settled this session, all measured:
* The firmware writes **only parameters, program words, tags and control bits** into VOP3-1's window (`r[0]`,
  `r[10..6]`, `r[0xb]`, `r[0xc]`, `r[0x14]`, `r[0x16]`, `r[0x1c]`, `r[0x20]`, `r[0x21]`, `r[0x24..0x2d]`, `r[1]`).
  **No audio register exists**; audio comes in through the chip's own path, as on VOP3-2.
* The window reads back as **bus noise** (0x800100 and 0xC00000 read floating values, 0x800200 reads zeros), so
  it is write-only - `docs/protocol.md` and `sweep.py` both say so.
* The **live constant table** `DAT_01068F78` (512 u16) is readable RAM and holds **149 nonzero words** where the
  EPROM has 132: the extras are the per-channel constants the firmware patches at note-on. Dumped to
  `docs/vop3/coefficients_live_0.bin` (program copy in `shadow_program_0.bin`).
* With the live constants and `bank_of = fs1r_bank`, the chain runs: steps 0x10c / 0x114 produce 0.6 / 15.15
  where zero constants gave 0.
* The **bank map still does not close the loop**: the output stage 0x174 (bank 8) reads r[0x35] which only bank 9
  writes, and no uniform shift -4..+4 of `fs1r_bank` makes the group's output nonzero. Two readings are
  consistent with the data and cannot be told apart from the program text: (a) the output stage reads a
  *previous pass's* state (a per-pass pipeline, not a same-pass chain), or (b) the bank of a channel's steps is
  set by the chip's channel allocation rather than by step position.

What would settle it, in one round, without any register writes (all reads + ordinary MIDI):
1. A note through a filter channel, then **stop the note and read `DAT_01068F78` again** - if the per-channel
   constants persist, the table is the channel's state and the model can be seeded from it directly.
2. The same note with the **filter channel number forced** (performance part 1 filter = 1, 2, 3, 4) so the
   mapping channel -> step stripe is read off the unit rather than inferred: with one voice, `probe_check`
   reports which segment changes.
3. If (1) shows the table is state, `vop3_filt_replay.py` can be seeded from it and the only remaining unknown
   is the input cell - which `op5s1` (the output stage's op changed to 5 = the chip input) already measures.

**Session 38s: the "Hall1 misses on R at 5002" note is stale - the s36k capture rule fixed it.**
`2026-10-01-215125-s25` (the Hall1/Hall9/no-effect step responses) is now exact past 6000 samples on **both**
channels and every config: rev1_B and rev9_B read first-bad 6000/6000 with err/unit rms 0.0003 / 0.0006, where the
session-28..35 notes recorded a miss at 5002 R. The board's sum is unchanged only because that sum counts
segments that were already clean past the window: the residual is now concentrated in the `_C` segments and the
`ut3c_mem1ra` family (err/unit rms 16.6 and 0.99), which are the ones worth attacking next. Recorded so the next
session does not chase a 5002 that no longer exists.

**Session 38t: the _C silence - a class-2 step with rA = 0 is not a discard; it feeds the next step's `s`.**
(FS1R.unlock `2026-10-01-215125-s25`, rev1_A/B/C.)

The probe sweeps step `1f1`'s constant (a Dry/Wet and High Damp parameter step, `docs/vop3_2_params.md`):
`_B` sets it to 16, `_C` sets it to 0. The unit's right output falls from rms 21.4 (`_B`) to **0.8** (`_C`);
the model gives 22.8 for both. So the model misses a real effect of this step.

`1f1` is `op 0, rA = 0, rB = 0x78, rd-en 0`, and `r[0x78]` is written by eight class-2 steps (012, 039, 088,
091, 09a, 14d, 195, 1ec) and read by fourteen. With `rA = 0` the model writes no register, so the step's
`y = s + k * r[0x78]` survives only as the running value `s` for the **next** step - which is the mechanism the
sweep is measuring, and the model only carries it when the next step reads `s` (rB = 0). The shipped step after
`1f1` is `1f2` (`op 0, rA = r05, rB = r77`), which takes `x = r[rB]`, not `s`, so the model drops the step's
contribution entirely and reads the same value whatever the constant is.

That is a concrete, testable rule: **`s` must accumulate across steps that do not read it**, i.e. a class-2
step's `y` is the running value for the next step even when the next step's operand is a register (the step's
own `s` term is added to whatever the next step computes when it uses `s`). The model resets/ignores `s` unless
the next step reads it. Fixing it needs the 398-probe self-check to stay green and the board sum to not regress;
the `rev1_C` segment (unit rms 0.8 vs model 22.8) is the test.

**Session 38t (correction): the rev1_B/C pair is carried state, not the 1f1 rule.**
`rev1_A/B/C` are `voice: off` configs: `_A` writes the effect's input steps with a note from the config's own
voice, `_B` and `_C` only sweep step `1f1`'s constant. With no source, the model gives 0 for every pass
(traced: `acc` 0 at 1f0/1f1/1f2, `r[0x78]` 0), yet the unit reads rms 21.4 in `_B` and 0.8 in `_C` - i.e. the
unit still holds the **decay of the preceding segment's note** and the `_C` write changes how fast that tail
falls. That is the same carried-state situation as `ua4k*`, not a step rule: the model cannot reproduce a
segment whose content is the previous segment's tail unless the tail is replayed with the same length and the
same parameter history. So the `_C` "miss" is a **probe-design artifact** (a no-source segment used as a
measurement), and `rev1_B`/`_C` should be read as "the unit's tail under a parameter change", not as
step-response data. The earlier s38t note above is withdrawn: with a source the model carries `s` through
`1f1` correctly.
What this says for the board: segments with `voice: off` and no source in a `keep` chain measure the previous
segment's decay, so they must be excluded from a first-bad sum or replayed with the preceding segment included.

**Session 38u: VOP3-1's staged table is the chip's LIVE PER-CHANNEL STATE, and it is readable.**
(rig `vop1_state.py`; one performance with part 0 filter 1 and part 1 filter 2, notes on MIDI ch 0 and ch 1.)

Reading `DAT_01068F78` (512 u16) at four points:

| point | nonzero | words that moved vs idle |
|---|---|---|
| idle (no note) | 148 | - |
| channel 0 note | 149 | 0x15, 0x48, 0x85 |
| channel 1 note | 150 | + 0x18, 0x4c, 0x86 |
| after both stop | 150 | unchanged - **it persists** |

Each note adds exactly three words, and the second channel's three are at a **stride of 3** from the first's
(0x15/0x18, 0x48/0x4c, 0x85/0x86) - the same interleave the ISA doc records for the channels' steps. So the
table is the chip's live per-channel constant/state table, indexed by channel, and it survives note-off.

This is the seed the VOP3-1 replay needs: `docs/vop3/coefficients_state_ch0.bin` / `_ch1.bin` hold it for a note on
channel 0 / channel 1. The model can be driven with the state the unit actually holds, which removes the last
guess (which constants the firmware patched) from the comparison. Combined with `op5s1` in
`2026-09-30-10` (the output stage's op changed to 5 = the chip's input), both sides of the filter are measurable.

**Session 38v: the constants were not the blocker - nothing in the model's VOP3-1 output path fires, whatever the seed.**
Four coefficient sets (EPROM 132 nonzero, the live table 149, the per-channel state at idle 148 and after a
note 150) each with the input driven into r[0x7f] / r[0x64] / nothing: every combination leaves the model's sum
bus at zero. So the silence is structural, and the constants are a red herring for it.

What that rules out and what it leaves:
* Ruled out: "the model runs with zero constants" (the s38q diagnosis). With the unit's own 150-word state table
  it still produces nothing.
* Left: the **channel stripe / bank assignment** - which steps belong to the channel whose group is at 0x108 -
  or the **output mechanism** (the op-1 route-0 steps summing into `bus`, which no VOP3-2 program uses and which
  has therefore never been validated on VOP3-1 against a recording).
The program's own shape says the output stages are the op-1 steps at 0x14c, 0x150, 0x154, 0x158, 0x16c, 0x16e,
0x170, 0x172, 0x174, 0x176, 0x178, 0x17a, each reading a register (`r62`, `r63`, `r65`, `r68`, `r34`, `r3a`,
`r40`, `r46`, `r35`, `r3b`, `r41`, `r47`) that the same group's op-0 steps write one step later in the same
stripe. Which stripe those pairs share is the one unknown, and it needs a measurement: set one step's constant
and see which segment of a recording changes (the session-8 method, which works), rather than more inference.

Harness state for whoever picks this up: `tools/vop3_filt_replay.py` loads the full program, accepts any
constant table, and compares against the measured input/output pair in `2026-09-30-10`. The remaining work is the
stripe map, not the arithmetic or the constants.

**Session 38w: VOP3-1's blocker is now exactly one thing - the register bank of a channel's steps.**
(`vop1_coefs2.py` with the filter wide open; the interpreter traced with `inp = 0.25`.)

* The constants are **not** the blocker after all: with the cutoff wide open the live table still reads 149
  nonzero and the class-1 loads at 0x012/0x015/0x018 carry `0c0d` (the cutoff), while the output-stage steps
  (0x14d, 0x174, ...) genuinely carry `k = 0` - `0x14d` is `op 0, rA = r31, rB = r31, k = 0`, i.e. `y = s`, so
  those steps are pure state carriers. That is by design, not a bad dump.
* With `inp = 0.25` driven for 30 passes the chain **is live**: `r[0x0c]` reaches 1.18 and steps 0x011/0x109/0x10c
  produce -224, exactly as the hardware's class-1 sign rule predicts. So the input path, the arithmetic and the
  constants are all fine.
* What stays at **0** is every register the output stages read (`r31`..`r35`, `r62`..`r68`), because those steps
  are in a different bank than the steps that write them: `0x14d` (which writes r31) is `fs1r_bank` 8 while
  `0x174` (which reads r35) is bank 8 too but `0x175` (which writes r35) is bank 9. The stripes the documented
  `fs1r_bank` assigns do not match the stripes the program's own dataflow implies.
* So the single remaining unknown is the stripe map - and it is one bit of information per group that only a
  measurement can supply: set one output-stage step's constant and see which segment of a recording moves (the
  session-8 method, already proven to work on this unit), or set a class-1 load's constant and see which
  channel's corner moves. Everything else in VOP3-1 is now in place: program, constants (live, dumped), input
  path (works), arithmetic (verified), and the harness (`tools/vop3_filt_replay.py`).

**Session 38x: VOP3-1 status - everything measured except one mapping, and the clean way to get it.**

Done and measured this session (all in `Vop3Filter` / the docs):
* The program (512 steps), its block/lane structure, the parameter file r[0x65..0x6c], the coefficient cells
  r[0x64/0x67/0x6a], the sum-bus output (no d[10]/d[11] write), and the coefficient port r[0]/r[11].
* **No audio register exists**: the firmware writes only parameters, program words, tags and control bits into
  VOP3-1's window (exhaustive over every caller of FUN_0000B5E2).
* The window is **write-only** (reads the floating pattern 0x644d/0xfffe like its 0x800100/0xC00000 neighbours).
* **The live constant table is readable RAM** and is the chip's per-channel state: each note adds three words at
  a stride of 3 and it persists after note-off (`coefficients_state_ch0/ch1.bin`, `coefficients_live_0.bin`).
* With the input driven, the chain **is live** in the interpreter (r[0x0c] 1.18, class-1 sign rule exact).

Not settled: **which stripe of steps belongs to the channel** whose output is the recording. Deduced (and it is
a hard constraint): steps 0x174 and 0x175 are adjacent and share the output register r35, so they must share a
bank - the documented `fs1r_bank` puts them in 8 and 9. But neither `fs1r_bank` nor two contiguous alternatives
makes the recording match, so the recording's segment roles (`ref` = filtered, `op5s1` = input) are also not yet
confirmed on this take; the two unknowns have to be separated by one round.

The clean round, all reads + ordinary MIDI, no register writes (the s38a-j lesson is that monitor register
writes to VOP3-1 are not worth attempting again):
1. Filter channel 1, one note, and record `DAT_01068F78` **per segment** while session 8's proven method
   rewrites one step constant at a time. The segment whose output moves names the step's channel - that is the
   stripe map, measured the way the unit has already proven it can be measured.
2. Record the same note once with the filter off and once on, in one take: that is the input/output pair, with no
   reliance on interpreting an op-code patch.

**Session 38y: VOP3-1's input/output pair, measured cleanly.** (FS1R.unlock `2026-10-03-002654-s38`.)
One take, one note, the same voice: the performance's part 1 with the **filter off** then **filter on**.

| segment | R rms | 20-40 | 40-80 | 80-160 | 160-320 | 320-640 | 640-1280 | 1.2-2.5k | 2.5-5k | 5-10k | 10-20k |
|---|---|---|---|---|---|---|---|---|---|---|---|
| filter off (input) | 0.0229 | 23.7 | 26.7 | 28.8 | 33.5 | 35.8 | 38.8 | 42.1 | 45.9 | 49.9 | 49.6 |
| filter on (output) | 0.0138 | 23.4 | 28.2 | 30.1 | 34.6 | 50.1 | 36.8 | 14.7 | -3.6 | -21.0 | -22.8 |

A textbook lowpass: the top three bands fall 21-26 dB, the 320-640 band gains +14 dB (the resonance at cutoff
64), and everything below 320 Hz is untouched. So VOP3-1's function on this voice is confirmed end to end
without reading a single op code, and this pair - not the session-9 `op5s1` patch - is the input/output pair a
replay should be scored against. `vop3_filt_replay.py` is being pointed at it.

**Session 39: a step's CONSTANT poked through FUN_0000B6A4 does not change the sound; only the step WORDS do.**
(FS1R.unlock `2026-10-03-003449-s39`.) Eight candidate steps (0x10d, 0x111, 0x115, 0x14f, 0x174, 0x175, 0x150,
0x16d - the ones the output stages and the stripes are built from), each written to 0x7fff and then 0x0000
through the firmware's own coefficient path (`STAGED[step] = v` then `FUN_0000B6A4(1, STEP_LIST + step*2)`), with a
note through filter channel 1. Every one of the 16 segments lands within +-0.7 dB of the reference on level and
within a few dB per band (the low bands' 1-4 dB movement is the segment-onset transient, and `ref_end` shows the
same). Contrast session 8: rewriting a step's **words** (its op code / fields) through `FUN_0000B5E2(0, step)` plus
the word registers moved the filter hard (62/96 segments). So on VOP3-1 the *program words* are the live,
changeable state and the per-step constant is not reached the same way - or the constant this voice uses is not
the one at that index.

That closes the poke route to the stripe map. The stripe is still the one unknown, and the only method left that
is known to work is the one session 8 used: rewrite a candidate step's **words** (e.g. clear the step, or change
its op) and see which segment moves. `docs/vop3_ship/39a.probes.json`-style, but that needs the s25 runner rather
than a one-off. Recorded so the next round starts from the method that works instead of the one that does not.


## Session 40 (2026-10-15): `w2[5]` and the transfer window

Seven s25 takes on the unit, run over SSH. `w2[5]` (`r8[5]`) makes a RAM access, read or write, address the RAM at
the slot offset without the ring pointer; marks transfer on every phase from step 3 (steps 000..002 transfer
nothing); a capture takes the latest transfer made 5..11 steps back (s36k had 3..11). The full write-up, every
probe and the numbers, is `docs/vop3_an_ram.md`, section "Session 40", and `docs/findings.md` 2026-10-15.

## Session 41 (2026-10-15): the effect LFOs' rate and depth

**41a/41b, rate** (FS1R.unlock `2026-10-15-153259-s25`, `-155937-s25`; `docs/vop3_ship/41a`, `41b`): a 1 kHz sine
through eight modulation effects, one at a time, each with its first ROM performance's effect block (Full B3's
Rotary SP and 2WayRotary, Vokodrone's insertion flanger from the demo song). The unit modulates the tone at the
Data List's LFO frequency and its odd harmonics; the engine modulated at twice that in all eight, because its LFO
swung -1..1 and the delay / allpass sweep folded through zero. On the effect chip each wave spans 1: the saws
-0.5..0.5, the triangle 0..1 (a centred triangle doubles both phasers, a 0..1 saw doubles both rotaries, so each
was fitted on its own). With that all eight land on the unit's three strongest envelope and FM lines.
Reported by WouterVanNifterick (Chorus too fast) and rgwan (Vokodrone's flanger).

**41c-g, the LFO itself** (FS1R.unlock `2026-10-15-173459-s25`, `-181203-s25`, `-183244-s25`, `-193346-s25`;
`docs/vop3_ship/41c`..`41g`). A free step (107, or 187 in the No-Effect variation window) moves LFO 8 / 12 into
m[2c] and step 0d0 plays m[2c] at k = 1/128 on the right DAC.

* 41c: chorus, flanger, Rotary SP saws read 0 .. +128 (readout 0..4), rising, mean +2.0; Phaser1's triangle 0 ..
  +44.3. **The waves are unipolar**, not the -0.5..0.5 the 41b fit assumed.
* 41d: the Rotary SP's two class-1 LFO readers (15c -> r[7b], 15d -> r[7d]) tapped: -4..+4, 2.407 Hz, sd 2.28 /
  2.34; the engine under the unipolar saw 2.32 / 2.29 (the centred saw gave the same, the reader wraps). The
  chorus's r[7c] reader: unit mean -0.75 sd 2.21, engine 0.28 / 2.52.
* 41e (m[0c] tapped directly) read nothing in the engine and was stopped before it finished; 41g replaces it.
* 41f: Vokodrone's flanger at FB words 4/34/64/94/124: the unit's notch depth at each, the engine's level 1.3x
  throughout; the FM excess (4.3x) is the same at every FB, so it is not the feedback loop.
* 41g: 2WayRotary's LFO (direction 0xC000) reads **-64 .. +64 rising**; Vokodrone's flanger (direction 0) 0 .. +128.
  So **direction bit 15 centres the wave** (subtracts half the range); s41b's "-0.5..0.5 saw" was this, applied to
  every LFO.
* The unipolar waves plus the direction rule keep all eight 41b effects on the unit's lines (the 41b check rerun).
  Rejected along the way: a 0..1 saw with direction as negation (2WayRotary's envelope lines out of order), class-1
  readers saturating instead of wrapping (no change), `idxw` index scales x2/x4/x8 (no change on Vokodrone).

Open: the modulation depth (envelope 0.4-1.1x, FM 0.04-0.8x, Vokodrone's flanger FM 4.3x); next, the delay-index
steps (1b4 / 1d3) tapped on the unit.

**41h-l, the flanger depth** (FS1R.unlock `2026-10-15-201341-s25`, `-202658-s25`, `-203846-s25`, `-231352-s25`;
`docs/vop3_ship/41h`..`41l`). Each probe makes one step also write its result to m[11] (the right DAC).

* 41h: Vokodrone's delay-index steps 1b4 / 1d3 read 0 .. 0.2715 on the unit, a triangle at the LFO rate, 1b4 and
  1d3 in antiphase; the engine's 0 .. 0.2714, mean and sd within 3%. The LFO reaches the delay address at the
  unit's depth.
* 41j: 1bd and 1ba write nothing on either side; 1a9 (r7d's load) +7.53 / sd 1.51 on the unit, +7.60 / 1.41 in the
  engine. m[2a] / m[2b] read 0 on both.
* 41k: the interpolated tap 1d5, the feedback source 1e9 and the output 1f5: level and envelope within 20%, but
  the unit's taps exceed the engine's +-8 clip (|R| to 2.77 readout, the engine pinned at 2.000). **The DRAM word
  is wider than +-8**: the s14 +-8 was the DAC. SAT is now 32 (d[]'s measured +-32, s38l), unbounded beyond +-11.
* 41l: the variation Flanger's and Chorus's index steps 16c / 182 match the engine to 2% (Flanger 0.0272 / 0.0157
  mean / sd on the unit, 0.0261 / 0.0157 engine; Chorus 0.4825 / 0.2676, 0.4822 / 0.2733).
* **The 4.3x was the harness.** `mod.cpp` drove the insertion bus with a 0.25 sine, hotter than the rig's sine
  voice (0.18 matches the unit's dry level), and at 0.25 the feedback node clips at +-8 and the flanger's FM
  quadruples. Rendered through the whole device from the same performance and voice bulks the rig sends
  (`/tmp/voko_mid.py` pattern: an SMF of the two bulks and note 60), Vokodrone's FM sd is 0.0028 against the unit's
  0.0030, and was 0.0051 before s41 (the pre-s41 doubled LFO is what rgwan heard).
* Hikari: VOP3-1 and VOP3-2 are one chip, so one LFO rule. `LFO_WAVE` bit 12 is the only LFO bit the two hosts set
  differently (the filter host boots every channel to 0x1000), so `Vop3::lfo` now reads bit 12 as the range
  selector: clear = unipolar 0..1 with 0x8000 centring (VOP3-2, measured), set = bipolar -1..1 with 0x4000 halving
  (VOP3-1, `20_fltmod` LFO2 envelopes 6.1 dB rms against 12.7 dB unipolar). Inferred, not probed: no take has
  flipped bit 12.
* The "deeper notches" of the variation Flanger, Chorus and insertion Phaser1 were the measurement. Every s25 take
  carries a one-sample phase glitch every 1.451 s, in every effect, read as FM spikes at the notches; and 41b's 4 s
  holds cannot show the 0.137 Hz chorus. On 41a's 40 s hold the variation Chorus's FM is a square of 11.10 Hz
  step on the unit, 11.08 in the engine. Tried and rejected: the fraction inverted (`1 - f`): it puts the chorus
  wobble at the unit's sd only by adding 18 Hz noise, and breaks insertion Chorus (FM sd 4.5x).
* With the +-32 clip, Vokodrone's 41k taps (1d5, 1e9, 1f5) match the unit within 6% over p50..max.

