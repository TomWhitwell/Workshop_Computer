#pragma once
#include <stdint.h>

struct Scale {
    const char* name;
    uint8_t     num_notes;
    uint8_t     notes[12]; // semitone offsets from root (0)
};

// Sorted CCW→CW: Dark/Minor → Bright/Major → Ambiguous
static constexpr uint8_t NUM_SCALES = 12;
static const Scale generative_scales[NUM_SCALES] = {
    {"Phrygian",         7, {0, 1, 3, 5, 7, 8, 10,  0,  0,  0,  0,  0}}, //  0: Fully CCW
    {"Hirajoshi",        5, {0, 2, 3, 7, 8,  0,  0,  0,  0,  0,  0,  0}}, //  1
    {"Harmonic Minor",   7, {0, 2, 3, 5, 7, 8, 11,  0,  0,  0,  0,  0}}, //  2
    {"Natural Minor",    7, {0, 2, 3, 5, 7, 8, 10,  0,  0,  0,  0,  0}}, //  3
    {"Minor Pentatonic", 5, {0, 3, 5, 7, 10,  0,  0,  0,  0,  0,  0,  0}}, //  4
    {"m7 Arpeggio",      4, {0, 3, 7, 10,  0,  0,  0,  0,  0,  0,  0,  0}}, //  5: Centre-Left
    {"Dorian",           7, {0, 2, 3, 5, 7, 9, 10,  0,  0,  0,  0,  0}}, //  6: Centre-Right
    {"Major Pentatonic", 5, {0, 2, 4, 7,  9,  0,  0,  0,  0,  0,  0,  0}}, //  7
    {"Ionian (Major)",   7, {0, 2, 4, 5, 7, 9, 11,  0,  0,  0,  0,  0}}, //  8
    {"Maj7 Arpeggio",    4, {0, 4, 7, 11,  0,  0,  0,  0,  0,  0,  0,  0}}, //  9
    {"Whole Tone",       6, {0, 2, 4, 6, 8, 10,  0,  0,  0,  0,  0,  0}}, // 10
    {"Chromatic",       12, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}},        // 11: Fully CW
};

// Quantize a MIDI note to the nearest degree of the given scale.
// Wraps correctly across octave boundaries.
// Returns the quantized MIDI note (same octave or adjacent).
static inline uint8_t QuantizeToScale(uint8_t midi_note, uint8_t scale_idx) {
    if (scale_idx >= NUM_SCALES) scale_idx = NUM_SCALES - 1;
    const Scale& s = generative_scales[scale_idx];

    int32_t octave = midi_note / 12;
    int32_t chroma = midi_note % 12;

    int32_t best_note = s.notes[0];
    int32_t best_dist = 13;

    for (int i = 0; i < s.num_notes; i++) {
        int32_t dist = chroma - s.notes[i];
        if (dist < 0) dist = -dist;
        if (dist > 6) dist = 12 - dist; // wrap around octave
        if (dist < best_dist) {
            best_dist = dist;
            best_note = s.notes[i];
        }
    }

    int32_t result = octave * 12 + best_note;
    if (result < 0)   result = 0;
    if (result > 127) result = 127;
    return (uint8_t)result;
}
