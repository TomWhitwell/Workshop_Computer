// Delay-line based modes: a shared fixed-point delay line with fractional
// (Hermite) reads, used by two zones:
//
//   EchoDelay     - classic delay: timbre = time, zone fraction = feedback.
//   StereoDoppler - binaural 2D position panner (after parasites' doppler):
//                   timbre = x position (left..right), y control = depth
//                   (0..2 units in front of the head), zone fraction = room
//                   size, a continuous sweep through the original's four
//                   button presets (tiny room .. huge hall: longer distance
//                   delays and steeper attenuation). Produces a true stereo
//                   pair (main = left, aux = right) via inter-aural time
//                   difference, distance attenuation, and a distance delay
//                   whose slow slewing produces the doppler pitch bends.
//
// The buffers make these objects large; they must live in static storage.

#ifndef ALLOY_DSP_DELAY_H_
#define ALLOY_DSP_DELAY_H_

#include <cstdint>

#include "dsp/xmod_algorithms.h"

namespace xmod {

// Power-of-two circular buffer of Q12 samples with Q8 fractional reads.
template <int32_t kSizeLog2>
class DelayLine
{
public:
	static constexpr int32_t kSize = 1 << kSizeLog2;
	static constexpr int32_t kMask = kSize - 1;

	void Write(int32_t sample)
	{
		buffer_[write_] = static_cast<int16_t>(Clip(sample));
		write_ = (write_ + 1) & kMask;
	}

	// delay_q8: delay behind the write head, in Q8 samples. 4-point Hermite
	// interpolation for clean pitch bends while the delay slews.
	int32_t ReadHermite(int32_t delay_q8) const
	{
		int32_t delay_int = delay_q8 >> 8;
		int32_t t = delay_q8 & 0xFF; // Q8 fraction

		int32_t i1 = (write_ - 1 - delay_int) & kMask;
		int32_t xm1 = buffer_[(i1 + 1) & kMask]; // one sample newer
		int32_t x0 = buffer_[i1];
		int32_t x1 = buffer_[(i1 - 1) & kMask];
		int32_t x2 = buffer_[(i1 - 2) & kMask];

		// Catmull-Rom in Q8 steps (values Q12, well within 32 bits).
		int32_t c = (x1 - xm1) >> 1;
		int32_t v = x0 - x1;
		int32_t w = c + v;
		int32_t a = w + v + ((x2 - x0) >> 1);
		int32_t b_neg = w + a;
		int32_t y = ((((((a * t) >> 8) - b_neg) * t) >> 8) + c);
		y = ((y * t) >> 8) + x0;
		return y;
	}

private:
	int16_t buffer_[kSize] = {};
	int32_t write_ = 0;
};

// Classic delay. timbre = delay time (~1ms..340ms), fraction = feedback
// (0..~95%), 50/50 dry/wet mix.
class EchoDelay
{
public:
	int32_t Process(int32_t carrier, int32_t modulator,
	                int32_t time, int32_t feedback)
	{
		int32_t dry = Clip((carrier + modulator) >> 1);

		// Target delay in Q8 samples: 48..~16350. 1019 ~= range/4095 in Q8.
		int32_t target_q8 = (48 << 8) + time * 1019;
		// Light smoothing to avoid zipper noise (fast enough to feel
		// immediate, unlike the doppler's deliberate slow slew).
		delay_q8_ += (target_q8 - delay_q8_) >> 6;

		int32_t wet = line_.ReadHermite(delay_q8_);

		// Feedback 0..~0.95 in Q12.
		int32_t fb = (feedback * 3891) >> 12;
		line_.Write(dry + ((wet * fb) >> 12));

		return Clip((dry + wet) >> 1);
	}

private:
	DelayLine<14> line_; // 16384 samples, ~0.34s
	int32_t delay_q8_ = 48 << 8;
};

// Binaural 2D doppler panner, following the parasites doppler geometry:
// the source sits at (x, y) relative to the listener's head, from which we
// derive a distance (delay + attenuation) and an angle (inter-aural time
// difference + equal-power pan). The room control sweeps continuously
// through the original's four button presets, interpolating both the room
// size (distance-delay scale) and the attenuation factor between anchors -
// like the continuous Chebyschev order sweep.
//
//   pos_x: 0..4095 -> x in -1..1 (left..right)
//   pos_y: 0..4095 -> y in 0..2 (at the head..far in front)
//   room:  0..4095 -> tiny room .. huge hall
//
// Main output = left ear, aux output = right ear.
class StereoDoppler
{
public:
	// Returns the left channel; right is available via aux() afterwards.
	int32_t Process(int32_t carrier, int32_t modulator,
	                int32_t pos_x, int32_t pos_y, int32_t room)
	{
		int32_t dry = Clip((carrier + modulator) >> 1);
		line_.Write(dry);

		// Base position, Q11 units: x in -2048..2047, y in 0..4095. Small
		// x offset avoids the discontinuity at the origin (the original's
		// +0.05).
		int32_t x = pos_x - 2048 + 102;
		int32_t y = pos_y;
		if (x > 2047) x = 2047;

		// Continuous room sweep through the original's four presets:
		// room size in delay samples and attenuation factor (Q11),
		// piecewise-linear between anchors.
		static constexpr int32_t kRoomSize[4] = {100, 1638, 3276, 8192};
		static constexpr int32_t kAttenQ11[4] = {1024, 8192, 16384, 30720};
		int32_t seg = (room * 3) >> 12;             // 0..2
		int32_t t = (room * 3) & 4095;              // fraction within segment
		int32_t room_samples = kRoomSize[seg] +
			(((kRoomSize[seg + 1] - kRoomSize[seg]) * t) >> 12);
		int32_t atten_k_q11 = kAttenQ11[seg] +
			(((kAttenQ11[seg + 1] - kAttenQ11[seg]) * t) >> 12);

		// Polar coordinates: distance (0..~1 Q11 after normalization by the
		// maximum sqrt(1^2+2^2)=sqrt(5)) and pan = x/d in -2048..2047.
		int32_t d_raw = SqrtU32(static_cast<uint32_t>(x * x + y * y));
		int32_t pan_t = d_raw > 0 ? (x << 11) / d_raw : 0;
		if (pan_t < -2048) pan_t = -2048;
		if (pan_t > 2047) pan_t = 2047;
		int32_t dist_t = (d_raw * 916) >> 12; // /sqrt(5): 0..~2048

		// Slow slew (~0.001/sample as in the original) -> pitch bends.
		pan_ += (pan_t - pan_) >> 10;
		dist_ += (dist_t - dist_) >> 10;

		// Delays: distance delay scaled by the room size (up to ~170ms in
		// the largest room) plus ITD up to ~1.5ms (72 samples at 48kHz) on
		// the far ear, Q8. dist_ (Q11) * room_samples >> 3 = Q8 samples.
		int32_t base_q8 = (12 << 8) + ((dist_ * room_samples) >> 3);
		int32_t itd_q8 = (pan_ * (72 << 8)) >> 11;
		int32_t target_l = base_q8 + (itd_q8 > 0 ? itd_q8 : 0);
		int32_t target_r = base_q8 + (itd_q8 < 0 ? -itd_q8 : 0);
		delay_l_q8_ += (target_l - delay_l_q8_) >> 11;
		delay_r_q8_ += (target_r - delay_r_q8_) >> 11;

		// Distance attenuation 1/(1 + k*d^2), k from the room sweep
		// (0.5 in the tiny room .. 15 in the hall), and equal-power pan.
		int32_t d2_q11 = (dist_ * dist_) >> 11;
		int32_t att_q12 = (2048 << 12) / (2048 + ((atten_k_q11 * d2_q11) >> 11));
		int32_t p = (pan_ + 2048) & 4095; // 0..4095
		int32_t fade_in = (p * (8190 - p)) >> 12;             // Q12, right
		int32_t q = kParamMax - p;
		int32_t fade_out = (q * (8190 - q)) >> 12;            // Q12, left
		int32_t gl = (fade_out * att_q12) >> 12;
		int32_t gr = (fade_in * att_q12) >> 12;

		int32_t left = (line_.ReadHermite(delay_l_q8_) * gl) >> 12;
		aux_ = Clip((line_.ReadHermite(delay_r_q8_) * gr) >> 12);
		return Clip(left);
	}

	int32_t aux() const { return aux_; }

private:
	// Integer square root of a 32-bit value (result fits in 16 bits).
	static int32_t SqrtU32(uint32_t v)
	{
		uint32_t res = 0;
		uint32_t bit = 1u << 30;
		while (bit > v) bit >>= 2;
		while (bit)
		{
			if (v >= res + bit)
			{
				v -= res + bit;
				res = (res >> 1) + bit;
			}
			else
			{
				res >>= 1;
			}
			bit >>= 2;
		}
		return static_cast<int32_t>(res);
	}

	DelayLine<14> line_;
	int32_t pan_ = 0;
	int32_t dist_ = 0;
	int32_t delay_l_q8_ = 12 << 8;
	int32_t delay_r_q8_ = 12 << 8;
	int32_t aux_ = 0;
};

} // namespace xmod

#endif // ALLOY_DSP_DELAY_H_
