#pragma once
#include <cstdint>
#include "DayData.h"

namespace constancy
{

// The morning at one moment: every stream 0..4096 (see DayData.h).
struct Morning
{
	int32_t sun = 0;
	int32_t tide = 0;
	int32_t tideRate = 0;
	int32_t wind = 0;
	int32_t cloud = 0;
};

// The furthest table position, x 256.
constexpr int32_t kDayEndQ8 = (kDayPoints - 1) << 8;

// The morning at a table position (x 256), blending smoothly between the
// two nearest points so nothing steps as Time moves.
inline Morning MorningAt(int32_t posQ8)
{
	if (posQ8 < 0)
		posQ8 = 0;
	if (posQ8 > kDayEndQ8)
		posQ8 = kDayEndQ8;
	int32_t i = posQ8 >> 8, f = posQ8 & 255;
	const DayPoint &a = kDay[i];
	const DayPoint &b = kDay[i + 1 < kDayPoints ? i + 1 : i];
	auto mix = [f](int32_t x, int32_t y) { return x + (((y - x) * f) >> 8); };
	Morning m;
	m.sun = mix(a.sun, b.sun);
	m.tide = mix(a.tide, b.tide);
	m.tideRate = mix(a.tideRate, b.tideRate);
	m.wind = mix(a.wind, b.wind);
	m.cloud = mix(a.cloud, b.cloud);
	return m;
}

} // namespace constancy
