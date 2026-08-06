#include "ComputerCard.h"
#include "hardware/clocks.h"
#include "hardware/sync.h"
#include "hardware/vreg.h"
#include "pico/multicore.h"
#include "pico.h"
#include "pico/time.h"

#include "knots/src/core/control_router.h"
#include "knots/src/core/midi_worker.h"
#include "knots/src/core/shared_state.h"
#include "knots/src/dsp/sine_lut.h"
#include "knots/src/engines/engine_registry.h"

namespace mtws {

namespace {
constexpr int32_t kUnityQ12 = 4096;
constexpr int kVCAGainSmoothShift = 6;

// Maps MIDI CC74 from 0..127 into the ComputerCard bipolar CV code range.
// The endpoints intentionally use the full raw code span so the output is
// approximately -6 V at startup and +6 V at the top of the CC range.
inline int16_t __not_in_flash_func(MapCC74ToCV2Code)(uint8_t cc74_value) {
  return int16_t(((uint32_t(cc74_value) * 4095U) + 63U) / 127U) - 2048;
}

// Slews one Q12 gain toward its latest target with a tiny integer one-pole
// step. `shift = 6` means each sample moves by roughly 1/64 of the remaining
// error, which lengthens the fixed CV2 fade enough to better suppress
// gate-driven clicks while keeping the path lightweight and responsive.
inline int32_t __not_in_flash_func(SmoothToward)(int32_t current, int32_t target, int shift) {
  if (shift <= 0) return target;
  int32_t delta = target - current;
  int32_t step = delta >> shift;
  if (step == 0 && delta != 0) {
    step = (delta > 0) ? 1 : -1;
  }
  return current + step;
}

// Available clock divisions for MIDI clock mode. Each entry is the number of
// input ticks (at 24 PPQN) per output pulse. Spread as 8 equal steps across
// the X knob. All values divide evenly into 24 for clean timing.
constexpr uint8_t kClockDivisions[] = {24, 12, 8, 6, 4, 3, 2, 1};
constexpr uint8_t kNumClockDivisions = 8;

// Maps X knob (0..4095) to an index in kClockDivisions (0..7).
inline uint8_t ClockDivisionIndexFromKnobX(uint16_t knob_x) {
  uint32_t idx = (uint32_t(knob_x) * kNumClockDivisions) >> 12;
  if (idx >= kNumClockDivisions) idx = kNumClockDivisions - 1U;
  return uint8_t(idx);
}

// Maps X knob (0..4095) to an internal clock period in control ticks.
// 20 BPM = 3000 ticks, 999 BPM = 60 ticks (linear across the knob).
inline uint32_t InternalClockPeriodFromKnobX(uint16_t knob_x) {
  uint32_t bpm = 20U + ((uint32_t(knob_x) * 979U + 2048U) >> 12);
  if (bpm < 20U) bpm = 20U;
  if (bpm > 999U) bpm = 999U;
  return 60000U / bpm;
}

// Clamps one raw knob reading into the documented 12-bit panel range so the
// rest of the control path can work in a consistent 0..4095 domain.
inline uint16_t ClampKnobU12(int32_t knob_value) {
  if (knob_value < 0) return 0;
  if (knob_value > 4095) return 4095;
  return uint16_t(knob_value);
}

}  // namespace

class MTWSApp : public ComputerCard {
 public:
  static constexpr uint32_t kControlDivisor = 48;
  static constexpr uint32_t kControlPeriodUs = (kControlDivisor * 1000000U) / 48000U;
  static constexpr uint8_t kSwitchDebounceTicks = 3;
  static constexpr uint32_t kNoteFlashOffSamples = 960;  // ~20ms
  // Control ticks without a new MIDI clock tick before switching to internal.
  static constexpr uint32_t kClockTimeoutTicks = 500;
  // ~5ms pulse width = 5 control ticks at ~1kHz control rate.
  static constexpr uint32_t kPulseWidthTicks = 5;
  // Requiring 16 raw knob counts cancels only deliberate X movement while
  // still ignoring the small idle jitter the smoothed knob can report.
  static constexpr uint16_t kZSwitchClockConfigDeadband = 16;

  MTWSApp()
      : lut_(),
        registry_(&lut_),
        midi_worker_(),
        published_ui_snapshot_index_(0),
        published_frame_index_(0),
        control_counter_(int(kControlDivisor)),
        switch_candidate_(Middle),
        switch_stable_(Middle),
        switch_stable_count_(0),
        switch_inputs_primed_(false),
        pulse2_high_prev_(false),
        panel_alt_latched_(false),
        selected_slot_(0),
        z_switch_hold_active_(false),
        z_switch_slot_armed_(false),
        z_switch_clock_edit_active_(false),
        z_switch_down_knob_x_(0),
        seen_note_on_counter_(0),
        note_flash_samples_remaining_(0),
        last_cv_note_sent_(255),
        last_cc74_sent_(0),
        last_pulse1_gate_sent_(2),
        smoothed_vca_gain_q12_(kUnityQ12),
        core1_started_(false),
        last_clock_tick_seen_(0),
        clock_inactive_counter_(kClockTimeoutTicks),
        clock_division_ticks_(6),
        midi_clock_div_counter_(0),
        internal_clock_period_(500),
        internal_clock_counter_(500),
        pulse2_width_counter_(0) {
    instance_ = this;

    EnableNormalisationProbe();
    CVOut2(MapCC74ToCV2Code(0));
    registry_.Init();
    registry_.Get(selected_slot_)->OnSelected();

    UISnapshot init_ui{};
    init_ui.knob_main = 2048;
    init_ui.knob_x = 0;
    init_ui.knob_y = 0;
    init_ui.cv1 = 0;
    init_ui.cv2 = 0;
    init_ui.audio1 = 0;
    init_ui.audio2 = 0;
    init_ui.audio1_connected = false;
    init_ui.audio2_connected = false;
    init_ui.cv2_connected = false;
    init_ui.pulse1_connected = false;
    init_ui.pulse1_high = false;
    init_ui.selected_slot = selected_slot_;
    init_ui.panel_alt_latched = panel_alt_latched_;
    ui_snapshots_[0] = init_ui;
    ui_snapshots_[1] = init_ui;

    MidiState init_midi{};
    init_midi.note_active = false;
    init_midi.current_note = 60;
    init_midi.last_note = 60;
    init_midi.cc74_value = 0;
    init_midi.note_on_counter = 0;
    init_midi.clock_tick_count = 0;

    GlobalControlFrame init_global = ControlRouter::BuildGlobalFrame(init_ui, init_midi);
    for (int i = 0; i < 2; ++i) {
      control_frames_[i].global = init_global;
      registry_.Get(init_global.selected_slot)->ControlTick(init_global, control_frames_[i].engine);
    }
    smoothed_vca_gain_q12_ = int32_t(init_global.vca_gain_q12);
  }

  static void Core1Entry() {
    if (instance_ != nullptr) {
      instance_->ControlAndMIDIWorkerCore();
    }
  }

  void ControlAndMIDIWorkerCore() {
    midi_worker_.Init();

    while (1) {
      midi_worker_.Poll();

      __dmb();
      uint32_t ui_read_index = published_ui_snapshot_index_;
      UISnapshot ui = ui_snapshots_[ui_read_index];
      MidiState midi = midi_worker_.Snapshot();

      GlobalControlFrame global = ControlRouter::BuildGlobalFrame(ui, midi);

      uint32_t read_index = published_frame_index_;
      uint32_t write_index = read_index ^ 1U;
      ControlFrame& frame = control_frames_[write_index];
      frame.global = global;
      registry_.Get(global.selected_slot)->ControlTick(global, frame.engine);

      __dmb();
      published_frame_index_ = write_index;

      busy_wait_us_32(kControlPeriodUs);
    }
  }

  // Advances to the next engine slot and lets the new engine refresh any
  // per-selection state before the next render call.
  inline void __not_in_flash_func(AdvanceSelectedSlot)() {
    selected_slot_ = uint8_t((selected_slot_ + 1U) % kNumOscillatorSlots);
    registry_.Get(selected_slot_)->OnSelected();
  }

  inline void __not_in_flash_func(CaptureUISnapshotAndHandleSwitching)() {
    const uint16_t knob_x = ClampKnobU12(KnobVal(Knob::X));
    Switch sw = SwitchVal();
    const bool pulse2_high_now = Connected(Input::Pulse2) && PulseIn2();

    // Prime the app-level switch and Pulse2 histories from live hardware on
    // the first control snapshot. This avoids turning startup settling into a
    // fake Z down->middle tap or a synthetic Pulse2 rising edge.
    if (!switch_inputs_primed_) {
      switch_candidate_ = sw;
      switch_stable_ = sw;
      switch_stable_count_ = 0;
      pulse2_high_prev_ = pulse2_high_now;
      if (sw == Up) {
        panel_alt_latched_ = true;
      } else if (sw == Middle) {
        panel_alt_latched_ = false;
      }
      switch_inputs_primed_ = true;
    }

    Switch prev_stable = switch_stable_;
    if (sw != switch_candidate_) {
      switch_candidate_ = sw;
      switch_stable_count_ = 0;
    } else if (switch_candidate_ != switch_stable_) {
      if (switch_stable_count_ < kSwitchDebounceTicks) {
        ++switch_stable_count_;
      }
      if (switch_stable_count_ >= kSwitchDebounceTicks) {
        switch_stable_ = switch_candidate_;
        switch_stable_count_ = 0;
      }
    } else {
      switch_stable_count_ = 0;
    }
    sw = switch_stable_;

    const bool entered_down = (sw == Down) && (prev_stable != Down);
    const bool left_down = z_switch_hold_active_ && (sw != Down);
    if (entered_down) {
      // A fresh Z-down gesture starts with one pending slot advance. Moving X
      // far enough while held converts that gesture into clock setup instead.
      z_switch_hold_active_ = true;
      z_switch_slot_armed_ = true;
      z_switch_clock_edit_active_ = false;
      z_switch_down_knob_x_ = knob_x;
    }
    if (z_switch_hold_active_ && sw == Down) {
      int32_t knob_delta = int32_t(knob_x) - int32_t(z_switch_down_knob_x_);
      if (knob_delta < 0) knob_delta = -knob_delta;
      if (knob_delta >= int32_t(kZSwitchClockConfigDeadband)) {
        z_switch_slot_armed_ = false;
        z_switch_clock_edit_active_ = true;
      }
    }

    const bool pulse2_high = pulse2_high_now;
    const bool pulse2_rising = pulse2_high && !pulse2_high_prev_;
    pulse2_high_prev_ = pulse2_high;
    // Keep Pulse2 slot switching masked for the entire Z-down gesture,
    // including the debounced release tick that resolves the pending slot tap.
    const bool pulse2_slot_switch_masked = z_switch_hold_active_;

    if (sw == Up) {
      panel_alt_latched_ = true;
    } else if (sw == Middle) {
      panel_alt_latched_ = false;
    }

    if (left_down) {
      if ((sw == Middle) && z_switch_slot_armed_) {
        // Slot changes are hard cuts. This removes the extra render of the
        // previous engine from the audio callback so switching costs no more
        // than normal steady-state rendering.
        AdvanceSelectedSlot();
      }
      z_switch_hold_active_ = false;
      z_switch_slot_armed_ = false;
      z_switch_clock_edit_active_ = false;
    } else if (pulse2_rising && !pulse2_slot_switch_masked) {
      // Slot changes are hard cuts. This removes the extra render of the
      // previous engine from the audio callback so switching costs no more than
      // normal steady-state rendering.
      AdvanceSelectedSlot();
    }

    UISnapshot ui{};
    ui.knob_main = ClampKnobU12(KnobVal(Knob::Main));
    ui.knob_x = knob_x;
    ui.knob_y = ClampKnobU12(KnobVal(Knob::Y));

    ui.cv1 = int16_t(CVIn1());
    ui.cv2 = int16_t(CVIn2());
    ui.audio1 = int16_t(AudioIn1());
    ui.audio2 = int16_t(AudioIn2());

    ui.audio1_connected = Connected(Input::Audio1);
    ui.audio2_connected = Connected(Input::Audio2);
    ui.cv2_connected = Connected(Input::CV2);
    ui.pulse1_connected = Connected(Input::Pulse1);
    ui.pulse1_high = PulseIn1();

    ui.selected_slot = selected_slot_;
    ui.panel_alt_latched = panel_alt_latched_;

    __dmb();
    uint32_t ui_read_index = published_ui_snapshot_index_;
    uint32_t ui_write_index = ui_read_index ^ 1U;
    ui_snapshots_[ui_write_index] = ui;
    __dmb();
    published_ui_snapshot_index_ = ui_write_index;
  }

  inline void __not_in_flash_func(UpdateSlotLEDs)(const ControlFrame& frame) {
    if (frame.global.note_on_counter != seen_note_on_counter_) {
      seen_note_on_counter_ = frame.global.note_on_counter;
      note_flash_samples_remaining_ = int(kNoteFlashOffSamples);
    }

    bool flash_off = note_flash_samples_remaining_ > 0;
    for (uint32_t i = 0; i < 6; ++i) {
      bool selected = (i == frame.global.selected_slot);
      LedOn(i, selected && !flash_off);
    }
  }

  // Applies MIDI-driven panel outputs at control rate so the audio callback
  // only renders engine audio and avoids extra per-sample GPIO/CV work.
  inline void __not_in_flash_func(UpdateMIDIOutputs)(const GlobalControlFrame& global) {
    if (global.last_midi_note != last_cv_note_sent_) {
      CVOut1MIDINote(global.last_midi_note);
      last_cv_note_sent_ = global.last_midi_note;
    }

    if (global.midi_cc74_value != last_cc74_sent_) {
      CVOut2(MapCC74ToCV2Code(global.midi_cc74_value));
      last_cc74_sent_ = global.midi_cc74_value;
    }

    const uint8_t pulse1_gate = global.midi_note_active ? 1U : 0U;
    if (pulse1_gate != last_pulse1_gate_sent_) {
      PulseOut1(global.midi_note_active);
      last_pulse1_gate_sent_ = pulse1_gate;
    }
  }

  // Drives PulseOut2 as a clock output. In MIDI clock mode, forwards 0xF8
  // ticks at the configured division. In internal mode, generates pulses at
  // the configured BPM. Configuration is set by holding Z down + X knob.
  inline void __not_in_flash_func(UpdateClockOutput)(const GlobalControlFrame& global) {
    const uint32_t tick_count = global.midi_clock_tick_count;
    const bool new_ticks = (tick_count != last_clock_tick_seen_);
    const bool midi_clock_active = (clock_inactive_counter_ < kClockTimeoutTicks);

    // Configuration mode starts only after X has moved far enough to claim the
    // gesture, so pressing Z alone leaves the previous clock setting intact.
    if ((switch_stable_ == Down) && z_switch_clock_edit_active_) {
      uint16_t knob_x = ClampKnobU12(KnobVal(Knob::X));
      if (midi_clock_active) {
        clock_division_ticks_ = kClockDivisions[ClockDivisionIndexFromKnobX(knob_x)];
      } else {
        internal_clock_period_ = InternalClockPeriodFromKnobX(knob_x);
      }
    }

    bool fire_pulse = false;

    if (new_ticks) {
      uint32_t incoming = tick_count - last_clock_tick_seen_;
      last_clock_tick_seen_ = tick_count;
      clock_inactive_counter_ = 0;

      midi_clock_div_counter_ += incoming;
      if (midi_clock_div_counter_ >= clock_division_ticks_) {
        midi_clock_div_counter_ = 0;
        fire_pulse = true;
      }
    } else {
      if (clock_inactive_counter_ < kClockTimeoutTicks) {
        ++clock_inactive_counter_;
      }
    }

    // Internal clock when MIDI clock is absent.
    if (!midi_clock_active && !new_ticks) {
      if (internal_clock_counter_ == 0) {
        internal_clock_counter_ = internal_clock_period_;
        fire_pulse = true;
      }
      --internal_clock_counter_;
    }

    if (fire_pulse) {
      pulse2_width_counter_ = kPulseWidthTicks;
    }
    if (pulse2_width_counter_ > 0) {
      --pulse2_width_counter_;
    }
    PulseOut2(pulse2_width_counter_ > 0);
  }

  virtual void __not_in_flash_func(ProcessSample)() override {
    if (!core1_started_) {
      core1_started_ = true;
      multicore_launch_core1(Core1Entry);
    }

    if (++control_counter_ >= int(kControlDivisor)) {
      control_counter_ = 0;
      CaptureUISnapshotAndHandleSwitching();

      uint32_t read_index = published_frame_index_;
      __dmb();
      const ControlFrame& frame = control_frames_[read_index];
      UpdateSlotLEDs(frame);
      UpdateMIDIOutputs(frame.global);
      UpdateClockOutput(frame.global);
    }

    if (note_flash_samples_remaining_ > 0) {
      --note_flash_samples_remaining_;
    }

    uint32_t read_index = published_frame_index_;
    __dmb();
    const ControlFrame& frame = control_frames_[read_index];

    int32_t new_out1 = 0;
    int32_t new_out2 = 0;
    registry_.Get(frame.global.selected_slot)->RenderSample(frame.engine, new_out1, new_out2);

    int32_t mixed_out1 = new_out1;
    int32_t mixed_out2 = new_out2;
    int32_t target_vca_gain_q12 = int32_t(frame.global.vca_gain_q12);
    smoothed_vca_gain_q12_ = SmoothToward(smoothed_vca_gain_q12_, target_vca_gain_q12, kVCAGainSmoothShift);

    int32_t out1 = int32_t((int64_t(mixed_out1) * smoothed_vca_gain_q12_) >> 12);
    int32_t out2 = int32_t((int64_t(mixed_out2) * smoothed_vca_gain_q12_) >> 12);

    if (out1 > 2047) out1 = 2047;
    if (out1 < -2048) out1 = -2048;
    if (out2 > 2047) out2 = 2047;
    if (out2 < -2048) out2 = -2048;

    AudioOut1(int16_t(out1));
    AudioOut2(int16_t(out2));
  }

 private:
  static MTWSApp* instance_;

  SineLUT lut_;
  EngineRegistry registry_;
  MIDIWorker midi_worker_;

  volatile uint32_t published_ui_snapshot_index_;
  UISnapshot ui_snapshots_[2];
  volatile uint32_t published_frame_index_;
  ControlFrame control_frames_[2];

  int control_counter_;
  Switch switch_candidate_;
  Switch switch_stable_;
  uint8_t switch_stable_count_;
  bool switch_inputs_primed_;
  bool pulse2_high_prev_;
  bool panel_alt_latched_;
  uint8_t selected_slot_;
  bool z_switch_hold_active_;
  bool z_switch_slot_armed_;
  bool z_switch_clock_edit_active_;
  uint16_t z_switch_down_knob_x_;

  uint32_t seen_note_on_counter_;
  int note_flash_samples_remaining_;
  uint8_t last_cv_note_sent_;
  uint8_t last_cc74_sent_;
  uint8_t last_pulse1_gate_sent_;
  int32_t smoothed_vca_gain_q12_;

  bool core1_started_;

  // Clock output state.
  uint32_t last_clock_tick_seen_;
  uint32_t clock_inactive_counter_;
  uint32_t clock_division_ticks_;
  uint32_t midi_clock_div_counter_;
  uint32_t internal_clock_period_;
  uint32_t internal_clock_counter_;
  uint32_t pulse2_width_counter_;
};

MTWSApp* MTWSApp::instance_ = nullptr;

}  // namespace mtws

int main() {
  vreg_set_voltage(VREG_VOLTAGE_1_15);
  sleep_ms(10);
  set_sys_clock_khz(200000, true);

  // Keep the large double-buffered control frames out of the main stack.
  static mtws::MTWSApp app;
  app.Run();
}
