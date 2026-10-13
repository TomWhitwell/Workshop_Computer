// Pot-pickup / soft-takeover: prevents a mode-switched knob from jumping
// a parameter when the knob's physical position doesn't match the value
// that parameter was last left at. Ported verbatim from
// releases/97_alloy/dsp/soft_takeover.h — same problem (a knob doing two
// jobs depending on a switch position), same fix, no changes.
//
// Usage: call Arm(target, current) once, at the moment a parameter
// becomes "live" again after being frozen, with target = the frozen
// value it should hold and current = the knob's position right now.
// Then call Allows(current) once per sample; it returns false (keep
// using the frozen value) until the knob gets close to target or
// physically crosses through it, at which point it latches true (start
// tracking the knob live) and stays true until the next Arm().

#ifndef UNCERTAINTY_DSP_SOFT_TAKEOVER_H_
#define UNCERTAINTY_DSP_SOFT_TAKEOVER_H_

#include <cstdint>

namespace uncertainty
{

	class SoftTakeover
	{
	public:
		constexpr void Arm(uint16_t target, uint16_t current)
		{
			target_ = target;
			previous_ = current;
			waiting_ = Distance(current, target) > kPickupTolerance;
		}

		constexpr bool Allows(uint16_t current)
		{
			if (!waiting_)
			{
				previous_ = current;
				return true;
			}

			const bool close = Distance(current, target_) <= kPickupTolerance;
			const bool crossed =
				(previous_ < target_ && current > target_)
				|| (previous_ > target_ && current < target_);
			previous_ = current;
			if (close || crossed)
			{
				waiting_ = false;
				return true;
			}
			return false;
		}

		constexpr bool waiting() const { return waiting_; }

	private:
		static constexpr uint16_t kPickupTolerance = 24;

		static constexpr uint16_t Distance(uint16_t a, uint16_t b)
		{
			return a > b ? static_cast<uint16_t>(a - b)
			             : static_cast<uint16_t>(b - a);
		}

		uint16_t target_ = 0;
		uint16_t previous_ = 0;
		bool waiting_ = false;
	};

} // namespace uncertainty

#endif // UNCERTAINTY_DSP_SOFT_TAKEOVER_H_
