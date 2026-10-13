/*
  Equanimity -- a Workshop Computer program card.

  A generative ambient card: seeded, Bloom-style melody notes (in one of six
  FM tones) that repeat on their own loops, over a slow drone, into a 1.1s
  and a 9.7s delay, all stirred by one slow "tide". Each set has a random
  root, scale and tone.

  This file connects the hardware (ComputerCard) to the instrument
  (Equanimity.h), and sets up the two cores:

    core 0  ProcessSample(), ComputerCard's 48kHz audio interrupt: reads
            the jacks, runs the melody, drone and delays, writes the jacks. Must
            finish every sample in ~20us.
    core 1  ControlLoop(): the tide, note loops, drone moves, triggers,
            knobs and LEDs, once a millisecond. Woken once per sample by core 0, so
            its activity stays locked to the sample clock (activity that
            isn't can alias into the audio inputs as whine -- see
            ComputerCard's NOTES.md).

  See README.md.
*/

#include "ComputerCard.h"
#include "Equanimity.h"
#include "hardware/clocks.h"
#include "hardware/sync.h"
#include "hardware/timer.h"
#include "hardware/vreg.h"
#include "pico/multicore.h"
#include "pico/rand.h"

// Static, not a member of the card: the instrument holds the two delay
// lines (about 130KB), far bigger than the 4KB core 0 stack.
static eq::Equanimity instrument;

class EquanimityCard : public ComputerCard
{
public:
	EquanimityCard()
	{
		// Jack detection: unpatched inputs then read as zero (so they add
		// nothing to the delays or the feedback), and edges from pulling a
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
		eq::Inputs in;
		in.knob[0] = KnobVal(Knob::Main);
		in.knob[1] = KnobVal(Knob::X);
		in.knob[2] = KnobVal(Knob::Y);
		in.sw = SwitchVal(); // Down=0, Middle=1, Up=2, same as eq::kSwitch*
		in.audio[0] = AudioIn1();
		in.audio[1] = AudioIn2();
		in.cv[0] = CVIn1();
		in.cv[1] = CVIn2();
		in.pulseRise[0] = PulseIn1RisingEdge();
		in.pulseRise[1] = PulseIn2RisingEdge();
		in.pulseConnected[0] = Connected(Input::Pulse1);
		in.pulseConnected[1] = Connected(Input::Pulse2);

		eq::Outputs out;
		instrument.Audio(in, out);

		AudioOut1((int16_t)out.audio[0]);
		AudioOut2((int16_t)out.audio[1]);
		CVOut1Precise(out.cv[0]);
		CVOut2Precise(out.cv[1]);
		PulseOut1(out.pulse[0]);
		PulseOut2(out.pulse[1]);

		// Wake core 1.
		__sev();
	}

private:
	static EquanimityCard *core1Card;

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

EquanimityCard *EquanimityCard::core1Card = nullptr;

#ifdef EQUANIMITY_BENCH
// Cycle-counting builds only (an emulator drives core 1's update directly).
extern "C" __attribute__((used, noinline)) bool bench_control_tick(int32_t *led)
{
	return instrument.ControlTick(led);
}
#endif

int main()
{
	// 192MHz at 1.15V: an officially supported RP2040 setting, and one of
	// the clocks ComputerCard recommends (a multiple of 48MHz, so neither
	// the CPU clock nor the CV-out PWM aliases into the audio inputs).
	//
	// Why not the usual 144MHz: eight FM voices and two delay lines
	// measured at up to 88% of the time available per sample there -- too
	// little margin, and every overrun is heard as broken audio. 192MHz
	// gives a third more time. (ComputerCard's caution about overclocking
	// is about running code from the program card's flash; this card copies
	// itself to RAM at boot and doesn't touch the flash after that.)
	vreg_set_voltage(VREG_VOLTAGE_1_15);
	busy_wait_us_32(1000); // let the core voltage settle before speeding up
	set_sys_clock_khz(192000, true);

	// Sine, note and mu-law tables, before any sound is made.
	eq::Tables::Init();
	// Hardware entropy: every power-on starts a different random sequence.
	instrument.Seed(get_rand_32());
	// ...and every new set (Pulse In 2) draws a fresh seed the same way.
	instrument.SetEntropy(get_rand_32);

	// On the stack, and small (the big buffers are in `instrument`). Not a
	// `static` local: that pulls in ~70KB of C++ exception-handling code,
	// which this card copies into RAM next to its delay lines.
	EquanimityCard card;
	card.StartSecondCore();
	card.Run(); // never returns
}
