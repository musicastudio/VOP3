# The MU2000's MEG effects against the FS1R's VOP3-2 (2026-10-07)

`MU2000/effects-fs1r-mu2000-params.md` showed that the FS1R's effect types have MU2000 namesakes with the same
parameters (820 of 820 shared parameter slots carry the same name; DOC, the two data lists). This document checks the claim behind it in the two firmwares:
the FS1R's VOP3-2 runs the MU-series XG effect library, recompiled for a different DSP. It does. The two firmwares
share the effect design tables byte for byte, number their effect programs and presets the same way, carry the same
preset words, and on seven of the FS1R's 96-step programs the VOP3 steps line up one for one with the MU2000's MEG
steps: the same constants, the same registers up to a renumbering, the same output shift. That last part is a second rosetta stone, next to the EX5's
(`ex5_meg.md`, `vop3_meg_match.md`), and a stronger one: the pairing is by step, not by role.

`python tools/mu2000_fs1r_match.py` asserts every numbered result below (six checks); `--pairs` also writes the
side-by-side listings `docs/mu2000_meg/pair_*.txt`. The MU2000 side comes from `python tools/extract_mu2000_meg.py`
(listings in `ghidra/MU2000_DISASM/`, every type's preset in `docs/mu2000_meg/types.json`).

## Sources

| | what | evidence |
|---|---|---|
| MU2000 v2.01 | `MU2000/mu2000_flash.bin` (SHA1 3923cc54...), the MU2000 EX program flash, SH-2 big-endian | FW |
| MU2000 v1.01 | `MU2000/xw87020.ic25` + `xw86920.ic24`, MAME's mu2000 "v101" pair | FW |
| FS1R v1.20 | `roms/fs1r_v120_eprom_cpuview.bin`; presets in `docs/vop3_2/params.json`; programs in `docs/vop3_disasm/` | FW |
| EX5 | `roms/EX5_TG1.bin` | FW |
| S-MU2000 | `docs/references/s-mu2000/`: the MU2000 EX's MEG run against the real unit, its 65 program hashes | DOC (measured on an MU2000) |
| FS1R Data List | the effect type list (FS1RE2.PDF p.12) as `MU2000/effects-fs1r-mu2000-params.md` transcribes it | DOC |

## The MU2000's programs (FW)

The SH-2 uploads 69 MEG programs from one block of flash (0x16C724-0x1790E4): section 0 (152 steps at pc 0x000: the
reverb all 18 reverb types share, then the master EQ and the output stage), 4 chorus programs (40 steps at pc 0x098),
32 variation programs (96 steps at pc 0x120) and 32 insertion programs (the same 32 on other registers, at pc 0x0C0).
`ghidra/MU2000_DISASM/README.md` lists the uploader, the type lookups and the preset loader with their addresses.

* Every one of S-MU2000's 65 program shapes is in the flash, found by the FNV-1a of its words, and for every
  variation and insertion shape the firmware's own type lookups (run as SH-2 code) select exactly the types S-MU2000
  lists for it. The four programs S-MU2000 does not list (variation and insertion 8 and 12) are selected by no type.
* A type's preset block holds its step constants (94 reverb, 40 chorus, 96 variation or insertion), its delay offsets
  (51, 12, 32, 32), its LFO words (none, 4, 6, 6) and 16 parameter defaults. The LFO word k goes to the MEG's LFO
  register k, and the variation preset loads k = 0x12-0x17, the insertion preset 0x0C-0x11: the LFOs MAME's
  `get_lfo(pc >> 4)` reads inside those windows. The firmware supports MAME's rule independently of MAME.
* v1.01 already has programs 0-18 of each bank and all four chorus programs, byte for byte; programs 19-31 came with
  the EX firmware. v1.01's section 0 differs in 8 output-stage steps only.

## 1. One set of design tables in three firmwares (FW)

Every table `vop3_2_params.md` and `vop3_meg_match.md` read out of the FS1R is byte-identical in the MU2000 flash
except the time-to-row table (2), and most of them in the EX5 TG firmware. The "run" is how far the identical bytes
continue from the table's start.

| FS1R | MU2000 | EX5 TG | run | what |
|---|---|---|---|---|
| 0x36F8B0 | 0x18EF0C | 0x2BA034 | 284 | reverb time, 70 floats in seconds |
| 0x36F9CC | (0x18F028, moved) | 0x2B9FEC | 70 in the EX5 | reverb time to comb decay row: the EX5's exactly, the MU2000's rows moved three places (2) |
| 0x36FA12 | 0x18F06E | 0x2B9BEC | 3077 | comb (h, k) pairs, the RT60 table of `vop3_meg_match.md`; the run continues through 0x36FE12, 0x36FE1D, 0x37006A, 0x3703BE and 0x37054E |
| 0x36F826 | 0x18BDE2 | 0x2BA18E | 77 | reverb selector per type |
| 0x37061C | 0x18BED6 | | 70 | room size table 2 |
| 0x371614 | 0x18A5E8 | 0x2BACF4 | 444 | filter designer K = tan(pi f / 44100) |
| 0x371708 | 0x18A6DC | 0x2BAC90 | 200 | filter designer V = 10^((g - 64)/20) |
| 0x3719D0, 0x371AD2 | 0x18AD18, 0x18AE1A | 0x2BA8A8, 0x2BA7A4 | 516 | u16 tables |
| 0x371BD4, 0x371CD4, 0x371DD4 | 0x18AF9E, 0x18B09E, 0x18B19E | 0x2BA620, 0x2BA520, 0x2BA420 | 781 | delay times |
| 0x370F70, 0x36F67C, 0x36F6A4 | 0x190534, 0x18ED64, 0x18ED8C | (0x2B91CC for 0x36F67C) | 170-424 | u16 tables |

Over the FS1R's effect data (0x35D000-0x372200) about 9.5 KB of its 32 KB of non-zero bytes recur verbatim (in runs
of 32 bytes or more) in the MU2000's effect data. The code that reads them is shared too: the FS1R's preset loader
(0x39B520 on) and the MU2000's (0x053E44 on) are the same compiled C, per program the same 44-byte instruction
sequence with only the literal pool different (preset stride 0x130 on the FS1R, 0x10C on the MU2000).

What this settles:

* **The comb design is Yamaha's, not the FS1R's.** The (h, k) table whose rows `vop3_meg_match.md` shows to be
  `k = 10^(-3L/(T fs))` with `h = sqrt(1 - k^2)/sqrt(24)` is the same 1024 bytes in all three firmwares. Why 24 stays
  open; it is a constant of the shared design, not an FS1R adjustment.
* **The FS1R's 44.1 kHz filter designer is inherited.** `vop3_2_params.md` found the bilinear designer running at
  fs = 44100 on a 48 kHz chip. Its K table is the MU2000's table byte for byte, and the MU2000 runs at 44.1 kHz. The
  FS1R kept the table, so its EQ and filter corners sit 48/44.1 = 8.8% above the design frequencies (inference;
  sessions 17-19 measured EQ peaks at 539 / 1131 / 2150 Hz for words designed at 500 / 1000 / 2000 Hz, CHIP).
* **Where the FS1R did retune for 48 kHz, the change is small and local.** The 0.1 ms to samples routine is the
  MU2000's with one constant changed: MU2000 0x0582A2 computes `(v * 0x468F) >> 12` (4.41 samples per 0.1 ms), FS1R
  `FUN_0039D47C` `(v * 0x4CCC) >> 12` (4.8). The early-reflection taps are rescaled in the presets (3). Everything left
  in samples or steps runs 48/44.1 faster, which is the Data List's 75 time and frequency ranges scaled by exactly
  48/44.1 against the MU2000's (delay maxima shorter, LFO frequencies higher; DOC, `MU2000/effects-fs1r-mu2000-params.md`).

## 2. The reverb (FW)

* The reverb type to preset block map is the same 13 bytes on both (FS1R 0x364F10, MU2000 0x181F48:
  0, 1, 1, 1, 1, 1, 2, 2, 1, 3, 3, 3, 3), and Hall1's preset carries the same nine delay offsets on both
  (0001 04e9 08ba 0bf8 0f43 211e 306f 3d4f 489e).
* Both preset blocks feed 94 step constants (FS1R list 0x35E02C, MU2000 0x16B4F0), but to different steps: the FS1R
  runs its reverb inside the 232-step base program with the mixer, the MU2000 in section 0 with the master EQ, and the
  two step lists do not align. The reverb is the same design laid out again. The MU2000's section 0 is the EX5's
  reverb revised: 61 of its 152 words are the EX5's at the same pc (most of 0x019-0x078), so the comb pairing of
  `vop3_meg_match.md` against the EX5 carries over to the MU2000.
* The one reverb design table that differs is the reverb time to comb row table, and the MU2000 is the odd one out:
  the FS1R's 70 entries (0x36F9CC) are the EX5's byte for byte (0x2B9FEC), while the MU2000's (0x18F028) are the same
  rows moved three places toward shorter decay (MU2000 row[t] = FS1R row[t - 3] for t = 3-69, with rows 00, 01, 12 in
  front). So the comb table and the time-to-row table the FS1R runs at 48 kHz are the ones the EX5 runs at 44.1 kHz.
  `vop3_meg_match.md` carries the correction: its 48 kHz fit rests on the FS1R's comb lengths, not on a table designed
  for 48 kHz.
* The FS1R re-voiced its reverb presets: Canyon's first nine parameter defaults equal the MU2000's, Hall1's differ.

## 3. The 96-step bank: one numbering, one preset set (FW)

The FS1R's insertion block (its kind 2, the `variation_NN` programs at window 0x1A0) is the MU2000's
variation/insertion bank. The FS1R's firmware type (msb, lsb) is the MU2000's (program, preset) for programs 0-7; after
that the FS1R skips the MU2000's 9 (DUAL ROTR), so FS1R 10, 12, 13 are MU2000 11, 13, 14. On each pair (FS1R names as
the FS1R firmware and type list give them, see the side findings):

| FS1R | (prog, preset) | MU2000 | constants equal in place | differing words on FS1R handler steps | offsets |
|---|---|---|---|---|---|
| Wah+DS+Dly, Wah+OD+Dly | (12, 1-2) | WAH+DT/OD+DLY (13, 1-2) | 23 of 23 | | |
| Cmp+DS+Dly, Cmp+OD+Dly, Dist+Delay, Odrv+Delay | (13, 1-4) | CMP+DT/OD+DLY, DT/OD+DELAY (14, 1-4) | 42 of 42, 42, 20, 20 | | |
| Distortion, Overdrive, Amp Sim | (5, 1-3) | same names | 11 of 11 | | |
| Compressor, Comp+Dist, Noise Gate | (5, 4 / 5 / 7) | COMPRESSOR, CMP+DT, NOISE GATE | 28 of 42, 15 of 36, 26 of 34 | all of them | |
| HM Enhncer | (5, 6) | HM ENHNCER | 41 of 49 | 4 of 8 | |
| A-Wah+Dist/Odrv, T-Wah+Dist/Odrv | (4, 3 / 4 / 7 / 6) | A-WAH+DT/OD, TOUCH WAH2, T-WAH+ODRV | 8 of 10, 15 of 17 | all of them | |
| Phaser1, Phaser2 | (2, 1-2) | PHASER 1, 2 | 6 of 8, 5 of 8 | | |
| ER 1, ER 2, Gate Rev, Revrs Gate | (3, 1-4) | same names | 2 of 19 | 7 of 17 | taps x 48/44.1 |
| the chorus, rotary, pitch, lo-fi and delay types | (0, 1, 7, 10) | same names | 1-5 | a minority | |

* **Nine FS1R presets equal the MU2000's word for word on all 96 steps** (FS1R insertion types 18, 19, 26-32), and on
  Compressor, Comp+Dist, Noise Gate and the four wahs every word that differs is on a step the FS1R's own parameter
  handler rewrites, so the raw preset word there never reaches the chip. HM Enhancer's other four differences are a
  swapped pair (0x139/0x13A) and the output pair 0x17E/0x17F, 0x2000 on the FS1R against 0x4000.
* **The early-reflection taps were rescaled for 48 kHz.** The four ER-program presets carry six fixed taps; the
  FS1R's are the MU2000's times 48000/44100, rounded down: 866, 1802, 2429, 3060, 3189, 3318 became 942, 1961, 2643,
  3330, 3471, 3611.
* **The FS1R carries MU2000 presets it never uses.** Its program 6 has a preset map (0x363E30, 0x364F25) though no FS1R
  type selects it: HALL 1, HALL 2, ROOM 1-3, STAGE 1-2, PLATE, HALL L, ROOM M and ROOM L equal the MU2000's
  variation-reverb presets word for word. The GM Level 2 types (HALL M, ROOM S, GM PLATE) map to preset 0 there, so the
  FS1R's copy predates them.
* **The chorus, rotary, pitch and lo-fi programs (0, 1, 7, 10) were laid out again.** Same presets by name and
  numbering, with the constants at other steps (for the four delay types the sets of non-zero words still agree).
* **The FS1R's 72-step bank (its variation block, kind 4, `ins2_NN`) has its own program numbering**, with MU-series
  presets on it: its Hall, Room, Stage, Plate, Karaoke and Noise Gate defaults differ from the MU2000's HALL 1,
  ROOM 1, STAGE 1, PLATE, KARAOKE 1 and NOISE GATE in at most 2 of their 16 words.

The FS1R's parameter defaults are mostly its own (it re-voiced its presets); the closest are DELAY LCR, DELAY L,R and
OVERDRIVE, 13 of 16 words each.

## 4. Step for step: the second rosetta (FW pairing, read against CHIP rules)

On these pairs the VOP3 step 0x1A0 + i and the MEG step 0x120 + i + shift name the same registers, up to a fixed
renumbering (VOP3 r[n] = MEG r(n - delta); corrected 2026-10-07, this read n + delta, and the pair files'
headers always gave the MEG register outright, e.g. `r(n+1)` for delta -1):

| FS1R | MU2000 | shift | delta | steps whose registers agree | evidence besides |
|---|---|---|---|---|---|
| 5 (distortion, dynamics) | 5 | 0 | 0 | 68 of 81 | presets (3) |
| 4 (wah) | 4 | 0 | 0 | 44 of 58 | presets |
| 12 (wah+dist+delay) | 13 | 0 | -1 | 47 of 63 | presets |
| 13 (dist+delay) | 14 | 0 | 0 | 39 of 76 | presets, word for word |
| 2 (phaser) | 2 | 0 | -1 | 38 of 57 | presets |
| 3 (early reflections) | 3 | 0 | -1 | 13 of 18 | presets, the taps |
| 6 (variation reverb) | 6 | 0 | -1 | 11 of 15 | the unused presets |
| 9 (no FS1R type, no preset map) | 10 (TALK MOD) | -1 | 0 | 42 of 75 | registers only |

No unpaired program reaches more than 0.50 of its steps at any shift and renumbering (0.39 over 50 steps or more).
The FS1R's VOP3-2 therefore also holds the MU2000's TALK MOD program, which no FS1R type can select (inference from
registers alone). FS1R programs 8, 11, 14 and 15 match no MU2000 program.

`docs/mu2000_meg/pair_fsNN_muMM.txt` lists each pair side by side: MEG step, the two preset constants on it (`k` the
FS1R's, `c` the MU2000's), VOP3 step. Read against the measured ISA (`vop3_isa.md`), the pairs give the VOP3 fields
a MEG meaning, and wherever a chip session measured the field, the pairing agrees with it (check 6, over the eight
pairs):

* **`route` is the MEG's output shift.** Route 0, 1, 2, 3 sits where the MEG shifts its result by 0, 1, 2, 4 bits on
  581 of 614 class-2/3 steps. Session 24 measured route as a x1 / x2 / x4 / x16 scale (CHIP); the MEG's `<< 1`,
  `<< 2`, `<< 4` are the same scale.
* **op 3 starts a sum, op 0 adds to it, op 0s subtracts it.** Op 3 `y = k*x` faces the MEG's fresh `p = c * x` 159
  times, op 0 `y = s + k*x` faces `p = c * x + p` 223 times, op 0s `y = -s + k*x` faces `p = c * x - p` 38 times
  (against 13, 11 and 1 the other way).
* **rA is the destination.** Where the MEG writes `rD = p`, the VOP3 step has rA = D (renumbered): `y = k*(r[02] +
  r[01])` on op 3 route 3 faces `r02 = (c * r01) << 4`, `y = s + k*(r[49] + r[47])` faces `r49 = (c * r47 + p) << 1`.
  Session 24 measured `rA != 0 writes r[rA] = y` (CHIP); the listing's `(r[A] + r[B])` is the older reading of the
  same two fields.
* **The z^-1 write is the MEG's register copy.** 18 of the 25 VOP3 steps with the z^-1 bit (`+` after `path`, r9b7)
  sit one step after a MEG copy `rD = rS`, the copy that shifts a biquad's state (MEG 0x122 `r48 = r47`, VOP3 0x1A3
  storing r[48] from step 0x1A2's x, which is r[47]). Session 27 measured the bit as "r[rA] takes the previous step's
  x operand" (CHIP). So the listing's `(r[n] + r[n])` on these steps is a z^-1 shift, not a doubling. The comb step
  0a5 of `vop3_meg_match.md` has rA = rB without the bit, and is the integrator sessions 17-19 measured.
* **Constants are the MEG's 16-bit words.** The same word sits on the same step on both chips, so on these steps
  the VOP3 coefficient has the MEG's 1.15 scale (inference).
* **Writes land three steps later on both chips** (MEG: S-MU2000 DOC; VOP3: sessions 24-27, CHIP), which is why the
  schedules can match one for one.
* **The insertion inputs:** the FS1R program reads r[07] / r[08] (session 22, CHIP) where the MEG program reads m2c /
  m2d, the variation send (S-MU2000 DOC).
* **Delay memory does not align.** The MEG accesses memory on every third step (offset index pc / 3), the VOP3 on
  steps 3 mod 4 (slot step >> 2), so the 96-step window has 32 MEG slots against 24 VOP3 ones, and the FS1R's
  kind-2 slot list adds 16 slots outside the window (2E-31, 44-46, 51-53, 56-58, 63-65). The arithmetic lines up;
  the memory traffic is rescheduled.

## Side findings

* **Seven FS1R insertion labels in `docs/vop3_2/params.json` were wrong** (and with them the parameter names
  `vop3_2_params.md` printed for those types). By the FS1R type list and by the MU2000 preset both, type 24 (5, 4)
  is Compressor, 25 (5, 5) Comp+Dist, 26 (13, 1) Cmp+DS+Dly, 33 (0, 14) Delay LCR, 34 (0, 15) Delay L,R, 36 (0, 17)
  CrossDelay and 37 (3, 1) ER 1; the file had them rotated or swapped. The firmware data under each number was right;
  the names came from the Data List's parameter tables, which `tools/extract_vop3_2_params.py` took in page order.
  The default parameters confirm it (type 33's are DELAY LCR's 333.3 / 166.7 / 500 / 500 ms). **Fixed 2026-10-07**
  from the FS1R's own name table (EPROM 0x37D839, read by the panel per type number through 0x3AFA64; FW): both files
  corrected, the extractor pairs the tables by name, check 5 asserts all 87 names.
* **FS1R T-Wah+Dist is preset (4, 7)**, which MU2000 v2.01 names TOUCH WAH2. The FS1R defaults agree with that
  preset on 11 of 16 words, words 11-13 (72, 74, 53) included, where the MU2000's own T-WAH+DIST (4, 5) has them 0;
  so (4, 7) is the preset with the distortion settings, and this is not an FS1R slip (inference).

## What to measure next

1. Run one aligned pair through both interpreters: `tools/meg_interp.py` has S-MU2000's rules (2026-10-07), so feed it
   and `tools/vop3_interp.py` the same input on FS1R program 5 / MU2000 program 5 with Distortion's constants, and
   compare step by step. A step that disagrees is a VOP3 rule the sessions have not measured yet. The preset alone
   leaves most of Distortion's gain words at 0 (its parameter handler writes them), so take the constants from the
   FS1R's handler output, not from the raw preset.
2. The ops the pairs leave open: op 4 faces `c * x + p` 61 times and op 7 faces `c * x` (the input read) 33 times;
   ops 2 and 5 appear a handful of times each.
3. The reverb: with the comb table and the presets shared, the remaining question is the step layout of the FS1R's
   base program against the MU2000's section 0; the 94-step constant lists give the parameter-to-step map on both.
