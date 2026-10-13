#pragma once
#include <cstdint>

namespace imp
{

// Shimmer reverb: a plate reverb whose tail is fed back through an
// octave-up pitch shifter, so every pass round the loop adds a layer an
// octave higher -- the rising, glassy halo "shimmer" is known for.
//
// The plate is the Dattorro figure-of-eight tank (the same design as the
// Reverb+ card), with delay lengths cut to ~60% so it fits in RAM beside
// the 0.75-second buffer. All maths is integer; the audio is scaled up by 8
// inside the reverb for extra headroom and precision.
//
// Timing -- this runs inside the audio interrupt, so it's built to be
// cheap and, just as important, the same cost on every sample:
//
//  * It runs at half the sample rate (24kHz), split in two: even samples
//    take in the input and run tank half A, odd samples run half B and the
//    output taps. That's ComputerCard's advice for work too long for one
//    sample -- spread it across calls -- and it halves the reverb's cost
//    per sample. The tail loses nothing audible: its bandwidth tops out at
//    12kHz, and the tank's damping filters roll it off well below that.
//    Delay lengths are halved to match, so reverb times are unchanged.
//
//  * All thirteen delay lines share ONE circular buffer with a single
//    write position (the trick Mutable Instruments' effects use), so every
//    access is just (position + constant) & mask.
//
// The first version -- full rate, separate lines -- cost ~1000 cycles per
// sample, a third of the whole budget, and was a large part of why the
// audio broke up on hardware.
//
// Safety: the shimmer feeds energy back into the tank. Decay is capped at
// 0.875 and the shimmer return at 0.2 (see kShimmerGain), the return is
// soft-clipped, and every write is saturated, so the worst case is loud,
// never a wrapped-around crackle.
class Shimmer
{
public:
	// Tank decay, Q15 (0.25..0.875 from the X knob; see Control.h). Longer
	// decays also get more tank modulation, so the tail animates as the knob
	// comes up rather than hanging still.
	void SetDecay(int32_t decayQ15)
	{
		decay_ = decayQ15;
		int32_t d = ((decayQ15 - 8192) * kMaxMod) / 20480;
		modDepth_ = d < 0 ? 0 : (d > kMaxMod ? kMaxMod : d);
	}

	// In and out are 12-bit scale. Kept out of line on purpose: inside its
	// own function the reverb's state sits a few bytes from `this`, so the
	// Cortex-M0+ reaches it with single short loads instead of spilling
	// registers across the whole audio interrupt.
	__attribute__((noinline)) void Process(int32_t inL, int32_t inR, int32_t &outL, int32_t &outR)
	{
		int32_t mono = inL + inR;
		if (!phase_)
		{
			// Even sample: average this and the previous input (a simple
			// anti-alias filter for the drop to 24kHz), then the input
			// diffusion and tank half A.
			w_ = (w_ - 1) & kMask;
			int32_t x = (mono + lastMono_) * 2; // mono average x 8
			// Q14 here, not Q15: when overlapping voices sum past full scale,
			// (x - bandwidth_) can reach ~80,000, and x Q15 would overflow.
			bandwidth_ += ((x - bandwidth_) * (kBandwidth >> 1)) >> 14;

			// Input diffusion: two short allpasses smear the attack into a
			// wash before it reaches the tank. (Dattorro uses four; the
			// tank's own diffusion does the rest, and dropping two buys back
			// audio-interrupt time for the shimmer.)
			x = Allpass<kIn1, 71>(bandwidth_, 24576);
			x = Allpass<kIn2, 189>(x, 20480);

			aOut_ = Read<kADel2>(1121);
			bOut_ = Read<kBDel2>(953);

			// Shimmer: the tank's own output, an octave up, back into the tank.
			//
			// The return is high-passed (~15Hz) before it goes back in. Low
			// frequency is what made the loop run away when this was first
			// built -- the shifter passes DC straight through, so any offset
			// recirculates and grows. Blocking it is what allows a shimmer
			// return loud enough to actually hear. The filter state is kept
			// with 8 extra bits: at the signal's own resolution it would
			// stick a few units from zero and feed the tank a steady offset.
			Write<kShift>((aOut_ + bOut_) >> 1);
			int32_t sh = OctaveUp();
			shHp_ += ((sh * 256) - shHp_) >> 8;
			sh -= shHp_ >> 8;
			x += SoftClip((sh * kShimmerGain) >> 15);
			x_ = x;

			// Tank half A, fed by half B's output (and vice versa on the odd
			// sample): the figure of eight that gives the plate its long,
			// dense tail.
			if (++modCount_ >= kModStep)
			{
				modCount_ = 0;
				modVal_ += modDir_;
				if (modVal_ >= modDepth_)
					modDir_ = -1;
				else if (modVal_ <= 0)
					modDir_ = 1;
			}

			int32_t t = AllpassMod<kAAp1, 271>(x + Decay(bOut_, decay_), -22938, modVal_);
			int32_t d = Read<kADel1>(1343);
			WriteTank<kADel1>(t);
			dampA_ += ((d - dampA_) * kDamping) >> 15;
			WriteTank<kADel2>(AllpassTank<kAAp2, 543>(Decay(dampA_, decay_), 16384));

			// The left channel's output taps are read here rather than on
			// the odd sample, to even out the work between the two samples.
			// (They read the tank as the odd sample left it, one tick back --
			// inaudible in a reverb tail.)
			tapL_ = Read<kBDel1>(81) + Read<kBDel1>(893) - Read<kBAp2>(575) + Read<kBDel2>(600) - Read<kADel1>(598) - Read<kAAp2>(57) - Read<kADel2>(321);

			// Output: halfway between the last two reverb frames.
			outL = (prevWetL_ + wetL_) >> 1;
			outR = (prevWetR_ + wetR_) >> 1;
		}
		else
		{
			// Odd sample: tank half B, then the output taps.
			// The other half sweeps the opposite way, so the two don't move
			// together.
			int32_t t = AllpassMod<kBAp1, 367>(x_ + Decay(aOut_, decay_), -22938, modDepth_ - modVal_);
			int32_t d = Read<kBDel1>(1269);
			WriteTank<kBDel1>(t);
			dampB_ += ((d - dampB_) * kDamping) >> 15;
			WriteTank<kBDel2>(AllpassTank<kBAp2, 801>(Decay(dampB_, decay_), 16384));

			// Stereo out: Dattorro's output taps, each channel mixing points
			// from both halves of the tank so left and right are related but
			// distinct.
			int32_t l = tapL_;
			int32_t r = Read<kADel1>(106) + Read<kADel1>(1088) - Read<kAAp2>(368) + Read<kADel2>(802) - Read<kBDel1>(633) - Read<kBAp2>(100) - Read<kBDel2>(36);

			// The output runs one reverb frame behind, so it can step
			// through the frames in order: previous frame now, the midpoint
			// on the next (even) sample, this frame after that -- straight-
			// line interpolation from 24kHz back up to 48kHz.
			prevWetL_ = wetL_;
			prevWetR_ = wetR_;
			// x0.375 and back down from the x8 internal scale: 3/64. Leaves
			// headroom for the tail to build on sustained tones. (A small
			// multiply: seven taps can sum past 200,000.)
			wetL_ = (l * 3) >> 6;
			wetR_ = (r * 3) >> 6;
			outL = prevWetL_;
			outR = prevWetR_;
		}
		lastMono_ = mono;
		phase_ ^= 1;
	}

private:
	static constexpr int32_t kSize = 16384;
	static constexpr int32_t kMask = kSize - 1;

	// Crossfade window for the octave shifter, in 24kHz ticks.
	//
	// A two-tap shifter recycles each head once per window, and that recycle
	// is heard as ripple at (tick rate / window): 23Hz with the 1024 this
	// card first used -- a buzz, and the reason the shimmer sounded coarse
	// rather than glassy. 4096 puts it at 5.9Hz. The cost is smearing
	// (a 171ms window), which a reverb tail hides.
	//
	// The Barber's Pole card's octave.h reaches the same conclusion from
	// measurements: "the window has to be long enough that the taps are
	// still meaningfully separated", and a short one leaves the wanted
	// octave no longer dominant.
	static constexpr int32_t kShiftWindow = 4096;

#ifndef IMP_SHIMMER_GAIN
#define IMP_SHIMMER_GAIN 16384 // 0.5
#endif
	static constexpr int32_t kShimmerGain = IMP_SHIMMER_GAIN;

	// Tank modulation (after Dattorro's excursion, as Reverb+ does it): the
	// first allpass of each tank half has its read tap swept a few samples
	// back and forth, which keeps a long tail moving instead of sitting
	// still. Depth follows the decay knob, so the animation arrives with the
	// longer settings. One step every kModStep ticks, so at full depth the
	// sweep takes about 1.7s each way.
	// 14 ticks. Measured on a steady tone, the wet level then breathes by
	// +/-0.4% at the bottom of the decay knob, 1.7% a third up, 4.1% at two
	// thirds and 6.4% at the top: present, not a wobble. 16 and above jump
	// to 17-38% at the top, which is chorus, not animation.
	static constexpr int32_t kMaxMod = 14; // ticks of sweep at full decay
	static constexpr int32_t kModStep = 1024; // ticks between one-sample steps

	// Where each delay line starts in the shared buffer. Each line of
	// length L needs L+1 slots (written at +0, read up to +L). Lengths are
	// in 24kHz ticks.
	static constexpr int32_t kIn1 = 0;
	static constexpr int32_t kIn2 = kIn1 + 71 + 1;
	static constexpr int32_t kAAp1 = kIn2 + 189 + 1;
	// kAAp1/kBAp1 carry kMaxMod extra slots: their read tap is modulated.
	static constexpr int32_t kADel1 = kAAp1 + 271 + 1 + kMaxMod;
	static constexpr int32_t kAAp2 = kADel1 + 1343 + 1;
	static constexpr int32_t kADel2 = kAAp2 + 543 + 1;
	static constexpr int32_t kBAp1 = kADel2 + 1121 + 1;
	static constexpr int32_t kBDel1 = kBAp1 + 367 + 1 + kMaxMod;
	static constexpr int32_t kBAp2 = kBDel1 + 1269 + 1;
	static constexpr int32_t kBDel2 = kBAp2 + 801 + 1;
	static constexpr int32_t kShift = kBDel2 + 953 + 1;
	static constexpr int32_t kEnd = kShift + kShiftWindow + 1;
	static_assert(kEnd <= kSize, "delay lines don't fit the shared buffer");

	// 0.2. Measured on the host (test/host_tests.cpp): with decay at its
	// 0.875 cap, 0.2 dies away to true silence for noise, low and high
	// sines and full-scale squares; 0.3 never dies. Each half of the tank
	// recirculates at decay^2 and the shimmer adds gain x decay on top, so
	// this is the margin that keeps the whole loop below 1.
	// One-pole filter coefficients, converted from the 48kHz design (0.7
	// and 0.55) so the cutoffs stay put at 24kHz: 1 - (1 - a)^2.
	static constexpr int32_t kBandwidth = 29818; // input low-pass
	static constexpr int32_t kDamping = 26132;	 // tank low-pass

	// The sample written `delay` ticks ago into the line at `base`.
	template <int32_t base>
	int32_t Read(int32_t delay) const { return buf_[(w_ + base + delay) & kMask]; }

	// Writes below a unit are flushed to zero. Integer rounding round the
	// tank can otherwise leave a single unit circulating forever -- measured
	// as a -1 DC offset still present a minute after the input stopped.
	// One unit here is an eighth of an output LSB, so nothing audible is
	// lost, and the tail now reaches true silence at every decay setting.
	template <int32_t base>
	void Write(int32_t v) { buf_[(w_ + base) & kMask] = (int16_t)Sat16(v); }

	// Writing into the tank flushes anything below a unit to zero. Integer
	// rounding round the loop can otherwise leave a single unit circulating
	// forever -- measured as a -1 DC offset still present a minute after the
	// input stopped. One unit is an eighth of an output LSB, so nothing
	// audible is lost, and the tail reaches true silence at every setting.
	// Only the lines inside the feedback loop need it.
	template <int32_t base>
	void WriteTank(int32_t v)
	{
		// Branchless |v| < 2 -> 0: mask is 0 when v is -1, 0 or 1.
		int32_t mask = -(int32_t)((uint32_t)(v + 1) > 2u);
		buf_[(w_ + base) & kMask] = (int16_t)(Sat16(v) & mask);
	}

	static int32_t Sat16(int32_t v) { return v > 32767 ? 32767 : (v < -32768 ? -32768 : v); }

	// Multiply by a Q15 gain below 1, rounding TOWARDS ZERO.
	//
	// A plain >> 15 rounds towards negative infinity, so -1 x anything
	// still floors to -1: a single unit circulates round the tank forever
	// and the tail stops at about -52dB instead of silence. Rounding
	// towards zero makes every pass strictly smaller, so it always lands on
	// zero. (Measured: with >> 15 the tail never died at some decay
	// settings; with this it reaches true silence at all of them.)
	static int32_t Decay(int32_t v, int32_t g)
	{
		// Branchless round-towards-zero: add 32767 first when negative.
		int32_t r = v * g;
		return (r + ((r >> 31) & 32767)) >> 15;
	}

	// Schroeder allpass: passes every frequency at the same level but
	// smears it in time -- the building block of reverb diffusion.
	template <int32_t base, int32_t len>
	int32_t Allpass(int32_t in, int32_t g)
	{
		int32_t d = Read<base>(len);
		int32_t v = Sat16(in - ((d * g) >> 15));
		Write<base>(v);
		return d + ((v * g) >> 15);
	}

	// The same, for a line inside the tank's feedback loop.
	template <int32_t base, int32_t len>
	int32_t AllpassTank(int32_t in, int32_t g)
	{
		int32_t d = Read<base>(len);
		int32_t v = Sat16(in - ((d * g) >> 15));
		WriteTank<base>(v);
		return d + ((v * g) >> 15);
	}

	// The same, with its read tap swept by `mod` ticks (0..kMaxMod).
	template <int32_t base, int32_t len>
	int32_t AllpassMod(int32_t in, int32_t g, int32_t mod)
	{
		int32_t d = buf_[(w_ + base + len + mod) & kMask];
		int32_t v = Sat16(in - ((d * g) >> 15));
		WriteTank<base>(v);
		return d + ((v * g) >> 15);
	}

	// Octave-up pitch shift. Two read heads sweep through a short delay
	// line at twice the speed it's written, which doubles the pitch. Each
	// head has to jump back when it catches up with the writer; the heads
	// are half a window apart, and each fades to silence (triangle window)
	// exactly when it jumps, so the jumps are never heard.
	int32_t OctaveUp()
	{
		shiftPhase_ = (shiftPhase_ + 1) & (kShiftWindow - 1);
		int32_t p1 = shiftPhase_;
		int32_t p2 = (shiftPhase_ + kShiftWindow / 2) & (kShiftWindow - 1);
		// Delay shrinks by one each tick, so the head reads two samples
		// further on every tick: double speed.
		int32_t r1 = Read<kShift>(kShiftWindow - p1);
		int32_t r2 = Read<kShift>(kShiftWindow - p2);
		// Triangle ramp, then smoothstep (t*t*(3-2t)) to round its corners:
		// a raised-cosine-shaped crossfade whose slope is continuous, so the
		// recycle doesn't leave a kink in the signal. The pair still sums to
		// 1 everywhere, because w2 is taken as the complement.
		int32_t tri = p1 < kShiftWindow / 2 ? p1 : kShiftWindow - p1; // 0..W/2
		int32_t t = (tri << 12) / (kShiftWindow / 2);				 // Q12 0..4096
		int32_t w1 = (((t * t) >> 12) * (12288 - 2 * t)) >> 13;		 // Q12 smoothstep
		int32_t w2 = 4096 - w1;
		return (r1 * w1 + r2 * w2) >> 12;
	}

	// Gentle limiter for the shimmer return: unchanged below 8192, then
	// squashed 4:1.
	static int32_t SoftClip(int32_t x)
	{
		int32_t a = x < 0 ? -x : x;
		if (a > 8192)
			a = 8192 + ((a - 8192) >> 2);
		return x < 0 ? -a : a;
	}

	// Small, hot state first (short offsets from `this`), the buffer last.
	int32_t w_ = 0;
	int32_t phase_ = 0;
	int32_t decay_ = 8192;
	int32_t lastMono_ = 0;
	int32_t bandwidth_ = 0;
	int32_t dampA_ = 0, dampB_ = 0;
	int32_t x_ = 0, aOut_ = 0, bOut_ = 0; // carried from the even to the odd sample
	int32_t tapL_ = 0;					  // left output taps, read on the even sample
	int32_t wetL_ = 0, wetR_ = 0, prevWetL_ = 0, prevWetR_ = 0;
	int32_t shiftPhase_ = 0;
	int32_t shHp_ = 0;					   // Q8 state of the shimmer return's DC blocker
	int32_t modDepth_ = 0, modVal_ = 0;	   // tank sweep, in ticks
	int32_t modDir_ = 1, modCount_ = 0;
	int16_t buf_[kSize] = {};
};

} // namespace imp
