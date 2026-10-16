# VOP3

An emulation core for the Yamaha **YSS236-F (XT013A00) "VOP3"** DSP, the microcoded chip behind the FS1R's filter (VOP3-1) and effects (VOP3-2), the AN1x's synthesis engine and the PLG150-AN board. It is two engines over one state: a reference **interpreter** and an **AsmJit compiler** that turns the loaded program into one straight-line native function, for x86-64 and arm64. The two are bit-exact against each other: a test runs them side by side and requires their whole state to match, byte for byte, after every pass. Against the real chip the core matches to within a few LSB (see [How exact it is](#how-exact-it-is)).

Every rule the core follows was read from firmware or measured on a real FS1R, and `docs/` holds that evidence: the instruction set as measured, the research journal session by session, the extracted programs of every known VOP3 image, and their disassembly.

## How the chip works

The VOP3 runs a **512-step VLIW program once per sample** (24.576 MHz / 512 = 48 kHz). The host CPU writes the program through a small register file and then only touches constants, delay offsets, LFOs and EGs. Nothing is computed outside the steps: every level, pan, send, delay time and filter coefficient a user changes is a step constant or a slot offset.

* **A step** is five 16-bit words (`w0..w4`, chip registers 0x0A down to 0x06), a 16-bit constant (0x0B) and a tag byte (0x0C). The words select a delay RAM access, an r-register write, an m-cell move, the multiplier inputs, the adder input, a result scale (x1, x2, x4, x16) and a result mode (wrap, saturate, clamp negatives, abs).
* **State**: the running value `p`, 128 r registers, 64 m cells, 16 t registers (multiplier gains), 128 delay slots into a 2^18-word delay RAM, 16 LFOs, 16 EG generators, two banks of 16 serial input slots and 16 serial output slots.
* **Pipeline**: a register written at step n is read from n + 3; m-cell writes and captures land three steps late; a delay RAM capture takes a transfer made 5 to 11 steps earlier. The core models each of these, since the shipped programs depend on them.
* **Control steps**: a gate holds a block of constant loads until the host's trigger (register 0x14), and conditional skips run a block only when the last compare was positive or negative.
* **I/O**: audio comes in and goes out through 64-step serial pages; a step moves a serial input, an LFO value or an EG level into an m cell, and a flagged step sends an m cell to the output slot of its page.

`docs/vop3_isa.md` is the full reference, field by field and register by register, with the evidence for each claim (**FW** from firmware, **CHIP** measured on a unit) and the session tags (`sNN`) that lead into `docs/vop3_research_journal.md`.

## Layout

* `src/yss236_core.h`: the microcode engine. Decoder, state and the interpreter (`step()`), header-only.
* `src/yss236_jit.cpp`: the compiler. Each step becomes only the code its decoded fields select; the arithmetic is the interpreter's, operation for operation, in double precision with no FMA.
* `src/vop3.h`, `src/vop3.cpp`, `src/vop3_regs.h`: the chip around the core. The host register file, auto-increment uploads, the LFOs and the EG generators.
* `tests/vop3_jit_check.cpp`: compiled against interpreted, over every FS1R program image in `docs/` and random programs.
* `tools/vop3_disasm.py`: the disassembler. `python3 tools/vop3_disasm.py --listings` regenerates `docs/vop3_disasm/`; with no arguments it runs its self-check.
* `examples/minimal.cpp`: the smallest host.
* `docs/`: the research (below).
* `third_party/asmjit`: AsmJit, under its own zlib licence.

## Building

CMake 3.20 and a C++20 compiler (AsmJit needs it).

```
cmake -B build -S .
cmake --build build --config Release
ctest --test-dir build -C Release
```

This builds the static library `vop3`, the check and the example. Link `vop3` and include `vop3.h`. Build the library with the flags the CMake file sets (`-ffp-contract=off`, or `/fp:precise` on MSVC): the compiled and interpreted passes are bit-identical only without FMA contraction.

## Using it

```cpp
#include "vop3.h"

Vop3 chip;

// Upload: select a step, then write its five words (register 0x0A - k for word k) and its constant.
// SELECT | vop3::AUTO_INCREMENT advances the step after each write, as the FS1R's uploader does.
chip.write(vop3::SELECT, step);
for (int k = 0; k < 5; k++) chip.write(vop3::PROGRAM - k, words[k]);
chip.write(vop3::SELECT, step);
chip.write(vop3::COEF, constant);

// Delay slots, LFOs and EGs go through the same select latch: SLOT_HIGH / SLOT_LOW, LFO_SPEED..LFO_DIRECTION,
// EG_RATE / EG_TARGET / EG_AIM.

// Per sample: fill the serial inputs, run, read the outputs. A full-scale word is Vop3::WORD.
chip.core.in[0][slot] = sample * Vop3::WORD;
chip.run();                                   // EGs, LFOs, then the 512-step pass
double out = yss236_core::dac(chip.core.out[slot]) / Vop3::WORD;   // the 18-bit word the DAC plays
```

`examples/minimal.cpp` runs as is. A program image in `docs/` is big-endian, five words per step (the AN boards' images are eight words per row: `--an` in the disassembler); `tests/vop3_jit_check.cpp` has an `upload()` that loads one.

**Interpreter or compiler.** `chip.run()` compiles the program on its first pass and recompiles after any program word changes; constants and slot offsets are read from memory, so writing them costs nothing. Set `chip.core.interpret = true`, or `VOP3_INTERP` in the environment, to stay on the interpreter. On an architecture AsmJit does not target, `yss236_core::jit_target` is null and the interpreter runs alone. The state lives in the core either way, so a host can switch between the two at any pass.

**The core alone.** `yss236_core` needs no chip wrapper: fill `prog`, `coef` and `slot`, call `decode_all()`, set `in[]` and `lfo[]`, and call `pass()`.

## How exact it is

"Exact" means two different things here.

**The compiler against the interpreter: bit-exact.** The interpreter in `yss236_core.h` is the reference, and the compiled pass reproduces it bit for bit, which is what lets a host switch between them at any pass. Four things make that hold:

* **The same operations in the same order.** Each compiled step does the interpreter's arithmetic in IEEE double precision, one operation per operation. Where the interpreter divides by a power of two the compiled code multiplies by its reciprocal, which rounds to the same exact value.
* **No fused multiply-add.** An FMA rounds once where a multiply then an add round twice, so the library is built with `-ffp-contract=off` (`/fp:precise` on MSVC) and the compiler never emits one. Built without those flags, the two drift apart in the last bit.
* **The same clamps.** Saturation compares in the order the x86 `minsd` / `maxsd` instructions do, so NaN and signed zero come out the same; arm64, whose `fmin` / `fmax` treat NaN differently, compares and selects instead.
* **The hard cases call the interpreter's own code.** RAM captures, the delay index queue and a mode-0 result that wraps are handed back to the interpreter's functions rather than written twice.

`tests/vop3_jit_check.cpp` proves it on whatever machine builds it. Two chips, one compiled and one interpreted, run every FS1R program image in `docs/` and then random programs, coefficients and slot offsets on the same noise. After every pass it compares the r registers, m cells, t registers, outputs, running value, pipeline queues, delay index and the 2 MB delay RAM, and names the first field that differs. On x86-64 it compares 37,600 passes, all identical.

**The core against the chip: within a few LSB.** The core does not reproduce the chip's integer datapath. It keeps values as doubles at the chip's scales and applies the chip's ranges, wraps, saturation, pipeline delays and output quantization (`yss236_core::dac()`, 18 bits). Its rules were fitted to probes run on a real FS1R and read back from its digital output, and checked against takes of the shipped programs:

* 398 single-step probes on the unit, every one reproduced.
* The Hall1 and Hall9 reverbs from an impulse, over 40,000 samples: 0.6 and 0.9 LSB rms, at most 6 LSB, correlation 0.999996 and better.
* Room1 and Plate: 1.8 and 1.3 LSB rms over 16,000 samples.
* Delay RAM reads on the input lines: within 1 LSB over 9,000 samples.

What is left is rounding: where the chip truncates inside its datapath, the core carries a double, and the difference surfaces as single LSBs that accumulate in long feedback tails. The research journal has each take, from session 24 on.

## Research

* `docs/vop3_isa.md`: the instruction set and register map as measured. Start here.
* `docs/vop3_research_journal.md`: how each rule was found, session by session, with the readings later withdrawn.
* `docs/vop3_microcode.md`, `docs/vop3_2_microcode.md`: the FS1R's filter and effect programs, and how the firmware assembles them.
* `docs/vop3_2_params.md`, `docs/vop3_2/params.json`, `docs/vop3_2/curves.json`: every effect type's parameters, the steps and slots each writes, and the word for every value.
* `docs/vop3_outputs.md`: the VOP3-2 mixer and output cells. `docs/vop3_pinout.md`: the 160-pin package.
* `docs/vop3_an.md`, `docs/vop3_an_ram.md`, `docs/an1x_param_map.md`: the PLG150-AN and AN1x programs, their table RAM and mask ROM, and the AN1x parameter-to-step map.
* `docs/vop3_meg_match.md`, `docs/vop3_alignment.md`, `docs/ex5_meg.md`, `docs/mu2000_meg.md`: the same Yamaha algorithms on the decoded SWP30 MEG (EX5, MU2000), matched step by step against the VOP3.
* `docs/vop3_blockers.md`: what stood between the disassembly and a running model, and what closed each.
* `docs/vop3/`, `docs/vop3_2/`, `docs/vop3_an/`, `docs/vop3_an1x/`: the extracted program images. `docs/vop3_disasm/`: their disassembly.
* `docs/vop3_ship/`, `docs/vop3_probes.json`: the probe sets run against the unit and the core's predictions.

What is still open is collected in `docs/vop3_isa.md` section 9. Some research documents cite scripts and capture folders that are not in this repo (the extraction and rig tooling that produced them); their results are recorded in the documents.

## Credits

* **Wouter van Nifterick** ([@WouterVanNifterick](https://github.com/WouterVanNifterick)): the microcode engine and interpreter, the chip model and the register map.
* **James Hansen** ([@jameshansen](https://github.com/jameshansen)): the AsmJit compiler and its check.
* **Zhiyuan Wan** ([@rgwan](https://github.com/rgwan)): the FS1R the chip was measured on, and the hardware sessions behind every CHIP rule.
* AI Models: Claude (Anthropic) assisted with the research, the tooling and this standalone release. GLM 5.3, GLM 5.3 Flash, Space Bunny, LongCat and Qwen Models used at various points during the research process.

## Licence

The code and documentation are released under the BSD 3-Clause licence, `LICENSE`. Every contributor, Wouter van Nifterick, Zhiyuan Wan and James Hansen, agreed to this release under the BSD licence. AsmJit, in `third_party/asmjit`, keeps its zlib licence.

The program images in `docs/vop3*/` and the listings made from them are Yamaha's microcode, extracted from the FS1R, AN1x and PLG150-AN firmware. They are included as research material for interoperability, and the BSD licence does not cover them.
