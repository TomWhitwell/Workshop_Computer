#pragma once
#include <cstdint>

namespace constancy
{

// Pot "catch-up" (also called pickup or hook), from Impermanence via Equanimity.
//
// The knobs change jobs between switch up (the Levels page) and middle
// (the Play page). When the switch moves, a knob is almost never where
// its other job was left; jumping straight to the knob position would make
// an audible leap.
//
// So each setting keeps its own stored value. After a switch change it is
// "unhooked": it ignores the knob until the knob passes back through the
// stored value, then it follows the knob again.
class Hook
{
public:
	explicit Hook(int32_t initial = 0) : value_(initial) {}

	// Call on every update while the knob controls this parameter.
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

	// Call when the knob comes back to this parameter.
	void Unhook(int32_t raw)
	{
		hooked_ = false;
		lastRaw_ = raw;
	}

	// Call at boot: take the knob as-is.
	void Grab(int32_t raw)
	{
		hooked_ = true;
		value_ = raw;
		lastRaw_ = raw;
	}

	int32_t Value() const { return value_; }

private:
	// Knobs jitter by roughly +/-10, so "close enough" is a bit wider.
	static constexpr int32_t kWindow = 24;

	int32_t value_;
	int32_t lastRaw_ = 0;
	bool hooked_ = false;
};

} // namespace constancy
