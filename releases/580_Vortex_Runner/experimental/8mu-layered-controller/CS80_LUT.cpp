#include "CS80_LUT.h"

// Small tables keep the audio loop predictable on the RP2040. The sine table is
// deliberately modest; it is used for LFO/ring colour rather than pristine audio.
const int16_t cs80SineLUT[Cs80SineLutSize] = {
       0,   201,   399,   594,   783,   965,  1137,  1299,
    1447,  1582,  1702,  1805,  1891,  1959,  2008,  2037,
    2047,  2037,  2008,  1959,  1891,  1805,  1702,  1582,
    1447,  1299,  1137,   965,   783,   594,   399,   201,
       0,  -201,  -399,  -594,  -783,  -965, -1137, -1299,
   -1447, -1582, -1702, -1805, -1891, -1959, -2008, -2037,
   -2047, -2037, -2008, -1959, -1891, -1805, -1702, -1582,
   -1447, -1299, -1137,  -965,  -783,  -594,  -399,  -201,
};

// 2^(n/12), Q16. Entry 12 is the octave endpoint for interpolation from B to C.
const uint32_t cs80SemitoneRatioQ16[13] = {
    65536u, 69433u, 73562u, 77936u, 82570u, 87480u, 92682u,
    98193u, 104032u, 110218u, 116771u, 123715u, 131072u
};

// One-pole filter coefficients in Q12, arranged as a musical-ish curve.
const uint16_t cs80FilterAlphaQ12[Cs80CurveLutSize] = {
    6u, 13u, 39u, 85u, 152u, 242u, 355u, 492u,
    653u, 840u, 1052u, 1289u, 1553u, 1844u, 2161u, 2506u
};

// Envelope increments in Q12 per sample. Low values give slow CS-style swells;
// high values give fast plucks without requiring division in the sample path.
const uint16_t cs80EnvelopeRateQ12[Cs80CurveLutSize] = {
    1u, 2u, 8u, 20u, 39u, 65u, 101u, 145u,
    200u, 265u, 341u, 429u, 528u, 639u, 764u, 901u
};
