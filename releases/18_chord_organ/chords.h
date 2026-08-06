#pragma once

#include <cstdint>

// Chord-Organ default chord table: 16 chords, up to 8 semitone offsets per chord.
// Value 255 = unused voice slot.
// From Music Thing Modular Chord-Organ Settings.h defaultNotes.
constexpr int kChordCount = 16;
constexpr int kMaxVoices = 8;

constexpr int8_t kChordNotes[kChordCount][kMaxVoices] = {
    { 0,  4,  7, 12,  0, -1, -1, -1 },  // 1  Major
    { 0,  3,  7, 12,  0, -1, -1, -1 },  // 2  Minor
    { 0,  4,  7, 11,  0, -1, -1, -1 }, // 3  Major 7th
    { 0,  3,  7, 10,  0, -1, -1, -1 }, // 4  Minor 7th
    { 0,  4,  7, 11, 14, -1, -1, -1 }, // 5  Major 9th
    { 0,  3,  7, 10, 14, -1, -1, -1 }, // 6  Minor 9th
    { 0,  5,  7, 12,  0, -1, -1, -1 }, // 7  Suspended 4th
    { 0,  7, 12,  0,  7, -1, -1, -1 }, // 8  Power 5th
    { 0,  5, 12,  0,  5, -1, -1, -1 }, // 9  Power 4th
    { 0,  4,  7,  8,  0, -1, -1, -1 }, // 10 Major 6th
    { 0,  3,  7,  8,  0, -1, -1, -1 }, // 11 Minor 6th
    { 0,  3,  6,  0,  3, -1, -1, -1 }, // 12 Diminished
    { 0,  4,  8,  0,  4, -1, -1, -1 }, // 13 Augmented
    { 0,  0,  0,  0,  0, -1, -1, -1 }, // 14 Root
    {-12,-12,  0,  0,  0, -1, -1, -1 }, // 15 Sub Octave
    {-12,  0,  0, 12, 24, -1, -1, -1 }, // 16 2 up 1 down octaves
};

// Per-voice amplitude (0..255). (sample * amp) >> 8 per voice; sum then >> 2 for DAC.
// Chosen so 5 voices at sine ±32000 stays under 2048 after sum.
constexpr int kAmpPerVoice[kMaxVoices] = {
    8, 7, 6, 5, 4, 4, 3, 3
};
