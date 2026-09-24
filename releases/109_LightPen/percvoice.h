// percvoice.h — the barcode's voice: two drum voices, a noise source and one
// filter, driven by the edges of a scanned code.
//
// Header-only and inlined on purpose, like sensors.h: this ends up inside
// BarcodeMode::AudioTick, which is __not_in_flash_func, and one XIP miss
// would cost more than the whole voice.
//
// Two voices, statically assigned: dark edges own one, light edges the other.
// That is exactly enough — a kick and a snare always ring together, and there
// is no stealing, no allocation and no priority logic to get wrong.
//
// Each voice has TWO envelopes, one for its tone and one for its noise, which
// is what separates a kick (long body, 2ms click) from a snare (short body,
// long hiss) with the same three lines of arithmetic.

#pragma once
#include <cstdint>
#include "fastmath.h"
#include "osc.h"
#include "svf.h"

namespace lp {

enum class Kit : uint8_t { Off, KickSnare, Clicks, Crackle, Noise, kCount };

/// Where a voice takes its noise from. One filter is shared, and its three
/// simultaneous taps cover every kit: the snare's band, the clicks' dull and
/// bright pair, and the crackle's high-pass.
enum class Tap : uint8_t { Raw, Lp, Bp, Hp };

struct HitSpec
{
	uint32_t inc0;        // starting pitch
	uint32_t incFloor;    // pitch it falls to
	uint32_t inc2;        // second body oscillator, 0 for none
	uint8_t  sweepShift;  // how fast the pitch falls
	uint8_t  envShift;    // tone decay
	uint8_t  nEnvShift;   // noise decay
	uint16_t noiseMix;    // 0 all tone .. 256 all noise, so it needs 9 bits
	Wave     wave;
	Tap      tap;
	uint16_t gain;        // Q8
};

struct KitSpec
{
	HitSpec  dark;        // bright -> dark
	HitSpec  light;       // dark -> bright
	int32_t  cutU;        // the shared filter, unipolar Q16
	int32_t  resU;
};

/// One drum voice. env and nEnv start at 1<<24 and decay by a shift PLUS ONE:
/// a plain shift stalls at a small non-zero value and leaves DC on the output,
/// the same trap slew_exact and the SVF's fractional states exist to avoid.
struct PercVoice
{
	uint32_t phase = 0, phase2 = 0;
	uint32_t inc = 0, inc2 = 0, incFloor = 0;
	int32_t  pitchDiff = 0;
	int32_t  env = 0, nEnv = 0;
	uint8_t  sweepShift = 8, envShift = 10, nEnvShift = 8;
	uint16_t noiseMix = 0;
	Wave     wave = Wave::Sine;
	Tap      tap = Tap::Raw;
	uint16_t gain = 128;

	void Strike(const HitSpec &s, int32_t incFloorOverride, uint8_t envShiftOverride)
	{
		inc = s.inc0;
		incFloor = incFloorOverride;
		inc2 = s.inc2;
		pitchDiff = (s.inc0 > incFloor) ? static_cast<int32_t>(s.inc0 - incFloor) : 0;
		sweepShift = s.sweepShift;
		envShift = envShiftOverride;
		nEnvShift = s.nEnvShift;
		noiseMix = s.noiseMix;
		wave = s.wave;
		tap = s.tap;
		gain = s.gain;
		env = 1 << 24;
		nEnv = 1 << 24;
	}

	/// Restrike the noise half only — the crackle kit's grains.
	void StrikeNoise(uint8_t shift, uint16_t g, Tap t)
	{
		nEnvShift = shift;
		gain = g;
		tap = t;
		noiseMix = 256;
		env = 0;
		nEnv = 1 << 24;
	}

	bool Idle() const { return env <= 0 && nEnv <= 0; }

	int32_t __attribute__((always_inline)) Render(int32_t raw, const Svf &svf)
	{
		if (Idle()) return 0;

		int32_t body = 0;
		if (env > 0)
		{
			phase += inc;
			body = wave_q15(wave, phase) >> 4;                  // +/-2047
			if (inc2)
			{
				phase2 += inc2;
				body = (body + (wave_q15(wave, phase2) >> 4)) >> 1;
			}
			body = (body * (env >> 9)) >> 15;
			env -= (env >> envShift) + 1;
			if (env < 0) env = 0;

			// Exponential fall toward the floor, not a linear ramp — this is
			// what makes a kick a kick rather than a click and then a tone.
			if (pitchDiff > 0)
			{
				pitchDiff -= (pitchDiff >> sweepShift) + 1;
				if (pitchDiff < 0) pitchDiff = 0;
				inc = incFloor + static_cast<uint32_t>(pitchDiff);
			}
		}

		int32_t noise = 0;
		if (nEnv > 0)
		{
			int32_t src = raw;
			if (tap == Tap::Lp) src = svf.Lp();
			else if (tap == Tap::Bp) src = svf.Bp();
			else if (tap == Tap::Hp) src = svf.Hp();
			noise = (src * (nEnv >> 9)) >> 15;
			nEnv -= (nEnv >> nEnvShift) + 1;
			if (nEnv < 0) nEnv = 0;
		}

		int32_t s = ((body * (256 - noiseMix)) + (noise * noiseMix)) >> 8;
		return (s * gain) >> 8;
	}
};

/// The kits. Frequencies and decays are chosen to sound like their names:
/// the snare's two bodies are deliberately NOT a simple ratio, because two
/// harmonically related bodies read as a tom.
static const KitSpec kKits[static_cast<int>(Kit::kCount)] = {
	// Off — never rendered.
	{},
	// Kick + snare.
	{
		{ HzToInc(110), HzToInc(50), 0, 9, 11, 4, 20, Wave::Sine, Tap::Raw, 140 },
		{ HzToInc(230), HzToInc(185), HzToInc(269), 8, 9, 10, 170, Wave::Tri, Tap::Bp, 110 },
		45003, 20000,     // 1600Hz, Q ~1.0: a broad band for the snare
	},
	// Clicks, one for each direction of travel: a dull tok and a bright tik.
	{
		{ HzToInc(700), HzToInc(700), 0, 8, 6, 4, 110, Wave::Sine, Tap::Lp, 128 },
		{ HzToInc(2100), HzToInc(2100), 0, 8, 5, 3, 150, Wave::Sine, Tap::Hp, 128 },
		45003, 52000,     // Q ~2.8, so the clicks have pitch
	},
	// Crackle: a dust thump into dark, a tick into light, and grains between.
	{
		{ HzToInc(120), HzToInc(120), 0, 8, 7, 5, 200, Wave::Sine, Tap::Raw, 150 },
		{ HzToInc(400), HzToInc(400), 0, 8, 5, 3, 256, Wave::Sine, Tap::Hp, 150 },
		41494, 20000,     // 1200Hz high-passed: surface noise, not thumps
	},
	// Shaped noise — continuous, so the hit specs are unused.
	{ {}, {}, 45003, 20000 },
};

/// Everything the barcode mode needs to make a sound.
class Perc
{
public:
	void SetKit(Kit k)
	{
		kit_ = k;
		const KitSpec &s = kKits[static_cast<int>(k)];
		svf_.Reset();
		svf_.Set(s.cutU, s.resU);
		a_.env = a_.nEnv = b_.env = b_.nEnv = 0;
		level_ = accent_ = 0;
	}

	Kit Current() const { return kit_; }

	/// An element boundary. dark: the element just entered is a dark run, so
	/// the edge was bright -> dark. widthQ16 is that element's width against
	/// the take's average, where 32768 is average.
	void Trigger(bool dark, int32_t widthQ16)
	{
		if (kit_ == Kit::Off) return;
		const KitSpec &s = kKits[static_cast<int>(kit_)];
		accent_ = 16384;

		if (kit_ == Kit::Noise) return;   // continuous: the boundary only accents

		const HitSpec &h = dark ? s.dark : s.light;
		// A wide bar is a lower, longer hit: +/- half an octave, and one more
		// shift of decay. Level stays put, so the kit stays balanced.
		int32_t octQ12 = ((32768 - widthQ16) * 2048) >> 15;
		uint32_t floorInc = static_cast<uint32_t>(
			pow2_scale(static_cast<int32_t>(h.incFloor), octQ12));
		uint8_t decay = static_cast<uint8_t>(
			clamp_i32(h.envShift + (widthQ16 >> 15), 4, 14));

		if (kit_ == Kit::Crackle)
		{
			// Grain density follows the code: dark runs crackle hard.
			density_ = dark ? 80 : 7;
			int32_t scaled = (density_ * (32768 + widthQ16)) >> 15;
			density_ = clamp_i32(scaled, 3, 280);
		}
		(dark ? a_ : b_).Strike(h, floorInc, decay);
	}

	/// Control rate, for the continuous kit: brightness opens the filter,
	/// width sharpens it, and the dark/light gate sets the level.
	void SetTone(int32_t brightQ16, int32_t widthQ16, bool dark)
	{
		if (kit_ != Kit::Noise) return;
		svf_.Set(brightQ16, 20000 + ((widthQ16 * 40000) >> 16));
		target_ = dark ? 32767 : 8192;
	}

	int32_t __attribute__((always_inline)) Render()
	{
		if (kit_ == Kit::Off) return 0;

		int32_t raw = rand_audio(rng_);
		svf_.Process(raw);

		if (kit_ == Kit::Noise)
		{
			// Band-pass, not low-pass: its level barely moves as the cutoff
			// sweeps, where a low-pass would ride 30dB from black to white.
			level_ = slew_exact(level_, target_, 7);
			int32_t amp = level_ + accent_;
			if (accent_ > 0) accent_ -= (accent_ >> 8) + 1;
			return (svf_.Bp() * (amp > 32767 ? 32767 : amp)) >> 15;
		}

		if (kit_ == Kit::Crackle && density_ > 0 && rand_q16(rng_) < density_)
		{
			// Mostly small, occasionally big — the shape of real surface noise.
			b_.StrikeNoise(5, static_cast<uint16_t>(90 >> (xorshift32(rng_) & 3)), Tap::Hp);
		}

		return a_.Render(raw, svf_) + b_.Render(raw, svf_);
	}

private:
	PercVoice a_, b_;          // a_ = dark edges, b_ = light edges and grains
	Svf       svf_;
	uint32_t  rng_ = 0x1B3F5D79u;
	Kit       kit_ = Kit::Off;
	int32_t   density_ = 0;    // grains per sample, out of 65536
	int32_t   level_ = 0, target_ = 0, accent_ = 0;
};

} // namespace lp
