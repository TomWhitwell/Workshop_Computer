// The "function" stage: six classic analog-computer shaping functions
// applied to the argument stage's output, selected by Knob Y's 6-way zone.
// Log and square-root units were real analog-computer building blocks
// (often literally a diode network); the differentiator entries reuse the
// sample-to-sample difference already needed by the Geiger gates elsewhere
// on this card, just scaled differently here.
//
// Nothing here calls into libm at audio rate. The RP2040 has no FPU, so
// logf() alone costs on the order of a thousand cycles against a ~20us
// (~2880 cycle) budget for the whole of ProcessSample; overrunning that
// desyncs the ADC mux rather than merely sounding bad. The log curve is
// therefore a table built once at startup, and the square root uses the
// integer sqrt in common.h.
#pragma once

#include <cmath>
#include <cstdint>

#include "common.h"

namespace scintillator
{

	class FunctionStage
	{
	public:
		FunctionStage()
		{
			// ln(|x|+1), normalised so full-scale in gives full-scale out.
			// +1 inside the log (rather than flooring |x| first) keeps the
			// curve continuous through the origin: x=0 gives 0, not a jump
			// up to some plateau. Flooring was a real bug - every |x| below
			// the floor mapped to the same output magnitude, so the sign of
			// x alone decided the output, and silence flickering across
			// zero became a loud square wave.
			const float scale = 2047.0f / logf(2049.0f);
			for (int i = 0; i <= kLogSteps; i++)
			{
				float mag = static_cast<float>(i * kLogStride);
				logTable_[i] = static_cast<int32_t>(logf(mag + 1.0f) * scale);
			}
		}

		// dxdt is the current sample's Differentiator::Update() result,
		// computed once per sample by the caller regardless of zone.
		int32_t __not_in_flash_func(Process)(int zone, int32_t x, int32_t dxdt) const
		{
			switch (zone)
			{
			case 0: // ln(|x|+1), sign of x preserved
			{
				int32_t mag = Cabs(x);
				if (mag > 2048) mag = 2048;
				int32_t i = mag / kLogStride;
				int32_t frac = mag - i * kLogStride;
				int32_t out = (logTable_[i] * (kLogStride - frac) + logTable_[i + 1] * frac) / kLogStride;
				return Clip(x < 0 ? -out : out);
			}

			case 1: // sqrt(|x|), sign preserved.
				// sqrt(m)*sqrt(2047) == sqrt(m*2047), so scaling to full
				// range falls out of one integer square root exactly.
			{
				int32_t out = IntSqrt(Cabs(x) * 2047);
				return Clip(x < 0 ? -out : out);
			}

			case 2: // x - no function
				return x;

			case 3: // x^2, always >= 0. >>11 (2048) rather than /2047 keeps
				// full-scale in at full-scale out to within 0.1%.
				return Clip((x * x) >> 11);

			case 4: // -dx/dt / 100 - a gentle, mostly-inaudible-until-fast
				// slope. 655/65536 is 1/100 to within 0.05%, and avoids a
				// division on the audio path.
				return Clip((-dxdt * 655) >> 16);

			case 5: // -dx/dt - the raw (large, clip-prone) slope
				return Clip(-dxdt);

			default:
				return 0;
			}
		}

	private:
		// 2048 covered in 256 steps of 8, plus a guard entry so the
		// interpolation can always read i+1.
		static constexpr int kLogSteps = 256;
		static constexpr int32_t kLogStride = 8;

		int32_t logTable_[kLogSteps + 2];
	};

	// A running sample-to-sample difference of the argument stage's output,
	// i.e. a discrete-time derivative. Shared by the two "-dx/dt" entries
	// above (they scale the same difference differently), and kept alive
	// every sample regardless of which function is selected, so switching
	// into either derivative entry never starts from a stale history.
	class Differentiator
	{
	public:
		int32_t __not_in_flash_func(Update)(int32_t x)
		{
			int32_t diff = x - last_;
			last_ = x;
			return diff;
		}

	private:
		int32_t last_ = 0;
	};

} // namespace scintillator
