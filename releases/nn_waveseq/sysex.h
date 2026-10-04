// SysEx protocol between the Wave Sequencer card and its web editor
// (web/index.html), used when a computer is plugged into the Computer's USB
// socket and the card is acting as a USB MIDI device.
//
// Every message is  F0 7D 57 <cmd> <payload...> F7
// (7D is the MIDI 'non-commercial' manufacturer ID, 57 is 'W').
// All values are 7-bit; wider values are sent as two bytes, high 7 bits first.
//
// Step values are in 8mu fader units, 0-127, sent page by page:
// WAVE 1-8, TIME 1-8, PITCH 1-8, LEVEL 1-8.
//
// Web -> card
//   HELLO    01                      card replies with STATE
//   SET      03 page step value      one step value
//   SET_ALL  04 version values[32]   every step value
//   PAGE     05 page                 page shown on the Computer's LEDs
//   RESET    06                      default sequence; card replies with STATE
//   MOTION   08 pitch(2) roll(2)     8mu tilt, each 0-4095 with 2048 level
//   PING     09                      sent every second; STATUS flows while
//                                    pings keep arriving
//   RESTART  0A                      restart from the first step
//   DIRECTION 0B dir                 0 forward, 1 ping-pong, 2 random
//
// Card -> web
//   STATE    02 version page values[32]
//   STATUS   07 cur next mix progress flags note(2) speed(2) xfade fm scan
//            cur, next   steps 0-7
//            mix         crossfade into next, 0-127
//            progress    position through the current step, 0-127
//            flags       bits 0-1 direction (0 forward, 1 ping-pong,
//                        2 random), bit 2 clocked, bit 3 8mu on card,
//                        bit 4 switch up (X/Y are speed and crossfade, not
//                        FM and scan), bit 5 a knob is waiting to pick up
//            note        base pitch in 1/8 semitones (MIDI note * 8)
//            speed       speed in 1/256 octave, offset by 2048
//            xfade       crossfade setting, 0-127
//            fm, scan    FM and wave scan amount settings, 0-127

#ifndef WAVESEQ_SYSEX_H
#define WAVESEQ_SYSEX_H

#include <stdint.h>

namespace sysex
{

static constexpr uint8_t kMfr = 0x7D;
static constexpr uint8_t kProduct = 0x57;
static constexpr uint8_t kVersion = 2;
static constexpr int kNumValues = 32;

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
	Restart = 0x0A,
	Direction = 0x0B,
};

// Collects one SysEx message from a byte stream.  Feed() returns true when
// a complete message for this card has arrived; its command and payload are
// then in cmd, payload and length.
struct Parser
{
	static constexpr int kMax = 48;
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

static constexpr int kStateLen = 4 + 2 + kNumValues + 1;
static constexpr int kStatusLen = 4 + 5 + 2 + 2 + 3 + 1;

} // namespace sysex

#endif
