#include "ComputerCard.h"

/// Test of normalisation probes
/// Lights LEDs if corresponding input has a jack plugged in.
class NormalisationProbeTest : public ComputerCard
{
public:
	
	virtual void ProcessSample()
	{
		LedOn(0, Connected(Input::Audio1));
		LedOn(1, Connected(Input::Audio2));
		LedOn(2, Connected(Input::CV1));
		LedOn(3, Connected(Input::CV2));
		LedOn(4, Connected(Input::Pulse1));
		LedOn(5, Connected(Input::Pulse2));
	}
};


int main()
{
	set_sys_clock_khz(144000, true);

	NormalisationProbeTest npt;

	// Turn on the normalisation probe
	npt.EnableNormalisationProbe();
	
	npt.Run();
}

  
