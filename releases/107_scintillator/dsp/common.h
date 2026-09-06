// Small shared helpers used across Scintillator's DSP blocks: clipping to
// the card's signed 12-bit audio range, absolute value, a cheap integer
// square root (for the argument section's sqrt(A^2+B^2)), and a minimal
// xorshift PRNG (for the Geiger gates' stochastic firing).
#pragma once

#include <cstdint>

// The Pico SDK defines this; falling back to a no-op lets these DSP
// headers also be compiled and tested on a host machine.
#ifndef __not_in_flash_func
#define __not_in_flash_func(f) f
#endif

namespace scintillator
{

	// Audio/CV out is signed 12-bit, -2048 to +2047 (~+-6V). Every DSP block
	// clips its own output to this range before handing it onward, since
	// AudioOut1/2 don't clamp for you.
	inline int32_t Clip(int32_t v)
	{
		if (v < -2048) return -2048;
		if (v > 2047) return 2047;
		return v;
	}

	inline int32_t Cabs(int32_t v)
	{
		return v < 0 ? -v : v;
	}

	// Integer square root of a non-negative value, bit-by-bit (Newton-ish)
	// method: no float, no library call, comfortably inside the sample
	// budget. Used for the argument section's sqrt(A^2+B^2), which only
	// ever sees a small (<=32-bit) sum of two squares.
	inline int32_t IntSqrt(int32_t v)
	{
		if (v <= 0) return 0;
		uint32_t x = static_cast<uint32_t>(v);
		uint32_t res = 0;
		uint32_t bit = 1u << 30; // highest even power of 4 <= any 32-bit value
		while (bit > x) bit >>= 2;
		while (bit != 0)
		{
			if (x >= res + bit)
			{
				x -= res + bit;
				res = (res >> 1) + bit;
			}
			else
			{
				res >>= 1;
			}
			bit >>= 2;
		}
		return static_cast<int32_t>(res);
	}

	// Minimal xorshift32 PRNG. Used only for the Geiger gates' stochastic
	// thinning, where "random enough to feel unpredictable" is the whole
	// requirement - no cryptographic or statistical rigour needed.
	class Xorshift32
	{
	public:
		explicit Xorshift32(uint32_t seed) : state_(seed ? seed : 1) {}

		uint32_t Next()
		{
			state_ ^= state_ << 13;
			state_ ^= state_ >> 17;
			state_ ^= state_ << 5;
			return state_;
		}

	private:
		uint32_t state_;
	};

} // namespace scintillator
