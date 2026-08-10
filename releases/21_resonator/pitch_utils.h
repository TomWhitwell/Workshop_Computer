#pragma once

#include <cstdint>
#include "pico/stdlib.h"

// Cross-core shared state for YIN pitch detection (Core 0 writes audio, Core 1 computes)
#define YIN_RING_BITS 6
#define YIN_RING_SIZE (1 << YIN_RING_BITS)
static volatile int16_t yinRing[YIN_RING_SIZE];  // audio sample ring buffer
static volatile uint32_t yinRingHead;             // write index (Core 0)
static volatile int32_t yinSharedPitchMV;         // pitch result (Core 1 writes, Core 0 reads)

// YIN circular buffer (1024 entries, in main SRAM to avoid SRAM4 conflicts)
static int16_t yinBuf[1024];

// Delay lookup table for 1V/oct pitch control
// 341 entries per octave, inverse exponential curve
// Base: C1 = 32.7Hz at 48kHz = 1468 samples, scaled by 64
// Higher input = shorter delay = higher pitch
// Formula: delay_vals[i] = 93952 / 2^(i/341)
// Ratio across table = 2.0 (one octave)
static const uint32_t delay_vals[341] = {
    93952, 93761, 93571, 93381, 93191, 93002, 92813, 92625, 92437, 92249,
    92062, 91875, 91688, 91502, 91316, 91131, 90946, 90761, 90577, 90393,
    90209, 90026, 89843, 89661, 89479, 89297, 89116, 88935, 88754, 88574,
    88394, 88214, 88035, 87857, 87678, 87500, 87322, 87145, 86968, 86792,
    86615, 86439, 86264, 86089, 85914, 85739, 85565, 85392, 85218, 85045,
    84872, 84700, 84528, 84356, 84185, 84014, 83844, 83673, 83503, 83334,
    83165, 82996, 82827, 82659, 82491, 82324, 82157, 81990, 81823, 81657,
    81491, 81326, 81161, 80996, 80831, 80667, 80503, 80340, 80177, 80014,
    79852, 79689, 79528, 79366, 79205, 79044, 78884, 78723, 78564, 78404,
    78245, 78086, 77927, 77769, 77611, 77454, 77296, 77139, 76983, 76826,
    76670, 76515, 76359, 76204, 76049, 75895, 75741, 75587, 75434, 75280,
    75128, 74975, 74823, 74671, 74519, 74368, 74217, 74066, 73916, 73766,
    73616, 73466, 73317, 73168, 73020, 72872, 72724, 72576, 72428, 72281,
    72135, 71988, 71842, 71696, 71551, 71405, 71260, 71116, 70971, 70827,
    70683, 70540, 70396, 70253, 70111, 69968, 69826, 69685, 69543, 69402,
    69261, 69120, 68980, 68840, 68700, 68561, 68421, 68282, 68144, 68005,
    67867, 67729, 67592, 67455, 67318, 67181, 67045, 66908, 66773, 66637,
    66502, 66367, 66232, 66097, 65963, 65829, 65696, 65562, 65429, 65296,
    65164, 65031, 64899, 64767, 64636, 64505, 64374, 64243, 64112, 63982,
    63852, 63723, 63593, 63464, 63335, 63207, 63078, 62950, 62822, 62695,
    62568, 62440, 62314, 62187, 62061, 61935, 61809, 61684, 61558, 61433,
    61309, 61184, 61060, 60936, 60812, 60689, 60565, 60442, 60320, 60197,
    60075, 59953, 59831, 59710, 59588, 59467, 59347, 59226, 59106, 58986,
    58866, 58747, 58627, 58508, 58389, 58271, 58153, 58034, 57917, 57799,
    57682, 57564, 57448, 57331, 57215, 57098, 56982, 56867, 56751, 56636,
    56521, 56406, 56292, 56177, 56063, 55949, 55836, 55722, 55609, 55496,
    55384, 55271, 55159, 55047, 54935, 54824, 54712, 54601, 54490, 54380,
    54269, 54159, 54049, 53939, 53830, 53720, 53611, 53503, 53394, 53285,
    53177, 53069, 52962, 52854, 52747, 52640, 52533, 52426, 52320, 52213,
    52107, 52001, 51896, 51790, 51685, 51580, 51476, 51371, 51267, 51163,
    51059, 50955, 50852, 50748, 50645, 50542, 50440, 50337, 50235, 50133,
    50031, 49930, 49828, 49727, 49626, 49525, 49425, 49325, 49224, 49124,
    49025, 48925, 48826, 48727, 48628, 48529, 48430, 48332, 48234, 48136,
    48038, 47941, 47843, 47746, 47649, 47552, 47456, 47360, 47263, 47167,
    47072
};

// Exponential delay lookup for 1V/oct pitch control
// in: 0-4095 (knob + CV combined)
// Returns delay in samples (right-shifted by octave)
inline int32_t ExpDelay(int32_t in) {
    if (in < 0) in = 0;
    if (in > 4091) in = 4091;
    int32_t oct = in / 341;
    int32_t suboct = in % 341;
    return delay_vals[suboct] >> oct;
}

// Convert a just intonation ratio (num:den) to millivolt offset for 1V/oct CV
// round(1000 * log2(num/den)) precomputed for all ratios in the chord table
// Uses num*256+den as a collision-free key for switch lookup
inline int32_t ratioToMillivolts(int num, int den) {
    switch (num * 256 + den) {
        case 1*256+1:   return 0;     // 1:1 unison
        case 9*256+8:   return 170;   // 9:8 major 2nd
        case 6*256+5:   return 263;   // 6:5 minor 3rd
        case 5*256+4:   return 322;   // 5:4 major 3rd
        case 4*256+3:   return 415;   // 4:3 perfect 4th
        case 36*256+25: return 526;   // 36:25 dim 5th
        case 3*256+2:   return 585;   // 3:2 perfect 5th
        case 5*256+3:   return 737;   // 5:3 major 6th
        case 9*256+5:   return 848;   // 9:5 minor 7th
        case 15*256+8:  return 907;   // 15:8 major 7th
        case 2*256+1:   return 1000;  // 2:1 octave
        case 9*256+4:   return 1170;  // 9:4 major 9th
        case 5*256+2:   return 1322;  // 5:2 major 10th
        case 3*256+1:   return 1585;  // 3:1 octave + 5th
        case 4*256+1:   return 2000;  // 4:1 two octaves
        default:        return 0;
    }
}

// Convert fractional period (fixed-point <<8, in 48kHz samples) to 1V/oct millivolts
// Reverse lookup into delay_vals table with sub-sample precision via table interpolation
inline int32_t periodToMillivoltsFrac(int32_t period_fp8) {
    // Clamp using integer part
    int32_t period = period_fp8 >> 8;
    if (period < 24) period_fp8 = 24 << 8;
    if (period > 1500) period_fp8 = 1500 << 8;
    period = period_fp8 >> 8;

    // Find octave from integer period
    int oct = 0;
    while (oct < 11 && (int32_t)(delay_vals[0] >> (oct + 1)) >= period) oct++;

    // Binary search using fractional period for sub-entry precision
    // Compare delay_vals[mid]*256 against period_fp8 * (1 << oct)
    int64_t scaledPeriod = (int64_t)period_fp8 << oct;
    int lo = 0, hi = 340;
    while (lo < hi) {
        int mid = (lo + hi) >> 1;
        if (((int64_t)delay_vals[mid] << 8) > scaledPeriod)
            lo = mid + 1;
        else
            hi = mid;
    }

    // Interpolate between adjacent table entries for sub-entry precision
    int32_t pitchCV_fp8;
    if (lo > 0 && lo <= 340) {
        int64_t upper = (int64_t)delay_vals[lo - 1] << 8;
        int64_t lower = (int64_t)delay_vals[lo] << 8;
        int64_t range = upper - lower;
        if (range > 0) {
            // frac: 0 at entry lo-1, 256 at entry lo
            int32_t frac = (int32_t)(((upper - scaledPeriod) << 8) / range);
            if (frac < 0) frac = 0;
            if (frac > 256) frac = 256;
            pitchCV_fp8 = ((oct * 341 + lo - 1) << 8) + frac;
        } else {
            pitchCV_fp8 = (oct * 341 + lo) << 8;
        }
    } else {
        pitchCV_fp8 = (oct * 341 + lo) << 8;
    }

    // (pitchCV - 3069) * 12014 / 4096, scaled for fp8 input (middle C = 0V)
    return (int32_t)(((int64_t)(pitchCV_fp8 - (3069 << 8)) * 12014) >> 20);
}
