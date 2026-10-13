// eightmu example
//
// Uses a Music Thing Modular 8mu USB MIDI controller as an input to a card,
// through the EightMU class in EightMU.h.
//
//   8mu fader 1     -> CV out 1
//   8mu fader 2     -> CV out 2
//   8mu pitch       -> Audio out 1
//   8mu roll        -> Audio out 2
//   8mu buttons A/B -> Pulse out 1/2
//   8mu buttons C/D -> Computer LEDs 2/3
//   8mu faders 1-8  -> brightness of the 8mu's own eight LEDs
//   Computer LED 0  -> lit while an 8mu is connected
//
// The 8mu is a USB device, so the Computer has to be acting as a USB host.
// That needs Computer Rev 1.1 hardware, with nothing plugged into the
// Computer's own USB socket.
//
// EightMU.h carries the USB MIDI host driver and the TinyUSB host
// configuration itself, so this card is just this one file.  All its build
// needs is the TinyUSB host libraries and CFG_TUSB_CONFIG_FILE - see
// CMakeLists.txt.

#include "ComputerCard.h"
#include "EightMU.h"


class EightMUCard : public ComputerCard
{
public:
	EightMUCard()
	{
		// Give the USB power circuitry time to settle, then only start the
		// USB host stack if this really is a host port.  Cards that don't
		// care can just call mu.Start() unconditionally.
		sleep_us(150000);
		if (USBPowerState() == DFP)
		{
			mu.Start(); // claims core1
		}
	}

	virtual void ProcessSample()
	{
		// EightMU::Fader returns 0-4064, similar to KnobVal, so
		// offset it to sweep the full -6V to +6V of the CV outputs.
		CVOut1(mu.Fader(0) - 2048);
		CVOut2(mu.Fader(1) - 2048);

		// Accelerometer axes are already in the -2048 to 2047 range of the
		// audio and CV jacks, so they can be used directly.
		AudioOut1(mu.Pitch());
		AudioOut2(mu.Roll());

		PulseOut1(mu.Button(0));
		PulseOut2(mu.Button(1));

		LedOn(0, mu.Connected());
		LedOn(2, mu.Button(2));
		LedOn(3, mu.Button(3));

		// Show fader positions on the 8mu's own LEDs.  Setting these takes
		// the LEDs over from the 8mu, which otherwise flashes them on MIDI
		// activity.  SetLed only stores the value; the MIDI messages are sent
		// from the other core, at 50Hz and only for LEDs that have changed.
		for (int i = 0; i < EightMU::numFaders; i++)
		{
			mu.SetLed(i, mu.Fader(i));
		}
	}

private:
	EightMU mu;
};


int main()
{
	set_sys_clock_khz(200000, true);

	EightMUCard card;
	card.Run();
}
