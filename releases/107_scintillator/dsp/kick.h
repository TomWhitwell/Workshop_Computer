// Audio Out 2: a synthesised kick drum, triggered from Pulse In 1.
//
// This replaced a pitch-tracking VCO. A VCO that follows whatever is being
// processed can't hold a rhythm - the point of a kick here is that it
// stays put and gives the patch a steady floor to sit on while the
// argument and function stages wander around above it.
//
// It's the standard synthesised kick, the one an 808 uses: a sine whose
// pitch drops sharply at the start of each hit, under an amplitude
// envelope. The pitch sweep is what you hear as the beater hitting the
// skin, and the depth and speed of that sweep matter far more to whether
// it reads as a "kick" than anything about the body tone.
//
// Everything on the per-sample path is integer. Both envelopes decay by
// subtracting a proportion of themselves, which is an exponential curve,
// with a +1 so they always reach exact silence - a purely proportional
// decay stalls at small values and would leave a DC tail sitting on the
// output between hits.
#pragma once

#include <cmath>
#include <cstdint>

#include "common.h"

namespace scintillator
{

	class Kick
	{
	public:
		static constexpr int kNumPresets = 3;

		Kick()
		{
			for (int i = 0; i < kTableSize; i++)
			{
				float ph = 2.0f * 3.14159265358979f * static_cast<float>(i) / kTableSize;
				sineTable_[i] = static_cast<int16_t>(sinf(ph) * 2047.0f);
			}

			// Attack rate: envelope units added per sample, so bigger is
			// faster. Exponential across the control range, from ~85ms up
			// to instant.
			for (int i = 0; i <= kCtrlSteps; i++)
			{
				float t = static_cast<float>(i) / kCtrlSteps;
				attackTable_[i] = static_cast<int32_t>(16.0f * powf(4096.0f, t));
			}

			// Decay rate: the proportion of itself the envelope sheds each
			// sample, in 1/65536ths. 1 is a ~1.4s tail, 255 is ~5ms.
			for (int i = 0; i <= kCtrlSteps; i++)
			{
				float t = static_cast<float>(i) / kCtrlSteps;
				decayTable_[i] = static_cast<int32_t>(powf(255.0f, t));
				if (decayTable_[i] < 1) decayTable_[i] = 1;
			}

			// Base pitch: ~35Hz to ~160Hz, exponential so the knob is even
			// across its travel rather than bunching the useful range up at
			// one end.
			for (int i = 0; i <= kCtrlSteps; i++)
			{
				float t = static_cast<float>(i) / kCtrlSteps;
				float hz = 35.0f * powf(160.0f / 35.0f, t);
				pitchTable_[i] = static_cast<int32_t>(hz * (4294967296.0f / 48000.0f));
			}
		}

		// Start a hit. Retriggering mid-decay simply starts again.
		void __not_in_flash_func(Trigger)()
		{
			ampEnv_ = 0;
			pitchEnv_ = kEnvFull;
			attacking_ = true;
			// Every hit starts from the same point in the cycle, so the
			// thump is identical each time rather than varying with
			// whatever phase the oscillator happened to be at.
			phase_ = 0;
		}

		void NextPreset()
		{
			preset_++;
			if (preset_ >= kNumPresets) preset_ = 0;
		}

		int Preset() const { return preset_; }

		// pitchKnob is 0..4095 (MAIN). attackCv / decayCv are CV In 1 / 2,
		// -2048..2047, offsetting the preset's attack and decay.
		int32_t __not_in_flash_func(Process)(int32_t pitchKnob, int32_t attackCv, int32_t decayCv)
		{
			const EnvPreset &ps = kPresets[preset_];

			// --- Amplitude envelope ---------------------------------
			if (attacking_)
			{
				ampEnv_ += attackTable_[Ctrl(ps.attack + attackCv)];
				if (ampEnv_ >= kEnvFull)
				{
					ampEnv_ = kEnvFull;
					attacking_ = false;
				}
			}
			else if (ampEnv_ > 0)
			{
				int32_t rate = decayTable_[Ctrl(ps.decay + decayCv)];
				ampEnv_ -= (((ampEnv_ >> 8) * rate) >> 8) + 1;
				if (ampEnv_ < 0) ampEnv_ = 0;
			}

			if (ampEnv_ == 0) return 0; // silent between hits

			// --- Pitch envelope -------------------------------------
			if (pitchEnv_ > 0)
			{
				pitchEnv_ -= (((pitchEnv_ >> 8) * ps.pitchDecay) >> 8) + 1;
				if (pitchEnv_ < 0) pitchEnv_ = 0;
			}

			// Base pitch from the knob, shifted by the preset so "deep and
			// low" really does sit lower than "short and snappy" at the
			// same knob setting.
			int32_t baseWord = pitchTable_[pitchKnob >> 4];
			baseWord = (baseWord >> 12) * ps.pitchScale;

			// Sweep: the pitch starts a multiple of the base and falls to
			// it. sweepAmt is in Q12, so >>12 of baseWord scales cleanly
			// without overflowing.
			int32_t sweepAmt = (ps.sweep * pitchEnv_) >> 16;
			int32_t freqWord = baseWord + ((baseWord >> 12) * sweepAmt);

			phase_ += static_cast<uint32_t>(freqWord);

			// --- Body -----------------------------------------------
			uint32_t idx = phase_ >> (32 - kTableBits);
			uint32_t frac = (phase_ >> (32 - kTableBits - 8)) & 0xFF;
			int32_t s0 = sineTable_[idx];
			int32_t s1 = sineTable_[(idx + 1) & (kTableSize - 1)];
			int32_t body = (s0 * (256 - static_cast<int32_t>(frac)) + s1 * static_cast<int32_t>(frac)) >> 8;

			return Clip((body * ampEnv_) >> 16);
		}

		// 0..4095, for the envelope LED.
		int32_t EnvelopeLed() const
		{
			int32_t v = ampEnv_ >> 4;
			return v > 4095 ? 4095 : v;
		}

	private:
		struct EnvPreset
		{
			int32_t attack;     // index into attackTable_
			int32_t decay;      // index into decayTable_
			int32_t sweep;      // extra pitch at the start of a hit, Q12
			int32_t pitchDecay; // how fast that sweep collapses
			int32_t pitchScale; // base pitch multiplier, Q12
		};

		// Short and snappy: a hard click, gone almost immediately.
		// Tight and round: the general-purpose one.
		// Deep and low: a long sub tail, pitched well down.
		// The attack indices sit mid-range on purpose. Pushed up near the
		// top they'd already be instant, and CV In 1 could then only ever
		// make the attack slower - half the control does nothing.
		static constexpr EnvPreset kPresets[kNumPresets] = {
			{2600, 3100, 5 * 4096, 200, 5120},
			{2600, 2200, 4 * 4096, 120, 4096},
			{2200, 1100, 3 * 4096, 70, 3072},
		};

		static constexpr int kTableBits = 10;
		static constexpr int kTableSize = 1 << kTableBits;
		static constexpr int kCtrlSteps = 256;
		static constexpr int32_t kEnvFull = 65536;

		// Clamp a preset index plus its CV offset into the control range,
		// then scale to the tables' 257 entries.
		static int32_t __not_in_flash_func(Ctrl)(int32_t v)
		{
			if (v < 0) v = 0;
			if (v > 4095) v = 4095;
			return v >> 4;
		}

		int16_t sineTable_[kTableSize];
		int32_t attackTable_[kCtrlSteps + 1];
		int32_t decayTable_[kCtrlSteps + 1];
		int32_t pitchTable_[kCtrlSteps + 1];

		uint32_t phase_ = 0;
		int32_t ampEnv_ = 0;
		int32_t pitchEnv_ = 0;
		bool attacking_ = false;
		int preset_ = 1; // "tight and round" is the sensible default
	};

} // namespace scintillator
