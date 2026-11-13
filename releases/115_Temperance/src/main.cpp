/*
  Temperance -- a Workshop Computer program card.

  Two micro loopers sharing one clock, feeding a clocked delay and a plate
  reverb. The clock sets the sample rate of everything before the reverb:
  slow it down and the loops get longer, lower and grittier in one move.

  This file connects the hardware (ComputerCard) to the instrument
  (Temperance.h), and sets up the two cores:

    core 0  ProcessSample(), ComputerCard's 48kHz audio interrupt: the
            clock, both loopers, the crusher, the reverb and the dry mix.
            Must finish every sample in ~20us.
    core 1  ControlLoop(): once a millisecond, the knobs, switch, CV ins,
            the reverb's LFOs and the LEDs. Woken once per sample by core 0,
            so its activity stays locked to the sample clock (activity that
            isn't can alias into the audio as whine -- see ComputerCard's
            NOTES.md).

  See README.md.
*/

#include "ComputerCard.h"
#include "Temperance.h"
#include "hardware/clocks.h"
#include "hardware/sync.h"
#include "hardware/timer.h"
#include "hardware/vreg.h"
#include "pico/multicore.h"
#include "pico/rand.h"

// Static, not a member of the card: the instrument holds the loop and
// reverb buffers (about 158KB), far bigger than the 4KB core 0 stack.
static tem::Temperance instrument;

class TemperanceCard : public ComputerCard
{
public:
	// Start core 1. Called from main() before Run(), as in ComputerCard's
	// second_core example.
	void StartSecondCore()
	{
		core1Card = this;
		multicore_launch_core1(Core1Entry);
	}

	// Core 0, 48kHz, inside the audio interrupt.
	void ProcessSample() override
	{
		tem::Inputs in;
		in.knob[0] = KnobVal(Knob::Main);
		in.knob[1] = KnobVal(Knob::X);
		in.knob[2] = KnobVal(Knob::Y);
		in.sw = SwitchVal(); // Down=0, Middle=1, Up=2, same as tem::kSwitch*
		in.cv[0] = CVIn1();
		in.cv[1] = CVIn2();
		in.audio[0] = AudioIn1();
		in.audio[1] = AudioIn2();
		// Which jacks are patched comes from the normalisation probe
		// (enabled in main). See Clock.h for how the record pulses cope
		// with the probe's pattern just after a cable is pulled out.
		in.pulsePatched[0] = Connected(Input::Pulse1);
		in.pulsePatched[1] = Connected(Input::Pulse2);
		in.audio2Patched = Connected(Input::Audio2);
		in.rise[0] = PulseIn1RisingEdge();
		in.rise[1] = PulseIn2RisingEdge();
		in.high[0] = PulseIn1();
		in.high[1] = PulseIn2();

		tem::Outputs out;
		instrument.Audio(in, out);

		AudioOut1((int16_t)out.audio[0]);
		AudioOut2((int16_t)out.audio[1]);
		PulseOut1(out.pulse[0]);
		PulseOut2(out.pulse[1]);
		// Calibrated volts (ComputerCard reads each Computer's calibration
		// from its EEPROM), so CV out 1's whole volts land exactly on CV in
		// 1's clock steps when it's patched back. Only written when they
		// change: at most once a millisecond.
		if (out.cvMv[0] != cv_[0])
		{
			cv_[0] = out.cvMv[0];
			CVOut1Millivolts(cv_[0]);
		}
		if (out.cvMv[1] != cv_[1])
		{
			cv_[1] = out.cvMv[1];
			CVOut2Millivolts(cv_[1]);
		}

		// Wake core 1.
		__sev();
	}

private:
	static TemperanceCard *core1Card;

	static void Core1Entry() { core1Card->ControlLoop(); }

	// Core 1: one control update per millisecond.
	void ControlLoop()
	{
		int32_t led[6];
		int32_t shown[6] = {-1, -1, -1, -1, -1, -1};
		while (true)
		{
			__wfe(); // sleep until core 0 finishes a sample
			if (!instrument.ControlTick(led))
				continue;
			// Only touch the LED hardware when something changed.
			for (int i = 0; i < 6; i++)
			{
				if (led[i] != shown[i])
				{
					LedBrightness(i, (uint16_t)led[i]);
					shown[i] = led[i];
				}
			}
		}
	}

	int32_t cv_[2] = {-100000, -100000}; // nothing written yet
};

TemperanceCard *TemperanceCard::core1Card = nullptr;

#ifdef TEMPERANCE_BENCH
// Cycle-counting builds only (tools/bench/cycles.py): an emulator drives
// core 1's update directly.
extern "C" __attribute__((used, noinline)) bool bench_control_tick(int32_t *led)
{
	return instrument.ControlTick(led);
}
#endif

int main()
{
	// 192MHz at 1.15V: an officially supported RP2040 setting, and one of
	// the clocks ComputerCard recommends (a multiple of 48MHz, so neither
	// the CPU clock nor the CV-out PWM aliases into the audio). The caution
	// about overclocking is about running code from the program card's
	// flash; this card copies itself to RAM at boot and doesn't touch the
	// flash after that. (As Hubris and Recurrence.)
	//
	// Why not the directive's 144MHz: the bench (tools/bench) puts the
	// worst sample at 91% of a 144MHz budget -- both loopers on their first
	// pass after a stop (still recording the seam's tail) and the plate,
	// all at the 48kHz clock -- with RAM contention between the cores not
	// modelled. At 192MHz it's 69%.
	vreg_set_voltage(VREG_VOLTAGE_1_15);
	busy_wait_us_32(1000); // let the core voltage settle before speeding up
	set_sys_clock_khz(192000, true);

	// Hardware entropy: the random voltages and triggers differ every
	// power-on.
	instrument.Seed(get_rand_32());

	// On the stack, and small (the big buffers are in `instrument`). Not a
	// `static` local: that pulls in ~70KB of C++ exception-handling code,
	// which this card would copy into RAM next to its buffers. (Recurrence
	// found this.)
	TemperanceCard card;
	card.EnableNormalisationProbe();
	card.StartSecondCore();
	card.Run(); // never returns
}
