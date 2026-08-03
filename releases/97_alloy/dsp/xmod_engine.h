// Meta-style algorithm sweep for the fixed-point cross-modulator.
//
// The algorithm control (0..4095) is divided into kNumAlgorithms zones, and
// like Warps' META mode the output crossfades between adjacent algorithms
// around each zone boundary rather than hard-switching. The position WITHIN
// a zone doubles as a secondary parameter for algorithms that have one
// (fold bias, ring-mod character, freq-shifter feedback, delay feedback,
// doppler room size, vocoder release), mirroring the original META sweep.
//
// The Y control is per-zone: the frequency shifter (sideband crossfade),
// bitcrusher (operator morph), and doppler (depth) use it as a third
// algorithm parameter; every other zone uses it as input drive.
//
// The waveshaping zones (fold, ring mods, xor) are processed 2x oversampled
// to tame aliasing (the original runs them 6x oversampled).
//
// NOTE: Engine owns two delay buffers (~64 KB); instances must live in
// static storage, not on the (2 KB) RP2040 stack.

#ifndef ALLOY_DSP_XMOD_ENGINE_H_
#define ALLOY_DSP_XMOD_ENGINE_H_

#include <cstdint>

#include "dsp/delay.h"
#include "dsp/frequency_shifter.h"
#include "dsp/vocoder.h"
#include "dsp/xmod_algorithms.h"

namespace xmod {

enum Algorithm : int32_t
{
	kXfade = 0,
	kFold,
	kRingModAnalog,
	kRingModDigital,
	kRingModMorph,
	kXor,
	kComparator,
	kComparator8,
	kComparatorChebyschev,
	kChebyschev,
	kBitcrusher,
	kFrequencyShifter,
	kEchoDelay,
	kStereoDoppler,
	kVocoder,
	kNumAlgorithms,
};

class Engine
{
public:
	// algorithm_control, parameter, y: 0..4095 (knob values, possibly
	// CV-summed and clamped by the caller).
	// The Y control is per-zone: zones with a more interesting third
	// parameter use it directly (frequency shifter sideband crossfade,
	// bitcrusher operator morph, doppler depth); everywhere else it is
	// input drive, with a floor so signal always flows at knob zero.
	void SetControls(int32_t algorithm_control, int32_t parameter, int32_t y)
	{
		// Real pots rarely reach the full ADC range (observed max ~3725 of
		// 4095 on hardware). Rescale by ~1.12 so a maxed knob lands solidly
		// at 4095, otherwise the last zone is unreachable.
		algorithm_control = (algorithm_control * 4589) >> 12;
		if (algorithm_control > 4095) algorithm_control = 4095;

		// Position within the sweep: integer zone plus 0..4095 fraction.
		int32_t scaled = algorithm_control * kNumAlgorithms;
		zone_ = scaled >> 12;
		if (zone_ >= kNumAlgorithms) zone_ = kNumAlgorithms - 1;
		zone_fraction_ = scaled & 4095;

		parameter_ = parameter;
		y_ = y;

		// Drive floor ~10%: Q12 gain 0.1x..2.0x.
		drive_gain_q12_ = 410 + ((y * (8192 - 410)) >> 12);
	}

	// True for zones where the Y control is repurposed as a third algorithm
	// parameter instead of input drive.
	static bool UsesYAsParameter(int32_t algorithm)
	{
		return algorithm == kBitcrusher
			|| algorithm == kFrequencyShifter
			|| algorithm == kStereoDoppler;
	}

	// Launch the vocoder's core-1 band worker. Call once before audio
	// starts; core 1 must not be used for anything else.
	void StartVocoderWorker() { vocoder_.StartWorker(); }

	// Zone index (0..kNumAlgorithms-1), for UI feedback.
	int32_t zone() const { return zone_; }

	// True while the vocoder is holding its captured spectral envelope
	// (release knob at the end-stop), for UI feedback.
	bool vocoder_frozen() const
	{
		return zone_ == kVocoder && vocoder_.frozen();
	}

	// True when the current zone produces its own aux/right output; the
	// caller should then route aux() to Audio Out 2 instead of the dry sum.
	bool has_stereo_aux() const
	{
		return zone_ == kStereoDoppler
			|| zone_ == kFrequencyShifter
			|| zone_ == kBitcrusher;
	}

	int32_t aux() const
	{
		switch (zone_)
		{
		case kFrequencyShifter: return frequency_shifter_.aux();
		case kBitcrusher: return bitcrush_aux_;
		case kStereoDoppler: return stereo_doppler_.aux();
		default: return 0;
		}
	}

	int32_t Process(int32_t carrier, int32_t modulator)
	{
		// Input drive with saturation (gain up to 2x, soft-limited), except
		// in zones where Y is an algorithm parameter (unity gain there).
		if (!UsesYAsParameter(zone_))
		{
			carrier = SoftLimit((carrier * drive_gain_q12_) >> 11);
			modulator = SoftLimit((modulator * drive_gain_q12_) >> 11);
		}

		int32_t out = RunAlgorithm(zone_, carrier, modulator);

		// Crossfade into the next algorithm across the top quarter of the
		// zone, so most of each zone is the pure algorithm.
		constexpr int32_t kFadeStart = 3072; // last 25% of the zone
		if (zone_fraction_ > kFadeStart && zone_ + 1 < kNumAlgorithms)
		{
			int32_t next = RunAlgorithm(zone_ + 1, carrier, modulator);
			int32_t t = (zone_fraction_ - kFadeStart) << 2; // 0..4092
			out = (out * (4095 - t) + next * t) >> 12;
		}

		previous_carrier_ = carrier;
		previous_modulator_ = modulator;
		return Clip(out);
	}

private:
	// 2x-oversampled dispatch for the aliasing-prone waveshapers: run the
	// algorithm on the midpoint of the previous and current inputs as well
	// as the current inputs, and average (a crude half-band decimator, but
	// markedly cleaner than none at these fold/ring gains).
	template <typename Fn>
	int32_t Oversample2x(int32_t carrier, int32_t modulator, Fn algorithm)
	{
		int32_t mid_c = (carrier + previous_carrier_) >> 1;
		int32_t mid_m = (modulator + previous_modulator_) >> 1;
		int32_t y_mid = algorithm(mid_c, mid_m);
		int32_t y = algorithm(carrier, modulator);
		return (y_mid + y) >> 1;
	}

	int32_t RunAlgorithm(int32_t algorithm, int32_t carrier, int32_t modulator)
	{
		const int32_t p = parameter_;
		const int32_t f = zone_fraction_;
		switch (algorithm)
		{
		case kXfade:
			return Xfade(carrier, modulator, p);
		case kFold:
			return Oversample2x(carrier, modulator,
				[p, f](int32_t c, int32_t m) { return Fold(c, m, p, f); });
		case kRingModAnalog:
			return Oversample2x(carrier, modulator,
				[p](int32_t c, int32_t m) { return RingModAnalog(c, m, p); });
		case kRingModDigital:
			return Oversample2x(carrier, modulator,
				[p](int32_t c, int32_t m) { return RingModDigital(c, m, p); });
		case kRingModMorph:
			return Oversample2x(carrier, modulator,
				[p, f](int32_t c, int32_t m) { return RingModMorph(c, m, p, f); });
		case kXor:
			return Oversample2x(carrier, modulator,
				[p](int32_t c, int32_t m) { return Xor(c, m, p); });
		case kComparator:
			return Comparator(carrier, modulator, p);
		case kComparator8:
			return Comparator8(carrier, modulator, p);
		case kComparatorChebyschev:
			return ComparatorChebyschev(carrier, modulator, p);
		case kChebyschev:
			return Chebyschev(carrier, modulator, p);
		case kBitcrusher:
			// X = degradation amount, Y = operator morph (parasites order);
			// aux gets the degraded dry mix for the stereo trick.
			bitcrush_aux_ =
				BitcrushSample(Clip((carrier + modulator) >> 1), p);
			return Bitcrusher(carrier, modulator, y_, p);
		case kFrequencyShifter:
			// X = shift, Y = up/down sideband crossfade, zone fraction =
			// feedback; aux carries the opposite sideband mix.
			return frequency_shifter_.Process(carrier, modulator, p, y_, f);
		case kEchoDelay:
			return echo_delay_.Process(carrier, modulator, p, f);
		case kStereoDoppler:
			// X = left/right, Y = depth, zone fraction = room size.
			return stereo_doppler_.Process(carrier, modulator, p, y_, f);
		case kVocoder:
			return vocoder_.Process(carrier, modulator, p, f);
		default:
			return Clip((carrier + modulator) >> 1);
		}
	}

	int32_t zone_ = 0;
	int32_t zone_fraction_ = 0;
	int32_t parameter_ = 0;
	int32_t y_ = 2048;
	int32_t drive_gain_q12_ = 4096;
	int32_t bitcrush_aux_ = 0;
	int32_t previous_carrier_ = 0;
	int32_t previous_modulator_ = 0;

	FrequencyShifter frequency_shifter_;
	EchoDelay echo_delay_;
	StereoDoppler stereo_doppler_;
	Vocoder vocoder_;
};

} // namespace xmod

#endif // ALLOY_DSP_XMOD_ENGINE_H_
