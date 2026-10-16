# Register capture sessions

Index of what each hardware register session settled. The sessions themselves ran against a patched firmware that answers memory reads and writes over sysex, so the unit's own RAM, both YMP706s and both VOP3s can be read from a laptop.

**The raw data is not in this repo.** It lives in the private companion repo `musicastudio/FS1R.unlock`, alongside the patch and the capture scripts that produced it. This file carries the conclusions, which are public, and names where the working is for anyone who has access. Conclusions that changed a constant are in [findings.md](../findings.md), and the current state of what is known is in [STATUS.md](../../STATUS.md).

The audio capture kit is a separate thing and lives in this repo, at [captures/](../../captures/README.md).

| Session | Date | What it settled |
|---|---|---|
| Session 1 | 2026-09-18 | Four runs: the gate, the voice image for every segment of the audio capture set, the filter EG sweep, and the driver's RAM idle and under load. The voice image was read back for all 492 segments with the exact voice and performance bulk that made each one |
| Session 2 | 2026-09-19 | Nine stages, plus one unbroken recording across three sweeps. The CPU side checked at 92,928 points. Two constants the session went after came back measured |
| Band and skirt sweep | 2026-09-19 | The three parameters that shape a voiced operator. First run aimed at the wrong register and was repeated once the firmware was read |
| Filter and level sweeps | 2026-09-20, 2026-09-21 | Filter coefficients and the Fseq level path |
| Session 3 | 2026-09-23 | The filter EG's rate law off the CPU's stage word, LFO2 confirmed as VOP3-1's, and the six tonal request files recorded (`16_velocity` to `20_fltmod`) |
| Session 4 | not yet run | `fs1r_capture_session4.py`: the key code table at every semitone, the unvoiced resonance carrier's level law, the per-channel arrays under loud notes. Nothing in the engine waits on it |

## What the sessions changed in the engine

**The frequency EG depth was over twice too strong at mid settings.** The sysex byte reaches register 0x60/0x68 through a table at EPROM 0x35B2E0, now extracted as `FEGLVL`, and it is far from linear.

**The pan table index was one step left of the hardware's.** The voice image carries twice the part's pan byte at +0x2E and `FUN_00025BC8` indexes at `pan >> 1`, so the index is the part byte itself.

**The CPU side of the note-on path is checked byte for byte**, 240,476 checks over 802 segments with no mismatch. What that leaves open is the chip side, since the voice image is identical under every note and every velocity, so the level offsets, the frequency words and the bandwidth registers are not in it. Reading those needs the per-channel arrays at 0x0103B384 and 0x01044C**, which is a session that has not been run.

**Two capture scripts were writing to the wrong hardware**, and reading the firmware settled both. `sweep.py filtcoef` passed a coefficient slot to `FUN_0000B5E2` as if it were a VOP3 register. The band sweep reported that only `frmt` responded until it was pointed at register 0x230, after which `res1` and `res2` responded too.
