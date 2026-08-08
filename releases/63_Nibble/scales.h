// scales.h — the twelve scales the Y knob selects between.
//
// Tables taken from 89_Lockstep/src/markov_scales.h (Workshop System release
// 89), which orders them dark -> bright so a single knob sweep is musical. The
// `name` field is dropped: this card has no display, and a const char* per
// entry is flash we would never read.
//
// ---------------------------------------------------------------------------
// WHY DEGREE-INDEXED AND NOT NEAREST-NOTE QUANTISING
// ---------------------------------------------------------------------------
//
// The obvious thing is to treat the Four Voltages reading as a pitch and snap
// it to the nearest scale tone, which is what Lockstep's own QuantizeToScale()
// does. That is wrong here, and it is worth being explicit about why.
//
// The levels are NOT pitches. They are resistor-divider outputs whose spacing
// is a property of the network, not of music, and the Four Voltages knob moves
// all of them unpredictably. Snapping them to nearest notes would mean:
//
//   - the combo -> pitch mapping changes every time the knob is touched, and
//   - several combos collapse onto the same note whenever two levels happen to
//     land inside one semitone of each other.
//
// Indexing by DEGREE instead gives the property an instrument actually needs:
// combo index 0..9 maps to scale degree 0..9, deterministically, forever. Move
// the knob, re-learn, and the same buttons still play the same degrees. The
// voltages decide only WHICH combo you pressed, never which note it is.
//
// Access function shape is from 19_CA_Sequencer/main.cpp:220.

#pragma once
#include <stdint.h>

namespace nib {

struct Scale
{
	uint8_t len;          ///< how many degrees before the octave repeats
	uint8_t steps[12];    ///< semitone offsets from the root
};

constexpr int kNumScales = 12;

/// Ordered dark -> bright, so the Y knob reads as a single brightness axis and
/// the LED bar display (which lights more LEDs as the index climbs) matches it.
constexpr Scale kScales[kNumScales] = {
	{7,  {0, 1, 3, 5, 7, 8, 10}},                    //  0 Phrygian      (fully CCW)
	{5,  {0, 2, 3, 7, 8}},                           //  1 Hirajoshi
	{7,  {0, 2, 3, 5, 7, 8, 11}},                    //  2 Harmonic Minor
	{7,  {0, 2, 3, 5, 7, 8, 10}},                    //  3 Natural Minor
	{5,  {0, 3, 5, 7, 10}},                          //  4 Minor Pentatonic
	{4,  {0, 3, 7, 10}},                             //  5 m7 Arpeggio
	{7,  {0, 2, 3, 5, 7, 9, 10}},                    //  6 Dorian
	{5,  {0, 2, 4, 7, 9}},                           //  7 Major Pentatonic
	{7,  {0, 2, 4, 5, 7, 9, 11}},                    //  8 Ionian (Major)
	{4,  {0, 4, 7, 11}},                             //  9 Maj7 Arpeggio
	{6,  {0, 2, 4, 6, 8, 10}},                       // 10 Whole Tone
	{12, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}},    // 11 Chromatic     (fully CW)
};

/// How many DEGREES above a note its harmony sits, for each scale.
///
/// Two degrees is a third in anything diatonic, and that is the default. The
/// exception is Chromatic, where two degrees is a whole tone — an interval that
/// reads as a detune rather than a harmony. Chromatic gets four degrees (a
/// major third) so the harmony output stays musical across the whole knob.
///
/// The arpeggio scales give fifths rather than thirds at two degrees, which is
/// correct and left alone: on a chord tone the "third up" IS a fifth.
static inline int HarmonyDegreesFor(int scaleIdx)
{
	return (scaleIdx == kNumScales - 1) ? 4 : 2;   // Chromatic is the last entry
}

/// Degree -> MIDI note. Degrees past the end of the scale wrap into the next
/// octave, so ten combos span between ~2.5 octaves (a 4-note arpeggio) and
/// under one (chromatic). Both extremes are musically useful and the difference
/// is audible immediately, which is what makes the Y knob worth having.
static inline uint8_t QuantizeNote(int root, int scaleIdx, int degree)
{
	if (scaleIdx < 0) scaleIdx = 0;
	if (scaleIdx >= kNumScales) scaleIdx = kNumScales - 1;
	if (degree < 0) degree = 0;

	const Scale &s = kScales[scaleIdx];
	int octave = degree / s.len;
	int note   = root + octave * 12 + s.steps[degree % s.len];

	if (note < 0)   note = 0;
	if (note > 127) note = 127;
	return static_cast<uint8_t>(note);
}

} // namespace nib
