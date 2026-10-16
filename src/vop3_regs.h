// license:BSD-3-Clause
// copyright-holders:Wouter van Nifterick

// The YSS236 ("VOP3") host register map and the LFO waves. docs/vop3_isa.md section 2 describes each register.
#pragma once
#include <cstdint>

namespace vop3 {

// The chip's registers. A host writes register reg at base + 2 * reg. The unnamed ones the firmware writes
// (0x01, 0x05, 0x0F..0x12, 0x1B, 0x1C, 0x28..0x2A) are configuration and host RAM access, which Vop3 ignores.
enum Reg : uint32_t {
    SELECT          = 0x00,     // the step, slot, LFO, EG or CONFIG entry the next indexed writes go to
    PROGRAM         = 0x0A,     // a step's five program words: word k at PROGRAM - k
    COEF            = 0x0B,     // a step's constant
    TAG             = 0x0C,     // a step's tag
    SLOT_HIGH       = 0x0D,     // a delay slot's offset, bits 17-9
    SLOT_LOW        = 0x0E,     //                        bits 8-0
    TRIGGER         = 0x14,     // the constant loads of every gated block land at once
    CONFIG          = 0x16,     // a 15-entry table: entry i is written after SELECT i + 1
    UPLOADING       = 0x17,     // 0x23 while a program is rewritten, 0 after
    LFO_RESET       = 0x20,     // each set bit n returns LFO n to its start phase
    EG_STATUS       = 0x21,     // write: the EGs that may report; read: 0x10 | the lowest that did, 0 if none
    LFO_SPEED       = 0x24,     // 10 bits, cycles per sample x 2^20
    LFO_WAVE        = 0x25,     // bits 7..5 the wave, 4..0 the start phase in 32nds, bit 12 bipolar
    LFO_DEPTH       = 0x26,     // unsigned 1.15
    LFO_DIRECTION   = 0x27,     // unipolar: 0x8000 centres the wave; bipolar: 0x4000 halves it
    EG_RATE         = 0x2B,     // 0 jumps, 255 holds; a write acknowledges the segment
    EG_TARGET       = 0x2C,     // the level at which a segment ends; a write arms it
    EG_AIM          = 0x2D,     // the level it heads for
};
constexpr uint32_t AUTO_INCREMENT = 0x8000;    // on a SELECT: each write moves on to the next step

// An LFO's wave, in bits 5-7 of LFO_WAVE
enum Wave { TRIANGLE, SAW_DOWN, SAW_UP, SQUARE, SINE, SAMPLE_HOLD };

}  // namespace vop3
