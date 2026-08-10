#pragma once

// Chord modes
enum ChordMode {
    HARMONIC = 0,    // 1:1, 2:1, 3:1, 4:1 (harmonic series)
    FIFTH = 1,       // 1:1, 3:2, 2:1, 3:1 (stacked fifths)
    MAJOR7 = 2,      // 1:1, 5:4, 3:2, 15:8 (major 7th chord)
    MINOR7 = 3,      // 1:1, 6:5, 3:2, 9:5 (minor 7th chord)
    DIM = 4,         // 1:1, 6:5, 36:25, 3:2 (diminished)
    SUS4 = 5,        // 1:1, 4:3, 3:2, 2:1 (suspended 4th)
    ADD9 = 6,        // 1:1, 5:4, 3:2, 9:4 (major add 9)
    MAJOR10 = 7,     // 1:1, 5:4, 3:2, 5:2 (major chord with 10th)
    SUS2 = 8,        // 1:1, 9:8, 3:2, 2:1 (suspended 2nd)
    MAJOR = 9,       // 1:1, 5:4, 3:2, 2:1 (major triad)
    MINOR = 10,      // 1:1, 6:5, 3:2, 2:1 (minor triad)
    MAJOR6 = 11,     // 1:1, 5:4, 3:2, 5:3 (major 6th)
    DOM7 = 12,       // 1:1, 5:4, 3:2, 9:5 (dominant 7th)
    MIN9 = 13,       // 1:1, 6:5, 3:2, 9:4 (minor add 9)
    TANPURA_PA = 14, // 1:1, 3:2, 2:1, 4:1 (Sa, Pa, Sa', Sa'')
    TANPURA_MA = 15, // 1:1, 4:3, 2:1, 4:1 (Sa, Ma, Sa', Sa'')
    TANPURA_NI = 16, // 1:1, 15:8, 2:1, 4:1 (Sa, Ni, Sa', Sa'')
    TANPURA_NI_KOMAL = 17  // 1:1, 9:5, 2:1, 4:1 (Sa, ni, Sa', Sa'')
};

static const int NUM_MODES = 18;
static const int MAX_PROGRESSION_LENGTH = 18;

struct ProgressionBuffer {
    ChordMode chords[MAX_PROGRESSION_LENGTH];
    int length;
};

// Calculate frequency ratio based on chord mode and string number
// Using fixed-point math to avoid floating-point on Cortex-M0+
// Returns numerator and denominator for each ratio
inline void getFrequencyRatios(ChordMode mode,
                               int& num1, int& den1, int& num2, int& den2,
                               int& num3, int& den3, int& num4, int& den4) {
    // String 1: Fundamental
    num1 = 1;
    den1 = 1;

    switch (mode) {
        case HARMONIC:
            // Harmonic series: 1:1, 2:1, 3:1, 4:1
            num2 = 2; den2 = 1;
            num3 = 3; den3 = 1;
            num4 = 4; den4 = 1;
            break;
        case FIFTH:
            // Stacked fifths: 1:1, 3:2, 2:1, 3:1
            num2 = 3; den2 = 2;
            num3 = 2; den3 = 1;
            num4 = 3; den4 = 1;
            break;
        case MAJOR7:
            // Major 7th: 1:1, 5:4, 3:2, 15:8
            num2 = 5; den2 = 4;
            num3 = 3; den3 = 2;
            num4 = 15; den4 = 8;
            break;
        case MINOR7:
            // Minor 7th: 1:1, 6:5, 3:2, 9:5
            num2 = 6; den2 = 5;
            num3 = 3; den3 = 2;
            num4 = 9; den4 = 5;
            break;
        case DIM:
            // Diminished: 1:1, 6:5, 36:25, 3:2
            num2 = 6; den2 = 5;
            num3 = 36; den3 = 25;
            num4 = 3; den4 = 2;
            break;
        case SUS4:
            // Suspended 4th: 1:1, 4:3, 3:2, 2:1
            num2 = 4; den2 = 3;
            num3 = 3; den3 = 2;
            num4 = 2; den4 = 1;
            break;
        case ADD9:
            // Major add 9: 1:1, 5:4, 3:2, 9:4
            num2 = 5; den2 = 4;
            num3 = 3; den3 = 2;
            num4 = 9; den4 = 4;
            break;
        case MAJOR10:
            // Major 10th: 1:1, 5:4, 3:2, 5:2 (root, M3, P5, M10)
            num2 = 5; den2 = 4;
            num3 = 3; den3 = 2;
            num4 = 5; den4 = 2;
            break;
        case SUS2:
            // Suspended 2nd: 1:1, 9:8, 3:2, 2:1 (root, M2, P5, octave)
            num2 = 9; den2 = 8;
            num3 = 3; den3 = 2;
            num4 = 2; den4 = 1;
            break;
        case MAJOR:
            // Major triad: 1:1, 5:4, 3:2, 2:1 (root, M3, P5, octave)
            num2 = 5; den2 = 4;
            num3 = 3; den3 = 2;
            num4 = 2; den4 = 1;
            break;
        case MINOR:
            // Minor triad: 1:1, 6:5, 3:2, 2:1 (root, m3, P5, octave)
            num2 = 6; den2 = 5;
            num3 = 3; den3 = 2;
            num4 = 2; den4 = 1;
            break;
        case MAJOR6:
            // Major 6th: 1:1, 5:4, 3:2, 5:3 (root, M3, P5, M6)
            num2 = 5; den2 = 4;
            num3 = 3; den3 = 2;
            num4 = 5; den4 = 3;
            break;
        case DOM7:
            // Dominant 7th: 1:1, 5:4, 3:2, 9:5 (root, M3, P5, m7)
            num2 = 5; den2 = 4;
            num3 = 3; den3 = 2;
            num4 = 9; den4 = 5;
            break;
        case MIN9:
            // Minor add 9: 1:1, 6:5, 3:2, 9:4 (root, m3, P5, M9)
            num2 = 6; den2 = 5;
            num3 = 3; den3 = 2;
            num4 = 9; den4 = 4;
            break;
        case TANPURA_PA:
            // Tanpura Pa: 1:1, 3:2, 2:1, 4:1 (Sa, Pa, Sa', Sa'')
            num2 = 3; den2 = 2;
            num3 = 2; den3 = 1;
            num4 = 4; den4 = 1;
            break;
        case TANPURA_MA:
            // Tanpura Ma: 1:1, 4:3, 2:1, 4:1 (Sa, Ma, Sa', Sa'')
            num2 = 4; den2 = 3;
            num3 = 2; den3 = 1;
            num4 = 4; den4 = 1;
            break;
        case TANPURA_NI:
            // Tanpura Ni: 1:1, 15:8, 2:1, 4:1 (Sa, Ni, Sa', Sa'')
            num2 = 15; den2 = 8;
            num3 = 2; den3 = 1;
            num4 = 4; den4 = 1;
            break;
        case TANPURA_NI_KOMAL:
            // Tanpura ni: 1:1, 9:5, 2:1, 4:1 (Sa, ni, Sa', Sa'')
            num2 = 9; den2 = 5;
            num3 = 2; den3 = 1;
            num4 = 4; den4 = 1;
            break;
    }
}
