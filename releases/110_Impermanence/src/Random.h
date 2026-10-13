#pragma once
#include <cstdint>

namespace imp
{

// Small, fast pseudo-random generator (xorshift32). Seeded once at power-on
// from the RP2040's hardware entropy, so no two sessions share a sequence --
// part of why a lost loop can't be recalled.
class Random
{
public:
	explicit Random(uint32_t seed = 0x9E3779B9u) { Seed(seed); }

	void Seed(uint32_t seed) { state_ = seed ? seed : 0x9E3779B9u; }

	uint32_t Next()
	{
		state_ ^= state_ << 13;
		state_ ^= state_ >> 17;
		state_ ^= state_ << 5;
		return state_;
	}

	// Uniform integer in [0, n), n up to 65536. Multiply-and-shift instead
	// of %, which the RP2040 has no fast instruction for.
	int32_t Below(int32_t n) { return (int32_t)(((Next() >> 16) * (uint32_t)n) >> 16); }

	// True with probability chance/4096 -- the same 0..4095 scale as a knob.
	bool Chance(int32_t chance) { return (int32_t)(Next() & 4095) < chance; }

private:
	uint32_t state_;
};

} // namespace imp
