#pragma once
#include <cstdint>

namespace tem
{

// Output limiter for the DAC's +/-2047: unchanged up to 3/4 of full scale,
// then squashed 4:1, then a hard stop. (From Impermanence, via Hubris and
// Recurrence.)
inline int32_t SoftClip(int32_t x)
{
	int32_t a = x < 0 ? -x : x;
	if (a > 1536)
	{
		a = 1536 + ((a - 1536) >> 2);
		if (a > 2047)
			a = 2047;
	}
	return x < 0 ? -a : a;
}

// v x a Q15 gain below 1, rounded towards zero, so every pass round a
// feedback loop is strictly smaller and a tail can't hang on at -1.
inline int32_t ScaleQ15(int32_t v, int32_t gQ15)
{
	int32_t r = v * gQ15;
	return (r + ((r >> 31) & 32767)) >> 15;
}

inline int32_t Sat16(int32_t v) { return v > 32767 ? 32767 : (v < -32768 ? -32768 : v); }

} // namespace tem
