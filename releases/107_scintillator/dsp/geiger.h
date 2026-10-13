// The Geiger-counter gates: Pulse Out 1/2 fire stochastically when A and B
// "collide" - when one signal's direction reverses at almost the same
// moment the other's reverses the opposite way (think two particle tracks
// crossing). Named for the vibe (a scintillator crystal flashing when a
// particle hits it), not for any literal detection or averaging - there's
// no boxcar filter or radiation model here, just a sign-of-derivative
// coincidence detector.
//
// "Violence" - how far each signal travelled between its last direction
// reversal and this one - sets both the probability the gate actually
// fires (quiet collisions mostly don't, violent ones almost always do)
// and how wide the fired pulse is.
//
// Violence deliberately measures that excursion rather than the slope at
// the instant of the reversal. A smooth wave barely moves right at its
// peak, and how sharply it turns there scales with the square of its
// frequency, so slope-at-the-reversal made firing depend on pitch instead
// of level: measured on a full-scale sine, the gates were silent below
// ~220Hz and only reached full rate above ~880Hz, which meant winding an
// oscillator up to a painful pitch just to get triggers. The distance
// between reversals is the same for a loud low note as a loud high one,
// so level decides, which is what "violent" should mean here.
//
// Two things turn that into a musical trigger rate rather than an
// audio-rate one:
//
//   - The detector runs at 1/kDetectorDivider of the sample rate, so the
//     "direction" it watches is the slope across several samples rather
//     than every sample-to-sample wiggle. That makes it follow a signal's
//     larger gestures and stops high-frequency content and noise from
//     registering as constant direction changes. On its own it isn't
//     enough: measured across clean, noisy and white-noise inputs the
//     divider alone still let the rate swing from 47/s to over 2000/s,
//     and on noise the gate simply sat high the whole time.
//   - Each output then waits out a gap before it may fire again - a fixed
//     floor plus a random extra, drawn fresh after every hit. See
//     DrawGap() for why the random part is essential rather than a
//     flourish: a fixed gap alone produces a metronome, because there is
//     nearly always a collision waiting the moment it expires.
//
// With only one input patched there is nothing to collide with, so the
// gates instead fire on that signal's own direction reversals - its peaks
// and troughs - alternating between the two outputs. Without this the card
// is simply dead with one cable in: a collision needs *both* signals to
// reverse in opposite directions, so a silent second input means no
// collision can ever happen. That used to be masked by the unpatched
// input's noise floor flipping direction constantly, which is not
// something to rely on.
#pragma once

#include <cstdint>

#include "common.h"

namespace scintillator
{

	class Geiger
	{
	public:
		explicit Geiger(uint32_t seed) : rng_(seed) {}

		// Called once per sample on the same A, B fed to the argument
		// stage. The pulse timers run at full sample rate so gate widths
		// stay accurate; only the collision detection is divided down.
		void __not_in_flash_func(Process)(int32_t a, int32_t b)
		{
			if (pulse1Timer_ > 0) pulse1Timer_--;
			if (pulse2Timer_ > 0) pulse2Timer_--;
			if (sinceFire1_ < nextGap1_) sinceFire1_++;
			if (sinceFire2_ < nextGap2_) sinceFire2_++;

			// Track which inputs are actually carrying something.
			if (a != 0) silentA_ = 0; else if (silentA_ < kSilenceSamples) silentA_++;
			if (b != 0) silentB_ = 0; else if (silentB_ < kSilenceSamples) silentB_++;

			if (++dividerCount_ < kDetectorDivider) return;
			dividerCount_ = 0;

			int32_t diffA = a - lastA_;
			int32_t diffB = b - lastB_;
			lastA_ = a;
			lastB_ = b;

			int newSignA = Sign(diffA, signA_);
			int newSignB = Sign(diffB, signB_);

			bool flipA = (signA_ != 0 && newSignA != 0 && newSignA != signA_);
			bool flipB = (signB_ != 0 && newSignB != 0 && newSignB != signB_);

			// How far each signal has travelled since its last reversal.
			// Only meaningful on a flip, which is the only place it's used.
			int32_t excA = 0, excB = 0;
			if (flipA)
			{
				excA = Cabs(a - lastExtremeA_);
				lastExtremeA_ = a;
			}
			if (flipB)
			{
				excB = Cabs(b - lastExtremeB_);
				lastExtremeB_ = b;
			}

			if (windowCounter_ > 0) windowCounter_--;

			bool aActive = (silentA_ < kSilenceSamples);
			bool bActive = (silentB_ < kSilenceSamples);

			if (aActive != bActive)
			{
				// Solo mode: one input only, so fire on its own direction
				// reversals and alternate which output gets each pulse.
				// One excursion rather than two summed makes this a little
				// less likely to fire than a real collision of the same
				// size, which is reasonable enough - there genuinely is
				// less going on.
				bool flip = aActive ? flipA : flipB;
				int32_t exc = aActive ? excA : excB;
				if (flip && TryFire(exc, soloToGate1_)) soloToGate1_ = !soloToGate1_;

				if (newSignA != 0) signA_ = newSignA;
				if (newSignB != 0) signB_ = newSignB;
				return;
			}

			if (flipA)
			{
				if (windowCounter_ > 0 && windowLeader_ == kLeaderB && newSignA == -windowSign_)
				{
					// B opened the window, A's opposite-direction flip
					// completes it: a "B-led" collision.
					TryFire(windowViolence_ + excA, /*aLed=*/false);
					windowCounter_ = 0;
				}
				else
				{
					windowLeader_ = kLeaderA;
					windowSign_ = newSignA;
					windowViolence_ = excA;
					windowCounter_ = kWindowTicks;
				}
			}

			if (flipB)
			{
				if (windowCounter_ > 0 && windowLeader_ == kLeaderA && newSignB == -windowSign_)
				{
					// A opened the window, B's opposite-direction flip
					// completes it: an "A-led" collision.
					TryFire(windowViolence_ + excB, /*aLed=*/true);
					windowCounter_ = 0;
				}
				else
				{
					windowLeader_ = kLeaderB;
					windowSign_ = newSignB;
					windowViolence_ = excB;
					windowCounter_ = kWindowTicks;
				}
			}

			if (newSignA != 0) signA_ = newSignA;
			if (newSignB != 0) signB_ = newSignB;
		}

		bool Gate1Active() const { return pulse1Timer_ > 0; }
		bool Gate2Active() const { return pulse2Timer_ > 0; }

	private:
		enum Leader
		{
			kLeaderNone,
			kLeaderA,
			kLeaderB
		};

		// Run the detector at a quarter of the 48kHz sample rate.
		static constexpr int32_t kDetectorDivider = 4;
		// ~4ms coincidence window, counted in detector ticks so it stays
		// the same length in real time whatever the divider is set to.
		static constexpr int32_t kWindowTicks = (48 * 4) / kDetectorDivider;
		// Minimum spacing between pulses on one output, ~80ms - the main
		// control over how fast the gates run, and worth re-measuring
		// rather than trusting old figures, since the switch to
		// excursion-based violence made loud material fire far more
		// readily than it used to. Measured on saw, sine and white noise
		// alike: 40ms gives ~24/s, 80ms ~12/s, 120ms ~8/s, 200ms ~5/s.
		static constexpr int32_t kMinGapSamples = 48 * 60;
		// Random extra wait on top of that floor, up to this much.
		static constexpr int32_t kGapSpreadSamples = 48 * 400;
		// Violence at or above this counts as "maximum" for both firing
		// probability and pulse width.
		static constexpr int32_t kMaxViolence = 3000;
		static constexpr int32_t kMinPulseSamples = 48;  // ~1ms
		static constexpr int32_t kMaxPulseSamples = 480; // ~10ms
		// A channel counts as unpatched once it has read exactly zero for
		// this long. The input conditioner's dead zone makes an unpatched
		// jack read exactly zero, so this stands in for jack detection
		// without needing the normalisation probe.
		static constexpr int32_t kSilenceSamples = 48 * 100; // ~100ms

		// How long this output has to wait before it may fire again: a
		// fixed floor plus a random extra, drawn fresh after every hit.
		//
		// A fixed gap alone turns the output into a metronome. Once
		// anything is playing there is nearly always a collision waiting,
		// so every gate fires the instant the gap expires and the spacing
		// measured dead even. Squaring the random value weights the extra
		// wait towards short - most hits follow on fairly quickly, with
		// occasional long pauses - which is the shape a real Geiger
		// counter has, since radioactive decay is a Poisson process and
		// its intervals bunch up and then gape.
		int32_t __not_in_flash_func(DrawGap)()
		{
			int32_t r = static_cast<int32_t>(rng_.Next() & 0xFFF); // 0..4095
			int32_t extra = (r * r) >> 12;                         // still 0..4095, weighted short
			return kMinGapSamples + ((extra * kGapSpreadSamples) >> 12);
		}

		static int Sign(int32_t diff, int prevSign)
		{
			if (diff > 0) return 1;
			if (diff < 0) return -1;
			return prevSign; // a flat sample continues whatever direction was last moving
		}

		// Returns true if a pulse actually fired.
		bool __not_in_flash_func(TryFire)(int32_t violence, bool aLed)
		{
			// Refuse early rather than burning a random draw on a pulse
			// that can't happen yet.
			if (aLed ? (sinceFire1_ < nextGap1_) : (sinceFire2_ < nextGap2_)) return false;

			if (violence > kMaxViolence) violence = kMaxViolence;

			uint32_t roll = rng_.Next() % kMaxViolence;
			if (static_cast<int32_t>(roll) >= violence) return false; // thinned out - too quiet to register

			int32_t width = kMinPulseSamples + ((kMaxPulseSamples - kMinPulseSamples) * violence) / kMaxViolence;
			if (aLed)
			{
				pulse1Timer_ = width;
				sinceFire1_ = 0;
				nextGap1_ = DrawGap();
			}
			else
			{
				pulse2Timer_ = width;
				sinceFire2_ = 0;
				nextGap2_ = DrawGap();
			}
			return true;
		}

		Xorshift32 rng_;

		int32_t lastA_ = 0;
		int32_t lastB_ = 0;
		int32_t lastExtremeA_ = 0;
		int32_t lastExtremeB_ = 0;
		int signA_ = 0;
		int signB_ = 0;

		int32_t windowCounter_ = 0;
		Leader windowLeader_ = kLeaderNone;
		int windowSign_ = 0;
		int32_t windowViolence_ = 0;

		int32_t pulse1Timer_ = 0;
		int32_t pulse2Timer_ = 0;
		int32_t dividerCount_ = 0;
		int32_t sinceFire1_ = kMinGapSamples;
		int32_t sinceFire2_ = kMinGapSamples;
		int32_t nextGap1_ = kMinGapSamples;
		int32_t nextGap2_ = kMinGapSamples;
		int32_t silentA_ = kSilenceSamples;
		int32_t silentB_ = kSilenceSamples;
		bool soloToGate1_ = true;
	};

} // namespace scintillator
