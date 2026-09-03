// Threshold-crossing comparator/gate, Buchla-266-style: watches Audio In 1
// for excursions outside a fixed +-1V window around 0V, and fires a short
// trigger pulse each time the signal leaves that window (in either
// direction).
//
// The window has no CV control (confirmed fixed in the spec) — it's a
// simple, predictable "how often does this signal get loud" detector,
// useful for turning audio-rate chaos into clock-like triggers.
//
// A hysteresis margin is added so noise sitting right on the +-1V line
// doesn't retrigger the pulse dozens of times per crossing.

#ifndef UNCERTAINTY_DSP_COMPARATOR_H_
#define UNCERTAINTY_DSP_COMPARATOR_H_

#include <cstdint>

namespace uncertainty
{

	class Comparator
	{
	public:
		// window and hysteresis are in raw ADC codes (-2048..2047 = ~-6..+6V).
		Comparator(int32_t window, int32_t hysteresis)
			: window_(window), hysteresis_(hysteresis) {}

		// Call once per audio sample with the current input.
		// Returns true for the samples during which the output pulse
		// should be held high.
		bool Process(int32_t in)
		{
			bool outside = (in > window_ + hysteresis_) || (in < -window_ - hysteresis_);
			bool inside = (in < window_ - hysteresis_) && (in > -window_ + hysteresis_);

			if (!inside_window_ && inside)
			{
				inside_window_ = true; // re-armed, ready to fire on next exit
			}
			else if (inside_window_ && outside)
			{
				inside_window_ = false;
				pulse_samples_remaining_ = kPulseLengthSamples;
			}

			if (pulse_samples_remaining_ > 0)
			{
				pulse_samples_remaining_--;
				return true;
			}
			return false;
		}

	private:
		// ~1ms at 48kHz: short enough to read as a trigger, long enough
		// for downstream gear to reliably see it.
		static constexpr int32_t kPulseLengthSamples = 48;

		int32_t window_;
		int32_t hysteresis_;
		bool inside_window_ = true;
		int32_t pulse_samples_remaining_ = 0;
	};

} // namespace uncertainty

#endif // UNCERTAINTY_DSP_COMPARATOR_H_
