// SysEx protocol between the CVSeq card and its web editor (web/index.html),
// used when a computer is plugged into the Computer's USB socket and the card
// is acting as a USB MIDI device.
//
// Every message is  F0 7D 43 <cmd> <payload...> F7
// (7D is the MIDI 'non-commercial' manufacturer ID, 43 is 'C').
// All values are 7-bit; wider values are sent as two bytes, high 7 bits first.
//
// Step values are in 8mu fader units, 0-127, sent page by page (8 pages of
// 32 steps): SHAPE 1, SHAPE 2, MORPH, LEVEL, then the second pages START,
// END, QUANTISE, CHANCE.  Page numbers are button (0-3) + 4 for a button's
// second page.  Steps are 0-31, in four banks of eight; the sequence plays
// steps 0 to length-1.
//
// Web -> card
//   HELLO     01                      card replies with STATE
//   SET       03 page step value      one step value (step 0-31)
//   SET_ALL   04 version length values[256]   every step value, and length
//   PAGE      05 page                 page shown on the Computer's LEDs
//   RESET     06                      default sequence; card replies with STATE
//   MOTION    08 pitch(2) roll(2)     8mu tilt, each 0-4095 with 2048 level
//   PING      09                      sent every second; STATUS flows while
//                                     pings keep arriving
//   RESTART   0A                      restart from the first step
//   DIRECTION 0B dir                  0-7, see below
//   BANK      0C bank                 bank the faders edit, 0-3
//   LENGTH    0D length               sequence length, 1-32
//
// Card -> web
//   STATE     02 version page bank length values[256]
//   STATUS    07 cur progress flags flags2 out(2) speed(2) depth offset
//                smooth morph scale bank length
//             cur        step 0-31
//             progress   position through the step, 0-127, in time
//             flags      bits 0-2 direction, bit 3 clocked, bit 4 8mu on
//                        card, bit 5 switch up (knobs are scale, depth and
//                        offset, not rate, smoothing and morph), bit 6 a
//                        knob is waiting
//             flags2     bit 0 this step plays its shape backwards, bit 1
//                        this step lost its chance roll and is holding
//             out        CV Out 1 in millivolts, offset by 8192
//             speed      step rate in 1/256 octave, offset by 8192, where 0
//                        is one step per second
//             depth      -100% to +100%, as 0-127 with 64 = 0
//             offset     -5V to +5V, as 0-127 with 64 = 0V
//             smooth     smoothing setting, 0-127
//             morph      morph offset, as 0-127 with 64 = none
//             scale      0 Off, then 1-15: Chromatic, Major, Minor,
//                        Harmonic minor, Dorian, Phrygian, Lydian,
//                        Mixolydian, Major pentatonic, Minor pentatonic,
//                        Blues, Whole tone, Diminished, Fifths, Octaves
//
// Directions
//   0 step forward     steps 1-8, shapes forwards
//   1 step ping-pong   steps 1-8 then 7-2, shapes forwards
//   2 step backward    steps 8-1, shapes forwards
//   3 true ping-pong   steps 1-8 forwards, then 8-1 with shapes backwards:
//                      the whole sequence played forwards then in reverse
//   4 true reverse     steps 8-1 with shapes backwards: the whole sequence
//                      played in reverse
//   5 random step      a random step each time, never the same twice running,
//                      shapes forwards
//   6 random reverse   steps 1-8, each shape at random forwards or backwards
//   7 true random      a random step, at random forwards or backwards

#ifndef CVSEQ_SYSEX_H
#define CVSEQ_SYSEX_H

#include <stdint.h>

namespace sysex
{

static constexpr uint8_t kMfr = 0x7D;
static constexpr uint8_t kProduct = 0x43;
static constexpr uint8_t kVersion = 2;
static constexpr int kNumValues = 256;

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
	Bank = 0x0C,
	Length = 0x0D,
};

// Collects one SysEx message from a byte stream.  Feed() returns true when
// a complete message for this card has arrived; its command and payload are
// then in cmd, payload and length.
struct Parser
{
	static constexpr int kMax = 300;
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

static constexpr int kStateLen = 4 + 4 + kNumValues + 1;
static constexpr int kStatusLen = 4 + 4 + 2 + 2 + 5 + 2 + 1;

} // namespace sysex

#endif
