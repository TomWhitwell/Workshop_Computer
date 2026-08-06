#include <stdint.h>

#include "BreakyAudioBank.h"
#include "BreakySampleManager.h"
#include "ComputerCard.h"
#include "hardware/clocks.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"

class Breaky : public ComputerCard {
 public:
  Breaky() = default;

  void ProcessSample() override {
    if (boot_mute_samples_ > 0) {
      --boot_mute_samples_;
      AudioOut1(0);
      AudioOut2(0);
      stop_switch_pulse_outputs();
      for (int i = 0; i < kNumLeds; ++i) {
        LedOff(i);
      }
      return;
    }

    if (breaky_audio_bank_mutating()) {
      AudioOut1(0);
      AudioOut2(0);
      stop_switch_pulse_outputs();
      update_empty_leds();
      return;
    }

    update_random_cv_outputs();

    const bool has_audio = breaky_audio_sample_count() > 0;
    if (has_audio) {
      ensure_active_sample_valid();
    }
    handle_switch_actions(has_audio);
    update_switch_pulse_outputs();

    if (!has_audio) {
      AudioOut1(0);
      AudioOut2(0);
      update_empty_leds();
      return;
    }

    handle_pulse_cv_position();
    update_external_clock();
    update_playback_rate();
    update_timestretch();

    const StereoFrame output =
        timestretch_active_ ? render_stretched_sample() : render_direct_sample();

    AudioOut1(output.left);
    AudioOut2(output.right);

    update_leds();
  }

 private:
  static constexpr int kNumLeds = 6;
  static constexpr uint32_t kAudioOutputSampleRate = 48000u;
  static constexpr uint32_t kBootMuteSamples = kAudioOutputSampleRate / 10u;
  static constexpr uint32_t kLedUpdateDivider = 1024u;
  static constexpr uint32_t kKnobMax = 4095u;
  static constexpr uint32_t kTempoMinBpm = 100u;
  static constexpr uint32_t kTempoRangeBpm = 100u;
  static constexpr uint32_t kControlUpdateDivider = kAudioOutputSampleRate / 1000u;
  static constexpr uint32_t kExternalClockMinIntervalSamples = kAudioOutputSampleRate / 1000u;
  static constexpr uint32_t kExternalClockMinTimeoutSamples = kAudioOutputSampleRate / 2u;
  static constexpr uint32_t kExternalClockTimeoutMultiplier = 4u;
  static constexpr uint32_t kPulseFlashSamples = kAudioOutputSampleRate / 20u;
  static constexpr uint32_t kExternalClockPpqn = 2u;
  static constexpr uint16_t kSwitchDebounceSamples = 96u;
  static constexpr uint32_t kSwitchPulseOutSamples = kAudioOutputSampleRate / 50u;
  static constexpr int16_t kCvMin = -2048;
  static constexpr int16_t kCvMax = 2047;
  static constexpr uint32_t kStretchQ8One = 256u;
  static constexpr uint32_t kStretchQ8Bypass = (11u * kStretchQ8One + 5u) / 10u;
  static constexpr uint32_t kStretchQ8Max = 10u * kStretchQ8One;
  static constexpr uint32_t kGrainLengthSamples = 2048u;
  static constexpr uint32_t kGrainHopSamples = 1024u;
  static constexpr uint32_t kGrainHopShift = 10u;
  static constexpr uint32_t kRandomCv1PeriodTicks = 14286u;  // 0.07 Hz at ~1 kHz.
  static constexpr uint32_t kRandomCv2PeriodTicks = 9091u;   // 0.11 Hz at ~1 kHz.
  static constexpr uint32_t kRandomCv1LagAlphaQ16 = 5u;
  static constexpr uint32_t kRandomCv2LagAlphaQ16 = 7u;
  static constexpr int32_t kQ16One = 65536;
  static constexpr uint64_t kSourceFrameIncQ32 =
      (static_cast<uint64_t>(BREAKY_BANK_SAMPLE_RATE) << 32u) / kAudioOutputSampleRate;

  struct StereoFrame {
    int16_t left;
    int16_t right;
  };

  struct Grain {
    uint64_t start_phase_q32;
    uint64_t phase_inc_q32;
    uint16_t age;
  };

  struct SmoothRandomLfo {
    int32_t value_q16;
    int32_t target_q16;
    uint32_t ticks_until_target;
    uint32_t period_ticks;
    uint32_t lag_alpha_q16;
  };

  uint64_t phase_q32_ = 0;
  uint64_t phase_inc_q32_ = kSourceFrameIncQ32;
  uint64_t timestretch_source_inc_q32_ = kSourceFrameIncQ32;
  uint32_t led_divider_ = 0;
  uint32_t control_update_divider_ = kControlUpdateDivider;
  uint32_t stretch_update_divider_ = kControlUpdateDivider;
  uint32_t random_cv_update_divider_ = kControlUpdateDivider;
  uint32_t boot_mute_samples_ = kBootMuteSamples;
  uint32_t clock_age_samples_ = 0;
  uint32_t empty_led_samples_ = 0;
  uint32_t last_external_clock_interval_ = 0;
  uint32_t clock_timeout_samples_ = kExternalClockMinTimeoutSamples;
  uint32_t pulse_flash_samples_ = 0;
  uint32_t switch_pulse1_samples_ = 0;
  uint32_t switch_pulse2_samples_ = 0;
  uint32_t stretch_q8_ = kStretchQ8One;
  uint32_t random_cv_seed_ = 0x8badf00du;
  uint16_t switch_down_samples_ = 0;
  uint16_t switch_up_samples_ = 0;
  uint8_t active_sample_ = 0;
  SmoothRandomLfo random_cv1_ = {0, 0, 0, kRandomCv1PeriodTicks, kRandomCv1LagAlphaQ16};
  SmoothRandomLfo random_cv2_ = {0, 0, 0, kRandomCv2PeriodTicks, kRandomCv2LagAlphaQ16};
  Grain grains_[2] = {{0, kSourceFrameIncQ32, 0},
                      {0, kSourceFrameIncQ32, kGrainHopSamples}};
  bool clock_seen_once_ = false;
  bool external_clock_active_ = false;
  bool switch_jump_armed_ = true;
  bool switch_change_armed_ = true;
  bool timestretch_active_ = false;
  bool grains_initialized_ = false;

  void ensure_active_sample_valid() {
    const uint32_t count = breaky_audio_sample_count();
    if (count == 0) {
      active_sample_ = 0;
    } else if (active_sample_ >= count) {
      active_sample_ = 0;
      phase_q32_ = 0;
      invalidate_timestretch_grains();
    }
  }

  static int16_t decode_sample(uint8_t value) {
    const int16_t signed_sample = static_cast<int8_t>(value);
    return static_cast<int16_t>(signed_sample * 16);
  }

  static int16_t interpolate(int16_t a, int16_t b, uint32_t frac) {
    const int32_t diff = static_cast<int32_t>(b) - static_cast<int32_t>(a);
    return static_cast<int16_t>(
        static_cast<int32_t>(a) + static_cast<int32_t>((static_cast<int64_t>(diff) * frac) >> 32u));
  }

  static int16_t clamp_cv(int32_t value) {
    if (value < kCvMin) {
      return kCvMin;
    }
    if (value > kCvMax) {
      return kCvMax;
    }
    return static_cast<int16_t>(value);
  }

  static uint32_t clamp_knob(int32_t value) {
    if (value < 0) {
      return 0;
    }
    if (value > static_cast<int32_t>(kKnobMax)) {
      return kKnobMax;
    }
    return static_cast<uint32_t>(value);
  }

  uint32_t next_random_u32() {
    uint32_t x = random_cv_seed_;
    x ^= x << 13u;
    x ^= x >> 17u;
    x ^= x << 5u;
    random_cv_seed_ = x;
    return x;
  }

  int16_t random_bipolar_cv() {
    return static_cast<int16_t>(kCvMin + static_cast<int32_t>(next_random_u32() & 0x0fffu));
  }

  const BreakyAudioSample& current_sample() const {
    return breaky_audio_sample(active_sample_);
  }

  uint64_t loop_len_q32() const {
    return static_cast<uint64_t>(current_sample().frame_count) << 32u;
  }

  uint64_t wrap_phase(uint64_t phase) const {
    const uint64_t loop_len = loop_len_q32();
    while (phase >= loop_len) {
      phase -= loop_len;
    }
    return phase;
  }

  uint64_t subtract_phase(uint64_t phase, uint64_t amount) const {
    const uint64_t loop_len = loop_len_q32();
    amount %= loop_len;
    if (phase >= amount) {
      return phase - amount;
    }
    return loop_len - (amount - phase);
  }

  static uint32_t grain_window(uint16_t age) {
    if (age >= kGrainLengthSamples) {
      return 0;
    }
    if (age <= kGrainHopSamples) {
      return age;
    }
    return kGrainLengthSamples - age;
  }

  StereoFrame read_frame(uint32_t frame) const {
    const int16_t sample =
        decode_sample(breaky_audio_read_byte(current_sample().offset + frame));
    return {sample, sample};
  }

  StereoFrame read_interpolated_phase(uint64_t phase_q32) const {
    phase_q32 = wrap_phase(phase_q32);
    const uint32_t frame = static_cast<uint32_t>(phase_q32 >> 32u);
    const uint32_t frame_count = current_sample().frame_count;
    const uint32_t next_frame = frame + 1u < frame_count ? frame + 1u : 0u;
    const uint32_t frac = static_cast<uint32_t>(phase_q32);

    const StereoFrame a = read_frame(frame);
    const StereoFrame b = read_frame(next_frame);
    const int16_t left = interpolate(a.left, b.left, frac);
    const int16_t right = interpolate(a.right, b.right, frac);
    return {left, right};
  }

  uint32_t current_frame() const {
    return static_cast<uint32_t>(phase_q32_ >> 32u);
  }

  void advance_phase() {
    advance_phase_by(phase_inc_q32_);
  }

  void advance_phase_by(uint64_t increment_q32) {
    phase_q32_ = wrap_phase(phase_q32_ + increment_q32);
  }

  void update_playback_rate() {
    if (external_clock_active_) {
      control_update_divider_ = kControlUpdateDivider;
      return;
    }

    if (control_update_divider_ < kControlUpdateDivider) {
      ++control_update_divider_;
      return;
    }
    control_update_divider_ = 0;

    refresh_free_playback_rate();
  }

  void refresh_free_playback_rate() {
    const uint32_t knob = static_cast<uint32_t>(KnobVal(X));
    const uint32_t target_bpm =
        kTempoMinBpm + ((knob * kTempoRangeBpm) + (kKnobMax / 2u)) / kKnobMax;
    phase_inc_q32_ =
        ((static_cast<uint64_t>(BREAKY_BANK_SAMPLE_RATE) * target_bpm) << 32u) /
        (static_cast<uint64_t>(kAudioOutputSampleRate) * current_sample().source_bpm);
    if (timestretch_active_) {
      refresh_timestretch_source_inc();
    }
  }

  void update_timestretch() {
    if (stretch_update_divider_ < kControlUpdateDivider) {
      ++stretch_update_divider_;
      return;
    }
    stretch_update_divider_ = 0;

    int32_t stretch_control = KnobVal(Main);
    if (Connected(Input::CV1)) {
      stretch_control += CVIn1();
    }
    const bool was_active = timestretch_active_;
    const uint32_t target_stretch_q8 = stretch_from_knob_q8(clamp_knob(stretch_control));
    if (target_stretch_q8 < kStretchQ8Bypass) {
      timestretch_active_ = false;
      stretch_q8_ = kStretchQ8One;
      timestretch_source_inc_q32_ = phase_inc_q32_;
      if (was_active) {
        invalidate_timestretch_grains();
      }
      return;
    }

    timestretch_active_ = true;
    stretch_q8_ = target_stretch_q8;
    refresh_timestretch_source_inc();
    if (!was_active) {
      invalidate_timestretch_grains();
    }
  }

  static uint32_t stretch_from_knob_q8(uint32_t knob) {
    const uint64_t eased_knob = static_cast<uint64_t>(knob) * knob * knob;
    const uint64_t eased_max = static_cast<uint64_t>(kKnobMax) * kKnobMax * kKnobMax;
    const uint64_t range = kStretchQ8Max - kStretchQ8One;
    return static_cast<uint32_t>(
        kStretchQ8One + ((eased_knob * range) + (eased_max / 2u)) / eased_max);
  }

  void refresh_timestretch_source_inc() {
    timestretch_source_inc_q32_ = (phase_inc_q32_ << 8u) / stretch_q8_;
  }

  void update_random_cv_outputs() {
    if (random_cv_update_divider_ < kControlUpdateDivider) {
      ++random_cv_update_divider_;
      return;
    }
    random_cv_update_divider_ = 0;

    tick_random_lfo(random_cv1_);
    tick_random_lfo(random_cv2_);
    CVOut1(q16_to_cv(random_cv1_.value_q16));
    CVOut2(q16_to_cv(random_cv2_.value_q16));
  }

  void tick_random_lfo(SmoothRandomLfo& lfo) {
    if (lfo.ticks_until_target == 0) {
      lfo.target_q16 = static_cast<int32_t>(random_bipolar_cv()) * kQ16One;
      lfo.ticks_until_target = lfo.period_ticks;
    } else {
      --lfo.ticks_until_target;
    }

    const int64_t delta =
        static_cast<int64_t>(lfo.target_q16) - static_cast<int64_t>(lfo.value_q16);
    lfo.value_q16 += static_cast<int32_t>((delta * lfo.lag_alpha_q16) / kQ16One);
  }

  static int16_t q16_to_cv(int32_t value_q16) {
    return clamp_cv(value_q16 / kQ16One);
  }

  void update_external_clock() {
    if (clock_age_samples_ < UINT32_MAX) {
      ++clock_age_samples_;
    }
    if (pulse_flash_samples_ > 0) {
      --pulse_flash_samples_;
    }

    if (PulseIn1RisingEdge()) {
      pulse_flash_samples_ = kPulseFlashSamples;

      const uint32_t interval = clock_age_samples_;
      clock_age_samples_ = 0;

      if (clock_seen_once_ && interval >= kExternalClockMinIntervalSamples) {
        set_external_clock_interval(interval);
      }
      clock_seen_once_ = true;
    }

    if (external_clock_active_ && clock_age_samples_ > clock_timeout_samples_) {
      external_clock_active_ = false;
      clock_seen_once_ = false;
      control_update_divider_ = kControlUpdateDivider;
      update_leds(true);
    }
  }

  void set_external_clock_interval(uint32_t interval) {
    last_external_clock_interval_ = interval;
    refresh_external_playback_rate(interval);
    clock_timeout_samples_ = interval * kExternalClockTimeoutMultiplier;
    if (clock_timeout_samples_ < kExternalClockMinTimeoutSamples) {
      clock_timeout_samples_ = kExternalClockMinTimeoutSamples;
    }
    external_clock_active_ = true;
    update_leds(true);
  }

  void refresh_external_playback_rate(uint32_t interval) {
    const uint64_t numerator =
        (static_cast<uint64_t>(60u) * BREAKY_BANK_SAMPLE_RATE) << 32u;
    phase_inc_q32_ =
        numerator / (static_cast<uint64_t>(interval) * current_sample().source_bpm *
                     kExternalClockPpqn);
    if (timestretch_active_) {
      refresh_timestretch_source_inc();
    }
  }

  void handle_switch_actions(bool has_audio) {
    const Switch switch_value = SwitchVal();
    if (switch_value != Down) {
      switch_down_samples_ = 0;
      switch_jump_armed_ = true;
    } else if (switch_down_samples_ < kSwitchDebounceSamples) {
      ++switch_down_samples_;
    } else if (switch_jump_armed_) {
      trigger_switch_pulse1();
      if (has_audio) {
        jump_to_y_knob_position();
      }
      switch_jump_armed_ = false;
    }

    if (switch_value != Up) {
      switch_up_samples_ = 0;
      switch_change_armed_ = true;
    } else if (switch_up_samples_ < kSwitchDebounceSamples) {
      ++switch_up_samples_;
    } else if (switch_change_armed_) {
      trigger_switch_pulse2();
      if (has_audio) {
        change_sample_from_y_knob_position();
      }
      switch_change_armed_ = false;
    }
  }

  void trigger_switch_pulse1() {
    switch_pulse1_samples_ = kSwitchPulseOutSamples;
  }

  void trigger_switch_pulse2() {
    switch_pulse2_samples_ = kSwitchPulseOutSamples;
  }

  void update_switch_pulse_outputs() {
    PulseOut1(switch_pulse1_samples_ > 0);
    PulseOut2(switch_pulse2_samples_ > 0);

    if (switch_pulse1_samples_ > 0) {
      --switch_pulse1_samples_;
    }
    if (switch_pulse2_samples_ > 0) {
      --switch_pulse2_samples_;
    }
  }

  void stop_switch_pulse_outputs() {
    switch_pulse1_samples_ = 0;
    switch_pulse2_samples_ = 0;
    PulseOut1(false);
    PulseOut2(false);
  }

  void handle_pulse_cv_position() {
    if (!PulseIn2RisingEdge()) {
      return;
    }

    if (Connected(Input::CV2)) {
      jump_to_knob_position(clamp_knob(KnobVal(Y) + CVIn2()));
    } else {
      jump_to_y_knob_position();
    }
  }

  void jump_to_y_knob_position() {
    jump_to_knob_position(static_cast<uint32_t>(KnobVal(Y)));
  }

  void jump_to_knob_position(uint32_t knob) {
    const uint32_t frame_count = current_sample().frame_count;
    const uint32_t frame = static_cast<uint32_t>(
        (static_cast<uint64_t>(knob) * (frame_count - 1u)) / kKnobMax);
    phase_q32_ = static_cast<uint64_t>(frame) << 32u;
    invalidate_timestretch_grains();
    update_leds(true);
  }

  void jump_to_start() {
    phase_q32_ = 0;
    invalidate_timestretch_grains();
    update_leds(true);
  }

  uint8_t sample_from_y_knob_position() {
    const uint32_t knob = static_cast<uint32_t>(KnobVal(Y));
    const uint32_t count = breaky_audio_sample_count();
    if (count == 0) {
      return 0;
    }
    const uint32_t scaled =
        (static_cast<uint64_t>(knob) * count) / (kKnobMax + 1u);
    return static_cast<uint8_t>(scaled);
  }

  void change_sample_from_y_knob_position() {
    const uint8_t next_sample = sample_from_y_knob_position();
    if (next_sample == active_sample_) {
      return;
    }

    const uint32_t old_frame_count = current_sample().frame_count;
    const uint32_t old_frame = static_cast<uint32_t>(phase_q32_ >> 32u);
    const uint32_t old_frac = static_cast<uint32_t>(phase_q32_);
    active_sample_ = next_sample;
    const uint32_t new_frame_count = current_sample().frame_count;

    const uint64_t new_frame =
        (static_cast<uint64_t>(old_frame) * new_frame_count) / old_frame_count;
    const uint64_t new_frac =
        (static_cast<uint64_t>(old_frac) * new_frame_count) / old_frame_count;
    phase_q32_ = wrap_phase((new_frame << 32u) + new_frac);
    if (external_clock_active_ && last_external_clock_interval_ > 0) {
      refresh_external_playback_rate(last_external_clock_interval_);
    } else {
      refresh_free_playback_rate();
    }
    invalidate_timestretch_grains();
    update_leds(true);
  }

  StereoFrame render_direct_sample() {
    invalidate_timestretch_grains();
    const StereoFrame output = read_interpolated_phase(phase_q32_);
    advance_phase();
    return output;
  }

  StereoFrame render_stretched_sample() {
    if (!grains_initialized_) {
      initialize_timestretch_grains();
    }

    int32_t left = 0;
    int32_t right = 0;
    accumulate_grain(grains_[0], left, right);
    accumulate_grain(grains_[1], left, right);

    advance_phase_by(timestretch_source_inc_q32_);
    advance_grain(grains_[0]);
    advance_grain(grains_[1]);

    return {static_cast<int16_t>(left >> kGrainHopShift),
            static_cast<int16_t>(right >> kGrainHopShift)};
  }

  void initialize_timestretch_grains() {
    const uint64_t previous_grain_offset =
        static_cast<uint64_t>(kGrainHopSamples) * phase_inc_q32_;
    grains_[0] = {phase_q32_, phase_inc_q32_, 0};
    grains_[1] = {subtract_phase(phase_q32_, previous_grain_offset),
                  phase_inc_q32_, static_cast<uint16_t>(kGrainHopSamples)};
    grains_initialized_ = true;
  }

  void invalidate_timestretch_grains() {
    grains_initialized_ = false;
  }

  void accumulate_grain(const Grain& grain, int32_t& left, int32_t& right) const {
    const uint32_t weight = grain_window(grain.age);
    if (weight == 0) {
      return;
    }

    const uint64_t grain_phase =
        grain.start_phase_q32 +
        (static_cast<uint64_t>(grain.age) * grain.phase_inc_q32);
    const StereoFrame sample = read_interpolated_phase(grain_phase);
    left += static_cast<int32_t>(sample.left) * static_cast<int32_t>(weight);
    right += static_cast<int32_t>(sample.right) * static_cast<int32_t>(weight);
  }

  void advance_grain(Grain& grain) {
    ++grain.age;
    if (grain.age < kGrainLengthSamples) {
      return;
    }

    grain.start_phase_q32 = phase_q32_;
    grain.phase_inc_q32 = phase_inc_q32_;
    grain.age = 0;
  }

  void update_leds(bool force = false) {
    ++led_divider_;
    if (!force && led_divider_ < kLedUpdateDivider) {
      return;
    }
    led_divider_ = 0;

    const uint32_t frame_count = current_sample().frame_count;
    const uint32_t segment =
        static_cast<uint32_t>((static_cast<uint64_t>(current_frame()) * kNumLeds) /
                              frame_count);
    if (pulse_flash_samples_ > 0) {
      for (int i = 0; i < kNumLeds; ++i) {
        LedOn(i, true);
      }
      return;
    }

    if (external_clock_active_) {
      const uint32_t inner_segment =
          static_cast<uint32_t>((static_cast<uint64_t>(current_frame()) * 4u) /
                                frame_count);
      LedOn(0, true);
      LedOn(5, true);
      for (int i = 1; i < 5; ++i) {
        LedOn(i, static_cast<uint32_t>(i - 1) == inner_segment);
      }
      return;
    }

    for (int i = 0; i < kNumLeds; ++i) {
      LedOn(i, static_cast<uint32_t>(i) == segment);
    }
  }

  void update_empty_leds() {
    ++empty_led_samples_;
    const uint32_t step =
        (empty_led_samples_ / (kAudioOutputSampleRate / 8u)) % kNumLeds;
    for (int i = 0; i < kNumLeds; ++i) {
      LedOn(i, static_cast<uint32_t>(i) == step);
    }
  }
};

int main() {
  set_sys_clock_khz(200000, true);
  stdio_init_all();
  breaky_audio_bank_init();
  multicore_launch_core1(breaky_sample_manager_core);

  Breaky card;
  card.EnableNormalisationProbe();
  card.Run();
}
