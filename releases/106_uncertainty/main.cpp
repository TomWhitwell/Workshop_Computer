// Uncertainty — a Buchla 266 Source of Uncertainty tribute, plus a
// Buchla-lineage wavefolder and comparator, for the Music Thing Modular
// Workshop Computer.
//
// Five blocks share the card:
//   Noise source   - flat / low-biased (pink) / high-biased (blue),
//                    cycled by tapping the Z switch down. -> Audio Out 2
//   FRV            - Fluctuating Random Voltage: a random walk that glides
//                    continuously between new targets. Rate set by X (and
//                    CV In 2). Runs on the second core, since its rate can
//                    be far slower than the 48kHz audio loop. -> CV Out 1
//   QRV            - Quantized Random Voltage: a fresh random value latched
//                    on each Pulse In 1 trigger, held with no slew. Range
//                    set by Y. -> CV Out 2
//   Wavefolder     - Chris Johnson's antiderivative-antialiased fold
//                    (ported from Utility Pair), drive from Main knob +
//                    CV In 1 (bipolar). Audio In 1 -> Audio Out 1
//   Comparator     - fixed +-1V window on Audio In 1; fires a short pulse
//                    each time the signal leaves the window. -> Pulse Out 1
//
// See README.md for the full panel layout and the hardware-reality
// corrections (voltage range, switch behaviour) this build is based on.

#include <cmath>
#include <cstdint>

#include "ComputerCard.h"
#include "pico/multicore.h"
#include "pico/time.h"

#include "dsp/comparator.h"
#include "dsp/noise.h"
#include "dsp/qrv.h"
#include "dsp/wavefolder.h"

namespace uncertainty
{

	// Millivolts <-> LED brightness (0..4095), for the "0V off, +6V
	// brightest" FRV/QRV indicators. mv is expected in 0..6000.
	static inline int32_t MillivoltsToLed(int32_t mv)
	{
		if (mv < 0) mv = 0;
		if (mv > 6000) mv = 6000;
		return static_cast<int32_t>((mv * 4095) / 6000);
	}

} // namespace uncertainty

class Uncertainty : public ComputerCard
{
public:
	Uncertainty()
	{
		multicore_launch_core1(Core1Entry);
	}

	// ---- Core 1: FRV control-rate loop -------------------------------
	//
	// FRV's rate can be as slow as 0.05Hz, far below audio rate, so its
	// random-walk-with-slew is computed here instead of in ProcessSample.
	// It reads the X knob and CV In 2 directly (rather than only inside
	// ProcessSample) — the same pattern the ComputerCard "second_core"
	// example uses, since KnobVal/CVIn read a volatile word that's safe
	// to sample from either core.
	void FRVLoop()
	{
		constexpr float kMinHz = 0.05f;
		constexpr float kLog10 = 2.302585093f; // ln(10)
		constexpr float kDecadesSpan = 3.0f * kLog10;

		float target = 0.0f;
		float current = 0.0f;
		float secondsToNextTarget = 0.0f;
		uint32_t lastUs = time_us_32();

		while (true)
		{
			uint32_t nowUs = time_us_32();
			float dt = static_cast<float>(nowUs - lastUs) * 1.0e-6f;
			lastUs = nowUs;

			// X sets the base rate; CV In 2 can swing it by up to half the
			// knob's own travel, in either direction.
			float knobFrac = KnobVal(Knob::X) / 4095.0f;
			float cvMod = (CVIn2() / 2047.0f) * 0.5f;
			float normalized = knobFrac + cvMod;
			if (normalized < 0.0f) normalized = 0.0f;
			if (normalized > 1.0f) normalized = 1.0f;

			float rateHz = kMinHz * expf(kDecadesSpan * normalized);

			secondsToNextTarget -= dt;
			if (secondsToNextTarget <= 0.0f)
			{
				uint32_t draw = uncertainty::Xorshift32(frvSeed_) & 0x0FFF; // 0..4095
				target = (draw / 4095.0f) * 6000.0f;                       // millivolts
				secondsToNextTarget = 1.0f / rateHz;
			}

			// Exponential glide that reaches ~95% of the way to a fresh
			// target over the course of one interval, so it arrives about
			// when the next target is chosen rather than stepping.
			float tau = (1.0f / rateHz) / 3.0f;
			if (tau > 0.0f)
			{
				float coeff = 1.0f - expf(-dt / tau);
				current += (target - current) * coeff;
			}

			frvOutMillivolts_ = static_cast<int32_t>(current);

			// Control-rate is plenty here; free up the core.
			sleep_us(200);
		}
	}

	static void Core1Entry()
	{
		static_cast<Uncertainty *>(ThisPtr())->FRVLoop();
	}

	// ---- Core 0: audio-rate callback ----------------------------------
	virtual void ProcessSample() override
	{
		// Startup smoke test: walk all six LEDs in sequence once, so a
		// fresh flash visibly proves LEDs, PWM and the sample clock all
		// work before any patching happens.
		if (RunStartupBlink()) return;

		// --- Noise source: cycle colour on each Down-tap of the switch.
		// SwitchChanged() fires on any transition; gating on Down here
		// means the mode advances on the press only (springs back to
		// Middle/Up without incident).
		if (SwitchVal() == Switch::Down && SwitchChanged())
		{
			noiseMode_ = (noiseMode_ + 1) % uncertainty::NoiseSource::kNumColours;
		}
		AudioOut2(noise_.Next(static_cast<uncertainty::NoiseSource::Colour>(noiseMode_)));
		LedOn(0, noiseMode_ == uncertainty::NoiseSource::Flat);
		LedOn(2, noiseMode_ == uncertainty::NoiseSource::LowBiased);
		LedOn(4, noiseMode_ == uncertainty::NoiseSource::HighBiased);

		// --- FRV: core 1 computes the value; here we just output it.
		CVOut1Millivolts(frvOutMillivolts_);
		LedBrightness(1, uncertainty::MillivoltsToLed(frvOutMillivolts_));

		// --- QRV: fresh random value on each Pulse In 1 rising edge,
		// range scaled 0..6000mV by the Y knob.
		int32_t qrvRangeMv = (KnobVal(Knob::Y) * 6000) >> 12;
		int32_t qrvMv = qrv_.Process(PulseIn1RisingEdge(), qrvRangeMv);
		CVOut2Millivolts(qrvMv);
		LedBrightness(3, uncertainty::MillivoltsToLed(qrvMv));

		// --- Wavefolder: Audio In 1 folded by Main knob's drive amount,
		// modulated bipolar by CV In 1.
		AudioOut1(wavefolder_.Process(AudioIn1(), KnobVal(Knob::Main), CVIn1()));

		// --- Comparator: fixed +-1V (0V-centred) window on Audio In 1.
		// 1V of the card's ~6V range is roughly 2047/6 codes.
		bool pulse = comparator_.Process(AudioIn1());
		PulseOut1(pulse);
		LedOn(5, pulse);
	}

private:
	// Returns true while the startup blink is still running (and has
	// already written the LEDs for this sample).
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

	static constexpr int32_t kComparatorWindow = 341;    // ~1V in ADC codes
	static constexpr int32_t kComparatorHysteresis = 20; // ~60mV deadband

	int32_t startupSample_ = 0;
	int32_t noiseMode_ = uncertainty::NoiseSource::Flat;

	uncertainty::NoiseSource noise_{1};
	uncertainty::Wavefolder wavefolder_;
	uncertainty::Comparator comparator_{kComparatorWindow, kComparatorHysteresis};
	uncertainty::QRV qrv_{12345};

	uint32_t frvSeed_ = 7;
	volatile int32_t frvOutMillivolts_ = 0;
};

int main()
{
	// 144MHz + 96k audio-input oversampling is the directive's recommended
	// default (reduces ADC tonal artifacts). No 192kHz/192MHz mode exists
	// in ComputerCard.
	set_sys_clock_khz(144000, true);

	Uncertainty card;
	card.Run();
}
