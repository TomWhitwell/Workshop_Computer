// calibration.h — capturing the card's view of the world.
//
// Hold Down while powering on, release, and the card takes FIVE captures, one
// per tap of Down: red, green, blue, white, black. Up cancels and keeps what
// was already saved.
//
// There is no mode to choose, because the captures say which calibration you
// meant. Show it four bright surfaces and a dark one:
//
//   FIVE-POINT  red, green, blue, white, black — a screen showing full-screen
//     primaries. Adds a cross-talk un-mix. Gelled LDRs are hopelessly broad —
//     a blue screen lights all three cells — and that mixing is linear IN
//     LIGHT, so the three primary captures measure the mixing matrix and boot
//     inverts it (sensors.cpp). White also pins each cell's power law by
//     additivity: white = red + green + blue.
//
//   TWO-POINT  white, white, white, white, black — just show white for the
//     first four taps. Level and curve, no colour.
//
// CapturesLookTwoPoint() tells them apart, and the margin is wide: on a real
// display white is the SUM of the primaries, so at least one cell always
// reads white well clear of the dimmest primary (168 counts even with gels
// that can barely tell colours apart), while four presentations of the same
// white vary by only a dozen. A mis-read degrades safely in any case — four
// near-identical "primaries" make a singular matrix, which the separability
// test declines, falling back to the white and black that were captured
// anyway.
//
// Use a black CARD for the last step, not a black screen, so zero means the
// same thing in both calibrations.
//
// This class only captures. sensors.cpp does the maths, calibstore.h stores
// it, and main.cpp owns the switch, the save and the reboot.

#pragma once
#include <cstdint>
#include "fastmath.h"

namespace lp {

struct RawRGB
{
	int32_t r, g, b;
};

enum class CalibMode : uint8_t { TwoPoint = 0, FivePoint = 1 };

// Set by the boot-time maths, shown on the LEDs, cached in flash.
constexpr uint8_t kQualGammaDefault = 1;   // additivity gave no answer; default used
constexpr uint8_t kQualRowClamped   = 2;   // white row-scale hit its limit
constexpr uint8_t kQualLowSep       = 4;   // gels cannot separate; fell back to two-point

struct CalibData
{
	int32_t black[3];
	int32_t white[3];
	int32_t prim[3][3];   // prim[j][i]: channel i reading under primary j
	uint8_t mode;         // CalibMode
	uint8_t quality;      // kQual* bits
	uint8_t reserved[2];
};

/// Less white-to-black difference than this (of +/-2048) is not a reading:
/// the LDR is missing, or both captures saw the same thing. ~0.2V.
constexpr int32_t kMinCalibSpan = 64;

/// A capture outside this is pinned at a rail — unplugged, or pointed at the sun.
constexpr int32_t kCaptureMin = 16;
constexpr int32_t kCaptureMax = 2040;

/// What an uncalibrated card assumes: 0V is black, full scale is white.
static inline CalibData CalibDefaults()
{
	CalibData d{};
	for (int i = 0; i < 3; i++) { d.black[i] = 0; d.white[i] = 2047; }
	d.mode = static_cast<uint8_t>(CalibMode::TwoPoint);
	return d;
}

/// True when the four bright captures are the same surface shown four times,
/// which is how a two-point calibration is asked for. A real set of primaries
/// cannot look like this: white is their sum, so at least one cell reads it
/// clearly brighter than the dimmest primary.
static inline bool CapturesLookTwoPoint(const CalibData &d)
{
	for (int ch = 0; ch < 3; ch++)
	{
		int32_t span = d.white[ch] - d.black[ch];
		if (span < 0) span = -span;
		int32_t tol = span / 6;              // generous: allows a wandering hand
		if (tol < 32) tol = 32;
		for (int j = 0; j < 3; j++)
		{
			int32_t diff = d.white[ch] - d.prim[j][ch];
			if (diff < 0) diff = -diff;
			if (diff > tol) return false;
		}
	}
	return true;
}

/// Bit i set: channel i (R, G, B) has too small a span to use.
static inline uint8_t CalibFailMask(const CalibData &d)
{
	uint8_t mask = 0;
	for (int i = 0; i < 3; i++)
	{
		int32_t span = d.white[i] - d.black[i];
		if (span < kMinCalibSpan && span > -kMinCalibSpan) mask |= 1u << i;
	}
	return mask;
}

class Calibration
{
public:
	static constexpr int     kCaptureShift   = 14;             // 16384 samples, ~0.34s
	static constexpr int32_t kCaptureSamples = 1 << kCaptureShift;
	static constexpr int     kDoneTicks      = kCtrlRate;      // 1s "saved" display
	static constexpr int     kFailTicks      = kCtrlRate * 2;  // 2s "failed" display

	static constexpr int kReportTicks = kCtrlRate;   // 1s "this is what I recorded"

	void Begin()
	{
		step_ = Step::WaitRed;
		ticks_ = 0;
		failMask_ = 0;
		data_ = CalibDefaults();
	}

	/// Every sample.
	void Sample(const RawRGB &raw)
	{
		if (!Capturing()) return;
		sum_[0] += raw.r;
		sum_[1] += raw.g;
		sum_[2] += raw.b;
		if (++count_ < kCaptureSamples) return;

		int32_t avg[3];
		for (int i = 0; i < 3; i++) avg[i] = sum_[i] >> kCaptureShift;

		for (int i = 0; i < 3; i++)
		{
			if (avg[i] < kCaptureMin || avg[i] > kCaptureMax)
			{
				// Retry just this capture: the others are still good.
				Fail(WaitFor(step_), 0);
				return;
			}
		}

		int32_t *dst = Destination(step_);
		for (int i = 0; i < 3; i++) dst[i] = avg[i];

		Advance();
	}

	/// Control rate: the debounced Down press.
	void Tap()
	{
		if (IsWait(step_)) StartCapture();
	}

	/// Control rate.
	void Tick()
	{
		blink_++;
		if (ticks_ > 0 && --ticks_ == 0)
		{
			if (step_ == Step::Failed) step_ = retry_;
			else if (step_ == Step::Report) { step_ = Step::Done; ticks_ = kDoneTicks; }
			else if (step_ == Step::Done) step_ = Step::Save;
		}
	}

	bool ReadyToSave() const { return step_ == Step::Save; }
	const CalibData &Result() const { return data_; }

	/// Control rate. Fills level[6], 0..4095. Layout as leds.h: left column
	/// 0/2/4, right column 1/3/5.
	///
	/// While choosing: the left column counts the sequence (one LED for two
	/// captures, three for five) and the right column previews the first
	/// target. While waiting for a tap: the left column is a live raw R/G/B
	/// meter, so the wand can be aimed, and the right column IS the target —
	/// red, green, blue, all three for white, all three dim for black. Dim
	/// rather than dark: a dark card is indistinguishable from a crashed one.
	void Leds(const RawRGB &raw, uint16_t level[6]) const
	{
		for (int i = 0; i < 6; i++) level[i] = 0;
		bool slow = (blink_ % kCtrlRate) < kCtrlRate / 2;   // 1Hz
		bool fast = (blink_ % 188) < 94;                    // 8Hz

		switch (step_)
		{
		case Step::WaitRed:
		case Step::WaitGreen:
		case Step::WaitBlue:
		case Step::WaitWhite:
		case Step::WaitBlack:
			level[0] = Meter(raw.r);
			level[2] = Meter(raw.g);
			level[4] = Meter(raw.b);
			if (slow) Target(step_, level);
			break;

		case Step::CapRed:
		case Step::CapGreen:
		case Step::CapBlue:
		case Step::CapWhite:
		case Step::CapBlack:
		{
			int n = 1 + ((count_ * 5) >> kCaptureShift);
			for (int i = 0; i < n; i++) level[i] = 4095;
			break;
		}

		case Step::Report:
		{
			// Two LEDs or five, so you can see which calibration it decided
			// you gave it before it saves and reboots.
			int n = (data_.mode == static_cast<uint8_t>(CalibMode::TwoPoint)) ? 2 : 5;
			if (fast) for (int i = 0; i < n; i++) level[i] = 4095;
			break;
		}

		case Step::Failed:
			if (!fast) break;
			if (failMask_)
			{
				for (int ch = 0; ch < 3; ch++)
					if ((failMask_ >> ch) & 1) level[1 + 2 * ch] = 4095;
			}
			else
			{
				Target(retry_, level);
			}
			break;

		case Step::Done:
		case Step::Save:
			for (int i = 0; i < 6; i++) level[i] = 4095;
			break;
		}
	}

private:
	enum class Step : uint8_t
	{
		WaitRed, CapRed, WaitGreen, CapGreen, WaitBlue, CapBlue,
		WaitWhite, CapWhite, WaitBlack, CapBlack,
		Report, Failed, Done, Save,
	};

	static bool IsWait(Step s)
	{
		return s == Step::WaitRed || s == Step::WaitGreen || s == Step::WaitBlue
		    || s == Step::WaitWhite || s == Step::WaitBlack;
	}

	bool Capturing() const
	{
		return step_ == Step::CapRed || step_ == Step::CapGreen || step_ == Step::CapBlue
		    || step_ == Step::CapWhite || step_ == Step::CapBlack;
	}

	static Step WaitFor(Step capture)
	{
		return static_cast<Step>(static_cast<uint8_t>(capture) - 1);
	}

	int32_t *Destination(Step capture)
	{
		switch (capture)
		{
		case Step::CapRed:   return data_.prim[0];
		case Step::CapGreen: return data_.prim[1];
		case Step::CapBlue:  return data_.prim[2];
		case Step::CapWhite: return data_.white;
		default:             return data_.black;
		}
	}

	/// The right column for a step's target colour.
	static void Target(Step wait, uint16_t level[6])
	{
		constexpr uint16_t kDim = 410;   // ~10%: "waiting, and the target is black"
		switch (wait)
		{
		case Step::WaitRed:   level[1] = 4095; break;
		case Step::WaitGreen: level[3] = 4095; break;
		case Step::WaitBlue:  level[5] = 4095; break;
		case Step::WaitWhite: level[1] = level[3] = level[5] = 4095; break;
		case Step::WaitBlack: level[1] = level[3] = level[5] = kDim; break;
		default: break;
		}
	}

	void StartCapture()
	{
		step_ = static_cast<Step>(static_cast<uint8_t>(step_) + 1);
		count_ = 0;
		sum_[0] = sum_[1] = sum_[2] = 0;
	}

	void Fail(Step retry, uint8_t mask)
	{
		failMask_ = mask;
		retry_ = retry;
		step_ = Step::Failed;
		ticks_ = kFailTicks;
	}

	void Advance()
	{
		switch (step_)
		{
		case Step::CapRed:   step_ = Step::WaitGreen; break;
		case Step::CapGreen: step_ = Step::WaitBlue;  break;
		case Step::CapBlue:  step_ = Step::WaitWhite; break;
		case Step::CapWhite: step_ = Step::WaitBlack; break;
		default:
		{
			uint8_t mask = CalibFailMask(data_);
			if (mask)
			{
				// White and black too close: the whole sequence is suspect.
				Fail(Step::WaitRed, mask);
				return;
			}
			// The captures say which calibration was meant.
			data_.mode = static_cast<uint8_t>(CapturesLookTwoPoint(data_)
				? CalibMode::TwoPoint : CalibMode::FivePoint);
			step_ = Step::Report;
			ticks_ = kReportTicks;
			break;
		}
		}
	}

	static uint16_t Meter(int32_t raw)
	{
		return static_cast<uint16_t>(clamp_i32(raw * 2, 0, 4095));
	}

	CalibData data_ = CalibDefaults();
	Step      step_ = Step::WaitRed;
	Step      retry_ = Step::WaitRed;
	int32_t   count_ = 0;
	int32_t   sum_[3] = { 0, 0, 0 };
	int       ticks_ = 0;
	uint32_t  blink_ = 0;
	uint8_t   failMask_ = 0;
};

} // namespace lp
