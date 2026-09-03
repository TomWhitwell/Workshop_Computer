// One physical knob, two jobs, picked by the Z switch's Up position:
// a "primary" role (active in Middle/Down) and a "secondary" role that's
// an attenuverter for that same section (active in Up). Built on
// SoftTakeover so switching roles never snaps a value to wherever the
// knob happens to be sitting — each role's value freezes while the other
// is active, and only resumes tracking the knob once the knob physically
// returns to (or crosses) the frozen value.
//
// The attenuverter curve is centred at the knob's mechanical middle
// (12 o'clock): dead centre mutes, turning towards full CCW ramps
// towards -1 (inverted, ramping up in the negative direction), full CW
// ramps towards +1 — the standard bipolar attenuverter taper.

#ifndef UNCERTAINTY_DSP_DUAL_ROLE_KNOB_H_
#define UNCERTAINTY_DSP_DUAL_ROLE_KNOB_H_

#include <cstdint>

#include "soft_takeover.h"

namespace uncertainty
{

	class DualRoleKnob
	{
	public:
		// Call once per sample (or per control-rate tick, for a knob
		// tracked on core 1) with whether the secondary role is active
		// right now and the knob's current raw reading (0..4095).
		void Update(bool secondaryActive, int32_t knob)
		{
			uint16_t k = static_cast<uint16_t>(knob);

			if (!initialized_)
			{
				primary_ = k;
				secondary_ = k;
				secondaryActive_ = secondaryActive;
				initialized_ = true;
				return;
			}

			if (secondaryActive != secondaryActive_)
			{
				// Role just switched: arm a catch-up for whichever value
				// is about to go live again, targeting the value it was
				// frozen at.
				if (secondaryActive)
					secondaryTakeover_.Arm(secondary_, k);
				else
					primaryTakeover_.Arm(primary_, k);
				secondaryActive_ = secondaryActive;
			}

			if (secondaryActive_)
			{
				if (secondaryTakeover_.Allows(k)) secondary_ = k;
			}
			else
			{
				if (primaryTakeover_.Allows(k)) primary_ = k;
			}
		}

		// The primary-role value, raw 0..4095 — frozen while the
		// secondary (attenuverter) role is active.
		int32_t Primary() const { return primary_; }

		// Applies this knob's attenuverter setting to a signal.
		// signal: any range that fits comfortably under ~2^20 (audio and
		// millivolt-scale signals on this card both qualify).
		// Returns signal scaled by a factor from -1 (full CCW) through 0
		// (12 o'clock) to +1 (full CW).
		int32_t Attenuvert(int32_t signal) const
		{
			int32_t offset = static_cast<int32_t>(secondary_) - kCenter;
			return (signal * offset) >> 11; // >>11 == /2048 == /kCenter
		}

	private:
		static constexpr int32_t kCenter = 2048;

		uint16_t primary_ = 0;
		uint16_t secondary_ = 0;
		bool secondaryActive_ = false;
		bool initialized_ = false;
		SoftTakeover primaryTakeover_;
		SoftTakeover secondaryTakeover_;
	};

} // namespace uncertainty

#endif // UNCERTAINTY_DSP_DUAL_ROLE_KNOB_H_
