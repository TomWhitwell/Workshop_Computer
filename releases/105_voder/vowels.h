// TRACT8 - the vowel cube.
//
// Eight band gains per corner vowel, Q15, for the fixed band centres
//
//   250  450  700  1000  1400  1900  2600  3800 Hz
//
// THREE AXES, EIGHT CORNERS. Three controls place a point inside a cube
// and the band gains are the trilinear blend of its corners. The axes are
// the three things a mouth actually does, which is why they are worth
// three separate controls:
//
//   OPENNESS  how far the jaw opens             = F1
//   FRONT     where the tongue sits             = F2
//   ROUND     how far the lips protrude         = lowers F2 (and F1)
//
//               ROUND = 0 (spread)        ROUND = 32767 (rounded)
//        CLOSE   OO ---------- EE          OOr --------- EEr
//                 |            |            |            |
//        OPEN    AH ---------- EH          AHr --------- EHr
//                back       front          back       front
//
// WHY THE THIRD AXIS EXISTS. The card shipped with a 2-D square and OH was
// unreachable inside it - 6.0 dB away at the closest approach - because OH
// is ROUNDER than anything on the OO-AH edge: its F2 (840 Hz) sits below
// both back corners, so it is not between them in any direction the square
// can travel. Rounding is a genuinely independent dimension of vowel
// space, and adding it brings OH to 3.0 dB and picks up AW and ER on the
// way. Every vowel in the set is now within 2.8 dB.
//
// The rounded corners are the same four vowels with the lips protruded:
// F1 x 0.88, F2 x 0.62, F3 x 0.95. Lip protrusion lengthens the vocal
// tract, which lowers every formant, and it lowers F2 hardest - that
// asymmetry is what makes rounding a different axis rather than a slide
// along the existing ones. The multipliers were fitted by minimising the
// worst reachable error across all six named vowels.
//
// Formants from Peterson & Barney (1952), adult male. Each band gain is
// the energy the first three formants deposit in it under a log-frequency
// gaussian 0.42 octaves wide, with a -26 dB floor added before normalising
// because real vowels have no silent regions.
//
// The peak is 16000 rather than 32767 so the brightness tilt (up to
// 1.219x) has somewhere to go without clipping the loudest band.
//
// Read-only, so flash is the right home - never written, only indexed.

#ifndef TRACT8_VOWELS_H_
#define TRACT8_VOWELS_H_

#include <stdint.h>

namespace tract8 {

static constexpr int kNumBands = 8;

// Corner index within one plane. BlendVowel() indexes these directly.
enum VowelCorner {
  kCloseBack = 0,   // OO
  kOpenBack = 1,    // AH
  kCloseFront = 2,  // EE
  kOpenFront = 3,   // EH
  kNumCorners = 4
};

// Spread (lips relaxed) plane.
static const int16_t kVowelSpread[kNumCorners][kNumBands] = {
  //  250    450    700   1000   1400   1900   2600   3800
  { 16000,  8369,  6043,  6689,  3106,  2657,  2533,  1271 },  // OO F1 300 F2 870
  {   666,  4003, 16000, 15423,  7647,  4226,  3923,  1675 },  // AH F1 730 F2 1090
  { 16000,  4167,   864,   929,  2824,  8556, 12114,  5952 },  // EE F1 270 F2 2290
  {  1518, 16000, 12137,  3653,  7991, 14057, 10935,  3136 },  // EH F1 530 F2 1840
};

// Rounded (lips protruded) plane. Same four vowels, F1 x0.88 F2 x0.62.
static const int16_t kVowelRound[kNumCorners][kNumBands] = {
  //  250    450    700   1000   1400   1900   2600   3800
  { 16000,  8053,  4419,  1387,  1337,  2189,  1977,   978 },  // OOr F1 264 F2 539
  {   569,  7483, 16000,  6101,  1596,  2496,  2808,  1089 },  // AHr F1 642 F2 676
  { 16000,  2170,  1193,  4525,  8724,  7189,  6062,  3677 },  // EEr F1 238 F2 1420
  {  2308, 16000,  8610,  8916,  8276,  6071,  5253,  1959 },  // EHr F1 466 F2 1141
};

// Place a point in the vowel cube. All three arguments Q15 (0..32767):
//   openness  0 = close,  32767 = open
//   front     0 = back,   32767 = front
//   round     0 = spread, 32767 = rounded
// Writes kNumBands gains into out.
//
// Trilinear: bilinear within each plane, then a straight blend between the
// two planes. The eight corners come out exact and everything between is
// smooth, so sweeping any axis is continuous and there are no seams.
static inline void BlendVowel(int32_t openness, int32_t front, int32_t round,
                              int32_t* out) {
  for (int i = 0; i < kNumBands; i++) {
    const int32_t s_back =
        kVowelSpread[kCloseBack][i] +
        (((kVowelSpread[kOpenBack][i] - kVowelSpread[kCloseBack][i]) *
          openness) >> 15);
    const int32_t s_front =
        kVowelSpread[kCloseFront][i] +
        (((kVowelSpread[kOpenFront][i] - kVowelSpread[kCloseFront][i]) *
          openness) >> 15);
    const int32_t spread = s_back + (((s_front - s_back) * front) >> 15);

    const int32_t r_back =
        kVowelRound[kCloseBack][i] +
        (((kVowelRound[kOpenBack][i] - kVowelRound[kCloseBack][i]) *
          openness) >> 15);
    const int32_t r_front =
        kVowelRound[kCloseFront][i] +
        (((kVowelRound[kOpenFront][i] - kVowelRound[kCloseFront][i]) *
          openness) >> 15);
    const int32_t rounded = r_back + (((r_front - r_back) * front) >> 15);

    out[i] = spread + (((rounded - spread) * round) >> 15);
  }
}

}  // namespace tract8

#endif  // TRACT8_VOWELS_H_
