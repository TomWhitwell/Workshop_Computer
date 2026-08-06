#pragma once

#include <cstddef>
#include <cstdint>

#define COMPUTERCARD_NOIMPL
#include "ComputerCard.h"
#include "ConfigStore.h"
#include "GridsEngine.h"
#include "pico/critical_section.h"

class GridsCard : public ComputerCard {
 public:
  GridsCard();
  void Housekeeping();
  /** Expose board ID for USB device-vs-host port selection (see 20_reverb). */
  ComputerCard::HardwareVersion_t HardwareRevision() const { return HardwareVersion(); }

 protected:
  void ProcessSample() override;

 private:
  struct KnobLayerState {
    int32_t stored = 0;
    bool picked_up = true;
  };

  struct RuntimeParams {
    int32_t x = 0;
    int32_t y = 0;
    int32_t fill = 0;
    /** Per-lane density (macro mode sets all three equal to blended fill). */
    int32_t lane2_fill = 2048;
    int32_t lane3_fill = 2048;
  };

  static constexpr uint32_t kSampleRate = 48000;

  void TickUiAndSwitch();
  void HandleTapTempo();
  void TriggerOutputs(const GridsEngine::Outputs& out);
  void TickPulseTimers();
  int32_t ApplyPickup(Knob knob, KnobLayerState& state);
  void RefreshRuntimeParams();
  void HandleIncomingSysEx();
  void SendConfigSysEx();
  void ReceiveConfigSysEx(const uint8_t* payload, size_t len);
  void MarkConfigDirty();
  static void SanitizeConfig(ConfigStore::Data& cfg);
  uint16_t CurrentPulseSamples() const;
  bool ExternalClockActive();
  void RecomputeNominalTickSamples();
  /** swing_pct: 50 straight … 75 max shuffle (sequencer-style %). */
  static uint32_t InternalClockSpacingSamples(uint32_t nominal_samples, uint8_t swing_pct_50_75, uint8_t engine_step_before_tick);

  ConfigStore store_;
  ConfigStore::Data cfg_{};
  ConfigStore::Data pending_sysex_cfg_{};
  critical_section_t cfg_cs_{};
  bool pending_sysex_cfg_valid_ = false;
  GridsEngine engine_;

  RuntimeParams normal_params_{};
  RuntimeParams alt_params_{};
  bool alt_layer_ = false;
  KnobLayerState main_latch_{};
  KnobLayerState x_latch_{};
  KnobLayerState y_latch_{};

  uint32_t sample_count_ = 0;
  uint32_t samples_per_tick_ = 6000;
  uint32_t next_tick_at_ = 0;
  uint32_t last_tap_sample_ = 0;

  uint32_t switch_down_start_ = 0;
  bool long_press_consumed_ = false;

  /** Debounced Z (reduces bounce + wrong mode while holding momentary Down). */
  static constexpr uint16_t kZDebounceSamples = 200;
  Switch z_filtered_ = Switch::Middle;
  Switch z_pending_ = Switch::Middle;
  uint16_t z_debounce_count_ = 0;

  /** Middle ↔ Up: keep audio params until the corresponding knob moves past deadband. */
  int32_t persist_blended_ = 2048;
  int32_t persist_d1_ = 2048;
  int32_t persist_d2_ = 2048;
  int32_t persist_d3_ = 2048;
  int32_t persist_map_x_ = 2048;
  int32_t persist_map_y_ = 2048;
  int32_t knob_base_main_ = 0;
  int32_t knob_base_x_ = 0;
  int32_t knob_base_y_ = 0;
  bool knob_live_main_ = true;
  bool knob_live_x_ = true;
  bool knob_live_y_ = true;

  /** Pattern map used when Z is Up (Main/X/Y are per-lane density). Updated in normal (middle) mode. */
  int32_t held_map_x_ = 2048;
  int32_t held_map_y_ = 2048;

  uint16_t pulse_1_countdown_ = 0;
  uint16_t pulse_2_countdown_ = 0;
  uint16_t cv1_pulse_countdown_ = 0;
  uint16_t cv2_pulse_countdown_ = 0;
  uint16_t beat_led_countdown_ = 0;

  uint64_t last_change_us_ = 0;
  bool pending_save_ = false;
};

