// Weights: the musical engine, with no hardware in it.
//
// Two voices of random pitch CV, a gate and an envelope each.  At every step
// each voice draws a note of the scale, an octave, a gate length and a
// ratchet, each one weighted by a page of eight faders, and plays them.
//
// Everything here is plain integer C++ so that it runs unchanged in the
// Computer's audio interrupt and in the tests on a PC (test/engine_test.cpp).
// main.cpp feeds it the clock input once a sample and reads its outputs.

#ifndef WEIGHTS_ENGINE_H
#define WEIGHTS_ENGINE_H

#include <stdint.h>
#include <math.h>

namespace weights
{

static constexpr int32_t kSampleRate = 48000;
static constexpr int kFaders = 8;
static constexpr int kVoices = 2;

// The kinds of page.  Each has a page shared by both voices, and a page of
// voice 2's own, used once any of its faders is up.
//   Button A  NOTES (page 0)     NOTES 2 (4)     ATTACK (8)   ATTACK 2 (12)
//   Button B  OCTAVES (1)        OCTAVES 2 (5)   DECAY (9)    DECAY 2 (13)
//   Button C  GATES (2)          GATES 2 (6)     PLUCK (10)   PLUCK 2 (14)
//   Button D  RATCHETS (3)       RATCHETS 2 (7)  DEPTH (11)   DEPTH 2 (15)
// The page numbers are the protocol's (sysex.h), so the envelope pages,
// added later, come last.
enum Kind {Notes, Octaves, Gates, Ratchets, Attack, Decay, Pluck, Depth, kKinds};
static constexpr int kPairs = 4;      // kinds on pages 0-7 (first + 4 = second)
static constexpr int kEnvKinds = 4;   // ATTACK, DECAY, PLUCK, DEPTH on pages 8-15
static constexpr int kPages = 2 * kPairs + 2 * kEnvKinds;

// Pages are layer * 4 + button: each button cycles through four layers, its
// first page, voice 2's page, its envelope page and voice 2's envelope page.
static constexpr int kLayers = 4;
static constexpr int ButtonOf(int page) {return page & 3;}
static constexpr int LayerOf(int page) {return page >> 2;}
static constexpr int KindOf(int page) {return (page & 3) + (page >= 2 * kPairs ? kPairs : 0);}
// The voice whose page it is: 0 for a first page (both voices), 1 for voice 2's
static constexpr int VoiceOfPage(int page) {return (page >> 2) & 1;}
static constexpr int FirstPage(int kind) {return kind < kPairs ? kind : kind + kPairs;}
static constexpr int SecondPage(int kind) {return FirstPage(kind) + 4;}

// A fader this low (of 127) counts as zero: never chosen.  8mu faders don't
// always read exactly 0 at the bottom, and a second page only hands voice 2
// back to the first page when every fader on it is at zero.
static constexpr int kZeroFader = 2;
static inline int Weight(int faderValue) {return faderValue <= kZeroFader ? 0 : faderValue;}

//----------------------------------------------------------------------------
// Scales
//
// Each scale is its notes within one octave, in cents above the root (C at
// 0V).  An equal temperament is just its number of steps (tet).  Faders 1-8
// on the NOTES page are the scale's notes, one each, from the root up; a
// scale with fewer than eight ignores the extra faders.  (No scale has more
// than eight, so every note has a fader of its own.)
//----------------------------------------------------------------------------
struct Scale
{
	const char *name;
	uint8_t n;          // notes per octave
	uint8_t tet;        // equal steps per octave, or 0 to use cents[]
	int16_t cents[8];
};

static constexpr int kNumScales = 37;
static const Scale kScales[kNumScales] = {
	{"Major",           7, 0, {0, 200, 400, 500, 700, 900, 1100}},
	{"Natural minor",   7, 0, {0, 200, 300, 500, 700, 800, 1000}},
	{"Harmonic minor",  7, 0, {0, 200, 300, 500, 700, 800, 1100}},
	{"Melodic minor",   7, 0, {0, 200, 300, 500, 700, 900, 1100}},
	{"Dorian",          7, 0, {0, 200, 300, 500, 700, 900, 1000}},
	{"Phrygian",        7, 0, {0, 100, 300, 500, 700, 800, 1000}},
	{"Lydian",          7, 0, {0, 200, 400, 600, 700, 900, 1100}},
	{"Mixolydian",      7, 0, {0, 200, 400, 500, 700, 900, 1000}},
	{"Locrian",         7, 0, {0, 100, 300, 500, 600, 800, 1000}},
	{"Major pentatonic", 5, 0, {0, 200, 400, 700, 900}},
	{"Minor pentatonic", 5, 0, {0, 300, 500, 700, 1000}},
	{"Blues",           6, 0, {0, 300, 500, 600, 700, 1000}},
	{"Whole tone",      6, 0, {0, 200, 400, 600, 800, 1000}},
	{"Diminished",      8, 0, {0, 200, 300, 500, 600, 800, 900, 1100}},
	{"Bebop dominant",  8, 0, {0, 200, 400, 500, 700, 900, 1000, 1100}},
	{"Hirajoshi",       5, 0, {0, 200, 300, 700, 800}},
	{"Fifths",          2, 0, {0, 700}},
	{"Octaves",         1, 0, {0}},
	// Indian ragas, in just intonation (pure ratios, as they're usually
	// sung).  Todi's flat third is the low 32/27; Marwa has no fifth.
	{"Raga Bhairav",    7, 0, {0, 112, 386, 498, 702, 814, 1088}},
	{"Raga Todi",       7, 0, {0, 112, 294, 590, 702, 814, 1088}},
	{"Raga Marwa",      6, 0, {0, 112, 386, 590, 884, 1088}},
	{"Raga Malkauns",   5, 0, {0, 316, 498, 814, 996}},
	{"Raga Hindol",     5, 0, {0, 386, 590, 884, 1088}},
	// Arabic maqamat: Rast, Bayati and Saba with their neutral (quarter-tone)
	// steps; Hijaz in equal semitones
	{"Maqam Rast",      7, 0, {0, 200, 350, 500, 700, 900, 1050}},
	{"Maqam Bayati",    7, 0, {0, 150, 300, 500, 700, 800, 1000}},
	{"Maqam Hijaz",     7, 0, {0, 100, 400, 500, 700, 800, 1000}},
	{"Maqam Saba",      7, 0, {0, 150, 300, 400, 700, 800, 1000}},
	// Just intonation: pure ratios rather than equal semitones
	{"Just major",      7, 0, {0, 204, 386, 498, 702, 884, 1088}},
	{"Just minor",      7, 0, {0, 204, 316, 498, 702, 814, 1018}},
	// Harmonics 8 to 15 of the root, folded into one octave, and its mirror:
	// the root's subharmonics 16 down to 9
	{"Harmonic series", 8, 0, {0, 204, 386, 551, 702, 841, 969, 1088}},
	{"Subharmonic series", 8, 0, {0, 112, 231, 359, 498, 649, 814, 996}},
	// Gamelan tunings vary from orchestra to orchestra; these are typical
	{"Slendro",         5, 0, {0, 231, 474, 717, 955}},
	{"Pelog",           7, 0, {0, 120, 258, 539, 675, 785, 943}},
	{"Pelog selisir",   5, 0, {0, 120, 258, 675, 785}},
	// Equal temperaments: the octave in 5, 7 or 8 equal steps
	{"5-TET",           5, 5, {}},
	{"7-TET",           7, 7, {}},
	{"8-TET",           8, 8, {}},
};

// The notes fader f covers: count of them from first (count 0 = ignored)
static inline void FaderNotes(const Scale &s, int f, int &first, int &count)
{
	if (s.n <= kFaders)
	{
		first = f;
		count = f < s.n ? 1 : 0;
	}
	else
	{
		first = (f * s.n) / kFaders;
		count = ((f + 1) * s.n) / kFaders - first;
	}
}

// Which faders mean anything on the NOTES page, as a bit mask
static inline uint8_t ActiveNoteFaders(const Scale &s)
{
	return s.n >= kFaders ? 0xFF : uint8_t((1 << s.n) - 1);
}

// The fader a note of the scale belongs to
static inline int FaderOfNote(const Scale &s, int note)
{
	if (s.n <= kFaders) return note;
	for (int f = kFaders - 1; f > 0; f--)
		if (note >= (f * s.n) / kFaders) return f;
	return 0;
}

static inline int32_t NoteCents(const Scale &s, int note)
{
	if (s.tet) return (note * 1200 + s.tet / 2) / s.tet;
	return s.cents[note];
}

// OCTAVES page: fader 1 is -2V, fader 8 +5V.  Fader 3 (0V) is the octave of
// the root at C, so with the highest note of a scale on fader 8 the output
// reaches nearly +6V, the most the CV outputs give.
static constexpr int kLowestOctave = -2;

// A note in millivolts, 1V/oct: 1000mV an octave, so a cent is 5/6 mV
static inline int32_t NoteMillivolts(const Scale &s, int note, int octaveFader)
{
	return (octaveFader + kLowestOctave) * 1000 + (NoteCents(s, note) * 5 + 3) / 6;
}

//----------------------------------------------------------------------------
// Ratchets
//
// RATCHETS faders 1-4 play a step's note 1x, 1.5x, 2x or 3x; faders 5-8
// play 1.5x, 2x, 3x or 4x, with each hit drawing a new note, octave and gate
// (a single hit with a new note would be the same as fader 1).  The hits are
// at these fractions of the step: 1.5x is two hits, the second two
// thirds of the way through, so it pushes against the beat.
//----------------------------------------------------------------------------
enum Speed {Once, OneAndHalf, Twice, Thrice, FourTimes, kSpeeds};
static constexpr int kMaxHits = 4;
static constexpr int kHits[kSpeeds] = {1, 2, 2, 3, 4};
// Where the hits fall, in twelfths of the step (so the sums stay in 32 bits)
static constexpr int32_t kHitTwelfths[kSpeeds][kMaxHits] = {
	{0, 0, 0, 0}, {0, 8, 0, 0}, {0, 6, 0, 0}, {0, 4, 8, 0}, {0, 3, 6, 9}};
// The speed of each RATCHETS fader
static constexpr int8_t kFaderSpeed[8] = {Once, OneAndHalf, Twice, Thrice, OneAndHalf, Twice, Thrice, FourTimes};
static inline int SpeedOf(int ratchetFader) {return kFaderSpeed[ratchetFader & 7];}
static inline bool NewNoteEachHit(int ratchetFader) {return ratchetFader >= 4;}

// ATTACK, DECAY and PLUCK pages: how far each hit's envelope strays from the
// knobs.  The faders are a spread from much less (fader 1) to much more
// (fader 8), with faders 4 and 5 a little either side.  Attack and decay
// times go from a quarter to four times the knob's (in 1/4096 octave: +-2
// octaves); pluckiness from half its range less to half its range more.
// A page all at zero means no deviation at all: the knobs' envelope.
// DEPTH pages: the envelope's peak, fader 1 an eighth of full (about 0.6V)
// up to fader 8 full (5V), for accents; all at zero, every envelope is full.
static constexpr int32_t kDevOctaves[kFaders] = {-8192, -5851, -3511, -1170, 1170, 3511, 5851, 8192};
static constexpr int32_t kDevPluck[kFaders] = {-2048, -1463, -878, -293, 293, 878, 1463, 2048};

// GATES page: fader 1 is a rest (no gate, no envelope), faders 2-7 are 1/7
// to 6/7 of the time to the next hit, and fader 8 is tied: the gate stays up
// into the next note, which then doesn't retrigger the envelope.
static constexpr int kRestFader = 0;
static constexpr int kTiedFader = 7;
// Gates fall at least this long before the next hit, so a new note always
// gives a fresh rising edge: 1ms
static constexpr int32_t kGateGap = 48;

//----------------------------------------------------------------------------
// Small helpers
//----------------------------------------------------------------------------

// A simple, fast random number generator (xorshift32)
static inline uint32_t NextRandom(uint32_t &s)
{
	s ^= s << 13;
	s ^= s >> 17;
	s ^= s << 5;
	return s;
}

// A random number from 0 to n-1, from a 32-bit random number, without a
// division
static inline int Below(uint32_t r, uint32_t n) {return int((uint64_t(r) * n) >> 32);}

// Weighted choice: fader i is chosen with a chance proportional to its
// value.  Only faders in the active mask can be chosen; if they are all at
// zero, each of them is equally likely.
static inline int Choose(const volatile uint8_t *faders, uint8_t active, uint32_t r)
{
	int32_t w[kFaders];
	uint32_t sum = 0;
	int count = 0;
	for (int i = 0; i < kFaders; i++)
	{
		w[i] = (active >> i) & 1 ? Weight(faders[i]) : 0;
		sum += uint32_t(w[i]);
		if ((active >> i) & 1) count++;
	}
	if (count == 0) return 0;
	if (sum == 0)
	{
		int k = Below(r, uint32_t(count));
		for (int i = 0; i < kFaders; i++)
			if ((active >> i) & 1 && k-- == 0) return i;
		return 0;
	}
	int32_t t = int32_t((uint64_t(r) * sum) >> 32);
	for (int i = 0; i < kFaders; i++)
	{
		if (t < w[i]) return i;
		t -= w[i];
	}
	return kFaders - 1; // not reached
}

// 2^(i/256) in Q30, for ExpScale
struct ExpTable
{
	uint32_t t[257];
	ExpTable()
	{
		for (int i = 0; i <= 256; i++) t[i] = uint32_t(1073741824.0f * exp2f(float(i) / 256.0f));
	}
};
static const ExpTable gExp;

// base * 2^(oct/4096): exponential controls (times, rates) without floats
static inline uint32_t ExpScale(uint32_t base, int32_t oct)
{
	int32_t whole = oct >> 12;
	int32_t frac = oct & 4095;
	int i = frac >> 4, r = frac & 15;
	uint32_t m = gExp.t[i] + (((gExp.t[i + 1] - gExp.t[i]) * uint32_t(r)) >> 4);
	uint64_t v = (uint64_t(base) * m) >> 30;
	if (whole >= 0)
	{
		if (whole > 31) return 0xFFFFFFFF;
		v <<= whole;
	}
	else
	{
		if (whole < -31) return 0;
		v >>= -whole;
	}
	return v > 0xFFFFFFFFull ? 0xFFFFFFFF : uint32_t(v);
}

//----------------------------------------------------------------------------
// Knobs
//
// The Computer's knobs don't always reach the very ends of their travel, so
// the outer kKnobEndZone at each end reads as fully 0 or 4095 and the rest is
// stretched to fill the range.  Every setting below takes the mapped value.
//----------------------------------------------------------------------------
static constexpr int32_t kKnobEndZone = 128;
static inline int32_t MapKnob(int32_t raw)
{
	int32_t v = ((raw - kKnobEndZone) * 4095) / (4095 - 2 * kKnobEndZone);
	return v < 0 ? 0 : (v > 4095 ? 4095 : v);
}

// Clock divide and multiply, in eleven equal zones round the knob, x1 in the
// middle.  A step lasts one beat: the clock divided by den, or multiplied by
// num.
struct Ratio {int8_t num, den;};
static constexpr int kNumRatios = 11;
static constexpr Ratio kRatios[kNumRatios] = {
	{1, 8}, {1, 6}, {1, 4}, {1, 3}, {1, 2}, {1, 1}, {2, 1}, {3, 1}, {4, 1}, {6, 1}, {8, 1}};
static constexpr int kUnityRatio = 5;

static inline int ScaleFromKnob(int32_t v) {return (v * kNumScales) >> 12;}
static inline int RatioFromKnob(int32_t v) {return (v * kNumRatios) >> 12;}
// The knob value in the middle of a zone, for defaults
static inline int32_t KnobForZone(int zone, int zones) {return (zone * 4096 + 2048) / zones;}

// Slew on the pitch CV, two-sided: off in a zone round the middle of the
// knob (so off is easy to find), then a time constant from 1ms just outside
// it to about 2s at either end.  Turned right, every note slews; turned
// left, only a note that a tied gate leads into does, so the ties glide and
// the rest jump.  Returned as the fraction of the way the output moves
// towards the note each sample, in Q24; 1<<24 means no slew.
static constexpr int32_t kSlewCentreZone = 96;
static inline int32_t SlewFromKnob(int32_t v)
{
	int32_t d = v - 2048;
	if (d < 0) d = -d;
	int32_t s = d - kSlewCentreZone;
	if (s < 0) return 1 << 24;
	// 1/(1ms at 48kHz) = 2^24/48, falling 11 octaves (to ~2s) by the end
	int32_t a = int32_t(ExpScale(349525, -(s * 45056) / (2047 - kSlewCentreZone)));
	return a < 1 ? 1 : a;
}
static inline bool SlewTiesOnly(int32_t v) {return v < 2048;}

// Envelope times, as the amount an envelope stage moves on each sample
// (Q31, 2^31 being the whole stage).  Attack 0.5ms to 2s; decay 10ms to 4s.
static inline uint32_t AttackFromKnob(int32_t v)
{
	return ExpScale(89478485, -(v * 49012) / 4095);   // 2^31 / 24 samples
}
static inline uint32_t DecayFromKnob(int32_t v)
{
	return ExpScale(4473924, -(v * 35405) / 4095);    // 2^31 / 480 samples
}

// Internal clock when nothing is plugged into Pulse In 1: 40-300 BPM, set by
// Main with the switch held down, or by the web editor
static constexpr int32_t kMinBpm = 40, kMaxBpm = 300;
static inline int32_t ClampBpm(int32_t bpm) {return bpm < kMinBpm ? kMinBpm : (bpm > kMaxBpm ? kMaxBpm : bpm);}
// The knob turns the tempo exponentially, the same ratio for every bit of
// turn: 40 BPM fully down, about 110 in the middle, 300 fully up
// (300/40 = 7.5 = 2^2.907, and 2.907 octaves is 11908 in 1/4096 octave)
static inline int32_t BpmFromKnob(int32_t v)
{
	return ClampBpm(int32_t((ExpScale(40 << 16, (v * 11908) / 4095) + 32768) >> 16));
}
// The knob position for a tempo, for picking it up
static inline int32_t KnobFromBpm(int32_t bpm)
{
	bpm = ClampBpm(bpm);
	int32_t v = int32_t((log2f(float(bpm) / 40.0f) * 4095.0f) / 2.907f + 0.5f);
	return v < 0 ? 0 : (v > 4095 ? 4095 : v);
}
static inline int32_t PeriodFromBpm(int32_t bpm)
{
	return (kSampleRate * 60) / ClampBpm(bpm);
}

//----------------------------------------------------------------------------
// Envelope: attack up to full, then decay to zero.  Pluckiness bends the
// decay, from a straight line (soft) through to a sharp drop with a long
// tail (a pluck).  A retrigger climbs from wherever the envelope is, so
// fast ratchets never click.
//----------------------------------------------------------------------------
struct Envelope
{
	enum Stage : uint8_t {Idle, Attack, Decay};
	Stage stage = Idle;
	uint32_t pos = 0;           // through the stage, Q31
	int32_t from = 0, out = 0;  // Q15

	void Trigger()
	{
		from = out;
		pos = 0;
		stage = Attack;
	}

	// The decay's shape: x falls from 1 to 0 (Q15) and is bent towards x^2,
	// x^4 and finally x^8 as pluck (0-4095) rises
	static int32_t Bend(int32_t x, int32_t pluck)
	{
		int32_t x2 = (x * x) >> 15;
		int32_t x4 = (x2 * x2) >> 15;
		int32_t x8 = (x4 * x4) >> 15;
		int32_t p = pluck * 3;
		int seg = p >> 12;
		int32_t f = p & 4095;
		int32_t a = seg == 0 ? x : (seg == 1 ? x2 : x4);
		int32_t b = seg == 0 ? x2 : (seg == 1 ? x4 : x8);
		return a + (((b - a) * f) >> 12);
	}

	int32_t Tick(uint32_t attackInc, uint32_t decayInc, int32_t pluck)
	{
		if (stage == Attack)
		{
			pos += attackInc;
			if (pos >= 0x80000000u)
			{
				stage = Decay;
				pos = 0;
				out = 32767;
			}
			else out = from + (((32767 - from) * int32_t(pos >> 16)) >> 15);
		}
		else if (stage == Decay)
		{
			pos += decayInc;
			if (pos >= 0x80000000u)
			{
				stage = Idle;
				out = 0;
			}
			else out = Bend(32767 - int32_t(pos >> 16), pluck);
		}
		return out;
	}
};

//----------------------------------------------------------------------------
// The engine
//----------------------------------------------------------------------------
class Engine
{
public:
	// Fader values, 0-127, as an 8mu sends them: [page][fader].  Written by
	// the 8mu handling, or by the web editor on the other core.
	volatile uint8_t faders[kPages][kFaders];

	// Settings, set from the knobs (and the web editor) at control rate
	int scale = 0;   // Major
	int ratio = kUnityRatio;
	int32_t slewAlpha = 1 << 24;
	bool slewTiesOnly = false;        // slew only notes a tie leads into
	uint32_t attackInc = 89478485, decayInc = 4473924;
	int32_t pluck = 2048;
	int32_t internalPeriod = 24000;   // 120 BPM

	// Outputs, refreshed by Tick()
	int32_t cvMv[kVoices] = {};       // pitch, millivolts
	int16_t audio[kVoices] = {};      // envelope, 0 to about +5V
	int32_t level[kVoices] = {};      // the same as a gain, 0 to 32767 (Q15)
	bool gate[kVoices] = {};

	// What each voice is playing, for the LEDs and the web editor
	struct Voice
	{
		uint32_t rng;
		int noteFader = 0, note = 0, octaveFader = 2, gateFader = 3, ratchetFader = 0;
		// Envelope deviations (attack, decay, pluck, depth): the fader drawn
		// on each page, -1 for none
		int dev[kEnvKinds] = {-1, -1, -1, -1};
		uint32_t attackInc = 89478485, decayInc = 4473924;  // after deviation
		uint32_t baseAttack = 0, baseDecay = 0;              // the knobs' at that time
		int32_t targetMv = 0;
		int64_t slewed = 0;           // millivolts, Q16
		bool glide = false;           // this note came in through a tie
		int hits = 1, nextHit = 1;
		int32_t hitAt[kMaxHits] = {};
		int32_t sinceHit = 0x7FFFFFF; // samples since the last hit
		bool gateOn = false, tied = false;
		int32_t gateLeft = 0;         // samples to go; -1 = held to the next hit
		int32_t gap = 0;              // samples of forced gap before a gate
		uint8_t hitCount = 0;         // counts hits, for the web editor
		Envelope env;

		// The next set of choices, prepared a step at a time in the quiet
		// samples between hits (see Prepare), so that a hit only has to
		// take them.  prepared counts the steps done; kPrepSteps is ready.
		struct Choices {int ratchet, noteFader, note, octave, gate, dev[kEnvKinds]; int scale;};
		Choices next = {};
		int prepared = 0;
	};
	static constexpr int kPrepSteps = 4 + kEnvKinds;
	Voice voice[kVoices];

	// Clock and step timing
	bool clocked = false;             // following Pulse In 1
	int32_t clockPeriod = 24000;      // samples between clocks
	int32_t stepLen = 24000;          // samples a step lasts, as estimated
	int32_t sinceStep = 0;            // samples into the step
	uint8_t stepCount = 0;            // counts steps, for the web editor

	static constexpr int32_t kMaxClockPeriod = 60 * kSampleRate; // a minute

	Engine()
	{
		// The limits of the envelope times, which deviation stays within
		fastestAttack = AttackFromKnob(0);
		slowestAttack = AttackFromKnob(4095);
		fastestDecay = DecayFromKnob(0);
		slowestDecay = DecayFromKnob(4095);
		DefaultFaders();
		Rescan();
		voice[0].rng = 0x2545F491;
		voice[1].rng = 0x9E3779B9;
	}

	// Default probabilities, so the card does something with no 8mu.  The
	// second pages start at zero, so voice 2 shares the first pages.
	void DefaultFaders()
	{
		static const uint8_t def[kPairs][kFaders] = {
			{127, 25, 80, 30, 100, 40, 20, 50},  // notes: root, third, fifth
			{0, 20, 127, 90, 25, 0, 0, 0},       // octaves: around 0V and +1V
			{10, 15, 40, 90, 110, 60, 30, 25},   // gates: mostly middling
			{127, 10, 30, 12, 0, 6, 16, 6},      // ratchets: mostly single hits
		};
		for (int p = 0; p < kPages; p++)
			for (int i = 0; i < kFaders; i++)
				faders[p][i] = p < kPairs ? def[p][i] : 0;
	}

	// Stir some unpredictability into the random numbers (from clock timing
	// or fader movement), so the card doesn't play the same lines after
	// every power-up
	void Stir(uint32_t x)
	{
		voice[0].rng ^= x * 0x9E3779B1u;
		voice[1].rng ^= x * 0x85EBCA77u;
		if (!voice[0].rng) voice[0].rng = 1;
		if (!voice[1].rng) voice[1].rng = 1;
	}

	// True if every fader on a page is at zero
	bool Empty(int page) const
	{
		for (int i = 0; i < kFaders; i++)
			if (Weight(faders[page][i]) > 0) return false;
		return true;
	}

	// The same for every page, as bits, kept up to date by Tick() one page
	// a sample (so within a third of a millisecond).  The draws at the start
	// of a step use these rather than looking through every page's faders,
	// which keeps the audio interrupt short.
	uint16_t emptyPages = 0;
	void Rescan()
	{
		for (int p = 0; p < kPages; p++) ScanPage(p);
	}

	// A voice 2 page is in use once any of its faders is up; while it is
	// all zero, voice 2 shares the first page with voice 1
	bool Shared(int kind) const {return Empty(SecondPage(kind));}

	// The page voice v draws its choices of this kind from (from the
	// cached flags)
	int PageFor(int v, int kind) const
	{
		return (v == 1 && !((emptyPages >> SecondPage(kind)) & 1)) ? SecondPage(kind) : FirstPage(kind);
	}
	const volatile uint8_t *Faders(int v, int kind) const {return faders[PageFor(v, kind)];}

	const Scale &CurrentScale() const {return kScales[scale];}

	// One sample.  clockEdge is a rising edge at Pulse In 1.
	// True after a Tick() that drew new choices, the heaviest work there is,
	// so that the card can leave its own control work for the next sample
	bool busy = false;

	void Tick(bool clockEdge)
	{
		busy = false;
		ScanPage(scanPage);
		scanPage = (scanPage + 1) & (kPages - 1);
		// One step of preparing the voices' next choices, alternating voices
		prepVoice ^= 1;
		if (voice[prepVoice].prepared < kPrepSteps) Prepare(prepVoice);
		else if (voice[prepVoice ^ 1].prepared < kPrepSteps) Prepare(prepVoice ^ 1);

		// A step starts with voice 1's choices, and voice 2's follow one
		// sample later, so the two never land in the same interrupt
		if (ClockTick(clockEdge))
		{
			sinceStep = 0;
			stepCount++;
			StartVoice(0);
			voice2Pending = true;
			busy = true;
		}
		else
		{
			if (sinceStep < 0x7FFFFFFF) sinceStep++;
			if (voice2Pending)
			{
				voice2Pending = false;
				StartVoice(1);
				busy = true;
			}
		}

		for (int v = 0; v < kVoices; v++)
		{
			Voice &vc = voice[v];
			if (vc.sinceHit < 0x7FFFFFF) vc.sinceHit++;
			// Ratchet hits after the first, through the step
			if (vc.nextHit < vc.hits && sinceStep >= vc.hitAt[vc.nextHit])
			{
				if (NewNoteEachHit(vc.ratchetFader)) {Draw(v); busy = true;}
				Hit(v);
			}

			// The gate, with its gap before a note that follows too closely
			if (vc.gap > 0) vc.gap--;
			else if (vc.gateOn && vc.gateLeft > 0 && --vc.gateLeft == 0) vc.gateOn = false;
			gate[v] = vc.gateOn && vc.gap == 0;

			// Pitch, with slew (or with the knob left of centre, slew only
			// into a note a tie leads into)
			int64_t target = int64_t(vc.targetMv) * 65536;
			if (slewAlpha >= (1 << 24) || (slewTiesOnly && !vc.glide)) vc.slewed = target;
			else vc.slewed += ((target - vc.slewed) * slewAlpha) >> 24;
			cvMv[v] = int32_t(vc.slewed >> 16);

			// Envelope, 0 to about +5V on the audio outputs, with this note's
			// deviations from the knobs
			if (vc.baseAttack != attackInc || vc.baseDecay != decayInc) EnvelopeRates(vc);
			int32_t pl = pluck + (vc.dev[2] >= 0 ? kDevPluck[vc.dev[2]] : 0);
			pl = pl < 0 ? 0 : (pl > 4095 ? 4095 : pl);
			int32_t e = vc.env.Tick(vc.attackInc, vc.decayInc, pl);
			if (vc.dev[3] >= 0) e = (e * (vc.dev[3] + 1)) >> 3;   // DEPTH
			level[v] = e;
			audio[v] = int16_t((e * 1706) >> 15);
		}
	}

	// The time to the next hit (or the end of the step) from hit k
	int32_t HitInterval(const Voice &vc, int k) const
	{
		int32_t end = k + 1 < vc.hits ? vc.hitAt[k + 1] : stepLen;
		int32_t d = end - vc.hitAt[k];
		return d < 1 ? 1 : d;
	}

	// Where the hits of a ratchet speed fall in the current step, in samples
	int32_t HitPosition(int speed, int k) const
	{
		return (stepLen * kHitTwelfths[speed][k]) / 12;   // stepLen < 2^27: no overflow
	}

private:
	// Clock state
	bool haveClock = false;
	int32_t sinceClock = 0x7FFFFFFF;
	int32_t internalCount = 0;
	int32_t subPhase = 0;
	int subCount = 0;
	int divCount = 0;

	// Works out the beats: Pulse In 1's clock (or the internal one when
	// nothing is plugged in), divided or multiplied by the ratio.  Returns
	// true on a beat, when a step starts.
	bool ClockTick(bool edge)
	{
		if (sinceClock < 0x7FFFFFFF) sinceClock++;
		if (edge)
		{
			// Two clocks up to a minute apart give the period; until then
			// (and after the clock stops) the internal clock keeps time
			if (haveClock && sinceClock <= kMaxClockPeriod)
			{
				extPeriod = sinceClock < 48 ? 48 : sinceClock;
				if (!clocked) divCount = 0;
				clocked = true;
			}
			haveClock = true;
			sinceClock = 0;
		}
		// The clock counts as stopped after four of its own periods without
		// one (never less than 2s), so slow clocks are followed rather than
		// dropped between pulses
		int32_t timeout = extPeriod > kSampleRate / 2 ? extPeriod * 4 : 2 * kSampleRate;
		if (clocked && sinceClock >= timeout)
		{
			clocked = false;
			internalCount = 0;
		}

		bool clockNow;
		if (clocked)
		{
			clockNow = edge;
			clockPeriod = extPeriod;
		}
		else
		{
			clockPeriod = internalPeriod;
			clockNow = ++internalCount >= internalPeriod;
			if (clockNow) internalCount = 0;
		}

		// Multiplying: num beats a clock, evenly spaced at the measured
		// period and started afresh by every clock, so they never drift
		// from it (and never run on past a clock that slows down)
		const Ratio r = kRatios[ratio];
		bool beat = false;
		if (clockNow)
		{
			subPhase = 0;
			subCount = 0;
			beat = true;
		}
		else if (subCount < r.num - 1)
		{
			subPhase += r.num;
			if (subPhase >= clockPeriod)
			{
				subPhase -= clockPeriod;
				subCount++;
				beat = true;
			}
		}
		if (!beat) return false;
		// Dividing: a step every den beats
		if (++divCount < r.den) return false;
		divCount = 0;
		// (a clock is at most a minute, 2.9M samples, so x8 fits in 32 bits)
		int32_t len = (clockPeriod * r.den) / r.num;
		stepLen = len < 1 ? 1 : (len > 0x7FFFFFF ? 0x7FFFFFF : len);
		return true;
	}
	int32_t extPeriod = 24000;
	uint32_t fastestAttack = 0, slowestAttack = 0, fastestDecay = 0, slowestDecay = 0;

	// The attack and decay rates for a voice's deviations: a longer time
	// is a smaller step each sample
	void EnvelopeRates(Voice &vc)
	{
		vc.baseAttack = attackInc;
		vc.baseDecay = decayInc;
		vc.attackInc = attackInc;
		vc.decayInc = decayInc;
		if (vc.dev[0] >= 0)
		{
			uint32_t a = ExpScale(attackInc, -kDevOctaves[vc.dev[0]]);
			vc.attackInc = a > fastestAttack ? fastestAttack : (a < slowestAttack ? slowestAttack : a);
		}
		if (vc.dev[1] >= 0)
		{
			uint32_t d = ExpScale(decayInc, -kDevOctaves[vc.dev[1]]);
			vc.decayInc = d > fastestDecay ? fastestDecay : (d < slowestDecay ? slowestDecay : d);
		}
	}

	int scanPage = 0;
	bool voice2Pending = false;

	int prepVoice = 0;
	uint32_t pageSig[kPages] = {};

	// Look at one page: is it all at zero, and has it changed?  A change
	// means the prepared choices may be out of date, so they're redrawn.
	void ScanPage(int p)
	{
		uint32_t sig = 0;
		bool empty = true;
		for (int i = 0; i < kFaders; i++)
		{
			int w = Weight(faders[p][i]);
			if (w) empty = false;
			sig = (sig << 3) ^ (sig >> 29) ^ uint32_t(w);
		}
		if (empty) emptyPages |= uint16_t(1 << p);
		else emptyPages &= uint16_t(~(1 << p));
		if (sig != pageSig[p])
		{
			pageSig[p] = sig;
			voice[0].prepared = voice[1].prepared = 0;
		}
	}

	// One step of drawing voice v's next choices
	void Prepare(int v)
	{
		Voice &vc = voice[v];
		Voice::Choices &c = vc.next;
		int step = vc.prepared++;
		switch (step)
		{
		case 0:
			c.ratchet = Choose(Faders(v, Ratchets), 0xFF, NextRandom(vc.rng));
			break;
		case 1:
		{
			const Scale &s = CurrentScale();
			c.scale = scale;
			c.noteFader = Choose(Faders(v, Notes), ActiveNoteFaders(s), NextRandom(vc.rng));
			int first, count;
			FaderNotes(s, c.noteFader, first, count);
			c.note = first + (count > 1 ? Below(NextRandom(vc.rng), uint32_t(count)) : 0);
			break;
		}
		case 2:
			c.octave = Choose(Faders(v, Octaves), 0xFF, NextRandom(vc.rng));
			break;
		case 3:
			c.gate = Choose(Faders(v, Gates), 0xFF, NextRandom(vc.rng));
			break;
		default:
		{
			// The envelope's deviations: none from a page all at zero
			int k = step - 4;
			int page = PageFor(v, kPairs + k);
			c.dev[k] = (emptyPages >> page) & 1 ? -1 : Choose(faders[page], 0xFF, NextRandom(vc.rng));
			break;
		}
		}
	}

	// Make sure voice v's next choices are ready (normally they long since
	// are), and that the note suits the scale playing now
	void Ready(int v)
	{
		Voice &vc = voice[v];
		while (vc.prepared < kPrepSteps) Prepare(v);
		if (vc.next.scale != scale)
		{
			int keep = vc.prepared;
			vc.prepared = 1;
			Prepare(v);
			vc.prepared = keep;
		}
	}

	// A new step for voice v: its ratchet and its first note
	void StartVoice(int v)
	{
		Voice &vc = voice[v];
		Ready(v);
		vc.ratchetFader = vc.next.ratchet;
		int sp = SpeedOf(vc.ratchetFader);
		vc.hits = kHits[sp];
		for (int k = 0; k < vc.hits; k++) vc.hitAt[k] = HitPosition(sp, k);
		vc.nextHit = 0;
		Draw(v);
		Hit(v);
	}

	// A new note, octave, gate length and envelope for voice v: the choices
	// prepared in advance, which then start being prepared afresh
	void Draw(int v)
	{
		Voice &vc = voice[v];
		Ready(v);
		const Voice::Choices &c = vc.next;
		vc.noteFader = c.noteFader;
		vc.note = c.note;
		vc.octaveFader = c.octave;
		vc.gateFader = c.gate;
		for (int k = 0; k < kEnvKinds; k++) vc.dev[k] = c.dev[k];
		vc.targetMv = NoteMillivolts(CurrentScale(), vc.note, vc.octaveFader);
		EnvelopeRates(vc);
		vc.prepared = 0;
	}

	// Play the next hit of voice v's step: its gate and envelope
	void Hit(int v)
	{
		Voice &vc = voice[v];
		int k = vc.nextHit++;
		vc.sinceHit = 0;
		vc.hitCount++;
		if (vc.gateFader == kRestFader)
		{
			// A rest: no gate, no envelope (the pitch still moves)
			vc.gateOn = false;
			vc.gap = 0;
			vc.tied = false;
			vc.glide = false;
			return;
		}
		// After a tied note the gate is still up: the new note joins on
		// without a new envelope.  Otherwise a new envelope, and if the
		// last gate hasn't fallen yet a short gap so the gate rises afresh.
		bool legato = vc.tied && vc.gateOn;
		vc.glide = legato;
		if (!legato)
		{
			vc.env.Trigger();
			if (vc.gateOn) vc.gap = kGateGap;
		}
		vc.gateOn = true;
		if (vc.gateFader == kTiedFader)
		{
			vc.gateLeft = -1;
			vc.tied = true;
		}
		else
		{
			int32_t interval = HitInterval(vc, k);
			int32_t len = (interval * vc.gateFader) / 7;
			if (len > interval - kGateGap) len = interval - kGateGap;
			if (len < kGateGap / 2) len = kGateGap / 2;
			vc.gateLeft = len; // counted once the gap is over
			vc.tied = false;
		}
	}
};

} // namespace weights

#endif
