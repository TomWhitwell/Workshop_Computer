// diag.cpp — minimal diagnostic firmware to bisect the "totally dark" boot.
//
// Build this INSTEAD of main.cpp (see CMake DIAG option) to test, in stages,
// what works on the hardware. Each stage lights LEDs from core 0 only. If even
// STAGE 0 is dark, the problem is the base card/build, not the emulator.
//
//   STAGE 0 : pure ComputerCard, no emulator, no 2nd core. Blinks LED 0.
//             If this is DARK -> base card/build/flash problem.
//   STAGE 1 : also launch core 1 doing nothing but incrementing a counter.
//             LED 2 shows the counter is advancing (2nd core alive).
//   STAGE 2 : core 1 constructs + resets the emulator (no Run yet). LED 4 lit
//             if it got past emulator construction without faulting.
//
// Set kStage below and rebuild.

#include "ComputerCard.h"
#include "pico/multicore.h"

static constexpr int kStage = 5;

#include "spectrum.h"
#include "machine.h"
using namespace zx;

static Spectrum gSpectrum;
static Machine  gMachine;
static volatile uint32_t gCore1Counter = 0;
static volatile bool     gEmuConstructed = false;

static volatile bool gRanOnce = false; // STAGE 3: survived one gMachine.Run()
static uint32_t gLastSample = 0;        // STAGE 4: core-1 pacing reference

static void core1_entry()
{
	if (kStage >= 2)
	{
		gMachine.Attach(&gSpectrum);
		gMachine.Reset();
		gEmuConstructed = true;
	}
	while (true)
	{
		gCore1Counter++;
		// STAGE 3: actually run the Z80 a little each loop. If the chip wedges
		// here, the fault is inside gMachine.Run (the tick loop / bus / z80).
		if (kStage == 3)
		{
			gMachine.Run(1000);   // ~1000 T-states
			gRanOnce = true;
		}
		else if (kStage >= 4)
		{
			// STAGE 4: the EMULATOR'S EXACT core-1 pacing loop (from main.cpp),
			// to reproduce the full-emulator dark boot inside the harness.
			uint32_t now = gSpectrum.xc.sampleCount;
			uint32_t elapsed = now - gLastSample;
			gLastSample = now;
			if (elapsed == 0) { tight_loop_contents(); }
			else
			{
				if (elapsed > 96) elapsed = 96;
				gMachine.Run(elapsed * (kCpuHz / 48000));
				gRanOnce = true;
			}
		}
		else
		{
			for (volatile int i = 0; i < 10000; i++) {} // slow it down
		}
	}
}

class Diag : public ComputerCard
{
public:
	Diag()
	{
		if (kStage >= 1)
			multicore_launch_core1(core1_entry);
	}

	// __not_in_flash_func like OneBit's ProcessSample, to match the known-good card.
	virtual void __not_in_flash_func(ProcessSample)()
	{
		counter_++;
		// STAGE 0: core 0 alive — blink LEDs 0 AND 1 at ~1.5Hz (unmistakable).
		bool on = (counter_ >> 15) & 1;
		LedOn(0, on);
		LedOn(1, !on);   // alternating, so "blinking" is obvious vs stuck-on

		// STAGE 1: core 1 alive — LED 2 blinks iff gCore1Counter is advancing.
		if (kStage >= 1)
		{
			if ((counter_ & 0x3FFF) == 0)
			{
				led2_ = (gCore1Counter != lastC1_);
				lastC1_ = gCore1Counter;
			}
			LedOn(2, led2_);
		}

		// STAGE 2: emulator constructed on core 1 without faulting.
		if (kStage >= 2)
			LedOn(4, gEmuConstructed);

		// STAGE 3: survived running the Z80 tick loop at least once.
		if (kStage >= 3)
			LedOn(5, gRanOnce);

		// STAGE 4: reproduce the emulator's core-0 side: advance the time
		// reference (so core 1 pacing runs) AND drive the CV/Pulse/Audio outs
		// exactly like main.cpp, to catch a hardware-output-related wedge.
		if (kStage >= 4)
		{
			PulseOut1(gSpectrum.xc.beeper);
			PulseOut2(gSpectrum.xc.mic);
			AudioOut2(gSpectrum.xc.aySample);
			uint8_t b = gSpectrum.xc.border;
			CVOut1(int16_t((b * 2047) / 7));
			gSpectrum.xc.sampleCount++;
		}
		// STAGE 5: add the switch reading + paused/reset flags (the last thing
		// main.cpp does that STAGE 4 does not). If this goes dark, it's here.
		if (kStage >= 5)
		{
			Switch sw = SwitchVal();
			if (sw == Switch::Down && SwitchChanged())
				gSpectrum.xc.resetRequest = true;
			gSpectrum.xc.paused = (sw == Switch::Middle);
			// Also use LedBrightness (not LedOn) like main.cpp, on LED 3.
			LedBrightness(3, gRanOnce ? 2047 : 0);
		}
	}

private:
	uint32_t counter_ = 0;
	uint32_t lastC1_ = 0;
	bool     led2_ = false;
};

int main()
{
	set_sys_clock_khz(144000, true);
	Diag d;
	d.Run();
}
