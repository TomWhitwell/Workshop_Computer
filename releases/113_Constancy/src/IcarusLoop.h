#pragma once
#include <cstdint>
#include "Config.h"
#include "Dsp.h"
#include "Params.h"
#include "Tables.h"

namespace constancy
{

// A Schroeder allpass: passes every frequency at the same level, but
// smears each one in time a little differently. A few in a row inside a
// feedback loop turn each repeat into a softer, more diffuse wash -- here,
// standing in for the hall reverb a real CS-80 record would have. Works at
// the feedback path's half scale.
template <int N>
struct Allpass
{
	int32_t index = 0;
	int16_t buffer[N] = {};

	__attribute__((always_inline)) int32_t Process(int32_t x)
	{
		int32_t delayed = buffer[index];
		int32_t w = Clamp16(x + ((kAllpassGainQ15 * delayed) >> 15));
		buffer[index] = (int16_t)w;
		if (++index == N)
			index = 0;
		return delayed - ((kAllpassGainQ15 * w) >> 15);
	}
};

// A Moog-style ladder: four one-pole lowpass stages in a row (24dB per
// octave), with some of the output fed back to the input for resonance.
// Its input has just been through the soft clipper, which does the
// ladder's overdrive (a tanh stage here as well measured too costly for
// what it added); a clamp keeps the resonance from overflowing it. The
// output is lifted to make up for the level the resonance takes away.
struct Ladder
{
	int32_t s[4] = {};

	__attribute__((always_inline)) int32_t Process(int32_t x, int32_t coef, int32_t resQ12)
	{
		int32_t u = Clamp16(x - ((s[3] * resQ12) >> 12));
		OnePole(s[0], u, coef);
		OnePole(s[1], s[0], coef);
		OnePole(s[2], s[1], coef);
		OnePole(s[3], s[2], coef);
		return (s[3] * (4096 + resQ12)) >> 12;
	}
};

// One side of the loop: everything from reading the delay line to the
// value that's fed back, for one channel. Template arguments are its
// three allpass lengths, which differ between the sides.
template <int A, int B, int C>
struct LoopSide
{
	Allpass<A> ap0;
	Allpass<B> ap1;
	Allpass<C> ap2;
	DcBlocker<7> dc; // ~30Hz at the loop's 24kHz
	Ladder ladder;
	int32_t poleA = 0, poleB = 0;
	int32_t inPrev = 0;
	int32_t out = 0, outPrev = 0;

	// `delayed` is what the delay line gave back (half scale), `in` the new
	// sound, `fb` and `destruct` the slid feedback and dropout levels.
	// Returns the value to feed back (half scale, before the rotation).
	__attribute__((always_inline)) int32_t Process(int32_t delayed, int32_t in, const EngineParams &p, int32_t fb, int32_t destruct)
	{
		int32_t x = dc.Process(ap2.Process(ap1.Process(ap0.Process(delayed))));
		// Back to full scale (x 2), plus the input, driven and soft-clipped.
		x = Shape(table::softClip, ((x * 2 + in) * p.driveQ12) >> 12);
		x = ladder.Process(x, p.ladderCoef, p.ladderResQ12);
		x = (x * destruct) >> 15;
		outPrev = out;
		out = x;
		// x feedback, at half scale, rounding towards zero so echoes can
		// die all the way to silence.
		x = Clamp16(ScaleTowardZero(x, fb) >> 1);
		// OnePole(0.4) at 48kHz, which is OnePole(0.16) at the loop's 24kHz:
		// a gentle lowpass.
		x = OnePole(poleA, x, kLoopPoleA_Q15);
		// OnePole(-0.08): y = 0.92x - 0.08y', written as x - 0.08(x + y') so
		// nothing overflows. A slight tilt towards the highs.
		poleB = x - ((kLoopPoleB_Y_Q15 * (x + poleB)) >> 15);
		return poleB;
	}
};

// The Icarus loop, on core 0: a fixed-point port of the feedback loop in
// Infinite Digits' Engine_Icarus.sc (MIT licence), in stereo. For each side:
//
//   read the delay line (Icarus's DelayC, its time lagged by core 1)
//   -> three allpasses (Constancy's addition: diffusion)
//   -> take out DC (LeakDC)
//   -> add the new sound, drive it (x 1.25 at the Sun knob's centre) and
//      soft-clip it (softclip)
//   -> the ladder (MoogLadder), its cutoff following the sun
//   -> the dropouts (Icarus's "destruction": the loop ducks to half for a
//      moment, at random, when the delay is short)
//   = the loop's output, which is also fed back:
//   x feedback -> OnePole(0.4) -> OnePole(-0.08) -> Rotate2(0.2), a
//      36-degree turn of the stereo image every lap -> written back into
//      the delay line.
//
// Feedback can go above 1: the clipper and the ladder hold the loop at a
// saturated, burning sustain rather than letting it run away. The feedback
// path works at half scale (16384 = full) so there's room for that inside
// 16-bit samples.
//
// Half rate: the loop runs at 24kHz, the left side on even samples and the
// right on odd ones, so each sample does half the work (the whole loop at
// full rate didn't leave enough time for the voices) and the delay line
// needs half the memory. Each side's input is the average of two samples
// (a gentle lowpass so the voices' highs don't fold back down), and its
// output is blended between its last two values. The loop loses its top
// octave above ~11kHz -- a little grit, mostly hidden by the ladder.
class IcarusLoop
{
public:
	// `fb` and `destruct` are the feedback (Q12) and dropout level (Q15),
	// already slid smoothly by the caller.
	__attribute__((noinline)) void Process(int32_t inL, int32_t inR, const EngineParams &p, int32_t fb, int32_t destruct,
										   int32_t &outL, int32_t &outR)
	{
		// The read point glides towards where core 1 asks for it.
		delay_ += (p.delayQ8 - delay_) >> 6;
		inL = Clamp(inL);
		inR = Clamp(inR);

		if (!odd_)
		{
			// The left side.
			fbL_ = left_.Process(Read(bufL_), (inL + left_.inPrev) >> 1, p, fb, destruct);
			outL = (left_.out + left_.outPrev) >> 1;
			outR = right_.out;
		}
		else
		{
			// The right side, then both sides' feedback, turned by 36
			// degrees (Rotate2), into the delay line.
			int32_t fbR = right_.Process(Read(bufR_), (inR + right_.inPrev) >> 1, p, fb, destruct);
			int32_t wl = (kRotateCosQ15 * fbL_ + kRotateSinQ15 * fbR) >> 15;
			int32_t wr = (kRotateCosQ15 * fbR - kRotateSinQ15 * fbL_) >> 15;
			bufL_[write_] = (int16_t)Clamp16(wl);
			bufR_[write_] = (int16_t)Clamp16(wr);
			if (write_ == 0)
			{
				// The copy past the end, for interpolation.
				bufL_[kDelaySize] = bufL_[0];
				bufR_[kDelaySize] = bufR_[0];
			}
			if (++write_ == kDelaySize)
				write_ = 0;
			outL = left_.out;
			outR = (right_.out + right_.outPrev) >> 1;
		}
		left_.inPrev = inL;
		right_.inPrev = inR;
		odd_ = !odd_;
	}

private:
	// The delay line, `delay_` behind the write point, blending between
	// the two nearest stored samples.
	__attribute__((always_inline)) int32_t Read(const int16_t *buf) const
	{
		int32_t pos = (write_ << 8) - delay_;
		if (pos < 0)
			pos += kDelaySize << 8;
		int32_t i = pos >> 8, f = pos & 255;
		return buf[i] + (((buf[i + 1] - buf[i]) * f) >> 8);
	}

	// The loop's input, held within 4x full scale so nothing overflows.
	static int32_t Clamp(int32_t x) { return x > 131071 ? 131071 : (x < -131072 ? -131072 : x); }

	// Per-sample state first (short reach on the M0+), buffers last.
	int32_t write_ = 0;
	int32_t delay_ = (kDelaySunriseMs * 24) << 8;
	bool odd_ = false;
	int32_t fbL_ = 0;
	LoopSide<kAllpassLeft[0], kAllpassLeft[1], kAllpassLeft[2]> left_;
	LoopSide<kAllpassRight[0], kAllpassRight[1], kAllpassRight[2]> right_;
	int16_t bufL_[kDelaySize + 1] = {};
	int16_t bufR_[kDelaySize + 1] = {};
};

} // namespace constancy
