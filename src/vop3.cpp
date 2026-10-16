// license:BSD-3-Clause
// copyright-holders:Wouter van Nifterick

// The chip is compiled here, once, with the library's floating-point flags: an inline copy compiled
// elsewhere under other flags could stand in for this one at link time.
#include "vop3.h"
#include <algorithm>
#include <cmath>

using namespace vop3;

void Vop3::write(uint32_t reg, uint16_t v)
{
    reg &= 0xff;
    if (reg == SELECT) { select = v; return; }
    const int step = select & (yss236_core::STEPS - 1);
    if (select & AUTO_INCREMENT) select = (uint16_t)((select & ~(yss236_core::STEPS - 1)) | ((step + 1) & (yss236_core::STEPS - 1)));
    switch (reg) {
        case PROGRAM - 4: case PROGRAM - 3: case PROGRAM - 2: case PROGRAM - 1:
        case PROGRAM:       core.prog[step][PROGRAM - reg] = v; core.decode(step); break;
        case COEF:          core.coef[step] = v; break;
        case TAG:           core.tag[step] = (uint8_t)v; break;
        case SLOT_HIGH:
        case SLOT_LOW:      core.slot[step & (yss236_core::SLOTS - 1)][reg - SLOT_HIGH] = v; break;
        case TRIGGER:       core.trigger(); break;
        case LFO_RESET:     // to the start phase, LFO_WAVE's low 5 bits
            for (int n = 0; n < yss236_core::LFOS; n++) if (v >> n & 1) phase[n] = (uint32_t)(lfoReg[n][1] & 0x1f) << 27;
            break;
        case EG_STATUS:     enable = v; break;
        case LFO_SPEED: case LFO_WAVE: case LFO_DEPTH:
        case LFO_DIRECTION: lfoReg[step & (yss236_core::LFOS - 1)][reg - LFO_SPEED] = v; break;
        case EG_RATE: case EG_TARGET:
        case EG_AIM:        if (step < 16) egWrite(step, reg, v); break;
        default:            break;      // serial port setup and the mute: not modelled
    }
}

// EG_STATUS read: bit 4 and the lowest channel whose segment ended, of those enabled.
uint16_t Vop3::read(uint32_t reg) const
{
    const uint16_t p = done & enable;
    if ((reg & 0xff) != EG_STATUS || !p) return 0;
    int n = 0;
    while (!(p >> n & 1)) n++;
    return (uint16_t)(0x10 | n);
}

// Rate 0 jumps to the target and 255 holds. Any other rate approaches the aim, covering two thirds of the way
// in Vop3::FEG_SEG_S at rate 0 and in twice that every Vop3::FEG_RATE_OCT. A segment ends when the level
// crosses its target.
// OPEN: when a segment arms. Here the target write arms it and the level moves whenever the rate lets it,
// so a segment whose rate is written last starts as well.
// OPEN: a segment armed within two units of its target ends after Vop3::FEG_FLAT_S whatever its rate.
// That is what the unit was measured to do. How it does it is not known.
void Vop3::egWrite(int n, uint32_t reg, uint16_t v)
{
    Eg& g = eg[n];
    if (reg == EG_AIM) { g.aim = (int16_t)v; return; }
    if (reg == EG_RATE) {
        g.rate = (uint8_t)v;
        g.k = 1.0 - std::exp(-std::log(3.0) / (Vop3::FEG_SEG_S * Vop3::SR * std::exp2(g.rate / Vop3::FEG_RATE_OCT)));
        done &= ~(1u << n);             // a rate write acknowledges the segment
    } else g.target = (int16_t)v;
    if (!g.rate) g.level = g.target;
    if (reg == EG_TARGET) {
        const double d = g.level - g.target;
        g.side = d > 0 ? 1 : d < 0 ? -1 : 0;
        g.flat = g.side && std::fabs(d) <= 2 ? (int)(Vop3::FEG_FLAT_S * Vop3::SR) : 0;
    }
}

// An LFO's value, -1..1 times its depth: its LFO_SPEED, LFO_WAVE and start phase, LFO_DEPTH, LFO_DIRECTION.
float Vop3::lfo(int n) const
{
    const uint16_t* reg = lfoReg[n];
    const float x = (float)phase[n] * (1.0f / 4294967296.0f);
    float v;
    // One rule for both chips (they are the same part). LFO_WAVE bit 12 picks the range: clear, the saws and the
    // triangle are unipolar 0..1 and LFO_DIRECTION 0x8000 centres them (s41c/g, VOP3-2: an LFO played on the DAC
    // reads 0..+128, 2WayRotary's with direction 0xC000 -64..+64); set, they are bipolar -1..1 and 0x4000 halves
    // (VOP3-1, whose host boots every LFO_WAVE to 0x1000: 20_fltmod's LFO2 envelopes, 6.1 dB, against 12.7 dB
    // read unipolar). Square, sine and S&H, which no measured program uses, stay -1..1.
    // OPEN: 0x4000 without bit 12, 0x8000 with it; the quarter-cycle between the two triangles.
    const bool bipolar = reg[1] & 0x1000;
    switch ((reg[1] >> 5) & 7) {
        case SAW_DOWN:    v = bipolar ? 1.0f - 2.0f * x : 1.0f - x; break;
        case SAW_UP:      v = bipolar ? 2.0f * x - 1.0f : x; break;
        case SQUARE:      v = x < 0.5f ? 1.0f : -1.0f; break;
        case SINE:        v = std::sin(2.0f * 3.14159265f * x); break;
        case SAMPLE_HOLD: v = hold[n]; break;
        default:          v = bipolar ? (x < 0.25f ? 4.0f * x : x < 0.75f ? 2.0f - 4.0f * x : 4.0f * x - 4.0f)
                                      : (x < 0.5f ? 2.0f * x : 2.0f - 2.0f * x); break;   // the triangle
    }
    if (bipolar) { if (reg[3] & 0x4000) v *= 0.5f; }
    else if (reg[3] & 0x8000) v -= 0.5f;
    return v * ((float)reg[2] * (1.0f / 32768.0f));
}

// One sample: step the EGs and the LFOs, then run the program over core.in into core.out.
// The speed word is 10 bits, cycles per sample * 2^20 (s41: the effects modulate at the Data List's LFO Freq).
// A full LFO fills a register at 128.
// OPEN: the EGs step once a sample here; the chip's own step rate is not known.
void Vop3::run()
{
    for (int n = 0; n < 16; n++) {
        Eg& g = eg[n];
        if (g.rate && g.rate != 255) {
            if (g.flat) { if (!--g.flat) g.level = g.target; }
            else g.level = std::clamp(g.level + (g.aim - g.level) * g.k, -512.0, 511.0);
            if (g.side && !g.flat && (g.level - g.target) * g.side <= 0) { g.level = g.target; g.side = 0; done |= 1u << n; }
        }
        core.in[3][n] = g.level * (yss236_core::RANGE / 512);
    }
    for (int n = 0; n < yss236_core::LFOS; n++) {
        const uint32_t was = phase[n];
        phase[n] = was + ((uint32_t)(lfoReg[n][0] & 0x3ff) << 12);
        if (((lfoReg[n][1] >> 5) & 7) == SAMPLE_HOLD && phase[n] < was) {
            rnd = rnd * 1664525u + 1013904223u;
            hold[n] = (float)(int16_t)(rnd >> 16) * (1.0f / 32768.0f);
        }
        core.lfo[n] = lfo(n) * yss236_core::RANGE;
    }
    core.pass();
}
