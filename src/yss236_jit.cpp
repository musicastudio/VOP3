// license:BSD-3-Clause
// copyright-holders:James Hansen

// The YSS236 program compiled to x86-64 or arm64: one straight-line function per program, run once per pass in
// place of step(). Each step becomes only the code its decoded fields select. The interpreter in yss236_core.h
// stays the reference: tests/vop3_jit_check.cpp runs the two side by side over the FS1R's shipped programs
// and random ones and compares their whole state after every pass.
//
// What the compiled pass keeps from step():
//   - The state lives where the interpreter keeps it, in the core, so either can run any pass.
//   - A write issued at step st lands at the start of st + 3 through the interpreter's queue slot for it,
//     m_rq[st & 3] and the like (n is a multiple of 4 when a pass starts). Which register it lands in is
//     known when compiling, unless the issuing step can be skipped or the write lands in the next pass: then
//     the slot carries the register, as in the interpreter.
//   - A gate or a conditional skip that does not run is a forward branch to a path that lands the writes
//     due in the steps it skips, then joins the step it skips to.
//   - The arithmetic is the interpreter's, operation for operation, in double precision with no FMA. Its
//     clamps compare in the order minsd and maxsd do, so NaN and signed zero agree as well; arm64 compares
//     and selects, since its fmin and fmax return NaN. A scaling by a power of two is a multiply where the
//     interpreter divides; both round the same exact value.
//   - Captures, the delay index queue and a mode-0 result that wraps call the interpreter's own code.
// The steps are written once, in gen, over the few operations struct emit gives for each architecture.
// Built with the library's floating-point flags, like the interpreter it stands in for. AsmJit needs C++20.
#define _CRT_SECURE_NO_WARNINGS
#include "yss236_core.h"
#include <cstddef>
#include <cstdlib>
#include <functional>
#include <mutex>

#if defined(_M_X64) || defined(__x86_64__)
#include <asmjit/x86.h>
#define YSS236_JIT "x86-64"
#elif defined(_M_ARM64) || defined(__aarch64__)
#include <asmjit/a64.h>
#define YSS236_JIT "arm64"
#endif

struct yss236_core::jit_code { void (*fn)(yss236_core *, double *); };

// The interpreter's own code, for what the compiled pass hands back to it. n is set first where it is read.
struct yss236_jit
{
	using C = yss236_core;
	static void at(C *c, int st) { c->n = c->m_n0 + st + 1; }
	static int index(C *c, int st) { at(c, st); return c->cur_idx(); }
	static void queue_index(C *c, int st) { at(c, st); c->queue_i(std::floor(c->m_tmp[0] * 256.0)); }
	static double capture(C *c, int st) { return c->captured(c->m[c->ops[st].dm], st); }
	static double wrap(C *c, int) { return C::pymod(c->m_tmp[0], 2 * C::RANGE) - C::RANGE; }
	static double wrap_acc(C *c, int) { return C::pymod(c->m_tmp[0], 2 * C::ACC) - C::ACC; }
};

#ifdef YSS236_JIT
namespace {

using namespace asmjit;
using C = yss236_core;

// The constants the code reads
struct alignas(16) consts
{
	uint64_t abs[2] = { ~0ull >> 1, ~0ull >> 1 };
	uint64_t sign[2] = { 1ull << 63, 1ull << 63 };
	double zero = 0.0, k256 = 256.0, inv256 = 1.0 / 256, inv32768 = 1.0 / 32768;
	double range = C::RANGE, range2 = 2 * C::RANGE, acc = C::ACC, acc2 = 2 * C::ACC;
	double hi = C::RANGE - 1.0 / 256, lo = -C::RANGE;
	double sat = C::SAT, nsat = -C::SAT, dsat = C::DSAT, ndsat = -C::DSAT;
	double thi = 32767.0, tlo = -32768.0;
	double rscale[4] = { C::RSCALE[0], C::RSCALE[1], C::RSCALE[2], C::RSCALE[3] };
};
const consts K;

std::mutex lock;
JitRuntime &runtime()
{
	static JitRuntime *rt = new JitRuntime;   // never destroyed: a core may outlive static destruction
	return *rt;
}

struct failure : ErrorHandler
{
	bool failed = false;
	void handle_error(Error, const char *, BaseEmitter *) override { failed = true; }
};

static_assert(sizeof(C::xfer) == 16, "m_hist entries are 16 bytes");

// Each architecture's operations on values 0 to 5, its floating-point registers of those numbers, and on the
// core's fields by address. Value 5 is scratch, and a call keeps none of them.
#if defined(_M_X64) || defined(__x86_64__)
using namespace asmjit::x86;

#ifdef _WIN32
const Gp ARG0 = rcx, ARG1 = edx;
#else
const Gp ARG0 = rdi, ARG1 = esi;
#endif

// x86-64: the core through rbx, the delay RAM through r12 and the constants through r13. rax and rdx are
// scratch, and rcx holds the delay RAM address from address() to the access.
struct emit
{
	Assembler &a;
	C &c;

	int32_t off(const void *p) const { return int32_t((const char *)p - (const char *)&c); }
	Mem q(const void *p) const { return qword_ptr(rbx, off(p)); }
	Mem k(const double &v) const { return qword_ptr(r13, int32_t((const char *)&v - (const char *)&K)); }

	static bool usable() { return CpuInfo::host().features().x86().has_sse4_1(); }   // roundsd
	static void regs(FuncFrame &frame, FuncArgsAssignment &args)
	{
		frame.add_dirty_regs(rax, rcx, rdx, rbx, r12, r13, ARG0.r64(), ARG1.r64());
		frame.add_dirty_regs(xmm0, xmm1, xmm2, xmm3, xmm4, xmm5);
		frame.update_call_stack_size(32);     // the callee's home area on Windows
		args.assign_all(rbx, r12);
	}
	void start() { a.mov(r13, Imm(uint64_t(&K))); }

	void ld(int v, const double *p) { a.movsd(xmm(v), q(p)); }
	void st(const double *p, int v) { a.movsd(q(p), xmm(v)); }
	void ldk(int v, const double &x) { a.movsd(xmm(v), k(x)); }
	void mov(int v, int w) { if (v != w) a.movapd(xmm(v), xmm(w)); }
	void zero(int v) { a.xorpd(xmm(v), xmm(v)); }
	void add(int v, int w) { a.addsd(xmm(v), xmm(w)); }
	void sub(int v, int w) { a.subsd(xmm(v), xmm(w)); }
	void mul(int v, int w) { a.mulsd(xmm(v), xmm(w)); }
	void addk(int v, const double &x) { a.addsd(xmm(v), k(x)); }
	void subk(int v, const double &x) { a.subsd(xmm(v), k(x)); }
	void mulk(int v, const double &x) { a.mulsd(xmm(v), k(x)); }
	void neg(int v) { a.xorpd(xmm(v), ptr_128(r13, offsetof(consts, sign))); }
	void abs(int v) { a.andpd(xmm(v), ptr_128(r13, offsetof(consts, abs))); }
	void floor(int v, int w) { a.roundsd(xmm(v), xmm(w), 9); }
	// v = v < w ? v : w, and v = w < v ? v : w
	void min(int v, int w) { a.minsd(xmm(v), xmm(w)); }
	void max(int v, int w) { a.maxsd(xmm(v), xmm(w)); }
	void mink(int v, const double &x) { a.minsd(xmm(v), k(x)); }
	void maxk(int v, const double &x) { a.maxsd(xmm(v), k(x)); }

	// s16(*p) times scale into v
	void coef(int v, const uint16_t *p, const double &scale)
	{
		a.movsx(eax, word_ptr(rbx, off(p)));
		a.xorpd(xmm(v), xmm(v));
		a.cvtsi2sd(xmm(v), eax);
		a.mulsd(xmm(v), k(scale));
	}

	// m_flag from v's sign
	void flag(int v)
	{
		a.xorpd(xmm5, xmm5);
		a.comisd(xmm(v), xmm5);
		a.seta(al);
		a.comisd(xmm5, xmm(v));
		a.seta(cl);
		a.movzx(eax, al);
		a.movzx(ecx, cl);
		a.sub(eax, ecx);
		a.mov(dword_ptr(rbx, off(&c.m_flag)), eax);
	}

	void set32(const int32_t *p, int v) { a.mov(dword_ptr(rbx, off(p)), v); }
	void set8(const bool *p, int v) { a.mov(byte_ptr(rbx, off(p)), v); }
	void copy8(const bool *to, const bool *from)
	{
		a.movzx(eax, byte_ptr(rbx, off(from)));
		a.mov(byte_ptr(rbx, off(to)), al);
	}

	Label label() { return a.new_label(); }
	void bind(Label l) { a.bind(l); }
	void jmp(Label l) { a.jmp(l); }
	void if_zero(const bool *p, Label l)
	{
		a.cmp(byte_ptr(rbx, off(p)), 0);
		a.je(l);
	}
	// to l when m_flag is positive, or negative
	void if_flag(bool positive, Label l)
	{
		a.cmp(dword_ptr(rbx, off(&c.m_flag)), 0);
		if (positive)
			a.jg(l);
		else
			a.jl(l);
	}
	// to l unless 0 <= v < x, and when v is NaN
	void if_outside(int v, const double &x, Label l)
	{
		a.comisd(xmm(v), k(K.zero));
		a.jb(l);
		a.comisd(xmm(v), k(x));
		a.jae(l);
	}

	void call(void *fn, int st)
	{
		a.mov(ARG0, rbx);
		a.mov(ARG1, st);
		a.mov(rax, Imm(uint64_t(fn)));
		a.call(rax);
	}

	// The delay RAM address of the step's slot into ecx; with the index, cur_idx() is called first
	void address(int st, int older, bool index, bool tbl)
	{
		if (index)
			call((void *)&yss236_jit::index, st);
		int const s = st >> 2;
		a.movzx(ecx, word_ptr(rbx, off(&c.slot[s][0])));
		a.and_(ecx, 0x1ff);
		a.shl(ecx, 9);
		a.movzx(edx, word_ptr(rbx, off(&c.slot[s][1])));
		a.and_(edx, 0x1ff);
		a.or_(ecx, edx);
		if (!tbl)
			a.add(ecx, dword_ptr(rbx, off(&c.ptr)));
		if (older)
			a.add(ecx, older);
		if (index)
			a.add(ecx, eax);
		a.and_(ecx, C::RAM_WORDS - 1);
	}
	void ram_ld(int v) { a.movsd(xmm(v), qword_ptr(r12, rcx, 3)); }
	void ram_st(int v) { a.movsd(qword_ptr(r12, rcx, 3), xmm(v)); }

	// push_hist(st, value 0)
	void hist(int st)
	{
		Label full = a.new_label();
		a.mov(eax, dword_ptr(rbx, off(&c.m_nhist)));
		a.cmp(eax, C::STEPS);
		a.jge(full);
		a.mov(edx, eax);
		a.shl(edx, 4);
		a.mov(dword_ptr(rbx, rdx, 0, off(&c.m_hist[0].st)), st);
		a.movsd(qword_ptr(rbx, rdx, 0, off(&c.m_hist[0].v)), xmm0);
		a.inc(eax);
		a.mov(dword_ptr(rbx, off(&c.m_nhist)), eax);
		a.bind(full);
	}

	// The write in a queue slot lands in file[reg] if the slot says it was issued (reg > 0 for r, >= 0 else)
	void land(C::pend *slot, double *file, bool r)
	{
		Label none = a.new_label();
		a.movsxd(rax, dword_ptr(rbx, off(&slot->reg)));
		a.test(eax, eax);
		if (r)
			a.jle(none);
		else
			a.js(none);
		a.movsd(xmm0, q(&slot->v));
		a.movsd(qword_ptr(rbx, rax, 3, off(file)), xmm0);
		a.bind(none);
		a.mov(dword_ptr(rbx, off(&slot->reg)), -1);
	}
};
#else
using namespace asmjit::a64;

// arm64: the core through x19, the delay RAM through x20 and the constants through x21. Most of the core's
// fields lie past what an offset from x19 reaches, so x22 points at r[], below them. w9, w11, x16, x17 and d16
// are scratch, and w10 holds the delay RAM address from address() to the access. x18 is the platform's on
// Apple and Windows and is never touched.
struct emit
{
	Assembler &a;
	C &c;

	static Vec V(int v) { return a64::d(uint32_t(v)); }
	int32_t off(const void *p) const { return int32_t((const char *)p - (const char *)&c); }
	static bool fits(int32_t o, int size) { return o >= 0 && o % size == 0 && o / size < 4096; }
	// A field of size bytes: an unsigned offset from x19 or x22, or its address in x17
	Mem at(const void *p, int size)
	{
		int32_t const o = off(p), h = o - off(c.r);
		if (fits(o, size))
			return ptr(x19, o);
		if (fits(h, size))
			return ptr(x22, h);
		a.mov(x17, Imm(int64_t(o)));
		a.add(x17, x19, x17);
		return ptr(x17);
	}
	// A field's address into r
	void addr(Gp const &r, const void *p)
	{
		int32_t const o = off(p), h = o - off(c.r);
		if (h >= 0 && h < 4096)
			a.add(r, x22, Imm(h));
		else
		{
			a.mov(r, Imm(int64_t(o)));
			a.add(r, x19, r);
		}
	}
	Mem k(const double &v) const { return ptr(x21, int32_t((const char *)&v - (const char *)&K)); }

	static bool usable() { return true; }
	static void regs(FuncFrame &frame, FuncArgsAssignment &args)
	{
		frame.add_dirty_regs(x19, x20, x21, x22);
		args.assign_all(x19, x20);
	}
	void start()
	{
		a.mov(x21, Imm(uint64_t(&K)));
		a.mov(x22, Imm(int64_t(off(c.r))));
		a.add(x22, x19, x22);
	}

	void ld(int v, const double *p) { a.ldr(V(v), at(p, 8)); }
	void st(const double *p, int v) { a.str(V(v), at(p, 8)); }
	void ldk(int v, const double &x) { a.ldr(V(v), k(x)); }
	void mov(int v, int w) { if (v != w) a.fmov(V(v), V(w)); }
	void zero(int v) { a.fmov(V(v), xzr); }
	void add(int v, int w) { a.fadd(V(v), V(v), V(w)); }
	void sub(int v, int w) { a.fsub(V(v), V(v), V(w)); }
	void mul(int v, int w) { a.fmul(V(v), V(v), V(w)); }
	void addk(int v, const double &x) { ldk(16, x); add(v, 16); }
	void subk(int v, const double &x) { ldk(16, x); sub(v, 16); }
	void mulk(int v, const double &x) { ldk(16, x); mul(v, 16); }
	void neg(int v) { a.fneg(V(v), V(v)); }
	void abs(int v) { a.fabs(V(v), V(v)); }
	void floor(int v, int w) { a.frintm(V(v), V(w)); }
	// v = v < w ? v : w, and v = w < v ? v : w. mi and gt are false when unordered, which selects w as
	// minsd and maxsd do.
	void min(int v, int w)
	{
		a.fcmp(V(v), V(w));
		a.fcsel(V(v), V(v), V(w), Imm(CondCode::kMI));
	}
	void max(int v, int w)
	{
		a.fcmp(V(v), V(w));
		a.fcsel(V(v), V(v), V(w), Imm(CondCode::kGT));
	}
	void mink(int v, const double &x) { ldk(16, x); min(v, 16); }
	void maxk(int v, const double &x) { ldk(16, x); max(v, 16); }

	// s16(*p) times scale into v
	void coef(int v, const uint16_t *p, const double &scale)
	{
		a.ldrsh(w9, at(p, 2));
		a.scvtf(V(v), w9);
		mulk(v, scale);
	}

	// m_flag from v's sign
	void flag(int v)
	{
		a.fcmp(V(v), Imm(0));
		a.cset(w9, Imm(CondCode::kGT));
		a.cset(w11, Imm(CondCode::kMI));
		a.sub(w9, w9, w11);
		a.str(w9, at(&c.m_flag, 4));
	}

	void set32(const int32_t *p, int v)
	{
		if (v)
		{
			a.mov(w9, Imm(v));
			a.str(w9, at(p, 4));
		}
		else
			a.str(wzr, at(p, 4));
	}
	void set8(const bool *p, int v)
	{
		if (v)
		{
			a.mov(w9, Imm(v));
			a.strb(w9, at(p, 1));
		}
		else
			a.strb(wzr, at(p, 1));
	}
	void copy8(const bool *to, const bool *from)
	{
		a.ldrb(w9, at(from, 1));
		a.strb(w9, at(to, 1));
	}

	Label label() { return a.new_label(); }
	void bind(Label l) { a.bind(l); }
	void jmp(Label l) { a.b(l); }
	void if_zero(const bool *p, Label l)
	{
		a.ldrb(w9, at(p, 1));
		a.cbz(w9, l);
	}
	// to l when m_flag is positive, or negative
	void if_flag(bool positive, Label l)
	{
		a.ldr(w9, at(&c.m_flag, 4));
		a.cmp(w9, Imm(0));
		if (positive)
			a.b_gt(l);
		else
			a.b_lt(l);
	}
	// to l unless 0 <= v < x, and when v is NaN: lt holds for unordered, ge does not
	void if_outside(int v, const double &x, Label l)
	{
		a.fcmp(V(v), Imm(0));
		a.b_lt(l);
		ldk(16, x);
		a.fcmp(V(v), V(16));
		a.b_ge(l);
	}

	void call(void *fn, int st)
	{
		a.mov(x0, x19);
		a.mov(w1, Imm(st));
		a.mov(x16, Imm(uint64_t(fn)));
		a.blr(x16);
	}

	// The delay RAM address of the step's slot into w10; with the index, cur_idx() is called first
	void address(int st, int older, bool index, bool tbl)
	{
		if (index)
			call((void *)&yss236_jit::index, st);
		int const s = st >> 2;
		a.ldrh(w10, at(&c.slot[s][0], 2));
		a.and_(w10, w10, Imm(0x1ff));
		a.ldrh(w11, at(&c.slot[s][1], 2));
		a.and_(w11, w11, Imm(0x1ff));
		a.orr(w10, w11, w10, Imm(lsl(9)));
		if (!tbl)
		{
			a.ldr(w11, at(&c.ptr, 4));
			a.add(w10, w10, w11);
		}
		if (older)
			a.add(w10, w10, Imm(older));
		if (index)
			a.add(w10, w10, w0);
		a.and_(w10, w10, Imm(C::RAM_WORDS - 1));
	}
	void ram_ld(int v) { a.ldr(V(v), ptr(x20, x10, lsl(3))); }
	void ram_st(int v) { a.str(V(v), ptr(x20, x10, lsl(3))); }

	// push_hist(st, value 0)
	void hist(int st)
	{
		Label full = a.new_label();
		a.ldr(w9, at(&c.m_nhist, 4));
		a.cmp(w9, Imm(C::STEPS));
		a.b_ge(full);
		addr(x16, &c.m_hist[0]);
		a.add(x16, x16, x9, Imm(lsl(4)));
		a.mov(w11, Imm(st));
		a.str(w11, ptr(x16, int32_t(offsetof(C::xfer, st))));
		a.str(d0, ptr(x16, int32_t(offsetof(C::xfer, v))));
		a.add(w9, w9, Imm(1));
		a.str(w9, at(&c.m_nhist, 4));
		a.bind(full);
	}

	// The write in a queue slot lands in file[reg] if the slot says it was issued (reg > 0 for r, >= 0 else)
	void land(C::pend *slot, double *file, bool r)
	{
		Label none = a.new_label();
		a.ldrsw(x9, at(&slot->reg, 4));
		if (r)
		{
			a.cmp(w9, Imm(0));
			a.b_le(none);
		}
		else
			a.tbnz(w9, Imm(31), none);
		a.ldr(d0, at(&slot->v, 8));
		addr(x16, file);
		a.str(d0, ptr(x16, x9, lsl(3)));
		a.bind(none);
		a.mov(w9, Imm(-1));
		a.str(w9, at(&slot->reg, 4));
	}
};
#endif

class gen
{
	enum { R, D, T };                  // the three write queues

	emit e;
	C &c;
	Label step_at[C::STEPS];
	std::vector<std::function<void()>> stubs;   // out-of-line code, emitted after the pass's return
	int8_t writes[C::STEPS][3];        // the register, cell or t register a step writes when it runs, or -1
	bool skippable[C::STEPS] = {};     // a control step before it may skip it

	C::pend *queue(int w, int i) const { return &(w == R ? c.m_rq : w == D ? c.m_dq : c.m_tq)[i & 3]; }
	double *file(int w) const { return w == R ? c.r : w == D ? c.m : c.t; }

	// What each step writes and which steps may be skipped
	void analyse()
	{
		for (int st = 0; st < C::STEPS; st++)
		{
			C::op const &o = c.ops[st];
			int8_t *w = writes[st];
			w[R] = w[D] = w[T] = -1;
			if (o.ctlop)
			{
				for (int s = st + 1; s < ((st & 0x100) | o.target); s++)
					skippable[s] = true;
				continue;
			}
			int const cls = o.mmode;
			if (o.dm && o.msrc <= C::M_CAPTURE2)
				w[D] = o.dm;
			if (cls == 0)
			{
				if (o.alu && o.dr && !o.rcopy)
					w[R] = o.dr;
				continue;
			}
			if (o.dr)
				w[R] = o.dr;
			if (o.tw & 3)
				w[T] = o.tn;
			if (o.dm && cls == 1 && o.msrc == C::M_RESULT && (o.dwr || !o.dr))
				w[D] = o.dm;
			if (o.dm && cls >= 2 && (o.msrc == C::M_PREVIOUS ||
					(o.msrc == C::M_RESULT && (!(o.m2m && o.asel == C::A_NONE) || o.dm != o.sm))))
				w[D] = o.dm;
		}
	}

	// The writes issued three steps ago land
	void land(int st)
	{
		for (int w = R; w <= T; w++)
		{
			C::pend *slot = queue(w, st + 1);
			int const from = st - 3;
			if (from >= 0 && writes[from][w] < 0)
				continue;                             // nothing was issued
			if (from >= 0 && !skippable[from])
			{
				e.ld(0, &slot->v);                    // issued for sure, to a known register
				e.st(file(w) + writes[from][w], 0);
				continue;
			}
			e.land(slot, file(w), w == R);            // the slot says
		}
	}

	// Issue a write from v
	void put(int w, int st, int v)
	{
		C::pend *slot = queue(w, st);
		e.st(&slot->v, v);
		if (skippable[st] || st + C::LAT >= C::STEPS)
			e.set32(&slot->reg, writes[st][w]);
	}

	// mode(), or accmode() with acc set, of src into dst
	void mode(int dst, int src, int mo, bool acc, int st)
	{
		e.mov(dst, src);
		if (mo)
		{
			e.mink(dst, K.hi);
			e.maxk(dst, K.lo);
			if (mo == 2)
				e.maxk(dst, K.zero);
			else if (mo == 3)
				e.abs(dst);
			return;
		}
		Label slow = e.label(), back = e.label();
		e.addk(dst, acc ? K.acc : K.range);
		e.if_outside(dst, acc ? K.acc2 : K.range2, slow);
		e.subk(dst, acc ? K.acc : K.range);
		e.bind(back);
		stubs.push_back([this, dst, acc, st, slow, back] {
			e.bind(slow);
			for (int i = 0; i < 6; i++)
				if (i != dst)
					e.st(&c.m_tmp[2 + i], i);
			e.st(&c.m_tmp[0], dst);
			e.call(acc ? (void *)&yss236_jit::wrap_acc : (void *)&yss236_jit::wrap, st);
			e.mov(dst, 0);
			for (int i = 0; i < 6; i++)
				if (i != dst)
					e.ld(i, &c.m_tmp[2 + i]);
			e.jmp(back);
		});
	}

	void clip(int v, double const &c, double const &nc)
	{
		e.mink(v, c);
		e.maxk(v, nc);
	}

	// The adder input into dst. x is in 1 and the m operand in 2.
	void adder(int dst, C::op const &o)
	{
		switch (o.asel)
		{
			case C::A_P: e.ld(dst, &c.acc); break;
			case C::A_R:
			case C::A_M:
				if (o.asel == C::A_R ? o.sr : o.sm)
					e.mov(dst, o.asel == C::A_R ? 1 : 2);
				else
				{
					e.ld(dst, &c.acc);
					e.mulk(dst, K.inv32768);
				}
				break;
			default: e.zero(dst); break;
		}
	}

	// px = x; haspx = hasx; x = xv; hasx = sr != 0, with xv into 1
	void fetch_x(C::op const &o)
	{
		if (o.sr)
			e.ld(1, &c.r[o.sr]);
		else
			e.zero(1);
		e.ld(0, &c.x);
		e.st(&c.px, 0);
		e.copy8(&c.haspx, &c.hasx);
		e.st(&c.x, 1);
		e.set8(&c.hasx, o.sr != 0);
	}

	// psrc = src; src = m[sm], with the new src into 2
	void fetch_m(C::op const &o)
	{
		e.ld(0, &c.src);
		e.st(&c.psrc, 0);
		e.ld(2, &c.m[o.sm]);
		e.st(&c.src, 2);
	}

	void t_write(int st, int y)
	{
		C::op const &o = c.ops[st];
		if (o.tw & 2)
		{
			e.mov(0, y);
			e.mulk(0, K.k256);
			if (o.tfrac)
			{
				e.floor(5, 0);
				e.sub(0, 5);
			}
			else
			{
				e.ldk(5, K.thi);
				e.min(5, 0);
				e.ldk(0, K.tlo);
				e.max(0, 5);
				e.mulk(0, K.inv32768);
			}
			put(T, st, 0);
		}
		else if (o.tw & 1)
		{
			e.coef(0, &c.coef[st], K.inv32768);
			put(T, st, 0);
		}
	}

	void control(int st)
	{
		C::op const &o = c.ops[st];
		int const to = (st & 0x100) | o.target;
		Label run = e.label();
		if (o.cskip == 6 || o.cskip == 4)
			e.if_flag(o.cskip == 6, run);
		e.set32(&c.m_skip, to);
		if (to > st + 1)
		{
			Label skip = e.label();
			e.jmp(skip);
			stubs.push_back([this, st, to, skip] {
				e.bind(skip);
				for (int s = st + 1; s < to && s <= st + C::LAT; s++)
					land(s);                          // later ones were issued inside the skip
				e.jmp(step_at[to]);
			});
		}
		e.bind(run);
	}

	void step(int st)
	{
		e.bind(step_at[st]);
		land(st);
		C::op const &o = c.ops[st];
		if (o.ctlop)
		{
			control(st);
			return;
		}
		int const cls = o.mmode;
		int const page = (st >> 5) & 15;
		bool const xfer = st >= C::XFER_FROM;
		bool const write = xfer && o.memop == 1;

		if (xfer && (o.memop == 2 || o.memop == 3 || o.memop == 4 || o.memop == 6))
		{
			e.address(st, (o.memop & 4) ? 1 : 0, o.idxa, o.tbl);
			e.ram_ld(0);
			e.hist(st);
		}

		if (o.dm)
			switch (o.msrc)
			{
				case C::M_SERIAL: case C::M_SERIAL2: case C::M_EG:
					e.ld(0, &c.in[o.msrc][page]);
					put(D, st, 0);
					break;
				case C::M_LFO:
					e.ld(0, &c.lfo[st >> 5]);
					put(D, st, 0);
					break;
				case C::M_CAPTURE: case C::M_CAPTURE2:
					e.call((void *)&yss236_jit::capture, st);
					put(D, st, 0);
					break;
				default:
					break;
			}

		if (o.sout)
		{
			e.ld(0, &c.m[o.sm]);
			e.st(&c.out[page], 0);
		}

		if (cls < 2)
			fetch_x(o);

		if (cls == 1)
		{
			fetch_m(o);
			adder(4, o);
			e.coef(3, &c.coef[st], K.inv256);         // LOADK * s16(coef)
			if (o.sub)
				e.neg(4);
			e.add(3, 4);
			if (o.shift)
				e.mulk(3, K.rscale[o.shift]);
			if (o.dr)
			{
				mode(0, 3, o.clamp, false, st);
				put(R, st, 0);
			}
			mode(3, 3, o.clamp, true, st);
			e.st(&c.acc, 3);
			if (o.cmp)
				e.flag(3);
			if (o.msrc == C::M_RESULT && o.dm && (o.dwr || !o.dr))
			{
				e.mov(0, 3);
				clip(0, K.dsat, K.ndsat);
				put(D, st, 0);
			}
			t_write(st, 3);
			return;
		}

		if (cls == 0)
		{
			if (write)
			{
				Label none = e.label();
				e.if_zero(&c.haswl, none);
				e.address(st, 0, o.idxa, o.tbl);
				e.ld(0, &c.wlatch);
				e.ram_st(0);
				e.set8(&c.haswl, 0);
				e.bind(none);
				if (o.idxa)
					e.ld(1, &c.x);                    // x again for the adder: the index call keeps no register
			}
			if (o.alu)
			{
				fetch_m(o);
				adder(3, o);
				if (o.sub)
					e.neg(3);
				if (o.shift)
					e.mulk(3, K.rscale[o.shift]);
				if (o.dr && !o.rcopy)
				{
					mode(0, 3, o.clamp, false, st);
					put(R, st, 0);
				}
				mode(3, 3, o.clamp, true, st);
				e.st(&c.acc, 3);
				if (o.cmp)
					e.flag(3);
			}
			return;
		}

		// classes 2 and 3
		fetch_x(o);
		fetch_m(o);
		int const m2 = o.m2m ? 2 : 1;
		if (cls == 3)
			e.mov(3, m2);
		else
		{
			if (o.m1 >= 2)
				e.ld(3, &c.t[((o.m1 & 1) << 3) | o.t]);
			else
				e.coef(3, &c.coef[st], K.inv32768);
			e.mul(3, m2);
		}
		adder(4, o);
		if (o.sub)
			e.sub(3, 4);
		else
			e.add(3, 4);
		if (o.shift)
			e.mulk(3, K.rscale[o.shift]);
		if (o.dr && o.rcopy)
		{
			Label none = e.label();                   // the previous step's x, if it had one
			e.mov(0, 1);
			e.if_zero(&c.haspx, none);
			e.ld(0, &c.px);
			e.bind(none);
			put(R, st, 0);
		}
		else if (o.dr)
		{
			mode(0, 3, o.clamp, false, st);
			put(R, st, 0);
		}
		mode(3, 3, o.clamp, true, st);
		e.st(&c.acc, 3);
		if (o.cmp)
			e.flag(3);
		if (o.dm && o.msrc == C::M_PREVIOUS)
		{
			e.ld(0, &c.psrc);
			put(D, st, 0);
		}
		else if (o.dm && o.msrc == C::M_RESULT && (!(o.m2m && o.asel == C::A_NONE) || o.dm != o.sm))
		{
			e.mov(0, 3);
			clip(0, K.dsat, K.ndsat);
			put(D, st, 0);
		}
		t_write(st, 3);
		if (o.idxw)
		{
			e.st(&c.m_tmp[0], 3);
			e.call((void *)&yss236_jit::queue_index, st);
		}
		if (o.wdata)
		{
			if (write)
				e.set8(&c.haswl, 0);
			else
			{
				e.ld(0, &c.acc);
				clip(0, K.sat, K.nsat);
				e.st(&c.wlatch, 0);
				e.set8(&c.haswl, 1);
			}
		}
		if (write)
		{
			e.address(st, 0, o.idxa, o.tbl);
			e.ld(0, &c.acc);
			clip(0, K.sat, K.nsat);
			if (!o.wdata)
			{
				Label none = e.label();               // a latched word goes instead
				e.if_zero(&c.haswl, none);
				e.ld(0, &c.wlatch);
				e.set8(&c.haswl, 0);
				e.bind(none);
			}
			e.ram_st(0);
		}
	}

public:
	gen(Assembler &a, C &c) : e{ a, c }, c(c) {}

	void pass()
	{
		analyse();
		e.start();
		for (auto &l : step_at)
			l = e.label();
		for (int st = 0; st < C::STEPS; st++)
			step(st);
	}

	void out_of_line()
	{
		for (size_t i = 0; i < stubs.size(); i++)   // a skip path's landings can add stubs of their own
			stubs[i]();
	}
};

C::jit_code *compile(C &c)
{
	static bool const usable = emit::usable();
	if (!usable)
		return nullptr;
	std::lock_guard<std::mutex> guard(lock);
	JitRuntime &rt = runtime();
	CodeHolder code;
	failure fail;
	if (code.init(rt.environment(), rt.cpu_features()) != Error::kOk)
		return nullptr;
	code.set_error_handler(&fail);
	Assembler a(&code);

	FuncDetail func;
	func.init(FuncSignature::build<void, C *, double *>(), rt.environment());
	FuncFrame frame;
	frame.init(func);
	frame.set_func_calls();
	FuncArgsAssignment args(&func);
	emit::regs(frame, args);
	args.update_func_frame(frame);
	frame.finalize();

	a.emit_prolog(frame);
	a.emit_args_assignment(frame, args);
	gen g(a, c);
	g.pass();
	a.emit_epilog(frame);
	g.out_of_line();
	if (fail.failed)
		return nullptr;

	void (*fn)(C *, double *);
	if (rt.add(&fn, &code) != Error::kOk)
		return nullptr;
	return new C::jit_code{ fn };
}

}  // namespace

void yss236_core::jit_free::operator()(jit_code *code) const
{
	std::lock_guard<std::mutex> guard(lock);
	runtime().release(code->fn);
	delete code;
}
const char *const yss236_core::jit_target = YSS236_JIT;
#else
static yss236_core::jit_code *compile(yss236_core &) { return nullptr; }
void yss236_core::jit_free::operator()(jit_code *code) const { delete code; }
const char *const yss236_core::jit_target = nullptr;
#endif

bool yss236_core::jit_pass()
{
	static bool const off = std::getenv("VOP3_INTERP") != nullptr;
	if (interpret || off || (n & 3))
		return false;
	if (m_stale)
	{
		m_jit.reset(compile(*this));
		m_stale = false;
	}
	if (!m_jit)
		return false;
	m_n0 = n;
	m_jit->fn(this, ram.data());
	n = m_n0 + STEPS;
	return true;
}
