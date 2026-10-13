#pragma once
#include <cstdint>
#include "Config.h"
#include "Params.h"
#include "Tables.h"

namespace eq
{

// The melody: eight two-operator FM voices, on core 0.
//
// Each voice is two sine oscillators. The modulator wobbles the phase of
// the carrier (the note itself); how hard it wobbles is the "modulation
// index". A big index spreads the sound into many partials -- bright. A
// small one leaves little more than a sine. The ratio between the two
// frequencies decides which partials: whole numbers for marimba, harp or
// vibraphone, in-between ratios for the metallic glockenspiel and bell.
// The index dies away faster than the volume, so each strike starts bright
// and mellows as it rings. Each strike brings its tone's settings with it
// (see Config.h and Notes.h).
//
// Envelopes are worked out once every 8 samples per voice (one voice each
// sample, in turn, so the work is spread evenly), then glide in a straight
// line to the next value sample by sample. Exponential decays need a
// multiply each time; doing that every 8th sample instead of every sample
// leaves more time for the oscillators.
class BellBank
{
public:
	// New strikes from core 1. Only called when params.strikeEpoch changes.
	void Accept(const EngineParams &p)
	{
		for (int v = 0; v < kVoices; v++)
		{
			const VoiceCmd &cmd = p.voice[v];
			if (cmd.seq != voice_[v].seq)
			{
				voice_[v].seq = cmd.seq;
				Strike(voice_[v], cmd);
			}
		}
	}

	// One sample of all eight voices, summed. Full scale is +/-32768 (16
	// times the DAC's range, for headroom and precision in the delays).
	__attribute__((noinline)) int32_t Process()
	{
		int32_t sum = 0;
		for (int v = 0; v < kVoices; v++)
		{
			Voice &o = voice_[v];
			if (o.stage == kIdle)
				continue;
			// Modulator, then the carrier with its phase pushed around by
			// the modulator. The index is in phase units per unit of
			// modulator output; the multiply wraps round the 32-bit phase
			// circle, which is exactly what a phase offset should do.
			int32_t m = Sine(o.modPhase);
			o.modPhase += o.modInc;
			uint32_t deviation = (uint32_t)m * (uint32_t)(o.index >> 8);
			int32_t c = Sine(o.carPhase + deviation);
			o.carPhase += o.carInc;
			int32_t x = (c * (o.amp >> 9)) >> 15;
			// Gentle one-pole lowpass: softer for notes born at low tide.
			o.lp += ((x - o.lp) * o.lpCoef + 16384) >> 15; // rounded, so no DC creeps in
			sum += o.lp;
			o.amp += o.ampStep;
			o.index += o.indexStep;
		}
		UpdateEnvelope(voice_[nextEnvelope_]);
		nextEnvelope_ = (nextEnvelope_ + 1) & (kVoices - 1);
		return sum;
	}

	int32_t Amp(int v) const { return voice_[v].amp; }

private:
	static constexpr int32_t kIdle = 0, kAttack = 1, kDecay = 2, kRelease = 3;
	// Below this (about -72dB from full scale) a voice falls silent.
	static constexpr int32_t kSilent = 1 << 12;

	struct Voice
	{
		// Every sample (kept together at the front: on this chip, fields a
		// short reach from the start of a structure are cheaper to load).
		uint32_t carPhase, carInc, modPhase, modInc;
		int32_t amp, ampStep;	  // envelope units: 1 << 24 is full scale
		int32_t index, indexStep; // envelope units
		int32_t lp, lpCoef;
		int32_t stage;
		// Every 8 samples.
		int32_t peakAmp, attackInc, ampDecay, indexDecay;
		uint32_t seq;
		VoiceCmd pending; // a strike waiting for the old note to fade
	};

	void Strike(Voice &o, const VoiceCmd &cmd)
	{
		// A different note (or tone) while the old one still rings: fade the
		// old one out quickly first (about 5ms), so it doesn't click. A
		// repeat of the same note just strikes again from wherever it has
		// decayed to.
		if (o.stage != kIdle && (cmd.carInc != o.carInc || cmd.modInc != o.modInc))
		{
			o.pending = cmd;
			o.stage = kRelease;
			return;
		}
		Begin(o, cmd);
	}

	static void Begin(Voice &o, const VoiceCmd &cmd)
	{
		o.carInc = cmd.carInc;
		o.modInc = cmd.modInc;
		o.peakAmp = cmd.peakAmp;
		o.attackInc = cmd.attackInc;
		o.ampDecay = cmd.ampDecay;
		o.indexDecay = cmd.indexDecay;
		o.lpCoef = cmd.lpCoef;
		o.index = cmd.peakIndex;
		o.indexStep = 0;
		o.stage = kAttack;
	}

	// Every 8 samples, per voice: where should the envelopes be 8 samples
	// from now? Then glide there.
	static void UpdateEnvelope(Voice &o)
	{
		int32_t target;
		switch (o.stage)
		{
		case kIdle:
			return;
		case kAttack: // a straight climb, over the tone's attack time
			target = o.amp + o.attackInc;
			if (target >= o.peakAmp)
			{
				target = o.peakAmp;
				o.stage = kDecay;
			}
			break;
		case kDecay: // exponential: lose the same fraction every 8 samples
			// (+1: at least one unit, or a quiet voice's step rounds down to
			// nothing and it hangs just above silence forever)
			target = o.amp - (((o.amp >> 8) * o.ampDecay) >> 12) - 1;
			if (target < kSilent)
			{
				Silence(o);
				return;
			}
			break;
		default: // kRelease: lose a quarter every 8 samples, then the new note
			target = o.amp - (o.amp >> 2);
			if (target < kSilent)
			{
				Silence(o);
				Begin(o, o.pending);
				return;
			}
			break;
		}
		o.ampStep = (target - o.amp) >> 3;
		// (Shifted differently from the volume above: the fastest tones lose
		// their brightness in 30ms, and their bigger decay figure would
		// overflow 32 bits with the volume's shifts.)
		int32_t indexTarget = o.index - (((o.index >> 12) * o.indexDecay) >> 8);
		o.indexStep = (indexTarget - o.index) >> 3;
	}

	static void Silence(Voice &o)
	{
		o.stage = kIdle;
		o.amp = o.ampStep = 0;
		o.indexStep = 0;
		o.lp = 0;
	}

	int32_t nextEnvelope_ = 0;
	Voice voice_[kVoices] = {};
};

} // namespace eq
