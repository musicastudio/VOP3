# YSS236-F (VOP3) pinout

Yamaha AN200 service manual page 12, "I LSI PIN DISCRIPTION / G YSS236-F (XT013A00) VOP3, PLG150-AN: IC4", kept as
`docs/yamaha_an200_service_manual.pdf`. 160 pins. The FS1R's own service manual is scanned images, so its copy of this
table is not readable by machine; the AN200's is typeset. Pointed out by rgwan, 2026-09-15, along with the FS1R's audio
path (`docs/research.md` 2.0.1).

The manual's own wording is kept, "Sync.signal" and "intput" included. Its VDD and VSS rows say "Ground" and "Power
supply" the wrong way round, so those cells are left empty here rather than copy the swap.

| pin | name | I/O | function | pin | name | I/O | function |
|---|---|---|---|---|---|---|---|
| 1 | VSS |  |  | 81 | SO7 | O | Serial output |
| 2 | WA17 | O | External memory address bus | 82 | SO6 | O | Serial output |
| 3 | WA16 | O | External memory address bus | 83 | SO5 | O | Serial output |
| 4 | WA15 | O | External memory address bus | 84 | VDD |  |  |
| 5 | WA14 | O | External memory address bus | 85 | VSS |  |  |
| 6 | WA13 | O | External memory address bus | 86 | SO4 | O | Serial output |
| 7 | WA12 | O | External memory address bus | 87 | SO3 | O | Serial output |
| 8 | WA11 | O | External memory address bus | 88 | SO2 | O | Serial output |
| 9 | WA10 | O | External memory address bus | 89 | SO1 | O | Serial output |
| 10 | VDD |  |  | 90 | SO0 | O | Serial output |
| 11 | VSS |  |  | 91 | WDCK | O | Data enable for DAC |
| 12 | WA09 | O | External memory address bus | 92 | SWPKON | O | SWP00 format key on output |
| 13 | WA08 | O | External memory address bus | 93 | IRQN | O | EG interrupt |
| 14 | WA07 | O | External memory address bus | 94 | VDD |  |  |
| 15 | WA06 | O | External memory address bus | 95 | VSS |  |  |
| 16 | WA05 | O | External memory address bus | 96 | XTAL_I | I | Quartz crystal terminal |
| 17 | WA04 | O | External memory address bus | 97 | XTAL_O | O | Quartz crystal terminal |
| 18 | WA03 | O | External memory address bus | 98 | MCLK | O | Oscillate clock output |
| 19 | WA02 | O | External memory address bus | 99 | VDD |  |  |
| 20 | VDD |  |  | 100 | VSS |  |  |
| 21 | VSS |  |  | 101 | MICN | I | Initial clear |
| 22 | WA01 | O | External memory address bus | 102 | CLKIN | I | Master clock input |
| 23 | WA00 | O | External memory address bus | 103 | SYWIN | I | Sync.signal input |
| 24 | WEN | O | External memory control (WEN) | 104 | SYW | O | Sync.signal output |
| 25 | OEN | O | External memory control (OEN) | 105 | SYWD | O | Sync.signal output |
| 26 | RASN | O | External memory control (RASN) | 106 | VDD |  |  |
| 27 | CASN | O | External memory control (CASN) | 107 | VSS |  |  |
| 28 | CEN | O | External memory control (CEN) | 108 | CLKO | O | For test (512 fs output) |
| 29 | VDD |  |  | 109 | WCLK | O | 2 times sync.clock output (256fs) |
| 30 | VSS |  |  | 110 | HCLK | O | 4 times sync.clock output (128fs) |
| 31 | WD19 | I/O | External memory data bus | 111 | QCLK | O | 8 times sync.clock output (64fs) |
| 32 | WD18 | I/O | External memory data bus | 112 | TSTCI | I | PLL test input |
| 33 | WD17 | I/O | External memory data bus | 113 | VDD |  |  |
| 34 | WD16 | I/O | External memory data bus | 114 | VSS |  |  |
| 35 | WD15 | I/O | External memory data bus | 115 | (NC) |  |  |
| 36 | WD14 | I/O | External memory data bus | 116 | VDD(PLL) |  |  |
| 37 | VDD |  |  | 117 | CPO | O | PLL control output |
| 38 | VSS |  |  | 118 | CPIN | I | PLL control input |
| 39 | WD13 | I/O | External memory data bus | 119 | REF | I | PLL control intput |
| 40 | WD12 | I/O | External memory data bus | 120 | VSS(PLL) |  |  |
| 41 | WD11 | I/O | External memory data bus | 121 | (NC) |  |  |
| 42 | WD10 | I/O | External memory data bus | 122 | VDD |  |  |
| 43 | WD09 | I/O | External memory data bus | 123 | VSS |  |  |
| 44 | WD08 | I/O | External memory data bus | 124 | TSTCS | I | PLL test input |
| 45 | WD07 | I/O | External memory data bus | 125 | CA6 | I | CPU address bus |
| 46 | VDD |  |  | 126 | CA5 | I | CPU address bus |
| 47 | VSS |  |  | 127 | CA4 | I | CPU address bus |
| 48 | WD06 | I/O | External memory data bus | 128 | CA3 | I | CPU address bus |
| 49 | WD05 | I/O | External memory data bus | 129 | CA2 | I | CPU address bus |
| 50 | WD04 | I/O | External memory data bus | 130 | VDD |  |  |
| 51 | WD03 | I/O | External memory data bus | 131 | VSS |  |  |
| 52 | WD02 | I/O | External memory data bus | 132 | CA1 | I | CPU address bus |
| 53 | WD01 | I/O | External memory data bus | 133 | CA0 | I | Lo/Hi select in 8 bits write |
| 54 | WD00 | I/O | External memory data bus | 134 | CSN | I | Chip select |
| 55 | VDD |  |  | 135 | RDN | I | Register read |
| 56 | VSS |  |  | 136 | WRN | I | Register write |
| 57 | TST2 | O | Test output | 137 | BTYP |  | Data bus type select |
| 58 | TST1 | O | Test output | 138 | VDD |  |  |
| 59 | TST0 | O | Test output | 139 | VSS |  |  |
| 60 | MS | I | Memory select | 140 | CD15 | I/O | CPU data bus |
| 61 | LRCLK | O | LR clock for ADC | 141 | CD14 | I/O | CPU data bus |
| 62 | SI7 | I | Serial input | 142 | CD13 | I/O | CPU data bus |
| 63 | SI6 | I | Serial input | 143 | CD12 | I/O | CPU data bus |
| 64 | VDD |  |  | 144 | CD11 | I/O | CPU data bus |
| 65 | VSS |  |  | 145 | VDD |  |  |
| 66 | SI5 | I | Serial input | 146 | VSS |  |  |
| 67 | SI4 | I | Serial input | 147 | CD10 | I/O | CPU data bus |
| 68 | SI3 | I | Serial input | 148 | CD09 | I/O | CPU data bus |
| 69 | SI2 | I | Serial input | 149 | CD08 | I/O | CPU data bus |
| 70 | SI1 | I | Serial input | 150 | CD07 | I/O | CPU data bus |
| 71 | SI0 | I | Serial input | 151 | CD06 | I/O | CPU data bus |
| 72 | DB1 | I | Output bit type select for DAC | 152 | CD05 | I/O | CPU data bus |
| 73 | DB0 | I | Output bit type select for DAC | 153 | VDD |  |  |
| 74 | VDD |  |  | 154 | VSS |  |  |
| 75 | VSS |  |  | 155 | CD04 | I/O | CPU data bus |
| 76 | ODFM | I | Output mode select for DAC | 156 | CD03 | I/O | CPU data bus |
| 77 | OFS3 | I | Serial output format select | 157 | CD02 | I/O | CPU data bus |
| 78 | OFS2 | I | Serial output format select | 158 | CD01 | I/O | CPU data bus |
| 79 | OFS1 | I | Serial output format select | 159 | CD00 | I/O | CPU data bus |
| 80 | OFS0 | I | Serial output format select | 160 | VDD |  |  |

## What it says about the FS1R

- **Serial audio, 8 in and 8 out.** SI0-SI7 and SO0-SO7 are exactly the pins rgwan read off the board: VOP3-2 takes
  FS1B's four busses on SI0-3 and drives the two DACs from SDO1 and SDO3, VOP3-1 takes CHOUT on SI1/SI3/SI5/SI7 and
  returns on SO3/SO7. Nothing in the pinout contradicts the channel-loop reading of VOP3-1.
- **The CPU side is CA0-CA6, CD00-CD15, CSN, RDN, WRN, BTYP**: 7 address lines, so 128 register addresses, with CA0
  doubling as the high/low byte select when BTYP asks for an 8-bit bus. The FS1R uses the 16-bit bus, so CA0-CA6 are the
  CPU's A1-A7 and one chip's 128 registers occupy 0x100 bytes.
- **Each chip has its own window on CS2, decoded on A8 and A9** (rgwan, 2026-09-16). VOP3-1 answers with A8 = 0 and
  A9 = 1, so 0x800200; VOP3-2 with A8 = 0 and A9 = 0, so 0x800000; A8 = 1 clocks the LED and LCD-contrast latch at
  0x800100. Nothing above A9 is decoded. The firmware has a full driver for each, `FUN_0000B5E2` and `FUN_00039648`,
  and the one we found first is the filter chip's. `docs/research.md` section 8 has the logic and the two drivers side
  by side.
- **External memory: WA00-WA17, WD00-WD19, with WEN, OEN, RASN, CASN and CEN**, so up to 256K words of 20-bit DRAM per
  chip. **IC31's are left open in the schematic** (rgwan, 2026-09-16), so VOP3-1 has no delay memory and cannot be
  running a reverb. That confirms the split from the diagram. The FS1R's 512 KB DRAM at 0x01000000 is the CPU's, on a
  different bus.
- **Clocking.** CLKIN is the master clock in, XTAL_I/O plus MCLK let a VOP3 run off its own crystal instead, SYWIN takes
  a sync signal and SYW/SYWD pass it on, and CLKO, WCLK, HCLK and QCLK are 512, 256, 128 and 64 fs outputs. A VOP3 can
  therefore be the clock source for the tone generators, whose pinout wants CLK in and HCLK out. On the FS1R the main
  DAC is the I2S master (rgwan), so the VOP3s take the clock rather than make it, and which chip passes which sync
  signal along is not settled.
- **DAC-side pins** WDCK, ODFM, OFS0-3 and DB0/DB1 set the output format and word length, which is how VOP3-2 drives
  the two LC78834s directly with no glue.
- **Synthesis plumbing that the FS1R does not use.** SWPKON is a key-on output in SWP00 format, IRQN is an EG interrupt,
  and LRCLK is an ADC's word clock. Those are for the AN1x, where this chip is the synthesis engine rather than an
  effect processor, and for a unit with an audio input, which the FS1R has not got.

Nothing here decodes the instruction set. It bounds the machine: 128 CPU registers, 16 serial audio ports, one bank of
external DRAM, and an interrupt back to the CPU.
