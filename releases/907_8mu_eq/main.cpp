// 8mu EQ
//
// Standalone ComputerCard port of a Patcher+ patch
//
// 8mu-based stereo graphic EQ, with eight bandpass filters per channel at
// 60, 120, 240, 480, 960, 1920, 3840 and 7680Hz.
//
//   Audio in 1/2     - left/right input, normalled to a noise source.
//                      A mono input into Audio in 1 alone feeds both channels.
//   Switch           - normalled noise is pink-ish when up, white otherwise
//   Main knob        - filter Q, 0.5 to 3.5
//   Knob Y           - slew rate limit on the 8mu faders
//   8mu faders 1-8   - amplitude of each band (square law, zero at the bottom)
//   Audio out 1/2    - left/right output
//   8mu LEDs 1-8     - signal level in each band of the left channel
//   LEDs 0/1         - audio inputs 1/2
//   LEDs 4/5         - audio outputs 1/2
//   All LEDs flash at 2Hz while no 8mu is connected


#include <cmath>

#include "ComputerCard.h"
#include "EightMU.h"


static constexpr int numBands = 8;
static constexpr int controlInterval = 8;
static_assert(controlInterval == numBands, "one band coefficient update per control phase");


static inline int32_t Clip(int32_t x, int32_t lo, int32_t hi)
{
	if (x < lo)
	{
		return lo;
	}
	if (x > hi)
	{
		return hi;
	}
	return x;
}


// Linear congruential generator
static uint32_t lcgSeed = 1;
static inline uint32_t Rnd()
{
	lcgSeed = 1664525 * lcgSeed + 1013904223;
	return lcgSeed;
}

// White noise, -2048 to 2047
static inline int32_t Noise()
{
	return int32_t(Rnd() >> 20) - 2048;
}


////////////////////////////////////////////////////////////////////////////////
// State variable filter, bandpass output only.
//
// The coefficients depend only on cutoff and Q, so they are held separately
// and shared by the left and right filters of each band. Internal state is
// clipped to +-32767 to avoid 32-bit wrapping, and the denominator is rounded
// up to keep the pole radius below one at low cutoffs.

struct SVFCoeffs
{
	int32_t gFixed; // tan(pi*fc/48000) in Q16, fixed at startup
	int32_t a1v, a2v, a3v;

	void SetFreq(float hz)
	{
		gFixed = (int32_t)(tanf((float)M_PI * hz / 48000.0f) * 65536.0f + 0.5f);
	}

	// Q in Q12, converted to the damping k = 1/Q in Q16
	static int32_t QToK(int32_t qIn)
	{
		qIn = Clip(qIn, 2048, 819200);
		return (1 << 28) / qIn;
	}

	void SetK(int32_t kFixed)
	{
		int32_t gpkFp = gFixed + kFixed;
		int32_t gHi = gFixed >> 8, gLo = gFixed & 0xFF;
		int32_t gkHi = gpkFp >> 8, gkLo = gpkFp & 0xFF;
		int32_t pHi = gHi * gkHi;
		int32_t pMid = gHi * gkLo + gLo * gkHi;
		int32_t pLo = gLo * gkLo;
		int32_t denomFp = (1 << 16) + pHi + (((pMid << 8) + pLo + 65535) >> 16);

		a1v = 0x80000000 / denomFp;
		a2v = (a1v * gFixed) >> 16;
		a3v = (a2v * gFixed) >> 16;
	}
};

class SVFBandpass
{
public:
	int32_t __not_in_flash_func(Process)(int32_t x, const SVFCoeffs &c)
	{
		int32_t v3 = x - ic2eq;

		// Clip to avoid wrapping
		ic1eq = Clip(ic1eq, -32767, 32767);
		v3 = Clip(v3, -32767, 32767);

		int32_t v1a = c.a1v * ic1eq + c.a2v * v3 + v1rem;
		v1rem = v1a & 0x3FFF;
		int32_t v1 = v1a >> 14;

		int32_t v2a = c.a2v * ic1eq + c.a3v * v3 + v2rem;
		v2rem = v2a & 0x3FFF;
		int32_t v2 = v2a >> 14;

		ic1eq = v1 - ic1eq;
		ic2eq = v2 + ic2eq;

		return v1 >> 1;
	}

private:
	int32_t ic1eq = 0, ic2eq = 0, v1rem = 0, v2rem = 0;
};


////////////////////////////////////////////////////////////////////////////////
// VA first-order low-pass with fixed cutoff

class LP6
{
public:
	void SetFreq(float hz)
	{
		// G = g/(1+g) in Q24
		float g = tanf((float)M_PI * hz / 48000.0f);
		int32_t gq = (int32_t)(g / (1.0f + g) * 16777216.0f + 0.5f);
		gHi = gq >> 12;
		gLo = gq & 0xFFF;
	}

	int32_t __not_in_flash_func(Process)(int32_t in)
	{
		int32_t d = in - s;
		int32_t q = d * gLo + rLo;
		rLo = q & 0xFFF;
		int32_t p = d * gHi + (q >> 12) + rHi;
		rHi = p & 0xFFF;
		int32_t v = p >> 12;
		int32_t y = v + s;
		s = y + v;
		return y;
	}

private:
	int32_t s = 0, rLo = 0, rHi = 0;
	int32_t gHi = 0, gLo = 0;
};


////////////////////////////////////////////////////////////////////////////////
// Slew rate limiter
//
// Rates are in units per second, i.e. 48000ths of a unit per sample. The
// split into whole and fractional steps per sample is held in SlewRate, so
// the divide happens once when a rate changes rather than in every slew.

struct SlewRate
{
	bool instant = true; // jump straight to the target
	int32_t rate = -1, whole = 0, frac = 0;

	void Set(int32_t newRate)
	{
		if (newRate == rate)
		{
			return;
		}
		instant = false;
		rate = newRate;
		whole = newRate / 48000;
		frac = newRate - whole * 48000;
	}
};

class Slew
{
public:
	int32_t __not_in_flash_func(Process)(int32_t target, const SlewRate &rise, const SlewRate &fall)
	{
		if (needsInit)
		{
			needsInit = false;
			state = target;
		}

		int32_t diff = target - state;
		if (diff == 0)
		{
			frac = 0;
			dir = 0;
			return state;
		}

		int32_t newDir = (diff > 0) ? 1 : -1;
		if (newDir != dir)
		{
			frac = 0;
			dir = newDir;
		}

		const SlewRate &r = (newDir > 0) ? rise : fall;
		if (r.instant)
		{
			state = target;
			return state;
		}

		int32_t steps = r.whole;
		frac += r.frac;
		if (frac >= 48000)
		{
			frac -= 48000;
			steps++;
		}
		if (newDir > 0)
		{
			state = std::min(state + steps, target);
		}
		else
		{
			state = std::max(state - steps, target);
		}
		return state;
	}

private:
	int32_t state = 0, frac = 0, dir = 0;
	bool needsInit = true;
};


////////////////////////////////////////////////////////////////////////////////

class EightMUEQ : public ComputerCard
{
public:
	EightMUEQ()
	{
		static constexpr float bandHz[numBands] = {60, 120, 240, 480, 960, 1920, 3840, 7680};
		for (int b = 0; b < numBands; b++)
		{
			coeffs[b].SetFreq(bandHz[b]);
			coeffs[b].SetK(SVFCoeffs::QToK(2048));
		}
		for (int c = 0; c < 2; c++)
		{
			pinkLo[c].SetFreq(973.0f);
			pinkHi[c].SetFreq(1879.0f);
		}
		peakFall.Set(30 << 12);

		EnableNormalisationProbe();

		// Give the USB power circuitry time to settle, then only start the
		// USB host stack if this really is a host port.
		sleep_us(150000);
		if (USBPowerState() == DFP)
		{
			mu.Start(); // claims core1
		}
	}

	virtual void __not_in_flash_func(ProcessSample)()
	{
		// Control-rate work is spread over an 8-sample cycle, one filter
		// band per sample, so no single sample carries all the divides
		int phase = controlCounter;
		if (++controlCounter >= controlInterval)
		{
			controlCounter = 0;
		}
		bool control = (phase == 0);

		if (control)
		{
			// Main knob --> filter Q = 0.5 + 3A
			kFixed = SVFCoeffs::QToK(2048 + 3 * KnobVal(Main));
		}
		else if (phase == 1)
		{
			// Knob Y --> fader slew rate = 0.1 + 20A^2
			int32_t ky = KnobVal(Y);
			faderRate.Set(410 + ((20 * ky * ky) >> 12));
		}
		coeffs[phase].SetK(kFixed);

		// Fader gains, shared by both channels
		int32_t gain[numBands];
		for (int b = 0; b < numBands; b++)
		{
			gain[b] = faderSlew[b].Process(mu.Fader(b), faderRate, faderRate);
		}

		// Normalled noise: white, or the sum of two one-pole low-passes
		// of it when the switch is up
		bool pink = (SwitchVal() == Up);
		int32_t in[2];
		for (int c = 0; c < 2; c++)
		{
			int32_t white = Noise();
			int32_t normal = white;
			if (pink)
			{
				normal = pinkHi[c].Process(white) + pinkLo[c].Process(white);
			}
			else
			{
				// Keep the filters running, so switching is smooth
				pinkHi[c].Process(white);
				pinkLo[c].Process(white);
			}
			in[c] = normal;
		}
		if (Connected(Input::Audio1))
		{
			in[0] = AudioIn1() << 1;
		}
		if (Connected(Input::Audio2))
		{
			in[1] = AudioIn2() << 1;
		}
		else if (Connected(Input::Audio1))
		{
			in[1] = in[0];
		}

		// Filter bank. Each band is scaled by the square of its fader.
		int32_t sum[2] = {0, 0};
		int32_t level[numBands];
		for (int b = 0; b < numBands; b++)
		{
			for (int c = 0; c < 2; c++)
			{
				int32_t bp = svf[c][b].Process(in[c], coeffs[b]);
				sum[c] += (((bp * gain[b]) >> 12) * gain[b]) >> 12;
				if (c == 0)
				{
					// Band energy into a peak detector: instant rise, fall at 30.0/s
					level[b] = peak[b].Process((bp * bp) >> 12, peakRise, peakFall);
				}
			}
		}

		AudioOut1(Clip(sum[0] >> 1, -2048, 2047));
		AudioOut2(Clip(sum[1] >> 1, -2048, 2047));

		if (control)
		{
			if (mu.Connected())
			{
				LedBrightness(0, Clip(in[0], 0, 4095));
				LedBrightness(1, Clip(in[1], 0, 4095));
				LedOff(2);
				LedOff(3);
				LedBrightness(4, Clip(sum[0], 0, 4095));
				LedBrightness(5, Clip(sum[1], 0, 4095));
			}
			else
			{
				// No 8mu: flash all LEDs at 2Hz. Counts control steps (6kHz).
				if (++flashCounter >= 3000)
				{
					flashCounter = 0;
				}
				bool on = (flashCounter < 1500);
				for (int i = 0; i < 6; i++)
				{
					LedOn(i, on);
				}
			}
			for (int b = 0; b < numBands; b++)
			{
				mu.SetLed(b, level[b]);
			}
		}

	}

private:
	EightMU mu;
	SVFCoeffs coeffs[numBands];
	SVFBandpass svf[2][numBands];
	LP6 pinkLo[2], pinkHi[2];
	Slew faderSlew[numBands];
	Slew peak[numBands];
	SlewRate faderRate, peakRise, peakFall;
	int32_t kFixed = 0;
	int controlCounter = 0;
	int flashCounter = 0;
};


int main()
{
	set_sys_clock_khz(240000, true);

	static EightMUEQ card;
	card.Run();
}
