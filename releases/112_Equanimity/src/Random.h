#pragma once
#include <cstdint>

namespace eq
{

// Small, fast pseudo-random generator (xorshift32), from Impermanence.
// Seeded at power-on from the RP2040's hardware entropy, so every session
// is new. All of the card's randomness flows through one of these.
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

	// Uniform integer in [lo, hi].
	int32_t Between(int32_t lo, int32_t hi) { return lo + Below(hi - lo + 1); }

	// True with probability chance/4096.
	bool Chance(int32_t chance) { return (int32_t)(Next() & 4095) < chance; }

	// True with probability chance/16777216: for rare events checked every
	// millisecond, like a note birth or a drone move.
	bool Chance24(int32_t chance) { return (int32_t)(Next() >> 8) < chance; }

private:
	uint32_t state_;
};

} // namespace eq
