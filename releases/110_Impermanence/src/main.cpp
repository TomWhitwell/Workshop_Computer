/*
  Impermanence -- a Workshop Computer program card.

  A 0.75-second stereo buffer, sliced, reordered and pitch-scattered by a
  single Chaos knob that re-randomises only when it moves, into a shimmer
  reverb. No undo, no presets: once the knob moves, the old loop is gone.

  This file connects the hardware (ComputerCard) to the instrument
  (Impermanence.h), and sets up the two cores:

    core 0  ProcessSample(), ComputerCard's 48kHz audio interrupt: reads
            the jacks, runs the audio half of the instrument, writes the
            jacks. Must finish every sample in ~20us.
    core 1  ControlLoop(): knobs, catch-up, re-cuts, slice maps and LEDs.
            Woken once per sample by core 0, so its activity stays locked
            to the sample clock (activity that isn't can alias into the
            audio inputs as whine -- see ComputerCard's NOTES.md).

  See README.md and docs/design-brief.md.
*/

#include "ComputerCard.h"
#include "Impermanence.h"
#include "hardware/clocks.h"
#include "hardware/sync.h"
#include "hardware/timer.h"
#include "hardware/vreg.h"
#include "pico/multicore.h"
#include "pico/rand.h"

// Set to 1 to check the audio interrupt fits its time budget on hardware:
// LED 5 then lights (and stays lit) if any ProcessSample call took longer
// than kBudgetMicros. Every sample gets ~20.8us; at 192MHz ComputerCard's
// own per-sample work takes ~5us of that, so ProcessSample should stay
// under ~15us. (Measured in an emulator: ~9us typical, ~11us worst.)
#define IMPERMANENCE_PROFILE 0
constexpr uint32_t kBudgetMicros = 15;

// Static, not a member of the card: the instrument holds the 144KB audio
// buffer, far bigger than the 4KB core 0 stack the card object lives on.
static imp::Impermanence instrument;

class ImpermanenceCard : public ComputerCard
{
public:
	ImpermanenceCard()
	{
		// Jack detection: whether Audio In 2 is patched (else record In 1 in
		// mono), and whether the Pulse Ins are, so edges from pulling a
		// cable out can be ignored.
		EnableNormalisationProbe();
		core1Card = this;
		multicore_launch_core1(Core1Entry);
	}

	// Core 0, 48kHz, inside the audio interrupt.
	void ProcessSample() override
	{
#if IMPERMANENCE_PROFILE
		uint32_t startMicros = timer_hw->timerawl;
#endif
		imp::Inputs in;
		in.knob[0] = KnobVal(Knob::Main);
		in.knob[1] = KnobVal(Knob::X);
		in.knob[2] = KnobVal(Knob::Y);
		in.sw = SwitchVal(); // Down=0, Middle=1, Up=2, same as imp::kSwitch*
		in.audio[0] = AudioIn1();
		in.audio[1] = AudioIn2();
		in.cv[0] = CVIn1();
		in.cv[1] = CVIn2();
		in.pulse1Rise = PulseIn1RisingEdge();
		in.pulse2Rise = PulseIn2RisingEdge();
		in.audio2Connected = Connected(Input::Audio2);
		in.pulse1Connected = Connected(Input::Pulse1);
		in.pulse2Connected = Connected(Input::Pulse2);

		imp::Outputs out;
		instrument.Audio(in, out);

		AudioOut1(out.audio[0]);
		AudioOut2(out.audio[1]);
		CVOut1(out.cv[0]);
		CVOut2(out.cv[1]);
		PulseOut1(out.pulse[0]);
		PulseOut2(out.pulse[1]);

#if IMPERMANENCE_PROFILE
		if (timer_hw->timerawl - startMicros > kBudgetMicros)
			overran_ = true;
#endif
		// Wake core 1 for its next control update.
		__sev();
	}

private:
	static ImpermanenceCard *core1Card;

	static void Core1Entry() { core1Card->ControlLoop(); }

	// Core 1: one control update per audio sample, at most.
	void ControlLoop()
	{
		int32_t led[6];
		int32_t shown[6] = {-1, -1, -1, -1, -1, -1};
		while (true)
		{
			__wfe(); // sleep until core 0 finishes a sample
			if (!instrument.ControlTick(led))
				continue;
#if IMPERMANENCE_PROFILE
			led[5] = overran_ ? 4095 : 0;
#endif
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

#if IMPERMANENCE_PROFILE
	volatile bool overran_ = false;
#endif
};

ImpermanenceCard *ImpermanenceCard::core1Card = nullptr;

#ifdef IMPERMANENCE_BENCH
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
	// Why not the usual 144MHz: at 144MHz this card's audio interrupt
	// measured at up to ~100% of the time available per sample -- no
	// safety margin, and every overrun is heard as broken audio. 192MHz
	// gives a third more time, bringing the worst case to ~75%.
	// (ComputerCard's caution about overclocking the Computer is about
	// running code from the program card's flash; this card copies itself
	// to RAM at boot and doesn't touch the flash after that.)
	vreg_set_voltage(VREG_VOLTAGE_1_15);
	busy_wait_us_32(1000); // let the core voltage settle before speeding up
	set_sys_clock_khz(192000, true);
	// Hardware entropy: every power-on starts a different random sequence,
	// so no session's slice maps can ever come back.
	instrument.Seed(get_rand_32());
	// Constructed after the clock change: the constructor sets up hardware
	// and starts core 1.
	ImpermanenceCard card;
	card.Run(); // never returns
}
