/*
  Constancy -- a Workshop Computer program card.

  One Cornish morning, forever: 17 April 2024 at Calamansac on the Helford
  River, from first light to two hours after sunrise. The tide, the sun,
  the wind and the cloud of that dawn are held in memory, and the music
  moves only when you move through the morning with the Time knob.

  The voice is a CS-80-style supersaw through a fixed-point port of the
  feedback loop from Infinite Digits' Icarus (norns); the notes and keys come
  from Equanimity.

  This file connects the hardware (ComputerCard) to the instrument
  (Constancy.h), and sets up the two cores:

    core 0  ProcessSample(), ComputerCard's 48kHz audio interrupt: reads
            the jacks, runs the saws, filters and the Icarus loop, writes
            the jacks. Must finish every sample in ~20us.
    core 1  ControlLoop(): the morning's data, notes, envelopes, pitch
            drift, the Turing machine, knobs, CVs and LEDs, once a
            millisecond. Woken once per
            sample by core 0, so its activity stays locked to the sample
            clock (activity that isn't can alias into the audio inputs as
            whine -- see ComputerCard's NOTES.md).

  See README.md.
*/

#include "ComputerCard.h"
#include "Constancy.h"
#include "hardware/clocks.h"
#include "hardware/sync.h"
#include "hardware/timer.h"
#include "hardware/vreg.h"
#include "pico/multicore.h"
#include "pico/rand.h"

// Static, not a member of the card: the instrument holds the delay line
// (about 65KB), far bigger than the 4KB core 0 stack.
static constancy::Constancy instrument;

class ConstancyCard : public ComputerCard
{
public:
	ConstancyCard()
	{
		// Jack detection: unpatched inputs then read as zero (so they add
		// nothing to the loop or the controls), and edges from pulling a
		// cable out of a Pulse In can be ignored.
		EnableNormalisationProbe();
	}

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
		constancy::Inputs in;
		in.knob[0] = KnobVal(Knob::Main);
		in.knob[1] = KnobVal(Knob::X);
		in.knob[2] = KnobVal(Knob::Y);
		in.sw = SwitchVal(); // Down=0, Middle=1, Up=2, same as constancy::kSwitch*
		in.audio[0] = AudioIn1();
		in.audio[1] = AudioIn2();
		in.cv[0] = CVIn1();
		in.cv[1] = CVIn2();
		in.pulseRise[0] = PulseIn1RisingEdge();
		in.pulseRise[1] = PulseIn2RisingEdge();
		in.pulseConnected[0] = Connected(Input::Pulse1);
		in.pulseConnected[1] = Connected(Input::Pulse2);

		constancy::Outputs out;
		instrument.Audio(in, out);

		AudioOut1((int16_t)out.audio[0]);
		AudioOut2((int16_t)out.audio[1]);
		CVOut1Precise(out.cv1);
		// The Turing machine's pitch, through the Computer's calibration
		// (made with the Simple MIDI card) so it tracks 1V per octave. Only
		// when it changes: it's the same for thousands of samples at a time.
		if (out.cv2Note != cv2Shown)
		{
			CVOut2MIDINote((uint8_t)out.cv2Note);
			cv2Shown = out.cv2Note;
		}
		PulseOut1(out.pulse[0]);
		PulseOut2(out.pulse[1]);

		// Wake core 1.
		__sev();
	}

private:
	static ConstancyCard *core1Card;
	int32_t cv2Shown = -1; // the note CV Out 2 is set to

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
};

ConstancyCard *ConstancyCard::core1Card = nullptr;

#ifdef CONSTANCY_BENCH
// Cycle-counting builds only (an emulator drives core 1's update directly).
extern "C" __attribute__((used, noinline)) bool bench_control_tick(int32_t *led)
{
	return instrument.ControlTick(led);
}

// ...and seeds the instrument, as main() does.
extern "C" __attribute__((used, noinline)) void bench_seed()
{
	instrument.Seed(1);
}
#endif

int main()
{
	// 192MHz at 1.15V: an officially supported RP2040 setting, and one of
	// the clocks ComputerCard recommends (a multiple of 48MHz, so neither
	// the CPU clock nor the CV-out PWM aliases into the audio inputs).
	// Why not the usual 144MHz: see the measured budget in README.md. (The
	// caution about overclocking is about running code from the program
	// card's flash; this card copies itself to RAM at boot and doesn't
	// touch the flash after that.)
	vreg_set_voltage(VREG_VOLTAGE_1_15);
	busy_wait_us_32(1000); // let the core voltage settle before speeding up
	set_sys_clock_khz(192000, true);

	// Hardware entropy: every power-on starts in a different key.
	instrument.Seed(get_rand_32());
	// ...and every reseed draws a fresh seed the same way.
	instrument.SetEntropy(get_rand_32);

	// On the stack, and small (the big buffers are in `instrument`). Not a
	// `static` local: that pulls in ~70KB of C++ exception-handling code,
	// which this card copies into RAM next to its delay line.
	ConstancyCard card;
	card.StartSecondCore();
	card.Run(); // never returns
}
