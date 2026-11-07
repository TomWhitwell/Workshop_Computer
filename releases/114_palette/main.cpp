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
    17 FM Offset        base FM index 0 – 4; above 1 Osc 1's frequency
                        swings through zero and runs backwards (through-zero FM)
    18 FM Env Amt       bipolar: how far the FM envelope moves the FM index
    19 FM Attack        FM envelope attack, 1 ms – 4 s
    20 FM Decay         FM envelope decay, 1 ms – 4 s (fixed snappy curve)
    21 Osc 1 P.Env Amt  Osc 1 pitch envelope depth, ±48 semitones, unquantised
    22 Osc 1 P.Env Atk  1 ms – 4 s
    23 Osc 1 P.Env Dec  1 ms – 4 s
    24 Osc 1 P.Env Shp  curve: slow start / linear / snappy
    25-28               the same for Osc 2
    29 Sync             0 off, 1 soft, 2 hard: Osc 2 follows Osc 1. Each
                        time Osc 1 starts a cycle, hard sync restarts Osc 2's
                        cycle; soft sync reverses its direction instead

  Panel
    Main knob   Randomness (Turing): noon = random, CW = locked,
                CCW = locked + inverted (pattern repeats at twice the length)
    X knob      Transpose, 0 – +24 semitones
    Y knob      Internal clock rate (used when Pulse In 1 is unplugged)
                The editor can set these three too; the knob then picks up
                (soft takeover) once turned to the new value. LED 5 blinks
                fast while a knob is waiting.
    Z switch    Up = lock all loops, and Main chooses one of six layout slots
                saved on the card (its LED lights for 1.5 s); Middle = normal;
                Down = scramble register
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

constexpr uint8_t FW_MAJOR = 0, FW_MINOR = 8, FW_PATCH = 0;
constexpr uint8_t CONFIG_FORMAT = 5;   // bump when the serialised layout changes

constexpr int MAX_LOOPS = 8;
constexpr int NUM_PARAMS = 30;
constexpr int REG_BITS = 64;
constexpr int MAX_WIDTH = 16;

enum ParamId
{
	P_TRIG = 0, P_PITCH1, P_PITCH2, P_SLEW, P_SHAPE1, P_SHAPE2, P_MIX,
	P_CUTOFF, P_RES, P_ENVAMT, P_FATK, P_FDEC, P_FSHAPE, P_VATK, P_VDEC, P_VSHAPE,
	P_FMON, P_FMOFF, P_FMENVAMT, P_FMATK, P_FMDEC,
	P_P1AMT, P_P1ATK, P_P1DEC, P_P1SHAPE, P_P2AMT, P_P2ATK, P_P2DEC, P_P2SHAPE,
	P_SYNC
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
	{0, 1023},      // FM offset: index = value / 256
	{-1023, 1023},  // FM env amount
	{0, 1023}, {0, 1023},  // FM attack, decay
	{-1023, 1023}, {0, 1023}, {0, 1023}, {0, 1023},  // Osc 1 pitch env: amount, A, D, shape
	{-1023, 1023}, {0, 1023}, {0, 1023}, {0, 1023},  // Osc 2 pitch env
	{0, 2},         // Sync: off, soft, hard
};

// Assignment flags
constexpr uint8_t FLAG_POPCOUNT = 1;   // count set bits instead of binary value
constexpr uint8_t FLAG_INVERT = 2;     // flip the normalised value (1 - x)
constexpr uint8_t FLAG_HOLD = 4;       // keep the current value: register steps don't change it

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
constexpr int CONFIG_BYTES = 2 + MAX_LOOPS * 3 + 3 + NUM_PARAMS * 8; // 269

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
	c.params[P_FMOFF]  = {1, 16, 4, 0,           0,  256};  // base index 0-1
	c.params[P_FMENVAMT] = {1, 24, 4, 0,         0,  700};  // envelope adds up to ~index 2.7
	c.params[P_FMATK]  = {0,  0, 1, 0,           0,    0};  // unassigned: instant attack
	c.params[P_FMDEC]  = {1, 28, 3, 0,         250,  750};
	// Pitch envelopes: Osc 1 sometimes starts with a quick downward zap of
	// up to an octave; Osc 2's envelope is off (amount held at 0)
	c.params[P_P1AMT]   = {1, 33, 2, 0,           0,  256};
	c.params[P_P1ATK]   = {0,  0, 1, 0,           0,    0};
	c.params[P_P1DEC]   = {0,  0, 1, 0,         350,  350};
	c.params[P_P1SHAPE] = {0,  0, 1, 0,         900,  900};
	c.params[P_P2AMT]   = {0,  0, 1, 0,           0,    0};
	c.params[P_P2ATK]   = {0,  0, 1, 0,           0,    0};
	c.params[P_P2DEC]   = {0,  0, 1, 0,         350,  350};
	c.params[P_P2SHAPE] = {0,  0, 1, 0,         900,  900};
	c.params[P_SYNC]    = {0,  0, 1, 0,           0,    0};  // off until assigned
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
		p.flags &= (FLAG_POPCOUNT | FLAG_INVERT | FLAG_HOLD);
		p.min = Clamp(p.min, paramRange[i].lo, paramRange[i].hi);
		p.max = Clamp(p.max, paramRange[i].lo, paramRange[i].hi);
	}
}

// Byte layout shared with the web editor and the flash store:
//   [0]  CONFIG_FORMAT            [1] numLoops
//   [2..25]  8 x (length, divider, chaos)
//   [26,27] scaleMask (lo, hi)    [28] root
//   [29..268] 30 x (enabled, start, width, flags, minLo, minHi, maxLo, maxHi)
//   flags: bit 0 count mode, bit 1 invert, bit 2 hold
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

// How many parameters each layout format holds. Each format only added
// parameters at the end, so an older layout reads as a prefix, and the
// parameters it doesn't have take their factory settings.
static int ParamsInFormat(int format)
{
	switch (format)
	{
	case 1: return 16;  // first release
	case 2: return 18;  // + FM On, FM Amount
	case 3: return 21;  // + FM envelope
	case 4: return 29;  // + pitch envelopes
	case 5: return 30;  // + sync
	default: return 0;
	}
}

static bool Deserialise(Config &c, const uint8_t *b)
{
	int count = ParamsInFormat(b[0]);
	if (count == 0 || count > NUM_PARAMS) return false;
	DefaultConfig(c);
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
	for (int i = 0; i < count; i++)
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
// Flash storage: the last 4 kB sector of the card holds six layout slots.
// Each slot keeps a layout, the register pattern it was saved with, and a
// name. The card starts up playing the slot it last played.

constexpr uint32_t FLASH_OFFSET = PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE;
constexpr uint32_t STORE_MAGIC = 0x534C4150;  // "PALS"
constexpr int NUM_SLOTS = 6;                  // one per LED
constexpr int NAME_LEN = 16;                  // 7-bit ASCII, 0-padded

struct SlotImage
{
	uint8_t used;
	char name[NAME_LEN];
	uint8_t cfg[CONFIG_BYTES];
	uint8_t reg[8];    // the register pattern, little endian
};

struct Store
{
	uint32_t magic;
	uint8_t format;    // CONFIG_FORMAT the slots were saved with
	uint8_t current;   // the slot played at power-up
	uint8_t pad[2];
	SlotImage slots[NUM_SLOTS];
	uint32_t checksum;
};
static_assert(sizeof(Store) <= FLASH_SECTOR_SIZE, "slots must fit one flash sector");

// The single-layout image firmware 0.2-0.5 saved, read once to carry a
// saved layout over into slot 1
constexpr uint32_t LEGACY_MAGIC = 0x50414C34; // "PAL4"
struct LegacyImage
{
	uint32_t magic;
	uint8_t cfg[CONFIG_BYTES];
	uint8_t pad[3];
	uint64_t reg;
	uint32_t checksum;
};

static uint32_t Checksum(const uint8_t *d, int n)
{
	uint32_t h = 2166136261u; // FNV-1a
	for (int i = 0; i < n; i++) { h ^= d[i]; h *= 16777619u; }
	return h;
}

static void SetName(char *dst, const char *src)
{
	memset(dst, 0, NAME_LEN);
	for (int i = 0; i < NAME_LEN && src[i]; i++) dst[i] = char(src[i] >= 32 && src[i] < 127 ? src[i] : '?');
}

static void PutSlot(SlotImage &sl, const Config &c, uint64_t reg)
{
	sl.used = 1;
	Serialise(c, sl.cfg);
	for (int i = 0; i < 8; i++) sl.reg[i] = uint8_t(reg >> (8 * i));
}

static uint64_t SlotReg(const SlotImage &sl)
{
	uint64_t r = 0;
	for (int i = 7; i >= 0; i--) r = (r << 8) | sl.reg[i];
	return r;
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

	// shift: run at 1/2^shift of the sample rate (e.g. 2 for every 4th sample)
	int32_t Process(int32_t atk, int32_t dec, int32_t shape, int shift = 0)
	{
		int32_t c = shape - 512;
		if (stage == 1)
		{
			phase += envInc[atk] << shift;
			if (phase >= (1u << 24)) { stage = 2; phase = 0; level = 65535; }
			// Unsigned: (65535 - start) x curve can reach 2^32, which
			// overflows a signed 32-bit int (heard as a pop mid-attack)
			else level = startLevel + int32_t((uint32_t(65535 - startLevel) * uint32_t(Curve(phase >> 8, c))) >> 16);
		}
		else if (stage == 2)
		{
			phase += envInc[dec] << shift;
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

		// Read the layout slots, then play the one played last
		LoadStore();
		Config cfg;
		bool loaded = false;
		const SlotImage &cur = store.slots[store.current];
		if (cur.used)
		{
			uint8_t b[CONFIG_BYTES];
			memcpy(b, cur.cfg, CONFIG_BYTES);
			if (Deserialise(cfg, b)) { reg = SlotReg(cur); loaded = true; }
		}
		if (!loaded)
		{
			DefaultConfig(cfg);
			ValidateConfig(cfg);
			reg = (uint64_t(Rand()) << 32) | Rand();
		}
		currentSlot = store.current;
		UpdateUsedMask();
		shadowCfg = cfg;
		BuildEngine(engines[0], cfg);
		eng = &engines[0];
		for (int i = 0; i < NUM_PARAMS; i++) pubVal[i] = paramVal[i] = ParamValue(i);
		FinishRefresh(false);
		for (int j = 0; j < NUM_SMOOTHED; j++) smooth[j] = paramVal[smoothedIds[j]] << 8;
		fmGate = paramVal[P_FMON] ? (256 << 8) : 0;
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
	//  - pitch maths, pitch envelopes and smoothing each run every 4th
	//    sample, the knobs one at a time, and the LEDs and the editor's
	//    snapshot every 32nd sample, staggered so their costs never add up.

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
	// notes and whether to fire the envelopes. A held parameter keeps its
	// value, except when a whole new layout and register arrive (a slot is
	// loaded): then it takes the value they give, and holds that.
	void RefreshStep()
	{
		if (refreshIndex >= NUM_PARAMS) return;
		if (refreshFull || !(eng->cfg.params[refreshIndex].flags & FLAG_HOLD))
			pubVal[refreshIndex] = paramVal[refreshIndex] = ParamValue(refreshIndex);
		if (++refreshIndex == NUM_PARAMS)
		{
			FinishRefresh(refreshMoved != 0);
			refreshMoved = 0;
			refreshFull = false;
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
		// Held, it is like a fixed setting: fire on every step, or never.
		if (stepped)
		{
			const ParamCfg &t = eng->cfg.params[P_TRIG];
			bool relevant = (t.enabled && !(t.flags & FLAG_HOLD)) ? (refreshMoved & eng->paramMask[P_TRIG]) != 0 : true;
			if (relevant && paramVal[P_TRIG] >= 512) FireEnvelopes();
		}

		refreshCount++;  // pubVal (for the editor) is complete
	}

	void FireEnvelopes()
	{
		fltEnv.Trigger();
		volEnv.Trigger();
		fmEnv.Trigger();
		pEnv1.Trigger();
		pEnv2.Trigger();
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
			if (fullRefreshRequest) { refreshFull = true; fullRefreshRequest = false; }
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
		case 1:
			HandleKnob((frame >> 2) % 3);                 // each knob at 4 kHz
			if (slot == 1) PublishSnapshot();             // 1.5 kHz
			else if (slot == 5) UpdateLeds();             // 1.5 kHz
			break;
		case 2: UpdateSmoothing(); break;                 // 12 kHz
		default: UpdatePitchEnvelopes(); break;           // 12 kHz
		}

		// Oscillators. Osc 2 first: it is the FM modulator. With soft sync
		// it may be running backwards (dir2 < 0).
		if (dir2 > 0) phase2 += inc2; else phase2 -= inc2;
		int32_t o2 = Wave(phase2, smooth[1] >> 8);

		// Through-zero FM: Osc 1's frequency is scaled by (1 + index x Osc 2),
		// index = depth / 256 (0 to 4). Above index 1 the frequency goes
		// negative for part of each cycle and the phase runs backwards,
		// which keeps the pitch centre steady however deep the FM goes.
		// Depth = FM Offset + FM Env Amt x the FM envelope, faded in and out
		// by FM On.
		int32_t fme = fmEnv.Process(paramVal[P_FMATK], paramVal[P_FMDEC], kFmEnvShape);
		int32_t depth = Clamp((smooth[6] >> 8) + (((smooth[7] >> 8) * fme) >> 16), 0, 1023);
		int32_t fm = (depth * (fmGate >> 8)) >> 8;
		uint32_t i1 = inc1;
		if (fm > 0)
		{
			int64_t f1 = int64_t(int32_t(inc1)) + ((int64_t(int32_t(inc1)) * (o2 * fm)) >> 23);
			if (f1 > 0x7FFFFFFF) f1 = 0x7FFFFFFF;
			else if (f1 < -0x7FFFFFFF) f1 = -0x7FFFFFFF;
			i1 = uint32_t(int32_t(f1));
		}
		uint32_t was1 = phase1;
		phase1 += i1;

		// Sync: Osc 2 follows Osc 1. When Osc 1 starts a new cycle (its phase
		// wraps, either way round with through-zero FM), hard sync restarts
		// Osc 2's cycle and soft sync reverses its direction. Heard from the
		// next sample. Not band-limited: the edges alias, which is part of
		// the classic sync sound.
		int32_t sync = paramVal[P_SYNC];
		if (sync != 1) dir2 = 1;
		if (sync && (int32_t(i1) >= 0 ? phase1 < was1 : phase1 > was1))
		{
			if (sync == 2) phase2 = 0;
			else dir2 = -dir2;
		}
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
		// FM fades in and out over ~3 ms when FM On changes, so switching
		// it never clicks
		int32_t gateTarget = paramVal[P_FMON] ? 256 : 0;
		fmGate += ((gateTarget << 8) - fmGate) >> 5;

		// Y: ~1 Hz (CCW) to ~16 Hz (CW), exponential
		int32_t yq = settings[KY] * 4;
		int32_t base = 48000 >> (yq >> 12);
		clockPeriod = base - ((base * (yq & 4095)) >> 13);
	}

	// Every 4th sample: the two pitch envelopes, at 12 kHz. Env Amt is
	// ±1023 for ±48 semitones, unquantised, added on top of the (quantised)
	// note by UpdatePitch.
	void UpdatePitchEnvelopes()
	{
		int32_t pe1 = pEnv1.Process(paramVal[P_P1ATK], paramVal[P_P1DEC], paramVal[P_P1SHAPE], 2);
		int32_t pe2 = pEnv2.Process(paramVal[P_P2ATK], paramVal[P_P2DEC], paramVal[P_P2SHAPE], 2);
		sweep1 = (((smooth[8] >> 8) * pe1) >> 16) * 3075;  // 3075 = 48 st / 1023 in Q16
		sweep2 = (((smooth[9] >> 8) * pe2) >> 16) * 3075;
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

		// Add the pitch envelopes' sweeps (worked out on another sample)
		inc1 = PitchToInc(Clamp(pitch1 + sweep1, 0, 127 << 16));
		inc2 = PitchToInc(Clamp(pitch2 + sweep2, 0, 127 << 16));

		// Calibrated pitch out (MIDI 60 = 0 V)
		CVOut1Millivolts(int32_t((int64_t(pitch1 - (60 << 16)) * 5333) >> 22));
	}

	// LEDs: bits 0..3 of the register, clock, volume envelope.
	// LED 5 blinks fast instead while a knob is waiting to pick up.
	// For 1.5 s after a layout is chosen, they show its slot instead: that
	// slot's LED lit, or blinking if the slot is empty.
	void UpdateLeds()
	{
		if (int32_t(slotLedUntil - frame) > 0)
		{
			bool on = !slotLedEmpty || ((frame >> 12) & 1);
			for (int i = 0; i < 6; i++) LedOn(i, i == slotLedShow && on);
			return;
		}
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
			| (knobLatched[KMain] ? 0 : 8) | (knobLatched[KX] ? 0 : 16) | (knobLatched[KY] ? 0 : 32)
			| (mainSelectsSlot ? 64 : 0));
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

		// Switch up locks the loops, so Main has nothing to do: it chooses a
		// layout slot instead. Back in the middle, Main picks up its
		// randomness setting again.
		if (i == KMain)
		{
			bool up = SwitchVal() == Up;
			if (up) { SelectSlotWithMain(k); lastKnob[i] = k; return; }
			if (mainSelectsSlot) { mainSelectsSlot = false; knobLatched[KMain] = false; }
		}

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

	// Main chooses one of six slots: six zones round the knob. Flicking the
	// switch up just shows the current slot; a layout only changes once Main
	// is turned into a different zone, and only if that slot has one.
	void SelectSlotWithMain(int32_t k)
	{
		// The zone k is clearly in (48 away from a boundary), or -1
		int32_t lo = ((k > 48 ? k - 48 : 0) * NUM_SLOTS) >> 12;
		int32_t hi = ((k < 4047 ? k + 48 : 4095) * NUM_SLOTS) >> 12;
		int32_t zone = lo == hi ? lo : -1;
		if (!mainSelectsSlot)
		{
			mainSelectsSlot = true;
			slotZone = zone;
			ShowSlot(currentSlot);
			return;
		}
		if (zone < 0 || zone == slotZone) return;
		slotZone = zone;
		ShowSlot(zone);
		if ((slotUsedMask >> zone) & 1) slotRequest = int8_t(zone); // core 1 loads it
	}

	void ShowSlot(int s)
	{
		slotLedShow = s;
		slotLedEmpty = !((slotUsedMask >> s) & 1);
		slotLedUntil = frame + 72000; // 1.5 s
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
	void StartCore1()
	{
		gCard = this;
		// Only talk to a computer: if the Computer is supplying USB power
		// (host side), there is no editor to talk to. Core 1 runs either way,
		// as it also loads and saves layout slots.
		usbDevice = USBPowerState() != DFP;
		multicore_launch_core1(Core1Main);
	}

private:
	// The web editor counts as linked while its pings keep arriving
	bool WebLinked() const
	{
		return pinged && (time_us_32() - lastPingUs) < 3000000;
	}

	static void Core1Main()
	{
		Palette &c = *gCard;
		if (c.usbDevice) tusb_init();
		static sysex::Parser parser;
		uint32_t lastStatusUs = 0;
		while (true)
		{
			c.ServiceSlots();
			if (!c.usbDevice) continue;

			tud_task();
			uint8_t buf[64];
			while (tud_midi_available())
			{
				uint32_t n = tud_midi_stream_read(buf, sizeof(buf));
				if (n == 0) break;
				for (uint32_t i = 0; i < n; i++)
					if (parser.Feed(buf[i])) c.OnSysEx(parser.cmd, parser.payload, parser.length);
			}

			// Live status, about 30 times a second, while the editor is there
			uint32_t now = time_us_32();
			if (c.WebLinked() && now - lastStatusUs >= 33000)
			{
				lastStatusUs = now;
				static uint8_t msg[4 + sysex::kStatusPayload + 1];
				Write(msg, c.EncodeStatus(msg));
			}
		}
	}

	////////////////////////////////////////////////////////////////////////////
	// Layout slots (core 1, apart from LoadStore at power-up)

	void LoadStore()
	{
		const Store *f = reinterpret_cast<const Store *>(XIP_BASE + FLASH_OFFSET);
		if (f->magic == STORE_MAGIC && f->format == CONFIG_FORMAT && f->current < NUM_SLOTS
			&& f->checksum == Checksum(reinterpret_cast<const uint8_t *>(f), offsetof(Store, checksum)))
		{
			memcpy(&store, f, sizeof(Store));
			return;
		}
		if (MigrateStore()) return;
		// A fresh card: slot 1 is the factory layout, or the layout saved by
		// earlier firmware, which kept just one
		memset(&store, 0, sizeof(Store));
		store.magic = STORE_MAGIC;
		store.format = CONFIG_FORMAT;
		const LegacyImage *old = reinterpret_cast<const LegacyImage *>(XIP_BASE + FLASH_OFFSET);
		Config c;
		uint8_t b[CONFIG_BYTES];
		memcpy(b, old->cfg, CONFIG_BYTES);
		if (old->magic == LEGACY_MAGIC
			&& old->checksum == Checksum(reinterpret_cast<const uint8_t *>(old), offsetof(LegacyImage, checksum))
			&& Deserialise(c, b))
		{
			PutSlot(store.slots[0], c, old->reg);
			SetName(store.slots[0].name, "Saved");
		}
		else
		{
			DefaultConfig(c);
			ValidateConfig(c);
			PutSlot(store.slots[0], c, (uint64_t(Rand()) << 32) | Rand());
			SetName(store.slots[0].name, "Factory");
		}
	}

	// Slots saved by firmware 0.6, whose layouts held 29 parameters (format
	// 4): carry them over, each layout read as format 4 and stored again in
	// today's format. The flash itself is rewritten at the next save.
	bool MigrateStore()
	{
		constexpr int kOldCfg = 2 + MAX_LOOPS * 3 + 3 + 29 * 8;  // 261
		constexpr int kOldSlot = 1 + NAME_LEN + kOldCfg + 8;      // 286
		constexpr int kOldChecksumAt = 8 + NUM_SLOTS * kOldSlot;   // 1724
		const uint8_t *f = reinterpret_cast<const uint8_t *>(XIP_BASE + FLASH_OFFSET);
		uint32_t magic, sum;
		memcpy(&magic, f, 4);
		memcpy(&sum, f + kOldChecksumAt, 4);
		if (magic != STORE_MAGIC || f[4] != 4 || f[5] >= NUM_SLOTS || sum != Checksum(f, kOldChecksumAt)) return false;

		memset(&store, 0, sizeof(Store));
		store.magic = STORE_MAGIC;
		store.format = CONFIG_FORMAT;
		store.current = f[5];
		for (int s = 0; s < NUM_SLOTS; s++)
		{
			const uint8_t *o = f + 8 + s * kOldSlot;
			Config c;
			if (!o[0] || !Deserialise(c, o + 1 + NAME_LEN)) continue;
			uint64_t r = 0;
			for (int i = 7; i >= 0; i--) r = (r << 8) | o[1 + NAME_LEN + kOldCfg + i];
			PutSlot(store.slots[s], c, r);
			memcpy(store.slots[s].name, o + 1, NAME_LEN);
		}
		return true;
	}

	void UpdateUsedMask()
	{
		uint8_t m = 0;
		for (int i = 0; i < NUM_SLOTS; i++) if (store.slots[i].used) m |= uint8_t(1 << i);
		slotUsedMask = m;
	}

	// Called round core 1's loop: load a slot the Main knob chose, and note
	// which slot is playing in flash once it has stayed put for 5 s (so a
	// sweep of the knob doesn't wear the flash).
	void ServiceSlots()
	{
		int r = slotRequest;
		if (r >= 0) { slotRequest = -1; LoadSlot(r); }
		if (store.current != currentSlot && time_us_32() - slotChangedUs > 5000000)
		{
			store.current = uint8_t(currentSlot);
			WriteStore();
		}
	}

	bool LoadSlot(int s)
	{
		if (s < 0 || s >= NUM_SLOTS || !store.slots[s].used) return false;
		uint8_t b[CONFIG_BYTES];
		memcpy(b, store.slots[s].cfg, CONFIG_BYTES);
		Config c;
		if (!Deserialise(c, b)) return false;
		uint32_t count = refreshCount;
		QueueConfig(c, true);
		while (regPending) tight_loop_contents();
		pendingReg = SlotReg(store.slots[s]); __dmb(); regPending = true;
		currentSlot = s;
		slotChangedUs = time_us_32();
		if (usbDevice && WebLinked()) { WaitForRefresh(count); SendState(); SendSlots(); }
		return true;
	}

	// Wait (up to 20 ms) until the audio core has taken a new layout or
	// register and re-read every parameter, so STATE carries its values
	void WaitForRefresh(uint32_t since)
	{
		uint32_t t0 = time_us_32();
		while ((pendingEngine || regPending || refreshCount == since) && time_us_32() - t0 < 20000) tight_loop_contents();
	}

	// Save the playing layout and register into a slot, and play from it
	void SaveSlot(int s, const char *name)
	{
		SlotImage &sl = store.slots[s];
		PutSlot(sl, shadowCfg, ReadSnapshot().reg);
		if (name && name[0]) SetName(sl.name, name);
		else if (!sl.name[0]) { char n[NAME_LEN] = "Layout 1"; n[7] = char('1' + s); SetName(sl.name, n); }
		currentSlot = s;
		store.current = uint8_t(s);
		WriteStore();
		UpdateUsedMask();
	}

	// Write all six slots to flash. The audio core runs entirely from RAM
	// (copy_to_ram), so it keeps playing while flash is busy; only core 1's
	// own interrupts need stopping.
	void __not_in_flash_func(WriteStore)()
	{
		static uint8_t sector[FLASH_SECTOR_SIZE] __attribute__((aligned(4)));
		store.magic = STORE_MAGIC;
		store.format = CONFIG_FORMAT;
		store.checksum = Checksum(reinterpret_cast<const uint8_t *>(&store), offsetof(Store, checksum));
		memset(sector, 0xFF, sizeof(sector));
		memcpy(sector, &store, sizeof(Store));
		uint32_t ints = save_and_disable_interrupts();
		flash_range_erase(FLASH_OFFSET, FLASH_SECTOR_SIZE);
		flash_range_program(FLASH_OFFSET, sector, FLASH_SECTOR_SIZE);
		restore_interrupts(ints);
	}

	// SLOTS: count, current, then per slot: used, number of loops, 8 loop
	// lengths, name[16]
	void SendSlots()
	{
		static uint8_t msg[4 + 2 + NUM_SLOTS * (2 + MAX_LOOPS + NAME_LEN) + 1];
		int n = sysex::Header(msg, sysex::Slots);
		msg[n++] = NUM_SLOTS;
		msg[n++] = uint8_t(currentSlot);
		for (int s = 0; s < NUM_SLOTS; s++)
		{
			const SlotImage &sl = store.slots[s];
			msg[n++] = sl.used ? 1 : 0;
			// cfg bytes: [1] number of loops, then (length, divider, chaos) x 8
			msg[n++] = sl.used ? uint8_t(sl.cfg[1] & 0x7F) : 0;
			for (int l = 0; l < MAX_LOOPS; l++) msg[n++] = sl.used ? uint8_t(sl.cfg[2 + 3 * l] & 0x7F) : 0;
			for (int i = 0; i < NAME_LEN; i++) msg[n++] = uint8_t(sl.name[i] & 0x7F);
		}
		msg[n++] = 0xF7;
		Write(msg, n);
	}

	static void GetName(char *dst, const uint8_t *p)
	{
		for (int i = 0; i < NAME_LEN; i++) dst[i] = char(p[i] & 0x7F);
		dst[NAME_LEN] = 0;
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
			SendSlots();
			break;
		case sysex::LoadSlot:
			if (len >= 1) LoadSlot(p[0]);
			break;
		case sysex::ClearSlot:
			if (len >= 1 && p[0] < NUM_SLOTS)
			{
				memset(&store.slots[p[0]], 0, sizeof(SlotImage));
				WriteStore();
				UpdateUsedMask();
				SendSlots();
			}
			break;
		case sysex::RenameSlot:
			if (len >= 1 + NAME_LEN && p[0] < NUM_SLOTS && store.slots[p[0]].used)
			{
				char name[NAME_LEN + 1];
				GetName(name, p + 1);
				SetName(store.slots[p[0]].name, name);
				WriteStore();
				SendSlots();
			}
			break;
		case sysex::GetSlots:
			SendSlots();
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
			// [slot, name x 16]; with no payload, the slot that is playing
			int s = len >= 1 ? p[0] : currentSlot;
			if (s >= NUM_SLOTS) break;
			char name[NAME_LEN + 1] = {};
			if (len >= 1 + NAME_LEN) GetName(name, p + 1);
			SaveSlot(s, name);
			uint8_t msg[6];
			int n = sysex::Header(msg, sysex::Saved);
			msg[n++] = uint8_t(s);
			msg[n++] = 0xF7;
			Write(msg, n);
			SendSlots();
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
			uint32_t count = refreshCount;
			QueueConfig(c, true);
			WaitForRefresh(count);
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
	// full: re-read held parameters too (a whole new layout, not an edit)
	void QueueConfig(const Config &c, bool full = false)
	{
		while (pendingEngine) tight_loop_contents();
		fullRefreshRequest = full;
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
		static uint8_t msg[4 + 1 + 2 * CONFIG_BYTES + 6 + 2 * NUM_PARAMS + 1];
		int n = sysex::Header(msg, sysex::State);
		msg[n++] = sysex::kVersion;
		uint8_t b[CONFIG_BYTES];
		Serialise(shadowCfg, b);
		n += sysex::PutNibbles(msg + n, b, CONFIG_BYTES);
		Snapshot s = ReadSnapshot();
		for (int i = 0; i < 3; i++) n += sysex::Put14(msg + n, s.setting[i]);
		// Each parameter's value now, offset by 8192: the editor needs the
		// held ones, which the register alone no longer gives
		for (int i = 0; i < NUM_PARAMS; i++) n += sysex::Put14(msg + n, pubVal[i] + 8192);
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
		out[n++] = uint8_t(currentSlot & 0x7F);
		out[n++] = uint8_t(slotUsedMask & 0x7F);
		out[n++] = 0xF7;
		return n;
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
	int32_t dir2 = 1;  // Osc 2's direction: soft sync flips it
	int32_t svfLow = 0, svfBand = 0;
	Envelope fltEnv, volEnv, fmEnv, pEnv1, pEnv2;
	int32_t sweep1 = 0, sweep2 = 0;  // pitch envelope sweeps, Q16 semitones
	static constexpr int32_t kFmEnvShape = 896; // fixed curve: snappy, like a struck bell
	int32_t volLevel = 0;
	int32_t clockCount = 0, clockPeriod = 6000;
	uint32_t trigPulse = 0, clockPulse = 0;
	uint32_t tickCount = 0, trigCount = 0;
	int32_t probDisplay = 0;
	uint32_t rng = 1;
	uint32_t frame = 0;

	static constexpr int NUM_SMOOTHED = 10;
	const uint8_t smoothedIds[NUM_SMOOTHED] = {P_SHAPE1, P_SHAPE2, P_MIX, P_CUTOFF, P_RES, P_ENVAMT, P_FMOFF, P_FMENVAMT,
		P_P1AMT, P_P2AMT};
	int32_t smooth[NUM_SMOOTHED] = {};
	int32_t fmGate = 0;  // FM On, faded: 0-256 with 8 extra bits

	// Knob settings (Main randomness, X transpose, Y clock rate), 0-4095
	int32_t settings[3] = {2048, 0, 3000};
	bool knobLatched[3] = {true, true, true};
	bool knobStarted[3] = {};
	int32_t lastKnob[3] = {};

	// Shared between cores
	volatile uint64_t pendingReg = 0;
	volatile bool regPending = false;
	volatile int32_t knobRequest[3] = {-1, -1, -1};  // from the editor
	volatile bool fullRefreshRequest = false;        // with the next layout: re-read held parameters
	bool refreshFull = false;
	volatile int32_t pubVal[NUM_PARAMS] = {};        // parameter values, for STATE
	volatile uint32_t refreshCount = 0;
	volatile uint32_t stateSeq = 0;
	Snapshot snap = {};                               // guarded by stateSeq
	volatile uint32_t lastPingUs = 0;
	volatile bool pinged = false;

	// Layout slots. Main selects one with the switch up (audio core);
	// core 1 loads and saves them.
	bool mainSelectsSlot = false;
	int32_t slotZone = -1;
	int slotLedShow = 0;
	bool slotLedEmpty = false;
	uint32_t slotLedUntil = 0;
	volatile int8_t slotRequest = -1;   // audio core -> core 1
	volatile int currentSlot = 0;       // the slot last loaded or saved
	volatile uint8_t slotUsedMask = 1;  // bit n: slot n holds a layout

	// Core 1 only
	Config shadowCfg;    // the layout core 1 last handed to the audio core
	Store store;         // RAM copy of the slots in flash
	uint32_t slotChangedUs = 0;
	bool usbDevice = false;
};

int main()
{
	set_sys_clock_khz(144000, true);
	static Palette card;
	card.StartCore1();
	card.Run();
}
