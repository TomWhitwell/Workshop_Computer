#ifndef ANALOGUE_TO_MIDI_H
#define ANALOGUE_TO_MIDI_H

// Class that converts a signal in the range min_ to max_ into 7-bit values 0-127
// No filtering, but uses hysteresis to avoid jitter (so long as noise is less than 1/128 of range)
class AnalogueToMIDI
{
public:
	AnalogueToMIDI(int32_t minval_, int32_t maxval_)
	{
		minval = minval_;
		maxval = maxval_;
		currentMIDIValue = 0;
		previousMIDIValue = 0;
	}

	void operator()(int32_t in)
	{
		// Rescale input from 0 to 65535
		if (maxval == minval)
		{
			in = maxval;
		}
		else
		{
			in = ((in - minval) << 16) / (maxval - minval);
		}
		if (in < 0)
		{
			in = 0;
		}
		if (in > 65535)
		{
			in = 65535;
		}

		// For 0-127, naive mapping to MIDI is (in >> 9)
		// 0-511 -> 0
		// 512-1023 -> 0
		// etc.
		// Extend this by half a window (256) in either direction for hysteresis
		// and update MIDI output value if outside this window.
		int32_t windowMax = ((currentMIDIValue + 1) << 9) + 255;
		int32_t windowMin = (currentMIDIValue << 9) - 256;

		if (in > windowMax || in < windowMin)
		{
			currentMIDIValue = in >> 9;
		}
	}
	int8_t GetMIDIValueIfNew()
	{
		if (currentMIDIValue != previousMIDIValue)
		{
			previousMIDIValue = currentMIDIValue;
			return currentMIDIValue;
		}
		else
		{
			return -1;
		}
	}
private:
	int32_t minval, maxval;
	volatile int8_t currentMIDIValue;
	int8_t previousMIDIValue;
};

#endif
