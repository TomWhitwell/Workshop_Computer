/*
 * morse_table.h - lookup tables for the Dot Dash card.
 *
 * Two jobs live here:
 *   1. Turn a raw HID keyboard "usage code" (what USB keyboards actually send)
 *      into a normal ASCII character, respecting the Shift key.
 *   2. Turn an ASCII character into its Morse pattern, e.g. 'S' -> "...".
 *
 * Everything is plain integer/character data so it costs almost nothing to
 * look up and is easy to read. Comments explain the codes for anyone opening
 * this file later.
 */

#ifndef MORSE_TABLE_H
#define MORSE_TABLE_H

#include <stdint.h>

// ---------------------------------------------------------------------------
// Shift detection
// ---------------------------------------------------------------------------
// A USB keyboard report carries a "modifier" byte. Bit 1 is Left Shift and
// bit 5 is Right Shift. Either one means the user is holding Shift.
#define DD_MOD_LEFTSHIFT   0x02
#define DD_MOD_RIGHTSHIFT  0x20
#define DD_MOD_SHIFTMASK   (DD_MOD_LEFTSHIFT | DD_MOD_RIGHTSHIFT)

// ---------------------------------------------------------------------------
// HID usage code -> ASCII
// ---------------------------------------------------------------------------
// USB keyboards do not send 'A' or '1'. They send "usage codes": 0x04 is the
// physical key in the position of 'A', 0x1E is the key in the position of '1',
// and so on up to 0x38 for '/'. We translate those into ASCII here.
//
// We support letters, digits, space, and common punctuation. Anything else
// (function keys, arrows, Escape, the keypad) returns 0 and is ignored by the
// caller, because it has no sensible Morse representation.

// Index 0 corresponds to usage code 0x04. Unshifted characters:
static const char kHidUnshifted[53] = {
	'A','B','C','D','E','F','G','H','I','J','K','L','M',   // 0x04-0x10  a-m
	'N','O','P','Q','R','S','T','U','V','W','X','Y','Z',   // 0x11-0x1D  n-z
	'1','2','3','4','5','6','7','8','9','0',               // 0x1E-0x27  1-0
	0,0,0,0,                                               // 0x28-0x2B  Enter Esc Bksp Tab
	' ',                                                   // 0x2C       Space
	'-','=',                                               // 0x2D-0x2E  - =
	0,0,0,0,                                               // 0x2F-0x32  [ ] \ intl
	';','\'',0,                                            // 0x33-0x35  ; ' `
	',','.','/'                                            // 0x36-0x38  , . /
};

// Same keys, but with Shift held. Only the symbols that have a Morse code are
// returned; the rest are 0 (ignored).
static const char kHidShifted[53] = {
	'A','B','C','D','E','F','G','H','I','J','K','L','M',   // letters are unaffected
	'N','O','P','Q','R','S','T','U','V','W','X','Y','Z',
	0,0,0,0,0,0,0,0,0,0,                                   // ! @ # $ % ^ & * ( )
	0,0,0,0,                                               // Enter Esc Bksp Tab
	' ',                                                   // Space
	'_',0,                                                 // _ +
	0,0,0,0,                                               // { } | intl
	':','"',0,                                             // : " ~
	0,0,'?'                                                // < > ?
};

// Translate a HID usage code + shift state into an ASCII character.
// Returns 0 when the key has no Morse-friendly meaning.
inline char hidToAscii(uint8_t keycode, bool shift)
{
	if (keycode < 0x04 || keycode > 0x38)
		return 0;
	uint8_t idx = (uint8_t)(keycode - 0x04);
	return shift ? kHidShifted[idx] : kHidUnshifted[idx];
}

// ---------------------------------------------------------------------------
// Character -> pitch
// ---------------------------------------------------------------------------
// Every letter and digit gets its own note, rising in order so that 'A' is the
// lowest and '9' the highest:
//   A..Z = MIDI 60..85   (A is middle C)
//   0..9 = MIDI 86..95   (0 sits just above Z)
// Returns -1 for anything without its own pitch (punctuation, space). The
// caller keeps the previous note in that case, so a comma or full stop simply
// carries on at the pitch of the character before it.
inline int charToNote(char c)
{
	if (c >= 'a' && c <= 'z')
		c = (char)(c - 32); // fold to upper case
	if (c >= 'A' && c <= 'Z')
		return 60 + (c - 'A');
	if (c >= '0' && c <= '9')
		return 86 + (c - '0');
	return -1;
}

// ---------------------------------------------------------------------------
// ASCII -> Morse
// ---------------------------------------------------------------------------
// Returns a pointer to a static string of '.' (dot) and '-' (dash) characters,
// or nullptr if the character has no Morse code. Lower-case letters are
// accepted too - Morse does not care about case. A space is handled by the
// main state machine (it means "word gap") and returns nullptr here.
inline const char* morseFor(char c)
{
	if (c >= 'a' && c <= 'z')
		c = (char)(c - 32); // fold to upper case

	switch (c)
	{
		// Letters
		case 'A': return ".-";
		case 'B': return "-...";
		case 'C': return "-.-.";
		case 'D': return "-..";
		case 'E': return ".";
		case 'F': return "..-.";
		case 'G': return "--.";
		case 'H': return "....";
		case 'I': return "..";
		case 'J': return ".---";
		case 'K': return "-.-";
		case 'L': return ".-..";
		case 'M': return "--";
		case 'N': return "-.";
		case 'O': return "---";
		case 'P': return ".--.";
		case 'Q': return "--.-";
		case 'R': return ".-.";
		case 'S': return "...";
		case 'T': return "-";
		case 'U': return "..-";
		case 'V': return "...-";
		case 'W': return ".--";
		case 'X': return "-..-";
		case 'Y': return "-.--";
		case 'Z': return "--..";

		// Numbers
		case '0': return "-----";
		case '1': return ".----";
		case '2': return "..---";
		case '3': return "...--";
		case '4': return "....-";
		case '5': return ".....";
		case '6': return "-....";
		case '7': return "--...";
		case '8': return "---..";
		case '9': return "----.";

		// Common punctuation
		case '.': return ".-.-.-";
		case ',': return "--..--";
		case '?': return "..--..";
		case '\'': return ".----.";
		case '/': return "-..-.";
		case '(': return "-.--.";
		case ')': return "-.--.-";
		case ':': return "---...";
		case ';': return "-.-.-.";
		case '=': return "-...-";
		case '-': return "-....-";
		case '_': return "..--.-";
		case '"': return ".-..-.";
		case '@': return ".--.-.";

		default: return nullptr;
	}
}

#endif // MORSE_TABLE_H
