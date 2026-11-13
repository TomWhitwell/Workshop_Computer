// 2026-11-11: automatic 2MB/16MB geometry; original RAM release unchanged.
// 2026-11-07: exp.2 audio-deadline fix; upstream notices retained below.
/*
 * Adapted from Temperance 0.1.0 by Matt Allison.
 * Copyright (c) 2026 Matt Allison. Original code: MIT license.
 * Source: releases/115_Temperance/src/main.cpp
 * Experimental derivative: 0.1.0-flash-exp.1, 2026-11-04.
 * See ../ATTRIBUTION.md and ../LICENSES/Temperance-MIT.txt.
 */
/*
  Temperance Flash -- auto-detecting 2MB/16MB Workshop Computer program card.

  Two micro loopers sharing one clock, feeding a bit crusher and a plate
  reverb. The clock sets the sample rate of everything before the reverb:
  slow it down and the loops get longer, lower and grittier in one move.

  This file connects the hardware (ComputerCard) to the instrument
  (Temperance.h), and sets up the two cores:

    core 0  ProcessSample(), ComputerCard's 48kHz audio interrupt: the
            clock, both loopers, the crusher, the reverb and the dry mix.
            Must finish every sample in ~20us.
    core 1  ControlLoop(): flash erase/program, ADPCM cache decoding,
            knobs, switch, CV ins, reverb LFOs and LEDs. Flash work also
            services controls during suspended sector erases.

  See README.md.
*/

#include "ComputerCard.h"
#include "Temperance.h"
#include "pico/binary_info.h"
#include "hardware/clocks.h"
#include "hardware/sync.h"
#include "hardware/timer.h"
#include "hardware/vreg.h"
#include "pico/multicore.h"
#include "pico/rand.h"

bi_decl(bi_program_description(
    "Temperance: Matt Allison; Goldfish/MLRws: Dune Desormeaux; "
    "ComputerCard: Chris Johnson; plate design: Jon Dattorro. "
    "Experimental integration: OpenAI Codex. Overall licence: GPL-3.0. See LICENSE, ATTRIBUTION.md and LICENSES."));

// The instrument's reverb and input delay buffers belong outside the stack.
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

    static void ControlHook() { core1Card->UpdateControls(); }

    void UpdateControls()
    {
        int32_t led[6];
        if (!instrument.ControlTick(led)) return;
        if (!temflash::Healthy()) {
            // Refused hardware or latched storage fault: fast all-LED blink.
            int value = (time_us_32() / 125000u) & 1u ? 4095 : 0;
            for (auto &l : led) l = value;
        }
        for (int i = 0; i < 6; ++i) {
            if (led[i] != shown_[i]) {
                LedBrightness(i, (uint16_t)led[i]);
                shown_[i] = led[i];
            }
        }
    }

    void ControlLoop()
    {
        while (true) {
            UpdateControls();
            temflash::Service(ControlHook);
            __wfe();
        }
    }
    int32_t shown_[6] = {-1, -1, -1, -1, -1, -1};

	int32_t cv_[2] = {-100000, -100000}; // nothing written yet
};

TemperanceCard *TemperanceCard::core1Card = nullptr;

#ifdef TEMPERANCE_BENCH
extern "C" tem::Temperance *bench_instrument() { return &instrument; }
// Cycle-counting builds only (tools/bench/cycles.py): an emulator drives
// core 1's update directly.
extern "C" __attribute__((used, noinline)) bool bench_control_tick(int32_t *led)
{
	return instrument.ControlTick(led);
}
#endif

int main()
{
    // Match Temperance: all code/constants in RAM, 192MHz, 1.15V.
    // Flash writes belong exclusively to core 1. Hardware timing is experimental.
	vreg_set_voltage(VREG_VOLTAGE_1_15);
	busy_wait_us_32(1000); // let the core voltage settle before speeding up
	set_sys_clock_khz(192000, true);

	// Hardware entropy: the random voltages and triggers differ every
	// power-on.
	instrument.Seed(get_rand_32());
    temflash::Init(); // JEDEC probe before the second core or audio starts

	// On the stack, and small (the big buffers are in `instrument`). Not a
	// `static` local: that pulls in ~70KB of C++ exception-handling code,
	// which this card would copy into RAM next to its buffers. (Recurrence
	// found this.)
	TemperanceCard card;
	card.EnableNormalisationProbe();
	card.StartSecondCore();
	card.Run(); // never returns
}
