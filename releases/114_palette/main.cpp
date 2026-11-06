/*
  Palette — Workshop Computer program card
  ============================================

  An experiment: a simple monophonic synth voice where EVERY synthesis
  parameter is read from a Turing-Machine-style shift register.

  The register is 64 bits long. The web editor splits it into 1–8 independent
  "loops" (for example one 64-bit loop, or four 16-bit loops), each with its
  own length, clock divider and "chaos" amount. Every clock tick, each loop
  whose divider is due rotates by one bit; the bit that falls off the top is
  fed back into the bottom, and may be flipped at random (the Turing Machine
  idea: Main knob at noon = random, fully clockwise = locked loop).

  Each synth parameter is then "assigned" to a window of bits anywhere in the
  64-bit space: a start bit and a width (1–16 bits). Windows may overlap and
  may straddle the boundary between two loops. The bits are read either as a
  binary number or as a count of set bits ("popcount"), optionally inverted,
  and scaled into a Min..Max range chosen in the editor.

  Parameters (fixed order — the index is the stable ID used by the editor):
     0 Env Trigger      fires both envelopes when its value is in the top half
     1 Osc 1 Pitch      scale degrees above the root, quantised to the scale
     2 Osc 2 Pitch      scale degrees relative to Osc 1, same scale
     3 Slew             pitch glide time
     4 Osc 1 Shape      sine -> triangle -> saw -> square
     5 Osc 2 Shape      sine -> triangle -> saw -> square
     6 Osc Mix          Osc 1 <-> Osc 2
     7 Cutoff           low-pass cutoff, ~20 Hz – 14 kHz
     8 Resonance        up to self-oscillation
     9 Cutoff Env Amt   bipolar filter-envelope depth
    10 Filter Attack    1 ms – 4 s
    11 Filter Decay     1 ms – 4 s
    12 Filter Shape     envelope curve: slow-start / linear / snappy
    13 Vol Attack
    14 Vol Decay
    15 Vol Shape
    16 FM On            Osc 2 frequency-modulates Osc 1 (on when value >= 1)
    17 FM Amount        FM index 0 – 4; above 1 Osc 1's frequency swings
                        through zero and runs backwards (through-zero FM)

  Panel
    Main knob   Randomness (Turing): noon = random, CW = locked,
                CCW = locked + inverted (pattern repeats at twice the length)
    X knob      Transpose, 0 – +24 semitones
    Y knob      Internal clock rate (used when Pulse In 1 is unplugged)
                The editor can set these three too; the knob then picks up
                (soft takeover) once turned to the new value. LED 5 blinks
                fast while a knob is waiting.
    Z switch    Up = lock all loops, Middle = normal, Down = scramble register
    Pulse In 1  External clock
    Pulse In 2  Extra envelope trigger
    CV In 1     Pitch, 1V/oct (quantised to semitones)
    CV In 2     Added to the Main knob (randomness)
    Audio Out 1 The voice
    Audio Out 2 Filter envelope
    CV Out 1    Osc 1 pitch, calibrated 1V/oct (MIDI note 60 = 0 V)
    CV Out 2    Volume envelope
    Pulse Out 1 Envelope trigger
    Pulse Out 2 Clock

  Code layout
    Core 0 runs ProcessSample() at 48 kHz: clock, shift register, voice.
    Core 1 runs USB-MIDI: SysEx from the web editor (protocol in sysex.h),
    live status to it, and flash saves.

  Code written with AI assistance (Claude), following the USB/SysEx
  pattern of the CVSeq and WaveSeq cards.
*/

#include "ComputerCard.h"
#include "pico/multicore.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "tusb.h"
#include "sysex.h"
#include <cmath>
#include <cstring>
#include <cstddef>
#include <cstdlib>

////////////////////////////////////////////////////////////////////////////////
// Firmware / protocol versions

constexpr uint8_t FW_MAJOR = 0, FW_MINOR = 3, FW_PATCH = 0;
constexpr uint8_t CONFIG_FORMAT = 2;   // bump when the serialised layout changes

constexpr int MAX_LOOPS = 8;
constexpr int NUM_PARAMS = 18;
constexpr int REG_BITS = 64;
constexpr int MAX_WIDTH = 16;

enum ParamId
{
	P_TRIG = 0, P_PITCH1, P_PITCH2, P_SLEW, P_SHAPE1, P_SHAPE2, P_MIX,
	P_CUTOFF, P_RES, P_ENVAMT, P_FATK, P_FDEC, P_FSHAPE, P_VATK, P_VDEC, P_VSHAPE,
	P_FMON, P_FMAMT
};

// Native range of each parameter. The editor mirrors this table; Min/Max
// values sent from the editor are clamped into it.
struct ParamRange { int16_t lo, hi; };
static const ParamRange paramRange[NUM_PARAMS] = {
	{0, 1023},      // Env Trigger (fires when >= 512)
	{0, 48},        // Osc 1 pitch, scale degrees above root
	{-24, 24},      // Osc 2 pitch, scale degrees relative to Osc 1
	{0, 1023},      // Slew
	{0, 1023},      // Osc 1 shape
	{0, 1023},      // Osc 2 shape
	{0, 1023},      // Mix
	{0, 1023},      // Cutoff
	{0, 1023},      // Resonance
	{-1023, 1023},  // Cutoff env amount
	{0, 1023}, {0, 1023}, {0, 1023},  // Filter A, D, shape
	{0, 1023}, {0, 1023}, {0, 1023},  // Vol A, D, shape
	{0, 1},         // FM on
	{0, 1023},      // FM amount: index = value / 256
};

// Assignment flags
constexpr uint8_t FLAG_POPCOUNT = 1;   // count set bits instead of binary value
constexpr uint8_t FLAG_INVERT = 2;     // flip the normalised value (1 - x)

////////////////////////////////////////////////////////////////////////////////
// Configuration: everything the web editor can change

struct LoopCfg
{
	uint8_t length;   // bits, 1..64
	uint8_t divider;  // steps once every N clock ticks, 1..32
	uint8_t chaos;    // 0..100 % of the Main-knob flip probability
};

struct ParamCfg
{
	uint8_t enabled;  // 0 = unassigned: parameter holds its Min value
	uint8_t start;    // first (least significant) bit, 0..63
	uint8_t width;    // number of bits, 1..16
	uint8_t flags;    // FLAG_POPCOUNT | FLAG_INVERT
	int16_t min, max; // output range, in the parameter's native units
};

struct Config
{
	uint8_t numLoops;
	LoopCfg loops[MAX_LOOPS];
	uint16_t scaleMask; // bit n set = semitone n above root is in the scale
	uint8_t root;       // MIDI note of scale degree 0
	ParamCfg params[NUM_PARAMS];
};

// Serialised size: see Serialise() for the byte layout
constexpr int CONFIG_BYTES = 2 + MAX_LOOPS * 3 + 3 + NUM_PARAMS * 8; // 173

static void DefaultConfig(Config &c)
{
	memset(&c, 0, sizeof(c));
	// Four 16-bit loops running at different speeds: fast melody/rhythm,
	// slower timbre and slowest envelope changes.
	c.numLoops = 4;
	const uint8_t divs[4] = {1, 2, 4, 8};
	for (int i = 0; i < 4; i++) c.loops[i] = {16, divs[i], 100};
	for (int i = 4; i < MAX_LOOPS; i++) c.loops[i] = {8, 1, 100};

	c.scaleMask = (1 << 0) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 10); // minor pentatonic
	c.root = 48; // C3

	//                 en start width flags  min   max
	c.params[P_TRIG]   = {1,  0, 1, 0,           0, 1023};
	c.params[P_PITCH1] = {1,  1, 5, 0,           0,   10};
	c.params[P_PITCH2] = {1, 20, 2, 0,           0,    5};
	c.params[P_SLEW]   = {1, 26, 3, 0,           0,  350};
	c.params[P_SHAPE1] = {1, 32, 4, 0,           0, 1023};
	c.params[P_SHAPE2] = {1, 36, 4, FLAG_INVERT, 0, 1023};
	c.params[P_MIX]    = {1, 22, 4, FLAG_POPCOUNT, 200, 800};
	c.params[P_CUTOFF] = {1,  6, 6, 0,         300,  800};  // overlaps Osc 1 pitch
	c.params[P_RES]    = {1, 40, 3, 0,         100,  850};
	c.params[P_ENVAMT] = {1, 44, 4, 0,           0,  700};
	c.params[P_FATK]   = {1, 48, 2, 0,           0,  300};
	c.params[P_FDEC]   = {1, 50, 3, 0,         250,  750};
	c.params[P_FSHAPE] = {1, 53, 2, 0,         512, 1023};
	c.params[P_VATK]   = {1, 56, 2, 0,           0,  250};
	c.params[P_VDEC]   = {1, 58, 4, 0,         300,  800};
	c.params[P_VSHAPE] = {1, 62, 2, 0,         512, 1023};
	c.params[P_FMON]   = {1, 47, 1, 0,           0,    1};  // slow loop: FM comes and goes
	c.params[P_FMAMT]  = {1, 16, 4, 0,           0,  512};  // up to index 2: through zero
}

static int32_t Clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Make any config safe to run: firmware is the source of truth for ranges.
static void ValidateConfig(Config &c)
{
	c.numLoops = Clamp(c.numLoops, 1, MAX_LOOPS);
	int used = 0;
	for (int i = 0; i < MAX_LOOPS; i++)
	{
		LoopCfg &l = c.loops[i];
		l.divider = Clamp(l.divider, 1, 32);
		l.chaos = Clamp(l.chaos, 0, 100);
		l.length = Clamp(l.length, 1, REG_BITS);
		if (i < c.numLoops)
		{
			if (used + l.length > REG_BITS) l.length = REG_BITS - used;
			if (l.length < 1) { c.numLoops = i; break; } // no room left
			used += l.length;
		}
	}
	if (c.numLoops < 1) { c.numLoops = 1; c.loops[0].length = REG_BITS; }

	c.scaleMask &= 0x0FFF;
	if (c.scaleMask == 0) c.scaleMask = 0x0FFF; // empty scale -> chromatic
	c.root = Clamp(c.root, 0, 96);

	for (int i = 0; i < NUM_PARAMS; i++)
	{
		ParamCfg &p = c.params[i];
		p.enabled = p.enabled ? 1 : 0;
		p.start = Clamp(p.start, 0, REG_BITS - 1);
		p.width = Clamp(p.width, 1, MAX_WIDTH);
		if (p.start + p.width > REG_BITS) p.width = REG_BITS - p.start;
		p.flags &= (FLAG_POPCOUNT | FLAG_INVERT);
		p.min = Clamp(p.min, paramRange[i].lo, paramRange[i].hi);
		p.max = Clamp(p.max, paramRange[i].lo, paramRange[i].hi);
	}
}

// Byte layout shared with the web editor and the flash store:
//   [0]  CONFIG_FORMAT            [1] numLoops
//   [2..25]  8 x (length, divider, chaos)
//   [26,27] scaleMask (lo, hi)    [28] root
//   [29..172] 18 x (enabled, start, width, flags, minLo, minHi, maxLo, maxHi)
static void Serialise(const Config &c, uint8_t *b)
{
	int n = 0;
	b[n++] = CONFIG_FORMAT;
	b[n++] = c.numLoops;
	for (int i = 0; i < MAX_LOOPS; i++)
	{
		b[n++] = c.loops[i].length;
		b[n++] = c.loops[i].divider;
		b[n++] = c.loops[i].chaos;
	}
	b[n++] = c.scaleMask & 0xFF;
	b[n++] = c.scaleMask >> 8;
	b[n++] = c.root;
	for (int i = 0; i < NUM_PARAMS; i++)
	{
		const ParamCfg &p = c.params[i];
		b[n++] = p.enabled; b[n++] = p.start; b[n++] = p.width; b[n++] = p.flags;
		b[n++] = uint16_t(p.min) & 0xFF; b[n++] = uint16_t(p.min) >> 8;
		b[n++] = uint16_t(p.max) & 0xFF; b[n++] = uint16_t(p.max) >> 8;
	}
}

static bool Deserialise(Config &c, const uint8_t *b)
{
	if (b[0] != CONFIG_FORMAT) return false;
	int n = 1;
	c.numLoops = b[n++];
	for (int i = 0; i < MAX_LOOPS; i++)
	{
		c.loops[i].length = b[n++];
		c.loops[i].divider = b[n++];
		c.loops[i].chaos = b[n++];
	}
	c.scaleMask = b[n] | (b[n + 1] << 8); n += 2;
	c.root = b[n++];
	for (int i = 0; i < NUM_PARAMS; i++)
	{
		ParamCfg &p = c.params[i];
		p.enabled = b[n++]; p.start = b[n++]; p.width = b[n++]; p.flags = b[n++];
		p.min = int16_t(b[n] | (b[n + 1] << 8)); n += 2;
		p.max = int16_t(b[n] | (b[n + 1] << 8)); n += 2;
	}
	ValidateConfig(c);
	return true;
}

////////////////////////////////////////////////////////////////////////////////
// Flash storage: last 4 kB sector of the card

constexpr uint32_t FLASH_OFFSET = PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE;
constexpr uint32_t FLASH_MAGIC = 0x50414C32; // "PAL2"

struct FlashImage
{
	uint32_t magic;
	uint8_t cfg[CONFIG_BYTES];
	uint8_t pad[3];
	uint64_t reg;      // the register pattern at the time of saving
	uint32_t checksum;
};

static uint32_t Checksum(const uint8_t *d, int n)
{
	uint32_t h = 2166136261u; // FNV-1a
	for (int i = 0; i < n; i++) { h ^= d[i]; h *= 16777619u; }
	return h;
}

////////////////////////////////////////////////////////////////////////////////
// Lookup tables, computed once at start-up (float is fine here, not in audio)

static int16_t sineTable[257];
static uint32_t noteInc[130];     // oscillator phase increment for MIDI notes 0..129
static uint32_t envInc[1024];     // 24-bit envelope phase increment per sample
static int32_t cutoffCoef[1024];  // SVF frequency coefficient, Q15, for 96 kHz
static int32_t slewCoef[1024];    // one-pole glide coefficient, Q16

static void BuildTables()
{
	const float pi = 3.14159265f;
	for (int i = 0; i < 257; i++)
		sineTable[i] = int16_t(32767.0f * sinf(2.0f * pi * float(i) / 256.0f));

	for (int i = 0; i < 130; i++)
	{
		float f = 440.0f * exp2f((float(i) - 69.0f) / 12.0f);
		noteInc[i] = uint32_t(f / 48000.0f * 4294967296.0f);
	}

	// Envelope segment times: 1 ms .. 4.1 s, exponential across the range
	for (int i = 0; i < 1024; i++)
	{
		float ms = exp2f(float(i) * 12.0f / 1023.0f);
		envInc[i] = uint32_t(16777216.0f / (ms * 48.0f));
	}

	// Cutoff: 20 Hz .. ~14.5 kHz (9.5 octaves). Chamberlin SVF coefficient
	// f = 2 sin(pi fc / fs), with the filter run twice per sample (fs = 96 kHz)
	for (int i = 0; i < 1024; i++)
	{
		float fc = 20.0f * exp2f(float(i) * 9.5f / 1023.0f);
		cutoffCoef[i] = int32_t(32768.0f * 2.0f * sinf(pi * fc / 96000.0f));
	}

	// Glide: 0 = instant, otherwise time constant 0.5 ms .. ~1 s
	for (int i = 0; i < 1024; i++)
	{
		if (i == 0) { slewCoef[i] = 65536; continue; }
		float tc = 0.0005f * exp2f(float(i) * 11.0f / 1023.0f);
		slewCoef[i] = int32_t(65536.0f * (1.0f - expf(-1.0f / (tc * 12000.0f)))); // pitch runs at 12 kHz
		if (slewCoef[i] < 1) slewCoef[i] = 1;
	}
}

////////////////////////////////////////////////////////////////////////////////
// Small DSP helpers (fixed point; no float in the audio path)

static inline int32_t Mul64(int32_t a, int32_t b, int shift)
{
	return int32_t((int64_t(a) * b) >> shift);
}

// Soft clip for the audio output. Below the knee (±1400) the signal passes
// unchanged. Above it, the excess d is squashed as d * 600 / (d + 600), which
// starts at a slope of 1 (no corner to hear) and can never quite reach ±2000.
static inline int32_t SoftClip(int32_t x)
{
	const int32_t knee = 1400, room = 600;
	int32_t a = x < 0 ? -x : x;
	if (a <= knee) return x;
	int32_t d = a - knee;
	int32_t y = knee + (d * room) / (d + room);
	return x < 0 ? -y : y;
}

// Bandlimiting is deliberately not done: these are raw digital waveforms,
// the low-pass filter after them tames the harshness.
static inline int32_t Sine(uint32_t phase)
{
	uint32_t idx = phase >> 24, frac = (phase >> 8) & 0xFFFF;
	int32_t a = sineTable[idx], b = sineTable[idx + 1];
	return a + (((b - a) * int32_t(frac)) >> 16);
}
static inline int32_t Tri(uint32_t phase)
{
	int32_t p = int32_t(phase >> 16); // 0..65535
	return p < 32768 ? (p * 2 - 32767) : (32767 - (p - 32768) * 2);
}
static inline int32_t Saw(uint32_t phase) { return int32_t(phase >> 16) - 32768; }
static inline int32_t Square(uint32_t phase) { return phase < 0x80000000u ? 32767 : -32767; }

// Morph sine -> triangle -> saw -> square. shape 0..1023
static inline int32_t Wave(uint32_t phase, int32_t shape)
{
	int32_t u = shape * 3;
	int32_t seg = u >> 10; if (seg > 2) seg = 2;
	int32_t t = u - seg * 1024; if (t > 1023) t = 1023;
	int32_t a, b;
	if (seg == 0) { a = Sine(phase); b = Tri(phase); }
	else if (seg == 1) { a = Tri(phase); b = Saw(phase); }
	else { a = Saw(phase); b = Square(phase); }
	return (a * (1024 - t) + b * t) >> 10;
}

// Envelope curve. p = position in segment 0..65535, c = -512..511
// c < 0 : slow start (cubic), c = 0 : linear, c > 0 : fast start ("snappy")
static inline int32_t Curve(uint32_t p, int32_t c)
{
	uint32_t cube = (((p * p) >> 16) * p) >> 16;
	if (c >= 0)
	{
		uint32_t q = 65535 - p;
		uint32_t snappy = 65535 - ((((q * q) >> 16) * q) >> 16);
		return int32_t(p) + (((int32_t(snappy) - int32_t(p)) * c) >> 9);
	}
	return int32_t(p) + (((int32_t(cube) - int32_t(p)) * (-c)) >> 9);
}

// Attack/decay envelope, retriggerable from its current level (no clicks)
struct Envelope
{
	uint8_t stage = 0;       // 0 idle, 1 attack, 2 decay
	uint32_t phase = 0;      // 0 .. 1<<24 within the current stage
	int32_t level = 0;       // output, 0..65535
	int32_t startLevel = 0;  // level when the attack started

	void Trigger() { stage = 1; phase = 0; startLevel = level; }

	int32_t Process(int32_t atk, int32_t dec, int32_t shape)
	{
		int32_t c = shape - 512;
		if (stage == 1)
		{
			phase += envInc[atk];
			if (phase >= (1u << 24)) { stage = 2; phase = 0; level = 65535; }
			// Unsigned: (65535 - start) x curve can reach 2^32, which
			// overflows a signed 32-bit int (heard as a pop mid-attack)
			else level = startLevel + int32_t((uint32_t(65535 - startLevel) * uint32_t(Curve(phase >> 8, c))) >> 16);
		}
		else if (stage == 2)
		{
			phase += envInc[dec];
			if (phase >= (1u << 24)) { stage = 0; level = 0; }
			else level = 65535 - Curve(phase >> 8, c);
		}
		return level;
	}
};

static inline uint32_t Popcount64(uint64_t v)
{
	uint32_t lo = uint32_t(v), hi = uint32_t(v >> 32);
	return uint32_t(__builtin_popcount(lo) + __builtin_popcount(hi));
}

static inline uint64_t BitMask(int width) { return width >= 64 ? ~0ull : ((1ull << width) - 1); }
////////////////////////////////////////////////////////////////////////////////
// Everything the audio core needs from a layout, worked out in advance.
// Two of these exist: the audio core plays one while the USB core prepares
// the next, then the audio core switches over by swapping a pointer. That
// keeps the 48 kHz interrupt free of any big burst of work when the editor
// sends a layout (it sends them continuously while you drag).

struct Engine
{
	Config cfg;
	uint8_t loopOffset[MAX_LOOPS];
	uint64_t loopMask[MAX_LOOPS];
	int32_t loopChaosQ8[MAX_LOOPS];  // chaos 0-100 % as 0-256
	uint64_t usedMask;               // bits inside some loop
	uint64_t paramMask[NUM_PARAMS];  // bits each parameter reads
	int8_t scaleNotes[12];
	int32_t scaleLen;
};

static void BuildEngine(Engine &e, const Config &c)
{
	e.cfg = c;
	int offset = 0;
	e.usedMask = 0;
	for (int i = 0; i < MAX_LOOPS; i++)
	{
		e.loopOffset[i] = uint8_t(offset);
		e.loopMask[i] = i < c.numLoops ? BitMask(c.loops[i].length) : 0;
		e.loopChaosQ8[i] = (c.loops[i].chaos * 256 + 50) / 100;
		if (i < c.numLoops) { e.usedMask |= e.loopMask[i] << offset; offset += c.loops[i].length; }
	}
	for (int i = 0; i < NUM_PARAMS; i++)
		e.paramMask[i] = BitMask(c.params[i].width) << c.params[i].start;
	e.scaleLen = 0;
	for (int s = 0; s < 12; s++)
		if (c.scaleMask & (1 << s)) e.scaleNotes[e.scaleLen++] = int8_t(s);
}

////////////////////////////////////////////////////////////////////////////////
// The card

class Palette;
static Palette *gCard = nullptr;

class Palette : public ComputerCard
{
public:
	Palette()
	{
		BuildTables();
		EnableNormalisationProbe(); // lets us tell whether Pulse In 1 is patched

		// Seed the random generator from the card's unique ID
		uint64_t id = UniqueCardID();
		rng = uint32_t(id ^ (id >> 32)) | 1u;

		// Load saved settings, or defaults on a fresh card
		Config cfg;
		const FlashImage *img = reinterpret_cast<const FlashImage *>(XIP_BASE + FLASH_OFFSET);
		bool loaded = false;
		if (img->magic == FLASH_MAGIC &&
			img->checksum == Checksum(reinterpret_cast<const uint8_t *>(img), offsetof(FlashImage, checksum)))
		{
			uint8_t b[CONFIG_BYTES];
			memcpy(b, img->cfg, CONFIG_BYTES);
			if (Deserialise(cfg, b)) { reg = img->reg; loaded = true; }
		}
		if (!loaded)
		{
			DefaultConfig(cfg);
			ValidateConfig(cfg);
			reg = (uint64_t(Rand()) << 32) | Rand();
		}
		shadowCfg = cfg;
		BuildEngine(engines[0], cfg);
		eng = &engines[0];
		for (int i = 0; i < NUM_PARAMS; i++) paramVal[i] = ParamValue(i);
		FinishRefresh(false);
		for (int j = 0; j < NUM_SMOOTHED; j++) smooth[j] = paramVal[smoothedIds[j]] << 8;
		fmSmooth = (paramVal[P_FMON] ? paramVal[P_FMAMT] : 0) << 8;
	}

protected:
	////////////////////////////////////////////////////////////////////////////
	// Audio core
	//
	// ProcessSample must finish well inside ~20 us (about 3000 cycles at
	// 144 MHz), every sample: if it overruns, the Computer's knob and CV
	// readings get mixed up. So nothing here does a big burst of work:
	//  - after a clock step the loops are shifted one per sample, then the
	//    16 parameters re-read one per sample (about half a millisecond);
	//  - a new layout from the editor arrives ready-made (see Engine);
	//  - pitch maths and smoothing each run every 4th sample, the knobs one
	//    at a time, and the LEDs and the editor's snapshot every 32nd sample,
	//    each on a different sample so their costs never add up.

	uint32_t Rand() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

	// Read one parameter's bits and scale them into its Min..Max range
	int32_t ParamValue(int i)
	{
		const ParamCfg &p = eng->cfg.params[i];
		if (!p.enabled) return p.min;
		uint32_t bits = uint32_t((reg >> p.start) & BitMask(p.width)); // width <= 16
		// x = normalised value, 0..65535
		uint32_t x;
		if (p.flags & FLAG_POPCOUNT)
			x = (uint32_t(__builtin_popcount(bits)) * 65535u) / p.width;
		else
			x = p.width == 16 ? bits : (bits * 65535u) / ((1u << p.width) - 1);
		if (p.flags & FLAG_INVERT) x = 65535 - x;
		return p.min + ((int32_t(p.max - p.min) * int32_t(x) + 32768) >> 16);
	}

	// Convert a (possibly negative) scale degree into a MIDI note
	int32_t DegreeToNote(int32_t deg)
	{
		int32_t oct = 0, len = eng->scaleLen;
		while (deg < 0) { deg += len; oct--; }
		while (deg >= len) { deg -= len; oct++; }
		return eng->cfg.root + oct * 12 + eng->scaleNotes[deg];
	}

	// Start re-reading every parameter, one per sample. moved = the bits
	// that just moved (0 if this isn't a clock step).
	void StartRefresh(uint64_t moved)
	{
		refreshMoved |= moved;
		refreshIndex = 0;
	}

	// One step of the refresh; when the last parameter is in, work out the
	// notes and whether to fire the envelopes.
	void RefreshStep()
	{
		if (refreshIndex >= NUM_PARAMS) return;
		paramVal[refreshIndex] = ParamValue(refreshIndex);
		if (++refreshIndex == NUM_PARAMS)
		{
			FinishRefresh(refreshMoved != 0);
			refreshMoved = 0;
		}
	}

	void FinishRefresh(bool stepped)
	{
		int32_t deg1 = paramVal[P_PITCH1];
		note1 = DegreeToNote(deg1);
		note2 = DegreeToNote(deg1 + paramVal[P_PITCH2]);

		// The trigger is only evaluated when its own bits have moved (so a
		// slow loop doesn't fire repeatedly on unchanged bits). Unassigned,
		// it fires on every step if its Min is in the top half.
		if (stepped)
		{
			bool relevant = eng->cfg.params[P_TRIG].enabled ? (refreshMoved & eng->paramMask[P_TRIG]) != 0 : true;
			if (relevant && paramVal[P_TRIG] >= 512) FireEnvelopes();
		}
	}

	void FireEnvelopes()
	{
		fltEnv.Trigger();
		volEnv.Trigger();
		trigPulse = 240; // 5 ms
		trigCount++;
	}

	// A clock step. The loops are then shifted one per sample (ShiftStep),
	// and the parameters re-read after the last one.
	void Tick()
	{
		// Flip probability from the Main setting + CV 2 (Turing Machine style).
		// probQ16 = 65536 means "always flip", 0 means "never flip".
		int32_t k = Clamp(settings[KMain] + CVIn2(), 0, 4095);
		int32_t probQ16;
		if (k < 150) probQ16 = 65536;
		else if (k > 3945) probQ16 = 0;
		else probQ16 = ((3945 - k) * 17685) >> 10;
		if (SwitchVal() == Up) probQ16 = 0; // lock
		probDisplay = probQ16;
		tickProb = probQ16;

		// A step that arrives before the last one finished (only with a very
		// fast external clock) finishes that one at once
		while (shiftIndex < MAX_LOOPS) ShiftStep();
		shiftIndex = 0;
		shiftMoved = 0;
		clockPulse = 240;
		tickCount++;
	}

	// Shift one loop, if its clock divider is due
	void ShiftStep()
	{
		int i = shiftIndex++;
		const Engine &e = *eng;
		if (i < e.cfg.numLoops && ++loopCounter[i] >= e.cfg.loops[i].divider)
		{
			loopCounter[i] = 0;
			int len = e.cfg.loops[i].length, off = e.loopOffset[i];
			uint64_t m = e.loopMask[i];
			uint64_t seg = (reg >> off) & m;
			uint32_t top = uint32_t(seg >> (len - 1)) & 1;
			int32_t p = (tickProb * e.loopChaosQ8[i]) >> 8;
			if (int32_t(Rand() >> 16) < p) top ^= 1;
			seg = ((seg << 1) | top) & m;
			reg = (reg & ~(m << off)) | (seg << off);
			shiftMoved |= m << off;
		}
		if (shiftIndex == MAX_LOOPS) StartRefresh(shiftMoved);
	}

	virtual void ProcessSample()
	{
		frame++;
		uint32_t slot = frame & 31;

		// A new layout from the editor: switch to it (a pointer swap)
		Engine *ne = pendingEngine;
		if (ne)
		{
			eng = ne;
			pendingEngine = nullptr;
			for (int i = 0; i < MAX_LOOPS; i++)
				if (loopCounter[i] >= eng->cfg.loops[i].divider) loopCounter[i] = 0;
			StartRefresh(0);
		}
		if (regPending) { reg = pendingReg; __dmb(); regPending = false; StartRefresh(0); }

		// Z down: scramble the register
		if (SwitchChanged() && SwitchVal() == Down)
		{
			uint64_t r = (uint64_t(Rand()) << 32) | Rand();
			reg = (reg & ~eng->usedMask) | (r & eng->usedMask);
			StartRefresh(0);
		}

		// Clock: external on Pulse In 1 if patched, otherwise the Y setting
		if (Connected(Input::Pulse1))
		{
			if (PulseIn1RisingEdge()) Tick();
		}
		else if (++clockCount >= clockPeriod)
		{
			clockCount = 0;
			Tick();
		}
		if (PulseIn2RisingEdge()) FireEnvelopes();

		// After a step: shift one loop per sample, then re-read one
		// parameter per sample
		if (shiftIndex < MAX_LOOPS) ShiftStep();
		else RefreshStep();

		// Control-rate jobs, each on its own sample
		switch (slot & 3)
		{
		case 0: UpdatePitch(); break;                     // 12 kHz
		case 1: HandleKnob((frame >> 2) % 3); break;      // each knob at 4 kHz
		case 2: UpdateSmoothing(); break;                 // 12 kHz
		default:
			if (slot == 3) PublishSnapshot();             // 1.5 kHz
			else if (slot == 7) UpdateLeds();             // 1.5 kHz
			break;
		}

		// Oscillators. Osc 2 first: it is the FM modulator.
		phase2 += inc2;
		int32_t o2 = Wave(phase2, smooth[1] >> 8);

		// Through-zero FM: Osc 1's frequency is scaled by (1 + index x Osc 2),
		// index = amount / 256 (0 to 4). Above index 1 the frequency goes
		// negative for part of each cycle and the phase runs backwards,
		// which keeps the pitch centre steady however deep the FM goes.
		uint32_t i1 = inc1;
		int32_t fm = fmSmooth >> 8;
		if (fm > 0)
		{
			int64_t f1 = int64_t(int32_t(inc1)) + ((int64_t(int32_t(inc1)) * (o2 * fm)) >> 23);
			if (f1 > 0x7FFFFFFF) f1 = 0x7FFFFFFF;
			else if (f1 < -0x7FFFFFFF) f1 = -0x7FFFFFFF;
			i1 = uint32_t(int32_t(f1));
		}
		phase1 += i1;
		int32_t mix = smooth[2] >> 8;
		int32_t o1 = Wave(phase1, smooth[0] >> 8);
		int32_t osc = ((o1 * (1023 - mix) + o2 * mix) >> 10) >> 1; // ±16384

		// Envelopes
		int32_t fe = fltEnv.Process(paramVal[P_FATK], paramVal[P_FDEC], paramVal[P_FSHAPE]);
		int32_t ve = volEnv.Process(paramVal[P_VATK], paramVal[P_VDEC], paramVal[P_VSHAPE]);
		volLevel = ve;

		// Filter: Chamberlin state-variable low-pass, run twice per sample
		int32_t cut = Clamp((smooth[3] >> 8) + (((smooth[5] >> 8) * fe) >> 16), 0, 1023);
		int32_t f = cutoffCoef[cut];
		int32_t q = 32768 - (smooth[4] >> 8) * 31; // damping, Q14: 2.0 (none) .. ~0.06 (screaming)
		for (int n = 0; n < 2; n++)
		{
			svfLow += Mul64(f, svfBand, 15);
			int32_t high = osc - svfLow - Mul64(q, svfBand, 14);
			svfBand += Mul64(f, high, 15);
			// Hard limits keep high resonance stable (and add some grit)
			svfBand = Clamp(svfBand, -262144, 262143);
			svfLow = Clamp(svfLow, -262144, 262143);
		}

		// VCA, then a soft clip. A full-level note with no resonance peaks
		// around ±1700 of the DAC's ±2047, leaving headroom. Louder than the
		// knee (resonance peaks), the level bends smoothly towards ±2000, so
		// the output never reaches the DAC's limit and hard-clips.
		int32_t y = Mul64(svfLow, ve, 16) * 7 >> 6; // ±16384 nominal -> ±1792
		AudioOut1(SoftClip(y));
		AudioOut2(fe >> 5);
		CVOut2(ve >> 5);

		// Pulse outs: envelope trigger and clock, 5 ms each
		PulseOut1(trigPulse > 0);
		PulseOut2(clockPulse > 0);
		if (trigPulse) trigPulse--;
		if (clockPulse) clockPulse--;
	}

	// Every 4th sample: smoothing and the internal clock rate
	void UpdateSmoothing()
	{
		// Smooth the continuous parameters (they jump on each step) to avoid
		// zipper clicks: ~3 ms one-pole, values kept at 8 extra bits.
		for (int j = 0; j < NUM_SMOOTHED; j++)
			smooth[j] += ((paramVal[smoothedIds[j]] << 8) - smooth[j]) >> 5;
		// FM depth glides to its new value, and to or from zero when FM
		// turns on or off, so switching it never clicks
		int32_t fmTarget = paramVal[P_FMON] ? paramVal[P_FMAMT] : 0;
		fmSmooth += ((fmTarget << 8) - fmSmooth) >> 5;

		// Y: ~1 Hz (CCW) to ~16 Hz (CW), exponential
		int32_t yq = settings[KY] * 4;
		int32_t base = 48000 >> (yq >> 12);
		clockPeriod = base - ((base * (yq & 4095)) >> 13);
	}

	// Every 4th sample (a different one): pitch, glide, CV Out 1
	void UpdatePitch()
	{
		// Pitch: scale note + X transpose + CV 1 (1V/oct, semitone-quantised)
		int32_t transpose = (settings[KX] * 25) >> 12;
		int32_t cv = CVIn1();
		int32_t cvSemis = (cv * 72 + (cv >= 0 ? 1024 : -1024)) / 2048;
		curNote1 = Clamp(note1 + transpose + cvSemis, 0, 127);
		curNote2 = Clamp(note2 + transpose + cvSemis, 0, 127);
		int32_t k = slewCoef[Clamp(paramVal[P_SLEW], 0, 1023)];
		pitch1 += Mul64((curNote1 << 16) - pitch1, k, 16);
		pitch2 += Mul64((curNote2 << 16) - pitch2, k, 16);
		inc1 = PitchToInc(pitch1);
		inc2 = PitchToInc(pitch2);

		// Calibrated pitch out (MIDI 60 = 0 V)
		CVOut1Millivolts(int32_t((int64_t(pitch1 - (60 << 16)) * 5333) >> 22));
	}

	// LEDs: bits 0..3 of the register, clock, volume envelope.
	// LED 5 blinks fast instead while a knob is waiting to pick up.
	void UpdateLeds()
	{
		uint32_t lo = uint32_t(reg);
		for (int i = 0; i < 4; i++) LedOn(i, (lo >> i) & 1);
		LedOn(4, clockPulse > 0);
		bool waiting = !knobLatched[0] || !knobLatched[1] || !knobLatched[2];
		LedBrightness(5, waiting ? ((frame >> 12) & 1 ? 4095 : 0) : (volLevel >> 4));
	}

	// Publish a snapshot for the editor (seqlock: odd = being written)
	void PublishSnapshot()
	{
		stateSeq++; __dmb();
		snap.reg = reg; snap.tick = tickCount; snap.trig = trigCount; snap.prob = probDisplay;
		Switch sw = SwitchVal();
		snap.flags = uint8_t((sw == Up ? 2 : sw == Middle ? 1 : 0)
			| (Connected(Input::Pulse1) ? 4 : 0)
			| (knobLatched[KMain] ? 0 : 8) | (knobLatched[KX] ? 0 : 16) | (knobLatched[KY] ? 0 : 32));
		for (int i = 0; i < 3; i++) { snap.setting[i] = settings[i]; snap.knob[i] = lastKnob[i]; }
		snap.note1 = uint8_t(curNote1); snap.note2 = uint8_t(curNote2);
		__dmb(); stateSeq++;
	}

	////////////////////////////////////////////////////////////////////////////
	// Knobs, with soft takeover.
	// Main, X and Y each drive a stored setting (randomness, transpose, clock
	// rate). At power-up the settings take the knobs as they are. When the web
	// editor sets one, the knob does nothing until it is turned to (or past)
	// the new value, so nothing jumps. LED 5 blinks fast while one is waiting.

	enum KnobSetting {KMain, KX, KY};
	static constexpr int32_t kKnobEndZone = 40;  // ignore the last bit of travel each end

	static int32_t MapKnob(int32_t raw)
	{
		// (raw - 40) * 4095 / 4015, as a multiply and shift
		int32_t v = ((raw - kKnobEndZone) * 16712) >> 14;
		return v < 0 ? 0 : (v > 4095 ? 4095 : v);
	}

	void HandleKnob(int i)
	{
		static const Knob knobs[3] = {Main, X, Y};
		int32_t k = MapKnob(KnobVal(knobs[i]));
		if (!knobStarted[i]) { settings[i] = k; lastKnob[i] = k; knobStarted[i] = true; return; }

		int32_t req = knobRequest[i];
		if (req >= 0) { settings[i] = req; knobRequest[i] = -1; knobLatched[i] = false; }

		int32_t &v = settings[i];
		if (!knobLatched[i])
		{
			int32_t d = k - v;
			bool near = d > -48 && d < 48;
			bool crossed = (lastKnob[i] - v < 0) != (d < 0);
			if (near || crossed) knobLatched[i] = true;
		}
		if (knobLatched[i]) v = k;
		lastKnob[i] = k;
	}

	static inline uint32_t PitchToInc(int32_t pitchQ16)
	{
		int32_t i = pitchQ16 >> 16;
		int32_t frac = pitchQ16 & 0xFFFF;
		uint32_t a = noteInc[i], b = noteInc[i + 1];
		return a + uint32_t((int64_t(b - a) * frac) >> 16);
	}

	////////////////////////////////////////////////////////////////////////////
	// USB, on core 1 (same pattern as the CVSeq and WaveSeq cards)

public:
	void StartUSB()
	{
		gCard = this;
		// Only talk to a computer: if the Computer is supplying USB power
		// (host side), there is no editor to talk to.
		if (USBPowerState() != DFP) multicore_launch_core1(Core1Device);
	}

private:
	// The web editor counts as linked while its pings keep arriving
	bool WebLinked() const
	{
		return pinged && (time_us_32() - lastPingUs) < 3000000;
	}

	static void Core1Device()
	{
		tusb_init();
		static sysex::Parser parser;
		uint32_t lastStatusUs = 0;
		while (true)
		{
			tud_task();
			uint8_t buf[64];
			while (tud_midi_available())
			{
				uint32_t n = tud_midi_stream_read(buf, sizeof(buf));
				if (n == 0) break;
				for (uint32_t i = 0; i < n; i++)
					if (parser.Feed(buf[i])) gCard->OnSysEx(parser.cmd, parser.payload, parser.length);
			}

			// Live status, about 30 times a second, while the editor is there
			uint32_t now = time_us_32();
			if (gCard->WebLinked() && now - lastStatusUs >= 33000)
			{
				lastStatusUs = now;
				static uint8_t msg[4 + sysex::kStatusPayload + 1];
				Write(msg, gCard->EncodeStatus(msg));
			}
		}
	}

	// Send a whole message, waiting briefly for room if need be. If the
	// computer isn't reading, the rest is dropped; the editor's parser
	// resynchronises on the next F0.
	static void Write(const uint8_t *msg, int len)
	{
		uint32_t start = time_us_32();
		int sent = 0;
		while (sent < len && tud_mounted())
		{
			sent += int(tud_midi_stream_write(0, msg + sent, uint32_t(len - sent)));
			if (sent < len)
			{
				if (time_us_32() - start > 50000) return;
				tud_task();
			}
		}
	}

	void OnSysEx(uint8_t cmd, const uint8_t *p, int len)
	{
		switch (cmd)
		{
		case sysex::Hello:
			SendState();
			break;
		case sysex::SetConfig:
			if (len == 1 + 2 * CONFIG_BYTES && p[0] == sysex::kVersion)
			{
				uint8_t b[CONFIG_BYTES];
				sysex::GetNibbles(b, p + 1, CONFIG_BYTES);
				Config c;
				if (Deserialise(c, b)) QueueConfig(c);
			}
			break;
		case sysex::Save:
		{
			SaveToFlash();
			uint8_t msg[5];
			int n = sysex::Header(msg, sysex::Saved);
			msg[n++] = 0xF7;
			Write(msg, n);
			break;
		}
		case sysex::SetReg:
			if (len == 16)
			{
				uint8_t b[8];
				sysex::GetNibbles(b, p, 8);
				uint64_t r = 0;
				for (int i = 7; i >= 0; i--) r = (r << 8) | b[i];
				while (regPending) tight_loop_contents();
				pendingReg = r; __dmb(); regPending = true;
			}
			break;
		case sysex::Reset:
		{
			Config c;
			DefaultConfig(c);
			ValidateConfig(c);
			QueueConfig(c);
			SendState();
			break;
		}
		case sysex::Knob:
			if (len >= 3 && p[0] < 3) knobRequest[p[0]] = Clamp(sysex::Get14(p + 1), 0, 4095);
			break;
		case sysex::Ping:
			lastPingUs = time_us_32();
			pinged = true;
			break;
		default:
			break;
		}
	}

	// Prepare a new layout in the engine the audio core isn't playing, then
	// hand it over. Waits (briefly) until the previous hand-over is taken.
	void QueueConfig(const Config &c)
	{
		while (pendingEngine) tight_loop_contents();
		Engine *spare = (eng == &engines[0]) ? &engines[1] : &engines[0];
		BuildEngine(*spare, c);
		shadowCfg = c;
		__dmb();
		pendingEngine = spare;
	}

	struct Snapshot
	{
		uint64_t reg;
		uint32_t tick, trig;
		int32_t prob;
		uint8_t flags;
		int32_t setting[3], knob[3];
		uint8_t note1, note2;
	};

	Snapshot ReadSnapshot()
	{
		Snapshot s;
		uint32_t q;
		do
		{
			q = stateSeq; __dmb();
			s = snap;
			__dmb();
		} while ((q & 1) || q != stateSeq);
		return s;
	}

	void SendState()
	{
		static uint8_t msg[4 + 1 + 2 * CONFIG_BYTES + 6 + 1];
		int n = sysex::Header(msg, sysex::State);
		msg[n++] = sysex::kVersion;
		uint8_t b[CONFIG_BYTES];
		Serialise(shadowCfg, b);
		n += sysex::PutNibbles(msg + n, b, CONFIG_BYTES);
		Snapshot s = ReadSnapshot();
		for (int i = 0; i < 3; i++) n += sysex::Put14(msg + n, s.setting[i]);
		msg[n++] = 0xF7;
		Write(msg, n);
	}

	int EncodeStatus(uint8_t *out)
	{
		Snapshot s = ReadSnapshot();
		int n = sysex::Header(out, sysex::Status);
		uint8_t b[8];
		for (int i = 0; i < 8; i++) b[i] = uint8_t(s.reg >> (8 * i));
		n += sysex::PutNibbles(out + n, b, 8);
		out[n++] = uint8_t(s.tick & 0x7F);
		out[n++] = uint8_t(s.trig & 0x7F);
		out[n++] = s.flags & 0x7F;
		for (int i = 0; i < 3; i++) n += sysex::Put14(out + n, s.setting[i]);
		for (int i = 0; i < 3; i++) n += sysex::Put14(out + n, s.knob[i]);
		out[n++] = uint8_t(Clamp(s.prob >> 9, 0, 127));
		out[n++] = s.note1 & 0x7F;
		out[n++] = s.note2 & 0x7F;
		out[n++] = 0xF7;
		return n;
	}

	// Write config + current register to flash, from core 1. The audio core runs
	// entirely from RAM (copy_to_ram), so it keeps playing while flash is
	// busy; we only need to stop core 1's own interrupts.
	void __not_in_flash_func(SaveToFlash)()
	{
		static uint8_t sector[FLASH_SECTOR_SIZE] __attribute__((aligned(4)));
		memset(sector, 0xFF, sizeof(sector));
		FlashImage img;
		memset(&img, 0, sizeof(img));
		img.magic = FLASH_MAGIC;
		Serialise(shadowCfg, img.cfg);
		img.reg = ReadSnapshot().reg;
		img.checksum = Checksum(reinterpret_cast<const uint8_t *>(&img), offsetof(FlashImage, checksum));
		memcpy(sector, &img, sizeof(img));

		uint32_t ints = save_and_disable_interrupts();
		flash_range_erase(FLASH_OFFSET, FLASH_SECTOR_SIZE);
		flash_range_program(FLASH_OFFSET, sector, FLASH_SECTOR_SIZE);
		restore_interrupts(ints);
	}

	// Running state (audio core)
	Engine engines[2];
	Engine *volatile eng = nullptr;            // the layout being played
	Engine *volatile pendingEngine = nullptr;  // a new one from the USB core
	uint64_t reg = 0;
	uint8_t loopCounter[MAX_LOOPS] = {};
	int32_t paramVal[NUM_PARAMS] = {};
	int refreshIndex = NUM_PARAMS;             // next parameter to re-read
	uint64_t refreshMoved = 0;
	int shiftIndex = MAX_LOOPS;                // next loop to shift after a step
	uint64_t shiftMoved = 0;
	int32_t tickProb = 0;
	int32_t note1 = 48, note2 = 48, curNote1 = 48, curNote2 = 48;
	int32_t pitch1 = 48 << 16, pitch2 = 48 << 16;
	uint32_t inc1 = 0, inc2 = 0;
	uint32_t phase1 = 0, phase2 = 0;
	int32_t svfLow = 0, svfBand = 0;
	Envelope fltEnv, volEnv;
	int32_t volLevel = 0;
	int32_t clockCount = 0, clockPeriod = 6000;
	uint32_t trigPulse = 0, clockPulse = 0;
	uint32_t tickCount = 0, trigCount = 0;
	int32_t probDisplay = 0;
	uint32_t rng = 1;
	uint32_t frame = 0;

	static constexpr int NUM_SMOOTHED = 6;
	const uint8_t smoothedIds[NUM_SMOOTHED] = {P_SHAPE1, P_SHAPE2, P_MIX, P_CUTOFF, P_RES, P_ENVAMT};
	int32_t smooth[NUM_SMOOTHED] = {};
	int32_t fmSmooth = 0;  // FM amount actually used, 8 extra bits

	// Knob settings (Main randomness, X transpose, Y clock rate), 0-4095
	int32_t settings[3] = {2048, 0, 3000};
	bool knobLatched[3] = {true, true, true};
	bool knobStarted[3] = {};
	int32_t lastKnob[3] = {};

	// Shared between cores
	volatile uint64_t pendingReg = 0;
	volatile bool regPending = false;
	volatile int32_t knobRequest[3] = {-1, -1, -1};  // from the editor
	volatile uint32_t stateSeq = 0;
	Snapshot snap = {};                               // guarded by stateSeq
	volatile uint32_t lastPingUs = 0;
	volatile bool pinged = false;

	// USB core only
	Config shadowCfg; // the layout core 1 last handed to the audio core
};

int main()
{
	set_sys_clock_khz(144000, true);
	static Palette card;
	card.StartUSB();
	card.Run();
}
