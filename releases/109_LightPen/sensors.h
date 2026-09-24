// sensors.h — the shared R/G/B pipeline every mode reads from.
//
//   raw -> per-channel table -> un-mix matrix -> shared gain -> X gain -> slew
//
// Red is CV In 1, Green CV In 2, Blue Audio In 2 (read as DC), signed 12-bit
// as the library returns them.
//
// Everything below the tables is built at boot by SetCalibration() in
// sensors.cpp, which is where the reasoning lives. What matters here:
//
//   * The per-channel table is 513 entries over the whole signed input range,
//     read with linear interpolation. In a TWO-POINT calibration it carries
//     the old log-resistance curve and the two stages after it are exactly
//     identity, so that calibration behaves bit-for-bit as it always has. In
//     a FIVE-POINT calibration it carries LINEAR LIGHT instead, because
//     cross-talk between the gels only adds up in that domain.
//
//   * The gain after the un-mix is SHARED: one lookup on the brightest
//     channel, applied to all three. That is the whole trick. A per-channel
//     compressive curve lifts the residual bleed back into view and undoes
//     most of what the un-mix just won; taking the gain from max(c) keeps
//     every ratio c_i : c_j exactly as the un-mix left it.
//
// Keep this header inlined into ProcessSample (which is __not_in_flash_func)
// and keep the tables as members of the card object. Moving any of it to a
// .cpp puts it in flash, where one XIP miss costs more than the whole stage.

#pragma once
#include <cstdint>
#include "fastmath.h"
#include "calibration.h"

namespace lp {

struct SensorFrame
{
	int32_t ur, ug, ub;   // Q16 0..65535: 0 = calibrated black, 65535 = white
};

/// Unipolar Q16 (0..65535) to 0..~5V in CVOut2Precise units (+/-262143 ~ +/-6V).
static inline int32_t __attribute__((always_inline)) q16_to_cv5v(int32_t u)
{
	return (u * 27307) >> 13;
}

/// Unipolar Q16 to 0..~5V on an audio out. The Rev 1 documentation confirms
/// the CV/Audio outputs are bipolar DC-coupled, approximately -6V to +6V.
static inline int16_t __attribute__((always_inline)) q16_to_audio5v(int32_t u)
{
	return static_cast<int16_t>((u * 1706) >> 16);
}

class SensorPipeline
{
public:
	// Indexed by (raw + 2048) >> 3, so the table spans the input's whole
	// signed range rather than just the positive half: a wand wired the other
	// way round, or a channel sitting below 0V, still calibrates.
	static constexpr int kLutSize = 513;
	// Indexed by v >> 9, v being Q16 light with 65536 = calibrated white.
	static constexpr int kGainSize = 513;

	// 4x white of headroom, so the X knob can still pull back something
	// brighter than the reference. Paired with the matrix's row budget below:
	// 262140 * 6144 is 75% of int32, which is what makes Q10 the right format.
	static constexpr int32_t kLightCeiling = 262140;
	static constexpr int32_t kLightFloor   = -65536;
	static constexpr int32_t kMatrixRowBudget = 6144;   // sum of |A10| per row, Q10

	/// Once, at boot. Divides, logarithms, a 3x3 inverse — never the audio path.
	void SetCalibration(const CalibData &d);

	/// 0 when no un-mix is in force, else 1..5 for how well the gels separate
	/// colour. main.cpp shows it on the LEDs after a calibration.
	int SeparationBars() const { return sepBars_; }
	uint8_t Quality() const { return quality_; }

	/// Control rate: the knob laws use pow2_scale, which is too dear per sample.
	void SetKnobs(int32_t xKnob, int32_t yKnob)
	{
		// X: gain, +/-2 octaves (0.25x..4x) around unity at noon, held as
		// Q10 (unity 1024) so the product below fits 31 bits.
		gainQ10_ = pow2_scale(kQ16One, (xKnob - 2048) * 4) >> 6;
		// Y: slew shift 5..14. The floor of 5 (~240Hz) matches the smoothing the
		// library already gives the CV inputs, so Blue — on the audio input,
		// which only gets a 12kHz notch — is no noisier than Red and Green.
		// The top, 14, is a ~340ms time constant.
		shift_ = static_cast<uint8_t>(5 + ((yKnob * 10) >> 12));
	}

	/// Every sample.
	void Update(const RawRGB &raw, SensorFrame &f)
	{
		const int32_t l0 = CurveAt(raw.r, 0);
		const int32_t l1 = CurveAt(raw.g, 1);
		const int32_t l2 = CurveAt(raw.b, 2);

		// Un-mix. Row budget times light ceiling is 1.61e9, inside int32 —
		// see kMatrixRowBudget. Identity in a two-point calibration.
		int32_t c[3];
		for (int i = 0; i < 3; i++)
		{
			int32_t acc = a10_[i][0] * l0 + a10_[i][1] * l1 + a10_[i][2] * l2;
			c[i] = clamp_i32(acc >> 10, 0, kLightCeiling);
		}

		// One gain for all three, from the brightest. Because c[i] <= v, the
		// product below is bounded by v * G(v), which the table is built to
		// keep inside int32 whatever the light level.
		const int32_t g = GainAt(max3_i32(c[0], c[1], c[2]));
		f.ur = Channel((c[0] * g) >> 12, 0);
		f.ug = Channel((c[1] * g) >> 12, 1);
		f.ub = Channel((c[2] * g) >> 12, 2);
	}

	/// The per-channel table alone. Also used while building, so the
	/// references land on exactly 0 and full.
	inline int32_t __attribute__((always_inline)) CurveAt(int32_t raw, int i) const
	{
		int32_t n = clamp_i32(raw, -2048, 2047) + 2048;
		int32_t k = n >> 3;
		int32_t a = lut_[i][k];
		return a + (((lut_[i][k + 1] - a) * (n & 7)) >> 3);
	}

private:
	inline int32_t __attribute__((always_inline)) GainAt(int32_t v) const
	{
		int32_t k = v >> 9;
		int32_t a = gain_[k];
		return a + (((gain_[k + 1] - a) * (v & 511)) >> 9);
	}

	inline int32_t __attribute__((always_inline)) Channel(int32_t y, int i)
	{
		int32_t u = clamp_i32((y * gainQ10_) >> 10, 0, 65535);
		// slew_exact, not slew: Modes 1, 3 and 6 threshold-compare this value,
		// and the plain shift stalls short of its target by a
		// direction-dependent amount.
		state_[i] = slew_exact(state_[i], u, shift_);
		return state_[i];
	}

	void BuildTwoPoint(const CalibData &d);
	bool BuildFivePoint(const CalibData &d);
	void BuildGainTable(int32_t l0Q16);
	void Normalise(int ch, int32_t nBlack, int32_t nWhite);

	int32_t  lut_[3][kLutSize];
	int32_t  gain_[kGainSize];
	int32_t  a10_[3][3] = { { 1024, 0, 0 }, { 0, 1024, 0 }, { 0, 0, 1024 } };
	int32_t  state_[3]  = { 0, 0, 0 };
	int32_t  gainQ10_   = 1024;
	uint8_t  shift_     = 5;
	uint8_t  quality_   = 0;
	int      sepBars_   = 0;
};

} // namespace lp
