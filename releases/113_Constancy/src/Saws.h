#pragma once
#include <cstdint>
#include "Params.h"

namespace constancy
{

// One sawtooth wave, on core 0: a phase that climbs steadily and wraps
// round, read as a ramp from -16384 to +16384 (half of full scale, so
// several can be added together).
//
// A plain digital saw drops from top to bottom in a single sample, and
// that perfect cliff has harmonics far above what 48kHz can hold: they
// fold back down as inharmonic whistles (aliasing), worst on high notes.
// PolyBLEP rounds the cliff off over the sample either side of it with a
// small parabola, which removes most of the fold-back for the cost of a
// couple of multiplies -- and only on the two samples nearest the cliff.
// (Välimäki and Huovilainen's method, in whole numbers.)
//
// `rinc` is 2^31 / (inc >> 8), worked out on core 1, so finding how far
// through its sample the cliff falls (phase / inc) is a multiply rather
// than a divide.
__attribute__((always_inline)) inline int32_t Saw(uint32_t &phase, const SawParams &p)
{
	uint32_t inc = p.inc;
	uint32_t ph = phase + inc;
	phase = ph;
	// Flipping the top bit makes the wrap (2^32 -> 0) the cliff.
	int32_t s = (int32_t)(ph ^ 0x80000000u) >> 17;
	if (ph < inc)
	{
		// Just past the cliff: t is how far past, as a fraction of a sample
		// (Q15). Lift the bottom of the cliff by (1 - t)^2.
		int32_t t = (int32_t)(((ph >> 8) * p.rinc) >> 16);
		int32_t d = 32768 - t;
		s += (d * d) >> 16;
	}
	else if (ph > ~inc)
	{
		// Just before it: lower the top by (1 - u)^2.
		int32_t u = (int32_t)(((~ph >> 8) * p.rinc) >> 16);
		int32_t d = 32768 - u;
		s -= (d * d) >> 16;
	}
	return s;
}

// A stack of N saws: the supersaw. Saw 0 goes to both sides; odd saws go
// left, even ones right, so the detuned saws spread across the stereo
// field. Adds into l and r.
template <int N>
struct SawStack
{
	uint32_t phase[N] = {};

	__attribute__((always_inline)) void Process(const SawParams *p, int32_t &l, int32_t &r)
	{
		int32_t s = Saw(phase[0], p[0]);
		l += s;
		r += s;
		for (int i = 1; i < N; i++)
		{
			s = Saw(phase[i], p[i]);
			if (i & 1)
				l += s;
			else
				r += s;
		}
	}
};

} // namespace constancy
