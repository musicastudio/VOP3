# EX5 as a reference for the FS1R's VOP3 (2026-09-30)

## What the EX5 actually runs AN on

From the EX5 service manual (archive.org, `Yamaha_EX5_Service_Manual`) and `roms/EX5_TG1.bin`:

| IC | part | role |
|---|---|---|
| main CPU | MB91103PF (Fujitsu FR20) | panel/sequencer, `EX5_CPU.bin` |
| TG CPU (IC10 flash) | MB91103PF (Fujitsu FR20) | tone generator driver, `EX5_TG1.bin`, REALOS/FR20, mapped at 0x200000 |
| IC44, IC45 | TC203C760HF-002 SWP30B | AWM2 + MEG, master/slave, registers at 0x500000 / 0x580000 |
| IC50 "SUB CPU" | D65621GF SMI | DRAM/SIMM address multiplexer. **Not a processor.** |

There is no DSP besides the two MEGs. AN, FDSP, VL and all effects are MEG microcode uploaded by
the TG CPU. The "SUB CPU" theory is dead: IC50 pins are `LMA*/HMA*/RAS*` only.

MAME (`src/devices/sound/swp30.cpp`, Galibert) documents the MEG: 384-step VLIW, 64-bit
instructions, 1.15 constant and 16-bit offset per step, 42-bit MAC, 127 r + 63 m registers,
8 t registers, 24 LFOs, reverb RAM with 8 banked mappings. 7 instruction bits remain unknown.

## Register map (TG firmware -> SWP30 control slots, matches MAME `rctrl`)

`slot = 0x40*(idx>>1) | 0xe | (idx&1)`, byte address `= base + slot*2`; base 0x500000 (master) or
0x580000 (slave; `0xA7BF14` selects which).

| address | MAME idx | meaning |
|---|---|---|
| +0x81E | 0x21 | MEG program address |
| +0x89C/89E/91C/91E | 0x22..0x25 | program word 1..4 (auto-increments after word 4) |
| +0x101C/101E | 0x08/0x09 | wave RAM direct-access trigger / status |
| +0x109C/109E, 0x129C.., 0x131C.. | 0x40.. | reverb RAM enable / address / data |
| +0x24 + 2*(6*a+k) | MEG/Data | constant slot (writer `0x25E108`, index table `0x2C4BA0`) |

Uploader is `FUN_00224C00` (loop of 0x180 steps, zero-fills the rest).
`FUN_0025DE8A(base, idx, value)` is the generic slot writer.

## MEG programs in EX5_TG1.bin (CPU addresses)

| what | address | steps | note |
|---|---|---|---|
| **AN voice, normal** | 0x3C97D4 (u16 count, then 254×u64) | 254 | uploaded from `0x2245E2` |
| **AN voice, alt** | 0x3C8D68 (count 247) | 247 | uploaded from `0x225678` (layer/AN+FDSP path) |
| AN lookup tables into reverb RAM | 0x3BBDD8 (4097), 0x3C6C68 (4096), 0x3C3868 (4096), 0x3C5C68 (2048), 0x3BECE4 (2048 sine) | | at revram 0, 0x1001, 0x2001, 0x3001, 0x3800 |
| insertion effects, 3 slots × 21 types | 0x2AA76C, 0x2B0C44, 0x2B4B44 (stride 0x300) | 96 | pc 0x120 / 0x60 / 0x00 |
| reverb | 0x2AEDEC | 152 | pc 0 |
| chorus-type | 0x2AE66C | 40 | pc 0x98 |
| two 48-step slots | 0x2B01C4, 0x2AF744 | 48 | pc 0xC0 / 0xF0 |
| FDSP, 10 algorithms × 2 halves | 0x2DB12C, 0x2DED2C (stride 0x600) | 192 | names in EX5_CPU.bin @0x464B8: Seismic, Ring Mod, Tornado, Self FM, Phaser, Flange, PWM, Water, EG Pickup, EP Pickup |
| FDSP init / silence | 0x2DA52C, 0x2DAB2C | 192 | |
| register-init | 0x3CA28C | 256 | |

Insertion-effect constant tables: `0x2D88F4 + 0x34*type`; per-slot pc/offset tables at 0x2D3944..

## Tools

- `tools/meg_disasm.py ROM OFFSET [COUNT]` — MEG disassembler ported from MAME.
- `tools/frdasm/` — MAME's Fujitsu FR disassembler with a stub `emu.h`; `frdasm ROM BASE START END`.
  Ghidra has no FR module, so this is the disassembly path for both EX5 ROMs.
  `frdasm EX5_TG1.bin 0x200000 0x200000 0x2e0000 > tg.asm` takes 2 s.

## What this buys the VOP3 work

The VOP3 encoding (512 steps × 5 words, `docs/vop3_microcode.md`) is still undecoded, and the
EX5 does not run VOP3. What it gives is *plaintext*: the same Yamaha algorithms in a decoded ISA.

1. AN: EX5 has it as 254 MEG steps with readable dataflow; the PLG150-AN (H8/3002 — Ghidra
   `H8_300`) uploads the same synthesis to a VOP3. Same algorithm, two encodings: a Rosetta
   stone for the VOP3 opcode fields. Match structure first (mem-access count, LFO reads,
   register fan-in per stage), then constants (the EX5 exp/1-x tables must have VOP3 twins).
2. Effects: the FS1R's insertion/reverb list overlaps the EX5's. VOP3-2's images at
   EPROM 0x372204/0x373604 (unextracted) vs the EX5's 21×96-step programs give the same pairing.
3. Filter: the FS1R VOP3-1 per-voice filter vs the AN program's filter section (steps ~0x8C-0xC4
   are two cascaded biquad-looking 5-tap sections with `nodither`).

Next step: extract VOP3-2's effect images with `extract_vop3_2.py`, pick one effect present on
both machines (e.g. Phaser, Flanger, Amp Sim), and align its VOP3 5-word steps to the MEG
listing by counting delay-memory and LFO references.
