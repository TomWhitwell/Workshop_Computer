#include "ComputerCard.h"
#include <cmath>

/// Outputs sine wave at 440Hz

/// Uses an integer lookup table with linear interpolation, for speed.
/// At default clock rate of 125MHz, about 40 of these lookup-table
//  evaluations are possible in a 48kHz sample.

/// See (much simpler) sine_wave_float/ example for the same 440Hz sine
/// evaluated using floating-point arithmetic.

class SineWaveLookup : public ComputerCard
{
public:
	// 512-point (9-bit) lookup table
	// If memory was a concern we could reduce this to ~1/4 of the size,
	// by exploiting symmetry of sine wave, but this only uses 2KB of ~250KB on the RP2040
	constexpr static unsigned tableSize = 512;
	int16_t sine[tableSize];

	// Bitwise AND of index integer with tableMask will wrap it to table size
	constexpr static uint32_t tableMask = tableSize - 1;

	// Sine wave phase (0-2^32 gives 0-2pi phase range)
	uint32_t phase;
	
	SineWaveLookup()
	{
		// Initialise phase of sine wave to 0
		phase = 0;
		
		for (unsigned i=0; i<tableSize; i++)
		{
			// just shy of 2^15 * sin
			sine[i] = int16_t(32000*sin(2*i*M_PI/double(tableSize)));
		}

	}
	
	virtual void ProcessSample()
	{
		uint32_t index = phase >> 23; // convert from 32-bit phase to 9-bit lookup table index
		int32_t r = (phase & 0x7FFFFF) >> 7; // fractional part is last 23 bits of phase, shifted to 16-bit 

		// Look up this index and next index in lookup table
		int32_t s1 = sine[index];
		int32_t s2 = sine[(index+1) & tableMask];

		// Linear interpolation of s1 and s2, using fractional part
		// Shift right by 20 bits
		// (16 bits of r, and 4 bits to reduce 16-bit signed sine table to 12-bit output)
		int32_t out = (s2 * r + s1 * (65536 - r)) >> 20;

		AudioOut1(out);
		AudioOut2(out);
		
		// Want 440Hz sine wave
		// Phase is a 32-bit integer, so 2^32 steps
		// We will increment it at 48kHz, and want it to wrap at 440Hz
		// 
		// Increment = 2^32 * freq / samplerate
		//           = 2^32 * 440 / 48000
		//           = 39370533.54666...
		//          ~= 39370534
		phase += 39370534;
	}
};


int main()
{
	set_sys_clock_khz(144000, true);

	SineWaveLookup sw;
	sw.Run();
}

  
