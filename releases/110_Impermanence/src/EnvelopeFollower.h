#pragma once
#include <cstdint>

namespace imp
{

// CV Out 2: follows the loudness of the loop (before the reverb). Runs on
// core 0, every sample -- it's only a handful of operations.
//
// Rises quickly (~1ms) when the loop gets louder and falls slowly (~170ms)
// when it gets quieter -- the classic envelope-follower shape, so slice
// hits come out as usable envelopes for a VCA or filter.
//
// (CV Out 1's wandering voltage lives in Control.h, on core 1.)
class EnvelopeFollower
{
public:
	// Input: audio (12-bit). Returns CV output units, 0..2047 (~0..+6V).
	int32_t Process(int32_t in)
	{
		int32_t a = (in < 0 ? -in : in) << 8;
		if (a > env_)
			env_ += (a - env_) >> 6;
		else
			env_ -= (env_ - a) >> 13;
		int32_t out = env_ >> 7; // x2 gain: a 1024-peak loop reaches full scale
		return out > 2047 ? 2047 : out;
	}

private:
	int32_t env_ = 0; // Q8
};

} // namespace imp
