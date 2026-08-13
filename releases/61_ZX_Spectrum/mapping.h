// mapping.h — the input-mapping engine.
//
// Turns the Workshop Computer's physical inputs (6 jacks + momentary switch) into
// things the running Spectrum program reads: keyboard-matrix bits, Kempston
// joystick bits, or (later) raw input ports. This is the "patchable control
// surface" idea — the whole point of ZX being a modular instrument rather than a
// Spectrum-in-a-box.
//
// Runs on core 0 (inside ProcessSample), writing gSpectrum.kbd.rows[] and
// xc.kempston, which core 1's ULA/port reads consult. The mapping table is small
// and will later be editable over USB from the Web UI.

#pragma once
#include <cstdint>
#include "spectrum.h"

namespace zx {

// Named Spectrum keys — indices into kKeyTable (spectrum.cpp). Order MUST match
// that table (row 0..7, each row's 5 bits low..high).
enum Keycode : uint8_t {
	KEY_CAPS=0, KEY_Z, KEY_X, KEY_C, KEY_V,
	KEY_A, KEY_S, KEY_D, KEY_F, KEY_G,
	KEY_Q, KEY_W, KEY_E, KEY_R, KEY_T,
	KEY_1, KEY_2, KEY_3, KEY_4, KEY_5,
	KEY_0, KEY_9, KEY_8, KEY_7, KEY_6,
	KEY_P, KEY_O, KEY_I, KEY_U, KEY_Y,
	KEY_ENTER, KEY_L, KEY_K, KEY_J, KEY_H,
	KEY_SPACE, KEY_SYMSHIFT, KEY_M, KEY_N, KEY_B,
	KEY_COUNT,
	KEY_NONE = 0xFF,
};

// The six physical input jacks + the switch = the mapping sources.
enum Source : uint8_t {
	SRC_PULSE1, SRC_PULSE2,   // gates (digital)
	SRC_CV1, SRC_CV2,         // CV (analog, comparator)
	SRC_AUDIO1, SRC_AUDIO2,   // audio-rate (analog, comparator)
	SRC_SWITCH,               // momentary switch (Up/Down = active)
	SRC_COUNT,
};

// What a source drives.
enum TargetKind : uint8_t {
	TGT_NONE,
	TGT_KEY,        // press a Spectrum key (arg = Keycode)
	TGT_KEMPSTON,   // set a Kempston joystick bit (arg = bit mask 000FUDLR)
	TGT_PORT,       // expose the source's analog value (0-255) at a fixed Z80 port
	// --- AY/PT3-mode live mangling (only act when xc.mode == MODE_AY) ---
	TGT_AY_DUTY,    // shape a channel's square duty/PWM (arg = channel 0..2)
	TGT_AY_ENV,     // bend the envelope period (arg ignored)
	TGT_AY_NOISE,   // bend the noise period (arg ignored)
	TGT_AY_MUTE,    // gate-mute a channel (arg = channel 0..2; uses srcActive)
};

// Fixed Z80 port (low byte) each source appears on when mapped to TGT_PORT.
// Readable with IN A,(port).
//
// Chosen so neither PortRead() nor PortWrite() can mistake them for real
// hardware. Every one satisfies ALL of:
//   bit0 = 1 (odd)  -> the ULA only answers on even ports, so never the keyboard
//   bit5 = 1        -> Kempston tests (port & 0x0020) == 0, so never the joystick
//   bit1 = 1        -> the 128K paging latch decodes (port & 0x8002) == 0, i.e.
//                      A15=0 AND A1=0. Keeping A1 high means a stray OUT to one
//                      of these ports can NEVER trigger a bank swap. This one is
//                      the important one: a mis-decoded read returns junk, but a
//                      mis-decoded write silently repages RAM under the program.
//   not 0x5F        -> Knob Y peripheral (see spectrum.cpp)
//   not 0xFD/0xFE   -> AY select/data (0xFFFD/0xBFFD) and the ULA
// Also clear of the common add-ons a .z80 might poke: Interface I (0xEF/0xF7/
// 0xE7), Multiface (0x37), ULAplus (0x3B/0xBB), Currah uSpeech (0x3F), Fuller
// (0x7F), SpecDrum / Kempston mouse (0xDF/0xFB).
//
// Caveat: the AY checks run BEFORE these in both paths, so a high byte of
// 0x80-0xFF is shadowed by the AY regardless of low byte (true of any port on
// this machine). Read them with a high byte below 0x80 — IN A,(n) with a small
// A, or IN r,(C) with B < 0x80.
static constexpr uint8_t kSourcePort[SRC_COUNT] = {
	0x23, // SRC_PULSE1
	0x27, // SRC_PULSE2
	0x2B, // SRC_CV1
	0x2F, // SRC_CV2
	0x33, // SRC_AUDIO1
	0x63, // SRC_AUDIO2
	0x67, // SRC_SWITCH
};

// One mapping: source -> target, with a threshold for analog sources.
struct Mapping {
	TargetKind kind;
	uint8_t    arg;        // Keycode, Kempston bitmask, or AY channel (0..2)
	int16_t    threshold;  // analog: activate when value >= threshold (12-bit signed)
	bool       invert;     // invert the active sense
	uint8_t    depth = 255; // AY continuous-target mangle amount (0=none..255=full)
};

// Kempston joystick bits (port 0x1F: 000FUDLR).
enum : uint8_t {
	KEMP_RIGHT=0x01, KEMP_LEFT=0x02, KEMP_DOWN=0x04, KEMP_UP=0x08, KEMP_FIRE=0x10,
};

// The mapping engine: holds the table, evaluates it each sample.
class Mapper {
public:
	void LoadDefaults();                 // sensible starting patch
	void SetMapping(Source s, const Mapping &m) { table_[s] = m; }
	const Mapping &GetMapping(Source s) const { return table_[s]; }

	// Called each 48kHz sample on core 0. `srcActive[]` = evaluated on/off states
	// (for key/Kempston targets); `srcValue[]` = the 0-255 analog value of each
	// source (for TGT_PORT). Writes kbd.rows[], xc.kempston, and xc.portVal[].
	void Apply(const bool srcActive[SRC_COUNT], const uint8_t srcValue[SRC_COUNT], Spectrum &spec);

	// Keyboard passthrough (laptop keys via the Web UI). These are OR-combined
	// with the jack mappings in Apply(), so both work at once. keyIndex is a
	// kKeyTable index (0..KEY_COUNT-1). Called from core 1 (USB); the bitset is
	// volatile and read by core 0 in Apply().
	void PassthroughKey(uint8_t keyIndex, bool down);
	void PassthroughReset();

private:
	Mapping table_[SRC_COUNT];
	// One bit per matrix key held via passthrough (40 keys -> 2x uint32_t).
	volatile uint32_t passthrough_[2] = {0, 0};
};

} // namespace zx
