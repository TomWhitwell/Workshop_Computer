// SysEx protocol between the Fader Footie web page and the card.
//
// Every message is  F0 7D 46 cmd payload... F7  (7D: non-commercial
// manufacturer ID, 46: 'F' for Fader Footie).  14-bit values are sent as two
// bytes, high 7 bits first.
//
// Web -> card
//   PING      09                      sent every second; the card counts as
//                                     linked (and sends STATUS) while pings
//                                     keep arriving
//   OUTPUTS   10 cv1(2) cv2(2) d1(2) d2(2)
//             cv1, cv2   CV Out 1/2 in millivolts, offset by 8192
//             d1         ball's distance from team 2's goal (the one team 1
//                        attacks), 0 (in the goal mouth) to 4095 (furthest
//                        point of the table)
//             d2         ball's distance from team 1's goal
//   TOUCH     11 team                 team 0 or 1 touched the ball: a gate
//                                     on Pulse Out 1 or 2
//   GOAL      12 team                 team 0 or 1 scored: a burst of
//                                     triggers on Pulse Out 1 or 2
//   CONFIG    13 burst                triggers in a goal burst, 1-64
//
// Card -> web
//   STATUS    07 flags gate(2) rate smooth
//             flags      bits 0-1 switch (0 down, 1 middle, 2 up)
//             gate       touch gate length in ms (X knob)
//             rate       goal burst rate in Hz (Y knob)
//             smooth     distance smoothing, 0-127 (Main knob)
//   EVENT     20 what
//             1          switch tapped down: serve the ball again
//             2          switch held down for a second: new match
//             3          Pulse In 1: serve the ball again
//             4          Pulse In 2: new match

#ifndef FADER_FOOTIE_SYSEX_H
#define FADER_FOOTIE_SYSEX_H

#include <stdint.h>

namespace sysex
{

static constexpr uint8_t kMfr = 0x7D;
static constexpr uint8_t kProduct = 0x46;

enum Cmd : uint8_t
{
	Status = 0x07,
	Ping = 0x09,
	Outputs = 0x10,
	Touch = 0x11,
	Goal = 0x12,
	Config = 0x13,
	Event = 0x20,
};

enum EventId : uint8_t
{
	EvServe = 1,
	EvNewMatch = 2,
	EvPulseServe = 3,
	EvPulseNewMatch = 4,
};

// Collects one SysEx message from a byte stream.  Feed() returns true when
// a complete message for this card has arrived; its command and payload are
// then in cmd, payload and length.
struct Parser
{
	static constexpr int kMax = 64;
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

static constexpr int kStatusLen = 4 + 1 + 2 + 1 + 1 + 1;
static constexpr int kEventLen = 4 + 1 + 1;

} // namespace sysex

#endif
