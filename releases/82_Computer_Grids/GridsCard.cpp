#include "GridsCard.h"

#include <cstdlib>

#include <cstring>

#include "pico/time.h"

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wextra"
#endif
#include "tusb.h"
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

namespace {
constexpr uint8_t kSysExStart = 0xF0;
constexpr uint8_t kSysExEnd = 0xF7;
constexpr uint8_t kManufacturer = 0x7D;
constexpr uint8_t kDevice = 0x63;  // local card id
constexpr uint8_t kCmdGetConfig = 0x01;
constexpr uint8_t kCmdSetConfig = 0x03;
constexpr uint8_t kCmdSaveConfig = 0x04;
constexpr uint32_t kLongPressSamples = 24000;
constexpr uint32_t kPickupDeadband = 96;

size_t Encode7Bit(const uint8_t* raw, size_t raw_len, uint8_t* out, size_t out_max) {
  size_t out_idx = 0;
  for (size_t i = 0; i < raw_len; i += 7) {
    if (out_idx >= out_max) break;
    uint8_t msb = 0;
    uint8_t block[7] = {};
    size_t block_len = raw_len - i;
    if (block_len > 7) block_len = 7;
    for (size_t j = 0; j < block_len; ++j) {
      const uint8_t b = raw[i + j];
      if (b & 0x80) msb |= static_cast<uint8_t>(1u << j);
      block[j] = static_cast<uint8_t>(b & 0x7F);
    }
    out[out_idx++] = msb;
    for (size_t j = 0; j < block_len; ++j) {
      if (out_idx >= out_max) return out_idx;
      out[out_idx++] = block[j];
    }
  }
  return out_idx;
}

size_t Decode7Bit(const uint8_t* in, size_t in_len, uint8_t* raw, size_t raw_max) {
  size_t in_idx = 0;
  size_t raw_idx = 0;
  while (in_idx < in_len && raw_idx < raw_max) {
    const uint8_t msb = in[in_idx++];
    for (size_t j = 0; j < 7 && in_idx < in_len && raw_idx < raw_max; ++j) {
      uint8_t b = in[in_idx++];
      if (msb & (1u << j)) b |= 0x80;
      raw[raw_idx++] = b;
    }
  }
  return raw_idx;
}

template <typename T>
T Clamp(T value, T lo, T hi) {
  if (value < lo) return lo;
  if (value > hi) return hi;
  return value;
}
}  // namespace

void GridsCard::RecomputeNominalTickSamples() {
  samples_per_tick_ = (kSampleRate * 600U) / (cfg_.bpm10 * 4U);
  if (samples_per_tick_ == 0) samples_per_tick_ = 1;
}

uint32_t GridsCard::InternalClockSpacingSamples(uint32_t nominal_samples, uint8_t swing_pct_50_75,
                                                uint8_t engine_step_before_tick) {
  if (nominal_samples == 0) nominal_samples = 1;
  // swing_pct_50_75: 50 = straight, 75 = max shuffle; amount 0–25 maps to skew 0 … nominal/4.
  const unsigned amount = (swing_pct_50_75 > 75) ? 25 : (swing_pct_50_75 < 50) ? 0 : (swing_pct_50_75 - 50);
  if (amount == 0) return nominal_samples;
  const uint32_t skew = (nominal_samples * amount * 25U) / 2500U;
  if ((engine_step_before_tick & 1U) == 0U) {
    const uint32_t shortened = nominal_samples - skew;
    return shortened ? shortened : 1U;
  }
  return nominal_samples + skew;
}

GridsCard::GridsCard() {
  store_.Load(false);
  critical_section_init(&cfg_cs_);
  cfg_ = store_.Get();
  SanitizeConfig(cfg_);
  engine_.Seed(static_cast<uint32_t>(UniqueCardID()));
  RecomputeNominalTickSamples();

  normal_params_.fill = 2048;
  normal_params_.lane2_fill = 2048;
  normal_params_.lane3_fill = 2048;
  alt_params_.fill = cfg_.chaos * 32;
  alt_params_.x = cfg_.bpm10;
  alt_params_.y = ((static_cast<uint32_t>(cfg_.swing) - 50U) * 4095U) / 25U;

  const Switch z0 = SwitchVal();
  z_filtered_ = z0;
  z_pending_ = z0;
}

void GridsCard::ProcessSample() {
  if (pending_sysex_cfg_valid_) {
    critical_section_enter_blocking(&cfg_cs_);
    if (pending_sysex_cfg_valid_) {
      cfg_ = pending_sysex_cfg_;
      pending_sysex_cfg_valid_ = false;
    }
    critical_section_exit(&cfg_cs_);
    RecomputeNominalTickSamples();
  }

  sample_count_++;
  TickUiAndSwitch();
  TickPulseTimers();

  if (PulseIn2RisingEdge()) {
    engine_.Reset();
    RecomputeNominalTickSamples();
    if (!ExternalClockActive()) {
      next_tick_at_ = sample_count_ + InternalClockSpacingSamples(samples_per_tick_, cfg_.swing, 31);
    } else {
      next_tick_at_ = sample_count_ + samples_per_tick_;
    }
  }

  const bool ext_clock = ExternalClockActive();
  bool fire_tick = false;
  if (ext_clock) {
    fire_tick = PulseIn1RisingEdge();
  } else if (sample_count_ >= next_tick_at_) {
    fire_tick = true;
  }

  if (fire_tick) {
    uint8_t step_before_tick = 0;
    if (!ext_clock) {
      step_before_tick = engine_.Step();
    }
    RefreshRuntimeParams();
    const uint16_t map_x = static_cast<uint16_t>(normal_params_.x);
    const uint16_t map_y = static_cast<uint16_t>(normal_params_.y);
    uint16_t f1 = static_cast<uint16_t>(normal_params_.fill);
    uint16_t f2 = static_cast<uint16_t>(normal_params_.lane2_fill);
    uint16_t f3 = static_cast<uint16_t>(normal_params_.lane3_fill);
    const bool z_density_tick = (z_filtered_ == Switch::Up);
    if (alt_layer_ && !z_density_tick) {
      f1 = f2 = f3 = static_cast<uint16_t>(normal_params_.fill);
    }
    const auto outputs = engine_.Tick(map_x, map_y, f1, f2, f3, cfg_.chaos);
    TriggerOutputs(outputs);
    beat_led_countdown_ = CurrentPulseSamples();
    if (!ext_clock) {
      next_tick_at_ =
          sample_count_ + InternalClockSpacingSamples(samples_per_tick_, cfg_.swing, step_before_tick);
    }
  }

  const bool z_density = (z_filtered_ == Switch::Up);

  // Drive LEDs from audio core to avoid cross-core display glitches.
  LedOn(0, beat_led_countdown_ > 0);           // beat blink
  if (z_density) {
    LedBrightness(1, static_cast<uint16_t>(normal_params_.fill));       // lane 1 density (Main)
    LedBrightness(3, static_cast<uint16_t>(normal_params_.lane2_fill)); // lane 2 density (X)
    LedBrightness(5, static_cast<uint16_t>(normal_params_.lane3_fill)); // lane 3 density (Y)
  } else {
    LedBrightness(1, static_cast<uint16_t>(normal_params_.fill));
    LedBrightness(3, static_cast<uint16_t>(normal_params_.x));
    LedOn(5, cv1_pulse_countdown_ > 0 || cv2_pulse_countdown_ > 0 || alt_layer_);
  }
  LedOn(2, pulse_1_countdown_ > 0);            // lane 1 activity
  LedOn(4, pulse_2_countdown_ > 0);            // lane 2 activity

  if (beat_led_countdown_ > 0) beat_led_countdown_--;
}

void GridsCard::Housekeeping() {
  HandleTapTempo();
  HandleIncomingSysEx();

  bool should_save = false;
  ConfigStore::Data snapshot{};
  critical_section_enter_blocking(&cfg_cs_);
  if (pending_save_ && (time_us_64() - last_change_us_) > 1500000ULL) {
    snapshot = cfg_;
    pending_save_ = false;
    should_save = true;
  }
  critical_section_exit(&cfg_cs_);

  if (should_save) {
    store_.SaveData(snapshot);
  }
}

bool GridsCard::ExternalClockActive() {
  return Connected(Pulse1);
}

void GridsCard::TickUiAndSwitch() {
  const Switch raw = SwitchVal();
  const Switch z_before = z_filtered_;

  if (raw != z_pending_) {
    z_pending_ = raw;
    z_debounce_count_ = 0;
  } else if (z_pending_ != z_filtered_) {
    if (++z_debounce_count_ >= kZDebounceSamples) {
      z_filtered_ = z_pending_;
      z_debounce_count_ = 0;
    }
  } else {
    z_debounce_count_ = 0;
  }

  const bool z_stable_changed = (z_filtered_ != z_before);
  const Switch z_was = z_before;

  if (z_stable_changed && z_was != Switch::Down && z_filtered_ != Switch::Down) {
    const bool was_up = (z_was == Switch::Up);
    const bool now_up = (z_filtered_ == Switch::Up);
    if (!was_up && now_up) {
      persist_d1_ = persist_d2_ = persist_d3_ = normal_params_.fill;
      knob_base_main_ = KnobVal(Knob::Main);
      knob_base_x_ = KnobVal(Knob::X);
      knob_base_y_ = KnobVal(Knob::Y);
      knob_live_main_ = knob_live_x_ = knob_live_y_ = false;
    } else if (was_up && !now_up) {
      persist_map_x_ = held_map_x_;
      persist_map_y_ = held_map_y_;
      persist_blended_ =
          (normal_params_.fill + normal_params_.lane2_fill + normal_params_.lane3_fill) / 3;
      knob_base_main_ = KnobVal(Knob::Main);
      knob_base_x_ = KnobVal(Knob::X);
      knob_base_y_ = KnobVal(Knob::Y);
      knob_live_main_ = knob_live_x_ = knob_live_y_ = false;
    }
  }

  if (z_stable_changed) {
    const bool was_down = (z_was == Switch::Down);
    const bool now_down = (z_filtered_ == Switch::Down);
    if (now_down && !was_down) {
      switch_down_start_ = sample_count_;
      long_press_consumed_ = false;
    } else if (was_down && !now_down) {
      if (!long_press_consumed_ && !ExternalClockActive()) {
        const uint32_t now = sample_count_;
        const uint32_t dt = now - last_tap_sample_;
        if (last_tap_sample_ != 0 && dt > 2000 && dt < 48000 * 2) {
          const uint32_t bpm10 = (kSampleRate * 600U) / dt;
          if (bpm10 >= 400 && bpm10 <= 2600) {
            critical_section_enter_blocking(&cfg_cs_);
            cfg_.bpm10 = static_cast<uint16_t>(bpm10);
            critical_section_exit(&cfg_cs_);
            RecomputeNominalTickSamples();
            MarkConfigDirty();
          }
        }
        last_tap_sample_ = now;
      }
    }
  }

  const bool down = (z_filtered_ == Switch::Down);
  if (down && !long_press_consumed_ && (sample_count_ - switch_down_start_) > kLongPressSamples) {
    alt_layer_ = !alt_layer_;
    long_press_consumed_ = true;
    main_latch_.picked_up = false;
    x_latch_.picked_up = false;
    y_latch_.picked_up = false;
  }
}

void GridsCard::HandleTapTempo() {}

int32_t GridsCard::ApplyPickup(Knob knob, KnobLayerState& state) {
  const int32_t raw = KnobVal(knob);
  if (state.picked_up) {
    state.stored = raw;
    return raw;
  }
  const int32_t delta = raw - state.stored;
  if (delta > -static_cast<int32_t>(kPickupDeadband) && delta < static_cast<int32_t>(kPickupDeadband)) {
    state.picked_up = true;
    state.stored = raw;
  }
  return state.stored;
}

void GridsCard::RefreshRuntimeParams() {
  if (z_filtered_ == Switch::Down) {
    return;
  }

  const bool z_up = (z_filtered_ == Switch::Up);

  auto update_knob_live = [this]() {
    if (std::abs(KnobVal(Knob::Main) - knob_base_main_) > static_cast<int32_t>(kPickupDeadband)) {
      if (!knob_live_main_) {
        main_latch_.picked_up = true;
        main_latch_.stored = KnobVal(Knob::Main);
      }
      knob_live_main_ = true;
    }
    if (std::abs(KnobVal(Knob::X) - knob_base_x_) > static_cast<int32_t>(kPickupDeadband)) {
      if (!knob_live_x_) {
        x_latch_.picked_up = true;
        x_latch_.stored = KnobVal(Knob::X);
      }
      knob_live_x_ = true;
    }
    if (std::abs(KnobVal(Knob::Y) - knob_base_y_) > static_cast<int32_t>(kPickupDeadband)) {
      if (!knob_live_y_) {
        y_latch_.picked_up = true;
        y_latch_.stored = KnobVal(Knob::Y);
      }
      knob_live_y_ = true;
    }
  };

  if (z_up) {
    update_knob_live();
    // Z up: Main / X / Y = per-lane density; map = held knob position + CV1 (CV only when X/Y are live).
    int32_t d1 = knob_live_main_ ? ApplyPickup(Knob::Main, main_latch_) : persist_d1_;
    int32_t d2 = knob_live_x_ ? ApplyPickup(Knob::X, x_latch_) : persist_d2_;
    int32_t d3 = knob_live_y_ ? ApplyPickup(Knob::Y, y_latch_) : persist_d3_;

    int32_t mx = held_map_x_;
    int32_t my = held_map_y_;
    if (Connected(CV1) && (knob_live_x_ || knob_live_y_)) {
      const int32_t cv1 = (CVIn1() * cfg_.cv1_amount) / 64;
      if (cfg_.cv1_mode == ConfigStore::CV1ToX) mx += cv1;
      if (cfg_.cv1_mode == ConfigStore::CV1ToY) my += cv1;
      if (cfg_.cv1_mode == ConfigStore::CV1ToBlend) {
        mx += cv1 / 2;
        my += cv1 / 2;
      }
    }
    if (Connected(CV2) && (knob_live_main_ || knob_live_x_ || knob_live_y_)) {
      const int32_t cv2 = (CVIn2() * cfg_.cv2_amount) / 64;
      d1 += cv2;
      d2 += cv2;
      d3 += cv2;
    }

    if (mx < 0) mx = 0;
    if (mx > 4095) mx = 4095;
    if (my < 0) my = 0;
    if (my > 4095) my = 4095;
    if (d1 < 0) d1 = 0;
    if (d1 > 4095) d1 = 4095;
    if (d2 < 0) d2 = 0;
    if (d2 > 4095) d2 = 4095;
    if (d3 < 0) d3 = 0;
    if (d3 > 4095) d3 = 4095;

    normal_params_.x = mx;
    normal_params_.y = my;
    normal_params_.fill = d1;
    normal_params_.lane2_fill = d2;
    normal_params_.lane3_fill = d3;
    return;
  }

  if (alt_layer_) {
    RuntimeParams& active = alt_params_;
    active.fill = ApplyPickup(Knob::Main, main_latch_);
    active.x = ApplyPickup(Knob::X, x_latch_);
    active.y = ApplyPickup(Knob::Y, y_latch_);
    // Alt layer edits advanced params.
    critical_section_enter_blocking(&cfg_cs_);
    cfg_.chaos = static_cast<uint8_t>(alt_params_.fill >> 5);
    if (cfg_.chaos > 127) cfg_.chaos = 127;
    cfg_.bpm10 = static_cast<uint16_t>(600 + (alt_params_.x * 2000) / 4095);
    cfg_.swing = static_cast<uint8_t>(50 + (alt_params_.y * 25) / 4095);
    critical_section_exit(&cfg_cs_);
    RecomputeNominalTickSamples();
    MarkConfigDirty();
  } else {
    update_knob_live();

    int32_t x_knob = knob_live_x_ ? ApplyPickup(Knob::X, x_latch_) : persist_map_x_;
    int32_t y_knob = knob_live_y_ ? ApplyPickup(Knob::Y, y_latch_) : persist_map_y_;
    int32_t x = x_knob;
    int32_t y = y_knob;

    if (Connected(CV1) && (knob_live_x_ || knob_live_y_)) {
      const int32_t cv1 = (CVIn1() * cfg_.cv1_amount) / 64;
      if (cfg_.cv1_mode == ConfigStore::CV1ToX) x += cv1;
      if (cfg_.cv1_mode == ConfigStore::CV1ToY) y += cv1;
      if (cfg_.cv1_mode == ConfigStore::CV1ToBlend) {
        x += cv1 / 2;
        y += cv1 / 2;
      }
    }

    if (x < 0) x = 0;
    if (x > 4095) x = 4095;
    if (y < 0) y = 0;
    if (y > 4095) y = 4095;

    held_map_x_ = x_knob;
    held_map_y_ = y_knob;

    int32_t blended;
    if (!knob_live_main_) {
      blended = persist_blended_;
    } else {
      int32_t fill = ApplyPickup(Knob::Main, main_latch_);
      if (Connected(CV2)) {
        fill += (CVIn2() * cfg_.cv2_amount) / 64;
      }
      if (fill < 0) fill = 0;
      if (fill > 4095) fill = 4095;
      const int32_t laneBlend =
          ((fill * cfg_.lane1_fill_scale) + (fill * cfg_.lane2_fill_scale) + (fill * cfg_.lane3_fill_scale)) / 300;
      blended = laneBlend + cfg_.lane1_fill_offset + cfg_.lane2_fill_offset + cfg_.lane3_fill_offset;
      if (blended < 0) blended = 0;
      if (blended > 4095) blended = 4095;
    }

    normal_params_.x = x;
    normal_params_.y = y;
    normal_params_.fill = blended;
    normal_params_.lane2_fill = blended;
    normal_params_.lane3_fill = blended;
  }
}

uint16_t GridsCard::CurrentPulseSamples() const {
  const uint32_t ms = cfg_.pulse_ms ? cfg_.pulse_ms : 10;
  return static_cast<uint16_t>((kSampleRate * ms) / 1000);
}

void GridsCard::TriggerOutputs(const GridsEngine::Outputs& out) {
  const uint16_t len = CurrentPulseSamples();
  if (out.lane1) pulse_1_countdown_ = len;
  if (out.lane2) pulse_2_countdown_ = len;
  if (out.lane3) cv1_pulse_countdown_ = len;

  if (cfg_.aux_mode == ConfigStore::AuxAccent && out.accent) cv2_pulse_countdown_ = len;
  if (cfg_.aux_mode == ConfigStore::AuxClock) cv2_pulse_countdown_ = len;
  if (cfg_.aux_mode == ConfigStore::AuxLane3Mirror && out.lane3) cv2_pulse_countdown_ = len;
}

void GridsCard::TickPulseTimers() {
  const bool p1 = pulse_1_countdown_ > 0;
  const bool p2 = pulse_2_countdown_ > 0;
  PulseOut1(p1);
  PulseOut2(p2);
  CVOut1(cv1_pulse_countdown_ > 0 ? 2047 : -2048);
  CVOut2(cv2_pulse_countdown_ > 0 ? 2047 : -2048);

  if (pulse_1_countdown_ > 0) pulse_1_countdown_--;
  if (pulse_2_countdown_ > 0) pulse_2_countdown_--;
  if (cv1_pulse_countdown_ > 0) cv1_pulse_countdown_--;
  if (cv2_pulse_countdown_ > 0) cv2_pulse_countdown_--;
}

void GridsCard::HandleIncomingSysEx() {
  // Accumulate stream bytes until 0xF7 — a single tud_midi_stream_read() may return a
  // fragment (USB MIDI packs 1–3 data bytes per 32-bit packet), and the old logic
  // discarded those bytes when F7 was not yet in the buffer.
  static uint8_t rx_buf[384];
  static size_t rx_len = 0;
  while (tud_midi_available() && rx_len < sizeof(rx_buf)) {
    const uint32_t n =
        tud_midi_stream_read(rx_buf + rx_len, static_cast<uint32_t>(sizeof(rx_buf) - rx_len));
    if (n == 0) break;
    rx_len += n;
  }
  if (rx_len >= sizeof(rx_buf)) {
    rx_len = 0;
    return;
  }
  for (;;) {
    size_t i = 0;
    while (i < rx_len && rx_buf[i] != kSysExStart) {
      i++;
    }
    if (i >= rx_len) {
      rx_len = 0;
      return;
    }
    size_t j = i + 1;
    while (j < rx_len && rx_buf[j] != kSysExEnd) {
      j++;
    }
    if (j >= rx_len) {
      if (i > 0) {
        memmove(rx_buf, rx_buf + i, rx_len - i);
        rx_len -= i;
      }
      return;
    }
    uint8_t* msg = &rx_buf[i];
    const size_t msg_len = j - i + 1;
    if (msg_len >= 5 && msg[1] == kManufacturer && msg[2] == kDevice) {
      const uint8_t cmd = msg[3];
      if (cmd == kCmdGetConfig) {
        SendConfigSysEx();
      } else if (cmd == kCmdSetConfig && msg_len >= 6) {
        ReceiveConfigSysEx(&msg[4], msg_len - 5);
      } else if (cmd == kCmdSaveConfig) {
        MarkConfigDirty();
      }
    }
    const size_t after = j + 1;
    memmove(rx_buf, rx_buf + after, rx_len - after);
    rx_len -= after;
  }
}

void GridsCard::SendConfigSysEx() {
  ConfigStore::Data snapshot{};
  critical_section_enter_blocking(&cfg_cs_);
  snapshot = cfg_;
  critical_section_exit(&cfg_cs_);
  const uint8_t* raw = reinterpret_cast<const uint8_t*>(&snapshot);
  const size_t raw_len = sizeof(ConfigStore::Data);
  uint8_t msg[192] = {};
  size_t out = 0;
  msg[out++] = kSysExStart;
  msg[out++] = kManufacturer;
  msg[out++] = kDevice;
  msg[out++] = 0x02;
  out += Encode7Bit(raw, raw_len, &msg[out], sizeof(msg) - out - 1);
  msg[out++] = kSysExEnd;
  tud_midi_stream_write(0, msg, out);
}

void GridsCard::ReceiveConfigSysEx(const uint8_t* payload, size_t len) {
  if (len == 0) return;
  uint8_t decoded[sizeof(ConfigStore::Data)] = {};
  const size_t decoded_len = Decode7Bit(payload, len, decoded, sizeof(decoded));
  if (decoded_len < sizeof(ConfigStore::Data)) return;
  ConfigStore::Data incoming;
  std::memcpy(&incoming, decoded, sizeof(incoming));
  if (incoming.magic != ConfigStore::kMagic) return;
  SanitizeConfig(incoming);
  critical_section_enter_blocking(&cfg_cs_);
  pending_sysex_cfg_ = incoming;
  pending_sysex_cfg_valid_ = true;
  critical_section_exit(&cfg_cs_);
}

void GridsCard::MarkConfigDirty() {
  critical_section_enter_blocking(&cfg_cs_);
  pending_save_ = true;
  last_change_us_ = time_us_64();
  critical_section_exit(&cfg_cs_);
}

void GridsCard::SanitizeConfig(ConfigStore::Data& cfg) {
  cfg.magic = ConfigStore::kMagic;
  cfg.bpm10 = Clamp<uint16_t>(cfg.bpm10, 400, 2600);
  cfg.swing = Clamp<uint8_t>(cfg.swing, 50, 75);
  cfg.chaos = Clamp<uint8_t>(cfg.chaos, 0, 127);
  cfg.cv1_mode = (cfg.cv1_mode <= static_cast<uint8_t>(ConfigStore::CV1ToBlend))
                     ? cfg.cv1_mode
                     : static_cast<uint8_t>(ConfigStore::CV1ToBlend);
  cfg.cv2_mode = ConfigStore::CV2ToFill;
  cfg.cv1_amount = Clamp<int8_t>(cfg.cv1_amount, -127, 127);
  cfg.cv2_amount = Clamp<int8_t>(cfg.cv2_amount, -127, 127);
  cfg.lane1_fill_scale = Clamp<uint8_t>(cfg.lane1_fill_scale, 0, 200);
  cfg.lane2_fill_scale = Clamp<uint8_t>(cfg.lane2_fill_scale, 0, 200);
  cfg.lane3_fill_scale = Clamp<uint8_t>(cfg.lane3_fill_scale, 0, 200);
  cfg.lane1_fill_offset = Clamp<int8_t>(cfg.lane1_fill_offset, -127, 127);
  cfg.lane2_fill_offset = Clamp<int8_t>(cfg.lane2_fill_offset, -127, 127);
  cfg.lane3_fill_offset = Clamp<int8_t>(cfg.lane3_fill_offset, -127, 127);
  cfg.aux_mode = (cfg.aux_mode <= static_cast<uint8_t>(ConfigStore::AuxLane3Mirror))
                     ? cfg.aux_mode
                     : static_cast<uint8_t>(ConfigStore::AuxAccent);
  cfg.pulse_ms = Clamp<uint8_t>(cfg.pulse_ms, 1, 40);
  for (size_t i = 0; i < sizeof(cfg.reserved); ++i) cfg.reserved[i] = 0;
}

