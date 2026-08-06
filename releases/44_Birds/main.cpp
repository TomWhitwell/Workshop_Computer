#include "ComputerCard.h"

#include <algorithm>
#include <cstdint>
#include "pico/stdlib.h"
#include "hardware/clocks.h"

#ifndef BIRDS_CARD_NAME
#define BIRDS_CARD_NAME "Birds"
#endif

#ifndef BIRDS_CARD_NUMBER
#define BIRDS_CARD_NUMBER 44
#endif

#ifndef BIRDS_CARD_VERSION
#define BIRDS_CARD_VERSION "0.5.0"
#endif

class BirdCard : public ComputerCard
{
public:
	static constexpr const char *cardName = BIRDS_CARD_NAME;
	static constexpr uint8_t cardNumber = BIRDS_CARD_NUMBER;
	static constexpr const char *cardVersion = BIRDS_CARD_VERSION;
	static constexpr uint32_t sampleRate = 48000;
	static constexpr uint32_t two32 = 0xffffffffu;
	static constexpr uint32_t phaseIncPerHz = 89478; // 2^32 / 48000, rounded low
	static constexpr unsigned tableSize = 512;
	static constexpr uint32_t tableMask = tableSize - 1;
	static constexpr uint32_t pulseWidthSamples = sampleRate / 180; // about 5.5 ms

	enum Primitive : uint8_t
	{
		Rest,
		Tone,
		UpSweep,
		DownSweep,
		Swoop,
		ReverseSwoop,
		ChirpUp,
		ChirpDown,
		Trill,
		MelodyRun,
		Coo
	};

	struct BirdVoice
	{
		Primitive primitive = Rest;
		uint32_t phase = 0;
		uint32_t modPhase = 0;
		uint32_t modInc = 0;
		uint32_t pos = 0;
		uint32_t duration = 0;
		uint32_t pendingDelay = 0;
		uint32_t startHz = 1000;
		uint32_t endHz = 1400;
		uint32_t currentHz = 0;
		uint32_t trillPeriod = 900;
		uint32_t halfDuration = 1;
		uint32_t attackEnd = 1;
		uint32_t decayStart = 1;
		uint32_t attackStep = 32767;
		uint32_t decayStep = 32767;
		uint32_t amDepthQ12 = 0;
		uint32_t fmDepthQ12 = 0;
		uint32_t gateHighSamples = 1;
		uint32_t gateLowSamples = 1;
		uint32_t gateCountdown = 1;
		int32_t hzQ16 = 0;
		int32_t minHzQ16 = 0;
		int32_t maxHzQ16 = 0;
		int32_t slope1Q16 = 0;
		int32_t slope2Q16 = 0;
		int32_t level = 0;
		bool active = false;
		bool gateWasHigh = false;
		bool gateHigh = true;
	};

	struct PendingStart
	{
		bool active = false;
		Primitive primitive = Rest;
		uint8_t family = 0;
		uint16_t mainKnob = 0;
		uint16_t speedControl = 0;
		uint32_t delay = 0;
		bool wildMode = false;
	};

	int16_t sine[tableSize] {};
	uint32_t pitchScale[4096] {};
	BirdVoice birds[2];
	PendingStart pending[2];

	uint16_t reg = 0xACE1;
	uint32_t rngState = 0x12345678;
	uint32_t syllableCountdown = 4000;
	uint32_t reseedHoldoff = 0;
	uint32_t stepCounter = 0;
	uint32_t birdSeqStep[2] {};
	uint32_t pulseCount[2] {};
	uint32_t externalClockTimeout = 0;
	uint8_t prevPrim[2] {Rest, Rest};
	uint8_t lastFamily = 0;
	uint8_t slowUpdateCounter = 0;
	uint8_t duetLeader = 0;
	int16_t heldPitchCv[2] {};
	bool lastPulseOut[2] {};
	bool prevExtClock = false;

	BirdCard()
	{
		(void)cardName;
		(void)cardNumber;
		(void)cardVersion;

		for (unsigned i = 0; i < tableSize; ++i)
		{
			int32_t x = int32_t((i * 65536u) / tableSize);
			if (x > 32767)
			{
				x -= 65536;
			}
			const int32_t ax = x < 0 ? -x : x;
			int32_t y = (x * (32768 - ax)) >> 13;
			y = (y * 7) >> 3;
			sine[i] = int16_t(std::clamp(y, int32_t(-28672), int32_t(28672)));
		}
		for (unsigned i = 0; i < 4096; ++i)
		{
			const uint32_t p = i * 5u;
			const uint32_t octave = p >> 12;
			const uint32_t frac = p & 0xfffu;
			const uint32_t base = 512u << octave; // Q12: 0.125x at bottom
			pitchScale[i] = base + ((base * frac) >> 12);
		}
	}

	virtual void ProcessSample()
	{
		const uint16_t mainKnob = uint16_t(std::clamp(KnobVal(Main), int32_t(0), int32_t(4095)));
		const uint16_t pitchControl = controlWithCv(X, CVIn1());
		const uint16_t speedControl = controlWithCv(Y, CVIn2());
		const bool wildMode = SwitchVal() == Up;

		handleReseedSwitch();

		const bool extClockNow = PulseIn1();
		const bool extClockRise = extClockNow && !prevExtClock;
		prevExtClock = extClockNow;

		bool tick = false;
		if (extClockRise)
		{
			tick = true;
			externalClockTimeout = sampleRate * 2u;
		}
		else if (externalClockTimeout > 0)
		{
			--externalClockTimeout;
		}
		else if (syllableCountdown == 0)
		{
			tick = true;
		}
		else
		{
			--syllableCountdown;
		}

		updatePendingStarts();

		if (tick)
		{
			stepTuring(mainKnob);
			startSyllablePair(mainKnob, speedControl, wildMode);
			syllableCountdown = nextInternalInterval(mainKnob, speedControl);
		}

		int16_t out[2] {};
		for (int i = 0; i < 2; ++i)
		{
			out[i] = processBird(i, pitchControl);
			if (pulseCount[i] > 0)
			{
				--pulseCount[i];
			}
		}

		AudioOut1(out[0]);
		AudioOut2(out[1]);
		updatePulseOuts();

		if ((slowUpdateCounter++ & 0x7f) == 0)
		{
			updatePitchCv();
			updateLeds();
		}
	}

private:
	static uint32_t clampu32(uint32_t v, uint32_t lo, uint32_t hi)
	{
		return std::min(std::max(v, lo), hi);
	}

	static int32_t clampi32(int32_t v, int32_t lo, int32_t hi)
	{
		return std::min(std::max(v, lo), hi);
	}

	static int32_t addClampi32(int32_t a, int32_t b, int32_t lo, int32_t hi)
	{
		if (b > 0)
		{
			if (a >= hi || b > hi - a)
			{
				return hi;
			}
		}
		else if (b < 0)
		{
			if (a <= lo || b < lo - a)
			{
				return lo;
			}
		}
		return a + b;
	}

	static uint32_t maxu32(uint32_t a, uint32_t b)
	{
		return std::max(a, b);
	}

	static uint32_t ratio(uint32_t value, uint32_t numerator, uint32_t denominator)
	{
		return (value * numerator) / denominator;
	}

	uint16_t controlWithCv(Knob knob, int16_t cv)
	{
		return uint16_t(std::clamp(KnobVal(knob) + int32_t(cv), int32_t(0), int32_t(4095)));
	}

	static uint32_t durationScaleQ12(uint16_t yVal)
	{
		// Y high is fast, Y low is slow: about 0.1x to 10x.
		static constexpr uint32_t curve[9] = {
			410u, 729u, 1295u, 2302u, 4096u, 7282u, 12953u, 23031u, 40960u
		};
		const uint32_t pos = (4095u - uint32_t(yVal)) * 8u;
		const uint32_t index = pos >> 12;
		if (index >= 8u)
		{
			return curve[8];
		}
		const uint32_t frac = pos & 0xfffu;
		return curve[index] + (((curve[index + 1u] - curve[index]) * frac) >> 12);
	}

	static uint32_t scaleTime(uint32_t samples, uint16_t yVal, uint32_t minSamples, uint32_t maxSamples)
	{
		const uint32_t scaled = (samples * durationScaleQ12(yVal)) >> 12;
		return clampu32(scaled, minSamples, maxSamples);
	}

	static uint32_t scaleTimeWarped(uint32_t samples, uint16_t yVal, uint32_t warpQ12, uint32_t minSamples, uint32_t maxSamples)
	{
		uint32_t scaled = (samples * durationScaleQ12(yVal)) >> 12;
		scaled = (scaled * warpQ12) >> 12;
		return clampu32(scaled, minSamples, maxSamples);
	}

	uint32_t rng()
	{
		rngState = 1664525u * rngState + 1013904223u;
		return rngState;
	}

	bool locked(uint16_t mainKnob) const
	{
		return mainKnob > 3900u || mainKnob < 196u;
	}

	uint32_t stableStepHash(uint16_t mainKnob, uint32_t salt) const
	{
		const uint32_t periodIndex = mainKnob < 196u ? (stepCounter & 31u) : (stepCounter & 15u);
		uint32_t x = (uint32_t(reg) << 16) ^ (periodIndex * 0x9e3779b9u) ^ salt;
		x ^= x >> 16;
		x *= 0x7feb352du;
		x ^= x >> 15;
		x *= 0x846ca68bu;
		x ^= x >> 16;
		return x;
	}

	uint32_t variation(uint16_t mainKnob, uint32_t salt)
	{
		return locked(mainKnob) ? stableStepHash(mainKnob, salt) : rng();
	}

	void handleReseedSwitch()
	{
		if (reseedHoldoff > 0)
		{
			--reseedHoldoff;
		}

		if (SwitchVal() == Down && SwitchChanged() && reseedHoldoff == 0)
		{
			reg = uint16_t(rng() ^ (rng() >> 16));
			if (reg == 0 || reg == 0xffff)
			{
				reg ^= 0x5A3Cu;
			}
			stepCounter = 0;
			syllableCountdown = 1;
			reseedHoldoff = sampleRate / 4;
		}
	}

	void stepTuring(uint16_t mainKnob)
	{
		const bool returningBit = (reg & 0x8000u) != 0;
		uint32_t flipThreshold = 4095u - mainKnob;
		if (mainKnob > 3900u)
		{
			flipThreshold = 0;
		}
		else if (mainKnob < 196u)
		{
			flipThreshold = 4096u;
		}
		const bool flip = (rng() & 0xfffu) < flipThreshold;
		const bool nextBit = returningBit ^ flip;
		reg = uint16_t((reg << 1) | (nextBit ? 1u : 0u));
		++stepCounter;
	}

	uint32_t nextInternalInterval(uint16_t mainKnob, uint16_t yVal)
	{
		const uint32_t jitter = 500u + (variation(mainKnob, 0x31415926u) & 0x1fffu);
		const uint32_t phraseBits = (reg >> 4) & 0x7u;
		const uint32_t base = 7600u + phraseBits * 940u + jitter;
		return scaleTime(base, yVal, uint32_t(2200), uint32_t(90000));
	}

	void startSyllablePair(uint16_t mainKnob, uint16_t speedControl, bool wildMode)
	{
		const uint8_t family = wildMode ? uint8_t(2u + ((reg >> 13) & 1u)) : uint8_t((reg >> 14) & 1u);
		lastFamily = family;
		if (locked(mainKnob))
		{
			startLockedDuetStep(mainKnob, speedControl, wildMode, family);
			return;
		}

		if (!birds[0].active && !birds[1].active && !pending[0].active && !pending[1].active)
		{
			const int leader = duetLeader & 1u;
			duetLeader ^= 1u;
			Primitive p = nextPrimitiveForBird(leader, family, mainKnob, wildMode);
			if (p != Rest)
			{
				beginPhrase(leader, p, speedControl, family, mainKnob, wildMode);
				return;
			}
			p = nextPrimitiveForBird(leader ^ 1, family, mainKnob, wildMode);
			if (p != Rest)
			{
				beginPhrase(leader ^ 1, p, speedControl, family, mainKnob, wildMode);
			}
			return;
		}

		for (int singer = 0; singer < 2; ++singer)
		{
			const int mate = singer ^ 1;
			if (birds[singer].active && !birds[mate].active && !pending[mate].active)
			{
				schedulePartnerAnswer(singer, birds[singer].primitive, speedControl, family, mainKnob, wildMode);
			}
		}
	}

	Primitive nextPrimitiveForBird(int bird, uint8_t family, uint16_t mainKnob, bool wildMode)
	{
		const bool lockedNow = locked(mainKnob);
		const uint32_t rawStep = birdSeqStep[bird]++;
		const uint32_t sequenceStep = lockedNow ? (mainKnob < 196u ? (rawStep & 31u) : (rawStep & 15u)) : rawStep;
		return primitiveForBirdAtStep(bird, sequenceStep, family, mainKnob, wildMode);
	}

	Primitive primitiveForBirdAtStep(int bird, uint32_t sequenceStep, uint8_t family, uint16_t mainKnob, bool wildMode)
	{
		const bool lockedNow = locked(mainKnob);
		const uint8_t phraseLen = uint8_t(3u + ((reg >> (bird == 0 ? 9 : 5)) & 0x0fu));
		const bool phraseReset = (sequenceStep % phraseLen) == 0;
		const bool invertedPass = mainKnob < 256 && ((sequenceStep / phraseLen) & 1u);
		const bool invert = bird == 0 ? invertedPass : !invertedPass;
		const uint32_t salt = bird == 0 ? 0x4682ace1u : 0x9d35f00du;
		const uint32_t pattern = reg ^ (reg >> (bird == 0 ? 5 : 3)) ^ (sequenceStep * (bird == 0 ? 5u : 7u)) ^ salt;
		uint8_t bits = uint8_t(pattern & 0x0fu);
		if (wildMode)
		{
			bits ^= uint8_t((sequenceStep >> 1) & 0x03u);
		}
		return decodePrimitive(bits, family, prevPrim[bird], invert, phraseReset, lockedNow);
	}

	void startPhraseDirect(int bird, Primitive p, uint16_t speedControl, uint8_t family, uint16_t mainKnob, bool wildMode)
	{
		if (p == Rest || birds[bird].active)
		{
			return;
		}
		startBird(bird, p, speedControl, family, mainKnob, wildMode, 0);
		prevPrim[bird] = p;
	}

	void beginPhrase(int bird, Primitive p, uint16_t speedControl, uint8_t family, uint16_t mainKnob, bool wildMode)
	{
		if (p == Rest || birds[bird].active)
		{
			return;
		}
		startPhraseDirect(bird, p, speedControl, family, mainKnob, wildMode);
		schedulePartnerAnswer(bird, p, speedControl, family, mainKnob, wildMode);
	}

	void startLockedDuetStep(uint16_t mainKnob, uint16_t speedControl, bool wildMode, uint8_t family)
	{
		pending[0].active = false;
		pending[1].active = false;

		const uint32_t period = mainKnob < 196u ? 32u : 16u;
		const uint32_t phase = stepCounter & (period - 1u);
		const uint32_t phrasePhase = phase & 0x0fu;
		const int leader = int((phase >> 3) & 1u);
		const int caller = (phrasePhase & 1u) == 0u ? leader : (leader ^ 1);
		const int answerer = caller ^ 1;

		Primitive p = primitiveForBirdAtStep(caller, phase, family, mainKnob, wildMode);
		if ((phrasePhase & 1u) != 0u)
		{
			const uint32_t code = stableStepHash(mainKnob, 0x51a7b1ddu + phase * 43u);
			p = answerCode(Primitive(prevPrim[answerer]), p, code >> 8);
		}
		startPhraseDirect(caller, p, speedControl, family, mainKnob, wildMode);

		if ((phrasePhase == 6u || phrasePhase == 14u) && !birds[answerer].active)
		{
			Primitive overlap = primitiveForBirdAtStep(answerer, phase + 5u, family, mainKnob, wildMode);
			const uint32_t code = stableStepHash(mainKnob, 0x0f1e2d3cu + phase * 29u);
			overlap = answerCode(p, overlap, code >> 8);
			startPhraseDirect(answerer, overlap, speedControl, family, mainKnob, wildMode);
		}
	}

	Primitive answerCode(Primitive heard, Primitive own, uint32_t code)
	{
		if (own == Rest)
		{
			if ((code & 0x07u) != 0u)
			{
				return Rest;
			}
			own = heard;
		}

		if (own == heard && (code & 0x03u) != 0u)
		{
			switch (heard)
			{
			case Tone: return (code & 0x04u) ? UpSweep : DownSweep;
			case UpSweep: return (code & 0x04u) ? Swoop : DownSweep;
			case DownSweep: return (code & 0x04u) ? ReverseSwoop : UpSweep;
			case Swoop: return (code & 0x04u) ? ChirpUp : ReverseSwoop;
			case ReverseSwoop: return (code & 0x04u) ? ChirpDown : Swoop;
			case ChirpUp: return (code & 0x04u) ? Tone : ChirpDown;
			case ChirpDown: return (code & 0x04u) ? Tone : ChirpUp;
			case Trill: return (code & 0x04u) ? ChirpUp : Tone;
			case MelodyRun: return (code & 0x04u) ? Swoop : Trill;
			case Coo: return (code & 0x04u) ? Tone : ReverseSwoop;
			default: return own;
			}
		}
		return own;
	}

	void schedulePartnerAnswer(int singer, Primitive heard, uint16_t speedControl, uint8_t family, uint16_t mainKnob, bool wildMode)
	{
		const int mate = singer ^ 1;
		if (heard == Rest || birds[mate].active || pending[mate].active)
		{
			return;
		}

		const uint32_t code = variation(mainKnob, 0x6d2b79f5u + uint32_t(singer) * 0x1111u + birdSeqStep[mate] * 37u);
		if ((code & 0xffu) > 205u)
		{
			return;
		}

		Primitive p = nextPrimitiveForBird(mate, family, mainKnob, wildMode);
		p = answerCode(heard, p, code >> 8);
		if (p == Rest)
		{
			return;
		}

		const uint32_t gap = scaleTime(700u + ((code >> 16) & 0x0fffu), speedControl, uint32_t(120), uint32_t(18000));
		uint32_t delay = birds[singer].duration + gap;
		const bool overlap = ((code >> 5) & 0x07u) == 0u;
		if (overlap && birds[singer].duration > gap + 480u)
		{
			delay = birds[singer].duration - gap;
		}

		pending[mate].active = true;
		pending[mate].primitive = p;
		pending[mate].family = family;
		pending[mate].mainKnob = mainKnob;
		pending[mate].speedControl = speedControl;
		pending[mate].delay = delay;
		pending[mate].wildMode = wildMode;
	}

	Primitive decodePrimitive(uint8_t bits, uint8_t family, uint8_t previous, bool invert, bool phraseReset, bool lockedNow)
	{
		static constexpr Primitive whistle[16] = {
			Rest, Tone, UpSweep, Rest, Swoop, Rest, ChirpUp, Tone,
			Rest, DownSweep, UpSweep, Rest, ChirpDown, Coo, Rest, Swoop
		};
		static constexpr Primitive liquid[16] = {
			Rest, UpSweep, DownSweep, Swoop, ChirpUp, ReverseSwoop, Tone, Rest,
			Rest, Swoop, UpSweep, Coo, Rest, MelodyRun, Rest, ReverseSwoop
		};
		static constexpr Primitive trillBird[16] = {
			Rest, Coo, Trill, Rest, UpSweep, Rest, ChirpUp, MelodyRun,
			DownSweep, Rest, Rest, Trill, ChirpDown, UpSweep, Rest, Rest
		};
		static constexpr Primitive wildTonal[16] = {
			Rest, MelodyRun, Rest, Coo, Trill, Rest, DownSweep, Swoop,
			ChirpDown, Rest, Rest, Trill, UpSweep, Rest, MelodyRun, Rest
		};

		Primitive p = Rest;
		switch (family)
		{
		case 0: p = whistle[bits]; break;
		case 1: p = liquid[bits]; break;
		case 2: p = trillBird[bits]; break;
		default: p = wildTonal[bits]; break;
		}

		if (phraseReset && family < 2 && p == Rest)
		{
			p = (family == 0) ? Swoop : UpSweep;
		}

		if (!lockedNow && previous == Trill && p == Trill && (rng() & 1u))
		{
			p = Rest;
		}
		else if (!lockedNow && previous != Rest && p != Rest && !phraseReset && (rng() & 0xffu) < 80u)
		{
			p = Rest;
		}

		if (invert)
		{
			p = invertPrimitive(p);
		}
		return p;
	}

	Primitive invertPrimitive(Primitive p)
	{
		switch (p)
		{
		case UpSweep: return DownSweep;
		case DownSweep: return UpSweep;
		case Swoop: return ReverseSwoop;
		case ReverseSwoop: return Swoop;
		case ChirpUp: return ChirpDown;
		case ChirpDown: return ChirpUp;
		default: return p;
		}
	}

	uint32_t baseFrequency(uint8_t family, uint16_t mainKnob, int bird)
	{
		uint32_t hz = 1500;
		if (family == 0)
		{
			hz = 1700;
		}
		else if (family == 1)
		{
			hz = 1250;
		}
		else if (family == 2)
		{
			hz = 1150;
		}
		else if (family >= 3)
		{
			hz = 1050;
		}

		if (bird == 1)
		{
			const int32_t cents = int32_t((variation(mainKnob, 0x12344321u) >> 5) & 0x1fu) - 16;
			hz = uint32_t((int32_t(hz) * (1024 + cents)) >> 10);
		}
		return clampu32(hz, uint32_t(60), uint32_t(8000));
	}

	void startBird(int bird, Primitive p, uint16_t speedControl, uint8_t family, uint16_t mainKnob, bool wildMode, uint32_t delay)
	{
		BirdVoice &v = birds[bird];
		const uint32_t var = variation(mainKnob, 0x22002200u + uint32_t(bird) * 17u + uint32_t(p) * 131u);
		const uint32_t base = baseFrequency(family, mainKnob, bird);
		const uint32_t durJitter = (var >> 8) & 0x7ffu;

		v.primitive = p;
		v.pos = 0;
		v.pendingDelay = delay;
		v.phase += var & 0xffffu;
		v.modPhase += (var >> 7) & 0xffffu;
		v.modInc = 0;
		v.amDepthQ12 = 0;
		v.fmDepthQ12 = 0;
		v.active = delay == 0;
		v.gateWasHigh = false;
		v.gateHigh = true;
		v.level = 0;

		uint32_t dur = 5200u + durJitter;
		if (family == 1)
		{
			dur = 4200u + durJitter;
		}
		else if (family == 2)
		{
			dur = 3000u + (durJitter >> 1);
		}
		else if (family >= 3)
		{
			dur = 3600u + (durJitter >> 1);
		}

		uint32_t timeWarpQ12 = 2560u + ((var >> 19) & 0x0fffu);

		switch (p)
		{
		case Tone:
			v.startHz = base;
			v.endHz = ratio(base, 26, 25);
			if ((var & 0x00400000u) != 0u)
			{
				dur += 4200u + ((var >> 5) & 0x0fffu);
			}
			break;
		case UpSweep:
			v.startHz = wildMode ? ratio(base, 11, 20) : ratio(base, 17, 20);
			v.endHz = wildMode ? ratio(base, 49, 20) : ratio(base, 7, 4);
			if ((var & 0x03u) == 0u)
			{
				dur += wildMode ? 9000u : 3600u;
			}
			if ((var & 0x01000000u) != 0u)
			{
				timeWarpQ12 += 3072u;
			}
			break;
		case DownSweep:
			v.startHz = wildMode ? ratio(base, 49, 20) : ratio(base, 7, 4);
			v.endHz = wildMode ? ratio(base, 11, 20) : ratio(base, 17, 20);
			if ((var & 0x03u) == 0u)
			{
				dur += wildMode ? 9000u : 3600u;
			}
			if ((var & 0x01000000u) != 0u)
			{
				timeWarpQ12 += 3072u;
			}
			break;
		case Swoop:
		case ReverseSwoop:
			v.startHz = ratio(base, 19, 20);
			v.endHz = ratio(base, 37, 25);
			dur += 1200u;
			if ((var & 0x02000000u) != 0u)
			{
				timeWarpQ12 += 2048u;
			}
			break;
		case ChirpUp:
			v.startHz = ratio(base, 11, 10);
			v.endHz = base * 3u;
			dur = 2200u + (durJitter >> 1);
			timeWarpQ12 = 1800u + ((var >> 17) & 0x07ffu);
			break;
		case ChirpDown:
			v.startHz = base * 3u;
			v.endHz = ratio(base, 11, 10);
			dur = 2200u + (durJitter >> 1);
			timeWarpQ12 = 1800u + ((var >> 17) & 0x07ffu);
			break;
		case Trill:
			v.startHz = ratio(base, 23, 20);
			v.endHz = ratio(base, 31, 20);
			v.trillPeriod = 90u + ((var >> 4) & 0x17fu);
			dur = ((var >> 18) & 1u) ? 1800u + (durJitter >> 2) : 5200u + durJitter;
			break;
		case MelodyRun:
			if (var & 0x10000u)
			{
				v.startHz = ratio(base, 3, 4);
				v.endHz = ratio(base, 11, 5);
			}
			else
			{
				v.startHz = base * 2u;
				v.endHz = ratio(base, 4, 5);
			}
			v.trillPeriod = 80u + ((var >> 5) & 0xffu);
			dur = 7200u + (durJitter << 1);
			timeWarpQ12 = 2800u + ((var >> 18) & 0x17ffu);
			break;
		case Coo:
		{
			const uint32_t cooBase = ratio(base, wildMode ? 11u : 13u, 20u);
			v.startHz = ratio(cooBase, 9, 10);
			v.endHz = ratio(cooBase, (var & 0x4000u) ? 6u : 11u, (var & 0x4000u) ? 5u : 10u);
			v.modInc = (14u + ((var >> 11) & (wildMode ? 0x1fu : 0x0fu))) * phaseIncPerHz;
			v.amDepthQ12 = 900u + ((var >> 21) & 0x03ffu);
			v.fmDepthQ12 = 18u + ((var >> 16) & 0x001fu);
			dur = 9800u + (durJitter << 2);
			timeWarpQ12 = 3600u + ((var >> 18) & 0x1fffu);
			break;
		}
		default:
			break;
		}

		v.startHz = clampu32(v.startHz, uint32_t(12), uint32_t(19000));
		v.endHz = clampu32(v.endHz, uint32_t(12), uint32_t(19000));
		v.duration = scaleTimeWarped(maxu32(dur, uint32_t(600)), speedControl, timeWarpQ12, uint32_t(900), uint32_t(76000));
		v.trillPeriod = scaleTime(v.trillPeriod, speedControl, uint32_t(40), uint32_t(9000));
		prepareSyllable(v);
	}

	bool updatePendingStarts()
	{
		bool started = false;
		for (int bird = 0; bird < 2; ++bird)
		{
			PendingStart &p = pending[bird];
			if (!p.active)
			{
				continue;
			}
			if (p.delay > 0)
			{
				--p.delay;
				continue;
			}
			if (!birds[bird].active)
			{
				beginPhrase(bird, p.primitive, p.speedControl, p.family, p.mainKnob, p.wildMode);
				started = true;
			}
			p.active = false;
		}
		return started;
	}

	int16_t processBird(int bird, uint16_t pitchControl)
	{
		BirdVoice &v = birds[bird];
		if (!v.active)
		{
			if (v.pendingDelay > 0)
			{
				--v.pendingDelay;
				if (v.pendingDelay == 0)
				{
					v.active = true;
					v.pos = 0;
					pulseCount[bird] = pulseWidthSamples;
				}
			}
			v.level = 0;
			v.currentHz = 0;
			return 0;
		}

		if (v.pos == 0)
		{
			pulseCount[bird] = pulseWidthSamples;
		}

		if (v.pos >= v.duration)
		{
			v.active = false;
			v.level = 0;
			v.currentHz = 0;
			return 0;
		}

		updateGate(v, bird);
			uint32_t hz = currentContourHz(v);
			hz = applyBodyFm(v, hz);
			hz = applyLivePitchControl(hz, pitchControl);
			v.currentHz = hz;
			v.phase += hz * phaseIncPerHz;
			int32_t osc = sine[(v.phase >> 23) & tableMask] >> 4;

			int32_t amp = envelope(v);
			amp = applyBodyAm(v, amp);

			++v.pos;
			v.modPhase += v.modInc;
			advanceContour(v);
			v.level = (osc * amp) >> 15;
			return int16_t(std::clamp(v.level, int32_t(-2048), int32_t(2047)));
	}

	void prepareSyllable(BirdVoice &v)
	{
		const uint32_t attack = maxu32(v.duration / 8u, uint32_t(80));
		const uint32_t decay = maxu32(v.duration / 5u, uint32_t(120));
		v.attackEnd = attack;
		v.decayStart = v.duration > decay ? v.duration - decay : 0;
		v.attackStep = maxu32(32767u / attack, uint32_t(1));
		v.decayStep = maxu32(32767u / decay, uint32_t(1));
		v.halfDuration = maxu32(v.duration / 2u, uint32_t(1));
		v.gateHighSamples = maxu32((v.trillPeriod * 3u) / 5u, uint32_t(1));
		v.gateLowSamples = maxu32(v.trillPeriod - v.gateHighSamples, uint32_t(1));
		v.gateCountdown = v.gateHighSamples;
		v.gateHigh = true;

		const int32_t startQ16 = int32_t(v.startHz << 16);
		const int32_t endQ16 = int32_t(v.endHz << 16);
		v.minHzQ16 = int32_t(std::min(v.startHz, v.endHz) << 16);
		v.maxHzQ16 = int32_t(std::max(v.startHz, v.endHz) << 16);

		if (v.primitive == Swoop || v.primitive == ReverseSwoop)
		{
			const uint32_t lo = std::min(v.startHz, v.endHz);
			const uint32_t hi = std::max(v.startHz, v.endHz);
			const int32_t loQ16 = int32_t(lo << 16);
			const int32_t hiQ16 = int32_t(hi << 16);

			if (v.primitive == ReverseSwoop)
			{
				v.hzQ16 = hiQ16;
				v.slope1Q16 = (loQ16 - hiQ16) / int32_t(v.halfDuration);
				v.slope2Q16 = (hiQ16 - loQ16) / int32_t(v.duration - v.halfDuration);
			}
			else
			{
				v.hzQ16 = loQ16;
				v.slope1Q16 = (hiQ16 - loQ16) / int32_t(v.halfDuration);
				v.slope2Q16 = (loQ16 - hiQ16) / int32_t(v.duration - v.halfDuration);
			}
			return;
		}

		if (v.primitive == MelodyRun)
		{
			v.hzQ16 = startQ16;
			v.slope1Q16 = (endQ16 - startQ16) / int32_t(v.duration);
			v.slope2Q16 = v.slope1Q16;
			return;
		}

		v.hzQ16 = startQ16;
		v.slope1Q16 = (endQ16 - startQ16) / int32_t(v.duration);
		v.slope2Q16 = v.slope1Q16;
	}

	uint32_t currentContourHz(const BirdVoice &v)
	{
		if (v.primitive == Trill)
		{
			const uint32_t lo = std::min(v.startHz, v.endHz);
			const uint32_t hi = std::max(v.startHz, v.endHz);
			const uint32_t span = hi - lo;
			const uint32_t period = maxu32(v.trillPeriod, uint32_t(2));
			const uint32_t half = maxu32(period >> 1, uint32_t(1));
			const uint32_t t = v.pos % period;
			const uint32_t tri = t < half ? (t * 4095u) / half : ((period - t) * 4095u) / maxu32(period - half, uint32_t(1));
			return lo + ((span * tri) >> 12);
		}
		const int32_t clamped = clampi32(v.hzQ16, v.minHzQ16, v.maxHzQ16);
		return uint32_t(clamped >> 16);
	}

	void advanceContour(BirdVoice &v)
	{
		if (v.primitive == Trill)
		{
			return;
		}

		v.hzQ16 = addClampi32(v.hzQ16, v.pos < v.halfDuration ? v.slope1Q16 : v.slope2Q16,
			v.minHzQ16, v.maxHzQ16);
	}

	int32_t envelope(const BirdVoice &v)
	{
		if (v.pos < v.decayStart && v.pos > v.attackEnd)
		{
			return 32767;
		}
		if (v.pos < v.attackEnd)
		{
			return int32_t(std::min(v.pos * v.attackStep, uint32_t(32767)));
		}
		return int32_t(std::min((v.duration - v.pos) * v.decayStep, uint32_t(32767)));
	}

	void updateGate(BirdVoice &v, int bird)
	{
		if (v.primitive != Trill && v.primitive != MelodyRun)
		{
			return;
		}

		if (v.gateCountdown > 0)
		{
			--v.gateCountdown;
		}

		if (v.gateCountdown == 0)
		{
			v.gateHigh = !v.gateHigh;
			v.gateCountdown = v.gateHigh ? v.gateHighSamples : v.gateLowSamples;
			if (v.gateHigh)
			{
				pulseCount[bird] = pulseWidthSamples;
				}
			}
		}

	uint32_t applyBodyFm(const BirdVoice &v, uint32_t hz)
	{
		if (v.primitive != Coo || v.fmDepthQ12 == 0)
		{
			return hz;
		}
		const int32_t mod = sine[(v.modPhase >> 23) & tableMask];
		const int32_t bendQ15 = (mod * int32_t(v.fmDepthQ12)) >> 12;
		const int32_t bent = int32_t(hz) + ((int32_t(hz) * bendQ15) >> 15);
		return clampu32(uint32_t(std::max(bent, int32_t(12))), uint32_t(12), uint32_t(19000));
	}

	int32_t applyBodyAm(const BirdVoice &v, int32_t amp)
	{
		if (v.primitive == Coo && v.amDepthQ12 != 0)
		{
			const int32_t mod = sine[((v.modPhase + 0x20000000u) >> 23) & tableMask];
			const uint32_t unipolarQ12 = uint32_t(2048 + (mod >> 4));
			const uint32_t levelQ12 = 4096u - v.amDepthQ12 + ((v.amDepthQ12 * unipolarQ12) >> 12);
			return (amp * int32_t(levelQ12)) >> 12;
		}
		return amp;
	}

	int16_t pitchCv(const BirdVoice &v)
	{
		if (v.currentHz == 0)
		{
			return 0;
		}
		const uint32_t lo = std::min(v.startHz, v.endHz);
		const uint32_t hi = std::max(v.startHz, v.endHz);
		const uint32_t span = maxu32(hi - lo, uint32_t(1));
		const uint32_t hz = clampu32(v.currentHz, lo, hi);
		const int32_t centered = (int32_t((hz - lo) * 4095u / span) - 2048);
		return int16_t(std::clamp(centered, int32_t(-2048), int32_t(2047)));
	}

	uint32_t applyLivePitchControl(uint32_t hz, uint16_t xVal)
	{
		return clampu32((hz * pitchScale[xVal]) >> 12, uint32_t(12), uint32_t(19000));
	}

	void updatePitchCv()
	{
		for (int i = 0; i < 2; ++i)
		{
			if (birds[i].currentHz != 0)
			{
				heldPitchCv[i] = pitchCv(birds[i]);
			}
		}
		CVOut1(heldPitchCv[0]);
		CVOut2(heldPitchCv[1]);
	}

	void updateLeds()
	{
		for (int i = 0; i < 2; ++i)
		{
			uint32_t top = 0;
			uint32_t bottom = 0;
			const BirdVoice &v = birds[i];
			if (v.currentHz > 0)
			{
				const uint32_t lo = std::min(v.startHz, v.endHz);
				const uint32_t hi = std::max(v.startHz, v.endHz);
				const uint32_t span = maxu32(hi - lo, uint32_t(1));
				const uint32_t hz = clampu32(v.currentHz, lo, hi);
				const uint32_t norm = ((hz - lo) * 4095u) / span;
				top = norm;
				bottom = 4095u - norm;
			}
			LedBrightness(uint32_t(i), uint16_t(top));
			LedBrightness(uint32_t(2 + i), uint16_t(bottom));
		}
		LedBrightness(4, pulseCount[0] > 0 ? 4095 : 0);
		LedBrightness(5, pulseCount[1] > 0 ? 4095 : 0);
	}

	void updatePulseOuts()
	{
		const bool p0 = pulseCount[0] > 0;
		const bool p1 = pulseCount[1] > 0;
		if (p0 != lastPulseOut[0])
		{
			lastPulseOut[0] = p0;
			PulseOut1(p0);
		}
		if (p1 != lastPulseOut[1])
		{
			lastPulseOut[1] = p1;
			PulseOut2(p1);
		}
	}
};

int main()
{
	set_sys_clock_khz(144000, true);

	BirdCard bc;
	bc.Run();
}
