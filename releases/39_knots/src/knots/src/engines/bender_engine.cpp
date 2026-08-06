#include "knots/src/engines/bender_engine.h"
#include "pico.h"

namespace mtws {

namespace {
// Converts knob/macros in 0..4095 to a Q12 interpolation range in 0..4096.
// The extra top code point allows an exact full-wet fold mix at maximum.
inline uint32_t U12ToQ12(uint16_t v) {
  if (v >= 4095U) return 4096U;
  return uint32_t(v);
}

// Maps macro X into the Utility-Pair gain domain used before the folder.
// `128` represents unity input gain because the folded input is scaled by `>> 7`.
// The upper bound `2048` was chosen because it matches the original Utility-Pair
// maximum multiplier range (~16x), giving a familiar amount of folding.
// Alternative considered: stop at `1024` (~8x) for a gentler maximum drive.
// CPU vs memory tradeoff: this is one multiply/shift at control rate and avoids
// storing a separate drive LUT for a simple linear mapping.
inline uint16_t FoldDriveQ7FromMacroX(uint16_t macro_x) {
  uint32_t x_q12 = U12ToQ12(macro_x);
  return uint16_t(128U + ((x_q12 * 1920U + 2048U) >> 12));
}

// Maps macro Y into the Utility-Pair crush step domain using the same squared
// response as the reference implementation. `0` means "effectively off" and is
// clamped to `1` inside CrushFunction, while `4095` gives the coarsest quantizer.
// Alternative considered: linear `1..4095`, but the squared curve gives more
// usable low-end travel before the crusher becomes extreme.
// CPU vs memory tradeoff: one control-rate multiply/shift keeps the mapping
// cheap and avoids an extra lookup table.
inline int16_t CrushAmountFromMacroY(uint16_t macro_y) {
  return int16_t((uint32_t(macro_y) * uint32_t(macro_y)) >> 12);
}
}  // namespace

BenderEngine::BenderEngine(SineLUT* lut)
    : lut_(lut),
      phase_(0),
      last_fold_integral_pre_(0),
      last_fold_input_pre_(0),
      last_fold_integral_post_(0),
      last_fold_input_post_(0),
      last_alt_(false) {}

// Resets oscillator phase and both antialiased fold histories.
void BenderEngine::Init() {
  phase_ = 0;
  last_fold_integral_pre_ = 0;
  last_fold_input_pre_ = 0;
  last_fold_integral_post_ = 0;
  last_fold_input_post_ = 0;
  last_alt_ = false;
}

void BenderEngine::OnSelected() {
  // Keep phase continuity.
}

// Converts the shared macro frame into the lighter-weight bender control frame.
// `X` controls both the fold drive and the dry/wet fold mix, while `Y` controls
// the Utility-Pair-style crush amount.
void BenderEngine::ControlTick(const GlobalControlFrame& global, EngineControlFrame& frame) {
  BenderControlFrame& out = frame.bender;
  out.phase_increment = global.pitch_inc;
  out.fold_drive_q7 = FoldDriveQ7FromMacroX(global.macro_x);
  out.fold_mix_q12 = uint16_t(U12ToQ12(global.macro_x));
  out.crush_amount = CrushAmountFromMacroY(global.macro_y);
  out.alt = global.mode_alt;
}

int32_t __not_in_flash_func(BenderEngine::Clamp12)(int32_t v) {
  if (v > 2047) return 2047;
  if (v < -2048) return -2048;
  return v;
}

// Wraps a signed value into the range 0..8191 using bitmask instead of modulo.
// Equivalent to ((v % 8192) + 8192) % 8192 but avoids two hardware divider
// calls (~16 cycles each on RP2040) by exploiting the power-of-2 period.
static inline int32_t WrapMod8192(int32_t v) {
  return v & 0x1FFF;
}

// Utility-Pair fold transfer function. Values within +/-2048 pass through,
// while larger magnitudes are reflected every 4096 samples.
int32_t __not_in_flash_func(BenderEngine::FoldFunction)(int32_t x) {
  x = WrapMod8192(x + 2048);

  if (x < 4096) {
    return x - 2048;
  }
  return (8191 - x) - 2048;
}

// Antiderivative of FoldFunction used for the derivative antialiasing step.
// This follows the Utility-Pair implementation directly so the folded sine uses
// the same transfer curve and the same antialiasing strategy.
int32_t __not_in_flash_func(BenderEngine::FoldIntegral)(int32_t x) {
  x = WrapMod8192(x + 2048);
  int32_t x2 = x * 2;
  if (x < 4096) {
    return ((x2 + 1) * (x2 - 8191)) >> 3;
  }
  return -((x2 - 8191) * (x2 - 16383)) >> 3;
}

// Differentiates successive fold integrals to suppress aliasing at fold corners.
// If the input sample did not move, the exact fold function is cheaper and
// produces the same result.
int32_t __not_in_flash_func(BenderEngine::AntiAliasedFold)(int32_t x, int32_t& last_integral, int32_t& last_input) {
  int32_t ret = 0;
  if (x == last_input) {
    ret = FoldFunction(x);
  } else {
    int32_t integral = FoldIntegral(x);
    ret = (integral - last_integral) / (x - last_input);
    last_input = x;
    last_integral = integral;
  }
  return Clamp12(ret);
}

// Utility-Pair bitcrusher transfer. `amount` is the quantizer step size in the
// signed 12-bit sample domain, so larger values produce fewer output levels.
// Uses floored division to avoid the two modulo operations in the original
// Utility-Pair implementation, saving ~32 cycles per call on RP2040.
int32_t __not_in_flash_func(BenderEngine::CrushFunction)(int32_t x, int32_t amount) {
  if (amount < 1) amount = 1;
  x += -2047 + (amount >> 1);
  int32_t q = x / amount;
  if (x < 0 && (q * amount != x)) q -= 1;
  x = q * amount + 2047;
  if (x < -2047) x = -2047;
  if (x > 2047) x = 2047;
  return x;
}

// Linear interpolation where `t_q12 = 0` returns dry and `4096` returns wet.
int32_t __not_in_flash_func(BenderEngine::LerpQ12)(int32_t a, int32_t b, uint32_t t_q12) {
  if (t_q12 > 4096U) t_q12 = 4096U;
  return a + int32_t((int64_t(b - a) * int32_t(t_q12)) >> 12);
}

// Renders one sine sample and routes it through the selected processing order.
// Normal mode keeps the current A/B comparison patch:
// - Out1: dry/wet sine -> fold
// - Out2: sine -> crusher
// Alt mode reorders the nonlinear stages:
// - Out1: fold -> crusher
// - Out2: crusher -> fold
void __not_in_flash_func(BenderEngine::RenderSample)(const EngineControlFrame& frame, int32_t& out1, int32_t& out2) {
  const BenderControlFrame& in = frame.bender;

  if (in.alt != last_alt_) {
    last_fold_integral_pre_ = 0;
    last_fold_input_pre_ = 0;
    last_fold_integral_post_ = 0;
    last_fold_input_post_ = 0;
    last_alt_ = in.alt;
  }

  phase_ += in.phase_increment;

  int32_t sine = lut_->LookupLinear(phase_);
  int32_t folded_input = int32_t((int64_t(sine) * in.fold_drive_q7) >> 7);
  int32_t folded = AntiAliasedFold(folded_input, last_fold_integral_pre_, last_fold_input_pre_);
  int32_t crushed = CrushFunction(sine, in.crush_amount);

  if (!in.alt) {
    out1 = Clamp12(LerpQ12(sine, folded, in.fold_mix_q12));
    out2 = crushed;
    return;
  }

  int32_t fold_then_crush = CrushFunction(folded, in.crush_amount);
  int32_t crush_then_fold_input = int32_t((int64_t(crushed) * in.fold_drive_q7) >> 7);
  int32_t crush_then_fold = AntiAliasedFold(crush_then_fold_input, last_fold_integral_post_, last_fold_input_post_);

  out1 = fold_then_crush;
  out2 = crush_then_fold;
}

}  // namespace mtws
