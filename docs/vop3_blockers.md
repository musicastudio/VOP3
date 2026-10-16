# VOP3 rewrite blockers

What stops a C++ rewrite of VOP3-1 (filter) and VOP3-2 (effects) from the disassembly, as found on 2026-09-30 when the rewrite was attempted. The decode in `docs/vop3_isa.md` is measured field by field on feed-forward test chains, and every one of those measurements holds. The blockers below are what those chains never exercised. `python tools/vop3_gaps.py` asserts the first three and fails as each closes.

The fitted models were not a reference for the rewrite: the chip replaced `vop3_effects.h` on 2026-10-13 and the filter model in `vop3_filter.h` on 2026-10-14. The only acceptance test is the demo pair: `captures/raw/FS1R DEMO.flac` (with VOP3) against `FS1R.unlock/captures/2026-09-22/fs1r_demos_without_vop3.flac` (without).

## 1. The decoded VOP3-1 filter has no memory

An impulse on any register that filter channel 0 reads and no class-1 step loads (`r[02..05]`, `r[0e]`, `r[10]`, `r[11]`, `r[62]`, `r[63]`, `r[65]`, `r[7f]`) leaves in the same pass with no tail. A filter without state is not a filter. Feeding class-2 results back into `r[n]` or `r[40|n]`, in the same pass or the next, changes nothing, so the state path is something the decode does not have yet. Candidates: `path` `r9[11:8]` (exact-match on the feedback loop, session 8; inert feed-forward, session 12), class 3, and blocker 4.

## 2. The assembled VOP3-2 program reaches no output

Base image plus reverb 0, variation 0 and insertion 0/0 at the `FUN_00039C8A` windows, audio on the input port, every constant 0.5: zero non-zero output samples in 4096. The interpreter's only output is `bus += y` on an op-1 step with route 0, a reading taken off VOP3-1's output stages (session 10), and no shipped VOP3-2 program has such a step. Only test image 1 names its outputs (`0e9`/`0eb`, op 7 reading `d[]`). Which steps of a shipped program feed the DAC, and by what mechanism, is unknown.

## 3. Class 3 is not decoded

`r6[15:14] = 3` was seen once, as "the loop runs away" when a class-2 step was flipped to 3 (session 8). Shipped programs use it deliberately: 32 steps of VOP3-1, 27 of the assembled VOP3-2, and 16 of reverb 0's 29 live steps (`0e8`, `0ea`, `0ec`, `0ee`, ... a pair per comb, around a class-2 accumulate). The reverb cannot run without it.

## 4. Nothing reads `w[]`

Class-2 results are written to `w[wdst]`, which the decode reads only through `rsrc`, and session 14 found `w[rsrc]` never enters the arithmetic. So every `-> w[n]` write in the listings goes nowhere the model can see. A write area the program fills on almost every step must be read somewhere, and this is the most likely seat of the missing state in blocker 1. The `r[]` stride test (`tools/vop3_verify.py`) rules out `w[n]` as `r[40|n]`.

## 5. `rd-en` with rB set is measured on op 1 only

Session 14 measured `rd-en` with rB set as "k is dropped" on an op-1 step (`y = x`). The disassembler and interpreter generalize that to `g = 0` on every op, which makes op 2 and op 3 print `y = 0`. Shipped programs use the combination on every op: VOP3-1 has 64 op 0, 48 op 1, 16 op 2, 64 op 3 and 32 op 5 steps; VOP3-2 76, 20, 14, 76 and 10. So 80 VOP3-1 steps decode to a constant zero, which no program would ship. Whether "dropped" means `g = 0`, `g = 1`, or that `w[rsrc]` replaces `k` on ops other than 1 is open, and the answer bears on blocker 4.

## 6. Class 1 with rB set

81 class-1 steps across the FS1R listings carry an rB (`r[68] = k/256 rB=r[62]`), and on class 1 every other `r6` bit was inert (session 8). The role of that rB is unknown; the decode treats these as plain loads.

## 7. Delay-memory marks off phase 3

Session 16 found DRAM transfers only on steps 3 mod 4 and reads on phase 0 or 2 transfer nothing. The shipped VOP3-2 programs carry 50 marks off phase 3: 30 writes (`mem1`) on phases 0, 1 and 2, 6 reads on phase 1 and 3 `mem6` marks. Off-phase writes and phase-1 anything were never tried. Yamaha would not ship 50 inert marks, so either the phase rule has more to it or these steps do something else. The AN programs press the point: their RAM is a lookup table (`docs/vop3_an_ram.md`), and PLG150-AN mode 0 puts 59 of its 95 table reads, the AN1x voice 66 of 95, on phases 0 and 2, including every read of the soft clipper. **Session 40 closed this (`docs/vop3_an_ram.md`): a RAM mark transfers on every phase from step 3 on; steps 000..002 transfer nothing; s16's "phase 3 only" was a capture taking a transfer 5..11 steps back, which the moved steps fell outside. Insertion program 13's shipped phase-0 reads carry its signal on the unit.**

## 8. The constants and offsets are not in the images

`FUN_00039652` writes each step's constant (register 0xB) from `DAT_0106842c`, and `FUN_000397F4` each slot's delay offset (0xD/0xE) from `DAT_0106882c` plus a per-selector base (`DAT_010683F8..08`). About thirty parameter handlers fill those arrays (`FUN_0039B09C`, `FUN_003A160C`, ...). Until they are read, a VOP3-2 program runs with no real coefficients or delay lengths. VOP3-1's constants are in the EPROM (`docs/vop3/coefficients_*.bin`) and patched per type and per tick (`FUN_0000D050`, `FUN_0000B6A4`).

## 9. Inferred, not measured

- **Register banking per filter channel.** Clearing channel 4's cutoff load closes channel 4 though four other steps load the same `r[61]` with the same word (session 8), so the interpreter banks `r[]` sixteen ways by step position. The banking rule is an inference.
- **What fills the unloaded `r[]`.** Below 0x40 nothing in a program writes them, so they are taken as hardware inputs (serial ports, internal generators). Which register carries which voice's audio into VOP3-1, and which carry the mixer buses into VOP3-2, is not mapped.
- **Output word and lag.** The 18-bit output (LSB 2^-17) and the 2-sample lag are measured (session 12) and not modelled.

## 10. The disassembler's self-checks did not catch this

`tools/vop3_disasm.py` and `tools/vop3_interp.py` both print `ok`: each reproduces the sessions it was built from and never runs a shipped program end to end. `tools/vop3_gaps.py` is that missing check.

## Routes that need no capture session

1. **The EX5 MEG, run by value.** MAME's `swp30.cpp` gives every MEG step explicit arithmetic. Port its step function next to `tools/vop3_interp.py`, run the EX5 AN program (`EX5_TG1.bin` 0x3C97D4) and the PLG150-AN/AN1x VOP3 AN program on the same patch, and match steps by signal. The MEG shows its state writes, feedback taps and output stage outright, which addresses blockers 1 to 5.
2. **The Yamaha iOS app's AN engine.** If it executes VOP3 microcode it is the whole ISA; if it is a native port, it gives values at every point the MEG gives only structure. AARCH64, so Ghidra reads it.
3. **The effect parameter handlers** (blocker 8), from the FS1R Ghidra database. They also label class-3 steps by role, as the steps a "reverb time" or "high damp" handler writes.
4. **The demo pair** as the acceptance test: a decode is right when the dry take run through it gives the wet take, song by song, which is several independent datasets.

A capture session closes any one blocker directly: class 3 on `0e8`/`0ea`, the output steps of a loaded effect, `rd-en` on op 2/3, and `path` with class 3 on VOP3-1's `098`/`0c4`.
