#include "Menu.h"
#include "Calibration.h"
#include "MidiToCv.h"

#include "hardware/vreg.h"

static Calibration *calPtr;
static MidiToCv *midiPtr;
static void calCore1()  { calPtr->Run(); }
static void midiCore1() { midiPtr->Run(); }

int main()
{
	vreg_set_voltage(VREG_VOLTAGE_1_15);
	set_sys_clock_khz(192000, true);

	bool calibration;
	{
		Menu menu;
		calibration = menu.DetectMode();
	}

	if (calibration)
	{
		static Calibration cal;
		calPtr = &cal;
		cal.EnableNormalisationProbe();
		multicore_launch_core1(calCore1);
		cal.USBCore();
	}
	else
	{
		static MidiToCv midi;
		midiPtr = &midi;
		midi.EnableNormalisationProbe();
		multicore_launch_core1(midiCore1);
		midi.USBCore();
	}
}
