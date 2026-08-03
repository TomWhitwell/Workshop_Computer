#pragma once

#include <cstdint>

namespace xmod
{

// Clocked probabilistic shift register in the style of the original Turing
// Machine. The active register can be 2..16 bits; all output mappings are
// normalized so short loops still cover their full output range.
class TuringSequencer
{
public:
	constexpr explicit TuringSequencer(uint32_t seed)
		: random_state_(seed != 0 ? seed : kFallbackSeed)
	{
		state_ = static_cast<uint16_t>(NextRandom());
		if (state_ == 0)
		{
			state_ = 1;
		}
		SetLength(8);
	}

	constexpr void SetLength(uint8_t length)
	{
		if (length < 2)
		{
			length = 2;
		}
		else if (length > 16)
		{
			length = 16;
		}

		if (length == length_)
		{
			return;
		}

		length_ = length;
		state_ &= ActiveMask();
		if (state_ == 0)
		{
			state_ = 1;
		}
		cycle_start_ = state_;
		step_ = 0;
	}

	// feedback_control follows the OG Turing response: 0 always inverts the
	// recirculated bit, 2048 is maximally uncertain, and 4095 repeats exactly.
	constexpr void Clock(uint16_t feedback_control)
	{
		bool invert = false;
		if (feedback_control == 0)
		{
			invert = true;
		}
		else if (feedback_control >= 4095)
		{
			invert = false;
		}
		else
		{
			invert = (NextRandom() & 0x0fffU) >= feedback_control;
		}

		const uint16_t feedback =
			static_cast<uint16_t>(((state_ >> (length_ - 1)) & 1U)
			                      ^ static_cast<uint16_t>(invert));
		state_ = static_cast<uint16_t>(((state_ << 1) | feedback)
		                               & ActiveMask());

		++step_;
		if (step_ >= length_)
		{
			step_ = 0;
			cycle_start_ = state_;
		}
	}

	constexpr void ResetToCycleStart()
	{
		state_ = cycle_start_;
		step_ = 0;
	}

	constexpr uint8_t length() const { return length_; }
	constexpr uint16_t state() const { return state_; }

	// MIDI C3 through C7 in a minor-pentatonic note pool. Spread zero returns
	// the root; full spread exposes four octaves.
	constexpr uint8_t MidiNote(uint16_t spread) const
	{
		if (spread > 4095)
		{
			spread = 4095;
		}

		const uint32_t max_degree =
			(static_cast<uint32_t>(spread) * 20U + 2047U) / 4095U;
		const uint32_t degree =
			(static_cast<uint32_t>(NormalizedValue()) * max_degree + 32767U)
			/ 65535U;
		return static_cast<uint8_t>(
			48U + (degree / 5U) * 12U
			+ kMinorPentatonic[degree % 5U]);
	}

	constexpr int16_t BipolarCv(uint16_t spread) const
	{
		if (spread > 4095)
		{
			spread = 4095;
		}

		const int32_t full_scale =
			(static_cast<int32_t>(NormalizedValue()) - 32768) / 16;
		return static_cast<int16_t>(
			(full_scale * static_cast<int32_t>(spread)) / 4095);
	}

	constexpr bool GateA() const { return (state_ & 1U) != 0; }

	constexpr bool GateB() const
	{
		return ((state_ >> (length_ / 2U)) & 1U) != 0;
	}

	constexpr bool LedBit(uint8_t led) const
	{
		return led < length_ && led < 6 && ((state_ >> led) & 1U) != 0;
	}

private:
	static constexpr uint32_t kFallbackSeed = 0x6d2b79f5U;
	inline static constexpr uint8_t kMinorPentatonic[5] = {0, 3, 5, 7, 10};

	constexpr uint16_t ActiveMask() const
	{
		return length_ == 16
			? 0xffffU
			: static_cast<uint16_t>((1U << length_) - 1U);
	}

	constexpr uint16_t NormalizedValue() const
	{
		const uint32_t mask = ActiveMask();
		return static_cast<uint16_t>(
			(static_cast<uint32_t>(state_) * 65535U + mask / 2U) / mask);
	}

	constexpr uint32_t NextRandom()
	{
		uint32_t x = random_state_;
		x ^= x << 13;
		x ^= x >> 17;
		x ^= x << 5;
		random_state_ = x;
		return x;
	}

	uint32_t random_state_;
	uint16_t state_ = 1;
	uint16_t cycle_start_ = 1;
	uint8_t length_ = 0;
	uint8_t step_ = 0;
};

} // namespace xmod
