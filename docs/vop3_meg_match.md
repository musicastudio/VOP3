# VOP3 against the EX5 MEG: step matching

`python tools/vop3_meg_match.py` asserts everything below that is marked **matched**. Started 2026-10-01 as route 1 of `docs/vop3_blockers.md`.

The MEG (EX5, decoded by MAME, `tools/meg_interp.py`) shows its arithmetic outright; the VOP3 does not. What lets a VOP3 step be paired with a MEG step by role, rather than by guessing at shape, is the FS1R firmware: `docs/vop3_2_params.md` names which step every effect constant goes to and what the constant is. The reverb combs are the first structure where both sides are pinned.

## Matched: the six reverb combs

FS1R reverb types 1-8 load six combs through `FUN_0039C796`, which writes four constants per comb to the step list at 0x35DB6C (`docs/vop3_2_params.md`, "Reverb combs"). Read back through the disassembler, every comb is the same four-step unit in the base program:

| comb | `k*in` (op 7) | op 4 | `s + k*(r+r)` (op 0) | DRAM write (op 5) | state register |
|---|---|---|---|---|---|
| 1 | 0a3 | 0a4 | 0a5 | 0a9 | r[37] |
| 2 | 0a6 | 0a7 | 0a8 | 0ad | r[38] |
| 3 | 0aa | 0ab | 0ac | 0b1 | r[39] |
| 4 | 0ae | 0af | 0b0 | 0b5 | r[3a] |
| 5 | 0b2 | 0b3 | 0b4 | 0d1 | r[3b] |
| 6 | 0d2 | 0d3 | 0d4 | 0d7 | r[3c] |

The firmware's constants on those steps, in that order: `x = h(time) * b0`, `x` again, the damping LPF's `-a1`, and the comb feedback `g(time)`. The EX5 reverb (`EX5_TG1.bin` 0x2AEDEC, 152 steps) has six damped combs too (MEG 078, 07b, 07f, 083, 087, 08b, state r12..r17), each `c*m(new) + c*m(old)`, then `(c*r + p) << 1 -> r` (the one-pole LPF), then `mw = c*m04 + r` (the delay write: input plus filtered feedback).

Asserted by the script: the four ops of every unit, that the op-0 step and the op-5 step read the same register (`rA = rB` on op 0, `rB` on op 5), that the op-5 step is the unit's DRAM write, and the MEG's six `c*input + state` writes.

**Reading, not measured** (each needs the interpreter or a capture to confirm):

* The op-0 step with rA = rB is the comb's one-pole damping filter and its register the filter state: its constant is the LPF's `-a1`, and `x = r[rA] + r[rB]` doubles the register, the same doubling the MEG writes as `(c*r + p) << 1`.
* The op-5 step writes `input + g * state` to the delay line, as the MEG's `mw = c*m04 + r`. The listing's `sgn(s)` for op 5 was measured on a VOP3-1 output stage; with a gain on it and a delay write behind it, op 5 here must be doing something else, or its meaning depends on route or rB.
* The op-7/op-4 pair applies the LPF's `b0` and `b1` (equal for this first-order LPF, and the firmware writes the same word to both), as the MEG's `c*m(new) + c*m(old)`. The listing reads op 7 as `k*in` (daddr 0, so the chip input); for a comb its source should be the delay tap, so either op 7's source is not what the listing says on VOP3-2 or the tap arrives another way.

## Matched: the comb gain table is an RT60 design

The table at 0x36FA12 that `FUN_0039C796` indexes (`j = u8@0x36F9CC[time] + c - 0x38`) holds pairs `(k, h)`. On all 256 rows `h = sqrt(1 - k^2) / sqrt(24)` (0.1992..0.2088 against 0.2041), and `log2(-log10 k)` falls 0.0584 per row. So `k` is a comb feedback gain on a geometric time grid (the RT60 law `k = 10^(-3 L / (T fs))`, one row = a 2^0.0584 = 4.1% step in `T/L`), `c` is the comb's length class, and `h = sqrt(1 - k^2)` scaled by a constant is the standard gain that keeps a feedback comb's total power independent of its decay. The constant `1/sqrt(24)` is measured; why 24 is not. This is the Schroeder/Moorer comb design, which agrees with the roles above independently of the MEG.

## Matched: the combs decay at the documented Reverb Time, at 48 kHz

Each comb's length is the distance between its write slot and the slot its output is read from, both from the offsets the firmware writes (`params.json`, Hall1: write slots 2a, 2b, 2c, 2d, 34, 35; reads 22, 24, 25, 27, 28, 32; lengths 4974, 4267, 3587, 3150, 2453, 4466 samples). With `k` from 0x36FA12 at the firmware's index, `y[t] = x[t] + k y[t - L]` decays 60 dB in:

| Reverb Time | comb RT60 at 48 kHz (s) |
|---|---|
| 0.8 | 0.81 0.81 0.80 0.80 0.79 0.82 |
| 2.0 | 1.97 1.98 1.96 1.94 1.93 1.99 |
| 5.0 | 4.99 5.03 4.97 4.93 4.89 5.05 |
| 20.0 | 19.74 19.91 19.69 19.50 19.36 20.01 |

Every comb lands within 4% of the Data List's Reverb Time from 0.8 to 20 s. The script asserts this, and the assertion fails at 44.1 kHz (11% off), with the read slots paired in reverse (x4), or with the gain index shifted by 8 rows (41%). Three sources agree on 48 kHz: the board (`hardware.h`), the delay offsets, and the gain table, which was designed for it. The bilinear filter designer runs at 44.1 kHz (`FUN_00037414`, table 0x371614), so the EQ corners on the unit are 48/44.1 = 8.8% above the documented frequencies, unless something downstream corrects for it.

The constants then sort themselves out:

* **`k` is the loop gain on its own.** The firmware writes `x = k * b0` to the op-7 and op-4 steps (`b0 = b1`, the damping LPF's taps), and the LPF's DC gain `2 b0 / (1 + a1)` is below 1 (0.72 at Hall1's 1.0 damp). The loop `k * LPF` decays faster than the documented time (1.81 to 1.88 s against 2.0 at time 2.0, falling to 0.86 of the time at 20 s, in a 48 kHz run with the LPF in the loop). Only `k` alone gives the documented time, so the LPF in the comb is normalized to unit DC gain on the chip (the word `b0` is `k * b0`, and the step scales by `1 / (b0 + b1)` somewhere), or the documented time is defined at the comb's DC loop without the damping, which is how Moorer specifies it.
* **`h = sqrt(1 - k^2) / sqrt(24)` is the input gain, on the op-5 step.** That step is the comb's DRAM write (`dram[2a]`), so its word scales the input as it goes into the delay line: `write = h * in + feedback`. The listing's `sgn(s)` reading for op 5 does not apply to these steps.

## What this closes and what it does not

Against `docs/vop3_blockers.md`:

* **Blocker 1 (no memory) and blocker 4 (`w[]` never read):** if the reading above holds, the comb's state lives in `r[37..3c]`, read twice by the op-0 step and once by the op-5 write. Nothing in the decoded program writes those registers, so the candidate state path is **the op-0 rA = rB step writing its result back to `r[rA]`**. This was tested directly on VOP3-1 channel 0 (72 steps of the same shape) and decides nothing, because an impulse on any input register does not reach the output even in the first pass: the filter's signal path is broken somewhere before the state question can be asked. The VOP3-2 combs are the cleaner test, once blocker 2 lets the reverb produce output.
* **Blocker 3 (class 3):** not touched. The comb units are all class 2; the class-3 steps in reverb 0 are a pair per comb around them, so they are the next thing to pair against the MEG's delay-read and allpass steps.
* **Blocker 5 (`rd-en`):** none of the comb steps sets it.

The op-5 reading above contradicts the listing's `sgn(s)`, which was measured on a VOP3-1 output stage; both can hold if op 5's meaning depends on route or rB. That needs one capture: a known value in `r[37]` with step 0a9's constant set, and the DRAM word it writes.

## Next

1. Pair the reverb's class-3 steps (`0e8`, `0ea`, ... in `reverb_00`) against the EX5 reverb's allpass and early-reflection section (MEG 000-075: `mem_r` taps, `m3x = p` feedbacks), using the `FUN_0039CF82` early-reflection constants the same way.
2. Run the six comb units through `tools/vop3_interp.py` itself. The comb decay above is from the firmware's constants and offsets alone; the chip's own data path for the unit (op 7 reading the delay tap, op 4, op 0 with rA = rB, op 5 writing `h * in + feedback` to `dram[2a]`) is the part that has to reproduce it, and doing so would close blockers 1 and 4 for VOP3-2. The interpreter's `k*in` for op 7 has to become the delay tap for that (`d[38] <- xfer` in the step before), which is a change to measured meanings and needs one capture to confirm.
