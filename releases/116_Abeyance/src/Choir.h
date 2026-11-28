#pragma once
#include <cmath>
#include <cstdint>
#include "Config.h"
#include "Random.h"
#include "Tables.h"

namespace ab
{

// The choir: a pool of 8 voices (one per choir loop, plus a spare for notes
// that overlap), each three oscillators -- three singers on one note, 8
// cents flat, in tune and 7 cents sharp, each a further random 2 cents
// out -- reading single-cycle wavetables with sung vowel shapes built in,
// so the voice sounds sung without any live filters. The recipe is the
// web sketch's (the Abeyance page): three detuned sawtooths into three
// narrow vowel resonances and a low body.
//
// The wavetables
// --------------
// Built in RAM at power-up by adding sine waves together; nothing is made
// offline and nothing is read from flash while the card plays.
//
//   2 vowels x 5 tables, one every half octave, centred on G3, D-flat 4,
//   G4, D-flat 5 and G5: the choir's whole range in every piece, E3 to A5,
//   drift's octave jumps included. 1024 16-bit points each, 20KB in all.
//
// Each table sums the harmonics of its centre note, each weighted like a
// sawtooth (1/k) and then by what the web sketch's filters do to it: three
// band-passes at the vowel's resonances -- "ah" at 730, 1090 and 2440Hz,
// "oo" at 320, 870 and 2250Hz -- narrow (Q 8, 10 and 12), plus a low-pass
// at 2.2 x the note for the body of the voice, all four side by side. The
// filters' responses are worked out exactly (the same biquads a browser
// runs) and added with their phases, as the browser adds them, so the
// tables carry the same peaks and dips: it's the dips between resonances,
// as much as the peaks, that make a vowel rather than a filtered saw.
//
// Harmonics stop below 16kHz at the table's highest note (3 semitones
// above its centre), so nothing folds back into the audio: 68 harmonics in
// the lowest table, 17 in the highest. The weights are worked out once
// with floating point; the sums themselves are whole-number multiply-adds
// on the sine table. Each table is then scaled to a peak of 30,000.
//
// A note picks its table once, at note-on: the nearest centre, in the vowel
// chosen for that note. The resonances then sit at most 3 semitones from
// where they were rendered, which a choir hides well.
class Choir
{
public:
	static constexpr int kVoices = 8;
	static constexpr int kSingers = 3; // oscillators per voice
	static constexpr int kVowels = 2;  // ah, oo
	static constexpr int kCentres = 5;
	static constexpr int kTableBits = 10;
	static constexpr int kTableSize = 1 << kTableBits;
	static constexpr int32_t kCentre[kCentres] = {55, 61, 67, 73, 79}; // G3 .. G5

	// Plus a copy of the first point at the end, so interpolation never wraps.
	static inline int16_t table[kVowels][kCentres][kTableSize + 1];

	static void Init()
	{
		// Resonances: centre (Hz), Q, level -- the web sketch's.
		const float formant[kVowels][3][3] = {
			{{730, 8, 2.6f}, {1090, 10, 1.8f}, {2440, 12, 1.0f}}, // ah
			{{320, 8, 3.0f}, {870, 10, 1.6f}, {2250, 12, 0.8f}},  // oo
		};
		for (int v = 0; v < kVowels; v++)
		{
			for (int c = 0; c < kCentres; c++)
			{
				float f0 = 440.0f * exp2f((float)(kCentre[c] - 69) / 12.0f);
				float top = f0 * exp2f(3.0f / 12.0f);
				int harmonics = (int)(16000.0f / top);
				if (harmonics > kMaxHarmonics)
					harmonics = kMaxHarmonics;

				// The weights, largest scaled to 32767.
				float w[kMaxHarmonics + 1];
				float most = 0.0f;
				for (int k = 1; k <= harmonics; k++)
				{
					float f = f0 * (float)k;
					// The body: a low-pass at 2.2 x the note, at half level
					// (a browser's default Q of 1dB).
					Response body = LowPass(f, 2.2f * f0, 1.122f);
					float re = 0.5f * body.re, im = 0.5f * body.im;
					for (int r = 0; r < 3; r++)
					{
						Response b = BandPass(f, formant[v][r][0], formant[v][r][1]);
						re += formant[v][r][2] * b.re;
						im += formant[v][r][2] * b.im;
					}
					w[k] = sqrtf(re * re + im * im) / (float)k;
					most = w[k] > most ? w[k] : most;
				}
				int32_t wq[kMaxHarmonics + 1];
				for (int k = 1; k <= harmonics; k++)
					wq[k] = (int32_t)(32767.0f * w[k] / most);

				// The sums. Harmonic k starts at a phase that grows with k
				// squared (Schroeder's phases), which spreads the peaks of
				// the harmonics apart so the wave isn't spiky. (The ear
				// hardly hears these phases; it hears the levels.)
				int32_t sum[kTableSize];
				int32_t peak = 1;
				for (int i = 0; i < kTableSize; i++)
				{
					int32_t acc = 0;
					for (int k = 1; k <= harmonics; k++)
					{
						uint32_t at = (uint32_t)(k * i * (Tables::kSineSize / kTableSize)) +
									  (uint32_t)((k * k * Tables::kSineSize / 2) / harmonics);
						acc += (wq[k] * Tables::sine[at & (Tables::kSineSize - 1)]) >> 15;
					}
					sum[i] = acc;
					int32_t a = acc < 0 ? -acc : acc;
					peak = a > peak ? a : peak;
				}
				for (int i = 0; i < kTableSize; i++)
					table[v][c][i] = (int16_t)(((int64_t)sum[i] * 30000) / peak);
				table[v][c][kTableSize] = table[v][c][0];
			}
		}
	}

	// A new note. levelQ12: loudness. vowel: 0 ah, 1 oo. tempoQ12: the
	// piece's tempo; a faster piece sings shorter notes (times / the square
	// root of the tempo, as in the web sketch). Everything else -- each
	// singer's tuning and starting point, the note's length, its swell, its
	// vibrato rate -- is drawn from rng, so no two notes are alike.
	void NoteOn(int32_t note, int32_t loop, int32_t levelQ12, int32_t vowel, int32_t tempoQ12, Random &rng)
	{
		int v = Free(loop);
		Voice &c = voice_[v];
		if (c.active) // stealing: count it, and how loud it still was
		{
			steals_++;
			int32_t level = (int32_t)(((int64_t)c.env * 4096) / c.peak);
			loudestSteal_ = level > loudestSteal_ ? level : loudestSteal_;
		}
		int32_t t = (note - kCentre[0] + 3) / 6;
		t = t < 0 ? 0 : (t >= kCentres ? kCentres - 1 : t);
		c.table = table[vowel][t];
		uint32_t inc = Tables::noteInc[note];
		// -8, 0 and +7 cents, each +/-2 more. A cent is 2^(1/1200) - 1 =
		// 0.000578 of the pitch: 379 per 10,000 cents x 100, Q16.
		static constexpr int32_t kCents[kSingers] = {-800, 0, 700}; // hundredths of a cent
		for (int i = 0; i < kSingers; i++)
		{
			int32_t cents = kCents[i] + rng.Between(-200, 200);
			c.inc[i] = inc + (uint32_t)((int32_t)(inc >> 16) * ((cents * 379) / 1000));
			c.phase[i] = rng.Next();
		}

		// Timings, at tempo 1: a straight swell of 2.4-3.4s up to the full
		// level, held until 4.5s before the note's end (9-12s after it
		// started), then a fade that falls by e (8.7dB) every 1.1s.
		int32_t scale = Isqrt((uint32_t)(((int64_t)1 << 36) / tempoQ12)); // sqrt(1 / tempo), Q12
		auto Scaled = [scale](int32_t blocks) { return (int32_t)(((int64_t)blocks * scale) >> 12); };
		c.swell = Scaled(kSwellBlocks + rng.Below(kSwellSpreadBlocks));
		c.fadeAt = Scaled(kLengthBlocks + rng.Below(kLengthSpreadBlocks) - kFadeLeadBlocks);
		int32_t tau = Scaled(kFadeTauBlocks);
		c.fadeQ30 = (1 << 30) - (1 << 30) / tau;
		c.peak = (kPeak >> 12) * levelQ12;

		// Vibrato: 4.6-5.2Hz, growing from nothing at the start of the note
		// to +/-5 cents 1.5s after the swell tops out, as a held sung note's
		// does.
		c.vibratoInc = kVibratoInc + (uint32_t)rng.Below(kVibratoSpread >> 8) * 256u;
		c.vibratoPhase = rng.Next();
		c.vibratoGrow = c.swell + kVibratoLateBlocks;

		c.age = 0;
		c.env = 0;
		c.loop = loop;
		c.active = true;
	}

	// Silence every voice at once (a new piece, after its fade-out).
	void Clear()
	{
		for (int v = 0; v < kVoices; v++)
		{
			voice_[v].active = false;
			voice_[v].env = 0;
		}
	}

	// Add one block of every sounding voice into acc, panned by each
	// voice's loop. pitchQ16: tape wow and flutter (see Piano::Render).
	void Render(int32_t *accL, int32_t *accR, const int32_t *gainL, const int32_t *gainR, int32_t pitchQ16)
	{
		for (int v = 0; v < kVoices; v++)
		{
			Voice &c = voice_[v];
			if (c.active)
				RenderVoice(c, accL, accR, gainL[c.loop], gainR[c.loop], pitchQ16);
		}
	}

	// For the middle LEDs: how loud the choir is on each side, 0..4095,
	// from each voice's envelope and its loop's pan (gains Q15).
	void Loudness(const int32_t *gainL, const int32_t *gainR, int32_t out[2]) const
	{
		int64_t l = 0, r = 0;
		for (int v = 0; v < kVoices; v++)
		{
			const Voice &c = voice_[v];
			if (!c.active)
				continue;
			int32_t e = (int32_t)(((int64_t)c.env * 4096) / kPeak); // 0..4096
			l += (int64_t)e * gainL[c.loop] * gainL[c.loop] >> 30;
			r += (int64_t)e * gainR[c.loop] * gainR[c.loop] >> 30;
		}
		// Two full voices on one side light it fully.
		out[0] = (int32_t)(l * 2 > 4095 ? 4095 : l * 2);
		out[1] = (int32_t)(r * 2 > 4095 ? 4095 : r * 2);
	}

	uint32_t Steals() const { return steals_; }			   // for tests
	int32_t LoudestSteal() const { return loudestSteal_; } // of 4096, for tests

	int32_t Active() const // for tests
	{
		int32_t n = 0;
		for (int v = 0; v < kVoices; v++)
			n += voice_[v].active ? 1 : 0;
		return n;
	}

private:
	static constexpr int kMaxHarmonics = 80;
	// One voice at full level (all three singers together), 16-bit scale.
	// Set so the choir layer, over a long run, is as loud as the piano.
	static constexpr int32_t kPeak = 6500 << 12;
	// The shape of a sung note, at tempo 1 (the web sketch's): a straight
	// swell of 2.4s plus up to 1s, a note 9s plus up to 3s long, its fade
	// starting 4.5s before that end and falling by e every 1.125s, until
	// it's 60dB down and the voice is freed.
	static constexpr int32_t kSwellBlocks = BlocksFromMs(2400);
	static constexpr int32_t kSwellSpreadBlocks = BlocksFromMs(1000);
	static constexpr int32_t kLengthBlocks = BlocksFromMs(9000);
	static constexpr int32_t kLengthSpreadBlocks = BlocksFromMs(3000);
	static constexpr int32_t kFadeLeadBlocks = BlocksFromMs(4500);
	static constexpr int32_t kFadeTauBlocks = BlocksFromMs(1125);
	// Vibrato reaches its full depth this long after the swell tops out.
	static constexpr int32_t kVibratoLateBlocks = BlocksFromMs(1500);
	// 4.6Hz, plus up to 0.6Hz; +/-5 cents (2^(5/1200) - 1 = 0.00289, Q16).
	static constexpr uint32_t kVibratoInc = (uint32_t)(4294967296.0 * 4.6 / kBlocksPerSecond);
	static constexpr uint32_t kVibratoSpread = (uint32_t)(4294967296.0 * 0.6 / kBlocksPerSecond);
	static constexpr int32_t kVibratoDepthQ16 = 189;

	struct Voice
	{
		const int16_t *table;
		uint32_t phase[kSingers];
		uint32_t inc[kSingers];
		int32_t env; // 16-bit level, Q12
		int32_t peak;
		int32_t age; // blocks since note-on
		int32_t swell, fadeAt, vibratoGrow; // blocks
		int32_t fadeQ30; // the fade's fall per block
		uint32_t vibratoPhase, vibratoInc;
		int32_t loop;
		bool active;
	};

	// The frequency response of the biquad filters a browser runs (Robert
	// Bristow-Johnson's, at 48kHz), at f Hz, as a complex number.
	struct Response
	{
		float re, im;
	};
	static Response Biquad(float f, float b0, float b1, float b2, float a0, float a1, float a2)
	{
		float w = 6.2831853f * f / (float)kSampleRate;
		float c1 = cosf(w), s1 = sinf(w), c2 = cosf(2 * w), s2 = sinf(2 * w);
		// (b0 + b1 e^-jw + b2 e^-2jw) / (a0 + a1 e^-jw + a2 e^-2jw)
		float nr = b0 + b1 * c1 + b2 * c2, ni = -b1 * s1 - b2 * s2;
		float dr = a0 + a1 * c1 + a2 * c2, di = -a1 * s1 - a2 * s2;
		float d = dr * dr + di * di;
		return {(nr * dr + ni * di) / d, (ni * dr - nr * di) / d};
	}
	static Response BandPass(float f, float fc, float q)
	{
		float w0 = 6.2831853f * fc / (float)kSampleRate, alpha = sinf(w0) / (2 * q);
		return Biquad(f, alpha, 0, -alpha, 1 + alpha, -2 * cosf(w0), 1 - alpha);
	}
	static Response LowPass(float f, float fc, float q)
	{
		float w0 = 6.2831853f * fc / (float)kSampleRate, alpha = sinf(w0) / (2 * q), c = cosf(w0);
		return Biquad(f, (1 - c) / 2, 1 - c, (1 - c) / 2, 1 + alpha, -2 * c, 1 - alpha);
	}

	// Whole-number square root (once per note).
	static int32_t Isqrt(uint32_t x)
	{
		uint32_t r = 0, bit = 1u << 30;
		while (bit > x)
			bit >>= 2;
		while (bit)
		{
			if (x >= r + bit)
			{
				x -= r + bit;
				r = (r >> 1) + bit;
			}
			else
				r >>= 1;
			bit >>= 2;
		}
		return (int32_t)r;
	}

	// A free voice; else one of this loop's own (it's had its turn); else
	// the quietest.
	int Free(int32_t loop) const
	{
		int quiet = 0, own = -1;
		for (int v = 0; v < kVoices; v++)
		{
			if (!voice_[v].active)
				return v;
			if (voice_[v].loop == loop)
				own = v;
			if (voice_[v].env < voice_[quiet].env)
				quiet = v;
		}
		return own >= 0 ? own : quiet;
	}

	__attribute__((noinline)) static void RenderVoice(Voice &c, int32_t *accL, int32_t *accR, int32_t gl, int32_t gr,
													  int32_t pitchQ16)
	{
		// Where the envelope will be at the end of this block.
		c.age++;
		int32_t end;
		bool last = false;
		if (c.age < c.swell)
			end = (int32_t)(((int64_t)c.peak * c.age) / c.swell);
		else if (c.age < c.fadeAt)
			end = c.peak;
		else
		{
			end = (int32_t)(((int64_t)c.env * c.fadeQ30) >> 30);
			if (end < (c.peak >> 10)) // 60dB down: ramp to nothing and stop
			{
				end = 0;
				last = true;
			}
		}
		int32_t step = (end - c.env) / kBlock;

		// Vibrato, growing to its full depth, added to the tape's bend.
		int32_t grow = c.age >= c.vibratoGrow ? 4096 : (c.age * 4096) / c.vibratoGrow;
		int32_t bend = pitchQ16 + ((((Sine(c.vibratoPhase) * kVibratoDepthQ16) >> 15) * grow) >> 12);
		c.vibratoPhase += c.vibratoInc;
		uint32_t inc0 = c.inc[0] + (uint32_t)((int32_t)(c.inc[0] >> 16) * bend);
		uint32_t inc1 = c.inc[1] + (uint32_t)((int32_t)(c.inc[1] >> 16) * bend);
		uint32_t inc2 = c.inc[2] + (uint32_t)((int32_t)(c.inc[2] >> 16) * bend);

		// Two passes, as in Piano: the voice alone, then panned.
		const int16_t *t = c.table;
		uint32_t ph0 = c.phase[0], ph1 = c.phase[1], ph2 = c.phase[2];
		int32_t env = c.env;
		int32_t mono[kBlock];
		for (int i = 0; i < kBlock; i++)
		{
			env += step;
			int32_t s = Read(t, ph0) + Read(t, ph1) + Read(t, ph2);
			mono[i] = (s * (env >> 12)) >> 16;
			ph0 += inc0;
			ph1 += inc1;
			ph2 += inc2;
		}
		for (int i = 0; i < kBlock; i++)
		{
			accL[i] += (mono[i] * gl) >> 15;
			accR[i] += (mono[i] * gr) >> 15;
		}
		c.phase[0] = ph0;
		c.phase[1] = ph1;
		c.phase[2] = ph2;
		c.env = end;
		if (last)
			c.active = false;
	}

	// One table read at a phase, between two neighbouring points.
	__attribute__((always_inline)) static int32_t Read(const int16_t *t, uint32_t ph)
	{
		uint32_t i = ph >> (32 - kTableBits);
		int32_t f = (int32_t)((ph >> (16 - kTableBits)) & 0xFFFF);
		int32_t a = t[i];
		return a + (((t[i + 1] - a) * f) >> 16);
	}

	Voice voice_[kVoices] = {};
	uint32_t steals_ = 0;
	int32_t loudestSteal_ = 0;
};

} // namespace ab
