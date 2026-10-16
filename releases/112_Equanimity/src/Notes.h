#pragma once
#include <cstdint>
#include "Config.h"
#include "Params.h"
#include "Tables.h"

namespace eq
{

// Core 1 helpers for notes: the scales, and turning "strike this note, this
// loud, at this tide, in this tone" into everything a melody voice on core 0
// needs.

// The scales a new set can pick from.
constexpr int kScales = 6;

// Semitones above the root in each scale, as a bit mask (bit 0 = root,
// bit 11 = a semitone below the octave).
constexpr uint16_t kScaleMask[kScales] = {
	0b101010110101, // Major:            1 2 3 4 5 6 7
	0b010110101101, // Minor:            1 2 b3 4 5 b6 b7
	0b001010010101, // Major pentatonic: 1 2 3 5 6
	0b010010101001, // Minor pentatonic: 1 b3 4 5 b7
	0b011010101101, // Dorian:           1 2 b3 4 5 6 b7
	0b101011010101, // Lydian:           1 2 3 #4 5 6 7
};

// Is `note` in `scale` built on `root` (0 = C .. 11 = B)?
inline bool InScale(int32_t scale, int32_t root, int32_t note)
{
	return (kScaleMask[scale] >> ((note - root + 120) % 12)) & 1;
}

// A strike. `note` is the MIDI note before the tone's octave shift; `level`
// and `tide` are Q12 (0..4096).
inline VoiceCmd MakeStrike(int32_t note, int32_t level, int32_t tide, const Tone &tone)
{
	VoiceCmd c;
	int32_t played = note + 12 * tone.octave;
	played = played > 127 ? 127 : played;
	c.carInc = Tables::noteInc[played];
	c.modInc = (c.carInc >> 8) * (uint32_t)tone.ratioQ8;

	c.peakAmp = kVoicePeakQ12 * level; // Q12 x Q12 = Q24: envelope units
	int32_t attackBlocks = tone.attackMs * kSampleRate / 1000 / 8;
	c.attackInc = c.peakAmp / attackBlocks + 1;

	// Brightness: the index (radians, Q12) rises with the tide. A radian of
	// index is 2^32 / (2 pi x 32767) = 20861 phase units per unit of
	// modulator output; x 256 for envelope units, / 4096 for Q12 = 1304.
	int32_t indexQ12 = tone.indexLowTideQ12 + (((tone.indexHighTideQ12 - tone.indexLowTideQ12) * tide) >> 12);
	c.peakIndex = indexQ12 * 1304;

	// Ring time: from the tone's low-note time at C4 to its high-note time
	// three octaves up. An exponential decay to -60dB in T ms loses a
	// fraction 8 x ln(1000) / (T x 48) of itself every 8 samples; as a
	// fraction of 2^20 that is 1207300 / T.
	constexpr int32_t kSpan = 36;
	int32_t above = note - kMelodyBottom;
	above = above < 0 ? 0 : (above > kSpan ? kSpan : above);
	int32_t ringMs = tone.ringMsLow - (tone.ringMsLow - tone.ringMsHigh) * above / kSpan;
	c.ampDecay = 1207300 / ringMs;
	c.indexDecay = 1207300 / tone.indexDecayMs;

	c.lpCoef = kBellLpLowTideQ15 + (((kBellLpHighTideQ15 - kBellLpLowTideQ15) * tide) >> 12);
	return c;
}

} // namespace eq
