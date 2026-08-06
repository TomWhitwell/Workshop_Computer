#pragma once

#include <cstdint>

namespace xmod
{

// Prevents a mode-switched control from jumping until the physical knob
// reaches or crosses the value that was active before the mode change.
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

} // namespace xmod
