#pragma once
#include <cmath>
#include <cstdint>
#include "Config.h"
#include "Tables.h"

namespace ab
{

// The piano: a pool of 12 voices, each three sines --
//
//   the fundamental,
//   a copy 3 cents sharp at half its level (the two beat slowly against
//     each other, like the slightly mistuned strings of one piano note;
//     quieter than the fundamental so the beat never cancels it fully), and
//   the octave, quieter, and dying away faster than the fundamental.
//
// A strike rises in 2ms and then decays exponentially, never held: there
// are no dampers. Decay times come from a table by pitch, built at
// power-on: about 16s for the lowest note (A2), 3s for the highest (G6).
//
// Rendering: envelopes are worked out once per block (multiply by the
// table's per-block decay) and stepped linearly across the block's 32
// samples, so each sample costs three table lookups and a few adds.
// When all 12 voices are busy, the quietest one is taken.
class Piano
{
public:
	static constexpr int kVoices = 12;

	// Decay per block for each MIDI note, Q31: the fundamental pair and the
	// octave. Filled by Init().
	static inline int32_t decayFund[128];
	static inline int32_t decayOct[128];

	static void Init()
	{
		for (int n = 0; n < 128; n++)
		{
			// T60 (time to fall 60dB): 16s at A2 (45) down to 3s at G6 (91),
			// evenly in log time; the octave dies 2.5x faster.
			float t = (float)(n - 45) / 46.0f;
			float t60 = 16.0f * powf(3.0f / 16.0f, t);
			decayFund[n] = PerBlock(t60);
			decayOct[n] = PerBlock(t60 / 2.5f);
		}
	}

	// A new note. levelQ12: its loudness (4096 = full).
	void NoteOn(int32_t note, int32_t loop, int32_t levelQ12)
	{
		int v = Free();
		Voice &p = voice_[v];
		p.inc[0] = Tables::noteInc[note];
		p.inc[1] = p.inc[0] + (uint32_t)(((uint64_t)p.inc[0] * kSharpQ24) >> 24);
		p.phase[0] = p.phase[1] = 0;
		p.decA = decayFund[note];
		p.decB = decayOct[note];
		// The octave: 40% of the fundamental at the bottom, 20% at the top.
		int32_t octLevel = 1638 - ((note - kLowNote) * 819) / 46;
		p.peakA = (kPeak >> 12) * levelQ12;
		p.peakB = ((kPeak >> 12) * octLevel >> 12) * levelQ12;
		p.envA = p.envB = 0;
		p.attack = kAttackBlocks;
		p.loop = loop;
		p.active = true;
	}

	// Silence every voice at once (a new piece, after its fade-out).
	void Clear()
	{
		for (int v = 0; v < kVoices; v++)
		{
			voice_[v].active = false;
			voice_[v].envA = voice_[v].envB = 0;
		}
	}

	// Add one block of every sounding voice into acc, panned by each
	// voice's loop (gains Q15). pitchQ16: tape wow and flutter, the
	// fractional change in pitch (Q16; 0 = none).
	void Render(int32_t *accL, int32_t *accR, const int32_t *gainL, const int32_t *gainR, int32_t pitchQ16)
	{
		for (int v = 0; v < kVoices; v++)
		{
			Voice &p = voice_[v];
			if (p.active)
				RenderVoice(p, accL, accR, gainL[p.loop], gainR[p.loop], pitchQ16);
		}
	}

	int32_t Active() const // for tests and the bench
	{
		int32_t n = 0;
		for (int v = 0; v < kVoices; v++)
			n += voice_[v].active ? 1 : 0;
		return n;
	}

private:
	static constexpr int32_t kLowNote = 45;
	static constexpr int32_t kAttackBlocks = 3; // 2ms
	// One voice at full level: the pair peaks at 1.5 x this, about a third
	// of full scale, so a few overlapping strikes still fit.
	static constexpr int32_t kPeak = 7300 << 12; // envelope: 16-bit level, Q12
	// 3 cents sharp: 2^(3/1200) - 1, Q24.
	static constexpr int32_t kSharpQ24 = 29084;
	// Below this the voice is silent (-70dB of a full strike): free it.
	static constexpr int32_t kSilent = kPeak >> 12;

	static int32_t PerBlock(float t60)
	{
		// 60dB = x 0.001 over t60 x 1500 blocks.
		float f = powf(0.001f, 1.0f / (t60 * (float)kBlocksPerSecond));
		return (int32_t)(f * 2147483647.0f);
	}

	struct Voice
	{
		uint32_t phase[2]; // the fundamental and its sharp copy (the octave
		uint32_t inc[2];   // follows the fundamental)
		int32_t envA, envB;	  // fundamental pair, octave: 16-bit level, Q12
		int32_t decA, decB;	  // per-block decay, Q31
		int32_t peakA, peakB;
		int32_t attack;		  // blocks of attack left
		int32_t loop;
		bool active;
	};

	// A free voice, or else the quietest.
	int Free() const
	{
		int quiet = 0;
		for (int v = 0; v < kVoices; v++)
		{
			if (!voice_[v].active)
				return v;
			if (voice_[v].envA < voice_[quiet].envA)
				quiet = v;
		}
		return quiet;
	}

	__attribute__((noinline)) static void RenderVoice(Voice &p, int32_t *accL, int32_t *accR, int32_t gl, int32_t gr,
													  int32_t pitchQ16)
	{
		// Where the envelopes will be at the end of this block.
		int32_t endA, endB;
		if (p.attack > 0)
		{
			p.attack--;
			endA = p.peakA - (p.peakA / kAttackBlocks) * p.attack;
			endB = p.peakB - (p.peakB / kAttackBlocks) * p.attack;
		}
		else
		{
			endA = (int32_t)(((int64_t)p.envA * p.decA) >> 31);
			endB = (int32_t)(((int64_t)p.envB * p.decB) >> 31);
			if (endA < kSilent)
			{
				p.active = false;
				return;
			}
		}
		int32_t stepA = (endA - p.envA) / kBlock;
		int32_t stepB = (endB - p.envB) / kBlock;

		// Tape: every partial bent by the same fraction.
		uint32_t inc0 = p.inc[0] + (uint32_t)((int32_t)(p.inc[0] >> 16) * pitchQ16);
		uint32_t inc1 = p.inc[1] + (uint32_t)((int32_t)(p.inc[1] >> 16) * pitchQ16);
		uint32_t ph0 = p.phase[0], ph1 = p.phase[1];
		int32_t envA = p.envA, envB = p.envB;

		// Two passes, because the Cortex-M0+ has few registers: first the
		// voice alone, then panned into the mix (measured in tools/bench:
		// one pass spilled registers and cost twice as much).
		int32_t mono[kBlock];
		for (int i = 0; i < kBlock; i++)
		{
			envA += stepA;
			envB += stepB;
			// The pair (1 + 1/2), then the octave. The octave runs at
			// exactly twice the fundamental's phase, and is read without
			// interpolation: it's quiet enough that the table's coarseness
			// is far below hearing.
			int32_t pair = Sine(ph0) + (Sine(ph1) >> 1);
			int32_t oct = Tables::sine[(ph0 >> (31 - Tables::kSineBits)) & (Tables::kSineSize - 1)];
			mono[i] = (pair * (envA >> 12) + oct * (envB >> 12)) >> 15;
			ph0 += inc0;
			ph1 += inc1;
		}
		for (int i = 0; i < kBlock; i++)
		{
			accL[i] += (mono[i] * gl) >> 15;
			accR[i] += (mono[i] * gr) >> 15;
		}
		p.phase[0] = ph0;
		p.phase[1] = ph1;
		p.envA = endA;
		p.envB = endB;
	}

	Voice voice_[kVoices] = {};
};

} // namespace ab
