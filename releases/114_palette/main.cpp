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

  Panel
    Main knob   Randomness (Turing): noon = random, CW = locked,
                CCW = locked + inverted (pattern repeats at twice the length)
    X knob      Transpose, 0 – +24 semitones
    Y knob      Internal clock rate (used when Pulse In 1 is unplugged)
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
    Core 1 runs USB-MIDI: SysEx from the web editor, and flash saves.

  Code written with AI assistance (Claude), based on Chris Johnson's
  ComputerCard web_interface example.
*/

#include "ComputerCard.h"
#include "pico/multicore.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "tusb.h"
#include <cmath>
#include <cstring>
#include <cstddef>
#include <cstdlib>

////////////////////////////////////////////////////////////////////////////////
// Firmware / protocol versions

constexpr uint8_t FW_MAJOR = 0, FW_MINOR = 1, FW_PATCH = 0;
constexpr uint8_t CONFIG_FORMAT = 1;   // bump when the serialised layout changes

constexpr int MAX_LOOPS = 8;
constexpr int NUM_PARAMS = 16;
constexpr int REG_BITS = 64;
constexpr int MAX_WIDTH = 16;

enum ParamId
{
	P_TRIG = 0, P_PITCH1, P_PITCH2, P_SLEW, P_SHAPE1, P_SHAPE2, P_MIX,
	P_CUTOFF, P_RES, P_ENVAMT, P_FATK, P_FDEC, P_FSHAPE, P_VATK, P_VDEC, P_VSHAPE
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
constexpr int CONFIG_BYTES = 2 + MAX_LOOPS * 3 + 3 + NUM_PARAMS * 8; // 157

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
//   [29..156] 16 x (enabled, start, width, flags, minLo, minHi, maxLo, maxHi)
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
constexpr uint32_t FLASH_MAGIC = 0x50414C31; // "PAL1"

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
		slewCoef[i] = int32_t(65536.0f * (1.0f - expf(-1.0f / (tc * 48000.0f))));
		if (slewCoef[i] < 1) slewCoef[i] = 1;
	}
}

////////////////////////////////////////////////////////////////////////////////
// Small DSP helpers (fixed point; no float in the audio path)

static inline int32_t Mul64(int32_t a, int32_t b, int shift)
{
	return int32_t((int64_t(a) * b) >> shift);
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
			else level = startLevel + (((65535 - startLevel) * Curve(phase >> 8, c)) >> 16);
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
// USB-MIDI SysEx plumbing (adapted from ComputerCard's web_interface example)

class WebInterfaceComputerCard : public ComputerCard
{
public:
	void StartUSBCore() { instance = this; multicore_launch_core1(core1); }
	static void core1() { instance->USBCore(); }

	void MIDIStreamWriteBlocking(uint8_t cable, uint8_t const *data, uint32_t size)
	{
		uint32_t sent = 0;
		while (sent < size)
		{
			uint32_t n = tud_midi_stream_write(cable, data + sent, size - sent);
			sent += n;
			if (!n)
			{
				tud_task();
				if (!tud_midi_mounted()) return; // host went away mid-message
			}
		}
	}

	void SendSysEx(const uint8_t *data, uint32_t size)
	{
		if (!tud_midi_mounted()) return;
		uint8_t header[] = {0xF0, MIDI_MANUFACTURER_ID};
		uint8_t footer[] = {0xF7};
		MIDIStreamWriteBlocking(0, header, 2);
		MIDIStreamWriteBlocking(0, data, size);
		MIDIStreamWriteBlocking(0, footer, 1);
	}

	void USBCore()
	{
		sysexActive = false;
		sysexLen = 0;
		tusb_init();
		while (1)
		{
			tud_task();
			while (tud_midi_available())
			{
				uint32_t n = tud_midi_stream_read(rxBuf, rxBufSize);
				if (n > 0) ParseMIDIBytes(rxBuf, n);
			}
			MIDICore();
		}
	}

	void ParseMIDIBytes(uint8_t *buf, unsigned n)
	{
		for (unsigned i = 0; i < n; i++)
		{
			uint8_t b = buf[i];
			if (!sysexActive)
			{
				if (b == 0xF0) { sysexActive = true; sysexLen = 0; sysexBuf[sysexLen++] = b; }
			}
			else
			{
				if (sysexLen < sysexBufSize) sysexBuf[sysexLen++] = b;
				if (b == 0xF7)
				{
					if (sysexLen >= 3 && sysexBuf[1] == MIDI_MANUFACTURER_ID)
						ProcessIncomingSysEx(sysexBuf + 2, sysexLen - 3);
					sysexActive = false;
					sysexLen = 0;
				}
			}
		}
	}

	virtual void MIDICore() {}
	virtual void ProcessIncomingSysEx(uint8_t *, uint32_t) {}

private:
	static constexpr unsigned sysexBufSize = 512;
	static constexpr uint8_t MIDI_MANUFACTURER_ID = 0x7D; // prototyping / private use
	static constexpr unsigned rxBufSize = 64;
	bool sysexActive = false;
	unsigned sysexLen = 0;
	uint8_t sysexBuf[sysexBufSize];
	uint8_t rxBuf[rxBufSize];
	static WebInterfaceComputerCard *instance;
};
WebInterfaceComputerCard *WebInterfaceComputerCard::instance = nullptr;

// SysEx message IDs (first data byte). E = editor, F = firmware.
constexpr uint8_t MSG_HELLO = 0x01;        // E->F  [maj, min, patch]
constexpr uint8_t MSG_VERSION = 0x02;      // F->E  [maj, min, patch, cfgFormat, maxLoops, numParams]
constexpr uint8_t MSG_SET_CONFIG = 0x10;   // E->F  [config bytes as nibbles]
constexpr uint8_t MSG_ACK = 0x11;          // F->E  [request id, status 0 = ok]
constexpr uint8_t MSG_GET_CONFIG = 0x12;   // E->F
constexpr uint8_t MSG_CONFIG = 0x13;       // F->E  [config bytes as nibbles]
constexpr uint8_t MSG_SAVE = 0x14;         // E->F  save running config + register to flash
constexpr uint8_t MSG_SET_REG = 0x16;      // E->F  [8 register bytes as nibbles]
constexpr uint8_t MSG_DEFAULTS = 0x17;     // E->F  load factory defaults (not saved)
constexpr uint8_t MSG_STATE = 0x20;        // F->E  [reg nibbles x16, flags, prob 0..127]

// SysEx can only carry 7-bit bytes, so each 8-bit byte is sent as two nibbles
static void ToNibbles(const uint8_t *in, int n, uint8_t *out)
{
	for (int i = 0; i < n; i++) { out[2 * i] = in[i] >> 4; out[2 * i + 1] = in[i] & 0x0F; }
}
static void FromNibbles(const uint8_t *in, int n, uint8_t *out)
{
	for (int i = 0; i < n; i++) out[i] = uint8_t(((in[2 * i] & 0x0F) << 4) | (in[2 * i + 1] & 0x0F));
}

////////////////////////////////////////////////////////////////////////////////
// The card

class Palette : public WebInterfaceComputerCard
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
		ApplyConfig();
	}

protected:
	////////////////////////////////////////////////////////////////////////////
	// Audio core

	uint32_t Rand() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

	// Recompute everything derived from cfg. Runs on the audio core only.
	void ApplyConfig()
	{
		int offset = 0;
		usedMask = 0;
		for (int i = 0; i < cfg.numLoops; i++)
		{
			loopOffset[i] = uint8_t(offset);
			loopMask[i] = BitMask(cfg.loops[i].length);
			loopChaosQ8[i] = (cfg.loops[i].chaos * 256 + 50) / 100;
			usedMask |= loopMask[i] << offset;
			offset += cfg.loops[i].length;
			if (loopCounter[i] >= cfg.loops[i].divider) loopCounter[i] = 0;
		}
		for (int i = 0; i < NUM_PARAMS; i++)
			paramMask[i] = BitMask(cfg.params[i].width) << cfg.params[i].start;

		// Build the scale: list of semitone offsets present in scaleMask
		scaleLen = 0;
		for (int s = 0; s < 12; s++)
			if (cfg.scaleMask & (1 << s)) scaleNotes[scaleLen++] = int8_t(s);

		UpdateParams(0);
	}

	// Read one parameter's bits and scale them into its Min..Max range
	int32_t ParamValue(int i)
	{
		const ParamCfg &p = cfg.params[i];
		if (!p.enabled) return p.min;
		uint64_t bits = (reg >> p.start) & BitMask(p.width);
		// x = normalised value, 0..65535
		uint32_t x;
		if (p.flags & FLAG_POPCOUNT)
			x = (Popcount64(bits) * 65535u) / p.width;
		else
			x = p.width == 16 ? uint32_t(bits) : (uint32_t(bits) * 65535u) / uint32_t((1u << p.width) - 1);
		if (p.flags & FLAG_INVERT) x = 65535 - x;
		return p.min + ((int32_t(p.max - p.min) * int32_t(x) + 32768) >> 16);
	}

	// Convert a (possibly negative) scale degree into a MIDI note
	int32_t DegreeToNote(int32_t deg)
	{
		int32_t oct = 0;
		while (deg < 0) { deg += scaleLen; oct--; }
		while (deg >= scaleLen) { deg -= scaleLen; oct++; }
		return cfg.root + oct * 12 + scaleNotes[deg];
	}

	// Called on every clock tick, with the mask of bits that just moved
	void UpdateParams(uint64_t moved)
	{
		for (int i = 0; i < NUM_PARAMS; i++) paramVal[i] = ParamValue(i);

		int32_t deg1 = paramVal[P_PITCH1];
		note1 = DegreeToNote(deg1);
		note2 = DegreeToNote(deg1 + paramVal[P_PITCH2]);

		// The trigger is only evaluated when its own bits have moved (so a
		// slow loop doesn't fire repeatedly on unchanged bits). Unassigned,
		// it fires on every tick if its Min is in the top half.
		if (moved)
		{
			bool relevant = cfg.params[P_TRIG].enabled ? (moved & paramMask[P_TRIG]) != 0 : true;
			if (relevant && paramVal[P_TRIG] >= 512) FireEnvelopes();
		}
	}

	void FireEnvelopes()
	{
		fltEnv.Trigger();
		volEnv.Trigger();
		trigPulse = 240; // 5 ms
		trigFlag = true;
	}

	// Advance the shift register by one clock tick
	void Tick()
	{
		// Flip probability from Main knob + CV 2 (Turing Machine style).
		// probQ16 = 65536 means "always flip", 0 means "never flip".
		int32_t k = Clamp(KnobVal(Main) + CVIn2(), 0, 4095);
		int32_t probQ16;
		if (k < 150) probQ16 = 65536;
		else if (k > 3945) probQ16 = 0;
		else probQ16 = ((3945 - k) * 17685) >> 10;
		if (SwitchVal() == Up) probQ16 = 0; // lock
		probDisplay = probQ16;

		uint64_t moved = 0;
		for (int i = 0; i < cfg.numLoops; i++)
		{
			if (++loopCounter[i] < cfg.loops[i].divider) continue;
			loopCounter[i] = 0;

			int len = cfg.loops[i].length, off = loopOffset[i];
			uint64_t m = loopMask[i];
			uint64_t seg = (reg >> off) & m;
			uint32_t top = uint32_t(seg >> (len - 1)) & 1;
			int32_t p = (probQ16 * loopChaosQ8[i]) >> 8;
			if (int32_t(Rand() >> 16) < p) top ^= 1;
			seg = ((seg << 1) | top) & m;
			reg = (reg & ~(m << off)) | (seg << off);
			moved |= m << off;
		}
		UpdateParams(moved);
		clockPulse = 240;
		tickCount++;
	}

	virtual void ProcessSample()
	{
		// Pick up changes sent from the editor (core 1) between samples
		if (configPending) { cfg = pendingCfg; __dmb(); configPending = false; ApplyConfig(); }
		if (regPending) { reg = pendingReg; __dmb(); regPending = false; UpdateParams(0); }

		// Z down: scramble the register
		if (SwitchChanged() && SwitchVal() == Down)
		{
			uint64_t r = (uint64_t(Rand()) << 32) | Rand();
			reg = (reg & ~usedMask) | (r & usedMask);
			UpdateParams(0);
		}

		// Clock: external on Pulse In 1 if patched, otherwise Y knob
		if (Connected(Input::Pulse1))
		{
			if (PulseIn1RisingEdge()) Tick();
		}
		else
		{
			// Y: ~1 Hz (CCW) to ~16 Hz (CW), exponential
			int32_t yq = KnobVal(Y) * 4;
			int32_t base = 48000 >> (yq >> 12);
			int32_t period = base - ((base * (yq & 4095)) >> 13);
			if (++clockCount >= period) { clockCount = 0; Tick(); }
		}
		if (PulseIn2RisingEdge()) FireEnvelopes();

		// Smooth the continuous parameters (they jump on each tick) to avoid
		// zipper clicks: ~3 ms one-pole, values kept at 8 extra bits.
		for (int j = 0; j < NUM_SMOOTHED; j++)
		{
			int i = smoothedIds[j];
			smooth[j] += ((paramVal[i] << 8) - smooth[j]) >> 7;
		}
		int32_t shape1 = smooth[0] >> 8, shape2 = smooth[1] >> 8, mix = smooth[2] >> 8;
		int32_t cutoff = smooth[3] >> 8, res = smooth[4] >> 8, envAmt = smooth[5] >> 8;

		// Pitch: scale note + X transpose + CV 1 (1V/oct, semitone-quantised)
		int32_t transpose = (KnobVal(X) * 25) >> 12;
		int32_t cvSemis = (CVIn1() * 72 + (CVIn1() >= 0 ? 1024 : -1024)) / 2048;
		int32_t t1 = Clamp(note1 + transpose + cvSemis, 0, 127) << 16;
		int32_t t2 = Clamp(note2 + transpose + cvSemis, 0, 127) << 16;
		int32_t k = slewCoef[Clamp(paramVal[P_SLEW], 0, 1023)];
		pitch1 += Mul64(t1 - pitch1, k, 16);
		pitch2 += Mul64(t2 - pitch2, k, 16);

		// Oscillators
		phase1 += PitchToInc(pitch1);
		phase2 += PitchToInc(pitch2);
		int32_t o1 = Wave(phase1, shape1);
		int32_t o2 = Wave(phase2, shape2);
		int32_t osc = ((o1 * (1023 - mix) + o2 * mix) >> 10) >> 1; // ±16384

		// Envelopes
		int32_t fe = fltEnv.Process(paramVal[P_FATK], paramVal[P_FDEC], paramVal[P_FSHAPE]);
		int32_t ve = volEnv.Process(paramVal[P_VATK], paramVal[P_VDEC], paramVal[P_VSHAPE]);

		// Filter: Chamberlin state-variable low-pass, run twice per sample
		int32_t cut = Clamp(cutoff + ((envAmt * fe) >> 16), 0, 1023);
		int32_t f = cutoffCoef[cut];
		int32_t q = 32768 - res * 31; // damping, Q14: 2.0 (none) .. ~0.06 (screaming)
		for (int n = 0; n < 2; n++)
		{
			svfLow += Mul64(f, svfBand, 15);
			int32_t high = osc - svfLow - Mul64(q, svfBand, 14);
			svfBand += Mul64(f, high, 15);
			// Hard limits keep high resonance stable (and add some grit)
			svfBand = Clamp(svfBand, -262144, 262143);
			svfLow = Clamp(svfLow, -262144, 262143);
		}

		// Soft-ish saturation, then VCA
		int32_t y = svfLow;
		if (y > 20000) y = 20000 + ((y - 20000) >> 3);
		else if (y < -20000) y = -20000 + ((y + 20000) >> 3);
		y = Mul64(y, ve, 16) >> 3;
		AudioOut1(Clamp(y, -2047, 2047));
		AudioOut2(fe >> 5);

		// CV outs: calibrated pitch (MIDI 60 = 0 V) and volume envelope
		CVOut1Millivolts(int32_t((int64_t(pitch1 - (60 << 16)) * 5333) >> 22));
		CVOut2(ve >> 5);

		// Pulse outs: envelope trigger and clock, 5 ms each
		PulseOut1(trigPulse > 0);
		PulseOut2(clockPulse > 0);
		if (trigPulse) trigPulse--;
		if (clockPulse) clockPulse--;

		// LEDs: bits 0..3 of the register, clock, volume envelope
		uint32_t lo = uint32_t(reg);
		for (int i = 0; i < 4; i++) LedOn(i, (lo >> i) & 1);
		LedOn(4, clockPulse > 0);
		LedBrightness(5, ve >> 4);

		// Publish a snapshot for the editor (seqlock: odd = being written)
		stateSeq++; __dmb();
		stateReg = reg; stateTick = tickCount; stateProb = probDisplay;
		__dmb(); stateSeq++;
	}

	static inline uint32_t PitchToInc(int32_t pitchQ16)
	{
		int32_t i = pitchQ16 >> 16;
		int32_t frac = pitchQ16 & 0xFFFF;
		uint32_t a = noteInc[i], b = noteInc[i + 1];
		return a + uint32_t((int64_t(b - a) * frac) >> 16);
	}

	////////////////////////////////////////////////////////////////////////////
	// USB core

	void ProcessIncomingSysEx(uint8_t *data, uint32_t size) override
	{
		if (size < 1) return;
		uint8_t cmd = data[0];
		if (cmd == MSG_HELLO)
		{
			uint8_t v[] = {MSG_VERSION, FW_MAJOR, FW_MINOR, FW_PATCH, CONFIG_FORMAT, MAX_LOOPS, NUM_PARAMS};
			SendSysEx(v, sizeof(v));
		}
		else if (cmd == MSG_SET_CONFIG)
		{
			uint8_t ok = 1;
			if (size == 1 + 2 * CONFIG_BYTES)
			{
				uint8_t b[CONFIG_BYTES];
				FromNibbles(data + 1, CONFIG_BYTES, b);
				Config c;
				if (Deserialise(c, b)) { QueueConfig(c); ok = 0; }
			}
			uint8_t a[] = {MSG_ACK, cmd, ok};
			SendSysEx(a, sizeof(a));
		}
		else if (cmd == MSG_GET_CONFIG)
		{
			SendConfig();
		}
		else if (cmd == MSG_SAVE)
		{
			SaveToFlash();
			uint8_t a[] = {MSG_ACK, cmd, 0};
			SendSysEx(a, sizeof(a));
		}
		else if (cmd == MSG_SET_REG && size == 1 + 16)
		{
			uint8_t b[8];
			FromNibbles(data + 1, 8, b);
			uint64_t r = 0;
			for (int i = 7; i >= 0; i--) r = (r << 8) | b[i];
			while (regPending) tight_loop_contents();
			pendingReg = r; __dmb(); regPending = true;
		}
		else if (cmd == MSG_DEFAULTS)
		{
			Config c;
			DefaultConfig(c);
			ValidateConfig(c);
			QueueConfig(c);
			SendConfig();
		}
	}

	// Hand a new config to the audio core, and keep a copy for GET_CONFIG
	void QueueConfig(const Config &c)
	{
		while (configPending) tight_loop_contents();
		pendingCfg = c;
		shadowCfg = c;
		__dmb();
		configPending = true;
	}

	void SendConfig()
	{
		Config c = shadowCfg;
		uint8_t b[CONFIG_BYTES];
		Serialise(c, b);
		static uint8_t msg[1 + 2 * CONFIG_BYTES];
		msg[0] = MSG_CONFIG;
		ToNibbles(b, CONFIG_BYTES, msg + 1);
		SendSysEx(msg, sizeof(msg));
	}

	uint64_t ReadStateReg(uint32_t &tick, int32_t &prob)
	{
		uint32_t s;
		uint64_t r;
		do
		{
			s = stateSeq; __dmb();
			r = stateReg; tick = stateTick; prob = stateProb;
			__dmb();
		} while ((s & 1) || s != stateSeq);
		return r;
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
		uint32_t tick; int32_t prob;
		img.reg = ReadStateReg(tick, prob);
		img.checksum = Checksum(reinterpret_cast<const uint8_t *>(&img), offsetof(FlashImage, checksum));
		memcpy(sector, &img, sizeof(img));

		uint32_t ints = save_and_disable_interrupts();
		flash_range_erase(FLASH_OFFSET, FLASH_SECTOR_SIZE);
		flash_range_program(FLASH_OFFSET, sector, FLASH_SECTOR_SIZE);
		restore_interrupts(ints);
	}

	// Stream the live register to the editor, at most ~50 times a second
	void MIDICore() override
	{
		if (!tud_midi_mounted()) return;
		uint32_t now = time_us_32();
		if (now - lastStateSend < 20000) return;
		uint32_t tick; int32_t prob;
		uint64_t r = ReadStateReg(tick, prob);
		bool trig = trigFlag;
		if (tick == lastSentTick && r == lastSentReg && !trig && now - lastStateSend < 250000) return;
		trigFlag = false;
		lastStateSend = now;
		lastSentTick = tick;
		lastSentReg = r;

		uint8_t b[8];
		for (int i = 0; i < 8; i++) b[i] = uint8_t(r >> (8 * i));
		uint8_t msg[1 + 16 + 2];
		msg[0] = MSG_STATE;
		ToNibbles(b, 8, msg + 1);
		msg[17] = trig ? 1 : 0;
		msg[18] = uint8_t(Clamp(prob >> 9, 0, 127));
		SendSysEx(msg, sizeof(msg));
	}

private:
	// Running state (audio core)
	Config cfg;
	uint64_t reg = 0;
	uint64_t usedMask = 0;
	uint64_t loopMask[MAX_LOOPS] = {};
	uint64_t paramMask[NUM_PARAMS] = {};
	uint8_t loopOffset[MAX_LOOPS] = {};
	uint8_t loopCounter[MAX_LOOPS] = {};
	int32_t loopChaosQ8[MAX_LOOPS] = {};
	int8_t scaleNotes[12] = {};
	int32_t scaleLen = 12;
	int32_t paramVal[NUM_PARAMS] = {};
	int32_t note1 = 48, note2 = 48;
	int32_t pitch1 = 48 << 16, pitch2 = 48 << 16;
	uint32_t phase1 = 0, phase2 = 0;
	int32_t svfLow = 0, svfBand = 0;
	Envelope fltEnv, volEnv;
	int32_t clockCount = 0;
	uint32_t trigPulse = 0, clockPulse = 0;
	uint32_t tickCount = 0;
	int32_t probDisplay = 0;
	uint32_t rng = 1;

	static constexpr int NUM_SMOOTHED = 6;
	const uint8_t smoothedIds[NUM_SMOOTHED] = {P_SHAPE1, P_SHAPE2, P_MIX, P_CUTOFF, P_RES, P_ENVAMT};
	int32_t smooth[NUM_SMOOTHED] = {};

	// Shared between cores
	Config pendingCfg;
	volatile bool configPending = false;
	volatile uint64_t pendingReg = 0;
	volatile bool regPending = false;
	volatile uint32_t stateSeq = 0;
	volatile uint64_t stateReg = 0;
	volatile uint32_t stateTick = 0;
	volatile int32_t stateProb = 0;
	volatile bool trigFlag = false;

	// USB core only
	Config shadowCfg; // the config core 1 last handed to the audio core
	uint32_t lastStateSend = 0;
	uint32_t lastSentTick = 0xFFFFFFFF;
	uint64_t lastSentReg = 0;
};

int main()
{
	set_sys_clock_khz(144000, true);
	static Palette card;
	card.StartUSBCore();
	card.Run();
}
