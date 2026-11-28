/*
  Abeyance -- a Workshop Computer program card.

  13 loops of piano and choir drifting in and out of step, panned across a
  stereo reverb, after the tape loops of Brian Eno's Music for Airports. It
  plays on its own from power-up. Tap the switch down (or send Pulse In 1)
  for a new piece: a new root, scale, tempo and loop lengths.

  This file connects the hardware (ComputerCard) to the instrument
  (Abeyance.h), and sets up the two cores:

    core 0  ProcessSample(), ComputerCard's 48kHz audio interrupt: plays
            core 1's blocks back a sample at a time, adds the piano echo,
            the reverb, the audio inputs and the hiss, and drives every
            jack. Must finish every sample in ~20us.
    core 1  ControlLoop(): renders the 20 voices 32 samples at a time and
            runs everything control-rate (knobs, scheduler, LEDs, random CVs,
            pulse outs) once per block. Woken once per sample by core 0, so
            its activity stays locked to the sample clock (activity that
            isn't can alias into the audio inputs as whine -- see
            ComputerCard's NOTES.md).

  See README.md.
*/

#include "ComputerCard.h"
#include "Abeyance.h"
#include "hardware/clocks.h"
#include "hardware/sync.h"
#include "hardware/timer.h"
#include "hardware/vreg.h"
#include "pico/multicore.h"
#include "pico/rand.h"

// Static, not a member of the card: the instrument holds the reverb and
// echo lines, far bigger than the 4KB core 0 stack.
static ab::Abeyance instrument;

class AbeyanceCard : public ComputerCard
{
public:
	AbeyanceCard()
	{
		// Jack detection: unpatched audio inputs then read as zero (so they
		// add nothing to the reverb), and edges from pulling a cable out of
		// Pulse In 1 can be ignored.
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
		ab::Inputs in;
		in.knob[0] = KnobVal(Knob::Main);
		in.knob[1] = KnobVal(Knob::X);
		in.knob[2] = KnobVal(Knob::Y);
		in.sw = SwitchVal(); // Down=0, Middle=1, Up=2, same as ab::kSwitch*
		in.audio[0] = AudioIn1();
		in.audio[1] = AudioIn2();
		in.cv[0] = CVIn1();
		in.cv[1] = CVIn2();
		in.pulseRise = PulseIn1RisingEdge();
		in.pulseConnected = Connected(Input::Pulse1);

		ab::Outputs out;
		instrument.Audio(in, out);

		AudioOut1((int16_t)out.audio[0]);
		AudioOut2((int16_t)out.audio[1]);
		// Calibrated millivolts (ComputerCard reads each unit's calibration
		// from its EEPROM). Only worked out when the value changes: once
		// per block at most.
		if (out.cvMv[0] != lastCvMv[0])
		{
			CVOut1Millivolts(out.cvMv[0]);
			lastCvMv[0] = out.cvMv[0];
		}
		if (out.cvMv[1] != lastCvMv[1])
		{
			CVOut2Millivolts(out.cvMv[1]);
			lastCvMv[1] = out.cvMv[1];
		}
		PulseOut1(out.pulse[0]);
		PulseOut2(out.pulse[1]);

		// Wake core 1.
		__sev();
	}

private:
	static AbeyanceCard *core1Card;
	int32_t lastCvMv[2] = {-100000, -100000};

	static void Core1Entry() { core1Card->ControlLoop(); }

	// Core 1: render a block whenever there's room for one.
	void ControlLoop()
	{
		int32_t led[6];
		int32_t shown[6] = {-1, -1, -1, -1, -1, -1};
		while (true)
		{
			__wfe(); // sleep until core 0 finishes a sample
			if (!instrument.Poll(led))
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

AbeyanceCard *AbeyanceCard::core1Card = nullptr;

#ifdef ABEYANCE_BENCH
// Cycle-counting builds only (an emulator drives core 1's block directly).
extern "C" __attribute__((used, noinline)) bool bench_poll(int32_t *led)
{
	return instrument.Poll(led);
}
// Start every voice at once: the worst case for core 1.
extern "C" __attribute__((used, noinline)) void bench_fill()
{
	instrument.BenchFill();
}
#endif

int main()
{
	// 192MHz at 1.15V: an officially supported RP2040 setting, and one of
	// the clocks ComputerCard recommends (a multiple of 48MHz, so neither
	// the CPU clock nor the CV-out PWM aliases into the audio inputs).
	//
	// Why not the usual 144MHz: measured in tools/bench, core 1 needs up
	// to 83% of each block there (all 20 voices, on the block a new piece
	// starts), and core 0 72% of each sample -- too little margin, as the
	// bench can't see the two cores contending for RAM. 192MHz gives a
	// third more time: 62% and 54%. (The build guide's fallback says
	// 200MHz; 192 is the nearest clock that stays alias-free.)
	// ComputerCard's caution about overclocking is about running code from
	// the program card's flash; this card copies itself to RAM at boot and
	// doesn't touch the flash after that.
	vreg_set_voltage(VREG_VOLTAGE_1_15);
	busy_wait_us_32(1000); // let the core voltage settle before speeding up
	set_sys_clock_khz(192000, true);

	// Sine, note and decay tables and the choir's wavetables, before any
	// sound is made.
	ab::Tables::Init();
	ab::Piano::Init();
	ab::Choir::Init(); // ~20ms of whole-number sums
	// Hardware entropy for the drift and the random CVs. The first piece
	// is always the original one; only the details vary from power-on to
	// power-on.
	instrument.Seed(get_rand_32());

	// On the stack, and small (the big buffers are in `instrument`). Not a
	// `static` local: that pulls in ~70KB of C++ exception-handling code,
	// which this card copies into RAM next to its delay lines.
	AbeyanceCard card;
	card.StartSecondCore();
	card.Run(); // never returns
}
