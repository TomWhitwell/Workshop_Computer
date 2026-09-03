// Wavefolder, ported from Chris Johnson's proven "Utility Pair" wavefolder
// (https://github.com/chrisgjohnson/Utility-Pair, src/main.cpp,
// `wavefolder::fold_function` / `int_function` / `aa_wavefolder`).
//
// Two earlier attempts at this block (a hard triangle-reflection fold,
// then a smooth sine-table fold) both sounded wrong on real hardware.
// Both were the same mistake in different clothes: evaluating a
// nonlinear function fresh on every sample with no antialiasing.
// Wavefolding creates harmonics above what the signal had going in — a
// naive per-sample fold generates content above the 24kHz Nyquist limit,
// which does not disappear, it aliases: folds back down into the audible
// range as inharmonic noise. That happens whether the fold's corner is
// sharp (triangle) or smooth (sine) — a smoother corner just delays the
// problem to higher fold counts rather than removing it.
//
// The actual fix — proven on real hardware in Utility Pair, and written
// up in Demonstrations+HelloWorlds/PicoSDK/ComputerCard/NOTES.md under
// "Antiderivative antialiasing" (ADAA), after Parker et al., DAFx-16 —
// isn't a different fold shape at all. It's evaluating the fold's
// *antiderivative* F(x) = integral of fold(x), then taking the discrete
// slope (F(x) - F(prev_x)) / (x - prev_x) between this sample and the
// last one, instead of calling fold(x) directly. That discrete slope is
// mathematically the average of the continuous-time fold output across
// the interval between samples, which acts as a filter that suppresses
// exactly the above-Nyquist content a naive per-sample evaluation would
// have aliased. See NOTES.md for the full derivation; this file only
// needs the result.
//
// fold_function and int_function below are Chris Johnson's exact integer
// formulas — a fixed period-8192 triangle fold and its analytic integral.
// The only changes from the original are structural: his version keeps
// `lastval`/`lastx` as function-local statics inside a class template
// (correct there because Utility Pair instantiates one wavefolder per
// channel, so the template parameter gives each channel its own static
// storage) — here there's a single instance, so they're ordinary member
// variables instead. The knob/CV combination that fed `mult` in the
// original (paired-utility layout: knob X + CV in) is replaced with just
// this card's Main knob, per spec.

#ifndef UNCERTAINTY_DSP_WAVEFOLDER_H_
#define UNCERTAINTY_DSP_WAVEFOLDER_H_

#include <cstdint>

namespace uncertainty
{

	class Wavefolder
	{
	public:
		// audioIn: -2048..2047. knobMain: 0..4095 (raw KnobVal(Knob::Main)).
		// Returns the folded output, -2047..2047.
		int32_t Process(int32_t audioIn, int32_t knobMain)
		{
			// Same scaling Chris Johnson's code uses for its knob
			// contribution to `mult`: knob>>1 gives 0..2047, and `mult`
			// is a Q7 gain applied to the input before folding (mult=128
			// is unity gain, i.e. right at the edge of the fold's linear
			// region — below that the signal never reaches the fold
			// point and passes clean; above it, harder drive means more
			// folds, the way turning up a real wavefolder's drive knob
			// works).
			int32_t mult = knobMain >> 1;
			return AntialiasedFold((audioIn * mult) >> 7);
		}

	private:
		// Fixed period-8192, +-2048-amplitude triangle fold. `mult`
		// above is what makes this feel like a variable-intensity fold:
		// the nonlinearity itself never moves, only how hard the signal
		// is driven into it.
		static int32_t FoldFunction(int32_t x)
		{
			constexpr int32_t period = 8192;
			x = ((x + 2048) % period + period) % period;

			if (x < 4096)
				return x - 2048;
			else
				return (8191 - x) - 2048;
		}

		// Antiderivative (definite integral) of FoldFunction, needed by
		// the ADAA step below. Exact formula from Chris Johnson's
		// wavefolder — the piecewise-quadratic integral of a
		// piecewise-linear triangle wave, in fixed point.
		static int32_t IntegralOfFold(int32_t x)
		{
			constexpr int32_t period = 8192;
			x = ((x + 2048) % period + period) % period;
			int32_t x2 = x * 2;
			if (x < 4096)
				return ((x2 + 1) * (x2 - 8191)) >> 3;
			else
				return -((x2 - 8191) * (x2 - 16383)) >> 3;
		}

		static void Clip(int32_t &a)
		{
			if (a < -2047) a = -2047;
			if (a > 2047) a = 2047;
		}

		// The antiderivative-antialiasing step: instead of FoldFunction(x),
		// return the discrete slope of its integral between this sample
		// and the last one. Falls back to a direct evaluation only when
		// x hasn't moved (the slope would be 0/0) — at that point the
		// two are equal anyway, since there's no new content between two
		// identical samples for the filter to average over.
		int32_t AntialiasedFold(int32_t x)
		{
			int32_t result;
			if (x == lastX_)
			{
				result = FoldFunction(x);
			}
			else
			{
				int32_t val = IntegralOfFold(x);
				result = (val - lastIntegral_) / (x - lastX_);
				lastX_ = x;
				lastIntegral_ = val;
			}
			Clip(result);
			return result;
		}

		// AntialiasedFold's two branches both preserve the invariant
		// lastIntegral_ == IntegralOfFold(lastX_) (the fallback branch
		// touches neither, and the ADAA branch updates both together) —
		// but that invariant has to start true. Chris Johnson's original
		// leaves this as `static int32_t lastval=0, lastx=0`, which is
		// the same as writing lastIntegral_ = 0 here: only correct if
		// IntegralOfFold(0) happened to be 0, and it isn't (it's
		// -2097152 — this fixed-point integral formula has a nonzero
		// constant of integration even though the fold itself is odd
		// and passes through the origin). Caught by a host-side numeric
		// test: with the mismatched default, the first sample to differ
		// from the initial x=0 computed a slope against the wrong
		// reference point and landed on the -2047 clip rail instead of
		// a musical value.
		int32_t lastX_ = 0;
		int32_t lastIntegral_ = IntegralOfFold(0);
	};

} // namespace uncertainty

#endif // UNCERTAINTY_DSP_WAVEFOLDER_H_
