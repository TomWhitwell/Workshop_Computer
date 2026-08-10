// Fixed-point channel vocoder with spectral freeze, tracking the original
// Warps vocoder (warps/dsp/vocoder.cc) closely, restructured for per-sample
// integer processing on the RP2040.
//
// Parity with the original:
//   * 20 third-octave bands, 87 Hz .. 7040 Hz: band 0 is a low-pass, band 19
//     a high-pass, interior bands band-pass; each band is two cascaded
//     2-pole SVF sections (4-pole skirts), as in the original filter bank.
//   * Envelope follower release maps exponentially over the zone position
//     (f = 80Hz * 2^-6r, each band a third-octave faster), attack = 4x decay.
//   * Formant shift uses the original law: an exponential band-remapping
//     increment (4 * 2^-4s, so shift also stretches/compresses the spectral
//     envelope), block-smoothed peak followers as the source, 1/(1+x)
//     attenuation for bands mapped past the top, and a crossfade between
//     true per-sample vocoding (centre) and a static spectral-transfer
//     filter (extremes), via the original's double-smoothstep amount.
//   * Output limiter: 1.6x pre-gain, peak follower with fast-attack /
//     very-slow-release, gain reduction above unity, soft saturation.
//
// Deliberate divergences:
//   * Everything runs at the full 48kHz rate. The original decimates low
//     bands through polyphase SRCs purely as an STM32F4 CPU optimization;
//     full-rate processing avoids the SRC latency and phase compensation.
//   * Interior bands use two identical constant-peak sections (Q chosen so
//     the cascade's -3dB bandwidth is a third octave) instead of the
//     original's slightly staggered section frequencies.
//
// Filter and control-law constants are computed once with floats at
// construction time; the audio path is integer-only (Q14 filter
// coefficients, Q15 follower coefficients, Q12 gains).

#ifndef ALLOY_DSP_VOCODER_H_
#define ALLOY_DSP_VOCODER_H_

#include <cmath>
#include <cstdint>

#include "hardware/timer.h"
#include "pico/multicore.h"
#include "pico/platform.h"

#include "dsp/xmod_algorithms.h"

namespace xmod {

// Debug taps (read over SWD): worst-case core-1 compute time per sample,
// and worst-case time core 0 spent blocked waiting for a vocoder result.
inline volatile uint32_t g_debug_vocoder_worker_max_us = 0;
inline volatile uint32_t g_debug_vocoder_wait_max_us = 0;
// Exponential moving average of core-1 compute time, Q8 microseconds
// (divide by 256): distinguishes sustained overload from rare spikes.
inline volatile uint32_t g_debug_vocoder_worker_ema_q8 = 0;

// One band: two cascaded 2-pole sections (4-pole skirts).
//
// Topology is chosen per centre frequency:
//   * Low bands use Chamberlin SVF sections, which keep their precision
//     where a Q14 direct-form biquad's feedforward coefficients would
//     quantize to almost nothing.
//   * High bands use direct-form (RBJ) biquad sections. The Chamberlin SVF
//     goes UNSTABLE as the centre frequency approaches sample_rate/6 - this
//     is exactly why the original Warps runs its top bands at 96kHz
//     (fb_*_96000 in the design table). The biquad is stable at any centre
//     frequency, and its coefficient quantization is only a problem at low
//     frequencies, so the two forms cover each other's weaknesses.
class BandFilter
{
public:
	enum Mode { kLowPass, kBandPass, kHighPass };

	void Configure(Mode mode, float centre_hz, float q, float sample_rate)
	{
		// SVF stability margin: keep well below sample_rate/6 (~8kHz).
		use_svf_ = centre_hz < sample_rate / 24.0f; // < 2kHz at 48kHz
		mode_ = mode;
		if (use_svf_)
		{
			float f = 2.0f * sinf(3.14159265f * centre_hz / sample_rate);
			c0_ = static_cast<int32_t>(16384.0f * f + 0.5f); // f, Q14
			c1_ = static_cast<int32_t>(16384.0f / q + 0.5f); // damp, Q14
		}
		else
		{
			float w0 = 2.0f * 3.14159265f * centre_hz / sample_rate;
			float alpha = sinf(w0) / (2.0f * q);
			float a0 = 1.0f + alpha;
			// RBJ coefficients, Q14. Bandpass: b0 = alpha, b1 = 0,
			// b2 = -alpha. Highpass: b0 = b2 = (1+cos)/2, b1 = -(1+cos).
			if (mode == kHighPass)
			{
				float b = (1.0f + cosf(w0)) * 0.5f;
				c0_ = static_cast<int32_t>(16384.0f * (b / a0));
			}
			else
			{
				c0_ = static_cast<int32_t>(16384.0f * (alpha / a0));
			}
			c1_ = static_cast<int32_t>(16384.0f * (-2.0f * cosf(w0) / a0));
			c2_ = static_cast<int32_t>(16384.0f * ((1.0f - alpha) / a0));
		}
	}

	int32_t Process(int32_t x)
	{
		if (use_svf_)
		{
			// 3 headroom bits: keeps the f*state products of the lowest
			// band (f_q14 ~ 187 at 87Hz) from truncating to a deadband,
			// while worst-case products stay inside int32.
			return (SvfSection(1, SvfSection(0, x << kHeadroom)))
				>> kHeadroom;
		}
		return BiquadSection(1, BiquadSection(0, x));
	}

private:
	static constexpr int kHeadroom = 3;

	int32_t SvfSection(int s, int32_t in)
	{
		// state_[s]: [0] = lp, [1] = bp.
		int32_t lp = state_[s][0] + ((c0_ * state_[s][1]) >> 14);
		int32_t hp = in - lp - ((c1_ * state_[s][1]) >> 14);
		int32_t bp = state_[s][1] + ((c0_ * hp) >> 14);
		state_[s][0] = lp;
		state_[s][1] = bp;
		switch (mode_)
		{
		case kLowPass: return lp;
		case kHighPass: return hp;
		default:
			// Constant-peak band-pass: raw bp peaks at Q; scale by
			// 1/Q (= damp) for unity gain at the centre.
			return (bp * c1_) >> 14;
		}
	}

	int32_t BiquadSection(int s, int32_t in)
	{
		// state_[s]: [0] = x1, [1] = x2, [2] = y1, [3] = y2.
		int32_t* st = state_[s];
		// Rounded shift: plain >>14 truncates toward -inf and the -0.5LSB
		// bias integrates into DC through the resonant feedback.
		int32_t acc;
		if (mode_ == kHighPass)
		{
			// y = b0*(x - 2*x1 + x2) - a1*y1 - a2*y2
			acc = c0_ * (in - (st[0] << 1) + st[1]);
		}
		else
		{
			// y = b0*(x - x2) - a1*y1 - a2*y2   (b1 = 0, b2 = -b0)
			acc = c0_ * (in - st[1]);
		}
		int32_t y = (acc - c1_ * st[2] - c2_ * st[3] + 8192) >> 14;
		st[1] = st[0]; st[0] = in;
		st[3] = st[2]; st[2] = y;
		return y;
	}

	Mode mode_ = kBandPass;
	bool use_svf_ = true;
	int32_t c0_ = 0, c1_ = 0, c2_ = 0;
	int32_t state_[2][4] = {};
};

class Vocoder
{
public:
	Vocoder()
	{
		// Original band centres: third octaves (x 2^(1/3)), 87Hz..7040Hz.
		// Band 0 low-pass and band 19 high-pass so the whole spectrum,
		// bass and air included, passes through the vocoder.
		float f = 87.0f;
		for (int i = 0; i < kNumBands; ++i)
		{
			const BandFilter::Mode mode =
				i == 0 ? BandFilter::kLowPass
				: i == kNumBands - 1 ? BandFilter::kHighPass
				: BandFilter::kBandPass;
			// Band-pass Q per section such that the two-section cascade is
			// -3dB a sixth of an octave either side (third-octave band);
			// Butterworth damping for the end LP/HP bands.
			const float q = mode == BandFilter::kBandPass ? 2.78f : 0.707f;
			modulator_bands_[i].Configure(mode, f, q, kSampleRate);
			carrier_bands_[i].Configure(mode, f, q, kSampleRate);

			// Follower base coefficient at release = 0 (f = 80Hz at band 0,
			// a third-octave faster per band), Q15 of f/sample_rate.
			float base = 80.0f * powf(1.2599f, static_cast<float>(i));
			base_follow_q15_[i] =
				static_cast<int32_t>(32768.0f * base / kSampleRate + 0.5f);

			f *= 1.2599f; // 2^(1/3)
		}

		// 2^-x lookup, Q15, x = i/32 octaves for one octave; larger x uses
		// the integer part as a right shift.
		for (int i = 0; i <= kExp2Size; ++i)
		{
			exp2_lut_[i] = static_cast<int32_t>(
				32768.0f * powf(2.0f, -static_cast<float>(i) / kExp2Size)
				+ 0.5f);
		}
	}

	// True while the spectral envelope is held (release at the end-stop);
	// for UI feedback.
	bool frozen() const { return frozen_; }

	// Launch the band-processing worker on core 1. Must be called once,
	// before audio starts; core 1 must be otherwise unused.
	void StartWorker()
	{
		worker_instance_ = this;
		multicore_launch_core1(WorkerEntry);
	}

	// formant_shift: 0..4095, centre (2048) = neutral (TIMBRE knob, as in
	//                the original).
	// release:       0..4095 (position within the vocoder zone) = release
	//                time, short..long. As in the original, the raw control
	//                is eased with r*(2-r), and the top of the travel
	//                (eased > 0.995) freezes the spectral envelope: the
	//                carrier stays filtered by whichever formants were
	//                present just before the knob reached the end-stop.
	//
	// Two-sample pipeline: ALL band and control-rate processing runs on
	// core 1. Each call submits this sample's inputs and collects the
	// result computed from the inputs of two samples ago, so core 1 has
	// two full sample periods of elasticity: a slow sample (e.g. the
	// block-rate gain rebuild) eats pipeline slack instead of blocking
	// core 0. Core 0 keeps only the limiter. The extra latency (~42us on
	// the vocoder path only) is far below the original hardware's block
	// latency.
	int32_t __not_in_flash_func(Process)(int32_t carrier, int32_t modulator,
	                int32_t formant_shift, int32_t release)
	{
		// Original: set_release_time(r * (2 - r)); freeze when > 0.995.
		release = (release * (8192 - release)) >> 12;
		if (release > 4095) release = 4095;
		const bool freeze = release > 4075;
		frozen_ = freeze;

		int32_t out = 0;
		if (pending_ >= kPipelineDepth)
		{
			uint32_t t0 = time_us_32();
			out = static_cast<int32_t>(multicore_fifo_pop_blocking());
			uint32_t waited = time_us_32() - t0;
			if (waited > g_debug_vocoder_wait_max_us)
				g_debug_vocoder_wait_max_us = waited;
			// Join with the partial this core computed for the same sample.
			out += partial_ring_[ring_tail_];
			ring_tail_ = (ring_tail_ + 1) % kPipelineDepth;
			--pending_;
		}

		// Two words per sample: packed 12-bit audio pair, packed controls.
		multicore_fifo_push_blocking(
			(static_cast<uint32_t>(carrier) & 0xffffu) |
			(static_cast<uint32_t>(modulator) << 16));
		multicore_fifo_push_blocking(
			static_cast<uint32_t>(formant_shift) |
			(static_cast<uint32_t>(release) << 12) |
			(freeze ? 1u << 24 : 0u));

		// Core 0's own band share, computed while core 1 chews on the rest.
		// Its result joins core 1's partial for the same sample when that
		// emerges from the pipeline. Control tables (attack/decay/gains)
		// are refreshed by core 1; reads of stale-by-a-block words are
		// harmless (int32 loads are atomic on this core).
		partial_ring_[ring_head_] =
			ProcessBands(0, kSplitBand, carrier, modulator, freeze);
		ring_head_ = (ring_head_ + 1) % kPipelineDepth;
		++pending_;

		return Limit(out);
	}

private:
	static constexpr int kNumBands = 20;
	// Core 0 processes a small share of the bands to pull core 1's
	// per-sample time under the sample period (measured: 20 bands on
	// core 1 alone averaged ~21us, just over the 20.8us budget).
	static constexpr int kSplitBand = 3;
	static constexpr int kExp2Size = 32;
	static constexpr int kBlockSize = 16; // control-rate period, samples
	static constexpr float kSampleRate = 48000.0f;

	// Filter + follower + gain for bands [first, last); returns the partial
	// output sum. Core 0 runs [0, kSplitBand), core 1 the rest; the ranges
	// are disjoint so each core owns its bands' filter and envelope state.
	int32_t __not_in_flash_func(ProcessBands)(int first, int last,
		int32_t carrier, int32_t modulator, bool freeze)
	{
		int32_t out = 0;
		for (int i = first; i < last; ++i)
		{
			int32_t mod_band = modulator_bands_[i].Process(modulator);
			int32_t level = mod_band < 0 ? -mod_band : mod_band;
			// Follower gain sqrt(kNumBands) ~= 4.47; envelope in Q12 gain
			// units (4096 = unity for a full-scale +-2048 band signal):
			// 4096 * 4.47 / 2048 = 8.944 ~= 9159 / 1024.
			int32_t target = (level * 9159) >> 10;

			if (!freeze)
			{
				int32_t error = target - envelope_[i];
				int32_t coeff = error > 0 ? attack_q15_[i] : decay_q15_[i];
				envelope_[i] += (error * coeff) >> 15;
			}

			int32_t car_band = carrier_bands_[i].Process(carrier);
			int32_t gain = carrier_gain_q12_[i] +
				((vocoder_gain_q12_ * envelope_[i]) >> 12);
			out += (car_band * gain) >> 12;
		}
		return out;
	}

	static void WorkerEntry() { worker_instance_->WorkerLoop(); }

	// Core 1: the whole vocoder lives here - control-rate updates and all
	// band processing - so every piece of envelope/peak/gain state is
	// owned by exactly one core and no locking is needed. Between samples
	// (and whenever another algorithm zone is active) the core sleeps in
	// the FIFO pop.
	void __not_in_flash_func(WorkerLoop)()
	{
		for (;;)
		{
			uint32_t audio = multicore_fifo_pop_blocking();
			uint32_t controls = multicore_fifo_pop_blocking();
			int32_t carrier = static_cast<int16_t>(audio & 0xffffu);
			int32_t modulator = static_cast<int16_t>(audio >> 16);
			uint32_t t0 = time_us_32();
			int32_t formant_shift = controls & 4095;
			int32_t release = (controls >> 12) & 4095;
			bool freeze = (controls & (1u << 24)) != 0;

			// Control-rate work once per kBlockSize samples, as in the
			// original (which recomputes gains once per DSP block), but
			// staggered across consecutive samples so no single sample
			// carries all of it on top of the 20-band audio work.
			switch (block_phase_)
			{
			case 0: UpdateFollowerCoefficients(release); break;
			case 1: if (!freeze) UpdatePeaks(); break;
			case 2: UpdateFormantGains(formant_shift); break;
			default: break;
			}
			block_phase_ = (block_phase_ + 1) & (kBlockSize - 1);

			int32_t sum = ProcessBands(kSplitBand, kNumBands,
			                           carrier, modulator, freeze);
			uint32_t took = time_us_32() - t0;
			if (took > g_debug_vocoder_worker_max_us)
				g_debug_vocoder_worker_max_us = took;
			g_debug_vocoder_worker_ema_q8 = static_cast<uint32_t>(
				static_cast<int32_t>(g_debug_vocoder_worker_ema_q8) +
				((static_cast<int32_t>(took << 8) -
				  static_cast<int32_t>(g_debug_vocoder_worker_ema_q8))
				 >> 8));
			multicore_fifo_push_blocking(static_cast<uint32_t>(sum));
		}
	}

	// Block-rate peak smoothing of the envelopes (original: 0.5 up, 0.1
	// down per block); feeds the static spectral-transfer path.
	void UpdatePeaks()
	{
		for (int i = 0; i < kNumBands; ++i)
		{
			int32_t error = envelope_[i] - peak_[i];
			peak_[i] += (error * (error > 0 ? 16384 : 3277)) >> 15;
		}
	}

	// Q15 of 2^-(x/4096); x >= 0 in Q12 octaves.
	int32_t Exp2Neg(int32_t x_q12) const
	{
		int32_t shift = x_q12 >> 12;
		if (shift > 14) return 0;
		int32_t frac = x_q12 & 4095; // 0..4095 within one octave
		int32_t idx = frac >> 7;     // 32 steps
		int32_t t = frac & 127;
		int32_t v = exp2_lut_[idx] +
			(((exp2_lut_[idx + 1] - exp2_lut_[idx]) * t) >> 7);
		return v >> shift;
	}

	// Original: f = 80Hz * 2^(-72 * release / 12), each band a third-octave
	// faster; attack = 2f/sr, decay = f/2sr (4x asymmetry).
	void UpdateFollowerCoefficients(int32_t release)
	{
		// Quantize the control so knob jitter doesn't force recomputation
		// of 20 bands' coefficients every sample.
		int32_t quantized = release >> 4;
		if (quantized == cached_release_) return;
		cached_release_ = quantized;

		// 0..6 octaves down in Q12 (original: -72 semitones full travel).
		int32_t x_q12 = release * 6;
		int32_t scale_q15 = Exp2Neg(x_q12);
		for (int i = 0; i < kNumBands; ++i)
		{
			int32_t base = (base_follow_q15_[i] * scale_q15) >> 15;
			int32_t attack = base << 1;
			if (attack > 16384) attack = 16384;
			int32_t decay = base >> 1;
			if (decay < 1) decay = 1;
			attack_q15_[i] = attack;
			decay_q15_[i] = decay;
		}
	}

	// Original formant shift: exponential remap increment 4 * 2^(-4s), peak
	// followers as source, 1/(1+overshoot) attenuation past the top band,
	// and a crossfade between per-sample vocoding and the static shifted
	// spectral transfer, weighted by the double-smoothstep shift amount.
	void UpdateFormantGains(int32_t formant_shift)
	{
		// amount = smoothstep-ish of 2*|s-0.5|, applied twice. Q12.
		int32_t t = formant_shift - 2048;
		t = (t < 0 ? -t : t) << 1; // 0..4096
		int32_t amount = (t * (8192 - t)) >> 12;
		amount = (amount * (8192 - amount)) >> 12;

		// increment = 4 * 2^(-4s) in Q12 band units: 4.0 (s=0) .. 0.25 (s=1),
		// exactly 1.0 at centre (no shift, no stretch).
		int32_t increment =
			(Exp2Neg(formant_shift << 2) << 14) >> 15; // Q12

		constexpr int32_t kLast = (kNumBands - 1) << 12;
		int32_t pos = 0;
		for (int i = 0; i < kNumBands; ++i)
		{
			int32_t clamped = pos < 0 ? 0 : (pos > kLast ? kLast : pos);
			int32_t idx = clamped >> 12;
			int32_t frac = clamped & 4095;
			if (idx >= kNumBands - 1) { idx = kNumBands - 2; frac = 4095; }
			int32_t band_gain = peak_[idx] +
				(((peak_[idx + 1] - peak_[idx]) * frac) >> 12);

			int32_t overshoot = pos - kLast;
			if (overshoot > 0)
			{
				// band_gain peaks around 18k (<< 12 still fits int32), so a
				// 32-bit divide - hardware-assisted on RP2040 - suffices.
				band_gain = (band_gain << 12) / (4096 + overshoot);
			}
			pos += increment;

			carrier_gain_q12_[i] = (band_gain * amount) >> 12;
		}
		vocoder_gain_q12_ = 4096 - amount;
	}

	// Original output limiter: 1.6x pre-gain, peak follower (attack 0.05,
	// release 0.00002 per sample), unity gain until the peak exceeds full
	// scale, then 1/peak reduction, 0.8x post-gain into soft saturation.
	int32_t Limit(int32_t x)
	{
		int32_t s = (x * 6554) >> 12; // 1.6x, Q12
		int32_t mag = s < 0 ? -s : s;
		int32_t error = mag - limiter_peak_;
		if (error > 0) limiter_peak_ += (error * 1638) >> 15;
		else limiter_peak_ += error >> 16;
		if (limiter_peak_ < 64) limiter_peak_ = 64;

		int32_t gain_q12 = limiter_peak_ <= 2048
			? 4096
			: (2048 << 12) / limiter_peak_;
		int32_t y = (s * gain_q12) >> 12;
		// SoftLimit input scale is 2x the sample scale; fold in the 0.8x
		// post-gain: 2 * 0.8 = 1.6 = 1638/1024.
		return SoftLimit((y * 1638) >> 10);
	}

	BandFilter modulator_bands_[kNumBands];
	BandFilter carrier_bands_[kNumBands];
	int32_t base_follow_q15_[kNumBands] = {};
	int32_t attack_q15_[kNumBands] = {};
	int32_t decay_q15_[kNumBands] = {};
	int32_t envelope_[kNumBands] = {};
	int32_t peak_[kNumBands] = {};
	int32_t carrier_gain_q12_[kNumBands] = {};
	int32_t exp2_lut_[kExp2Size + 1] = {};
	int32_t vocoder_gain_q12_ = 4096;
	int32_t cached_release_ = -1;
	int32_t limiter_peak_ = 1024;
	// 3 results + 2 in-flight input words stays within the 8-word FIFO;
	// ~62us of vocoder-path latency, still far below audibility.
	static constexpr int kPipelineDepth = 3;
	int32_t block_phase_ = 0;
	int32_t pending_ = 0;
	bool frozen_ = false;
	int32_t partial_ring_[kPipelineDepth] = {};
	int32_t ring_head_ = 0;
	int32_t ring_tail_ = 0;
	static inline Vocoder* worker_instance_ = nullptr;
};

} // namespace xmod

#endif // ALLOY_DSP_VOCODER_H_
