// octave.h - the octave-stack pitch shifter: the live-audio half of the card.
//
// One delay line, N read heads running at 2^i times the write rate. Every
// partial of the input is transposed into N octave-spaced copies, weighted by
// the same window as the oscillator bank - so a 220 Hz partial appears at 55,
// 110, 220, 440 Hz and each copy RISES and fades exactly as an oscillator
// layer does.
//
// This is the third attempt at the live path, and the first that applies the
// actual Shepard CONSTRUCTION to the input rather than an effect that passes
// over it. The two before it failed the same way:
//
//   FREQUENCY SHIFTER  moved every partial by the same hertz, breaking the
//                      harmonic series. The input came back unrecognisable,
//                      so the relationship between source and output was
//                      inaudible.
//   RISING COMB        filtered the input with sweeping bands. A given partial
//                      is only heard while a band centre passes it, so it came
//                      and went rather than participating in the illusion.
//
// Both treated the input as something to process. This one octave-stacks it,
// which is what the card DOES - the internal voice is octave-stacked sines,
// and this is octave-stacked you.
//
// The buffer is passed in rather than owned, so main.cpp keeps the single
// large allocation in one place.

#ifndef SHEPARD_OCTAVE_H_
#define SHEPARD_OCTAVE_H_

#include <stdint.h>
#include <string.h>

#include "fixed.h"
#include "shepard.h"

namespace shepard {

// 65536 samples = 1.365 s. The delay buffer is the card's one large
// allocation, at 128 KB.
//
// A read head must stay within one crossfade window of the write pointer, and
// the window is 16384 - so the buffer needs to be comfortably larger than
// that. 65536 leaves three quarters of it as slack.
static constexpr int kOctBits = 16;
static constexpr int kOctLen = 1 << kOctBits;
static constexpr uint32_t kOctMask = kOctLen - 1;

// Crossfade window for hiding a head's recycle, in samples.
// 16384 samples = 341 ms. Long, deliberately.
//
// A two-tap shifter's taps sit half a window apart in the BUFFER, which at
// rate r is only (win/2)/r apart in real time - so as the ratio rises the two
// taps converge on the same moment and the crossfade stops hiding anything.
// Measured at 4x: the wanted octave was dominant by -9.9 dB at win 4096, +0.6
// at 8192, and +18.5 at 16384. The window has to be long enough that the taps
// are still meaningfully separated at the highest ratio used.
//
// It also halves every recycle rate, which is the other artefact this
// structure has. The cost is transient smearing, which matters less here than
// on a delay because the source is being octave-stacked anyway.
static constexpr int kOctWin = 16384;

// Maximum shift ratio, as Q16. 4x = 262144.
//
// This cap is what keeps the top layers from buzzing, and it is the same kind
// of compromise as the comb's frequency clamp.
//
// A head reading at rate r drifts from the write pointer at |r-1| samples per
// second, so it must be recycled every (kOctLen/2)/|r-1| seconds - and that
// recycle rate is audible if it lands in the audio band:
//
//     rate  2x   recycles at  2.9 Hz   shimmer
//     rate  4x                8.8 Hz   shimmer
//     rate  8x               20.5 Hz   edge of a buzz
//     rate 16x               43.9 Hz   an audible tone
//     rate 32x               90.8 Hz   a loud buzz
//
// (those figures are for kOctWin = 16384; a shorter window doubles them)
//
// Measured with the window applied, a layer buzzing above 20 Hz still carried
// up to 0.50 of window gain at N=12 - so it would be plainly heard. Capping at
// 4x holds the worst case at 8.8 Hz.
//
// The stack is also CENTRED (see kOctCentre in main.cpp): layer i shifts by
// 2^(master + i - N/2), so the rates straddle unity instead of running from 1x
// upward. That alone took the worst recycle from 91 Hz to 30 Hz, because heads
// reading SLOWER than the write pointer drift slowly too.
static constexpr uint32_t kOctMaxRate = 262144u;

class OctaveStack {
 public:
  // The buffer is passed in so it can be shared with SPIRAL - see main.cpp.
  void Init(int16_t* buf) {
    buf_ = buf;
    memset(buf_, 0, sizeof(int16_t) * kOctLen);
    write_ = 0;
    for (int i = 0; i < kMaxLayers; ++i) drift_q16_[i] = 0;
  }

  // Write one input sample. Call once per sample, before reading any heads.
  void __not_in_flash_func(Write)(int32_t x) {
    buf_[write_ & kOctMask] = (int16_t)ClampQ15(x + 32768) - 32768;
    write_ = (write_ + 1u) & kOctMask;
  }

  // Rotate the read positions with the stack at an octave wrap.
  //
  // Same requirement as the oscillator phases and, previously, the comb's
  // resonator state: at a wrap head i inherits head i-1's shift ratio, so its
  // read position must move with it or the head jumps to an unrelated point in
  // the buffer - a click, once per octave.
  void __not_in_flash_func(RotateUp)() {
    for (int i = kMaxLayers - 1; i > 0; --i) drift_q16_[i] = drift_q16_[i - 1];
    drift_q16_[0] = 0;
  }

  void __not_in_flash_func(RotateDown)() {
    for (int i = 0; i < kMaxLayers - 1; ++i) drift_q16_[i] = drift_q16_[i + 1];
    drift_q16_[kMaxLayers - 1] = 0;
  }

  // Read head i, advancing at rate_q16 (65536 = unity).
  //
  // Two taps half a window apart, crossfaded with the same sin^2 the layer
  // window uses - so the sum of the two gains is constant and the recycle does
  // not amplitude-modulate. A linear crossfade would dip in the middle.
  int32_t __not_in_flash_func(Read)(int i, uint32_t rate_q16) {
    // Track the head's DRIFT relative to the write pointer, wrapped to one
    // window - not an absolute read position.
    //
    // The obvious form, an absolute pointer advancing at `rate`, does not work:
    // at 2x it races away from the write pointer at 1x, laps the buffer
    // repeatedly, and the crossfade window has no relationship to the lap
    // period. Measured, that put a spurious component 45 dB ABOVE the intended
    // octave. Accumulating (rate - unity) instead keeps the head permanently
    // within one window of the write pointer, which is what the crossfade is
    // sized to hide.
    drift_q16_[i] += rate_q16 - 65536u;
    while (drift_q16_[i] >= ((uint32_t)kOctWin << 16)) {
      drift_q16_[i] -= ((uint32_t)kOctWin << 16);
    }

    const uint32_t p0 = (((write_ - (uint32_t)kOctWin) << 16) + drift_q16_[i]);
    const uint32_t p1 = p0 + ((uint32_t)(kOctWin / 2) << 16);

    const int32_t a = Tap(p0);
    const int32_t b = Tap(p1);

    // Crossfade on the drift's position within the window, with the same sin^2
    // the layer window uses - so the two gains sum constant and the recycle
    // does not amplitude-modulate.
    const uint32_t u = (drift_q16_[i] >> 14) << (32 - 16);
    const int32_t g = HannQ15(u);

    return MulQ15(a, 32767 - g) + MulQ15(b, g);
  }

 private:
  int32_t __not_in_flash_func(Tap)(uint32_t p_q16) const {
    const uint32_t idx = (p_q16 >> 16) & kOctMask;
    const int32_t frac = (int32_t)(p_q16 & 0xFFFF);
    const int32_t s0 = buf_[idx];
    const int32_t s1 = buf_[(idx + 1) & kOctMask];
    return s0 + (((s1 - s0) * frac) >> 16);
  }

  int16_t* buf_ = nullptr;
  uint32_t write_ = 0;
  uint32_t drift_q16_[kMaxLayers];
};

}  // namespace shepard

#endif  // SHEPARD_OCTAVE_H_
