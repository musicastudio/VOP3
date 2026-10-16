#!/usr/bin/env python3
"""VOP3 (YSS236) microcode disassembler. Each step prints its raw fields, then its operation as measured on the
FS1R (docs/vop3_research_journal.md, sessions 8-16; the rules in docs/vop3_isa.md): s = the running value, k = the step's constant, x = r[rB] (+ r[rA]),
y = the result (clipped at +-8; the next step's s). `d[n] = y/4` is a route-scaled write to data cell n,
`dram[slot] = y` a delay-memory write, `xfer = dram[slot]` a read, `d[n] <- xfer(...)` a capture.
VOP3-1 and VOP3-2 are one chip: every decode here applies to every image (FS1R, PLG150-AN, AN1x) alike.

    python tools/vop3_disasm.py --listings        regenerate every listing in docs/vop3_disasm/

    python tools/vop3_disasm.py docs/vop3/program_0.bin --coef docs/vop3/coefficients_0.bin --byte docs/vop3/byte_table.bin
    python tools/vop3_disasm.py docs/vop3_an/mode0.bin --an --first 4
    python tools/vop3_disasm.py docs/vop3_2/base.bin --byte docs/vop3_2/base_bytes.bin
    python tools/vop3_disasm.py docs/vop3_an1x/voice.bin --an --labels docs/an1x_param_map.md

Step formats:
  5-word (FS1R uploads): r10 r9 r8 r7 r6, big-endian, 10 bytes per step
  8-word (--an, PLG150-AN images): r0C slot r0B r10 r9 r8 r7 r6, 16 bytes per step
"""
import argparse
import struct


def fields(r10, r9, r8, r7, r6):
    # Meanings are from docs/vop3_research_journal.md: CHIP = measured on the FS1R (VOP3-1 filter, sessions 8-10; VOP3-2 digital
    # out, sessions 11-16), FW = read from the firmwares. VOP3-1 and VOP3-2 are the same chip (ISA doc "One chip"):
    # a result from either applies to every VOP3 image. Key names are kept for tools/vop3_interp.py.
    return dict(
        mem=r10 & 7,            # CHIP s16: DRAM access, only on steps 3 mod 4, slot = step >> 2: 1 write, 2/3 read, 4/6 read one older
        r10hi=r10 >> 3,
        op9=(r9 >> 8) & 0xf,    # CHIP r9[11:8] `path`: exact-match on a feedback-loop step (s8); no effect on a feed-forward step (s12)
        op9hi=(r9 >> 12) & 7,   # r9[14:12]: bit 12 opened a filter loop (s8), no effect feed-forward (s15); 13/14 inert
        ra=r9 & 0x7f,           # CHIP r9[6:0]: class 2 operand A (added to r[rB], ignored when rB = 0); class 1 destination
        r9b7=(r9 >> 7) & 1,     # CHIP r9[7]: a mode bit of its own (same effect as r9[15], s8)
        r9b15=r9 >> 15,
        daddr=r8 >> 7,          # CHIP s16: 0x180|n = the step writes d[n] (op 7 reads it); 0x100|n = capture DRAM xfer into d[n];
                                # below 0x100: lookup-table pointers into other steps' constants (FW)
        mmode=r8 & 0x7f,        # CHIP s12/s16: inert, the DRAM read and write included
        f7a=r7 >> 14,           # CHIP r7[15:14]: no effect on any probed step
        f7b=(r7 >> 12) & 3,     # CHIP s12/s16 r7[13:12] `route`: scale of what the step writes out (d[], DRAM): 1/16, 1/8, 1/4, 1
        f7c=(r7 >> 7) & 0x1f,   # r7[11:7], kept for the raw header
        f7d=(r7 >> 6) & 1,
        op7=(r7 >> 6) & 7,      # CHIP s11-13 r7[8:6]: op (OPS below)
        r7sel=(r7 >> 10) & 1,   # CHIP s12 r7[10]: negates the running value s
        r7abs=(r7 >> 11) & 1,   # CHIP s15 r7[11]: y = |y|
        rd_en=(r7 >> 4) & 1,    # CHIP s14 r7[4]: with rB set the gain k is dropped (w[] never enters); with rB clear see disasm
        rd=r7 & 0xf,            # r7[3:0] `rsrc`
        f7e=r7 & 0x3f,
        f6a=r6 >> 14,           # CHIP r6[15:14] class: 1 = r[rA] = k/256 (8.8, s14), 2 = operation, 0 = no arithmetic, 3 = see disasm
        rb=(r6 >> 7) & 0x7f,    # CHIP r6[13:7]: operand B; x = r[rB] (+ r[rA]), rB = 0 -> x = s
        f6c=r6 & 0x3f,          # CHIP r6[5:0]: the result's write slot w[]
    )


ROUTE = ("/16", "/8", "/4", "")                 # route 0..3 scales the step's writes out (s12, s16)
OPS = {0: "{s} + {gx}", 1: "{gx}", 2: "{gx}", 3: "{gx}", 4: "{s}", 5: "sgn({s})", 6: "0", 7: "{gsrc}"}   # s11-s15


def operation(step, f, voices=0):
    """The step as its measured operation: [expression, side effects]."""
    out = []
    if f["f6a"] == 1:
        out.append("r[%02x] = k/256" % f["ra"])
        if f["rb"]:
            out.append("rB=" + reg(f["rb"], voices))    # the r9[15] variant (AN uploads) carries an rB; role unknown
    elif f["f6a"] == 2:
        a, b = f["ra"], f["rb"]
        x = "s" if not b else "(%s + %s)" % (reg(a, voices), reg(b, voices)) if a else reg(b, voices)
        g = "0" if f["rd_en"] and b else "k"           # s14: rd-en with rB set drops k
        gx = "0" if g == "0" else "k*" + x
        if f["op7"] == 1 and b:                        # s14: op 1 reading rB adds it: x + k*x
            gx = x if g == "0" else "%s + k*%s" % (x, x)
        src = "d[%02x]" % (f["daddr"] & 0x7f) if f["daddr"] >> 7 == 3 else "in"
        e = OPS[f["op7"]].format(s="-s" if f["r7sel"] else "s", gx=gx, gsrc="0" if g == "0" else "k*" + src)
        e = e[:-4] if e.endswith(" + 0") else e
        out.append("y = " + ("|%s|" % e if f["r7abs"] else e))
        if f["f6c"]:
            out.append("-> w[%02x]" % f["f6c"])
        if f["daddr"] >> 7 == 3 and f["op7"] != 7:
            out.append("d[%02x] = y%s" % (f["daddr"] & 0x7f, ROUTE[f["f7b"]]))
        if f["op7"] == 1 and f["f7b"] == 0:
            out.append("bus += y")
        if f["rd_en"] and not b:
            out.append("rd w[%x]" % f["rd"])          # s12: 0d3 keeps k for every rsrc; s11: on 0e9 it changed the level
    else:                                              # class 0 (no arithmetic) / 3 (made a filter loop run away, s8; not decoded)
        for n, nm in ((f["ra"], "rA="), (f["rb"], "rB=")):
            if n:
                out.append(nm + reg(n, voices))
        if f["f6c"]:
            out.append("-> w[%02x]" % f["f6c"])
        if f["rd_en"]:
            out.append("rd w[%x]" % f["rd"])
    d = f["daddr"]
    if d >> 7 == 2:
        out.append("d[%02x] <- xfer(steps %03x..%03x)" % (d & 0x7f, max(0, step - 11), max(0, step - 5)))   # s40c: the latest 5..11 back
    elif d and d >> 8 == 0:
        out.append("d[%03x]" % d)                      # table pointer
    elif d >> 7 == 3 and f["f6a"] != 2:
        out.append("d[%03x]" % d)
    m, slot = f["mem"], step >> 2
    ram = "table" if f["mmode"] & 0x20 else "dram"     # s40: r8[5] addresses the slot offset with no ring pointer
    if m and step < 3:
        out.append("mem%d(step < 3: no xfer)" % m)     # s40a/b/d: steps 000..002 never transfer; every phase does (s40f)
    elif m == 1:
        out.append("%s[%02x] = y%s" % (ram, slot, ROUTE[f["f7b"]]))
    elif m in (2, 3):
        out.append("xfer = %s[%02x]" % (ram, slot))
    elif m in (4, 6):
        out.append("xfer = %s[%02x] older" % (ram, slot))   # one pass older than 2/3
    elif m:
        out.append("mem%d[slot %02x] untested" % (m, slot))
    return out


def encode(cls=2, ra=0, rb=0, wdst=0, op=0, sel=0, route=0, rd=None, path=0, mem=0, daddr=0, mmode=0,
           r9x=0, r7x=0, r6x=0, r10x=0):
    """The inverse of fields(): build (r10, r9, r8, r7, r6) for a probe step. rd=None leaves the read disabled;
    the *x arguments OR raw bits in for fields that have no name yet."""
    r10 = mem | r10x
    r9 = path << 8 | ra | r9x
    r8 = daddr << 7 | mmode
    r7 = route << 12 | sel << 10 | op << 6 | (0x10 | rd if rd is not None else 0) | r7x
    r6 = cls << 14 | rb << 7 | wdst | r6x
    return r10, r9, r8, r7, r6


def reg(n, voices=0):
    """Register name. With --voices V, r[1 + V*k + v] is input/state k of voice v (AN1x: V=5; the FS1R filter
    is V=6 per channel with r[6c] the class-1 control word, so its stride starts at 0, not 1)."""
    if voices and 0 < n <= 13 * voices:      # AN1x: k = 0..12 verified per-voice (tools/vop3_verify.py); above that, scheduler temporaries
        k, v = divmod(n - 1, voices)
        return "in[%d].%d" % (k, v)
    return "r[%02x]" % n


def disasm(step, words, coef=None, byt=None, labels=None, voices=0):
    r10, r9, r8, r7, r6 = words
    f = fields(r10, r9, r8, r7, r6)
    lab = ("   ; " + labels[step]) if labels and step in labels else ""
    if not any(words):
        return "%03x  nop%s" % (step, lab)
    parts = operation(step, f, voices)
    op = "path=%x.%x%s%s route=%d op=%d%s r7=%d.%02x class=%d" % (f["op9hi"], f["op9"], "+" if f["r9b7"] else "", "^" if f["r9b15"] else "",
                                                                 f["f7b"], f["op7"], "s" if f["r7sel"] else "", f["f7a"], f["f7c"], f["f6a"])
    k = ""
    if coef is not None and coef[step]:
        k += " k=%04x(%+.4f)" % (coef[step], struct.unpack(">h", struct.pack(">H", coef[step]))[0] / 32768.0)
    if byt is not None and byt[step]:
        k += " b=%d" % byt[step]
    return "%03x  %s  %s%s%s" % (step, op, " ".join(parts), k, lab)


def load_labels(path):
    """docs/an1x_param_map.md lines: - `33` VCF Cutoff  ... steps=[165 16d ...] | step=004/009"""
    import re
    labels = {}
    for line in open(path, encoding="utf-8"):
        m = re.match(r"- `(\w+)` (.+?)\s+handler=.*?(?:steps=\[([0-9a-f ]+)\]|step=([0-9a-f]+)/([0-9a-f]+))", line)
        if not m:
            continue
        name = m.group(2).strip()
        steps = m.group(3).split() if m.group(3) else [m.group(4), m.group(5)]
        for v, st in enumerate(steps):
            st = int(st, 16)
            if st:
                tag = "%s v%d" % (name, v) if m.group(3) else name
                labels[st] = (labels[st] + ", " + tag) if st in labels else tag
    return labels


def load(path, an=False):
    b = open(path, "rb").read()
    if an:
        rows = [struct.unpack_from(">8H", b, i * 16) for i in range(len(b) // 16)]
        return [r[3:] for r in rows], [r[2] for r in rows], [r[0] for r in rows]
    return [struct.unpack_from(">5H", b, i * 10) for i in range(len(b) // 10)], None, None


def listings():
    """Every docs/vop3_disasm/*.txt, as (name, argv)."""
    out = [("fs1r_vop3_1_filter_v%d" % v, ["docs/vop3/program_%d.bin" % v, "--coef", "docs/vop3/coefficients_%d.bin" % v,
                                             "--byte", "docs/vop3/byte_table.bin"]) for v in (0, 1)]
    out += [("fs1r_vop3_2_test_image_%d" % v, ["docs/vop3_2/program_%d.bin" % v]) for v in (0, 1)]
    out.append(("fs1r_vop3_2_base", ["docs/vop3_2/base.bin", "--byte", "docs/vop3_2/base_bytes.bin"]))
    for kind, n, first in (("reverb", 4, "0xe8"), ("variation", 16, "0x1a0"), ("ins1", 12, "0x110"), ("ins2", 12, "0x158")):
        out += [("fs1r_vop3_2_%s_%02d" % (kind, i), ["docs/vop3_2/%s_%02d.bin" % (kind, i), "--byte",
                 "docs/vop3_2/%s_%02d_bytes.bin" % (kind, i), "--first", first]) for i in range(n)]
    out += [("plg150an_base", ["docs/vop3_an/base.bin", "--an"])]
    out += [("plg150an_mode%d" % i, ["docs/vop3_an/mode%d.bin" % i, "--an", "--first", "4"]) for i in range(3)]
    out += [("an1x_voice", ["docs/vop3_an1x/voice.bin", "--an", "--labels", "docs/an1x_param_map.md", "--voices", "5"]),
            ("an1x_boot", ["docs/vop3_an1x/boot.bin", "--an"])]
    return out


def main(argv=None):
    import sys
    argv = sys.argv[1:] if argv is None else argv
    if argv == ["--listings"]:
        import contextlib
        import io
        from pathlib import Path
        import os
        os.chdir(Path(__file__).resolve().parents[1])
        for name, args in listings():
            buf = io.StringIO()
            with contextlib.redirect_stdout(buf):
                main(args)
            Path("docs/vop3_disasm/%s.txt" % name).write_text(buf.getvalue())
        return
    ap = argparse.ArgumentParser()
    ap.add_argument("program")
    ap.add_argument("--an", action="store_true", help="8-word PLG150-AN image")
    ap.add_argument("--coef", help="512 x u16 per-step constants (reg 0xB)")
    ap.add_argument("--byte", help="per-step byte table (reg 0xC)")
    ap.add_argument("--first", type=lambda x: int(x, 0), default=0, help="program address of the first step")
    ap.add_argument("--labels", help="parameter->step map (docs/an1x_param_map.md) to annotate steps")
    ap.add_argument("--voices", type=int, default=0, help="name r[01..3f] as in[k].v with this many voices per input (AN1x: 5)")
    a = ap.parse_args(argv)
    labels = load_labels(a.labels) if a.labels else None
    steps, coef, byt = load(a.program, a.an)
    if a.coef:
        c = open(a.coef, "rb").read(); coef = list(struct.unpack(">%dH" % (len(c) // 2), c))
    if a.byte:
        byt = list(open(a.byte, "rb").read())
    if coef is not None and len(coef) < a.first + len(steps):
        coef = [0] * a.first + list(coef)   # AN images carry the constant per row, at that row's address
    if byt is not None and len(byt) < a.first + len(steps):
        byt = [0] * a.first + list(byt)
    for i, w in enumerate(steps):
        print(disasm(a.first + i, w, coef, byt, labels, a.voices))


def demo():
    # FS1R VOP3-1 step 0x08e (ch0 cutoff constant): r7=8000 r6=4000, k=7fff
    f = fields(0, 0x0061, 0, 0x8000, 0x4000)
    assert f["ra"] == 0x61 and f["f6a"] == 1 and f["rb"] == 0 and f["daddr"] == 0
    assert "r[61] = k/256" in disasm(0x8e, (0, 0x0061, 0, 0x8000, 0x4000))
    # FS1R step 0x098, the cutoff MAC: reg 0x62 * reg 0x61, op 1, mode 0x3e (measured on the chip)
    f = fields(0, 0x1162, 0, 0x9280, 0xb0be)
    assert f["op9"] == 1 and f["ra"] == 0x62 and f["rb"] == 0x61 and f["f6c"] == 0x3e and f["f6a"] == 2
    assert f["op7"] == 2 and f["r7sel"] == 0 and f["rd_en"] == 0
    assert fields(0, 0, 0, 0x8291, 0)["rd_en"] == 1 and fields(0, 0, 0, 0x8291, 0)["rd"] == 1
    assert "y = k*(r[62] + r[61]) -> w[3e]" in disasm(0x98, (0, 0x1162, 0, 0x9280, 0xb0be))
    assert "y = 0" in disasm(0x9c, (0, 0x1162, 0, 0x8291, 0xb13d))        # rd-en with rB set: k dropped (s14)
    assert "k*(in[2].1 + in[2].1)" in disasm(0x58, encode(ra=0x0c, rb=0x0c, op=2), voices=5)   # AN1x r[0c] = input 2, voice 1
    # FS1R VOP3-2 test image 1, the right channel's delay line (session 16)
    assert "y = s -> w[04]" in disasm(0xd3, (0x0001, 0x2000, 0x0040, 0x7100, 0x8004)) and \
        "dram[34] = y" in disasm(0xd3, (0x0001, 0x2000, 0x0040, 0x7100, 0x8004))
    assert "xfer = dram[36]" in disasm(0xdb, (0x0002, 0, 0x0040, 0, 0))
    assert "d[0b] <- xfer(steps 0d5..0db)" in disasm(0xe0, (0, 0, 0x8580, 0, 0))
    assert "y = k*d[0b]" in disasm(0xe9, encode(op=7, route=1, daddr=0x18b))
    assert "xfer = dram[36]" in disasm(0xd8, (0x0002, 0, 0x0040, 0, 0))            # s40f: phase 0 transfers
    assert "mem2(step < 3: no xfer)" in disasm(0x000, (0x0002, 0, 0x0040, 0, 0))
    assert "xfer = table[04]" in disasm(0x011, (0x0002, 0, 0x0068, 0x4112, 0x800A))   # PLG150-AN mode 0's exp read
    assert "y = |-s + k*s|" in disasm(0, encode(op=0, sel=1, r7x=0x800)) and "d[0b] = y/16" in disasm(0, encode(op=4, daddr=0x18b))
    assert "y = s + k*(r[31] + r[30])" in disasm(0xd3, encode(op=0, ra=0x31, rb=0x30))       # s15 sum
    assert "y = r[30] + k*r[30]" in disasm(0xd3, encode(op=1, rb=0x30, route=3))            # s14 op 1
    # PLG150-AN step 0x11: r10=2 marks a memory access, r8 low bits carry the mode
    f = fields(2, 0x0017, 0x0068, 0x4112, 0x800A)
    assert f["mem"] == 2 and f["mmode"] == 0x68 and f["daddr"] == 0 and f["f7e"] == 0x12
    assert disasm(0, (0, 0, 0, 0, 0)) == "000  nop"
    assert encode(ra=0x62, rb=0x61, wdst=0x3e, op=2, route=1, path=1, r9x=0x1000, r7x=0x8200) == (0, 0x1162, 0, 0x9280, 0xb0be)
    assert encode(ra=0x62, rb=0x62, wdst=0x3d, op=2, rd=1, path=1, r9x=0x1000, r7x=0x8200) == (0, 0x1162, 0, 0x8291, 0xb13d)
    assert encode(cls=1, ra=0x61, r7x=0x8000) == (0, 0x0061, 0, 0x8000, 0x4000)
    print("ok")


if __name__ == "__main__":
    import sys
    demo() if len(sys.argv) == 1 else main()
