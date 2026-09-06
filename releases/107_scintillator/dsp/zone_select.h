// Divides a knob's 0-4095 travel into N equal zones, with a small
// hysteresis band at each boundary so a selection doesn't chatter when the
// knob sits right on an edge. Used for the argument section's 8-way select
// (Knob X) and the function section's 6-way select (Knob Y).
#pragma once

#include <cstdint>

namespace scintillator
{

	template <int N>
	class ZoneSelector
	{
	public:
		ZoneSelector()
		{
			for (int i = 1; i < N; i++)
			{
				boundary_[i - 1] = (4096 * i) / N;
			}
		}

		// Feed the current knob reading (0-4095); returns the current zone
		// (0..N-1). A Schmitt-trigger check against each boundary means the
		// knob has to clear a boundary by more than the hysteresis band
		// before the zone actually changes, so a knob resting on an edge
		// can't flicker between two zones.
		int Update(int32_t knobValue)
		{
			while (zone_ < N - 1 && knobValue >= boundary_[zone_] + kHysteresis) zone_++;
			while (zone_ > 0 && knobValue < boundary_[zone_ - 1] - kHysteresis) zone_--;
			return zone_;
		}

		int Zone() const { return zone_; }

	private:
		// ~2% of the knob's 4096-count range.
		static constexpr int32_t kHysteresis = 82;

		int32_t boundary_[N - 1];
		int zone_ = 0;
	};

} // namespace scintillator
