// Origami — dual oversampled wavefolder
//
// A West-Coast timbre tool: each channel drives its audio input into a folding
// nonlinearity, generating shimmering, metallic harmonics that bloom as you push
// the drive. Two independent folders, one per column of jacks.
//
//   Quiet input          -> nearly clean (folder only bites once you push past
//                           the rails with drive or CV)
//   Loud input / drive    -> the waveform reflects back on itself again and again,
//                           each fold adding a new band of harmonics
//   Bias (Main knob)      -> offsets the signal into the folder asymmetrically,
//                           bringing in *even* harmonics for a hollow/reedy colour
//
// The fold is computed at 4x oversampling and band-limited back down, so the
// (very harmonically rich) output aliases far less than a naive folder would.
//
// I/O (left column of jacks = channel 1, right column = channel 2):
//   Audio In 1/2     channel input (the signal to fold)
//   Audio Out 1/2    folded output
//   CV In 1/2        adds to that channel's fold drive (CV over fold amount;
//                    bipolar — negative CV backs the folding off)
//   CV Out 1/2       envelope follower of the folded output (0..+6V), handy for
//                    self-patching a VCA/gate elsewhere
//   Knob X / Knob Y  fold drive (depth) for channel 1 / channel 2
//   Main knob        bias / symmetry for both channels (centre = symmetric)
//   Switch           fold character: Down = triangle, Middle = sine, Up = hard clip
//   LEDs             each column glows with that channel's output level

#include "ComputerCard.h"
#include <cmath>

class Origami : public ComputerCard
{
	// --- fixed-point scale: R represents 1.0 (full-scale, matches the +/-2048 DAC)
	static constexpr int32_t R = 2048;
	static constexpr int32_t FOLD_PERIOD = 4 * R; // period of the triangle/sine fold

	// --- oversampling ---
	// The fold runs OS times faster than the audio rate; a short FIR then
	// band-limits and decimates back down. FIRLEN is kept small (and the MACs
	// are int32, single-cycle on the M0+) so the whole thing fits the ~4000-cycle
	// per-sample budget on hardware — a longer/int64 FIR overruns and outputs static.
	static constexpr int OS = 4;          // oversample factor (power of two)
	static constexpr int OS_SHIFT = 2;    // log2(OS)
	static constexpr int FIRLEN = 16;     // decimation FIR length (power of two)
	static constexpr int FIRMASK = FIRLEN - 1;

	// --- drive: Q8 fixed point (256 == 1.0x) ---
	static constexpr int32_t DRIVE_SHIFT = 8;
	static constexpr int32_t DRIVE_MIN = 1 << 4;   // 0.06x (lets CV fold all the way down)
	static constexpr int32_t DRIVE_MAX = 1 << 14;  // 64x

	// --- sine fold lookup ---
	static constexpr int SINE_BITS = 10;
	static constexpr int SINE_SIZE = 1 << SINE_BITS; // one full period
	static constexpr int SINE_MASK = SINE_SIZE - 1;

	// --- control-rate decimation (knobs refreshed here) ---
	static constexpr int CTRL_DECIM = 16;

	int16_t sineTab[SINE_SIZE]; // R*sin(2*pi*i/SINE_SIZE)
	int32_t firTab[FIRLEN];     // Q15 windowed-sinc low-pass (cutoff = fs/2 of base rate)

	// Per-channel state
	int32_t prevIn[2];          // previous base-rate input (for linear upsampling)
	int32_t ring[2][FIRLEN];    // most-recent oversampled folded samples
	int ringPos[2];
	int32_t env[2];             // output envelope follower (Q for CV out / LEDs)

	// Control values (refreshed at control rate)
	int32_t driveQ8[2];
	int32_t bias;
	int ctrl;

	// Reflect/wrap a value into the fold and return a folded sample in [-R, R].
	int32_t __not_in_flash_func(foldTriangle)(int32_t s)
	{
		int32_t m = s & (FOLD_PERIOD - 1); // s mod 4R, always 0..4R-1
		if (m < R)        return m;            // 0   .. R   (rising)
		if (m < 3 * R)    return 2 * R - m;    // R   .. -R  (falling through zero)
		return m - FOLD_PERIOD;                // -R  .. 0   (rising back)
	}

	int32_t __not_in_flash_func(foldSine)(int32_t s)
	{
		// One LUT period spans FOLD_PERIOD (== 4R == 8192); index = s/8.
		return sineTab[(s >> 3) & SINE_MASK];
	}

	static int32_t __not_in_flash_func(foldClip)(int32_t s)
	{
		if (s > R)  return R;
		if (s < -R) return -R;
		return s;
	}

public:
	Origami()
	{
		const float PI = (float)M_PI;

		// Sine fold table: one full period of a sine, peak +/-R.
		for (int i = 0; i < SINE_SIZE; i++)
			sineTab[i] = (int16_t)lroundf(R * sinf(2.0f * PI * i / SINE_SIZE));

		// Decimation low-pass: windowed-sinc (Hamming), cutoff at the base-rate
		// Nyquist so fold harmonics above fs/2 are removed before downsampling.
		const float fc = 0.5f / OS;        // cycles/sample at the oversampled rate
		const float mid = (FIRLEN - 1) / 2.0f;
		float sum = 0.0f;
		float h[FIRLEN];
		for (int n = 0; n < FIRLEN; n++)
		{
			float x = n - mid;
			float sinc = (x == 0.0f) ? (2.0f * fc)
			                         : sinf(2.0f * PI * fc * x) / (PI * x);
			float w = 0.54f - 0.46f * cosf(2.0f * PI * n / (FIRLEN - 1)); // Hamming
			h[n] = sinc * w;
			sum += h[n];
		}
		// Normalise to unity DC gain, then convert to Q15. The filter sees one
		// output sample per OS inputs, so it must also pass through that 1/OS of
		// the energy at unity — normalising to sum==1 across all taps does exactly
		// that (the OS-1 inserted samples between inputs are folded too, not zero).
		for (int n = 0; n < FIRLEN; n++)
			firTab[n] = (int32_t)lroundf((h[n] / sum) * 32768.0f);

		for (int i = 0; i < 2; i++)
		{
			prevIn[i] = 0;
			ringPos[i] = 0;
			env[i] = 0;
			driveQ8[i] = 1 << DRIVE_SHIFT;
			for (int k = 0; k < FIRLEN; k++) ring[i][k] = 0;
		}
		bias = 0;
		ctrl = 0;
	}

	virtual void __not_in_flash_func(ProcessSample)()
	{
		Switch mode = SwitchVal();

		// ---- control rate: refresh knob-derived parameters ----
		if (--ctrl <= 0)
		{
			ctrl = CTRL_DECIM;
			// Drive: knob maps ~1x..64x via a square law, so the useful folding
			// range isn't all crammed into the top of the knob. At k=4095 the
			// square law reaches ~64x; the DRIVE_MAX clamp catches only the very top.
			for (int i = 0; i < 2; i++)
			{
				int32_t k = KnobVal(i ? Y : X);             // 0..4095
				int32_t d = (1 << DRIVE_SHIFT) + ((k * k) >> 10); // 256(1x) .. ~16640(65x)
				driveQ8[i] = d;
			}
			// Bias: Main knob, bipolar around centre, +/- ~1.0 full-scale.
			bias = KnobVal(Main) - 2048;                    // -2048..+2047
		}

		for (int i = 0; i < 2; i++)
		{
			int32_t in = Connected(i ? Audio2 : Audio1) ? AudioIn(i) : 0;

			// Fold drive = knob drive + CV (bipolar). CV In is +/-2048 (~+/-6V);
			// scale so a few volts meaningfully opens or closes the folding.
			int32_t drive = driveQ8[i] + (CVIn(i) << 2);
			if (drive < DRIVE_MIN) drive = DRIVE_MIN;
			if (drive > DRIVE_MAX) drive = DRIVE_MAX;

			// ---- 4x oversample: linearly interpolate prevIn->in, fold each
			//      subsample, push into the decimation ring ----
			int32_t prev = prevIn[i];
			int32_t step = in - prev;
			for (int j = 1; j <= OS; j++)
			{
				int32_t sub = prev + ((step * j) >> OS_SHIFT);  // linear upsample
				int32_t s = ((sub * drive) >> DRIVE_SHIFT) + bias;

				int32_t folded = (mode == Down) ? foldTriangle(s)
				               : (mode == Middle) ? foldSine(s)
				                                  : foldClip(s);

				ring[i][ringPos[i]] = folded;
				ringPos[i] = (ringPos[i] + 1) & FIRMASK;
			}
			prevIn[i] = in;

			// ---- decimate: FIR low-pass over the oversampled folded samples ----
			// int32 is safe: |acc| <= 2048 * sum|taps| ~= 80M << 2^31, and each
			// product fits int32 so it's a single-cycle MULS on the M0+.
			int32_t acc = 0;
			int idx = ringPos[i];
			for (int k = 0; k < FIRLEN; k++)
			{
				idx = (idx - 1) & FIRMASK; // walk back from most-recent sample
				acc += firTab[k] * ring[i][idx];
			}
			int32_t out = acc >> 15;

			if (out > 2047) out = 2047;
			if (out < -2048) out = -2048;
			AudioOut(i, out);

			// ---- envelope follower of the output: fast attack, slow release ----
			int32_t a = out < 0 ? -out : out;
			if (a > env[i]) env[i] += (a - env[i]) >> 2;   // ~fast attack
			else            env[i] += (a - env[i]) >> 9;   // slow release

			int32_t cvOut = env[i];                         // 0..2047 (~0..+6V)
			if (cvOut > 2047) cvOut = 2047;
			CVOut(i, cvOut);

			int led = env[i] << 1;                          // 0..4095
			if (led > 4095) led = 4095;
			LedBrightness(0 + i, led);
			LedBrightness(2 + i, led);
			LedBrightness(4 + i, led);
		}
	}
};

// The hardware entry point. Skipped by the host simulator (sim/), which
// supplies its own main() and a mock ComputerCard; has no effect on the
// real Pico build, where COMPUTERCARD_HOST_SIM is never defined.
#ifndef COMPUTERCARD_HOST_SIM
int main()
{
	set_sys_clock_khz(192000, true);

	Origami origami;
	origami.EnableNormalisationProbe();
	origami.Run();
}
#endif
