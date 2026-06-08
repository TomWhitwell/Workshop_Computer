// Vactrol — dual vactrol-emulating low-pass gate
//
// A West-Coast staple: each channel is a combined VCA + low-pass filter,
// controlled together by a model of a vactrol (LED + photoresistor) with the
// characteristic fast attack / slow, exponential "plong" decay.
//
//   Ping a channel  -> percussive plucked / bonk envelope
//   Feed it CV      -> continuous combined VCA/VCF control
//   No audio input  -> self-pings: a short noise burst is fed in, so a
//                      resonant channel becomes a plucky drum/marimba voice
//                      driven by clock alone.
//
// I/O (left column of jacks = channel 1, right column = channel 2):
//   Audio In 1/2     channel input (if disconnected, channel self-pings)
//   Audio Out 1/2    channel output
//   Pulse In 1/2     ping (strike) the vactrol
//   CV In 1/2        continuous gate level (added to / max'd with the ping)
//   CV Out 1/2       the vactrol "glow" envelope, as a 0..+6V CV
//   Knob X / Knob Y  decay/response time for channel 1 / channel 2 (~10ms..3s)
//   Main knob        resonance ("colour") for both filters
//   Switch           Down = VCA only, Middle = LPG (VCA+VCF), Up = VCF only
//   LEDs             each column glows with that channel's envelope

#include "ComputerCard.h"
#include <cmath>

// Fast linear-congruential noise, as used elsewhere in the collection
static int32_t __not_in_flash_func(rnd12)()
{
	static uint32_t lcg_seed = 22222;
	lcg_seed = 1664525 * lcg_seed + 1013904223;
	return lcg_seed >> 20; // 0..4095
}

// State-variable low-pass filter (integer audio path, float coefficient setup).
// Adapted from the SVF used in 07_bumpers; widened to int64 internals so high
// resonance / self-oscillation cannot overflow.
template <typename T = int64_t, unsigned shift = 16>
class SVFLowPass
{
	T ic1eq, ic2eq, v1rem, v2rem;
	int32_t a1, a2, a3;
public:
	SVFLowPass()
	{
		ic1eq = ic2eq = v1rem = v2rem = 0;
		SetFreqRaw(tanf(M_PI * 200.0f / 48000.0f), 1.0f);
	}

	// g = tan(pi*f0/fs), k = 1/Q
	void SetFreqRaw(float g, float k)
	{
		float fa1 = (1l << shift) / (1.0f + g * (g + k));
		a1 = fa1;
		a2 = fa1 * g;
		a3 = fa1 * g * g;
	}

	int32_t __not_in_flash_func(operator())(int32_t x)
	{
		T v3 = x - ic2eq;
		T v1 = (a1 * ic1eq + a2 * v3 + v1rem);
		v1rem = v1 & 0x7FFF;
		v1 >>= (shift - 1);
		T v2 = (a2 * ic1eq + a3 * v3 + v2rem);
		v2rem = v2 & 0x7FFF;
		v2 >>= (shift - 1);
		ic1eq = v1 - ic1eq;
		ic2eq = v2 + ic2eq;
		return ic2eq;
	}
};

class Vactrol : public ComputerCard
{
	// --- fixed-point envelope scale ---
	static constexpr int32_t ONE = 1 << 24; // full vactrol conductance

	// --- control-rate decimation (filter coeffs / knobs updated here) ---
	static constexpr int CTRL_DECIM = 16;

	// --- envelope shaping ---
	static constexpr int32_t ATTACK_ALPHA = ONE >> 7; // ~2.7ms one-pole attack
	static constexpr int PING_HOLD = 240;             // 5ms minimum strike
	// Self-ping excitation spans the strike window so the noise is present while
	// the envelope-tracked cutoff opens (a shorter burst dies before the gate
	// opens and is inaudible).
	static constexpr int BURST_LEN = 240;             // ~5ms self-ping noise

	// --- filter cutoff range (Hz) mapped over the envelope ---
	static constexpr int NUM_CUTOFF = 256;
	static constexpr float F_MIN = 30.0f;
	static constexpr float F_MAX = 12000.0f;

	// Precomputed tables (built once, in constructor)
	float gTable[NUM_CUTOFF];   // tan-mapped cutoff coefficient, indexed by env
	int32_t decayTable[256];    // per-sample decay multiplier (Q24), indexed by knob
	float kTable[256];          // resonance 1/Q, indexed by Main knob

	// Per-channel state
	SVFLowPass<> lpf[2];
	int32_t env[2];      // current vactrol conductance, 0..ONE (Q24)
	int32_t decayMul[2]; // current decay multiplier (Q24)
	int pingHold[2];     // samples remaining of the current strike
	int burst[2];        // samples remaining of self-ping noise burst

	int ctrl;            // control-rate decimation counter
	float resK;          // current resonance 1/Q

public:
	Vactrol()
	{
		// Envelope -> filter cutoff (exponential frequency sweep)
		for (int c = 0; c < NUM_CUTOFF; c++)
		{
			float f = F_MIN * powf(F_MAX / F_MIN, c / float(NUM_CUTOFF - 1));
			gTable[c] = tanf(M_PI * f / 48000.0f);
		}

		// Knob -> decay time (~10ms..3s), expressed as a Q24 per-sample multiplier
		for (int s = 0; s < 256; s++)
		{
			float tSamples = 480.0f * powf(300.0f, s / 255.0f); // 480..144000
			decayTable[s] = ONE - int32_t(ONE / tSamples);
		}

		// Main knob -> resonance, Q from ~0.6 to ~6
		for (int s = 0; s < 256; s++)
		{
			float Q = 0.6f * powf(10.0f, s / 255.0f);
			kTable[s] = 1.0f / Q;
		}

		for (int i = 0; i < 2; i++)
		{
			env[i] = 0;
			decayMul[i] = decayTable[128];
			pingHold[i] = 0;
			burst[i] = 0;
		}
		ctrl = 0;
		resK = kTable[0];
	}

	virtual void __not_in_flash_func(ProcessSample)()
	{
		Switch mode = SwitchVal();

		// ---- control rate: refresh knob-derived parameters & filter coeffs ----
		if (--ctrl <= 0)
		{
			ctrl = CTRL_DECIM;
			resK = kTable[KnobVal(Main) >> 4];
			decayMul[0] = decayTable[KnobVal(X) >> 4];
			decayMul[1] = decayTable[KnobVal(Y) >> 4];

			// Filter cutoff tracks the current envelope of each channel
			for (int i = 0; i < 2; i++)
			{
				int idx = env[i] >> 16; // ONE>>16 == NUM_CUTOFF
				if (idx >= NUM_CUTOFF) idx = NUM_CUTOFF - 1;
				lpf[i].SetFreqRaw(gTable[idx], resK);
			}
		}

		for (int i = 0; i < 2; i++)
		{
			// --- strike detection ---
			if (PulseInRisingEdge(i))
			{
				pingHold[i] = PING_HOLD;
				if (Disconnected(i ? Audio2 : Audio1)) burst[i] = BURST_LEN;
			}

			// --- target conductance = max(continuous CV, current strike) ---
			int32_t cvLevel = CVIn(i);
			cvLevel = (cvLevel <= 0) ? 0 : cvLevel * 8192; // +full CV ~= ONE
			if (cvLevel > ONE) cvLevel = ONE;

			int32_t target = cvLevel;
			if (pingHold[i] > 0)
			{
				pingHold[i]--;
				if (target < ONE) target = ONE;
			}

			// --- vactrol response: fast attack, slow exponential decay ---
			if (target > env[i])
			{
				env[i] += (int32_t)(((int64_t)(target - env[i]) * ATTACK_ALPHA) >> 24);
			}
			else
			{
				env[i] = target +
					(int32_t)(((int64_t)(env[i] - target) * decayMul[i]) >> 24);
			}

			// --- channel input: external audio, or internal self-ping noise ---
			int32_t in;
			if (Connected(i ? Audio2 : Audio1))
			{
				in = AudioIn(i);
			}
			else if (burst[i] > 0)
			{
				burst[i]--;
				in = (rnd12() - 2048) >> 1; // ~+/-1024 excitation
			}
			else
			{
				in = 0;
			}

			// --- apply VCF and/or VCA according to switch mode ---
			int32_t filtered = (mode == Down) ? in : lpf[i](in);

			int32_t out;
			if (mode == Up)
			{
				out = filtered; // VCF only: cutoff follows env, no amplitude gate
			}
			else
			{
				// VCA (Down) or LPG (Middle): multiply by envelope (Q16 gain)
				out = (int32_t)(((int64_t)filtered * (env[i] >> 8)) >> 16);
			}

			if (out > 2047) out = 2047;
			if (out < -2048) out = -2048;
			AudioOut(i, out);

			// --- envelope available as CV out, and on the LED column ---
			int32_t cvOut = env[i] >> 13; // 0..2047 (~0..+6V)
			if (cvOut > 2047) cvOut = 2047;
			CVOut(i, cvOut);

			int led = env[i] >> 12; // 0..4096
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

	Vactrol vactrol;
	vactrol.EnableNormalisationProbe();
	vactrol.Run();
}
#endif
