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
//
// That hysteresis margin does NOT bound the firing rate for a periodic
// signal, though — it only raises how loud the signal has to be to count.
// Audio In 1 is normally a VCO feeding the wavefolder, and a steady tone
// above the window crosses it on the same schedule every cycle no matter
// where the threshold sits, right up until the threshold exceeds the
// tone's peak entirely — at which point it doesn't fire less, it stops
// firing at all. (Confirmed numerically: widening the window from 1V to
// 4.5V left a 220Hz test tone's trigger rate exactly unchanged at 440/s —
// twice the tone's frequency — the whole way, then dropped it straight to
// 0 once the window passed the tone's amplitude.) So a second, independent
// mechanism does the actual rate-limiting: after a pulse fires, new
// triggers are ignored for a minimum interval, however fast the input
// keeps crossing the window. That's the only thing that turns "crosses
// every audio cycle" into "fires a few times a second" for a steady tone.

#ifndef UNCERTAINTY_DSP_COMPARATOR_H_
#define UNCERTAINTY_DSP_COMPARATOR_H_

#include <cstdint>

namespace uncertainty
{

	class Comparator
	{
	public:
		// window, hysteresis: raw ADC codes (-2048..2047 = ~-6..+6V).
		// minRetriggerSamples: minimum gap, in samples, between the start
		// of one pulse and the next — the actual rate cap.
		Comparator(int32_t window, int32_t hysteresis, int32_t minRetriggerSamples)
			: window_(window), hysteresis_(hysteresis), minRetriggerSamples_(minRetriggerSamples) {}

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
			else if (inside_window_ && outside && retrigger_lockout_remaining_ == 0)
			{
				inside_window_ = false;
				pulse_samples_remaining_ = kPulseLengthSamples;
				retrigger_lockout_remaining_ = minRetriggerSamples_;
			}

			if (retrigger_lockout_remaining_ > 0)
			{
				retrigger_lockout_remaining_--;
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
		// for downstream gear to reliably see it. Independent of
		// minRetriggerSamples_ — this is how long each pulse lasts, that's
		// how far apart pulses have to start.
		static constexpr int32_t kPulseLengthSamples = 48;

		int32_t window_;
		int32_t hysteresis_;
		int32_t minRetriggerSamples_;

		bool inside_window_ = true;
		int32_t pulse_samples_remaining_ = 0;
		int32_t retrigger_lockout_remaining_ = 0;
	};

} // namespace uncertainty

#endif // UNCERTAINTY_DSP_COMPARATOR_H_
