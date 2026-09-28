#pragma once
#include <cstdint>

namespace imp
{

// Pot "catch-up" (also called pickup or hook).
//
// With the switch Up, the three knobs set Levels 1-3; in Middle they set
// Chaos / Decay / Wet. When you flip between the two, the knobs are almost
// never where the other page's parameters were left. Jumping straight to
// the knob position would make an audible leap -- and, for Chaos, would
// re-randomise the loop just because you flipped a switch.
//
// So each parameter keeps its own stored value. After a page change the
// parameter is "unhooked": it ignores the knob until the knob passes back
// through the stored value, then it follows the knob again.
class Hook
{
public:
	explicit Hook(int32_t initial = 0) : value_(initial) {}

	// Call every sample while this parameter's page is active.
	void Update(int32_t raw)
	{
		if (!hooked_)
		{
			// Hook when the knob is close to the stored value, or when it
			// has jumped across it between two readings (a fast turn).
			int32_t before = lastRaw_ - value_;
			int32_t now = raw - value_;
			bool crossed = (before <= 0 && now >= 0) || (before >= 0 && now <= 0);
			if (crossed || (now > -kWindow && now < kWindow))
				hooked_ = true;
		}
		if (hooked_)
			value_ = raw;
		lastRaw_ = raw;
	}

	// Call when this parameter's page becomes active after a page change.
	void Unhook(int32_t raw)
	{
		hooked_ = false;
		lastRaw_ = raw;
	}

	// Call at boot for the page the switch starts on: take the knob as-is.
	void Grab(int32_t raw)
	{
		hooked_ = true;
		value_ = raw;
		lastRaw_ = raw;
	}

	int32_t Value() const { return value_; }
	bool Hooked() const { return hooked_; }

private:
	// Knobs jitter by roughly +/-10, so "close enough" is a bit wider.
	static constexpr int32_t kWindow = 24;

	int32_t value_;
	int32_t lastRaw_ = 0;
	bool hooked_ = false;
};

} // namespace imp
