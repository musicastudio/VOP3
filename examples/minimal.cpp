// The smallest host: one program step that copies serial input slot 0 to output slot 0, then a few samples.
#include <cstdio>
#include "vop3.h"

int main() {
    Vop3 chip;
    // Step 0: m[1] <- serial input (page 0, half 0). Step 1: m[1] goes out on serial slot 0 (w4[6], step / 64 = 0).
    const uint16_t prog[2][5] = {
        {0, 0, 1 << 7, 0, 0},     // w2: dm = 1, msrc = serial
        {0, 0, 0, 0, 1 | 1 << 6}, // w4: sm = 1, sout
    };
    for (int st = 0; st < 2; st++) {
        chip.write(vop3::SELECT, (uint16_t)st);
        for (int w = 0; w < 5; w++) chip.write(vop3::PROGRAM - w, prog[st][w]);
    }
    for (int i = 0; i < 4; i++) {
        chip.core.in[0][0] = 0.25 * Vop3::WORD;   // a quarter of full scale in
        chip.run();                                // one sample: EGs, LFOs, then the 512-step pass
        std::printf("sample %d: out %.4f (%s)\n", i, chip.core.out[0] / Vop3::WORD, chip.core.m_jit ? "compiled" : "interpreted");
    }
}
