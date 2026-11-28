#pragma once
#include <cstdint>

namespace ab
{

// Small, fast pseudo-random generator (xorshift32), from Impermanence and
// Equanimity. Everything random in the card -- new pieces, drift, the random
// CVs, the tape hiss -- comes from one of these.
class Random
{
public:
	explicit Random(uint32_t seed = 0x9E3779B9u) { Seed(seed); }

	// xorshift never leaves zero, so zero is swapped for a fixed seed.
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

private:
	uint32_t state_;
};

} // namespace ab
