// fixed.h - fixed-point primitives for BARBER'S POLE.
//
// The RP2040's Cortex-M0+ has no FPU. A float multiply is soft-emulated at
// ~360 ns against a 20.8 us per-SAMPLE budget, so a single float in the audio
// path is not a slow choice - it is a broken one. Everything here exists to
// keep the DSP in integers.
//
// It also has no 64-bit multiply. `(int64_t)a * b >> 15` compiles to a call to
// __aeabi_lmul plus sign extension, 64-bit adds with carry and register
// spills. WorkshopSpectral measured that at over twice its frame budget before
// MulQ15 replaced it. So: no int64 in the hot path, ever.
//
// Verify after any refactor - the binary should contain no lmul at all:
//     arm-none-eabi-nm build/shepard.elf | grep aeabi_lmul     # want nothing
//
// Unlike WorkshopSpectral, this card does NOT set PICO_INT64_OPS_IN_RAM. That
// flag exists there because FFT butterflies genuinely need 64-bit products.
// Here an __aeabi_lmul in the binary means a bug to find, not a flag to add.

#ifndef SHEPARD_FIXED_H_
#define SHEPARD_FIXED_H_

#include <stdint.h>

// Host builds (the tools/ checks) have no Pico SDK; the attribute only matters
// on target, where it forces the function into RAM. Lifted from
// WorkshopSpectral's fft.h - the whole host-test mechanism depends on it.
#ifdef SHEPARD_HOST_BUILD
  #define __not_in_flash_func(f) f
#else
  #include "pico.h"
#endif

// REQUIRED for the Workshop Computer's crystal. Without it the card fails to
// boot on a cold power-up but works from a warm reset, which is exactly what
// makes the bug so confusing to chase.
//
// This is also set in CMakeLists.txt via target_compile_definitions. The
// duplication is deliberate: the upstream registry's PR validator greps the
// SOURCE for the #define form (tools/sitegen/src/validate/prRules.js) and does
// not see the CMake flag. The guard keeps the two from colliding.
#ifndef PICO_XOSC_STARTUP_DELAY_MULTIPLIER
#define PICO_XOSC_STARTUP_DELAY_MULTIPLIER 64
#endif

namespace shepard {

// Q15 multiply-and-shift, without any 64-bit arithmetic.
//
// LIFTED UNCHANGED from WorkshopSpectral's fft.h, including this derivation,
// because it is already verified bit-exact against the int64 form over 200,000
// random (w, x) pairs.
//
// Splitting x at bit 15 keeps everything in int32:
//
//     x = (x >> 15) * 32768 + lo,  lo = x - ((x >> 15) << 15)  in 0..32767
//     (w * x) >> 15  ==  w * (x >> 15) + ((w * lo) >> 15)
//
// Both products fit comfortably: |w| <= 32767 and |x| <= 20e6 gives at most
// ~2.0e9 against an int32 limit of 2.147e9.
static inline int32_t __not_in_flash_func(MulQ15)(int32_t w, int32_t x) {
  const int32_t hi = x >> 15;
  const int32_t lo = x - (hi << 15);
  return w * hi + ((w * lo) >> 15);
}

// Q30 multiply, for the Hilbert allpass coefficients.
//
// Same hi/lo split principle as MulQ15, but the coefficient rather than the
// data is the wide operand: the allpass coefficients are a^2 in Q30 (up to
// 1071056573, which needs all 30 bits) while the signal stays inside +-2^24.
//
//     a = ah * 32768 + al,   ah = a >> 15,  al = a & 0x7FFF
//     (a * x) >> 30  ==  ((ah * x) >> 15) + ((al * x) >> 30)
//
// Headroom: |x| <= 2^24 and ah <= 32767 gives ah*x <= 5.5e11 - too big. So the
// data is shifted down FIRST in the high term, which is exact for the bits
// that survive a >>30 anyway:
//
//     ((ah * (x >> 15)) << 0) + ((ah * (x & 0x7FFF)) >> 15) + ((al * (x >> 15)) >> 15)
//
// The dropped fourth term (al * xl >> 30) is at most 0.5 LSB. Verified against
// the int64 form in tools/hilbert_check.py.
static inline int32_t __not_in_flash_func(MulQ30)(int32_t a, int32_t x) {
  const int32_t ah = a >> 15;
  const int32_t al = a & 0x7FFF;
  const int32_t xh = x >> 15;
  const int32_t xl = x - (xh << 15);
  return ah * xh + ((ah * xl) >> 15) + ((al * xh) >> 15);
}

// Clamp to the +-2047 the DAC accepts. AudioOut/CVOut take int16_t but the
// hardware is 12-bit signed; anything outside wraps rather than saturating,
// which is a full-scale glitch rather than a clip.
static inline int32_t __not_in_flash_func(ClampDac)(int32_t v) {
  if (v > 2047) return 2047;
  if (v < -2048) return -2048;
  return v;
}

// Soft clip at the DAC's scale, for the final output sum.
//
// LIFTED FROM WorkshopSpectral's tape.h, including the derivation. Linear
// below the knee, then a RATIONAL curve that approaches the limit
// asymptotically and NEVER pins:
//
//     y = knee + over*room/(room + over),  over = |x| - knee
//
// That last property is the point. The quadratic knee it replaced had a narrow
// soft region and simply flattened above it, which is what made loud material
// sound clipped on hardware even after the gain staging was right.
static inline int32_t __not_in_flash_func(SoftClipOut)(int32_t x) {
  const int32_t lim = 2047;
  const int32_t knee = 1450;
  const int32_t ax = x < 0 ? -x : x;
  if (ax <= knee) return x;
  const int32_t over = ax - knee;
  const int32_t room = lim - knee;
  const int32_t y = knee + (over * room) / (room + over);
  return x < 0 ? -y : y;
}

static inline int32_t __not_in_flash_func(ClampQ15)(int32_t v) {
  if (v > 32767) return 32767;
  if (v < 0) return 0;
  return v;
}

}  // namespace shepard

#endif  // SHEPARD_FIXED_H_
