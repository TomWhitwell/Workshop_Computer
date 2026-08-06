#pragma once

#include <cstdint>

namespace mtws {

constexpr uint8_t kNumOscillatorSlots = 6;
constexpr uint8_t kNumCumulusVoices = 16;
constexpr uint8_t kNumSawsomeVoices = 7;

struct UISnapshot {
  uint16_t knob_main;
  uint16_t knob_x;
  uint16_t knob_y;
  int16_t cv1;
  int16_t cv2;
  int16_t audio1;
  int16_t audio2;
  bool audio1_connected;
  bool audio2_connected;
  bool cv2_connected;
  bool pulse1_connected;
  bool pulse1_high;
  uint8_t selected_slot;
  bool panel_alt_latched;
};

struct MidiState {
  bool note_active;
  uint8_t current_note;
  uint8_t last_note;
  uint8_t cc74_value;
  uint32_t note_on_counter;
  // Monotonic counter of received 0xF8 MIDI clock ticks.
  uint32_t clock_tick_count;
};

struct GlobalControlFrame {
  uint8_t selected_slot;
  bool mode_alt;
  uint16_t macro_x;
  uint16_t macro_y;
  uint32_t pitch_inc;
  uint16_t vca_gain_q12;
  uint8_t current_midi_note;
  uint8_t last_midi_note;
  uint8_t midi_cc74_value;
  bool midi_note_active;
  uint32_t note_on_counter;
  uint32_t midi_clock_tick_count;
};

struct SawsomeControlFrame {
  uint32_t voice_phase_increment[kNumSawsomeVoices];
  // Precomputed reciprocal for PolyBLEP: (1<<36) / phase_inc, computed at
  // control rate so audio-rate BLEP uses a 64-bit multiply (~15 cycles)
  // instead of a 64-bit division (~80 cycles).
  uint32_t voice_phase_inc_recip_q24[kNumSawsomeVoices];
  int16_t voice_gain_l_q12[kNumSawsomeVoices];
  int16_t voice_gain_r_q12[kNumSawsomeVoices];
  bool alt;
};

struct BenderControlFrame {
  uint32_t phase_increment;
  uint16_t fold_drive_q7;
  uint16_t fold_mix_q12;
  int16_t crush_amount;
  bool alt;
};

struct FloatableControlFrame {
  // Oscillator phase increment in 0.32 phase units/sample.
  uint32_t phase_inc;
  // Selects the active curated source bank.
  // 0: both outputs read bank 1, 1: both outputs read bank 4.
  uint8_t use_alt_banks;
  // Base source-wave index for Out1 morph in 0..14.
  uint8_t out1_wave_index;
  // Q12 morph fraction for Out1 in 0..4096 between out1_wave_index and +1.
  uint16_t out1_wave_frac_q12;
  // Base source-wave index for Out2 morph in 0..14.
  uint8_t out2_wave_index;
  // Q12 morph fraction for Out2 in 0..4096 between out2_wave_index and +1.
  uint16_t out2_wave_frac_q12;
};

struct CumulusControlFrame {
  uint32_t voice_phase_increment[kNumCumulusVoices];
  int32_t voice_gain_q12[kNumCumulusVoices];
  int32_t mix_norm_q12;
};

struct LosengeControlFrame {
  uint32_t phase_increment;
  uint32_t formant_increment[2][3];
  uint16_t formant_amplitude_q12[2][3];
  uint16_t voice_gain_q12;
};

struct DinSumControlFrame {
  uint32_t phase_increment;
  // Precomputed PolyBLEP reciprocal, same scheme as SawsomeControlFrame.
  uint32_t phase_inc_recip_q24;
  uint16_t morph_q12;
  uint16_t coherence_q12;
  int16_t random_lp_prev;
  int16_t random_lp_next;
  int16_t random_hp_prev;
  int16_t random_hp_next;
  bool alt;
  uint32_t frame_epoch;
};

struct EngineControlFrame {
  SawsomeControlFrame sawsome;
  BenderControlFrame bender;
  FloatableControlFrame floatable;
  CumulusControlFrame cumulus;
  LosengeControlFrame losenge;
  DinSumControlFrame din_sum;
};

struct ControlFrame {
  GlobalControlFrame global;
  EngineControlFrame engine;
};

}  // namespace mtws
