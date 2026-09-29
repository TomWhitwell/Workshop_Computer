// sensors.cpp — turning five captures into a response table, an un-mix
// matrix and a gain curve. All of it runs once, at boot.
//
// THE TWO-POINT PATH is unchanged from the card's first calibration: each
// channel is spread evenly across log resistance between its own black and
// white, because an LDR's resistance is a power law in light and a plain
// linear-in-voltage map is badly compressed at the bright end. The matrix is
// identity and the gain table is unity, so that path is bit-for-bit what it
// always was.
//
// THE FIVE-POINT PATH adds colour. Three gelled LDRs in one tip do not see
// three colours; they see three overlapping mixtures of them, plus whatever
// scatters between the cells. That mixing is linear in LIGHT, so:
//
//   1. Undo the divider to get each cell's resistance (the front end is known
//      exactly: 120k into a virtual earth, 1k in series with a ~6V supply).
//   2. Undo the power law to get light. The exponent is unknown per cell, so
//      solve it from the captures: on a screen white is red plus green plus
//      blue, so once a cell is correctly linearised its response to white
//      must equal the sum of its responses to the primaries.
//   3. The three primary captures ARE the mixing matrix. Invert it.
//
// A caution for whoever reads step 2 next: the exponent it recovers is not
// trustworthy in absolute terms — a black CARD (rather than a black screen)
// biases all three by about +20%, and a panel that dims full white biases
// them the other way. It is kept because the RATIOS between the three cells
// survive both, and those ratios are what stop a grey card reading tinted.

#include "sensors.h"

namespace lp {

namespace {

// The Computer's own front end, from the Rev 1 schematic. Each input is a
// 120k resistor into an op-amp's virtual earth (R17/R25/R27/R42), and CV Out 1
// reaches the jack through 1k (R55). So the wand's LDR and that 120k divide
// the supply, and a reading inverts to a resistance.
constexpr int32_t kInputOhms   = 120000;
constexpr int32_t kSeriesOhms  = 1000;
constexpr int32_t kSupplyUnits = 2048;      // CV Out 1 at full scale, in ADC units

constexpr int64_t kQ24One = 1 << 24;

// Gamma bracket. CdS is published at 0.5-0.9; the extra room absorbs the
// common-mode bias described at the top of this file.
constexpr int32_t kGammaLo      = 26214;    // 0.40
constexpr int32_t kGammaHi      = 78643;    // 1.20
constexpr int32_t kGammaDefault = 45875;    // 0.70

// How much noise, drift and screen-brightness error the un-mix may amplify,
// as the sum of |A| across a row. Also exactly what keeps the per-sample
// accumulator inside int32 — see kMatrixRowBudget in the header.
constexpr int64_t kMaxRowL1 = 6 * kQ24One;

// Below this the gels cannot tell the colours apart at all, and inverting
// would only amplify noise. Keep the two-point calibration instead.
constexpr int32_t kMinSeparation = 1966;    // 0.03 in Q16

/// The LDR resistance a reading implies, in ohms.
int32_t LdrOhms(int32_t n)
{
	if (n < 1) n = 1;                        // darker than the model can express
	if (n >= kSupplyUnits) return 1;         // brighter than it can express
	int32_t r = (kInputOhms * (kSupplyUnits - n)) / n - kSeriesOhms;
	return r < 1 ? 1 : r;
}

int32_t LogOhms(int32_t n)
{
	return log2_q16(static_cast<uint32_t>(LdrOhms(n)));
}

/// Light at a reading, normalised so the white capture is exactly 65536.
/// Normalising here rather than later is essential: R^(-1/gamma) for a real
/// resistance underflows Q16 to nothing.
int32_t LightAt(int32_t logR, int32_t logRWhite, int32_t tQ16)
{
	int64_t e = -static_cast<int64_t>(tQ16) * (logR - logRWhite);
	return exp2_q16(clamp_i32(static_cast<int32_t>(e >> 16), -32 * 65536, 8 * 65536));
}

/// white - (red + green + blue) + 2*black, in light. Zero at the true
/// exponent, positive below it and negative above it, whatever the ambient
/// level — the ambient terms cancel.
int32_t Additivity(int32_t gammaQ16, const int32_t logR[5])
{
	int32_t t = static_cast<int32_t>((static_cast<int64_t>(kQ16One) << 16) / gammaQ16);
	int32_t sum = 0;
	for (int j = 1; j <= 3; j++) sum += LightAt(logR[j], logR[0], t);
	return kQ16One - sum + 2 * LightAt(logR[4], logR[0], t);
}

/// logR: white, red, green, blue, black.
int32_t SolveGamma(const int32_t logR[5], bool &defaulted)
{
	if (!(Additivity(kGammaLo, logR) > 0 && Additivity(kGammaHi, logR) < 0))
	{
		// No sign change: a grey card used as black, a channel at a rail, or a
		// display whose white is nothing like the sum of its primaries.
		defaulted = true;
		return kGammaDefault;
	}
	int32_t lo = kGammaLo, hi = kGammaHi;
	for (int i = 0; i < 12; i++)     // to 0.0002 in gamma; no tolerance needed
	{
		int32_t mid = (lo + hi) >> 1;
		if (Additivity(mid, logR) > 0) lo = mid; else hi = mid;
	}
	return (lo + hi) >> 1;
}

int64_t Abs64(int64_t v) { return v < 0 ? -v : v; }

/// 3x3 inverse in Q24. False if the matrix is too close to singular to
/// invert honestly — with the shrinkage below it never is.
bool Invert24(const int64_t m[3][3], int64_t out[3][3])
{
	int64_t c[3][3];
	c[0][0] =  (m[1][1] * m[2][2] - m[1][2] * m[2][1]) >> 24;
	c[0][1] = -((m[1][0] * m[2][2] - m[1][2] * m[2][0]) >> 24);
	c[0][2] =  (m[1][0] * m[2][1] - m[1][1] * m[2][0]) >> 24;
	c[1][0] = -((m[0][1] * m[2][2] - m[0][2] * m[2][1]) >> 24);
	c[1][1] =  (m[0][0] * m[2][2] - m[0][2] * m[2][0]) >> 24;
	c[1][2] = -((m[0][0] * m[2][1] - m[0][1] * m[2][0]) >> 24);
	c[2][0] =  (m[0][1] * m[1][2] - m[0][2] * m[1][1]) >> 24;
	c[2][1] = -((m[0][0] * m[1][2] - m[0][2] * m[1][0]) >> 24);
	c[2][2] =  (m[0][0] * m[1][1] - m[0][1] * m[1][0]) >> 24;

	int64_t det = (m[0][0] * c[0][0] + m[0][1] * c[0][1] + m[0][2] * c[0][2]) >> 24;
	if (det < (kQ24One >> 8)) return false;

	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++)
			out[i][j] = (c[j][i] << 24) / det;     // adjugate, transposed
	return true;
}

/// Scale each row so the white capture — which is (1,1,1) in light, by
/// construction — comes back out as (1,1,1). This is what absorbs a display
/// whose white is not quite its primaries summed. Returns the largest row sum
/// of |A|, which is the amplification the un-mix will apply to noise.
int64_t RowScaleAndMaxL1(int64_t a[3][3], bool &clamped)
{
	int64_t worst = 0;
	for (int i = 0; i < 3; i++)
	{
		int64_t rowSum = a[i][0] + a[i][1] + a[i][2];
		int64_t scale = kQ24One;
		if (rowSum > 0) scale = (kQ24One << 24) / rowSum;
		if (scale < kQ24One / 2) { scale = kQ24One / 2; clamped = true; }
		if (scale > 2 * kQ24One) { scale = 2 * kQ24One; clamped = true; }

		int64_t l1 = 0;
		for (int j = 0; j < 3; j++)
		{
			a[i][j] = (a[i][j] * scale) >> 24;
			l1 += Abs64(a[i][j]);
		}
		if (l1 > worst) worst = l1;
	}
	return worst;
}

/// M shrunk toward its own diagonal: (1-b)M + b*diag(rowsums). Both have the
/// same row sums, so neutral stays neutral at every b — and b = 1 gives plain
/// per-channel normalisation, which is exactly the two-point card. So the
/// damping is a continuous dial from "full un-mix" to "no un-mix".
bool ShrinkInvert(const int64_t m[3][3], const int64_t sigma[3], int64_t betaQ24,
                  int64_t out[3][3], int64_t &maxL1, bool &clamped)
{
	int64_t reg[3][3];
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++)
		{
			reg[i][j] = ((kQ24One - betaQ24) * m[i][j]) >> 24;
			if (i == j) reg[i][j] += (betaQ24 * sigma[i]) >> 24;
		}
	if (!Invert24(reg, out)) return false;
	maxL1 = RowScaleAndMaxL1(out, clamped);
	return true;
}

/// |det| of M with its columns normalised: 1 means the three cells see three
/// independent things, 0 means they all see the same thing. Q16.
int32_t Separation(const int64_t m[3][3])
{
	int64_t norm[3];
	for (int j = 0; j < 3; j++)
	{
		int64_t ssq = 0;
		for (int i = 0; i < 3; i++)
		{
			int64_t v = m[i][j] >> 8;                    // Q16
			ssq += (v * v) >> 16;
		}
		norm[j] = fast_sqrt_q16(static_cast<int32_t>(clamp_i32(
			static_cast<int32_t>(ssq > INT32_MAX ? INT32_MAX : ssq), 0, INT32_MAX)));
		if (norm[j] < 1) norm[j] = 1;
	}

	int64_t c0 = (m[1][1] * m[2][2] - m[1][2] * m[2][1]) >> 24;
	int64_t c1 = (m[1][0] * m[2][2] - m[1][2] * m[2][0]) >> 24;
	int64_t c2 = (m[1][0] * m[2][1] - m[1][1] * m[2][0]) >> 24;
	int64_t det = (m[0][0] * c0 - m[0][1] * c1 + m[0][2] * c2) >> 24;   // Q24
	int64_t den = (((norm[0] * norm[1]) >> 16) * norm[2]) >> 16;        // Q16
	if (den < 1) den = 1;
	int64_t sep = (Abs64(det >> 8) << 16) / den;                        // Q16
	return static_cast<int32_t>(sep > 65535 ? 65535 : sep);
}

} // namespace

void SensorPipeline::Normalise(int ch, int32_t nBlack, int32_t nWhite)
{
	// The table is sampled every 8 counts, so a reference that falls between
	// two nodes would read a fraction of a percent off its end. Rescale so
	// black is exactly 0 and white exactly full: modes compare against both
	// ends, and "black" wants to mean zero. The table is allowed to run
	// NEGATIVE below black — clamping there would pin the nodes under the
	// reference and break this rescale, and Update() clamps at the point of use.
	int32_t uB = CurveAt(nBlack, ch);
	int32_t uW = CurveAt(nWhite, ch);
	if (uW - uB < 4096) return;
	for (int i = 0; i < kLutSize; i++)
	{
		int64_t v = (static_cast<int64_t>(lut_[ch][i] - uB) * 65535) / (uW - uB);
		lut_[ch][i] = clamp_i32(static_cast<int32_t>(v), kLightFloor, kLightCeiling);
	}
}

void SensorPipeline::BuildTwoPoint(const CalibData &d)
{
	for (int ch = 0; ch < 3; ch++)
	{
		const int32_t nW = d.white[ch];
		const int32_t nB = d.black[ch];

		// The resistance model only holds for the wand wired the documented
		// way round (LDR from CV Out 1 to the input, so more light reads
		// higher) and for readings inside the divider's range. Anything else
		// — an inverted build, a reading pinned at either rail — falls back to
		// the plain straight line between the two endpoints, which still
		// calibrates, just without the curve.
		bool model = nW > nB && nB > 0 && nW < kSupplyUnits;
		int32_t logW = 0, logB = 0, span = 0;
		if (model)
		{
			logW = LogOhms(nW);
			logB = LogOhms(nB);
			span = logB - logW;
			if (span < 4096) model = false;   // a sixteenth of an octave is noise
		}

		for (int i = 0; i < kLutSize; i++)
		{
			const int32_t n = (i << 3) - 2048;
			int32_t u;
			if (model)
			{
				int32_t l = LogOhms(n);
				u = static_cast<int32_t>((static_cast<int64_t>(logB - l) * 65535) / span);
			}
			else
			{
				u = static_cast<int32_t>(
					(static_cast<int64_t>(n - nB) * 65535) / (nW - nB));
			}
			lut_[ch][i] = clamp_i32(u, kLightFloor, kLightCeiling);
		}
		Normalise(ch, nB, nW);
	}
}

void SensorPipeline::BuildGainTable(int32_t l0Q16)
{
	// curve(x) = log2(1 + x/L0) / log2(1 + 1/L0), stored as the GAIN
	// curve(v)/v so the audio path needs no divide. With L0 taken from the
	// black capture this is algebraically the same curve the two-point
	// calibration uses, so both feel alike.
	const int32_t one = 16 << 16;   // log2_q16(65536)
	int32_t den = log2_q16(static_cast<uint32_t>(
		65536 + static_cast<int32_t>((static_cast<int64_t>(65536) << 16) / l0Q16))) - one;
	if (den < 1) den = 1;

	for (int k = 1; k < kGainSize; k++)
	{
		const int32_t v = k << 9;
		int32_t arg = 65536 + static_cast<int32_t>((static_cast<int64_t>(v) << 16) / l0Q16);
		int32_t num = log2_q16(static_cast<uint32_t>(arg)) - one;
		int32_t curve = static_cast<int32_t>((static_cast<int64_t>(num) << 16) / den);
		gain_[k] = static_cast<int32_t>((static_cast<int64_t>(curve) << 12) / v);
	}
	gain_[0] = gain_[1];
}

bool SensorPipeline::BuildFivePoint(const CalibData &d)
{
	// Every capture must be in range, and each primary must sit between this
	// channel's own black and white. Otherwise the geometry is wrong and the
	// light model cannot be trusted.
	for (int ch = 0; ch < 3; ch++)
	{
		if (!(d.white[ch] > d.black[ch] && d.black[ch] > 0 && d.white[ch] < kSupplyUnits))
			return false;
		for (int j = 0; j < 3; j++)
		{
			int32_t p = d.prim[j][ch];
			if (p < d.black[ch] - 32 || p > d.white[ch] + 32) return false;
			if (p < kCaptureMin || p > kCaptureMax) return false;
		}
	}

	bool defaulted = false;
	int32_t tQ16[3];
	int32_t blackLight[3];
	for (int ch = 0; ch < 3; ch++)
	{
		const int32_t logR[5] = {
			LogOhms(d.white[ch]), LogOhms(d.prim[0][ch]), LogOhms(d.prim[1][ch]),
			LogOhms(d.prim[2][ch]), LogOhms(d.black[ch]),
		};
		int32_t gamma = SolveGamma(logR, defaulted);
		tQ16[ch] = static_cast<int32_t>((static_cast<int64_t>(kQ16One) << 16) / gamma);
		blackLight[ch] = LightAt(logR[4], logR[0], tQ16[ch]);
	}
	// One cell's exponent far from the others means that cell's solve went
	// wrong, not that the cells differ that much. Use the default for all.
	{
		int32_t lo = tQ16[0], hi = tQ16[0];
		for (int ch = 1; ch < 3; ch++)
		{
			if (tQ16[ch] < lo) lo = tQ16[ch];
			if (tQ16[ch] > hi) hi = tQ16[ch];
		}
		if (hi > 2 * lo)
		{
			defaulted = true;
			for (int ch = 0; ch < 3; ch++)
			{
				tQ16[ch] = static_cast<int32_t>(
					(static_cast<int64_t>(kQ16One) << 16) / kGammaDefault);
				blackLight[ch] = LightAt(LogOhms(d.black[ch]), LogOhms(d.white[ch]), tQ16[ch]);
			}
		}
	}
	if (defaulted) quality_ |= kQualGammaDefault;

	// Linear-light tables, dark-subtracted and white-normalised.
	for (int ch = 0; ch < 3; ch++)
	{
		const int32_t logW = LogOhms(d.white[ch]);
		const int32_t b = blackLight[ch];
		const int32_t den = 65536 - b;
		if (den < 4096) return false;          // black and white too close in light
		for (int i = 0; i < kLutSize; i++)
		{
			const int32_t n = (i << 3) - 2048;
			int32_t u = LightAt(LogOhms(n), logW, tQ16[ch]);
			int64_t v = (static_cast<int64_t>(u - b) << 16) / den;
			lut_[ch][i] = clamp_i32(static_cast<int32_t>(v), kLightFloor, kLightCeiling);
		}
		Normalise(ch, d.black[ch], d.white[ch]);
	}

	// The mixing matrix, read straight off the tables: column j is what the
	// three cells saw under primary j.
	int64_t m[3][3];
	int64_t sigma[3];
	for (int i = 0; i < 3; i++)
	{
		sigma[i] = 0;
		for (int j = 0; j < 3; j++)
		{
			int32_t light = clamp_i32(CurveAt(d.prim[j][i], i), 0, kLightCeiling);
			m[i][j] = static_cast<int64_t>(light) << 8;    // Q16 -> Q24
			sigma[i] += m[i][j];
		}
		if (sigma[i] < (kQ24One >> 4)) return false;       // this cell saw nothing
	}

	const int32_t sep = Separation(m);
	if (sep < kMinSeparation)
	{
		quality_ |= kQualLowSep;
		return false;
	}

	// Damp until the amplification is inside budget. maxL1 falls as beta
	// rises, and beta = 1 always lands at 1.0, so this always terminates.
	int64_t lo = kQ24One / 100, hi = kQ24One;
	int64_t best[3][3];
	int64_t maxL1 = 0;
	bool clamped = false;
	{
		bool c = false;
		if (!ShrinkInvert(m, sigma, hi, best, maxL1, c)) return false;
		clamped = c;
	}
	for (int it = 0; it < 14; it++)
	{
		int64_t mid = (lo + hi) / 2;
		int64_t cand[3][3];
		int64_t l1 = 0;
		bool c = false;
		if (ShrinkInvert(m, sigma, mid, cand, l1, c) && l1 <= kMaxRowL1)
		{
			hi = mid;
			clamped = c;
			for (int i = 0; i < 3; i++)
				for (int j = 0; j < 3; j++) best[i][j] = cand[i][j];
		}
		else
		{
			lo = mid;
		}
	}
	if (clamped) quality_ |= kQualRowClamped;

	for (int i = 0; i < 3; i++)
	{
		int32_t row[3];
		int32_t l1 = 0;
		for (int j = 0; j < 3; j++)
		{
			row[j] = clamp_i32(static_cast<int32_t>(best[i][j] >> 14),
			                   -kMatrixRowBudget, kMatrixRowBudget);
			l1 += row[j] < 0 ? -row[j] : row[j];
		}
		// Rounding can nudge a row past its budget; scale it back so the
		// per-sample accumulator's bound holds exactly.
		for (int j = 0; j < 3; j++)
			a10_[i][j] = (l1 > kMatrixRowBudget)
				? static_cast<int32_t>((static_cast<int64_t>(row[j]) * kMatrixRowBudget) / l1)
				: row[j];
	}

	int32_t bAvg = (blackLight[0] + blackLight[1] + blackLight[2]) / 3;
	int32_t l0 = static_cast<int32_t>((static_cast<int64_t>(bAvg) << 16) / (65536 - bAvg));
	BuildGainTable(clamp_i32(l0, 1024, 16384));   // 1/64 .. 1/4

	sepBars_ = sep >= 19661 ? 5 : sep >= 9830 ? 4 : sep >= 5243 ? 3 : sep >= 1966 ? 2 : 1;
	return true;
}

void SensorPipeline::SetCalibration(const CalibData &d)
{
	quality_ = 0;
	sepBars_ = 0;
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++) a10_[i][j] = (i == j) ? 1024 : 0;
	for (int k = 0; k < kGainSize; k++) gain_[k] = 4096;

	if (d.mode == static_cast<uint8_t>(CalibMode::FivePoint) && BuildFivePoint(d)) return;

	// Either a two-point calibration, or a five-point one the maths could not
	// use. Both leave identity/unity above, so this path is exactly the card's
	// original behaviour.
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++) a10_[i][j] = (i == j) ? 1024 : 0;
	for (int k = 0; k < kGainSize; k++) gain_[k] = 4096;
	sepBars_ = 0;
	BuildTwoPoint(d);
}

} // namespace lp
