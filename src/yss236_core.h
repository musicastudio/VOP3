// license:BSD-3-Clause
// copyright-holders:Wouter van Nifterick

// Yamaha YSS236-F (XT013A00) "VOP3" DSP: the microcode engine.
//
// The chip runs a 512-step VLIW program once per sample 
// (24.576 MHz / 512 = 48 kHz). 
//
// Each step is five 16-bit words the host writes to 
//   - registers 10, 9, 8, 7 and 6 (w0..w4 here), 
//   - plus a 16-bit constant (register 0x0B) 
//   - and a tag byte (0x0C). 
//
// State is:
//   - the running value p
//   - 128 r registers, 
//   - 64 m cells
//   - 16 t registers
//   - 128 delay slots into a 2^18-word delay RAM
//   - 16 LFO values
//   - 16 EG levels
//   - 16 serial input slots (two banks)
//   - 16 serial output slots

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

class yss236_core
{
public:
	static constexpr int STEPS = 512;
	static constexpr int SLOTS = 128;
	static constexpr int LFOS = 16;
	static constexpr int RAM_WORDS = 1 << 18;
	static constexpr int MCELLS = 64;

	// Measured on the unit (sNN: the session in docs/vop3_research_journal.md; the rules in docs/vop3_isa.md)
	static constexpr int LAT = 3;              // s24 r10: a register written at step n is read from n + 3
	static constexpr int CAPMIN = 5;           // s40c: a capture takes a RAM transfer made 5..11 steps earlier
	static constexpr int XFER_FROM = 3;        // s40a/b/d: RAM marks on steps 000..002 transfer nothing
	static constexpr int DLAT = 3;             // s30/s31: m cell writes and captures land 3 steps later
	static constexpr double RANGE = 128.0;     // s24: registers span [-128, 128), word / 256
	static constexpr double ACC = 256.0;       // s24: the running value wraps at +-256 (mode 0)
	static constexpr double SAT = 32.0;        // s41k: the delay RAM word holds at least +-11 (Vokodrone's feedback
	                                           // node, tapped on the unit); +-32 as d[] (s38l). The s14 +-8 was the DAC
	static constexpr double DSAT = 128.0;      // s27: an m cell is a full register
	static constexpr double LOADK = 128.0;     // s14: a class-1 load v reads back as v / 256
	static constexpr double RSCALE[4] = { 1.0, 2.0, 4.0, 16.0 };   // s24: route 0..3 scales the result

	// A step's m write source (w2[15:13]) and adder input (w3[7:6])
	enum { M_SERIAL, M_SERIAL2, M_LFO, M_EG, M_CAPTURE, M_CAPTURE2, M_RESULT, M_PREVIOUS };
	enum { A_P, A_R, A_M, A_NONE };

	// One decoded step. Word k is chip register 10 - k.
	struct op
	{
		uint8_t memop;   // w0[2:0]  delay RAM: 1 write, 2/3 read, 4/6 read one word older
		uint8_t dr;      // w1[6:0]  r register written
		uint8_t rcopy;   // w1[7]    dr takes the previous step's x operand (z^-1)
		uint8_t tn;      // w1[11:8] t register written
		uint8_t tw;      // w1[12:11] t write: bit 1 the result, bit 0 alone the step's constant
		uint8_t wdata;   // w1[13]   this step's output is the data of the next delay RAM write
		uint8_t tfrac;   // w1[14]   t takes the fraction of p below one delay sample, 3 steps later
		uint8_t dwr;     // w1[12:8] nonzero: a class-1 step also writes its m cell (s29)
		uint8_t idxw;    // w2[2]    delay address index from p, in effect 3 steps later (the scale is OPEN)
		uint8_t idxa;    // w2[3]    add the index to this memory cycle
		uint8_t tbl;     // w2[5]    the RAM access ignores the ring pointer (s40; the AN boards' tables, docs/vop3_an_ram.md)
		uint8_t dm;      // w2[12:7] m cell written
		uint8_t msrc;    // w2[15:13] m write source: 0 serial, 1 serial bank 2, 2 LFO, 3 EG level, 4/5 capture, 6 p, 7 previous operand
		uint8_t t;       // w3[2:0]  t register read
		uint8_t m1;      // w3[4:3]  multiplier input 1: 0/1 constant, 2/3 t register
		uint8_t asel;    // w3[7:6]  adder input: 0 p, 1 r[sr], 2 m[sm], 3 none
		uint8_t m2m;     // w3[8]    multiplier input 2 is m[sm], else r[sr]
		uint8_t sub;     // w3[10]   negate the adder input
		uint8_t shift;   // w3[13:12] result x1, x2, x4, x16
		uint8_t clamp;   // w3[15:14] 0 wrap, 1 saturate, 2 clamp negatives to 0, 3 abs
		uint8_t sm;      // w4[5:0]  m cell read
		uint8_t sout;    // w4[6]    m[sm] goes to serial output step/64
		uint8_t sr;      // w4[13:7] r register read
		uint8_t mmode;   // w4[15:14] 0 off, 1 constant as value, 2 m1 * m2, 3 m2 as value
		uint8_t alu;     // the ALU runs (mmode, or any of asel, shift, clamp, sub)
		uint8_t ctl;     // w1[15]   control step
		uint8_t gate;    // control step with w3[3:0] = 8: the steps up to target wait for the host trigger
		uint8_t target;  // w4[15:8] step a gate skips to, within the same half of the program
		uint8_t cmp;     // w2[0]    the step's result sets the compare flag
		uint8_t ctlop;   // gate or cskip: the step is not run, it decides which of the following are
		uint8_t cskip;   // control step with w3[3:0] = 0xD: w3[6:4] 6 run what follows only if the flag is positive,
		                 // 4 only if it is negative, 1 never (a jump); 0 not one of these
	};

	// What the host writes
	uint16_t prog[STEPS][5];
	uint16_t coef[STEPS];
	uint8_t  tag[STEPS];
	uint16_t slot[SLOTS][2];      // registers 0x0D (bits 17-9) and 0x0E (bits 8-0); the AN boards' H8 drivers load the same pair

	// Decoded program
	op ops[STEPS];

	// State
	double r[128];
	double m[MCELLS];
	double t[16];
	double acc;                   // the running value p
	double x, px;                 // this and the previous step's r operand
	bool   hasx, haspx;           // whether those came from a register (rB != 0)
	double src, psrc;             // this and the previous step's m operand
	double wlatch;                // delay RAM write data (w1[13])
	bool haswl;
	double idx;                   // delay address index
	std::vector<double> ram;
	int32_t ptr;                  // delay RAM pointer, steps once per pass
	int64_t n;                    // global step counter

	// Serial inputs, EG levels and LFO values for the current pass. A cell move with source
	// 0, 1 or 3 takes in[source][slot]: the two serial banks and the EG levels. A slot is a
	// 64-step page and its half.
	static constexpr int SLOTS_IO = 16;
	double in[4][SLOTS_IO];       // [source][page * 2 + half]
	double out[SLOTS_IO];
	double lfo[LFOS];

	yss236_core()
	{
		std::memset(prog, 0, sizeof(prog));
		std::memset(coef, 0, sizeof(coef));
		std::memset(tag , 0, sizeof(tag));
		std::memset(slot, 0, sizeof(slot));
		ram.resize(RAM_WORDS);
		decode_all();
		reset();
	}

	void decode(int st)
	{
		op o;
		decode(prog[st], o);
		if (std::memcmp(&o, &ops[st], sizeof o))
			m_stale = true;               // the compiled pass no longer matches the program
		ops[st] = o;
	}

	static void decode(uint16_t const *w, op &o)
	{
		o.memop  = w[0] & 7;
		o.dr     = w[1] & 0x7f;
		o.rcopy  = (w[1] >> 7) & 1;
		o.tn     = (w[1] >> 8) & 15;
		o.tw     = (w[1] >> 11) & 3;
		o.ctl    = (w[1] >> 15) & 1;
		o.gate   = o.ctl && (w[3] & 15) == 8;
		o.target = w[4] >> 8;
		o.cmp    = w[2] & 1;
		{
			int const code = (w[3] >> 4) & 7;
			o.cskip = o.ctl && (w[3] & 15) == 0xD && (code == 6 || code == 4 || code == 1) ? code : 0;
		}
		o.ctlop = o.gate || o.cskip;
		o.wdata = (w[1] >> 13) & 1;
		o.tfrac = (w[1] >> 14) & 1;
		o.dwr   = (w[1] >> 8) & 0x1f;

		o.idxw  = (w[2] >> 2) & 1;
		o.idxa  = (w[2] >> 3) & 1;
		o.tbl   = (w[2] >> 5) & 1;
		o.dm    = (w[2] >> 7) & 0x3f;
		o.msrc  = (w[2] >> 13) & 7;

		o.t     = w[3] & 7;
		o.m1    = (w[3] >> 3) & 3;
		o.asel  = (w[3] >> 6) & 3;
		o.m2m   = (w[3] >> 8) & 1;
		o.sub   = (w[3] >> 10) & 1;
		o.shift = (w[3] >> 12) & 3;
		o.clamp = (w[3] >> 14) & 3;

		o.sm    = w[4] & 0x3f;
		o.sout  = (w[4] >> 6) & 1;
		o.sr    = (w[4] >> 7) & 0x7f;
		o.mmode = (w[4] >> 14) & 3;
		
		o.alu   = o.mmode || o.asel || o.shift || o.clamp || o.sub;
	}

	void decode_all()
	{
		for (int st = 0; st < STEPS; st++)
			decode(st);
	}

	void reset()
	{
		std::fill(std::begin(r), std::end(r), 0.0);
		std::fill(std::begin(m), std::end(m), 0.0);
		std::fill(std::begin(t), std::end(t), 0.0);
		std::fill(ram.begin(), ram.end(), 0.0);
		acc = x = px = src = psrc = wlatch = idx = 0.0;
		hasx = true;
		haspx = haswl = false;
		ptr = 0;
		n = 0;
		m_skip = 0;
		m_flag = 0;
		for (auto &q : m_rq) q.reg = -1;
		for (auto &q : m_dq) q.reg = -1;
		for (auto &q : m_tq) q.reg = -1;
		m_iqn = 0;
		m_nhist = 0;
		std::memset(in, 0, sizeof(in));
		std::memset(out, 0, sizeof(out));
		std::memset(lfo, 0, sizeof(lfo));
	}

	// Delay RAM offset of a slot as the host set it
	uint32_t slot_offset(int s) const
	{
		return (uint32_t(slot[s & 0x7f][0] & 0x1ff) << 9) | (slot[s & 0x7f][1] & 0x1ff);
	}

	// One pass of the program over the current in[], lfo[]; fills out[].
	void pass()
	{
		m_skip = 0;
		if (!jit_pass())
			for (int st = 0; st < STEPS; st++)
				step(st);
		m_nhist = 0;
		ptr = (ptr - 1) & (RAM_WORDS - 1);
	}

	// The host's trigger, register 0x14: the constant loads of every gated block land at once.
	// The FS1R points the block at its next voice right after the write, so waiting for the
	// pass to come round would clear the wrong registers.
	void trigger()
	{
		for (int st = 0; st < STEPS; st++)
			if (ops[st].gate)
				for (int g = st + 1; g < STEPS && !ops[g].ctl; g++)
					if (ops[g].mmode == 1 && ops[g].dr)
						r[ops[g].dr] = mode(LOADK * s16(coef[g]) * RSCALE[ops[g].shift], ops[g].clamp);
	}

	// The DAC word the unit plays for a cell value: floor to 18 bits of value / 4,
	// clipped at +-8 (s27/s28). Full scale 1.0 is 8.0 here.
	static double dac(double v)
	{
		return std::floor(std::clamp(v / 4.0, -8.0, 8.0) * 16384.0) / 16384.0;
	}

	// Pipeline state
	struct pend { int32_t reg; double v; };
	pend m_rq[4], m_dq[4], m_tq[4];        // writes landing at (n + LAT) & 3
	struct xfer { int32_t st; double v; };
	xfer m_hist[STEPS];                    // this pass's delay RAM transfers
	int32_t m_nhist;
	int32_t m_skip;                        // steps below this one are inside a gated block
	int32_t m_flag;                        // sign of the last compare step's result: -1, 0, 1
	// Delay index loads not yet in effect: the step each lands at and its value. 
	// They are applied when a memory cycle reads the index.
	int32_t m_iqn;
	int64_t m_iq_at[4];
	double m_iq_v[4];

	// The program compiled to native code (yss236_jit.cpp), which pass() runs in place of step() when it
	// can. A decode that changes a step marks it stale and the next pass compiles again; coefficients and
	// slot offsets are read from memory, so writing them does not. interpret, or VOP3_INTERP in the
	// environment, keeps a core on the interpreter, for A/B runs.
	bool interpret = false;
	bool m_stale = true;
	struct jit_code;
	struct jit_free { void operator()(jit_code *) const; };
	std::unique_ptr<jit_code, jit_free> m_jit;
	int64_t m_n0;                          // n when the compiled pass started
	double m_tmp[8];                       // its scratch: helper arguments and saved temporaries
	bool jit_pass();                       // runs the compiled pass; false when there is none
	static const char *const jit_target;   // the architecture compile() emits, or null where it has none
	friend struct yss236_jit;

private:

	static double s16(uint16_t k) { return int16_t(k) / 32768.0; }

	// floored modulo: the result takes the divisor's sign
	static double pymod(double a, double mod)
	{
		double v = std::fmod(a, mod);
		if (v != 0 && ((mod < 0) != (v < 0)))
			v += mod;
		return v;
	}

	// s24: w3[15:14] on the result. 
	//   0 wraps two's-complement, 
	//   1 saturates,
	//   2 clamps negatives to 0,
	//   3 takes |y|; 1-3 saturate at +-128.
	static double mode(double y, int mo)
	{
		if (mo == 0)
		{
			double const a = y + RANGE;
			if (a >= 0 && a < 2 * RANGE)      // fmod of an in-range value is that value: skip the call
				return a - RANGE;
			return pymod(a, 2 * RANGE) - RANGE;
		}
		y = clip2(y, -RANGE, RANGE - 1.0 / 256);
		return mo == 2 ? (0.0 < y ? y : 0.0) : mo == 3 ? std::fabs(y) : y;
	}

	// The running value has its own width: mode 0 wraps at +-256
	static double accmode(double y, int mo)
	{
		if (mo != 0)
			return mode(y, mo);
		double const a = y + ACC;
		if (a >= 0 && a < 2 * ACC)
			return a - ACC;
		return pymod(a, 2 * ACC) - ACC;
	}

	// std::max(lo, std::min(hi, v)) by value: MSVC compiles the std forms, which return references, through the stack
	static double clip2(double v, double lo, double hi) { double const m = v < hi ? v : hi; return lo < m ? m : lo; }
	static double clip(double v, double c) { return clip2(v, -c, c); }

	// s40a/b/d: a w2[5] access, read or write, addresses the RAM at the slot offset without the ring pointer
	// (the AN boards' lookup tables)
	uint32_t addr(int s, int older, bool tbl) const
	{
		if (tbl)
			return uint32_t(int32_t(slot_offset(s)) + older) & (RAM_WORDS - 1);
		return uint32_t(ptr + int32_t(slot_offset(s)) + older) & (RAM_WORDS - 1);
	}

	// s36k/s40c: a capture at step st takes the latest delay RAM read of this pass made
	// 5..11 steps earlier, else keeps its old value. A pass pushes its reads in step order.
	double captured(double old, int st) const
	{
		for (int i = m_nhist; i-- > 0;)
		{
			if (m_hist[i].st <= st - 12)
				break;
			if (m_hist[i].st <= st - CAPMIN)
				return m_hist[i].v;
		}
		return old;
	}

	void push_hist(int st, double v)
	{
		if (m_nhist < STEPS)
			m_hist[m_nhist++] = { st, v };
	}

	void queue_r(int reg, double v) { m_rq[(n + LAT) & 3] = { reg, v }; }
	void queue_d(int cell, double v) { m_dq[(n + DLAT) & 3] = { cell, v }; }
	void queue_t(int reg, double v) { m_tq[(n + LAT) & 3] = { reg, v }; }
	// The index in effect at this step: the loads that are due, in order.
	int cur_idx()
	{
		if (m_iqn)
		{
			int k = 0;
			while (k < m_iqn && m_iq_at[k] <= n)
				idx = m_iq_v[k++];
			for (int j = k; j < m_iqn; j++)
			{
				m_iq_at[j - k] = m_iq_at[j];
				m_iq_v[j - k] = m_iq_v[j];
			}
			m_iqn -= k;
		}
		return int(idx);
	}
	void queue_i(double v)
	{
		cur_idx(); // at most the loads of the last LAT steps are left
		m_iq_at[m_iqn] = n + LAT;
		m_iq_v[m_iqn++] = v;
	}

	// The t file write of a step whose result is y
	void t_write(op const &o, int st, double y)
	{
		// t as a 1.15 multiplier of the register value, or the fraction below one delay sample
		// (y * 256 is the delay in samples and the index its floor)
		double const w = y * 256.0;
		double const v = o.tfrac ? (w - std::floor(w)) : (w < -32768.0 ? -32768.0 : 32767.0 < w ? 32767.0 : w) / 32768.0;
		if (o.tw & 2)
			queue_t(o.tn, v);
		else if (o.tw & 1)
			queue_t(o.tn, s16(coef[st]));
	}

	void step(int st)
	{
		n++;
		// writes issued LAT steps ago land now
		{
			pend &q = m_rq[n & 3];
			if (q.reg > 0)
				r[q.reg] = q.v;
			q.reg = -1;
			pend &d = m_dq[n & 3];
			if (d.reg >= 0)
				m[d.reg] = d.v;
			d.reg = -1;
			pend &tq = m_tq[n & 3];
			if (tq.reg >= 0)
				t[tq.reg] = tq.v;
			tq.reg = -1;
		}

		op const &o = ops[st];
		if (st < m_skip)
			return;                           // inside a gated block
		if (o.ctlop)
		{
			// a gate waits for the host trigger; 
			// a conditional skip runs the steps up to the target only when its condition holds. 
			// Neither computes anything itself.
			bool const run = !o.gate && (o.cskip == 6 ? m_flag > 0 : o.cskip == 4 && m_flag < 0);
			if (!run)
				m_skip = (st & 0x100) | o.target;
			return;
		}
		int const cls = o.mmode;
		int const route = o.shift;
		int const mo = o.clamp;
		int const page = (st >> 5) & 15;   // I/O slot: page * 2 + half
		int const s = st >> 2;
		bool const xfer_phase = st >= XFER_FROM;   // s40f: every phase transfers; s40a/b/d: steps 000..002 never

		// delay RAM read for this slot
		if (xfer_phase && (o.memop == 2 || o.memop == 3 || o.memop == 4 || o.memop == 6))
		{
			int const older = (o.memop & 4) ? 1 : 0;
			uint32_t const a = addr(s, older + (o.idxa ? cur_idx() : 0), o.tbl);
			push_hist(st, ram[a]);
		}

		// m cell moves from the hardware side (serial input, LFO, delay RAM data)
		if (o.dm)
		{
			int const c = o.dm;
			switch (o.msrc)
			{
				case M_SERIAL:  case M_SERIAL2: case M_EG: queue_d(c, in[o.msrc][page]);    break;
				case M_LFO:                                queue_d(c, lfo[st >> 5]);        break;
				case M_CAPTURE: case M_CAPTURE2:           queue_d(c, captured(m[c], st)); break;
				default:                                   break;   // M_RESULT and M_PREVIOUS are written below from the result
			}
		}

		if (o.sout)
			out[page] = m[o.sm];

		if (cls != 2 && cls != 3)
		{
			// s27 r12: every step fetches x = r[rB] for the z^-1 latch
			px = x; haspx = hasx; hasx = o.sr != 0; x = o.sr ? r[o.sr] : 0.0;
		}

		if (cls == 1)
		{
			// a class-1 step fetches its m operand like any other: every LFO consumer is a
			// "p = c + m0c" step, which otherwise adds the last class-2/3 step's cell
			psrc = src; src = m[o.sm];
			// s24/s27a: r[rA] = k/256 + s, route-scaled; s takes it
			double const a = adder(o);
			double v = LOADK * s16(coef[st]) + (o.sub ? -a : a);
			v *= RSCALE[route];
			if (o.dr)
				queue_r(o.dr, mode(v, mo));
			acc = accmode(v, mo);
			if (o.cmp)
				m_flag = (acc > 0) - (acc < 0);
			// s29: class 1 with w1[12:8] set writes its m cell; so does one without an r register
			if (o.msrc == M_RESULT && o.dm && (o.dwr || !o.dr))
				queue_d(o.dm, clip(acc, DSAT));
			t_write(o, st, acc);
			return;
		}

		if (cls == 0)
		{
			// s34: a class-0 write step writes the latched data, otherwise nothing
			if (xfer_phase && o.memop == 1 && haswl)
			{
				ram[addr(s, o.idxa ? cur_idx() : 0, o.tbl)] = wlatch;
				haswl = false;
			}
			if (o.alu)
				alu_only(o);
			return;
		}

		// classes 2 and 3
		double const k = s16(coef[st]);
		bool const rden = o.m1 >= 2;
		double const g = cls == 3 ? 1.0 : rden ? t[((o.m1 & 1) << 3) | o.t] : k;   // s19 class 3 = unity; s18 G[rsrc]
		double const xv = o.sr ? r[o.sr] : 0.0;
		px = x; haspx = hasx; x = xv; hasx = o.sr != 0;
		psrc = src; src = m[o.sm];   // s29: the m operand of ops 4, 5, 7
		double const m2 = o.m2m ? src : xv;
		double const prod = cls == 3 ? m2 : g * m2;
		double const a = adder(o);
		double y = (o.sub ? prod - a : prod + a) * RSCALE[route];   // s12: sel negates the running value
		if (o.dr && o.rcopy)                  // s27: rA | 0x80 stores the previous step's x (z^-1)
			queue_r(o.dr, haspx ? px : xv);
		else if (o.dr)
			queue_r(o.dr, mode(y, mo));
		y = acc = accmode(y, mo);
		if (o.cmp)
			m_flag = (y > 0) - (y < 0);
		double outv = clip(y, SAT);
		if (o.dm)
		{
			int const c = o.dm;
			if (o.msrc == M_PREVIOUS)         // s32/s33: the previous step's m operand
				queue_d(c, psrc);
			else if (o.msrc == M_RESULT && (!(o.m2m && o.asel == A_NONE) || o.dm != o.sm))   // s30: op 7 does not write its own source
				queue_d(c, clip(y, DSAT));
		}
		t_write(o, st, y);
		if (o.idxw)
			queue_i(std::floor(y * 256.0));
		// s34: w1[13] makes this step's output the data of the next delay RAM
		// write; a writer carrying it writes its own.
		if (o.wdata)
		{
			if (xfer_phase && o.memop == 1)
				haswl = false;
			else
			{
				wlatch = outv;
				haswl = true;
			}
		}
		if (xfer_phase && o.memop == 1)
		{
			if (haswl)
			{
				outv = wlatch;
				haswl = false;
			}
			ram[addr(s, o.idxa ? cur_idx() : 0, o.tbl)] = outv;
		}
	}

	// The adder input of a step (w3[7:6]); the operand fetch happened already
	double adder(op const &o) const
	{
		switch (o.asel)
		{
			case A_P: return acc;
			case A_R: return o.sr ? x : acc / 32768.0;
			case A_M: return o.sm ? src : acc / 32768.0;
			default:  return 0.0;
		}
	}

	// A class-0 step with adder, shift, clamp or sub bits: the ALU runs without a product.
	void alu_only(op const &o)
	{
		psrc = src; src = m[o.sm];
		double y = o.sub ? -adder(o) : adder(o);
		y *= RSCALE[o.shift];
		if (o.dr && !o.rcopy)
			queue_r(o.dr, mode(y, o.clamp));
		acc = accmode(y, o.clamp);
		if (o.cmp)
			m_flag = (acc > 0) - (acc < 0);
	}
};
