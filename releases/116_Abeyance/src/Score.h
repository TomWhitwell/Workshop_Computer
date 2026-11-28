#pragma once
#include <cstdint>

namespace ab
{

// The music itself: 13 loops and 9 scales.
//
// Each loop stores scale steps, not notes, so a new piece can move it to
// any root and scale. A step is written as a "degree": 0 is the root, 1 the
// 2nd, ... 6 the 7th, 7 the root an octave up; -7 the root an octave down.
// So the guide's "low 5" is -3 and "high 7" is 13.
//
// The opening piece (root D-flat 4, major, tempo 1) gives exactly the notes
// in the guide's table. The choir's pitches and lengths follow Eno's 2/1;
// the piano phrases are new, written in D-flat so the two layers share the
// same five notes (D-flat, E-flat, F, A-flat, C -- steps 1, 2, 3, 5, 7).

constexpr int32_t kPiano = 0;
constexpr int32_t kChoir = 1;
constexpr int32_t kLoops = 13;

struct LoopDef
{
	uint8_t layer;
	uint8_t count;	   // notes in the phrase (choir loops: 1)
	int8_t degree[3];  // the phrase, in order
	uint16_t lengthCs; // the original loop length, centiseconds
	uint16_t spacingMs; // piano phrases: time between notes (at tempo 1)
	uint16_t openMs;	// opening piece: when the loop first plays
};

// Low to high, as in the guide's table. The opening offsets bring the loops
// in one by one over the first 23 seconds, the D-flat choir note first
// (well inside the 2 seconds the guide asks for).
constexpr LoopDef kLoopDefs[kLoops] = {
	{kPiano, 2, {-7, -3, 0}, 4130, 1600, 1500},	 // D-flat 3, A-flat 3    41.3s  centre
	{kChoir, 1, {-3, 0, 0}, 1780, 0, 3200},		 // A-flat 3              17.8s
	{kChoir, 1, {-1, 0, 0}, 2010, 0, 9000},		 // C4                    20.1s
	{kChoir, 1, {0, 0, 0}, 3180, 0, 400},		 // D-flat 4              31.8s
	{kChoir, 1, {1, 0, 0}, 2460, 0, 12500},		 // E-flat 4              24.6s
	{kChoir, 1, {2, 0, 0}, 1960, 0, 5500},		 // F4                    19.6s
	{kPiano, 2, {1, 4, 0}, 2940, 1200, 7000},	 // E-flat 4, A-flat 4    29.4s
	{kPiano, 1, {4, 0, 0}, 2610, 0, 10500},		 // A-flat 4              26.1s
	{kChoir, 1, {4, 0, 0}, 2110, 0, 16000},		 // A-flat 4              21.1s
	{kChoir, 1, {6, 0, 0}, 2360, 0, 20500},		 // C5                    23.6s
	{kPiano, 2, {6, 8, 0}, 2230, 1000, 14000},	 // C5, E-flat 5          22.3s
	{kPiano, 3, {9, 11, 8}, 3170, 900, 18000},	 // F5, A-flat 5, E-flat 5 31.7s
	{kPiano, 1, {13, 0, 0}, 3790, 0, 23000},	 // C6                    37.9s
};

// The opening piece's root: D-flat 4.
constexpr int32_t kOpeningRoot = 61;

// New pieces pick a root from 12 keys, A3 to A-flat 4, so every piece sits
// near the opening register. (The guide says "12 keys, A3 to G-flat 4", but
// that's 10; A-flat 4 is the 12th, and it's the root that gives the
// guide's stated top piano note, G6.)
constexpr int32_t kLowestRoot = 57;
constexpr int32_t kRoots = 12;

// The note ranges the guide gives across every possible piece. Drift's
// octave jumps only go where they stay inside these. The choir's range is
// also what its wavetables cover.
constexpr int32_t kPianoLow = 45, kPianoHigh = 91; // A2..G6
constexpr int32_t kChoirLow = 52, kChoirHigh = 81; // E3..A5

struct Scale
{
	uint8_t size; // 7, or 5 for the pentatonics and Hirajoshi
	int8_t semitones[7];
};

// The rub (the semitone clash) each scale gives these loops, from the guide:
// major and Lydian 7 against 1 (identical here: no loop uses the 4th),
// Dorian and minor 2 against flat 3, Hirajoshi 5 against flat 6;
// Mixolydian and the three pentatonics none.
constexpr int32_t kScales = 9;
constexpr int32_t kMajor = 0;
constexpr Scale kScaleDefs[kScales] = {
	{7, {0, 2, 4, 5, 7, 9, 11}},  // major
	{7, {0, 2, 4, 6, 7, 9, 11}},  // Lydian
	{7, {0, 2, 3, 5, 7, 9, 10}},  // Dorian
	{7, {0, 2, 3, 5, 7, 8, 10}},  // minor
	{7, {0, 2, 4, 5, 7, 9, 10}},  // Mixolydian
	{5, {0, 2, 4, 7, 9, 0, 0}},	  // major pentatonic
	{5, {0, 3, 5, 7, 10, 0, 0}},  // minor pentatonic
	{5, {0, 2, 5, 7, 10, 0, 0}},  // suspended pentatonic
	{5, {0, 2, 3, 7, 8, 0, 0}},	  // Hirajoshi
};

// Division that rounds down (towards minus infinity), for negative degrees.
inline int32_t FloorDiv(int32_t a, int32_t b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

// A degree to a MIDI note in a scale. For the five-note scales each degree
// is scaled by 5/7 and rounded (floor(5d/7 + 1/2) = floor((10d + 7) / 14)),
// which keeps each phrase's span -- though two steps can land on the same
// note.
inline int32_t DegreeToNote(int32_t degree, int32_t root, const Scale &scale)
{
	int32_t n = scale.size == 7 ? degree : FloorDiv(degree * 10 + 7, 14);
	int32_t octave = FloorDiv(n, scale.size);
	int32_t step = n - octave * scale.size;
	return root + octave * 12 + scale.semitones[step];
}

} // namespace ab
