// One physical knob, two jobs, chosen by the switch: a "middle" role
// (DSP mode) and an "up" role (Mix mode). Built on SoftTakeover so
// switching modes never snaps a value to wherever the knob happens to be
// sitting - each role's value freezes while the other is live, and only
// starts tracking the knob again once the knob physically returns to, or
// crosses through, the frozen value.
//
// Freezing a value while the other role is live is only half the job, and
// getting only that half is a real bug rather than a cosmetic one: the
// value holds correctly while the switch is up, then jumps to the knob's
// physical position the instant the switch comes back. On this card that
// meant the kick retuned itself to wherever MAIN had been left after
// setting the dry/wet blend, and flipping up slammed the input levels to
// wherever the argument and function knobs happened to sit.
#ifndef SCINTILLATOR_DSP_DUAL_ROLE_KNOB_H_
#define SCINTILLATOR_DSP_DUAL_ROLE_KNOB_H_

#include <cstdint>

#include "soft_takeover.h"

namespace scintillator
{

	class DualRoleKnob
	{
	public:
		// The two starting values are what each role reads before it has
		// ever been live, so a freshly flashed card begins somewhere
		// usable rather than at zero.
		constexpr DualRoleKnob(int32_t middleStart, int32_t upStart)
			: middle_(static_cast<uint16_t>(middleStart)),
			  up_(static_cast<uint16_t>(upStart)) {}

		// Call once per sample with which role is live and the knob's raw
		// reading (0..4095), whichever role is currently in use.
		void __not_in_flash_func(Update)(bool upActive, int32_t knob)
		{
			uint16_t k = static_cast<uint16_t>(knob);

			if (!initialised_)
			{
				// Whichever role is live at power-up adopts the knob
				// straight away; the other keeps its starting value until
				// the knob is moved to pick it up.
				upActive_ = upActive;
				if (upActive) { up_ = k; upSeeded_ = true; }
				else { middle_ = k; middleSeeded_ = true; }
				initialised_ = true;
				return;
			}

			if (upActive != upActive_)
			{
				// A role going live for the very first time adopts the
				// knob straight away. Its starting value is only a sensible
				// default, not something the player chose, and making them
				// hunt for it would mean sweeping the knob to one end
				// before the control did anything at all. Every switch
				// after that holds the value they actually set.
				if (upActive)
				{
					if (upSeeded_) upTakeover_.Arm(up_, k);
					else { up_ = k; upSeeded_ = true; }
				}
				else
				{
					if (middleSeeded_) middleTakeover_.Arm(middle_, k);
					else { middle_ = k; middleSeeded_ = true; }
				}
				upActive_ = upActive;
			}

			if (upActive_)
			{
				if (upTakeover_.Allows(k)) up_ = k;
			}
			else
			{
				if (middleTakeover_.Allows(k)) middle_ = k;
			}
		}

		int32_t Middle() const { return middle_; }
		int32_t Up() const { return up_; }

	private:
		uint16_t middle_;
		uint16_t up_;
		bool upActive_ = false;
		bool initialised_ = false;
		bool middleSeeded_ = false;
		bool upSeeded_ = false;
		SoftTakeover middleTakeover_;
		SoftTakeover upTakeover_;
	};

} // namespace scintillator

#endif // SCINTILLATOR_DSP_DUAL_ROLE_KNOB_H_
