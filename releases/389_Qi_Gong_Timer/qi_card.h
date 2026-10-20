// qi_card.h : the thin ComputerCard glue.  It only moves values between the hardware and the engine.
// (Every ComputerCard accessor is protected, so this subclass is where hardware calls live.)
//   Core 0: ProcessSample() at 48 kHz -> engine Process() (audio, timer, switch, pulses).
//   Core 1: Core1Loop() -> engine Control() every 32 samples (knobs, LEDs, smoothing); the only writer of the LEDs.
//   CV Out 1/2: held at 0 V (set once in Setup).
// AI-assisted (Claude / Anthropic).
#pragma once
#include "ComputerCard.h"
#ifndef MOCK_COMPUTERCARD
#include "pico/multicore.h"
#endif
#include "qi_engine.h"

class QiTimerCard : public ComputerCard
{
public:
	QiTimerCard() {}

	// Called from main() BEFORE Run() (not in the constructor: hardware/table set-up there can wedge the chip).
	void Setup()
	{
		// CV Out 1 and 2 are not used (owner, 19 Oct): hold them at 0 V.  This must be written once: ComputerCard starts
		// them at its internal 0, which (stored inverted) is the TOP of the range, not 0 V.
		CVOut1(0); CVOut2(0);
		engine_.Init((uint32_t)UniqueCardID());
		lastControl_ = engine_.SampleCount();
#ifndef MOCK_COMPUTERCARD
		self_ = this;
		multicore_launch_core1(Core1Entry);   // core 1: the UI loop (official guidance: no audio DSP there)
#endif
	}

	virtual void ProcessSample()
	{
#ifdef CARD_PROFILE
		gpio_put(DEBUG_1, 1);                 // scope GPIO 0: the pulse width is the CPU time per sample
#endif
		qi::Inputs in;
		in.sw     = (int32_t)SwitchVal();
		in.audio1 = AudioIn1();
		in.audio2 = AudioIn2();

		qi::Outputs out;
		engine_.Process(in, out);

		AudioOut1(out.audio1);  AudioOut2(out.audio2);
		PulseOut1(out.pulse1);  PulseOut2(out.pulse2);
#ifdef CARD_PROFILE
		gpio_put(DEBUG_1, 0);
#endif
	}

	// One step of core 1's work if 32 more samples have passed (returns true if it did some).  Core1Loop() repeats it
	// forever; the PC tests call it directly.  If core 1 ever falls behind it catches up, one 32-sample step at a time.
	bool Core1Step()
	{
		uint32_t n = engine_.SampleCount();
		if (n - lastControl_ < 32) return false;
		lastControl_ += 32;
		qi::Inputs in;
		in.main = KnobVal(Knob::Main);
		in.x    = KnobVal(Knob::X);
		in.y    = KnobVal(Knob::Y);
		in.sw   = (int32_t)SwitchVal();
		engine_.Control(in);
		qi::Outputs o;
		engine_.ReadControl(o);
		for (int i = 0; i < 6; ++i) LedBrightness(i, o.led[i]);
		return true;
	}

private:
#ifndef MOCK_COMPUTERCARD
	static QiTimerCard* self_;
	// Between steps core 1 waits on the timer (a peripheral) instead of polling RAM non-stop, so it competes as little as
	// possible with core 0's audio for memory.  A step is due every 32 samples (667 us); checking every 50 us is plenty.
	static void Core1Entry() { while (true) { if (!self_->Core1Step()) busy_wait_us(50); } }
#endif
	qi::Engine engine_;
	uint32_t lastControl_ = 0;
};
#ifndef MOCK_COMPUTERCARD
inline QiTimerCard* QiTimerCard::self_ = nullptr;
#endif
