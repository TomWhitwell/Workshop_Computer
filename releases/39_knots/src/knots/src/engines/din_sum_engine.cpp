#include "knots/src/engines/din_sum_engine.h"
#include "pico.h"

namespace mtws {

namespace {
// Converts knob/macros in 0..4095 to the local Q12 range in 0..4096.
// The extra top code point preserves an exact pure-saw endpoint.
inline uint16_t ToQ12(uint16_t v) {
  return (v >= 4095U) ? 4096U : v;
}

// The shared MTWS control thread publishes one new engine frame every 16
// samples, so the random target interpolation spans exactly 16 audio samples.
constexpr uint8_t kRandomInterpolationSteps = 16U;
// Since the interpolation length is 16 samples, `>> 4` maps the interpolated
// delta back into the native random-value domain.
constexpr uint8_t kRandomInterpolationShift = 4U;
// The low-passed random target is updated once per 16-sample control frame. A
// shift of 2 moves 25% toward the latest random value each update, which keeps
// the smooth branch visibly different from the residual high-passed branch.
// Alternative considered: shift 3 for slower drift, but it reduced the
// liveliness of normal mode too far.
// CPU vs memory tradeoff: one add/subtract/shift at control rate is cheaper
// than maintaining a larger smoothing buffer or a lookup-based response curve.
constexpr uint8_t kFilteredRandomShift = 2U;
// Unity in the local Q12 morph convention.
constexpr uint16_t kUnityQ12 = 4096U;
// Half scale in the local Q12 morph convention. This is the largest legal
// deviation range because the bounded morph corridor shrinks symmetrically from
// both endpoints toward the midpoint.
constexpr uint16_t kHalfQ12 = 2048U;
// Alt-mode wrap stickiness spans 1..8 cycles. Low Y holds the same waveform
// family for several cycles, while high Y rerolls almost every cycle.
constexpr uint8_t kAltHoldCyclesMin = 1U;
constexpr uint8_t kAltHoldCyclesMax = 8U;
// A 90-degree phase offset in the native 32-bit phase domain. This keeps Out2
// clearly different at the saw end without causing the strong octave-only mono
// sum of a full 180-degree offset.
constexpr uint32_t kOut2SawPhaseOffset = 0x40000000U;
}  // namespace

// Creates the engine and stores the shared sine LUT dependency.
// Input: pointer to the global LUT owned by the MTWS app.
// Output: engine ready to reset and render.
DinSumEngine::DinSumEngine(SineLUT* lut)
    : lut_(lut),
      random_state_(1U),
      alt_random_state_(0x6d2b79f5U),
      phase_(0U),
      control_random_lp_next_(0),
      control_random_hp_next_(0),
      frame_epoch_counter_(0U),
      active_frame_epoch_(0U),
      random_interp_step_(0U),
      active_alt_mode_(false),
      alt_cycle_is_saw_out1_(false),
      alt_cycle_is_saw_out2_(false),
      alt_cycle_hold_out1_(0U),
      alt_cycle_hold_out2_(0U) {
  Init();
}

// Resets phase, random target state, and the audio-rate interpolation cursor to
// deterministic startup values.
void DinSumEngine::Init() {
  random_state_ = 1U;
  alt_random_state_ = 0x6d2b79f5U;
  phase_ = 0U;
  control_random_lp_next_ = 0;
  control_random_hp_next_ = 0;
  frame_epoch_counter_ = 0U;
  active_frame_epoch_ = 0U;
  random_interp_step_ = 0U;
  active_alt_mode_ = false;
  alt_cycle_is_saw_out1_ = false;
  alt_cycle_is_saw_out2_ = false;
  alt_cycle_hold_out1_ = 0U;
  alt_cycle_hold_out2_ = 0U;
}

void DinSumEngine::OnSelected() {
  // Keep phase continuity and the current random state when switching back in.
}

// Clamps any intermediate sample to the signed 12-bit ComputerCard audio range.
// Input: unconstrained signed sample.
// Output: sample limited to -2048..2047.
int32_t __not_in_flash_func(DinSumEngine::Clamp12)(int32_t v) {
  if (v > 2047) return 2047;
  if (v < -2048) return -2048;
  return v;
}

// Interpolates between two signed samples with a Q12 morph amount.
// Inputs: endpoints `a` and `b`, and `t_q12` in the range 0..4096.
// Output: a point guaranteed to stay inside the closed interval [a, b].
int32_t __not_in_flash_func(DinSumEngine::LerpQ12)(int32_t a, int32_t b, uint16_t t_q12) {
  uint32_t t = t_q12;
  if (t > kUnityQ12) t = kUnityQ12;
  return int32_t(int64_t(a) + ((int64_t(b) - int64_t(a)) * int64_t(t) >> 12));
}

// Generates a PolyBLEP-corrected saw using a precomputed reciprocal to avoid
// 64-bit divisions at audio rate. See SawsomeEngine for the reciprocal scheme.
int32_t __not_in_flash_func(DinSumEngine::PolyBlepSawQ12)(uint32_t phase, uint32_t phase_inc, uint32_t recip_q24) {
  int32_t saw = int32_t((phase >> 20) & 0x0FFFU) - 2048;
  if (phase_inc == 0U) return saw;

  if (phase < phase_inc) {
    uint32_t t_q12 = uint32_t((uint64_t(phase) * recip_q24) >> 24);
    int32_t x = int32_t(t_q12);
    int32_t corr = x + x - int32_t((int64_t(x) * x) >> 12) - 4096;
    saw -= corr >> 1;
  }

  uint32_t tail = 0xFFFFFFFFU - phase;
  if (tail < phase_inc) {
    uint32_t t_q12 = uint32_t((uint64_t(tail) * recip_q24) >> 24);
    int32_t x = int32_t(t_q12);
    int32_t corr = int32_t((int64_t(x) * x) >> 12);
    saw += corr >> 1;
  }

  return Clamp12(saw);
}

// Advances the control-rate xorshift generator and returns a bipolar 12-bit
// random target suitable for bounded morph modulation.
// Input/output: mutable PRNG state.
// Output: signed value approximately in the range -2048..2047.
int16_t DinSumEngine::Rand12Bipolar(uint32_t& state) {
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return int16_t(((state >> 20) & 0x0FFFU) - 2048);
}

// Advances the xorshift generator and returns a unipolar 12-bit random value.
// Input/output: mutable PRNG state.
// Output: unsigned value in the range 0..4095 for probability comparisons.
uint16_t DinSumEngine::Rand12Unipolar(uint32_t& state) {
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return uint16_t((state >> 20) & 0x0FFFU);
}

// Builds one shared-engine control frame. This captures the current phase
// increment and macro values, then advances the low-pass and high-pass random
// targets used for the next 16-sample audio interpolation span.
// X controls the sine-to-saw morph position, while Y controls the random-source
// character in normal mode and the cycle-choice stickiness in alt mode.
void DinSumEngine::ControlTick(const GlobalControlFrame& global, EngineControlFrame& frame) {
  DinSumControlFrame& out = frame.din_sum;

  const uint16_t morph_q12 = ToQ12(global.macro_x);
  const uint16_t coherence_q12 = ToQ12(global.macro_y);
  const int16_t random_lp_prev = control_random_lp_next_;
  const int16_t random_hp_prev = control_random_hp_next_;
  const int16_t random_raw = Rand12Bipolar(random_state_);
  const int16_t random_lp_next = int16_t(
      int32_t(random_lp_prev) + ((int32_t(random_raw) - int32_t(random_lp_prev)) >> kFilteredRandomShift));

  // The high-passed branch is the residual between the raw target and the
  // smoothed branch. Clamping keeps it inside the same nominal bipolar range so
  // the later bounded-corridor math behaves consistently.
  int32_t random_hp_next = int32_t(random_raw) - int32_t(random_lp_next);
  if (random_hp_next < -2048) random_hp_next = -2048;
  if (random_hp_next > 2047) random_hp_next = 2047;

  control_random_lp_next_ = random_lp_next;
  control_random_hp_next_ = int16_t(random_hp_next);
  ++frame_epoch_counter_;

  out.phase_increment = global.pitch_inc;
  out.phase_inc_recip_q24 = (global.pitch_inc > 0) ? uint32_t((1ULL << 36) / global.pitch_inc) : 0U;
  out.morph_q12 = morph_q12;
  out.coherence_q12 = coherence_q12;
  out.random_lp_prev = random_lp_prev;
  out.random_lp_next = random_lp_next;
  out.random_hp_prev = random_hp_prev;
  out.random_hp_next = int16_t(random_hp_next);
  out.alt = global.mode_alt;
  out.frame_epoch = frame_epoch_counter_;
}

// Renders one stereo sample. In normal mode, both outputs choose a bounded
// random interpolation point between the shared sine and their saw targets.
// Out1 uses the local saw, while Out2 uses the same saw evaluated 90 degrees
// later in phase. In alt mode, each output chooses sine or saw for whole cycles
// with the same X-controlled probability but independent rerolls.
void __not_in_flash_func(DinSumEngine::RenderSample)(const EngineControlFrame& frame, int32_t& out1, int32_t& out2) {
  const DinSumControlFrame& in = frame.din_sum;

  if (in.frame_epoch != active_frame_epoch_) {
    active_frame_epoch_ = in.frame_epoch;
    random_interp_step_ = 0U;
  }

  const uint32_t previous_phase = phase_;
  phase_ += in.phase_increment;
  const bool wrapped = phase_ < previous_phase;

  const int32_t sine = lut_->LookupLinear(phase_);
  const int32_t saw_up = PolyBlepSawQ12(phase_, in.phase_increment, in.phase_inc_recip_q24);
  const int32_t saw_up_shifted = PolyBlepSawQ12(phase_ + kOut2SawPhaseOffset, in.phase_increment, in.phase_inc_recip_q24);

  if (in.alt) {
    // Low Y increases hold length so waveform-family decisions persist across
    // several wraps. High Y reduces the hold toward one cycle so alt mode
    // flickers more aggressively.
    const uint8_t hold_cycles = uint8_t(
        kAltHoldCyclesMax -
        ((uint32_t(in.coherence_q12) * uint32_t(kAltHoldCyclesMax - kAltHoldCyclesMin) + 2048U) >> 12));

    // Entering alt mode always starts fresh. On later wraps, reroll only when
    // the current choice has been held for the requested number of cycles.
    if (!active_alt_mode_) {
      active_alt_mode_ = true;
      alt_cycle_is_saw_out1_ = Rand12Unipolar(alt_random_state_) < in.morph_q12;
      alt_cycle_is_saw_out2_ = Rand12Unipolar(alt_random_state_) < in.morph_q12;
      alt_cycle_hold_out1_ = 0U;
      alt_cycle_hold_out2_ = 0U;
    } else if (wrapped) {
      if (++alt_cycle_hold_out1_ >= hold_cycles) {
        alt_cycle_is_saw_out1_ = Rand12Unipolar(alt_random_state_) < in.morph_q12;
        alt_cycle_hold_out1_ = 0U;
      }
      if (++alt_cycle_hold_out2_ >= hold_cycles) {
        alt_cycle_is_saw_out2_ = Rand12Unipolar(alt_random_state_) < in.morph_q12;
        alt_cycle_hold_out2_ = 0U;
      }
    }

    out1 = alt_cycle_is_saw_out1_ ? saw_up : sine;
    out2 = alt_cycle_is_saw_out2_ ? saw_up_shifted : sine;
    return;
  }

  active_alt_mode_ = false;
  alt_cycle_hold_out1_ = 0U;
  alt_cycle_hold_out2_ = 0U;

  // The interpolated random value is audio-rate smooth but only refreshed once
  // per 16-sample control block. The `>> 4` shift matches the block length
  // exactly. Alternative considered: stepped sample-and-hold, but the
  // interpolated form preserves pitch better and avoids obvious zippering.
  const int32_t random_lp_interp = int32_t(in.random_lp_prev) +
      (((int32_t(in.random_lp_next) - int32_t(in.random_lp_prev)) * int32_t(random_interp_step_)) >>
       kRandomInterpolationShift);
  const int32_t random_hp_interp = int32_t(in.random_hp_prev) +
      (((int32_t(in.random_hp_next) - int32_t(in.random_hp_prev)) * int32_t(random_interp_step_)) >>
       kRandomInterpolationShift);
  if (random_interp_step_ < (kRandomInterpolationSteps - 1U)) {
    ++random_interp_step_;
  }

  // Y crossfades between smooth and nervous random sources. Out1 follows the
  // knob directly, while Out2 uses the inverse crossfade so the outputs swap
  // character across the sweep and meet toward the midpoint.
  const int32_t random_interp_out1 = LerpQ12(random_lp_interp, random_hp_interp, in.coherence_q12);
  const int32_t random_interp_out2 = LerpQ12(random_hp_interp, random_lp_interp, in.coherence_q12);

  // `room_q12` is the maximum legal deviation from the knob-defined morph
  // position while staying inside the interval [0, 4096]. Using the smaller of
  // the two distances to the endpoints makes the instability collapse naturally
  // at both edges. The Q12 domain was kept because it preserves exact sine and
  // saw endpoints while staying cheap enough for audio-rate fixed-point math.
  // Alternative considered: 0..4095, but 4096 preserves an exact full-saw
  // endpoint in the final `>> 12` lerp.
  // CPU vs memory tradeoff: this is one compare/subtract per sample instead of
  // a lookup-based curve or precomputed modulation table.
  const uint16_t room_q12 =
      (in.morph_q12 < kHalfQ12) ? in.morph_q12 : uint16_t(kUnityQ12 - in.morph_q12);

  // `random_interp` is roughly +/-2048, so `>> 11` maps the product back into a
  // legal Q12 offset around the knob-defined morph position. A larger shift was
  // considered, but it made the center region too tame. A smaller shift pushed
  // too aggressively into the clamps and wasted corridor resolution.
  const int32_t offset_q12_out1 = (random_interp_out1 * int32_t(room_q12)) >> 11;
  int32_t schrodinger_p_q12_out1 = int32_t(in.morph_q12) + offset_q12_out1;
  if (schrodinger_p_q12_out1 < 0) schrodinger_p_q12_out1 = 0;
  if (schrodinger_p_q12_out1 > int32_t(kUnityQ12)) schrodinger_p_q12_out1 = int32_t(kUnityQ12);

  const int32_t offset_q12_out2 = (random_interp_out2 * int32_t(room_q12)) >> 11;
  int32_t schrodinger_p_q12_out2 = int32_t(in.morph_q12) + offset_q12_out2;
  if (schrodinger_p_q12_out2 < 0) schrodinger_p_q12_out2 = 0;
  if (schrodinger_p_q12_out2 > int32_t(kUnityQ12)) schrodinger_p_q12_out2 = int32_t(kUnityQ12);

  out1 = Clamp12(LerpQ12(sine, saw_up, uint16_t(schrodinger_p_q12_out1)));
  out2 = Clamp12(LerpQ12(sine, saw_up_shifted, uint16_t(schrodinger_p_q12_out2)));
}

}  // namespace mtws
