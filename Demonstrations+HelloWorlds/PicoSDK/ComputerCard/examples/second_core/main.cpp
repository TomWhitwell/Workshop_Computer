#include "ComputerCard.h"
#include "pico/multicore.h"
#include <cmath>

/*

Multicore processing example, showing one way of generating CV signals with
code that takes longer than a sample time at 48kHz.

ComputerCard's fixed 48kHz clock means that these routines can't be called
in ProcessSample(), but we can set up a loop on the second core of the RP2040
to run the slower code, and use the ProcessSample function only to update the
jacks when the slower routine has produced a new value.


In this example, an LFO calculated from the exponential of a sum of sine waves
is calculated on the second core taking at about 12kHz, roughly four times slower
than the audio samples. This is fast enough for an LFO, but aliases heavily if 
pushed much into audio rate.


In this example, the ProcessSample function does no interpolation - only a 
zero-order hold of the last value from the second core calculation.

A more sophisticated example would use linear (or better) interpolation of
these (non-equally-spaced) values.



User interface:
---------------
 
Main knob + CV in 1:    Speed
Knob X:                 LFO shape
Both audio and CV outs: LFO output

 */

constexpr float twopi = (float)M_TWOPI;

class SecondCore : public ComputerCard
{
	// Variables for communication between the two cores
	volatile int16_t out; // output from slow core to 48kHz core.
	volatile uint32_t sampleCount; // count of number of audio samples per 'slow core' loop
	
public:
	SecondCore()
	{
		out = 0;
		sampleCount = 0;
		// Start the second core
		multicore_launch_core1(core1);
	}

	// Boilerplate to call member function as second core
	static void core1()
	{
		((SecondCore *)ThisPtr())->SlowProcessingCore();
	}

	
	// Code for second RP2040 core, blocking
	void SlowProcessingCore()
	{
		float phase = 0.0f;
		
		while (1)
		{
			// Calculate a wobbly LFO with exp(sum of some sine waves)
			
			// This loop takes longer than one sample time at 48kHz, so can't
			// be run in ProcessSample
			
			float wobbles =
				  0.5f * sinf(5 * phase)
				+ 0.25f * sinf(2 * phase)
				+ 0.125f * sinf(3 * phase)
				+ 0.0625f * sinf(4 * phase);
			
			// exponentiate a sum of sines with coefficients set by knob X
			float outf = expf(sinf(phase) + wobbles * (KnobVal(Knob::X)/4096.0f));
			
			out = int16_t(outf * 270); // Copy output over to signed 12-bit integer value

			// Count how many audio samples passed since last loop
			uint32_t samplesPassed = sampleCount;
			sampleCount -= samplesPassed; 

			// dt is time elapsed, in seconds, since last loop
			float dt = samplesPassed/48000.0f;

			// Read knob and CV to get LFO speed
			float speed = expf((KnobVal(Knob::Main) + CVIn1()) * 0.002f - 1.0f);
				
			// increment phase appropriately, and wrap
			phase += speed * dt;
			if (phase > twopi)
			{
				phase -= twopi;
			}

			// For debugging, turn on LEDs to get some indication of how many
			// audio samples at 48kHz are taken by each loop of this routine
			LedOn(0, samplesPassed & 0x01); // Top left LED: up to 1 sample
			LedOn(1, samplesPassed & 0x02); // Top right LED: up to 3 samples
			LedOn(2, samplesPassed & 0x04); // Middle left LED: up to 7 samples
			LedOn(3, samplesPassed & 0x08); // Middle right LED: up to 15 samples
			LedOn(4, samplesPassed & 0x10); // Bottom left LED: up to 31 samples
			LedOn(5, samplesPassed & 0x20); // Bottom right LED: up to 63 samples
		}
	}

	
	// 48kHz audio processing function
	virtual void ProcessSample()
	{
		// Simply copy output of LFO calculation to the four analogue outputs
		CVOut1(out);
		CVOut2(out);
		AudioOut1(out);
		AudioOut2(out);

		// Increment sample count to provide time reference independent
		// of the execution 
		sampleCount++;
	}
};


int main()
{
	set_sys_clock_khz(144000, true);

	SecondCore sc;
	sc.Run();
}

  
