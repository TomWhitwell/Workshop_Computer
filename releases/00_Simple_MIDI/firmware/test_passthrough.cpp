#include "ComputerCard.h"

// Test passthrough: verifies calibrated input -> calibrated output round-trip.
//   AudioOut1 amplitude: main knob (0 = silent, max = full scale)
//   AudioIn1 millivolts -> CVOut1 millivolts  (calibrated)
//   AudioIn2 millivolts -> CVOut2 millivolts  (calibrated)
class PassthroughTest : public ComputerCard
{
	void ProcessSample() override
	{
		int32_t knob = KnobVal(Main);
		AudioOut1(knob-2048);

		if (SwitchVal() == Switch::Up)
		{
			CVOut1Millivolts(AudioIn1Millivolts());
			CVOut2Millivolts(AudioIn2Millivolts());
		}
		else
		{
			CVOut1(AudioIn1());
			CVOut2(AudioIn2());
		}
	}
};

int main()
{
	PassthroughTest test;
	test.Run();
}
