#include "ComputerCard.h"

/// Passthrough
/// Connects audio, CV and pulse inputs directly to corresponding outputs
/// Also displays switch position and knob values on LEDs
class Passthrough : public ComputerCard
{
public:

	// ProcessSample is called for every sample, at 48kHz
	virtual void ProcessSample()
	{
		// Transfer audio/CV/Pulse inputs directly to outputs
		AudioOut1(AudioIn1());
		AudioOut2(AudioIn2());

		CVOut1(CVIn1());
		CVOut2(CVIn2());

		PulseOut1(PulseIn1());
		PulseOut2(PulseIn2());

		// Get switch position and set LEDs 0, 2, 4 
		// (respectively, top left, middle left and bottom left)
		int s = SwitchVal();
		LedOn(4, s == Switch::Down);
		LedOn(2, s == Switch::Middle);
		LedOn(0, s == Switch::Up);

		// Set LED 1, 3, 5 brightness to knob values
		// (respectively, top rihgt, middle right and bottom right)
		LedBrightness(1, KnobVal(Knob::Main));
		LedBrightness(3, KnobVal(Knob::X));
		LedBrightness(5, KnobVal(Knob::Y));
	}
};


int main()
{
	// Create Passthrough object...
	Passthrough pt;

	// ... and run it to start audio/processing
	pt.Run();
}

  
