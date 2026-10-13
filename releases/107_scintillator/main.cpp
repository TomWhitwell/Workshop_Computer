// Scintillator - an analog-computer-style test-gear card for the Music
// Thing Modular Workshop Computer.
//
// Two audio inputs (A, B) feed an arithmetic "argument" stage (Knob X,
// 8-way) and then a classic analog-computer "function" stage (Knob Y,
// 6-way: log, root, square, differentiator). Audio Out 1 carries a
// dry/wet blend of that against the plain A+B mix.
//
// Audio Out 2 is a synthesised kick drum, triggered from Pulse In 1. It
// is deliberately independent of everything above it: the argument and
// function stages wander, and the kick holds the floor steady underneath.
// MAIN tunes it, CV In 1 and 2 shape its envelope, and a tap of the
// switch cycles three envelope presets.
//
// Two pulse outputs behave like Geiger-counter gates, firing
// stochastically when A and B "collide" (see dsp/geiger.h) - the card's
// name is a nod to the crystal in old radiation detectors that flashes
// when a particle hits it, a fitting image for that circuit, not a literal
// description of the DSP.
//
// Switch Middle is DSP mode: X and Y select the argument and function
// operations, and MAIN tunes the kick. Switch Up is Mix mode: X and Y
// become Audio In 1/2 level controls and MAIN becomes the dry/wet blend
// for Audio Out 1. All three knobs use pot pickup, so nothing jumps in
// either direction when the switch moves - see dsp/dual_role_knob.h.
// Switch Down is a momentary tap that cycles the kick's envelope preset.
//
// See README.md for the full panel layout.

#include <cstdint>

#include "ComputerCard.h"

#include "dsp/argument.h"
#include "dsp/common.h"
#include "dsp/dual_role_knob.h"
#include "dsp/function.h"
#include "dsp/geiger.h"
#include "dsp/input_conditioner.h"
#include "dsp/kick.h"
#include "dsp/zone_select.h"

// Set to 1 to turn the six LEDs into a ProcessSample timing display
// instead of their normal jobs: a bar graph of the worst-case time seen so
// far, one LED per ~3.3us, so all six lit means the ~20us budget is gone.
//
// This exists because overrunning that budget is the one failure mode here
// that doesn't announce itself as a timing problem: the ADC mux desyncs
// and knob readings start appearing in the audio input variables, so the
// card sounds like it has a DSP or routing bug instead. Worth re-checking
// with this whenever the audio path grows.
#define SCINTILLATOR_PROFILE 0

#if SCINTILLATOR_PROFILE
#include "pico/time.h"
#endif

namespace scintillator
{

	// Level-meter LED brightness: shows magnitude only (a single LED can't
	// show sign), clamped to the LED's 0-4095 range.
	inline int32_t LevelToLed(int32_t v)
	{
		int32_t b = Cabs(v) << 1;
		if (b > 4095) b = 4095;
		return b;
	}

} // namespace scintillator

class Scintillator : public ComputerCard
{
public:
	// Seeded from this card's unique hardware ID, so each physical card
	// draws from its own sequence rather than every card firing identically.
	Scintillator() : geiger_(static_cast<uint32_t>(UniqueCardID()) ^ 0x5CA1AB1Eu) {}

	// NB: the normalisation probe is deliberately NOT enabled here.
	//
	// It looks like the right tool - it's what makes Connected()/
	// Disconnected() work - but enabling it makes ComputerCard drive a
	// pseudo-random bit onto GPIO4 that is injected into any *unplugged*
	// jack, because that injection is exactly how the detection works
	// (see ComputerCard.h: "Force disconnected values to zero, rather
	// than the normalisation probe garbage"). The probe pin only changes
	// every 16 samples, so that garbage is a ~3kHz pseudo-random square
	// wave at signal level, and it only gets cleaned up if detection
	// succeeds on that particular board. When it doesn't, an unpatched
	// input carries loud, jittery noise straight into the chain - which
	// is what enabling it produced here.
	//
	// Two consequences worth knowing if you re-enable it:
	//   - Connected()/Disconnected() are meaningless without it.
	//     connected[] is only ever written inside ComputerCard's
	//     "if (useNormProbe)" block, so with the probe off every input
	//     reads as Disconnected(). Gating the inputs on Disconnected()
	//     while the probe is off silences the card completely.
	//   - Silence with nothing patched is handled instead by
	//     dsp/input_conditioner.h, which needs no probe and can't inject
	//     anything.

	virtual void ProcessSample() override
	{
#if SCINTILLATOR_PROFILE
		uint32_t profileStartUs = time_us_32();
#endif
		// Startup smoke test: walk all six LEDs once, so a fresh flash
		// proves LEDs, PWM and the sample clock all work before patching.
		if (RunStartupBlink()) return;

		bool mixMode = (SwitchVal() == Switch::Up);

		// Down is momentary/spring-loaded: SwitchChanged() fires once on
		// the transition into it, so a tap cycles the kick's envelope
		// preset exactly once however long the switch is held.
		if (SwitchVal() == Switch::Down && SwitchChanged())
		{
			kick_.NextPreset();
			presetFlash_ = kPresetFlashSamples;
		}

		// --- Front end: A, B -------------------------------------------
		//
		// Each input is conditioned (dsp/input_conditioner.h) before
		// anything else sees it: a DC blocker strips the standing offset
		// an unpatched, floating jack leaves behind, and a soft dead zone
		// removes the ADC's own noise floor. Without it an unpatched input
		// feeds its noise floor into everything downstream.
		//
		// There is no jack detection here - see the constructor for why
		// the normalisation probe is off, and why gating on Disconnected()
		// without it would silence the card entirely.
		//
		// In Mix mode, X and Y then set Audio In 1/2's level (0 = silence,
		// 4095 = unity) here at the front, so a level turned to zero
		// silences that input from the argument stage, the dry mix and the
		// Geiger gates alike. In DSP mode the inputs pass through at full
		// level and X/Y select operations instead.
		int32_t rawA = conditionerA_.Process(AudioIn1());
		int32_t rawB = conditionerB_.Process(AudioIn2());

		// All three knobs do two jobs, so all three run through pot
		// pickup (dsp/dual_role_knob.h). Each job keeps the value it was
		// left at, and only starts following the knob again once the knob
		// is moved back to it - so flipping the switch never jumps
		// anything, in either direction.
		mainKnob_.Update(mixMode, KnobVal(Knob::Main));
		xKnob_.Update(mixMode, KnobVal(Knob::X));
		yKnob_.Update(mixMode, KnobVal(Knob::Y));

		int32_t a, b;
		if (mixMode)
		{
			a = (rawA * xKnob_.Up()) >> 12;
			b = (rawB * yKnob_.Up()) >> 12;
		}
		else
		{
			a = rawA;
			b = rawB;
		}

		// The argument, function and kick-pitch settings are read from the
		// knobs' held middle-role values, so they carry on being applied
		// while the switch is up and are still there when it comes back.
		argZone_ = argZoneSel_.Update(xKnob_.Middle());
		funcZone_ = funcZoneSel_.Update(yKnob_.Middle());

		LedBrightness(0, scintillator::LevelToLed(a));
		LedBrightness(1, scintillator::LevelToLed(b));
		LedOn(3, mixMode);

		// --- Wet chain: argument -> function ----------------------------
		int32_t argOut = scintillator::ComputeArgument(argZone_, a, b);
		int32_t dxdt = functionDiff_.Update(argOut);
		int32_t wet = functionStage_.Process(funcZone_, argOut, dxdt);

		// --- Audio Out 1: dry/wet blend, in both switch positions --------
		//
		// MAIN is the blend control in Mix mode only, since in DSP mode it
		// is tuning the kick, so the blend keeps applying in both switch
		// positions from its held value.
		//
		// Dry is the same A/B mix the wet chain started from, taken
		// straight through with no further processing.
		int32_t dry = scintillator::Clip(a + b);
		int32_t blendAmount = mainKnob_.Up();
		AudioOut1(scintillator::Clip((dry * (4095 - blendAmount) + wet * blendAmount) >> 12));

		// --- Audio Out 2: the kick --------------------------------------
		//
		// Independent of the chain above, so it can hold a rhythm steady
		// while the argument and function stages are being played. MAIN
		// tunes it from its held DSP-mode value, CV In 1 offsets the
		// preset's attack and CV In 2 its decay.
		if (PulseIn1RisingEdge()) kick_.Trigger();
		AudioOut2(kick_.Process(mainKnob_.Middle(), CVIn1(), CVIn2()));

		// LED 3 follows the kick's envelope, so each hit is visible. Just
		// after a preset change it instead holds a brightness that says
		// which of the three is now selected - dim, half, or full - since
		// the switch springs back and can't show the setting itself.
		if (presetFlash_ > 0)
		{
			presetFlash_--;
			LedBrightness(2, kPresetLedLevels[kick_.Preset()]);
		}
		else
		{
			LedBrightness(2, kick_.EnvelopeLed());
		}

		// --- Geiger gates -------------------------------------------------
		geiger_.Process(a, b);
		PulseOut1(geiger_.Gate1Active());
		PulseOut2(geiger_.Gate2Active());
		LedOn(4, geiger_.Gate1Active());
		LedOn(5, geiger_.Gate2Active());

#if SCINTILLATOR_PROFILE
		uint32_t elapsed = time_us_32() - profileStartUs;
		if (elapsed > worstUs_) worstUs_ = elapsed;
		// One LED per ~3.3us of worst case; all six lit = at/over budget.
		for (int i = 0; i < 6; i++)
		{
			LedOn(i, worstUs_ * 6 >= (i + 1) * 20u);
		}
#endif
	}

private:
	bool RunStartupBlink()
	{
		constexpr int32_t kSamplesPerLed = 48000 / 8; // 125ms per LED
		constexpr int32_t kTotalSamples = kSamplesPerLed * 6;

		if (startupSample_ >= kTotalSamples) return false;

		int32_t active = startupSample_ / kSamplesPerLed;
		for (int32_t i = 0; i < 6; i++)
		{
			LedOn(i, i == active);
		}
		startupSample_++;
		return true;
	}

	// ~600ms of preset confirmation on LED 3 after a tap.
	static constexpr int32_t kPresetFlashSamples = 48000 * 6 / 10;
	static constexpr uint16_t kPresetLedLevels[3] = {900, 2200, 4095};

	int32_t startupSample_ = 0;
	int32_t presetFlash_ = 0;
#if SCINTILLATOR_PROFILE
	uint32_t worstUs_ = 0;
#endif

	scintillator::ZoneSelector<8> argZoneSel_;
	scintillator::ZoneSelector<6> funcZoneSel_;
	int argZone_ = 0;
	int funcZone_ = 0;

	// MAIN: kick pitch in DSP mode, dry/wet blend in Mix mode. The blend
	// starts fully wet, since a default of 0 would come up fully dry and
	// look like the card was doing nothing; the pitch starts mid-range.
	scintillator::DualRoleKnob mainKnob_{1800, 4095};
	// X and Y: argument/function select in DSP mode, input levels in Mix
	// mode. The levels start at unity so flipping up passes audio rather
	// than muting until the knobs are picked up.
	scintillator::DualRoleKnob xKnob_{0, 4095};
	scintillator::DualRoleKnob yKnob_{0, 4095};

	scintillator::InputConditioner conditionerA_;
	scintillator::InputConditioner conditionerB_;
	scintillator::FunctionStage functionStage_;
	scintillator::Differentiator functionDiff_;
	scintillator::Kick kick_;
	scintillator::Geiger geiger_;
};

int main()
{
	// 144MHz + 96k audio-input oversampling is the directive's recommended
	// default (reduces ADC tonal artifacts). No 192kHz/192MHz mode exists
	// in ComputerCard.
	set_sys_clock_khz(144000, true);

	Scintillator card;
	card.Run();
}
