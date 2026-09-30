// JP4k-sandsquall - JP-inspired supersaw voice for Workshop Computer.
//
// This is not an attempt to copy Roland firmware. It is a small Workshop-sized
// instrument built around the same playable idea: a wide, detuned stack of saws
// that can behave as a CV/gate oscillator, USB MIDI voice, or clocked internal
// sequencer. The first sequencer pattern is seeded from the main sting rhythm
// in the user's Sandstorm MIDI file.
//
// Audio-rate work stays deliberately plain: seven 32-bit phase accumulators,
// a two-pole fixed-point low-pass, one envelope, and integer mixing. Pitch, panel,
// USB MIDI, and sequencer control work is handled on core 1.

#include "ComputerCard.h"

#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "tusb.h"
#include "usb_midi_host.h"

namespace {

constexpr int32_t kControlMask = 31;
constexpr int32_t kMaxAudio = 2047;
constexpr int32_t kMinAudio = -2048;
constexpr int32_t kTuneSpreadDeadband = 96;
constexpr int32_t kTransientMax = 4095;
constexpr int32_t kPitchUnitsPerOctave = 4096;
constexpr int32_t kAudioPitchInputCountsPerVolt = 341;
constexpr int32_t kBaseMidiNote = 36;      // C2, matching fr330hfr33/Cosmik.
constexpr int32_t kCenterMidiNote = 60;    // C4 at Main noon with no CV.
constexpr int32_t kCenterPitchUnits =
    ((kCenterMidiNote - kBaseMidiNote) * kPitchUnitsPerOctave) / 12;
constexpr int32_t kMinPitchUnits = -2 * kPitchUnitsPerOctave;
constexpr int32_t kMaxPitchUnits = 8 * kPitchUnitsPerOctave;
constexpr uint32_t kC2PhaseIncrement = 5852465u;
constexpr uint8_t kMidiCcModWheel = 1;
constexpr uint8_t kMidiCcVolume = 7;
constexpr uint8_t kMidiCcSpread = 20;
constexpr uint8_t kMidiCcBrightnessAlt = 21;
constexpr uint8_t kMidiCcBrightness = 74;
constexpr int32_t kMidiActivitySamples = 24000;
constexpr int kPatternLengthMax = 32;
constexpr uint8_t kPatternRest = 127;
constexpr uint8_t kDefaultSequencerTempo = 136;
constexpr uint8_t kSysexManufacturer = 0x7d;
constexpr uint8_t kSysexCommandPattern = 0x01;
constexpr uint8_t kSysexCommandControls = 0x02;
constexpr uint8_t kMidiClock = 0xF8;
constexpr uint8_t kMidiStart = 0xFA;
constexpr uint8_t kMidiContinue = 0xFB;
constexpr uint8_t kMidiStop = 0xFC;
constexpr uint8_t kMidiClockTicksPerStep = 6; // 24 PPQN -> sixteenth notes.
constexpr uint32_t kMidiClockTimeoutUs = 500000;

int32_t clamp_int(int32_t v, int32_t lo, int32_t hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

uint32_t pitch_units_to_inc(int32_t units)
{
    static constexpr uint32_t kSemitoneRatioQ15[13] = {
        32768u, 34716u, 36781u, 38968u, 41285u, 43740u, 46341u,
        49097u, 52016u, 55109u, 58386u, 61858u, 65536u
    };

    units = clamp_int(units, kMinPitchUnits, kMaxPitchUnits);
    int32_t octaves = units / kPitchUnitsPerOctave;
    int32_t remainder = units % kPitchUnitsPerOctave;
    if (remainder < 0) {
        remainder += kPitchUnitsPerOctave;
        --octaves;
    }

    int32_t semitone = (remainder * 12) / kPitchUnitsPerOctave;
    int32_t start = (semitone * kPitchUnitsPerOctave) / 12;
    int32_t end = ((semitone + 1) * kPitchUnitsPerOctave) / 12;
    int32_t fraction = ((remainder - start) << 15) / (end - start);
    uint32_t ratio = kSemitoneRatioQ15[semitone] +
        (uint32_t)((((int32_t)kSemitoneRatioQ15[semitone + 1] -
        (int32_t)kSemitoneRatioQ15[semitone]) * fraction) >> 15);

    uint32_t increment = (uint32_t)(((uint64_t)kC2PhaseIncrement * ratio) >> 15);
    if (octaves >= 0)
        increment <<= octaves;
    else
        increment >>= -octaves;
    return increment;
}

int32_t clip_audio(int32_t v)
{
    return clamp_int(v, kMinAudio, kMaxAudio);
}

} // namespace

class JP4KSandsquall : public ComputerCard {
public:
    JP4KSandsquall()
    {
        static constexpr uint32_t kClusteredPhase[kSawCount] = {
            0xFFF00000u, 0x00080000u, 0x00180000u, 0x00000000u,
            0xFFE80000u, 0x00280000u, 0xFFD80000u
        };
        for (int i = 0; i < kSawCount; ++i) {
            phase_[i] = kClusteredPhase[i];
            inc_[i] = pitch_units_to_inc(kCenterPitchUnits);
        }
    }

    bool ShouldBootUsbHost()
    {
        return USBPowerState() == USBPowerState_t::DFP;
    }

    void SetUsbHostMode(bool host_mode)
    {
        usb_host_mode_ = host_mode;
    }

    void SetUsbMidiConnected(bool connected)
    {
        usb_midi_connected_ = connected;
        midi_activity_countdown_ = kMidiActivitySamples;
        if (!connected) {
            midi_clock_seen_ = false;
            midi_transport_running_ = false;
            midi_clock_ticks_ = 0;
            midi_clock_steps_ = 0;
            midi_start_events_ = 0;
            midi_stop_events_ = 0;
            midi_clock_last_us_ = 0;
        }
    }

    void ProcessUsbMidiByte(uint8_t byte)
    {
        midi_activity_countdown_ = kMidiActivitySamples;
        if (process_midi_realtime_byte(byte)) {
            return;
        }
        if (process_sysex_byte(byte)) {
            return;
        }
        process_midi_voice_byte(byte);
    }

    uint8_t MidiInChannel() const
    {
        return midi_in_channel_;
    }

    void ProcessUsbMidiVoiceByte(uint8_t byte)
    {
        ProcessUsbMidiByte(byte);
    }

    void SendPendingUsbMidiOutput()
    {
    }

    void RefreshControls()
    {
        update_controls();
    }

    virtual void __not_in_flash_func(ProcessSample)()
    {
        apply_pending_midi_events();
        if (midi_activity_countdown_ > 0) {
            --midi_activity_countdown_;
        }
        capture_sequencer_pulses();

        const bool lead_gate = !sequencer_mode_ && Connected(Input::Pulse1) && PulseIn1();
        const bool accent_gate = !sequencer_mode_ && Connected(Input::Pulse2) && PulseIn2();
        const bool midi_gate = !sequencer_mode_ && midi_note_active_;
        const bool sequence_gate = sequencer_gate_;
        const uint32_t sequence_trigger = sequencer_trigger_;
        const bool gate = lead_gate || midi_gate || sequence_gate;
        if (lead_gate && !last_lead_gate_) {
            sync_supersaw_phases();
            transient_env_ = kTransientMax;
        }
        last_lead_gate_ = lead_gate;
        if (sequence_trigger != last_sequencer_trigger_) {
            last_sequencer_trigger_ = sequence_trigger;
            sync_supersaw_phases();
            transient_env_ = kTransientMax;
        }
        if (accent_gate && !last_accent_gate_) {
            transient_env_ = kTransientMax;
        }
        last_accent_gate_ = accent_gate;

        if (gate) {
            envelope_ += attack_step_;
            if (envelope_ > 4095) envelope_ = 4095;
        } else {
            envelope_ -= release_step_;
            if (envelope_ < 0) envelope_ = 0;
        }

        if (!gate && envelope_ == 0) {
            filter_state_ = 0;
            filter_stage_ = 0;
            side_state_ = 0;
            AudioOut1(0);
            AudioOut2(0);
            CVOut1Precise((pitch_units_ - kCenterPitchUnits) << 3);
            CVOut2Precise((filter_coeff_ - 128) << 5);
            PulseOut1(false);
            PulseOut2(false);
            update_leds(false);
            return;
        }

        int32_t mono = render_supersaw();
        filter_state_ += ((mono - filter_state_) * filter_coeff_) >> 12;
        filter_stage_ += ((filter_state_ - filter_stage_) * filter_coeff_) >> 12;

        int32_t mixed = filter_stage_;
        if (transient_env_ > 0) {
            mixed += (mono * transient_env_) >> 13;
            transient_env_ -= transient_decay_;
            if (transient_env_ < 0) transient_env_ = 0;
        }
        mixed = (mixed * envelope_) >> 12;
        mixed = (mixed * level_) >> 11;

        int32_t right = mixed;
        int32_t left = mixed;
        if (stereo_mode_) {
            left += (side_state_ * stereo_width_) >> 12;
            right -= (side_state_ * stereo_width_) >> 12;
        }

        AudioOut1(clip_audio(left));
        AudioOut2(clip_audio(right));

        CVOut1Precise((pitch_units_ - kCenterPitchUnits) << 3);
        CVOut2Precise((filter_coeff_ - 128) << 5);
        PulseOut1(gate);
        PulseOut2(phase_[0] & 0x80000000u);

        update_leds(gate);
    }

private:
    static constexpr int kSawCount = 7;

    uint32_t phase_[kSawCount];
    uint32_t inc_[kSawCount];

    int32_t pitch_units_ = kCenterPitchUnits;
    int32_t filter_state_ = 0;
    int32_t filter_stage_ = 0;
    int32_t side_state_ = 0;
    int32_t filter_coeff_ = 900;
    int32_t stereo_width_ = 0;
    int32_t level_ = 2400;
    int32_t envelope_ = 0;
    int32_t transient_env_ = 0;
    int32_t transient_decay_ = 24;
    int32_t attack_step_ = 16;
    int32_t release_step_ = 4;
    bool drone_mode_ = false;
    bool sequencer_mode_ = false;
    bool stereo_mode_ = false;
    bool tune_mode_ = true;
    bool last_lead_gate_ = false;
    bool last_accent_gate_ = false;
    uint32_t last_sequencer_trigger_ = 0;

    uint8_t midi_in_channel_ = 0;
    uint8_t midi_running_status_ = 0;
    uint8_t midi_data_[2] = {};
    uint8_t midi_data_count_ = 0;
    uint8_t midi_clock_ticks_ = 0;
    uint32_t midi_clock_steps_ = 0;
    uint32_t midi_start_events_ = 0;
    uint32_t midi_stop_events_ = 0;
    bool midi_clock_seen_ = false;
    uint32_t midi_clock_last_us_ = 0;
    bool midi_transport_running_ = false;
    uint8_t midi_note_ = kCenterMidiNote;
    uint8_t midi_velocity_ = 100;
    volatile uint8_t pending_midi_note_ = kCenterMidiNote;
    volatile uint8_t pending_midi_velocity_ = 100;
    volatile uint8_t pending_midi_note_off_ = kCenterMidiNote;
    volatile bool pending_midi_note_on_ = false;
    volatile bool pending_midi_note_off_event_ = false;
    volatile bool midi_note_active_ = false;
    volatile int32_t midi_pitch_bend_ = 0;
    volatile int32_t midi_spread_cc_ = 0;
    volatile int32_t midi_brightness_cc_ = 0;
    volatile int32_t midi_volume_cc_ = 4095;
    volatile bool midi_spread_cc_active_ = false;
    volatile bool midi_brightness_cc_active_ = false;
    volatile bool midi_volume_cc_active_ = false;
    volatile bool usb_host_mode_ = false;
    volatile bool usb_midi_connected_ = false;
    volatile int32_t midi_activity_countdown_ = 0;
    volatile bool sequencer_gate_ = false;
    volatile uint8_t sequencer_note_ = kCenterMidiNote;
    volatile bool sequencer_accent_ = false;
    volatile uint32_t sequencer_trigger_ = 0;
    volatile uint32_t sequencer_clock_events_ = 0;
    volatile uint32_t sequencer_reset_events_ = 0;

    uint8_t sequencer_tempo_ = kDefaultSequencerTempo;
    uint8_t pattern_length_ = kPatternLengthMax;
    uint8_t pattern_step_ = 0;
    uint8_t pattern_note_[kPatternLengthMax] = {
        71, 71, 71, 71, 71, kPatternRest, 71, 71,
        71, 71, 71, 71, 71, kPatternRest, 76, 76,
        76, 76, 76, 76, 76, kPatternRest, 74, 74,
        74, 74, 74, 74, 74, kPatternRest, 69, 69
    };
    uint8_t pattern_gate_[kPatternLengthMax] = {
        36, 36, 40, 42, 78, 0, 42, 60,
        36, 36, 40, 44, 78, 0, 55, 76,
        42, 38, 42, 46, 82, 0, 55, 78,
        42, 38, 42, 46, 78, 0, 58, 88
    };
    uint32_t pattern_accent_mask_ =
        (1u << 0) | (1u << 4) | (1u << 7) | (1u << 8) |
        (1u << 12) | (1u << 15) | (1u << 16) | (1u << 20) |
        (1u << 23) | (1u << 24) | (1u << 28) | (1u << 31);
    int32_t sequencer_brightness_offset_ = 0;
    uint64_t next_internal_step_us_ = 0;
    uint64_t sequencer_step_period_us_ = 220588ull;
    uint64_t sequencer_gate_off_us_ = 0;
    uint32_t consumed_clock_events_ = 0;
    uint32_t consumed_reset_events_ = 0;
    uint32_t consumed_midi_clock_steps_ = 0;
    uint32_t consumed_midi_start_events_ = 0;
    uint32_t consumed_midi_stop_events_ = 0;
    bool audio_clock_high_ = false;
    bool audio_reset_high_ = false;
    bool sequencer_running_ = false;
    bool sequencer_down_hold_ = false;
    Switch last_switch_ = Switch::Up;
    bool sysex_receiving_ = false;
    bool sysex_matching_ = false;
    uint8_t sysex_index_ = 0;
    uint8_t sysex_command_ = 0;
    uint8_t sysex_payload_[80] = {};
    uint8_t sysex_payload_len_ = 0;

    void update_controls()
    {
        const int32_t main = KnobVal(Knob::Main);
        const int32_t x = midi_spread_cc_active_ ? midi_spread_cc_ : KnobVal(Knob::X);
        const int32_t y =
            (usb_host_mode_ && midi_brightness_cc_active_) ? midi_brightness_cc_ : KnobVal(Knob::Y);

        const Switch sw = SwitchVal();
        const uint32_t now_us = time_us_32();
        const bool pulse_clock_connected = Connected(Input::Pulse2);
        const bool midi_clock_active = midi_clock_is_active(now_us) && !pulse_clock_connected;

        // Z-down is neutral in synth mode. When it is entered from the
        // sequencer position, it is a momentary transport button.
        if (sw == Switch::Down && last_switch_ != Switch::Down) {
            sequencer_down_hold_ = sequencer_mode_;
            if (sequencer_down_hold_ && !pulse_clock_connected && !midi_clock_active) {
                sequencer_running_ = !sequencer_running_;
                sequencer_gate_ = false;
                if (sequencer_running_) {
                    pattern_step_ = 0;
                    next_internal_step_us_ = 0;
                }
            }
        } else if (sw != Switch::Down) {
            sequencer_down_hold_ = false;
        }

        drone_mode_ = false;
        sequencer_mode_ = (sw == Switch::Middle) ||
            (sw == Switch::Down && sequencer_down_hold_);
        stereo_mode_ = true;
        update_sequencer();
        last_switch_ = sw;

        // Main is now a playable transpose/tune control around middle C rather
        // than a huge sweep.
        //
        // Audio In 1 is the pitch input. One volt is one octave; ComputerCard
        // 0.4.0 stores audio-input calibration in EEPROM, with a documented
        // 341-counts-per-volt fallback for older or uncalibrated Computers.
        int32_t units = kCenterPitchUnits;
        if (sequencer_mode_ && sequencer_gate_) {
            units = midi_note_pitch_units(sequencer_note_);
        } else if (midi_note_active_) {
            units = midi_note_pitch_units(midi_note_);
        } else {
            units += (((main - 2048) * (2 * kPitchUnitsPerOctave)) >> 12);
        }
        if (Connected(Input::Audio1)) {
            if (InputsCalibrated()) {
                units += (AudioIn1Millivolts() * kPitchUnitsPerOctave) / 1000;
            } else {
                units += (AudioIn1() * kPitchUnitsPerOctave) /
                    kAudioPitchInputCountsPerVolt;
            }
        }
        if (!sequencer_mode_ && midi_note_active_) {
            units += (midi_pitch_bend_ * kPitchUnitsPerOctave) / (8192 * 6);
        }
        units = clamp_int(units, kMinPitchUnits, kMaxPitchUnits);
        pitch_units_ = units;

        const uint32_t base_inc = pitch_units_to_inc(units);

        int32_t spread = x + (x >> 2);
        if (x > 2048) {
            spread += (x - 2048) >> 1;
        }
        if (Connected(Input::CV2)) {
            spread += CVIn2();
        }
        spread = clamp_int(spread, 0, 4095);
        tune_mode_ = (spread <= kTuneSpreadDeadband);

        // Detune is deliberately asymmetric: the centre voice stays stable,
        // while the outer saws fan out more quickly for that animated JP feel.
        const int32_t detune = tune_mode_ ? 0 : ((spread * spread) >> 13) + (spread >> 2);
        constexpr int32_t ratios[kSawCount] = {-28, -17, -9, 0, 10, 19, 31};
        for (int i = 0; i < kSawCount; ++i) {
            int32_t offset = static_cast<int32_t>(
                ((static_cast<int64_t>(base_inc) * detune * ratios[i]) >> 22));
            int32_t detuned = static_cast<int32_t>(base_inc) + offset;
            if (detuned < 1) detuned = 1;
            inc_[i] = static_cast<uint32_t>(detuned);
        }

        // Y is a simple brightness control. At low values it rounds the stack
        // into a warm pad; high values leave the saw edge bright for external
        // filtering in the Workshop System.
        const int32_t filter_cv = Connected(Input::CV1) ? CVIn1() : 0;
        const int32_t shaped_y = clamp_int(
            y + filter_cv + (sequencer_mode_ ? sequencer_brightness_offset_ : 0), 0, 4095);
        filter_coeff_ = 220 + ((shaped_y * shaped_y) >> 12);
        if (shaped_y > 2048) {
            filter_coeff_ += (shaped_y - 2048) >> 1;
        }
        if (filter_coeff_ > 4095) filter_coeff_ = 4095;

        stereo_width_ = (stereo_mode_ && !tune_mode_) ? (spread >> 3) : 0;
        level_ = 3600 + ((4095 - (spread >> 1)) >> 3);
        if (midi_volume_cc_active_) {
            const int32_t volume_scale = 1024 + ((midi_volume_cc_ * 3) >> 2);
            level_ = (level_ * volume_scale) >> 12;
        }
        if (midi_note_active_) {
            const int32_t velocity_scale = 2048 + ((int32_t)midi_velocity_ << 4);
            level_ = (level_ * velocity_scale) >> 12;
        }
        const bool pulse2_high = !sequencer_mode_ && Connected(Input::Pulse2) && PulseIn2();
        if (pulse2_high || sequencer_accent_) level_ += 420;
        transient_decay_ = (pulse2_high || sequencer_accent_) ? 16 : 24;
        attack_step_ = 48;
        release_step_ = 10;
    }

    int32_t __not_in_flash_func(render_supersaw)()
    {
        if (tune_mode_) {
            phase_[3] += inc_[3];
            side_state_ = 0;
            return static_cast<int32_t>(phase_[3] >> 20) - 2048;
        }

        int32_t sum = 0;
        int32_t side = 0;
        for (int i = 0; i < kSawCount; ++i) {
            phase_[i] += inc_[i];
            int32_t saw = static_cast<int32_t>(phase_[i] >> 20) - 2048;
            sum += saw * ((i == 3) ? 12 : 6);
            side += saw * ((i & 1) ? 1 : -1);
        }

        side_state_ = side >> 5;
        return sum >> 5;
    }

    void __not_in_flash_func(sync_supersaw_phases)()
    {
        static constexpr uint32_t kClusteredPhase[kSawCount] = {
            0xFFF00000u, 0x00080000u, 0x00180000u, 0x00000000u,
            0xFFE80000u, 0x00280000u, 0xFFD80000u
        };
        for (int i = 0; i < kSawCount; ++i) {
            phase_[i] = kClusteredPhase[i];
        }
    }

    void process_midi_voice_byte(uint8_t byte)
    {
        if (byte >= 0xF8u) {
            return;
        }

        if (byte & 0x80u) {
            midi_running_status_ = byte;
            midi_data_count_ = 0;
            return;
        }

        const uint8_t type = midi_running_status_ & 0xF0u;
        if (type != 0x80u && type != 0x90u && type != 0xB0u && type != 0xE0u) {
            return;
        }

        midi_data_[midi_data_count_++] = byte & 0x7Fu;
        if (midi_data_count_ < 2u) {
            return;
        }

        midi_data_count_ = 0;
        const uint8_t channel = midi_running_status_ & 0x0Fu;
        if (channel != midi_in_channel_) {
            return;
        }

        if (type == 0x90u && midi_data_[1] > 0) {
            pending_midi_note_ = midi_data_[0];
            pending_midi_velocity_ = midi_data_[1];
            pending_midi_note_on_ = true;
            return;
        }

        if (type == 0x80u || (type == 0x90u && midi_data_[1] == 0)) {
            pending_midi_note_off_ = midi_data_[0];
            pending_midi_note_off_event_ = true;
            return;
        }

        if (type == 0xB0u) {
            const int32_t control = midi_cc_to_control(midi_data_[1]);
            if (midi_data_[0] == kMidiCcModWheel || midi_data_[0] == kMidiCcSpread) {
                midi_spread_cc_ = control;
                midi_spread_cc_active_ = true;
            } else if (midi_data_[0] == kMidiCcBrightness ||
                       midi_data_[0] == kMidiCcBrightnessAlt) {
                midi_brightness_cc_ = control;
                midi_brightness_cc_active_ = true;
            } else if (midi_data_[0] == kMidiCcVolume) {
                midi_volume_cc_ = control;
                midi_volume_cc_active_ = true;
            }
            return;
        }

        if (type == 0xE0u) {
            const int32_t bend = ((int32_t)midi_data_[1] << 7) | midi_data_[0];
            midi_pitch_bend_ = bend - 8192;
        }
    }

    bool process_midi_realtime_byte(uint8_t byte)
    {
        if (byte == kMidiClock) {
            midi_clock_seen_ = true;
            midi_clock_last_us_ = time_us_32();
            if (midi_transport_running_ && ++midi_clock_ticks_ >= kMidiClockTicksPerStep) {
                midi_clock_ticks_ = 0;
                ++midi_clock_steps_;
            }
            return true;
        }

        if (byte == kMidiStart) {
            midi_clock_seen_ = true;
            midi_clock_last_us_ = time_us_32();
            midi_transport_running_ = true;
            midi_clock_ticks_ = 0;
            ++midi_start_events_;
            return true;
        }

        if (byte == kMidiContinue) {
            midi_clock_seen_ = true;
            midi_clock_last_us_ = time_us_32();
            midi_transport_running_ = true;
            return true;
        }

        if (byte == kMidiStop) {
            midi_clock_seen_ = true;
            midi_clock_last_us_ = time_us_32();
            midi_transport_running_ = false;
            midi_clock_ticks_ = 0;
            ++midi_stop_events_;
            return true;
        }

        // Realtime messages can be interleaved inside any MIDI message.
        return byte >= 0xF8u;
    }

    bool process_sysex_byte(uint8_t byte)
    {
        if (byte == 0xF0u) {
            sysex_receiving_ = true;
            sysex_matching_ = true;
            sysex_index_ = 0;
            sysex_command_ = 0;
            sysex_payload_len_ = 0;
            return true;
        }

        if (!sysex_receiving_) {
            return false;
        }

        if (byte == 0xF7u) {
            if (sysex_matching_) {
                apply_sysex_message();
            }
            sysex_receiving_ = false;
            return true;
        }

        if (byte & 0x80u) {
            sysex_receiving_ = false;
            return false;
        }

        static constexpr uint8_t kId[4] = {'J', '4', 'K', 'S'};
        if (sysex_index_ == 0) {
            sysex_matching_ = (byte == kSysexManufacturer);
        } else if (sysex_index_ >= 1 && sysex_index_ <= 4) {
            sysex_matching_ = sysex_matching_ && (byte == kId[sysex_index_ - 1]);
        } else if (sysex_index_ == 5) {
            sysex_command_ = byte;
        } else if (sysex_payload_len_ < sizeof(sysex_payload_)) {
            sysex_payload_[sysex_payload_len_++] = byte;
        }
        ++sysex_index_;
        return true;
    }

    void apply_sysex_message()
    {
        if (sysex_command_ == kSysexCommandPattern) {
            if (sysex_payload_len_ < 4) {
                return;
            }
            const int32_t tempo = sysex_payload_[0] | ((int32_t)sysex_payload_[1] << 7);
            sequencer_tempo_ = clamp_int(tempo, 30, 240);
            pattern_length_ = clamp_int(sysex_payload_[2], 1, kPatternLengthMax);
            pattern_accent_mask_ = sysex_payload_[3];
            if (sysex_payload_len_ > 4) pattern_accent_mask_ |= (uint32_t)sysex_payload_[4] << 7;
            if (sysex_payload_len_ > 5) pattern_accent_mask_ |= (uint32_t)sysex_payload_[5] << 14;
            if (sysex_payload_len_ > 6) pattern_accent_mask_ |= (uint32_t)sysex_payload_[6] << 21;
            if (sysex_payload_len_ > 7) pattern_accent_mask_ |= (uint32_t)sysex_payload_[7] << 28;
            uint8_t offset = 8;
            for (uint8_t i = 0; i < pattern_length_ && offset < sysex_payload_len_; ++i) {
                uint8_t note = sysex_payload_[offset++];
                pattern_note_[i] = note <= 127 ? note : kPatternRest;
            }
            for (uint8_t i = 0; i < pattern_length_ && offset < sysex_payload_len_; ++i) {
                pattern_gate_[i] = clamp_int(sysex_payload_[offset++], 5, 100);
            }
            pattern_step_ = 0;
            next_internal_step_us_ = 0;
            sequencer_gate_ = false;
        } else if (sysex_command_ == kSysexCommandControls && sysex_payload_len_ >= 3) {
            midi_spread_cc_ = ((int32_t)sysex_payload_[0] * 4095) / 127;
            midi_brightness_cc_ = ((int32_t)sysex_payload_[1] * 4095) / 127;
            midi_volume_cc_ = ((int32_t)sysex_payload_[2] * 4095) / 127;
            midi_spread_cc_active_ = true;
            midi_brightness_cc_active_ = usb_host_mode_;
            midi_volume_cc_active_ = true;
        }
    }

    void update_sequencer()
    {
        if (!sequencer_mode_) {
            sequencer_gate_ = false;
            pattern_step_ = 0;
            next_internal_step_us_ = 0;
            consumed_clock_events_ = sequencer_clock_events_;
            consumed_reset_events_ = sequencer_reset_events_;
            consumed_midi_clock_steps_ = midi_clock_steps_;
            consumed_midi_start_events_ = midi_start_events_;
            consumed_midi_stop_events_ = midi_stop_events_;
            sequencer_running_ = false;
            return;
        }

        const uint64_t now = time_us_64();
        const bool pulse_clock_connected = Connected(Input::Pulse2);
        const uint32_t reset_events = sequencer_reset_events_;
        if (reset_events != consumed_reset_events_) {
            consumed_reset_events_ = reset_events;
            pattern_step_ = 0;
            next_internal_step_us_ = 0;
            sequencer_gate_ = false;
        }

        const uint32_t clock_events = sequencer_clock_events_;
        const bool external_step = clock_events != consumed_clock_events_;
        if (external_step) consumed_clock_events_ = clock_events;

        const bool midi_clock_active = midi_clock_is_active(time_us_32()) && !pulse_clock_connected;
        if (!midi_clock_active) {
            // Pulse In 2 has priority, so discard MIDI events collected while
            // a physical clock cable is patched.
            consumed_midi_clock_steps_ = midi_clock_steps_;
            consumed_midi_start_events_ = midi_start_events_;
            consumed_midi_stop_events_ = midi_stop_events_;
        } else {
            if (midi_start_events_ != consumed_midi_start_events_) {
                consumed_midi_start_events_ = midi_start_events_;
                pattern_step_ = 0;
                next_internal_step_us_ = 0;
                sequencer_gate_ = false;
                sequencer_running_ = true;
            }
            if (midi_stop_events_ != consumed_midi_stop_events_) {
                consumed_midi_stop_events_ = midi_stop_events_;
                sequencer_gate_ = false;
                sequencer_running_ = false;
            }
        }

        const bool midi_step = midi_clock_active && sequencer_running_ &&
            midi_transport_running_ &&
            midi_clock_steps_ != consumed_midi_clock_steps_;
        if (midi_step) {
            consumed_midi_clock_steps_ = midi_clock_steps_;
        }

        int32_t tempo = sequencer_tempo_;
        if (tempo == 0) {
            tempo = 136;
        }
        if (!usb_midi_connected_) {
            tempo = 60 + ((KnobVal(Knob::Main) * 181) >> 12);
        }
        sequencer_step_period_us_ = 60000000ull / ((uint64_t)tempo * 4ull);

        bool internal_step = false;
        if (sequencer_running_ && !pulse_clock_connected && !midi_clock_active) {
            if (next_internal_step_us_ == 0 || now >= next_internal_step_us_) {
                internal_step = true;
                next_internal_step_us_ = now + sequencer_step_period_us_;
            }
        }

        if (external_step || midi_step || internal_step) {
            play_pattern_step(now);
        }

        if (sequencer_gate_ && now >= sequencer_gate_off_us_) {
            sequencer_gate_ = false;
        }
    }

    void __not_in_flash_func(capture_sequencer_pulses)()
    {
        const bool reset_high = PulseIn1();
        const bool clock_high = PulseIn2();

        if (sequencer_mode_) {
            if (reset_high && !audio_reset_high_) {
                ++sequencer_reset_events_;
            }
            if (clock_high && !audio_clock_high_) {
                ++sequencer_clock_events_;
            }
        }

        audio_reset_high_ = reset_high;
        audio_clock_high_ = clock_high;
    }

    void play_pattern_step(uint64_t now)
    {
        static constexpr int16_t kSandstormBrightness[kPatternLengthMax] = {
             440,  260,  320,  420,  640,  120,  360,  560,
             500,  300,  360,  460,  700,  160,  620,  760,
             720,  520,  580,  660,  820,  220,  520,  700,
             500,  320,  380,  460,  640,  140,  440,  600
        };
        const uint8_t length = pattern_length_ == 0 ? kPatternLengthMax : pattern_length_;
        const uint8_t step = pattern_step_ % length;
        pattern_step_ = (uint8_t)((step + 1u) % length);
        sequencer_brightness_offset_ = kSandstormBrightness[step];

        const uint8_t note = pattern_note_[step];
        if (note == kPatternRest) {
            sequencer_gate_ = false;
            sequencer_accent_ = false;
            return;
        }

        sequencer_note_ = note;
        sequencer_accent_ = (pattern_accent_mask_ & (1u << step)) != 0;
        sequencer_gate_ = true;
        ++sequencer_trigger_;
        uint64_t gate = (sequencer_step_period_us_ * pattern_gate_[step]) / 100ull;
        if (gate < 10000ull) {
            gate = 10000ull;
        }
        sequencer_gate_off_us_ = now + gate;
    }

    bool midi_clock_is_active(uint32_t now_us) const
    {
        return midi_clock_seen_ &&
            static_cast<uint32_t>(now_us - midi_clock_last_us_) < kMidiClockTimeoutUs;
    }

    int32_t midi_cc_to_control(uint8_t value) const
    {
        return ((int32_t)value * 4095) / 127;
    }

    int32_t midi_note_pitch_units(uint8_t note) const
    {
        return ((int32_t)note - kBaseMidiNote) * kPitchUnitsPerOctave / 12;
    }

    void apply_pending_midi_events()
    {
        if (pending_midi_note_on_) {
            pending_midi_note_on_ = false;
            midi_note_ = pending_midi_note_;
            midi_velocity_ = pending_midi_velocity_;
            midi_note_active_ = true;
            sync_supersaw_phases();
            transient_env_ = kTransientMax;
        }

        if (pending_midi_note_off_event_) {
            pending_midi_note_off_event_ = false;
            if (pending_midi_note_off_ == midi_note_) {
                midi_note_active_ = false;
            }
        }
    }

    void update_leds(bool gate)
    {
        LedOn(0, drone_mode_);
        LedBrightness(0, sequencer_mode_ ? 192 : 4095);
        LedBrightness(1, sequencer_mode_ ? ((pattern_step_ & 1u) ? 4095 : 512) : stereo_width_);
        LedOn(2, gate);
        LedBrightness(3, filter_coeff_);
        int32_t midi_status = usb_host_mode_ ? 2600 : 700;
        if (!usb_midi_connected_) {
            midi_status = usb_host_mode_ ? 1200 : 250;
        }
        if (midi_activity_countdown_ > 0) {
            midi_status = 4095;
        }
        LedBrightness(4, midi_status);
        LedBrightness(5, midi_note_active_ ? 4095 : envelope_);
    }
};

JP4KSandsquall card;
static volatile uint8_t host_midi_device_address = 0;

extern "C" void tuh_midi_mount_cb(
    uint8_t dev_addr,
    uint8_t in_ep,
    uint8_t out_ep,
    uint8_t num_cables_rx,
    uint16_t num_cables_tx)
{
    (void)in_ep;
    (void)out_ep;
    (void)num_cables_rx;
    (void)num_cables_tx;

    if (host_midi_device_address == 0) {
        host_midi_device_address = dev_addr;
    }
    card.SetUsbMidiConnected(true);
}

extern "C" void tuh_midi_umount_cb(uint8_t dev_addr, uint8_t instance)
{
    (void)instance;

    if (dev_addr == host_midi_device_address) {
        host_midi_device_address = 0;
        card.SetUsbMidiConnected(false);
    }
}

extern "C" void tuh_midi_rx_cb(uint8_t dev_addr, uint32_t num_packets)
{
    if (dev_addr != host_midi_device_address || num_packets == 0) {
        return;
    }

    uint8_t cable = 0;
    uint8_t bytes[128];
    while (true) {
        uint32_t count = tuh_midi_stream_read(dev_addr, &cable, bytes, sizeof(bytes));
        if (count == 0) {
            break;
        }

        for (uint32_t i = 0; i < count; ++i) {
            card.ProcessUsbMidiByte(bytes[i]);
        }
    }
}

extern "C" void tuh_midi_tx_cb(uint8_t dev_addr)
{
    (void)dev_addr;
}

extern "C" void tud_mount_cb(void)
{
    card.SetUsbMidiConnected(true);
}

extern "C" void tud_umount_cb(void)
{
    card.SetUsbMidiConnected(false);
}

void usb_midi_worker()
{
    sleep_ms(100);
    const bool host_mode = card.ShouldBootUsbHost();
    card.SetUsbHostMode(host_mode);

    if (host_mode) {
        tuh_init(0);
    } else {
        tud_init(0);
    }

    while (true) {
        if (host_mode) {
            tuh_task();
        } else {
            tud_task();
            card.SendPendingUsbMidiOutput();

            uint8_t bytes[64];
            uint32_t count = tud_midi_stream_read(bytes, sizeof(bytes));
            for (uint32_t i = 0; i < count; ++i) {
                card.ProcessUsbMidiByte(bytes[i]);
            }
        }

        static uint32_t control_divider = 0;
        if ((control_divider++ & kControlMask) == 0) {
            card.RefreshControls();
        }
        sleep_us(50);
    }
}

int main()
{
#ifdef JP4K_SANDSQUALL_OVERCLOCK_240
    vreg_set_voltage(VREG_VOLTAGE_1_20);
    sleep_ms(10);
    set_sys_clock_khz(240000, true);
#else
    set_sys_clock_khz(192000, true);
#endif

    multicore_launch_core1(usb_midi_worker);
    card.EnableNormalisationProbe();
    card.Run();
}
