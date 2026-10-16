// vop3_jit_check - the compiled VOP3 program against the interpreter, byte for byte.
//
//   vop3_jit_check [docs dir] [samples per program]     defaults: the repo's docs/, 9600
//
// Two chips, one held on the interpreter, run the FS1R's shipped programs from docs/ (the VOP3-1 filter
// program with its constants, and the VOP3-2 effect base with every reverb, variation and insertion image
// at its window) on the same noise, each image uploaded over the last through the register file so the
// program changes under running state. Then random programs, coefficients and slot offsets. After every
// pass the two cores must hold the same state, and the first field that does not is printed with the pass it
// went wrong in. Exit code 0 when everything matched, 77 (ctest's skip) when this architecture has no
// compiler, so the interpreter runs alone. A compiler that is there and fails is a failure.
#define _CRT_SECURE_NO_WARNINGS
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include "vop3.h"

// The first field two cores differ in, or "" when they match. The delay RAM is only compared when asked:
// it is 2 MB.
static std::string diff(const yss236_core& a, const yss236_core& b, bool ram) {
    auto at = [](const char* name, const void* x, const void* y, size_t bytes, size_t elem) -> std::string {
        for (size_t i = 0; i < bytes / elem; i++)
            if (std::memcmp((const char*)x + i * elem, (const char*)y + i * elem, elem)) return std::string(name) + "[" + std::to_string(i) + "]";
        return "";
    };
    std::string w;
#define F(name, field) if (!(w = at(name, &a.field, &b.field, sizeof a.field, sizeof a.field[0])).empty()) return w;
#define S(field) if (std::memcmp(&a.field, &b.field, sizeof a.field)) return #field;
    F("r", r) F("m", m) F("t", t) F("out", out)
    S(acc) S(x) S(px) S(hasx) S(haspx) S(src) S(psrc) S(wlatch) S(haswl) S(idx) S(ptr) S(n) S(m_skip) S(m_flag) S(m_nhist) S(m_iqn)
    for (int i = 0; i < 4; i++) {
        const char* q[3] = {"m_rq", "m_dq", "m_tq"};
        const yss236_core::pend* pa[3] = {a.m_rq, a.m_dq, a.m_tq}; const yss236_core::pend* pb[3] = {b.m_rq, b.m_dq, b.m_tq};
        for (int k = 0; k < 3; k++)
            if (pa[k][i].reg != pb[k][i].reg || std::memcmp(&pa[k][i].v, &pb[k][i].v, 8)) return std::string(q[k]) + "[" + std::to_string(i) + "]";
    }
    for (int i = 0; i < a.m_iqn; i++)
        if (a.m_iq_at[i] != b.m_iq_at[i] || std::memcmp(&a.m_iq_v[i], &b.m_iq_v[i], 8)) return "m_iq[" + std::to_string(i) + "]";
    if (ram && !(w = at("ram", a.ram.data(), b.ram.data(), a.ram.size() * 8, 8)).empty()) return w;
    return "";
#undef F
#undef S
}

static uint32_t seed = 12345;
static double noise() { seed = seed * 1664525u + 1013904223u; return (int32_t)seed * (0.2 / 2147483648.0); }

static int fails = 0;
static long long passes = 0;
// one pass of both: false, and a report, at the first difference in the state or the output, or when b did
// not run compiled code (the comparison would be the interpreter against itself)
static bool same(const char* what, int type, int i, const yss236_core& a, const yss236_core& b, bool output, bool ram = false) {
    passes++;
    std::string w = !b.m_jit ? "nothing, b is not compiled," : diff(a, b, ram || i % 256 == 255);
    if (w.empty() && output) return true;
    std::printf("  %s %d: %s differs after pass %d\n", what, type, w.empty() ? "the output" : w.c_str(), i);
    fails++;
    return false;
}

// What the programs compared so far exercise
static int seen[8];
static void tally(const yss236_core& k) {
    for (const auto& o : k.ops) {
        seen[0] += o.gate; seen[1] += o.cskip == 6; seen[2] += o.cskip == 4; seen[3] += o.cskip == 1;
        seen[4] += !o.ctlop && o.dm && (o.msrc == 4 || o.msrc == 5); seen[5] += !o.ctlop && o.idxa; seen[6] += !o.ctlop && o.idxw;
        seen[7] += !o.ctlop && o.mmode >= 2 && o.dr && o.rcopy;
    }
}
static void coverage(const char* what) {
    std::printf("%-10s gates %d, skips if positive %d, if negative %d, always %d, captures %d, index reads %d, index loads %d, z^-1 copies %d\n",
        what, seen[0], seen[1], seen[2], seen[3], seen[4], seen[5], seen[6], seen[7]);
    std::memset(seen, 0, sizeof seen);
}

static uint64_t rng = 88172645463325252ull;
static uint32_t rnd() { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return uint32_t(rng); }

static std::vector<uint8_t> slurp(const std::string& path) {
    std::vector<uint8_t> b;
    if (FILE* f = std::fopen(path.c_str(), "rb")) {
        int c;
        while ((c = std::fgetc(f)) != EOF) b.push_back((uint8_t)c);
        std::fclose(f);
    }
    return b;
}

// A program image (big-endian, five words a step) uploaded at step first through the chip's registers, as a host does
static bool upload(Vop3& v, const std::string& path, int first, const std::vector<uint8_t>& coef = {}) {
    const std::vector<uint8_t> b = slurp(path);
    if (b.empty()) { std::printf("  missing %s\n", path.c_str()); fails++; return false; }
    for (size_t i = 0; i + 10 <= b.size(); i++) {
        const int st = (first + (int)i) & (yss236_core::STEPS - 1);
        v.write(vop3::SELECT, (uint16_t)st);
        for (int w = 0; w < 5; w++) v.write(vop3::PROGRAM - w, (uint16_t)(b[10 * i + 2 * w] << 8 | b[10 * i + 2 * w + 1]));
        v.write(vop3::SELECT, (uint16_t)st);
        v.write(vop3::COEF, coef.size() >= 2 * (size_t)st + 2 ? (uint16_t)(coef[2 * st] << 8 | coef[2 * st + 1]) : (uint16_t)rnd());
    }
    return true;
}

// Runs both chips over the same noise for n samples; false at the first difference
static bool run(const char* what, int id, Vop3& a, Vop3& b, int n) {
    tally(b.core);
    for (int i = 0; i < n; i++) {
        for (int s = 0; s < yss236_core::SLOTS_IO; s++) a.core.in[0][s] = b.core.in[0][s] = (noise() + (i % 4800 == 0 ? 0.9 : 0.0)) * Vop3::WORD;
        a.run(); b.run();
        if (!same(what, id, i, a.core, b.core, !std::memcmp(a.core.out, b.core.out, sizeof a.core.out))) return false;
    }
    return true;
}

int main(int argc, char** argv) {
    const std::string docs = std::string(argc > 1 ? argv[1] : VOP3_DOCS) + "/";
    const int N = argc > 2 ? std::atoi(argv[2]) : 9600;
    if (!yss236_core::jit_target) {
        std::printf("no VOP3 compiler for this architecture: the interpreter runs alone, nothing to compare\n");
        return 77;
    }
    int bad;

    // VOP3-1, the FS1R filter: both program variants with their constants
    {
        auto a = std::make_unique<Vop3>(), b = std::make_unique<Vop3>();
        a->core.interpret = true;
        bad = 0;
        for (int v = 0; v < 2; v++) {
            const std::vector<uint8_t> coef = slurp(docs + "vop3/coefficients_" + std::to_string(v) + ".bin");
            for (Vop3* c : {a.get(), b.get()}) upload(*c, docs + "vop3/program_" + std::to_string(v) + ".bin", 0, coef);
            if (!run("filter", v, *a, *b, N)) bad++;
        }
        std::printf("%-10s  2 programs: %s\n", "filter", bad ? "DIFFERS" : "identical");
        coverage("filter");
    }

    // VOP3-2, the FS1R effects: the base program, then every block image at its window over it
    {
        auto a = std::make_unique<Vop3>(), b = std::make_unique<Vop3>();
        a->core.interpret = true;
        const uint64_t keep = rng;
        for (Vop3* c : {a.get(), b.get()}) { rng = keep; upload(*c, docs + "vop3_2/base.bin", 0); }
        for (int s = 0; s < yss236_core::SLOTS; s++) {
            const uint16_t hi = (uint16_t)(rnd() & 0x1ff), lo = (uint16_t)(rnd() & 0x1ff);
            for (Vop3* c : {a.get(), b.get()}) { c->write(vop3::SELECT, (uint16_t)s); c->write(vop3::SLOT_HIGH, hi); c->write(vop3::SELECT, (uint16_t)s); c->write(vop3::SLOT_LOW, lo); }
        }
        const struct { const char* kind; int n, first; } blocks[] = {{"reverb", 4, 0xe8}, {"variation", 16, 0x1a0}, {"ins1", 12, 0x110}, {"ins2", 12, 0x158}};
        for (auto& bl : blocks) {
            bad = 0;
            for (int i = 0; i < bl.n; i++) {
                char name[64];
                std::snprintf(name, sizeof name, "vop3_2/%s_%02d.bin", bl.kind, i);
                const uint64_t k = rng;
                for (Vop3* c : {a.get(), b.get()}) { rng = k; upload(*c, docs + name, bl.first); }
                if (!run(bl.kind, i, *a, *b, N / 4)) bad++;
            }
            std::printf("%-10s %2d images: %s\n", bl.kind, bl.n, bad ? "DIFFERS" : "identical");
        }
        coverage("effects");
    }

    // Random programs, coefficients and slot offsets: field combinations the firmware's programs may never
    // use, results that wrap, skips either way, gates and triggers, and words rewritten between passes.
    // A control step is kept to one in eight so that the skips leave something to run.
    {
        auto ka = std::make_unique<yss236_core>(), kb = std::make_unique<yss236_core>();
        ka->interpret = true;
        const int PROGRAMS = N / 24, PASSES = 64;
        bad = 0;
        for (int p = 0; p < PROGRAMS && !bad; p++) {
            for (yss236_core* k : {ka.get(), kb.get()}) k->reset();
            for (int st = 0; st < yss236_core::STEPS; st++) {
                uint16_t w[5];
                for (uint16_t& x : w) x = (uint16_t)rnd();
                if (rnd() % 8) w[1] &= 0x7fff;
                const uint16_t k = (uint16_t)rnd();
                for (yss236_core* c : {ka.get(), kb.get()}) { std::memcpy(c->prog[st], w, sizeof w); c->coef[st] = k; }
            }
            for (int s = 0; s < yss236_core::SLOTS; s++) {
                const uint16_t hi = (uint16_t)rnd(), lo = (uint16_t)rnd();
                for (yss236_core* c : {ka.get(), kb.get()}) { c->slot[s][0] = hi; c->slot[s][1] = lo; }
            }
            for (yss236_core* c : {ka.get(), kb.get()}) c->decode_all();
            tally(*kb);
            for (int i = 0; i < PASSES; i++) {
                for (int s = 0; s < 4 * yss236_core::SLOTS_IO; s++) ka->in[s / 16][s % 16] = kb->in[s / 16][s % 16] = (int32_t)rnd() * (16.0 / 2147483648.0);
                for (int s = 0; s < yss236_core::LFOS; s++) ka->lfo[s] = kb->lfo[s] = (int32_t)rnd() * (128.0 / 2147483648.0);
                if (rnd() % 8 == 0) { ka->trigger(); kb->trigger(); }
                if (rnd() % 4 == 0) {
                    const int st = rnd() % yss236_core::STEPS, w = rnd() % 5; const uint16_t v = (uint16_t)rnd();
                    for (yss236_core* c : {ka.get(), kb.get()}) { c->prog[st][w] = v; c->decode(st); }
                }
                ka->pass(); kb->pass();
                if (!same("random", p, i, *ka, *kb, true, i == PASSES - 1)) { bad++; break; }
            }
        }
        std::printf("%-10s %2d programs of %d passes: %s\n", "random", PROGRAMS, PASSES, bad ? "DIFFERS" : "identical");
        coverage("random");
    }
    std::printf("%lld passes compared on %s, %s\n", passes, yss236_core::jit_target, fails ? "FAIL" : "all identical");
    return fails ? 1 : 0;
}
