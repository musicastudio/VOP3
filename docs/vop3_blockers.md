# From disassembly to a running core: the blockers and what closed them

When a rewrite of the FS1R's VOP3-1 (filter) and VOP3-2 (effects) from the disassembly was first attempted, the decode at the time could not run a shipped program. These were the gaps. All but one are closed, and the core runs every shipped FS1R program on the rules in `vop3_isa.md`. The session tags (`sNN`) lead into `vop3_research_journal.md`, which has the full account. The disassembler still prints the early field names (`w[]`, `d[]`, `rd-en`, `class`), and `vop3_isa.md` appendix A maps them to the core's.

| # | blocker | status | resolution | sessions |
|---|---|---|---|---|
| 1 | The decoded VOP3-1 filter had no memory | closed | a class-2/3 step writes its result to r[`dr`], and `rcopy` stores the previous step's operand (z^-1). Registers keep their values across passes, which is the filters' and combs' state | s17-19, s24, s27 |
| 2 | The assembled VOP3-2 program reached no output | closed | `sout` sends an m cell to a serial output slot; the main pair is m[10] / m[11], the individual pair m[28] / m[29] (`vop3_outputs.md`). The effect sends arrive in r registers the program reads in place | s22, s27, s35 |
| 3 | Class 3 was not decoded | closed | class 3 is class 2 at unity gain | s19, s24 |
| 4 | Nothing read `w[]` | closed | the field the early decode took as a `w[wdst]` write index is the m cell a step reads (`sm`), as its multiplier or adder input (`m2m`, `asel`). The core keeps one m cell file, written through `dm` (the early `d[]`) and read through `sm` | s29-s33 |
| 5 | `rd-en` with rB set was measured on op 1 only | closed | `rd-en` is bit 1 of `m1`: the multiplier gain comes from t register `rsrc` instead of the constant. s19 measured those gains as a fixed 16-entry table; the core reads it as the t file, which steps write through `tw`, and runs the shipped programs that way | s19, s24 |
| 6 | Class 1 with rB set | closed | a class-1 step adds its adder input to the loaded constant | s19, s24 |
| 7 | Delay RAM marks off phase 3 | closed | a mark transfers on every phase from step 003 on; steps 000..002 never transfer. The old "phase 3 only" was the capture window, 5..11 steps back | s40 |
| 8 | The constants and slot offsets are not in the program images | closed | read out of the firmware's parameter handlers: every effect type's step constants and slot offsets for every parameter value are in `vop3_2/params.json` and `vop3_2/curves.json` (`vop3_2_params.md`). VOP3-1's constants are in `vop3/coefficients_*.bin` | |
| 9 | Inferred rules: per-channel register banking, what fills the unloaded r registers, the output word | partly open | the unloaded r registers are the chip's inputs, written by the hardware before the pass (s22, s35). The DAC word is `yss236_core::dac()`. **VOP3-1's register banking per filter channel is still inferred**: which bank a channel's steps share has not been measured | s22, s35, s38q-s39 |
