// SysEx protocol between the EightTrack card and its web editor
// (web/index.html), used when a computer is plugged into the Computer's USB
// socket and the card is acting as a USB MIDI device.
//
// Every message is  F0 7D 45 <cmd> <payload...> F7
// (7D is the MIDI 'non-commercial' manufacturer ID, 45 is 'E').
// All values are 7-bit; wider values are sent as two bytes, high 7 bits first.
//
// Settings are in 8mu fader units, 0-127, sent page by page (6 pages of 8,
// one value per fader), numbered
//   0 TIME   1 VOLUME   2 PAN   3 WIDTH   4 FEEDBACK   5 FX
// On the 8mu, A is page 0, B 1, C 2 then 3, and D 4 then 5.
// PAN is two-sided, 64 the centre.  The eight FX values are
//   0 low-pass   1 high-pass   2 saturation   3 hiss
//   4 wow        5 flutter     6 dropouts     7 mix (0 dry, 64 both, 127 wet)
// A preset is the 48 values, page 0 first.
//
// Web -> card
//   HELLO      01                         card replies with STATE
//   SET        03 page head value         one value (head 0-7)
//   SET_ALL    04 version values[48]      every value
//   PAGE       05 page                    page shown on the Computer's LEDs
//   RESET      06                         default settings; card replies STATE
//   MOTION     08 pitch(2) roll(2)        8mu tilt, each 0-4095, 2048 level
//   PING       09                         sent every second; STATUS flows
//                                         while pings keep arriving
//   FREEZE     0A on                      0 or 1, ORed with the switch
//   TAP        0B                         one tap of tap tempo
//   SAVE       0C slot                    store the settings in slot 0-7 of
//                                         the card's flash
//   LOAD       0D slot                    load slot 0-7; card replies STATE
//   GET_SLOTS  0E                         card replies SLOTS
//   STORE      10 slot values[48]         write a preset into slot 0-7
//                                         without loading it
//   ERASE      11 slot                    empty slot 0-7
//
// Card -> web
//   STATE      02 version page slot maxms(2) values[48]
//              slot   last slot saved or loaded, 0-7, or 127 for none
//              maxms  the longest delay the tape holds, in ms
//   STATUS     07 flags ratio beat(2) mult(2) macroX macroY
//                 times[8](2 each) inL inR outL outR page slot
//              flags   bit 0 tempo synced, bit 1 a jack is in Pulse In 1,
//                      bit 2 synced to tap tempo, bit 3 frozen, bit 4 8mu
//                      on the card, bit 5 the switch is held down
//              ratio   the Main knob's clock ratio while synced, 0-8:
//                      /4 /3 /2 x2/3 x1 x3/2 x2 x3 x4
//              beat    the beat (clock or tap period) in ms
//              mult    the Main knob's time multiplier while free, in 1/256
//                      octave offset by 8192 (0 = x1)
//              macroX  X knob + CV In 1, 0-127 for x0 to x2 (64 = x1)
//              macroY  Y knob + CV In 2, the same
//              times   each head's delay right now, in ms (it glides)
//              in/out  peak levels, 0-127
//   SLOTS      0F valid[8] values[8][48]

#ifndef EIGHTTRACK_SYSEX_H
#define EIGHTTRACK_SYSEX_H

#include <stdint.h>

namespace sysex
{

static constexpr uint8_t kMfr = 0x7D;
static constexpr uint8_t kProduct = 0x45;
static constexpr uint8_t kVersion = 1;
static constexpr int kHeads = 8;
static constexpr int kPages = 6;
static constexpr int kNumValues = kPages * kHeads;
static constexpr int kSlots = 8;
static constexpr uint8_t kNoSlot = 127;

enum Cmd : uint8_t
{
	Hello = 0x01,
	State = 0x02,
	Set = 0x03,
	SetAll = 0x04,
	Page = 0x05,
	Reset = 0x06,
	Status = 0x07,
	Motion = 0x08,
	Ping = 0x09,
	Freeze = 0x0A,
	Tap = 0x0B,
	Save = 0x0C,
	Load = 0x0D,
	GetSlots = 0x0E,
	Slots = 0x0F,
	Store = 0x10,
	Erase = 0x11,
};

// Collects one SysEx message from a byte stream.  Feed() returns true when
// a complete message for this card has arrived; its command and payload are
// then in cmd, payload and length.
struct Parser
{
	static constexpr int kMax = 128;
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

static constexpr int kStateLen = 4 + 5 + kNumValues + 1;
static constexpr int kStatusLen = 4 + 2 + 2 + 2 + 2 + 2 * kHeads + 4 + 2 + 1;
static constexpr int kSlotsLen = 4 + kSlots + kSlots * kNumValues + 1;

} // namespace sysex

#endif
