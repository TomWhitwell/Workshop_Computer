// Fixed-point cross-modulation algorithms, modelled on the basic modes of
// Mutable Instruments Warps but rewritten in integer arithmetic for the
// FPU-less RP2040 (Cortex-M0+).
//
// Conventions (see also xmod_engine.h):
//   - Audio samples are Q12: int32_t in -2048..2047 (the ComputerCard range).
//   - Parameters are 0..4095, i.e. raw 12-bit knob values.
//   - All multiplies fit comfortably in 32 bits: |sample| <= 2^11 and
//     parameters <= 2^12, so products stay below 2^24 before shifting.

#ifndef ALLOY_DSP_XMOD_ALGORITHMS_H_
#define ALLOY_DSP_XMOD_ALGORITHMS_H_

#include <cstdint>

namespace xmod {

constexpr int32_t kSampleMax = 2047;
constexpr int32_t kSampleMin = -2048;
constexpr int32_t kParamMax = 4095;

inline int32_t Clip(int32_t x)
{
	if (x > kSampleMax) return kSampleMax;
	if (x < kSampleMin) return kSampleMin;
	return x;
}

// Equal-power crossfade between carrier and modulator, approximating the
// original's lut_xfade_in/out curves with the quadratic t*(2-t).
// parameter 0 -> carrier only, 4095 -> modulator only.
inline int32_t Xfade(int32_t carrier, int32_t modulator, int32_t parameter)
{
	// Q12 gain, ~0.707 at the midpoint instead of linear 0.5.
	int32_t fade_in = (parameter * (8190 - parameter)) >> 12;
	int32_t t = kParamMax - parameter;
	int32_t fade_out = (t * (8190 - t)) >> 12;
	return Clip((carrier * fade_out + modulator * fade_in) >> 12);
}

// Triangle fold, following the original's structure: the folded signal is
// carrier + modulator + 0.25*carrier*modulator (the ring term makes the two
// inputs interact inside the fold), scaled by the parameter (fold amount
// 0.02..1.02 of the full fold range) before folding. `bias` (0..4095, the
// original's two-parameter p_2) offsets the signal into the fold, skewing
// the folds asymmetric - rides the Main knob's position within the zone.
inline int32_t Fold(int32_t carrier, int32_t modulator, int32_t parameter,
                    int32_t bias)
{
	int32_t sum = carrier + modulator + ((carrier * modulator) >> 13);

	// Gain (0.02 + parameter) in Q12 = 82..4177, then x4 so a full-scale
	// input sweeps several fold periods like the original's LUT does.
	int32_t x = ((sum * ((82 + parameter) >> 2)) >> 8) + (bias >> 1);

	// Triangle fold into -2048..2047: map into one 8192-wide period, then
	// reflect the upper half back down.
	int32_t m = (x + 2048) & 8191;    // 0..8191 (power-of-two wrap)
	if (m >= 4096) m = 8191 - m;      // reflect: 0..4095
	return m - 2048;
}

// Integer soft clip: cubic-ish saturation of a Q12 value with |x| possibly
// exceeding the sample range. Approximates the diode "analog" character.
inline int32_t SoftLimit(int32_t x)
{
	if (x > 4095) x = 4095;
	if (x < -4096) x = -4096;
	// y = x - x^3/3 scaled for Q12 (x in -4096..4095, result in -2048..2047).
	int32_t x2 = (x * x) >> 12;          // Q12
	int32_t x3 = (x2 * x) >> 12;         // Q12
	return Clip((x - x3 / 3) >> 1);
}

// Diode transfer curve: dead zone below ~0.1, then quadratic. Integer
// version of the original's Diode() used by the analog ring modulator.
inline int32_t Diode(int32_t x)
{
	int32_t sign = x < 0 ? -1 : 1;
	int32_t d = (x < 0 ? -x : x) - 205; // dead zone 0.1 in Q11
	if (d < 0) return 0;
	return sign * ((d * d) >> 13);
}

// "Analog" ring modulation, following the original's diode ring topology:
//   ring = Diode(mod + 2*carrier) + Diode(mod - 2*carrier)
// The parameter is the post-diode gain (4x..28x) into the soft limiter, so
// timbre sweeps from clean diode ring into full saturation.
inline int32_t RingModAnalog(int32_t carrier, int32_t modulator, int32_t parameter)
{
	int32_t c2 = carrier << 1;
	int32_t ring = Diode(modulator + c2) + Diode(modulator - c2);
	// Gain (4 + 24p) in Q10: 4096..28672.
	int32_t gain = 4096 + parameter * 6;
	int64_t scaled = static_cast<int64_t>(ring) * gain >> 12;
	if (scaled > 4096) scaled = 4096;
	if (scaled < -4096) scaled = -4096;
	return SoftLimit(static_cast<int32_t>(scaled) << 1);
}

// Digital ring modulation, following the original:
//   ring = 4 * c * m * (1 + 8p);  out = ring / (1 + |ring|)
// The parameter is ring gain; the rational soft saturator (hardware divider
// on the RP2040) gives the characteristic rounded clipping.
inline int32_t RingModDigital(int32_t carrier, int32_t modulator, int32_t parameter)
{
	int32_t ring = (carrier * modulator) >> 9;   // 4x, Q11
	int64_t g = static_cast<int64_t>(ring) * (4096 + parameter * 8) >> 12;
	int32_t r = static_cast<int32_t>(g > (1 << 24) ? (1 << 24) : (g < -(1 << 24) ? -(1 << 24) : g));
	int32_t mag = r < 0 ? -r : r;
	return Clip((r * 2048) / (2048 + mag));
}

// Bitwise XOR of the two samples (two's-complement, as in the original which
// XORs 16-bit words). The parameter crossfades from the 0.7x sum into the
// XOR'd signal, matching sum + (mod - sum) * parameter.
inline int32_t Xor(int32_t carrier, int32_t modulator, int32_t parameter)
{
	// XOR as 16-bit words like the original, then back to Q12.
	int32_t x = static_cast<int16_t>((carrier << 4) ^ (modulator << 4)) >> 4;
	int32_t sum = ((carrier + modulator) * 2867) >> 12; // 0.7x
	return Clip(sum + (((x - sum) * parameter) >> 12));
}

inline int32_t Abs(int32_t x) { return x < 0 ? -x : x; }

// Comparator: faithful port of Warps' ALGORITHM_COMPARATOR. The parameter
// morphs through four comparison-derived signals:
//   direct -> threshold -> window -> window_2
inline int32_t Comparator(int32_t carrier, int32_t modulator, int32_t parameter)
{
	int32_t direct = modulator < carrier ? modulator : carrier;
	int32_t threshold = carrier > 102 ? carrier : modulator; // 0.05 in Q11
	int32_t window = Abs(modulator) > Abs(carrier) ? modulator : carrier;
	int32_t window_2 = Abs(modulator) > Abs(carrier) ? Abs(modulator) : -Abs(carrier);

	// x = parameter * 2.995 in Q12 zones (integral 0..2 + fraction).
	int32_t scaled = (parameter * 12267) >> 12;
	int32_t integral = scaled >> 12;
	int32_t fraction = scaled & 4095;

	const int32_t sequence[4] = { direct, threshold, window, window_2 };
	int32_t a = sequence[integral];
	int32_t b = sequence[integral + 1];
	return Clip(a + (((b - a) * fraction) >> 12));
}

// Comparator8: faithful port of ALGORITHM_COMPARATOR8 - an eight-stage morph
// through progressively stranger comparison/rectification combinations.
inline int32_t Comparator8(int32_t carrier, int32_t modulator, int32_t parameter)
{
	// x = parameter * 6.995 in Q12 zones (integral 0..6 + fraction).
	int32_t scaled = (parameter * 28651) >> 12;
	int32_t integral = scaled >> 12;
	int32_t fraction = scaled & 4095;

	bool m_less = modulator < carrier;
	bool m_bigger = Abs(modulator) > Abs(carrier);

	int32_t y_1, y_2;
	switch (integral)
	{
	case 0:
		y_1 = modulator + carrier;
		y_2 = m_less ? modulator : carrier;
		break;
	case 1:
		y_1 = m_less ? modulator : carrier;
		y_2 = (m_less ? Abs(carrier) : Abs(modulator)) * 2 - 2048;
		break;
	case 2:
		y_1 = (m_less ? Abs(carrier) : Abs(modulator)) * 2 - 2048;
		y_2 = m_less ? -carrier : modulator;
		break;
	case 3:
		y_1 = m_less ? -carrier : modulator;
		y_2 = m_bigger ? modulator : carrier;
		break;
	case 4:
		y_1 = m_bigger ? modulator : carrier;
		y_2 = m_bigger ? Abs(modulator) : -Abs(carrier);
		break;
	case 5:
		y_1 = m_bigger ? Abs(modulator) : -Abs(carrier);
		y_2 = carrier > 102 ? carrier : modulator;
		break;
	default:
		y_1 = carrier > 102 ? carrier : modulator;
		y_2 = carrier > 102 ? carrier : -Abs(modulator);
		break;
	}
	return Clip(y_1 + (((y_2 - y_1) * fraction) >> 12));
}

// Continuous analog <-> digital ring modulation morph, following
// ALGORITHM_RING_MODULATION: the timbre knob morphs between the two ring
// modulators, and `character` (the ring gain of both, the original's p_2)
// rides the Main knob's position within the zone.
inline int32_t RingModMorph(int32_t carrier, int32_t modulator,
                            int32_t parameter, int32_t character)
{
	int32_t analog = RingModAnalog(carrier, modulator, character);
	int32_t digital = RingModDigital(carrier, modulator, character);
	// parameter 0 -> digital, 4095 -> analog (matches y_2 + (y_1 - y_2) * p).
	return Clip(digital + (((analog - digital) * parameter) >> 12));
}

// Chebyschev polynomial waveshaper core: shapes a single Q11 sample
// (|x| <= 2047) through T_2..T_8, morphing continuously between adjacent
// orders as the parameter rises.
inline int32_t ChebyschevShape(int32_t x, int32_t parameter)
{
	// parameter 0..4095 -> order position 1.0..16.0 (the original parasites
	// mode uses degree 16), Q12 fraction between integer orders.
	int32_t scaled = 4096 + parameter * 15;     // Q12, 1.0..16.0
	int32_t order = scaled >> 12;               // 1..16
	int32_t fraction = scaled & 4095;

	// Recurrence T_{n+1} = 2 x T_n - T_{n-1}, all Q11.
	int32_t t_prev = 2048; // T_0 = 1
	int32_t t_cur = x;     // T_1 = x
	int32_t t_at_order = x, t_at_next = x;
	for (int32_t n = 2; n <= 16; ++n)
	{
		int32_t t_next = ((x * t_cur) >> 10) - t_prev;
		t_prev = t_cur;
		t_cur = t_next;
		if (n == order) t_at_order = t_cur;
		if (n == order + 1) { t_at_next = t_cur; break; }
	}
	if (order >= 16) t_at_next = t_at_order = t_cur;

	return Clip((t_at_order * (4095 - fraction) + t_at_next * fraction) >> 12);
}

// Chebyschev waveshaper zone: shapes the (already clipped) sum of both inputs.
inline int32_t Chebyschev(int32_t carrier, int32_t modulator, int32_t parameter)
{
	return ChebyschevShape(Clip((carrier + modulator) >> 1), parameter);
}

// COMPARATOR_CHEBYSCHEV combo: the comparator's output fed through the
// waveshaper, scaled by 0.8 as in the original. The knob drives the
// comparator morph; the shaper order is fixed mid-range (roughly T_5).
inline int32_t ComparatorChebyschev(int32_t carrier, int32_t modulator, int32_t parameter)
{
	constexpr int32_t kShaperOrder = 2048;
	int32_t x = Comparator8(carrier, modulator, parameter);
	x = ChebyschevShape(x, kShaperOrder);
	return Clip((x * 3277) >> 12); // * 0.8
}

// OR-quantization stage of the bitcrusher: degrades one Q12 sample against
// a stepped constant (37 steps, driven quadratically by `quantize`),
// interpolating between adjacent steps. Returns the degraded 16-bit word
// (sample << 4); callers combine words and shift back to Q12.
inline int32_t BitcrushWord(int32_t sample, int32_t quantize)
{
	// z = quantize^2 * 37 in Q12 steps.
	int32_t z = ((quantize * quantize) >> 12) * 37;
	int32_t z_integral = z >> 12;
	int32_t z_fraction = z & 4095;
	int16_t z_short_1 = static_cast<int16_t>(z_integral * 32768 / 37);
	int16_t z_short_2 = static_cast<int16_t>((z_integral + 1) * 32768 / 37);

	int16_t s16 = static_cast<int16_t>(Clip(sample) << 4);
	int32_t s_1 = s16 | z_short_1, s_2 = s16 | z_short_2;
	return s_1 + (((s_2 - s_1) * z_fraction) >> 12);
}

// Degrade one Q12 sample and return it in Q12 (for the bit-degraded aux
// output, as in the parasites dual bit-mangler's stereo trick).
inline int32_t BitcrushSample(int32_t sample, int32_t quantize)
{
	return Clip(static_cast<int16_t>(BitcrushWord(sample, quantize)) >> 4);
}

// Bitcrusher: faithful port of ALGORITHM_BITCRUSHER. Both inputs are
// OR-quantized against a stepped constant (37 steps, driven quadratically
// by `quantize`), then combined by one of four bitwise/arithmetic operators
// morphed by `parameter`:
//   sum -> OR -> XOR -> shift
inline int32_t Bitcrusher(int32_t carrier, int32_t modulator,
                          int32_t parameter, int32_t quantize)
{
	int16_t as = static_cast<int16_t>(BitcrushWord(carrier, quantize));
	int16_t bs = static_cast<int16_t>(BitcrushWord(modulator, quantize));

	int32_t ops[4];
	ops[0] = static_cast<int16_t>(as + bs);
	ops[1] = static_cast<int16_t>(as | bs);
	ops[2] = static_cast<int16_t>(as ^ bs);
	ops[3] = static_cast<int16_t>(as << ((bs >> 12) & 7));

	// Interpolate between adjacent operators: parameter * 3 in Q12.
	int32_t p = parameter * 3;
	int32_t p_integral = p >> 12;
	int32_t p_fraction = p & 4095;
	if (p_integral >= 3) { p_integral = 2; p_fraction = 4095; }
	int32_t y = ops[p_integral] +
	            (((ops[p_integral + 1] - ops[p_integral]) * p_fraction) >> 12);
	return Clip(y >> 4);
}

} // namespace xmod

#endif // ALLOY_DSP_XMOD_ALGORITHMS_H_
