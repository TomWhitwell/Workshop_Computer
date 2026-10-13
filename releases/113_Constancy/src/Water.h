#pragma once
#include <cstdint>
#include "Dsp.h"
#include "Params.h"

namespace constancy
{

// Water (Y), on core 0: the last thing before the outputs, a lowpass and a
// low-cut, CS-80 style, each two one-pole stages (12dB per octave). Core 1
// sets them from the knob: in the centre both are open; to the left the
// lowpass closes and the sound drowns; to the right the low-cut rises and
// it thins.
//
// Only one of the two is ever working, so only that one runs. Each fades
// in from nothing (`mix`, slid smoothly by the caller) as the knob leaves
// the centre, and one is only swapped for the other once its fade is back
// at nothing -- so crossing the centre never clicks.
struct Water
{
	int32_t mode = kWaterOpen;
	int32_t lp[4] = {}, hp[4] = {};

	__attribute__((always_inline)) void Process(int32_t &l, int32_t &r, const EngineParams &p, int32_t mix)
	{
		// Open has nothing to fade out, so it gives way at once; a working
		// filter only once its fade is back at nothing.
		if (p.waterMode != mode && (mode == kWaterOpen || mix == 0))
		{
			mode = p.waterMode;
			// A lowpass starting at the input, and a low-cut whose memory of
			// the lows starts at zero: both pick up from where the sound is.
			lp[0] = lp[1] = Clamp16(l);
			lp[2] = lp[3] = Clamp16(r);
			hp[0] = hp[1] = hp[2] = hp[3] = 0;
		}
		if (mix == 0)
			return;
		int32_t c = p.waterCoef;
		l = Clamp16(l);
		r = Clamp16(r);
		if (mode == kWaterLowpass)
		{
			// Blend from the sound itself towards the lowpass.
			int32_t fl = OnePole(lp[1], OnePole(lp[0], l, c), c);
			int32_t fr = OnePole(lp[3], OnePole(lp[2], r, c), c);
			l += ((fl - l) * mix) >> 12;
			r += ((fr - r) * mix) >> 12;
		}
		else if (mode == kWaterHighpass)
		{
			// Take away `mix` of the lows the low-cut finds, twice.
			l = LowCut(hp[1], LowCut(hp[0], l, c, mix), c, mix);
			r = LowCut(hp[3], LowCut(hp[2], r, c, mix), c, mix);
		}
	}

private:
	// One low-cut stage: a lowpass follows the lows and `mix` (Q12) of them
	// is taken away. The lowpass runs at 4x the signal with a Q13
	// coefficient, so it can follow slow changes finely (a Q15 lowpass at
	// 20Hz would move in coarse steps). Cutoffs up to about 3kHz.
	__attribute__((always_inline)) static int32_t LowCut(int32_t &state, int32_t in, int32_t coefQ15, int32_t mix)
	{
		int32_t x = in * 4;
		int32_t d = x - state;
		state += ((d * (coefQ15 >> 2)) >> 13) + (d > 0 ? 1 : 0);
		return in - ((state * mix) >> 14);
	}
};

} // namespace constancy
