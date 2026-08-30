// shepard.cpp - table construction and scale quantisation.
//
// The tables written here (sin_lut, pow2_lut) are PLAIN GLOBALS, deliberately.
// See the note in shepard.h: a static in a header can land in flash, where the
// writes below would be silently discarded.

#include "shepard.h"

#include <math.h>

namespace shepard {

// Written at init -> must be in RAM. Plain globals, so they land in BSS.
int16_t sin_lut[kSinLutSize];
int32_t pow2_lut[kPow2LutSize + 1];

// Read-only -> flash is correct. Values are 32767/sqrt(N), rounded.
const int16_t kInvSqrtN[kMaxLayers + 1] = {
      0,      0,      0,   // N = 0,1,2 unused; present so N indexes directly
  18919,                   // N = 3    0.577350
  16384,                   // N = 4    0.500000
  14654,                   // N = 5    0.447214
  13377,                   // N = 6    0.408248
  12385,                   // N = 7    0.377964
  11585,                   // N = 8    0.353553
  10923,                   // N = 9    0.333333
  10362,                   // N = 10   0.316228
   9880,                   // N = 11   0.301511
};

// The LIVE path's normaliser, and it is deliberately NOT 1/sqrt(N).
//
// 1/sqrt(N) is correct for the oscillator bank because those layers are
// mutually incoherent - different frequencies, unrelated phases - so their
// POWERS add and the total grows as sqrt(N).
//
// The live path is N copies of the SAME source transposed by octaves. Those
// are partially CORRELATED, so the sum grows faster than sqrt(N). Measured
// against a full-scale drone with the normaliser divided back out:
//
//     N= 3  raw  554.0        raw amplitude grows as N^0.70
//     N= 5  raw  736.2        (incoherent would be N^0.5,
//     N= 7  raw  919.9         fully coherent N^1.0)
//     N= 9  raw 1133.9
//     N=11  raw 1367.1
//
// So 1/sqrt(N) OVER-normalises it, and does so worst at low N. Heard on
// hardware as the audio path being quiet, and quieter still with few layers -
// which is exactly how it was reported.
//
// Values are 1/N^0.70, scaled so N=8 coincides with kInvSqrtN[8]. That keeps
// the reference point where the two paths already agreed and moves the rest
// to meet it: measured live rms is now 357-389 across N=3..11, against the
// synth's flat 443, with at least 3.0 dB of peak headroom everywhere.
//
// The exponent is measured, not derived. If the octave stack's structure ever
// changes, re-measure it - tools/passthru_check.py::check_live_level_flat
// asserts the flatness this produces.
const int16_t kInvPowN[kMaxLayers + 1] = {
      0,      0,      0,   // N = 0,1,2 unused; present so N indexes directly
  23018,                   // N = 3
  18820,                   // N = 4
  16098,                   // N = 5
  14169,                   // N = 6
  12720,                   // N = 7
  11585,                   // N = 8   coincides with kInvSqrtN[8]
  10668,                   // N = 9
   9910,                   // N = 10
   9270,                   // N = 11
};

// Scale degrees as Q32 offsets within one octave: degree at c cents is
// round(c / 1200 * 2^32). Read-only, so flash is correct.
//
// Verified to round-trip to exact cents in tools/quant_check.py.
const uint32_t kScale12[12] = {
  0x00000000u,   //    0 cents
  0x15555555u,   //  100
  0x2AAAAAABu,   //  200
  0x40000000u,   //  300
  0x55555555u,   //  400
  0x6AAAAAABu,   //  500
  0x80000000u,   //  600
  0x95555555u,   //  700
  0xAAAAAAABu,   //  800
  0xC0000000u,   //  900
  0xD5555555u,   // 1000
  0xEAAAAAABu,   // 1100
};

const uint32_t kScaleMaj[7] = {
  0x00000000u,   //    0
  0x2AAAAAABu,   //  200
  0x55555555u,   //  400
  0x6AAAAAABu,   //  500
  0x95555555u,   //  700
  0xC0000000u,   //  900
  0xEAAAAAABu,   // 1100
};

// Natural minor. Worth having as a distinct option: in a RISING glide a
// chromatic scale is nearly indistinguishable from smooth, so the modal
// choices are what actually change the character.
const uint32_t kScaleMin[7] = {
  0x00000000u,   //    0
  0x2AAAAAABu,   //  200
  0x40000000u,   //  300
  0x6AAAAAABu,   //  500
  0x95555555u,   //  700
  0xAAAAAAABu,   //  800
  0xD5555555u,   // 1000
};

const uint32_t kScalePent[5] = {
  0x00000000u,   //    0
  0x2AAAAAABu,   //  200
  0x55555555u,   //  400
  0x95555555u,   //  700
  0xC0000000u,   //  900
};

// Symmetric scales. Both repeat BELOW the octave - diminished every minor
// third, whole tone every whole tone - which suits a barber-pole unusually
// well: the stack is already self-similar at a smaller interval than the wrap,
// so the climb has no obvious home key to return to.
const uint32_t kScaleDim[8] = {
  0x00000000u,   //    0
  0x2AAAAAABu,   //  200
  0x40000000u,   //  300
  0x6AAAAAABu,   //  500
  0x80000000u,   //  600
  0xAAAAAAABu,   //  800
  0xC0000000u,   //  900
  0xEAAAAAABu,   // 1100
};

const uint32_t kScaleWhole[6] = {
  0x00000000u,   //    0
  0x2AAAAAABu,   //  200
  0x55555555u,   //  400
  0x80000000u,   //  600
  0xAAAAAAABu,   //  800
  0xD5555555u,   // 1000
};

// Table lookup by scale id. Static (file-scope) is fine here - these are
// pointers to const data, read only, never written.
static const uint32_t* const kScaleTable[kScaleCount] = {
  kScale12,      // unused for kScaleSmooth, present to keep indexing simple
  kScale12,
  kScaleMaj,
  kScaleMin,
  kScalePent,
  kScaleDim,
  kScaleWhole,
};

static const int kScaleLen[kScaleCount] = {
  12,   // unused
  12,
  7,
  7,
  5,
  8,
  6,
};

void ShepardInit() {
  // Floats HERE are correct and expected: this runs once, at startup, before
  // Run() installs the audio ISR. What must never happen is a float in the
  // sample path. -Wdouble-promotion / -Wfloat-conversion guard that; the
  // explicit casts below are what keep this function warning-clean.
  for (int i = 0; i < kSinLutSize; ++i) {
    const float th = 2.0f * 3.14159265358979f * (float)i / (float)kSinLutSize;
    sin_lut[i] = (int16_t)lrintf(sinf(th) * 32767.0f);
  }

  // 2^(k/256) in Q30. Entry 0 is 2^30 exactly; entry 256 is the guard, 2^31,
  // which does not fit int32 as a positive value - so it is stored as
  // 2^31 - 1. The resulting interpolation error in the top segment is 1 part
  // in 2^31, far below the table's own 0.9 ppm.
  for (int i = 0; i <= kPow2LutSize; ++i) {
    const double v = pow(2.0, (double)i / (double)kPow2LutSize) * 1073741824.0;
    pow2_lut[i] = (v >= 2147483647.0) ? 2147483647 : (int32_t)llrint(v);
  }
}

uint32_t DegreeToPitch(uint32_t pos_q32, int scale) {
  if (scale <= kScaleSmooth || scale >= kScaleCount) return pos_q32;

  const uint32_t* tab = kScaleTable[scale];
  const uint32_t n = (uint32_t)kScaleLen[scale];

  // Which degree, and how far through it. The octave carries over so the
  // stack keeps rising past the top of the scale.
  //
  // A 64-bit product would be the obvious form; this runs at control rate so
  // it would be affordable, but the split keeps the audio path's no-int64
  // rule intact everywhere rather than only where it is measured.
  const uint32_t hi = pos_q32 >> 16;
  const uint32_t idx = (hi * n) >> 16;          // 0 .. n-1
  const uint32_t within = (hi * n) & 0xFFFF;    // fraction through the degree

  const uint32_t here = tab[idx];
  const uint32_t next = (idx + 1u < n) ? tab[idx + 1u] : tab[0];

  // Interpolate across the gap so the pitch still glides between degrees
  // rather than jumping - the portamento in UpdateControl then smooths what
  // is left. Unsigned arithmetic wraps correctly at the octave.
  const uint32_t span = next - here;            // wraps for the last degree
  return here + (uint32_t)(((uint64_t)span * within) >> 16);
}

// Nearest degree, correct ACROSS THE OCTAVE WRAP.
//
// The wrap is the whole difficulty. A position at 1150 cents is nearer to the
// next octave's 0 (1200) than to 1100, so a plain "largest entry <= pos" walk
// gives the wrong answer for everything above the last degree. Comparing
// against the first entry plus a full octave handles it without a special
// case.
uint32_t SnapToScale(uint32_t pos_q32, int scale) {
  if (scale <= kScaleSmooth || scale >= kScaleCount) return pos_q32;

  const uint32_t* tab = kScaleTable[scale];
  const int n = kScaleLen[scale];

  // Find the last degree at or below pos. At most 12 compares - free at
  // control rate, and a binary search would not pay for its own complexity.
  int lo = 0;
  for (int i = 0; i < n; ++i) {
    if (tab[i] <= pos_q32) lo = i; else break;
  }

  const uint32_t below = tab[lo];
  // The next degree up, which for the last entry is the FIRST degree of the
  // octave above. Unsigned arithmetic wraps to exactly that.
  const uint32_t above = (lo + 1 < n) ? tab[lo + 1] : tab[0];

  const uint32_t d_below = pos_q32 - below;      // wraps correctly
  const uint32_t d_above = above - pos_q32;      // wraps correctly

  return (d_below <= d_above) ? below : above;
}

uint32_t StepScale(uint32_t pos_q32, int scale, int dir) {
  if (scale <= kScaleSmooth || scale >= kScaleCount) {
    return pos_q32;
  }

  const uint32_t* tab = kScaleTable[scale];
  const int n = kScaleLen[scale];

  // Start from where we actually are, snapped, so a step is always relative to
  // the audible pitch rather than to a free-running position that may have
  // drifted past a degree.
  const uint32_t snapped = SnapToScale(pos_q32, scale);

  int idx = 0;
  for (int i = 0; i < n; ++i) {
    if (tab[i] == snapped) { idx = i; break; }
  }

  idx += dir;
  // Wrapping the index is also wrapping the octave, which is exactly right:
  // stepping up off the top of the scale lands on the bottom, one octave
  // higher, and the Q32 arithmetic carries that for free.
  if (idx >= n) idx -= n;
  else if (idx < 0) idx += n;

  return tab[idx];
}

// PING decay coefficients, Q15 - see ping.h for the two approaches that
// overflowed before this one. Exponentially spaced, measured 11 ms to 1.7 s.
const int32_t kPingDecay[17] = {
  32275, 32407, 32503, 32574, 32626, 32664,
  32692, 32713, 32727, 32738, 32746, 32752,
  32756, 32760, 32762, 32763, 32765,
};

}  // namespace shepard
