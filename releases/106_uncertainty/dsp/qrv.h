// Quantized Random Voltage: on each trigger, latch a brand new random
// value and hold it — a classic sample-and-hold noise source, the stepped
// counterpart to FRV's continuous drift. No slew: each new value is a hard
// jump, which is the point (it's meant to sound "quantized" in time, not
// in pitch).

#ifndef UNCERTAINTY_DSP_QRV_H_
#define UNCERTAINTY_DSP_QRV_H_

#include <cstdint>

#include "noise.h"

namespace uncertainty
{

	class QRV
	{
	public:
		explicit QRV(uint32_t seed = 12345) : seed_(seed ? seed : 12345) {}

		// Call once per sample. trigger: fire a new random value this
		// sample. rangeMillivolts: current maximum output, 0..6000 (set by
		// the Y knob) — each new value is uniform over [0, rangeMillivolts].
		// Returns the held output in millivolts, 0..6000.
		int32_t Process(bool trigger, int32_t rangeMillivolts)
		{
			if (trigger)
			{
				// 12-bit uniform draw, scaled into the current range.
				uint32_t draw = Xorshift32(seed_) & 0x0FFF; // 0..4095
				current_ = static_cast<int32_t>((draw * static_cast<uint32_t>(rangeMillivolts)) >> 12);
			}
			return current_;
		}

	private:
		uint32_t seed_;
		int32_t current_ = 0;
	};

} // namespace uncertainty

#endif // UNCERTAINTY_DSP_QRV_H_
