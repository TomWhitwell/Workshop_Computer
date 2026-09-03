// Buchla-lineage wavefolder (not a Source of Uncertainty circuit itself,
// but built in the same spirit to sit alongside it on the card).
//
// This card's Audio In 1 is expected to carry a sine-ish signal (an
// external VCO, typically), and folding that with a hard-cornered
// triangle-reflection mirror sounds wrong: every reflection has a sharp
// corner, and a sharp corner in a smooth waveform is a burst of high
// harmonics with no natural roll-off — audibly harsh and alias-prone,
// especially as fold count increases. That's not a Buchla-style timbre,
// it's a fold that assumes a triangle core (like a 259) where the
// waveform is already all corners, so a corner-for-corner reflection lines
// up naturally.
//
// The fix used here treats the input sample as a phase and looks it up in
// a precomputed sine table, the same integer LUT + linear-interpolation
// technique the ComputerCard "sine_wave_lookup" example uses to draw an
// oscillator. A fixed-point multiplier on the phase before the lookup —
// set by CV In 1 — controls how many table cycles in's own -2048..2047
// excursion covers. Two things matter for this to sound like a folder and
// not a rectifier:
//
//   Zero-centred: in=0 must land on phase=0 (table value 0), and the
//                 mapping must be odd (f(-x) = -f(x)), or negative input
//                 gets pushed toward positive output — an accidental
//                 half-wave-rectify-ish DC bias, not a fold. That means
//                 doing the phase math in *signed* arithmetic, scaling
//                 the true signed angle, and only converting to unsigned
//                 (which wraps mod 2^32 for free — exactly the table's
//                 periodicity) at the very last step for the lookup.
//                 Biasing in to unsigned first and scaling *that* breaks
//                 the symmetry: halving an already-wrapped-positive
//                 representation of a negative angle does not equal
//                 halving the negative angle itself.
//   0.5 cycles (minimum): in's swing covers half a table revolution,
//                 -90deg to +90deg, where sin() is monotonic. That's a
//                 soft shaper (curved, but every input still maps to a
//                 unique output, and negative in gives negative out) —
//                 no actual folding yet. Anything more starts folding, so
//                 0.5 is the "just barely no folding" starting point.
//   N cycles (higher CV): in's swing covers N table revolutions, i.e.
//                 output rises and falls N times — N-ish folds, each one
//                 a smooth sine corner instead of a sharp mirror.
//
// This is the same shape a sine carrier phase-modulated by itself
// produces (sin(k*theta) for input theta), which is the natural way to
// think about "folding a sine": table lookups and one signed 32x32->64
// multiply per sample, no sinf() in the audio path.

#ifndef UNCERTAINTY_DSP_WAVEFOLDER_H_
#define UNCERTAINTY_DSP_WAVEFOLDER_H_

#include <cmath>
#include <cstdint>

namespace uncertainty
{

	class Wavefolder
	{
	public:
		Wavefolder()
		{
			// Built once at startup with double-precision sin() — this
			// never runs inside ProcessSample, so there's no budget
			// concern here the way there would be for a per-sample sinf().
			for (unsigned i = 0; i < kTableSize; i++)
			{
				table_[i] = static_cast<int16_t>(32000.0 * sin(2.0 * i * M_PI / double(kTableSize)));
			}
		}

		// in: audio input, -2048..2047.
		// intensity: fold control, -2048..2047 (raw CV in), higher = more
		// folds. Only the positive half is used, matching a unipolar
		// "amount" control fed from CV.
		int32_t Process(int32_t in, int32_t intensity) const
		{
			int32_t clamped = intensity;
			if (clamped < 0) clamped = 0;
			if (clamped > 2047) clamped = 2047;

			// Cycle count, Q8 fixed point: 0.5 cycles (soft shaper, no
			// folding) at minimum, up to kMaxCycles (dense folding) at
			// full CV.
			constexpr int32_t kMinCyclesQ8 = 128;         // 0.5 << 8
			constexpr int32_t kMaxCyclesQ8 = 20 * 256;    // 20 << 8
			int32_t cyclesQ8 = kMinCyclesQ8 + ((clamped * (kMaxCyclesQ8 - kMinCyclesQ8)) >> 11);

			// Signed angle where +-2^31 is +-one full revolution — in's own
			// range (-2048..2047) covers almost exactly that at cyclesQ8
			// = 1.0. `in * (1<<20)` cannot overflow int32_t (its extreme,
			// -2048 * 2^20 = -2^31, is exactly INT32_MIN, still
			// representable), so this is safe as a plain 32-bit multiply.
			int32_t signedPhase = in * (1 << 20);

			// Scale by the cycle count in signed 64-bit (the product can
			// exceed 32 bits: a full-scale signedPhase times a ~14-bit
			// cyclesQ8), then truncate to uint32_t. That truncation is a
			// well-defined mod-2^32 reduction, which is exactly the
			// table's periodicity — so scaling by a fractional cycle
			// count and wrapping into table range fall out of the same
			// cast for free.
			uint32_t phase = static_cast<uint32_t>(
				(static_cast<int64_t>(signedPhase) * cyclesQ8) >> 8);

			// Same table read + linear interpolation as sine_wave_lookup:
			// top 9 bits select the table entry, the next chunk is the
			// fractional position between it and the next entry.
			uint32_t index = phase >> 23;
			int32_t frac = (phase & 0x7FFFFF) >> 7;

			int32_t s1 = table_[index];
			int32_t s2 = table_[(index + 1) & kTableMask];

			// Interpolate, then rescale from the table's ~15-bit amplitude
			// down to our 12-bit audio range.
			return (s2 * frac + s1 * (65536 - frac)) >> 20;
		}

	private:
		static constexpr unsigned kTableSize = 512;
		static constexpr uint32_t kTableMask = kTableSize - 1;

		int16_t table_[kTableSize];
	};

} // namespace uncertainty

#endif // UNCERTAINTY_DSP_WAVEFOLDER_H_
