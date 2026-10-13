#include "ComputerCard.h"
#include <cmath>

/// 1V/oct VCO using calibrated audio input and sine wave lookup table.
///
/// AudioIn1 controls pitch: 0V = A4 (440Hz), +1V = A5 (880Hz), -1V = A3 (220Hz).
/// LED 5 lights if the audio inputs have not been calibrated.

class CalibratedInput : public ComputerCard
{
public:
	// 512-point sine wave lookup table
	constexpr static unsigned sineTableSize = 512;
	constexpr static uint32_t sineMask = sineTableSize - 1;
	int16_t sineTable[sineTableSize];

	// Phase increment table covering one octave (440Hz to 880Hz), one entry per millivolt.
	// At runtime: look up the fractional-millivolt part, then shift by octave count.
	constexpr static unsigned octaveTableSize = 1000;
	uint32_t octaveTable[octaveTableSize];

	uint32_t phase = 0;

	CalibratedInput()
	{
		for (unsigned i = 0; i < sineTableSize; i++)
		{
			sineTable[i] = int16_t(32000 * sinf(2 * i * M_PI / float(sineTableSize)));
		}

		// phaseIncrement = 2^32 * freq / sampleRate
		// freq = 440 * 2^(i/1000) for i in [0, 999]
		for (unsigned i = 0; i < octaveTableSize; i++)
		{
			octaveTable[i] = uint32_t(float(1ULL << 32) * 440.0f * powf(2.0f, i / 1000.0f) / 48000.0f);
		}
	}

	virtual void ProcessSample()
	{
		int32_t mv = AudioIn1Millivolts();
		CVOut1Millivolts(mv);
		CVOut2Millivolts(mv);
		
		// Floor-divide into octave offset and mV-within-octave [0, 999]
		int32_t octave = mv / 1000;
		int32_t frac   = mv % 1000;
		if (frac < 0) { frac += 1000; octave -= 1; }

		// Clamp: keeps frequency in audible range and prevents uint32_t overflow
		if (octave < -6) octave = -6;
		if (octave >  5) octave =  5; // 440 * 2^5 = 14080Hz, safely below Nyquist

		uint32_t inc = octaveTable[frac];
		if (octave >= 0)
		{
			inc <<= octave;
		}
		else
		{
			inc >>= (-octave);
		}

		phase += inc;

		// Sine lookup with linear interpolation (same technique as sine_wave_lookup example)
		uint32_t index = phase >> 23;
		int32_t  r     = (phase & 0x7FFFFF) >> 7;
		int32_t  s1    = sineTable[index & sineMask];
		int32_t  s2    = sineTable[(index + 1) & sineMask];
		int32_t  out   = (s2 * r + s1 * (65536 - r)) >> 20;

		AudioOut1(out);
		AudioOut2(out);

		LedOn(5, !InputsCalibrated());
	}
};

int main()
{
	set_sys_clock_khz(144000, true);

	CalibratedInput ci;
	ci.Run();
}
