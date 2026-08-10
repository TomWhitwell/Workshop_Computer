#include "ComputerCard.h"
#include "click.h"
#include "quantiser.h"

void tempTap();
void longHold();

class Fifths : public ComputerCard
{
public:
	// tonics on the circle of fifths starting from Gb all the way to F#,
	// always choosing an octave with a note as close as possible to the key center (0)
	constexpr static int8_t circle_of_fifths[13] = {-6, 1, -4, 3, -2, 5, 0, -5, 2, -3, 4, -1, 6};

	int8_t all_keys[13][12];

	uint32_t sampleCounter;
	uint32_t quarterNoteMs;
	uint32_t quarterNoteSamples = 12000;
	Click tap = Click(tempTap, longHold);
	bool tapping = false;
	bool switchHold = false;
	bool resync = false;
	bool pulse = false;
	bool pulse_int = false;
	bool pulseSeq = false;
	bool changeX = false;
	bool changeY = false;
	uint32_t tapTime = 0;
	uint32_t tapTimeLast = 0;
	int16_t counter = 0;
	int16_t pulseSeqCounter = 0;
	int16_t pulseIntCounter = 0;

	int16_t vcaOut;
	int16_t vcaCV;

	int16_t quantisedNote;
	int32_t quantizedAmbigThird;

	bool looping;

	int16_t buffer[12];
	bool pulseBuffer[12];

	int8_t looplength = 12;
	int8_t loopindex = 0;
	int8_t pulseindex = 0;

	int16_t lastX;
	int16_t lastY;

	int16_t mainKnob;
	int16_t vcaKnob;
	int16_t xKnob;

	int16_t pulseDuration = 200;
	int16_t thresh = 2048;

	Fifths()
	{
		// Constructor

		sampleCounter = 0;

		// set up scale matrix

		for (int i = 0; i < 13; i++)
		{
			populateScale(circle_of_fifths[i]);

			for (int j = 0; j < 12; j++)
			{
				all_keys[i][j] = scale[j];
			}
		}

		looping = SwitchVal() == Switch::Middle;

		for (int i = 0; i < 12; i++)
		{
			buffer[i] = (rnd12() - 2048) / 4;
			pulseBuffer[i] = rnd12() > 2048;
		}
	}

	// Helper: Calculate VCA output based on input connections and knob/CV values
	int16_t calculateVCAOut(int16_t input)
	{
		if (Connected(Input::Audio2))
		{
			// If Audio2 is connected, treat as CV input
			// vcaCV = virtualDetentedKnob((AudioIn2() * vcaKnob >> 12) + 2048) - 2048;
			// ComputerCard v0.3.0 inverts the audio input; negate to preserve original sign.
			vcaCV = -AudioIn2() * virtualDetentedKnob(vcaKnob) >> 12;
		}
		else
		{
			vcaCV = virtualDetentedKnob(KnobVal(Knob::Y));
		}

		if (Connected(Input::Audio1) && Connected(Input::Audio2))
		{
			return input * vcaCV >> 11;
		}
		else if (Connected(Input::Audio1))
		{
			return input * vcaKnob >> 12;
		}
		else if (Connected(Input::Audio2))
		{
			return input * vcaCV >> 11;
		}
		else
		{
			return input * vcaKnob >> 12;
		}
	}

	// Helper: Update pulse and counter logic
	void updatePulseAndCounters(Switch sw)
	{
		looping = !(sw == Switch::Up);
		if (PulseIn2())
		{
			looping = !looping;
		}

		// Timing and tap update
		tap.Update(SwitchVal() == Switch::Down);
		sampleCounter += 1;
		sampleCounter %= 0xFFFFFFFF;

		if (resync)
		{
			resync = false;
			sampleCounter = 0;
			counter = 0;
			pulseSeqCounter = 0;
		}

		if (Connected(Input::Pulse1))
		{
			pulse = PulseIn1RisingEdge();
		}
		else
		{
			pulse = sampleCounter % quarterNoteSamples == 0;
		}

		pulse_int = sampleCounter % quarterNoteSamples == 0; // internal pulse continues even if external pulse is present

		if (pulse && !counter)
		{
			counter = pulseDuration;
		}

		if (pulse_int && !pulseIntCounter)
		{
			pulseIntCounter = pulseDuration;
		}

		if (pulseSeqCounter)
		{
			pulseSeqCounter--;
		}

		if (counter)
		{
			counter--;
		}

		if (pulseIntCounter)
		{
			pulseIntCounter--;
		}
	}

	// Helper: Update switch and tap state
	void updateSwitchAndTapStates(Switch sw)
	{
		tapTimeLast = (pico_millis() - tapTime) % 0xFFFFFFFF;

		if (tapTimeLast > 2000 && tapping)
		{
			tapping = false;
		}

		if (switchHold && (tapTimeLast > 1000) && sw != Switch::Down)
		{
			switchHold = false;
			changeX = false;
			changeY = false;
		}
	}

	// Helper: Process quantizer logic on pulse
	void processQuantizerStep()
	{
		int8_t key_index;

		if (Connected(Input::CV2))
		{
			key_index = CVIn2() * 13 >> 12;

			key_index += 7;

			if (key_index < 0)
			{
				key_index += 13;
			}
			else if (key_index > 12)
			{
				key_index -= 13;
			}
		}
		else
		{
			key_index = KnobVal(Knob::Main) * 13 >> 12;
		}

		looplength = virtualDetentedKnob(KnobVal(Knob::X)) * 12 >> 12; // 0 - 11
		looplength = looplength + 1; // 1 - 12

		int16_t quant_input;

		if (looping)
		{
			quant_input = buffer[loopindex];
		}
		else
		{
			quant_input = vcaOut;
			buffer[loopindex] = quant_input;
		}

		if (Connected(Input::CV1)){
			quant_input += CVIn1() * 1365 >> 12; // CV1 is up to 2 octaves transpose
		}

		clip(quant_input);
		quantisedNote = quantSample(quant_input, all_keys[key_index]);
		quantizedAmbigThird = calculateAmbigThird(quantisedNote, key_index);
		CVOut1MIDINote(quantisedNote);
		CVOut2MIDINote(quantizedAmbigThird);
		loopindex = loopindex + 1;

		if (loopindex >= looplength)
		{
			loopindex = 0;
		}
	}

	// helper: Process pulse sequencer on internal pulse
	void processPulseSeq()
	{
		if (looping)
		{
			if (pulseBuffer[pulseindex] && !pulseSeqCounter)
			{
				pulseSeqCounter = pulseDuration;
			}
		}
		else
		{
			if (rnd12() < thresh)
			{
				pulseBuffer[pulseindex] = true;
				pulseSeqCounter = pulseDuration;
			}
			else
			{
				pulseBuffer[pulseindex] = false;
			}
		}

		pulseindex = pulseindex + 1;

		if (pulseindex >= looplength)
		{
			pulseindex = 0;
		}
	}

	virtual void ProcessSample() override
	{
		// Main audio/CV processing loop
		// 1. Update switch and tap states
		Switch sw = SwitchVal();
		updateSwitchAndTapStates(sw);

		// 4. Quantizer step on pulse
		if (pulse)
		{
			processQuantizerStep();
		}

		// 4.5 Update pulse sequencer
		if (pulse_int)
		{
			processPulseSeq();
		}

		PulseOut1(pulseIntCounter > 0);
		PulseOut2(pulseSeqCounter > 0);

		// 2. Update pulse and counters
		updatePulseAndCounters(sw);

		// 3. VCA calculation
		// ComputerCard v0.3.0 inverts the audio input (op-amp correction); negate to preserve behaviour.
		int16_t input = Connected(Input::Audio1) ? -AudioIn1() + 25 : (rnd12() - 2048); // DC offset for non-callibrated input
		mainKnob = virtualDetentedKnob(KnobVal(Knob::Main));
		vcaKnob = virtualDetentedKnob(KnobVal(Knob::Y));
		xKnob = virtualDetentedKnob(KnobVal(Knob::X));
		vcaOut = calculateVCAOut(input);
		clip(vcaOut);
		// ComputerCard v0.3.0 inverts the audio output; negate to preserve original output voltage.
		AudioOut1(2048 - mainKnob);
		AudioOut2(-vcaOut);

		// 5. Handle switchHold (shift functions) for knobs
		if (switchHold && ((cabs(xKnob - lastX) > 0) || changeX))
		{
			changeX = true;
			pulseDuration = xKnob * quarterNoteSamples >> 12;
			pulseDuration = pulseDuration + 20; // to avoid 0 duration
		}
		if (switchHold && (cabs(vcaKnob - lastY) > 0 || changeY))
		{
			changeY = true;
			thresh = vcaKnob;
			thresh++; // to avoid 0 duration
		}

		// 6. Output pulses and LED feedback
		LedBrightness(0, cabs(2048 - mainKnob) * 4095 >> 12);
		LedBrightness(1, cabs(vcaOut) * 4095 >> 12);
		LedBrightness(2, quantisedNote << 4);
		LedBrightness(3, quantizedAmbigThird << 4);
		LedOn(4, pulseIntCounter > 0);
		LedOn(5, pulseSeqCounter > 0);
		lastX = xKnob;
		lastY = vcaKnob;
	}

private:
	// RNG! Different values for each card but the same on each boot
	uint32_t __not_in_flash_func(rnd12)()
	{
		static uint32_t lcg_seed = 1;
		lcg_seed ^= UniqueCardID() >> 20;
		lcg_seed = 1664525 * lcg_seed + 1013904223;
		return lcg_seed >> 20;
	}

	int8_t *scale = new int8_t[12];

	void populateScale(int8_t tonic)
	{
		int8_t whole = 2;
		int8_t half = 1;
		int8_t temp[12] = {
			tonic,
			tonic,
			tonic + whole,
			tonic + whole,
			tonic + whole + whole,
			tonic + whole + whole,
			tonic + whole + whole + half,
			tonic + whole + whole + half + whole,
			tonic + whole + whole + half + whole,
			tonic + whole + whole + half + whole + whole,
			tonic + whole + whole + half + whole + whole,
			tonic + whole + whole + half + whole + whole + whole};
		for (int i = 0; i < 12; i++)
		{
			scale[i] = temp[i];
		}
	}

	int8_t calculateAmbigThird(int8_t input, int8_t key_index)
	{
		int8_t octave = input / 12;

		for (int i = 0; i < 12; i++)
		{
			if (input + 3 == 12 * octave + all_keys[key_index][i] || input + 3 == 12 * (octave + 1) + all_keys[key_index][i]) // check for minor third in this or next octave
			{
				return input + 3;
			}
		};

		return input + 4; // otherwise return major third
	}

	int16_t virtualDetentedKnob(int16_t val)
	{
		if (val > 4079)
		{
			val = 4095;
		}
		else if (val < 16)
		{
			val = 0;
		}

		if (cabs(val - 2048) < 16)
		{
			val = 2048;
		}

		return val;
	}

	int32_t cabs(int32_t a)
	{
		return (a > 0) ? a : -a;
	}

	void clip(int16_t &val)
	{
		if (val > 2047)
		{
			val = 2047;
		}
		else if (val < -2048)
		{
			val = -2048;
		}
	}
};

// define this here so that it can be used in the click library
// There is probably a better way to do this but I don't know it
Fifths card;

int main()
{
	card.EnableNormalisationProbe();
	card.Run();
}

// Callbacks for click library to call based on Switch state
void tempTap()
{
	uint32_t currentTime = pico_millis();

	if (!card.tapping)
	{
		card.tapTime = currentTime;
		card.tapping = true;
	}
	else
	{
		uint32_t sinceLast = (currentTime - card.tapTime) & 0xFFFFFFFF; // Handle overflow
		
		if (sinceLast > 20 && sinceLast < 3000)
		{								// Ignore bounces and forgotten taps > 3 seconds
			card.tapTime = currentTime; // Record time ready for next tap
			card.quarterNoteSamples = sinceLast * 48;
			card.resync = true;
			card.pulse = true;
		}
	}
}

void longHold()
{
	card.switchHold = true;
}
