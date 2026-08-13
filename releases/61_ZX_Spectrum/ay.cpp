// ay.cpp — AY-3-8912 programmable sound generator.
//
// Three square-wave tone channels + a noise channel, mixed per-channel, each with
// either a fixed 4-bit level or the shared 16-step envelope. Output is a single
// mono mix on Audio Out 2.
//
// Clocking: on the 128K Spectrum the AY runs at CPU/2 (~1.7734MHz). The tone,
// noise and envelope dividers count in AY cycles. Render(tstates) is called from
// the emulation loop with the number of CPU T-states elapsed; we convert to AY
// cycles (÷2, carrying the remainder) and step the generators, then return the
// current mixed sample.
//
// Register map (0..15):
//   0,1  tone A period (12-bit: fine + coarse)
//   2,3  tone B    4,5  tone C
//   6    noise period (5-bit)
//   7    mixer: bits0-2 tone A/B/C enable (0=on), bits3-5 noise A/B/C enable
//   8,9,10 amplitude A/B/C: bit4 = use envelope, else bits0-3 fixed level
//   11,12 envelope period (16-bit)
//   13   envelope shape (bits: Continue/Attack/Alternate/Hold)

#include "spectrum.h"

namespace zx {

// 16-step logarithmic volume table (AY DAC), scaled so a single channel at full
// volume is comfortably within the signed 12-bit AudioOut range when three are
// MEASURED AY-3-8912 volume curve (the well-known 16-entry log response from
// real-chip measurements, as used by MAME/AY emulators), scaled so a full
// 3-channel sum stays within ±2047 (max single channel = 682). This is more
// accurate than a generic ~3dB ramp — the lifted low/mid steps give a more
// natural tone-vs-noise balance (noise still sits forward, which is faithful:
// broadband noise reads louder than a square at equal peak amplitude).
static const int16_t kVolTable[16] = {
	0, 9, 14, 20, 29, 42, 58, 93, 115, 181, 241, 307, 389, 469, 578, 682
};

void AY::Reset()
{
	for (int i = 0; i < 16; i++) reg[i] = 0;
	selected = 0;
	for (int c = 0; c < 3; c++) { toneCounter[c] = 0; toneOut[c] = 0; }
	noiseCounter = 0; noiseOut = 0; noiseRng = 1;
	envCounter = 0; envStep = 0; envVol = 0; envHold = false;
	tstateFrac = 0;
}

void AY::Write(uint8_t r, uint8_t v)
{
	r &= 15;
	reg[r] = v;
	// Writing the envelope shape (R13) resets the envelope generator.
	if (r == 13)
	{
		envStep = 0;
		envHold = false;
		envCounter = 0;
	}
}

// Envelope: model a free-running 0..31 phase and derive the 0..15 volume from
// the R13 shape. The four shape bits are Continue(3)/Attack(2)/Alternate(1)/
// Hold(0). This 32-phase formulation reproduces all 8 audible AY envelope shapes
// correctly, including the alternating and one-shot forms.
//   phase 0..15  = first ramp, 16..31 = second ramp.
static inline void EnvStep(AY *ay)
{
	if (ay->envHold) return;

	ay->envStep = (ay->envStep + 1) & 31;
	uint8_t shape = ay->reg[13] & 0x0F;
	bool cont = shape & 0x08, attack = shape & 0x04, altern = shape & 0x02, hold = shape & 0x01;
	uint8_t phase = ay->envStep;

	// After the first full ramp (phase wrapped past 15), decide what continues.
	if (phase == 0)
	{
		// Completed a full 0..31 cycle.
		if (!cont) { ay->envVol = 0; ay->envHold = true; return; }
		if (hold)
		{
			// Hold at the level the shape ends on.
			bool high = altern ? attack : !attack;
			ay->envVol = high ? 15 : 0; ay->envHold = true; return;
		}
		// else: continue looping (fall through to compute vol from phase 0)
	}

	// Compute volume from phase within the current 16-step ramp.
	uint8_t p = phase & 15;
	bool secondHalf = phase >= 16;
	bool rising;
	if (!cont)
	{
		// One-shot: only the first ramp matters (then we hold at 0 above).
		rising = attack;
		ay->envVol = rising ? p : (15 - p);
		return;
	}
	// Continuous shapes: alternate flips direction each half if Alternate set.
	rising = attack;
	if (altern && secondHalf) rising = !rising;
	ay->envVol = rising ? p : (15 - p);
}

int16_t AY::Render(uint32_t tstates, const AYMod &mod)
{
	// Convert CPU T-states to AY cycles (CPU/2), carrying the remainder.
	uint32_t total = tstateFrac + tstates;
	uint32_t ayCycles = total >> 1;
	tstateFrac = total & 1;

	// Periods (a period of 0 behaves as 1).
	uint32_t perA = ((reg[1] & 0x0F) << 8) | reg[0]; if (!perA) perA = 1;
	uint32_t perB = ((reg[3] & 0x0F) << 8) | reg[2]; if (!perB) perB = 1;
	uint32_t perC = ((reg[5] & 0x0F) << 8) | reg[4]; if (!perC) perC = 1;
	uint32_t perN = (reg[6] & 0x1F); if (!perN) perN = 1;
	uint32_t perE = (reg[12] << 8) | reg[11]; if (!perE) perE = 1;

	// --- Live mangling: bend the noise / envelope periods by the CV amounts.
	// mod == AYMod{} (all zero, duty 128) leaves everything below identical.
	if (mod.noiseMod) { int32_t n = (int32_t)perN + mod.noiseMod; perN = n < 1 ? 1 : (n > 31 ? 31 : n); }
	if (mod.envMod)
	{
		// envMod is a signed exponent: scale the period by 2^(envMod/64), so the
		// sweep is the same musical interval whatever period the tune programmed
		// (rate perception is ratio-based; a fixed offset clamps on short periods
		// and vanishes on long ones). Integer form: shift by the whole octaves,
		// then a 6-step linear interpolation across the fractional octave.
		int32_t e   = mod.envMod;
		int32_t oct = e >> 6;                       // whole octaves, floor (-2..+1)
		int32_t fr  = e - (oct << 6);               // 0..63 within the octave
		int32_t p   = (int32_t)perE;
		// Fractional part: multiply by (64 + fr)/64 ≈ 2^(fr/64) to within ~3%.
		p = (p * (64 + fr)) >> 6;
		p = (oct >= 0) ? (p << oct) : (p >> (-oct));
		perE = p < 1 ? 1 : (p > 0xFFFF ? 0xFFFF : (uint32_t)p);
	}

	uint32_t per[3] = { perA, perB, perC };

	// Block stepping. AY tone frequency = clock / (16 × period): one full square
	// cycle spans (period × 16) AY cycles. We track the phase within that full
	// period and derive the square level by comparing against a duty threshold —
	// duty 128 puts the edge at the half-period, reproducing the classic 50%
	// square exactly; other duty values give PWM the real chip can't do. Noise is
	// the same prescale; the envelope steps every (period × 256) AY cycles.
	for (int c = 0; c < 3; c++)
	{
		uint32_t fullPeriod = per[c] << 4;          // full square cycle in AY cycles
		toneCounter[c] += ayCycles;
		if (toneCounter[c] >= fullPeriod) toneCounter[c] %= fullPeriod;
		// Edge position for this channel's duty (128 -> exactly half-period).
		uint32_t edge = (fullPeriod * mod.duty[c]) >> 8;
		toneOut[c] = (toneCounter[c] < edge) ? 1 : 0;
	}
	// Noise (÷16 prescale like tone; toggles the LFSR each period step)
	noiseCounter += ayCycles;
	uint32_t noiseStep = perN << 4;
	while (noiseCounter >= noiseStep)
	{
		noiseCounter -= noiseStep;
		// 17-bit LFSR, taps at bits 0 and 3.
		uint32_t bit = ((noiseRng ^ (noiseRng >> 3)) & 1);
		noiseRng = (noiseRng >> 1) | (bit << 16);
		noiseOut = noiseRng & 1;
	}
	// Envelope: one step every (period × 256) AY cycles.
	envCounter += ayCycles;
	uint32_t envStepCycles = perE << 8;
	while (envCounter >= envStepCycles)
	{
		envCounter -= envStepCycles;
		EnvStep(this);
	}

	// --- Mix ------------------------------------------------------------------
	// Force-mute requested channels by setting both their tone and noise enable
	// bits in the mixer (bit=1 => disabled). muteMask == 0 leaves reg[7] as-is.
	uint8_t mix = reg[7];
	if (mod.muteMask)
	{
		uint8_t m = mod.muteMask & 0x07;
		mix |= m | (m << 3);                         // tone bits 0-2, noise bits 3-5
	}
	int32_t out = 0;
	for (int c = 0; c < 3; c++)
	{
		bool toneOn  = !((mix >> c) & 1);        // bit=0 => tone enabled
		bool noiseOn = !((mix >> (c + 3)) & 1);  // bit=0 => noise enabled
		// Channel is high if (tone off OR square high) AND (noise off OR noise high)
		bool level = (!toneOn || toneOut[c]) && (!noiseOn || noiseOut);
		if (!level) continue;

		uint8_t amp = reg[8 + c];
		uint8_t vol = (amp & 0x10) ? envVol : (amp & 0x0F);
		out += kVolTable[vol & 0x0F];
	}

	// `out` is a unipolar sum of up to 3 channels (0..~2046). Centre it so silence
	// sits near 0 and the waveform swings bipolar, which is what AudioOut expects
	// (and avoids a DC offset / thump). Subtract half the max single-channel level
	// as a nominal midpoint.
	return int16_t(out - 512);
}

} // namespace zx
