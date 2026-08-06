#include "ComputerCard.h"
#include "quantiser.h"

#define SHIFT_REG_SIZE 6
#define RUNGLER_DAC_BITS 3
#define RUNGLER_DAC_CELLS 2
#define FWD 1
#define BACK 0

// 12 bit random number generator
uint32_t __not_in_flash_func(rnd12)()
{
	static uint32_t lcg_seed = 1;
	lcg_seed = 1664525 * lcg_seed + 1013904223;
	return lcg_seed >> 20;
}

/// BYOBenjolin
class BYOBenjolin : public ComputerCard
{
public:
	BYOBenjolin()
	{
		// Constructor sets up shift register
		for (int i = 0; i < SHIFT_REG_SIZE; i++)
		{
			bits[i] = 0;
		}
	}

	virtual void ProcessSample()
	{
		// Play a brief "random" twinkle animation on the LEDs at power-up,
		// before the normal rungler logic takes over.
		if (!startupAnimDone)
		{
			StartupAnimation();
			return;
		}

		//the outputs are empty integers that we will fill with binary values
		int16_t runglerOut1 = 0;
		int16_t runglerOut2 = 0;

		Switch sw = SwitchVal();

		//left pulse input rotates fwd/right and right pulse input rotates back/left
		fwdClock = PulseIn1RisingEdge();
		backClock = PulseIn2RisingEdge();

		//With a bit-depth of more than 1 in each cell of the shift register, having the lock knob at 0 doesn't "naturally" produce a stable output.
		//So this boolean forces stability in that position in order to make those familiar with Turing Machines feel at home.
		bool theIllusionOfStability = false;

		if (Connected(Input::Audio2))
		{
			// If AudioIn2 is connected, use it to control the Turing Probability
			// ComputerCard v0.3.0 inverts the audio input to correct the hardware
			// op-amp polarity; negate to preserve the original BYOB behaviour.
			turingP = KnobVal(Knob::Main) - AudioIn2();
		}
		else
		{
			// If AudioIn2 is not connected, use the main knob to control the Turing Probability
			turingP = KnobVal(Knob::Main);
		}

		clip(turingP, 0, 4095);

		// Virtual detentes for the Turing Probability knob
		if (turingP < 15)
		{
			turingP = 0;
			theIllusionOfStability = true;
		}
		else if (turingP > 4095 - 15)
		{
			turingP = 4095;
		}

		if (fwdClock)
		{

			rotate(bits, FWD);

			if (sw == Switch::Down)
			{
				//The equivalent of Turing Machines's write switch
				bits[0] = 0x3; // Always set the first bit to binary 11 = int 3 = hex 0x3
			}
			else if (sw == Switch::Up || theIllusionOfStability)
			{
				//The equivalent of the looping switch on a Rungler.
				bits[0] = ~bits[0] & 0x3; // Always Toggle the first bit
			}
			else // sw == Switch::Middle
			{
				if (Connected(Input::Audio1))
				{
					data = 2048 - AudioIn1(); // Convert to 0-4095 (audio input negated, see note above)
				}
				else
				{
					data = rnd12(); //default to noise as the data input, which mimics Turing Machine behavior
				}

				if (data > turingP)
				{
					bits[0] = ~data & 0x3; // Instead of just flipping the write bit, we are now fliping the entire 2-bit int
				}
			}

			calcOffset(); //offset and vca are S&H to the clock
			calcVca();
		}

		if (backClock)
		{
			rotate(bits, BACK);

			if (sw == Switch::Down)
			{
				bits[SHIFT_REG_SIZE - 1] = 0x0; // Always set the last bit to 0
			}
			else if (sw == Switch::Up || theIllusionOfStability)
			{
				bits[SHIFT_REG_SIZE - 1] = ~bits[SHIFT_REG_SIZE - 1] & 0x3; // Always Toggle the LAST bit
			}
			else // sw == Switch::Middle
			{
				if (Connected(Input::Audio1))
				{
					data = 2048 - AudioIn1(); // Convert to 0-4095 (audio input negated, see note above)
				}
				else
				{
					data = rnd12();
				}

				clip(data, 1, 4094);

				if (data > turingP)
				{
					bits[SHIFT_REG_SIZE - 1] = ~data & 0x3; //note the write bit is the LAST bit when clocking back/left. This is different than the fwd/right clocking.
				}
			}
			calcOffset();
			calcVca();
		}

		// Now we have a shift register full of bits, let's process them for output
		// Each output creates a 6 bit signal out of 3 2-bit cells (left and right on the LED screen)
		// Ie. for the right (channel 2) signal, if the shift register contains {0, 1, 2, 3, 0, 1} (in binary: 00, 01, 10, 11, 00, 01)
		// then we take the last 3 values and concatenate them to build a new six bit signal: binary 110001 which is 49 in decimal
		// for the left (channel 1) signal, we do the same but with the first 3 values in the shift register

		for (int i = 0; i < RUNGLER_DAC_BITS; i++)
		{
			runglerOut1 |= ((bits[i] << (2 * i)) & 0x3F);
		}

		for (int i = SHIFT_REG_SIZE - RUNGLER_DAC_BITS - 1; i < SHIFT_REG_SIZE; i++)
		{
			runglerOut2 |= ((bits[i] << (2 * (i - (SHIFT_REG_SIZE - RUNGLER_DAC_BITS)))) & 0x3F);
		}

		// convert 6 bit output to 12 bit values for DAC
		runglerOut1 = (runglerOut1 << (12 - (RUNGLER_DAC_BITS * RUNGLER_DAC_CELLS)));
		runglerOut2 = (runglerOut2 << (12 - (RUNGLER_DAC_BITS * RUNGLER_DAC_CELLS)));

		// Attenuate the output based on the vca value
		runglerOut1 = (runglerOut1 * vca) >> 12;
		runglerOut2 = (runglerOut2 * vca) >> 12;

		// Add offset to the outputs
		runglerOut1 += offset;
		runglerOut2 += offset;
		runglerOut1 -= 2048; // Convert to -2048 to 2047
		runglerOut2 -= 2048; // Convert to -2048 to 2047
		int16_t quantizedRunglerOut1 = runglerOut1; // save values for quantization
		int16_t quantizedRunglerOut2 = runglerOut2;
		// ComputerCard v0.3.0 inverts the audio output to correct the hardware
		// op-amp polarity, so the explicit "* -1" inversion is no longer needed
		// here to keep the original output polarity.
		//make sure the output values are in the correct range for the DAC
		clip(runglerOut1, -2048, 2047);
		clip(runglerOut2, -2048, 2047);
		clip(quantizedRunglerOut1, -2048, 2047);
		clip(quantizedRunglerOut2, -2048, 2047);

		// Quantize the output to the nearest MIDI note for our CV outputs
		quantizedRunglerOut1 = quantSample(quantizedRunglerOut1);
		quantizedRunglerOut2 = quantSample(quantizedRunglerOut2);

		// Output rungler signal
		AudioOut1(runglerOut1);
		AudioOut2(runglerOut2);

		// Output Quantized CV signals
		CVOut1MIDINote(quantizedRunglerOut1);
		CVOut2MIDINote(quantizedRunglerOut2);

		// Output pulse signals based on the bottom bit of each channel
		PulseOut1(bits[SHIFT_REG_SIZE - 4] & 0x1);
		PulseOut2(bits[SHIFT_REG_SIZE - 1] & 0x1);

		// show shiftreg state on LEDs
		// each LED shows a 2-bit value from the 6 step shift register
		// The first 3 LEDs show the first 3 bits of the shift register (left channel)
		// The last 3 LEDs show the last 3 bits of the shift register (right channel)
		// With 2-bit signals we end up with 4 brightness levels (and possible values) for each LED:
		for (int i = 0; i < 6; i++)
		{
			LedBrightness(ledMap[i], (bits[i] << 10) * vca >> 12);
		}
	}

private:
	int16_t bits[SHIFT_REG_SIZE];
	bool fwdClock = false;
	bool backClock = false;
	int16_t turingP;
	int16_t data;
	int16_t vca = 0;
	int8_t ledMap[SHIFT_REG_SIZE] = {0, 2, 4, 1, 3, 5};
	int16_t offset = 0;

	// Startup LED animation state
	bool startupAnimDone = false;
	uint32_t animCounter = 0;
	uint16_t animLed[SHIFT_REG_SIZE] = {0};
	static const uint32_t kStartupAnimFrames = 48000 * 2; // ~2 seconds at 48kHz

	// Random twinkle: each visible frame, fade all LEDs and randomly re-spark some.
	void StartupAnimation()
	{
		// Update visuals every 1024 samples (~47Hz) so the animation is visible.
		if ((animCounter & 0x3FF) == 0)
		{
			for (int i = 0; i < SHIFT_REG_SIZE; i++)
			{
				animLed[i] = (animLed[i] * 13) >> 4; // decay to ~81%
				if (rnd12() < 512)                   // ~12.5% chance to spark
				{
					animLed[i] = 4095;
				}
			}
			for (int i = 0; i < SHIFT_REG_SIZE; i++)
			{
				LedBrightness(ledMap[i], animLed[i]);
			}
		}

		if (++animCounter >= kStartupAnimFrames)
		{
			startupAnimDone = true;
			for (int i = 0; i < SHIFT_REG_SIZE; i++)
			{
				LedBrightness(ledMap[i], 0);
			}
		}
	}

	void rotate(int16_t *array, bool direction)
	{
		if (direction) // Rotate right
		{
			int16_t lastBit = array[SHIFT_REG_SIZE - 1];

			for (int i = SHIFT_REG_SIZE - 1; i > 0; i--)
			{
				array[i] = array[i - 1];
			}
			array[0] = lastBit;
		}
		else // Rotate left
		{
			int16_t firstBit = array[0];

			for (int i = 0; i < SHIFT_REG_SIZE - 1; i++)
			{
				array[i] = array[i + 1];
			}
			array[SHIFT_REG_SIZE - 1] = firstBit;
		}
	}

	void calcOffset()
	{
		if (Connected(Input::CV1))
		{
			offset = CVIn1() * (KnobVal(Knob::X) - 2048) >> 12;
			offset += 2048; // Convert to 0-4095
		}
		else
		{
			offset = KnobVal(Knob::X);
		}
	}

	void calcVca()
	{
		if (Connected(Input::CV2))
		{
			vca = CVIn2() * KnobVal(Knob::Y) >> 12;
			;
			clip(vca, 0, 2048);
			vca <<= 1; // Convert to 0-4095
		}
		else
		{
			vca = KnobVal(Knob::Y);
		}
	}

	void clip(int16_t &value, int16_t min, int16_t max)
	{
		if (value < min)
		{
			value = min;
		}
		else if (value > max)
		{
			value = max;
		}
	}
};

int main()
{
	BYOBenjolin byoBenj;
	byoBenj.EnableNormalisationProbe();
	byoBenj.Run();
}
