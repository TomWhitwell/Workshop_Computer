// SysEx protocol between the Palette card and its web editor (web/index.html),
// used when a computer is plugged into the Computer's USB socket and the card
// is acting as a USB MIDI device.  Same shape as the CVSeq and WaveSeq cards.
//
// Every message is  F0 7D 50 <cmd> <payload...> F7
// (7D is the MIDI 'non-commercial' manufacturer ID, 50 is 'P').
// All values are 7-bit.  14-bit values are two bytes, high 7 bits first.
// Config and register bytes are 8-bit, so they are sent as two nibbles each,
// high nibble first.
//
// Web -> card
//   HELLO      01                         card replies with STATE
//   SET_CONFIG 03 version cfg[157 x 2]    the whole layout (see main.cpp
//                                         Serialise), applied at once
//   SAVE       04                         save layout + register to flash;
//                                         card replies SAVED
//   SET_REG    05 reg[8 x 2]              the 64-bit register, little endian
//   RESET      06                         factory layout; card replies STATE
//   KNOB       0A knob value(2)           set a knob setting from the editor,
//                                         knob 0 Main, 1 X, 2 Y, value 0-4095.
//                                         The physical knob then does nothing
//                                         until it is turned to (or past) the
//                                         new value: soft takeover
//   PING       09                         sent every second; STATUS flows while
//                                         pings keep arriving
//
// Card -> web
//   STATE      02 version cfg[157 x 2] settings[3 x 2]
//   STATUS     07 reg[8 x 2] tick trig flags settings[3 x 2] knobs[3 x 2]
//                 prob note1 note2
//              tick      clock steps so far, low 7 bits (changes = a step)
//              trig      envelope triggers so far, low 7 bits
//              flags     bits 0-1 switch (0 down, 1 middle, 2 up), bit 2
//                        external clock on Pulse In 1, bits 3-5 the Main, X
//                        and Y knob is waiting to pick up its setting
//              settings  Main (randomness), X (transpose), Y (clock rate)
//                        as the card is using them, 0-4095
//              knobs     where the physical knobs are, 0-4095
//              prob      bit flip probability, 0-127 (127 = always)
//              note1/2   MIDI notes the oscillators are heading for,
//                        transpose and CV In 1 included
//   SAVED      08                         flash write finished

#ifndef PALETTE_SYSEX_H
#define PALETTE_SYSEX_H

#include <stdint.h>

namespace sysex
{

static constexpr uint8_t kMfr = 0x7D;
static constexpr uint8_t kProduct = 0x50;
static constexpr uint8_t kVersion = 2;

enum Cmd : uint8_t
{
	Hello = 0x01,
	State = 0x02,
	SetConfig = 0x03,
	Save = 0x04,
	SetReg = 0x05,
	Reset = 0x06,
	Status = 0x07,
	Saved = 0x08,
	Ping = 0x09,
	Knob = 0x0A,
};

// Collects one SysEx message from a byte stream.  Feed() returns true when
// a complete message for this card has arrived; its command and payload are
// then in cmd, payload and length.
struct Parser
{
	static constexpr int kMax = 400;
	uint8_t buf[kMax];
	int n = 0;
	bool in = false;

	uint8_t cmd = 0;
	const uint8_t *payload = nullptr;
	int length = 0;

	bool Feed(uint8_t b)
	{
		if (b == 0xF0)
		{
			in = true;
			n = 0;
			return false;
		}
		if (!in) return false;
		if (b == 0xF7)
		{
			in = false;
			if (n < 3 || buf[0] != kMfr || buf[1] != kProduct) return false;
			cmd = buf[2];
			payload = buf + 3;
			length = n - 3;
			return true;
		}
		if (b & 0x80)
		{
			// Any other status byte aborts the message, except real-time
			// bytes, which may legally appear inside SysEx
			if (b < 0xF8) in = false;
			return false;
		}
		if (n < kMax) buf[n++] = b;
		else in = false;
		return false;
	}
};

inline int Header(uint8_t *out, uint8_t cmd)
{
	out[0] = 0xF0;
	out[1] = kMfr;
	out[2] = kProduct;
	out[3] = cmd;
	return 4;
}

inline int Put14(uint8_t *out, int32_t v)
{
	if (v < 0) v = 0;
	if (v > 16383) v = 16383;
	out[0] = uint8_t(v >> 7);
	out[1] = uint8_t(v & 0x7F);
	return 2;
}

inline int32_t Get14(const uint8_t *in)
{
	return (int32_t(in[0] & 0x7F) << 7) | (in[1] & 0x7F);
}

inline int PutNibbles(uint8_t *out, const uint8_t *in, int n)
{
	for (int i = 0; i < n; i++) { out[2 * i] = in[i] >> 4; out[2 * i + 1] = in[i] & 0x0F; }
	return 2 * n;
}

inline void GetNibbles(uint8_t *out, const uint8_t *in, int n)
{
	for (int i = 0; i < n; i++) out[i] = uint8_t(((in[2 * i] & 0x0F) << 4) | (in[2 * i + 1] & 0x0F));
}

static constexpr int kStatusPayload = 16 + 1 + 1 + 1 + 6 + 6 + 1 + 2;

} // namespace sysex

#endif
