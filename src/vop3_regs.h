// license:BSD-3-Clause
// copyright-holders:Wouter van Nifterick

// The YSS236 ("VOP3")'s register map: what the firmware writes to, and the LFO waves.
#pragma once
#include <cstdint>

namespace vop3 {

// The chip's registers, by the number the firmware writes to. 0x01, 0x05, 0x1C and 0x28..0x2A
// are written at boot or after a phase reset and are not named anywhere.
enum Reg : uint32_t {
    SELECT          = 0x00,     // the step, slot, LFO or EG the next writes go to
    PROGRAM         = 0x0A,     // a step's five program words: word k at PROGRAM - k
    COEF            = 0x0B,     // a step's constant
    TAG             = 0x0C,     // a step's tag
    SLOT_HIGH       = 0x0D,     // a delay slot's offset, bits 17-9
    SLOT_LOW        = 0x0E,     //                        bits 8-0
    TRIGGER         = 0x14,     // the constant loads of every gated block land at once
    CONFIG          = 0x16,     // a fifteen-entry table: entry i is written after SELECT i + 1
    UPLOADING       = 0x17,     // 0x23 while a program is rewritten, 0 after
    LFO_RESET       = 0x20,     // the LFOs of a mask back to their start phase
    EG_STATUS       = 0x21,     // written, the EGs that may report; read, the lowest that did
    LFO_SPEED       = 0x24,
    LFO_WAVE        = 0x25,     // and the start phase in the low five bits
    LFO_DEPTH       = 0x26,
    LFO_DIRECTION   = 0x27,
    EG_RATE         = 0x2B,
    EG_TARGET       = 0x2C,     // the level at which a segment ends
    EG_AIM          = 0x2D,     // the level it heads for
};
constexpr uint32_t AUTO_INCREMENT = 0x8000;    // on a SELECT: each write moves on to the next step

// An LFO's wave, in bits 5-7 of LFO_WAVE
enum Wave { TRIANGLE, SAW_DOWN, SAW_UP, SQUARE, SINE, SAMPLE_HOLD };

}  // namespace vop3
