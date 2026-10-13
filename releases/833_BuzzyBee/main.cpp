#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "pico/multicore.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "tusb.h"

#include <cstdlib>
#include <cstring>

#include "ComputerCard.h"
#include "common.h"
#include "tanh_table.h"
#include "sintab.h"
#include "buzzrito_dsp.h"
#include "preset_store.h"
#include "usb_editor.h"

static volatile bool usb_editor_mode = false;
static volatile bool editor_save_requested = false;
static volatile bool editor_core0_parked = false;

class WorkshopBuzzrito : public ComputerCard
{
public:
    WorkshopBuzzrito()
    {
        std::memcpy(bp.presets, default_presets, sizeof(default_presets));
        preset_store_load(bp.presets);
        for (int i = 0; i < kNumSaws; ++i)
        {
            saw_wobble_pink_[i].seed = i + 1;
        }
    }

    void __not_in_flash_func(ProcessSample)() override
    {
        // Editor mode deliberately parks the renderer before TinyUSB begins.
        // A normal boot never sets this flag, so its performance path is the
        // same stable renderer as the non-USB firmware.
        if (usb_editor_mode)
        {
            if (SwitchVal() == Switch::Down)
            {
                if (editor_save_hold_samples_ < kEditorSaveHoldSamples)
                {
                    editor_save_hold_samples_++;
                }
                if (editor_save_hold_samples_ == kEditorSaveHoldSamples)
                {
                    editor_save_requested = true;
                }
            }
            else
            {
                editor_save_hold_samples_ = 0;
            }

            AudioOut1(0);
            AudioOut2(0);
            PulseOut1(false);
            PulseOut2(false);

            if (editor_save_requested)
            {
                // Do not return to ComputerCard's flash-resident callback
                // chain while core 1 erases/programs the preset sector.
                save_and_disable_interrupts();
                editor_core0_parked = true;
                while (true)
                {
                    tight_loop_contents();
                }
            }
            return;
        }
        update_controls();
        render_stable_swarm();
        AudioOut1(frame_[0] >> 4);
        AudioOut2(frame_[1] >> 4);
    }

    static bool EditorBootRequested()
    {
        WorkshopBuzzrito *card = static_cast<WorkshopBuzzrito *>(ThisPtr());
        return card != nullptr && card->SwitchVal() == Switch::Up;
    }

private:
    static constexpr int kNumSaws = 16;
    static constexpr int32_t kEditorSaveHoldSamples = SAMPLE_FREQ;
    static constexpr bool kInvertXKnob = false;
    static constexpr bool kInvertYKnob = true;
    static constexpr int32_t kSwitchHoldSamples = SAMPLE_FREQ / 4;
    static constexpr int32_t kMotionControlPeriod = 48;
    static constexpr int32_t kMotionPointDivider = 8;
    static constexpr int32_t kMotionMaxPoints = 256;
    static constexpr int32_t kMotionPingPongThreshold = 1365;
    static constexpr int32_t kOriginalBlockSize = 64;
    static constexpr int32_t kChordPitchHistorySize = 16;
    static constexpr int32_t kMiddleCOffsetQ19 =
        static_cast<int32_t>(23.4806373824f * (1 << 19));
    // C1ZZL3's hardware-tested AudioIn1 calibration: 341 signed ADC counts
    // represent one volt. Use Q12 multiplication instead of a division in
    // the audio callback; 341 counts maps exactly to 1000 mV.
    static constexpr int32_t kPitchInputCountsPerVolt = 341;
    static constexpr int32_t kPitchInputMvPerCountQ12 =
        (1000 * 4096 + (kPitchInputCountsPerVolt / 2)) / kPitchInputCountsPerVolt;
    struct MotionPoint
    {
        int16_t x;
        int16_t y;
    };
    struct PinkNoise
    {
        uint32_t seed = 0;
        uint32_t step = 0;
        int32_t sum = 0;
        int16_t values[4] = {0};
    };
    struct InterpNoise
    {
        int32_t values[4] = {0};
        uint32_t time = 0;
    };
    int16_t frame_[2] = {0};
    uint32_t saw_phase_[kNumSaws] = {0};
    uint32_t sub_phase_ = 0;
    uint32_t saw_delta_[kNumSaws] = {0};
    int32_t saw_ddelta_[kNumSaws] = {0};
    uint32_t sub_delta_ = 0;
    int32_t sub_ddelta_ = 0;
    int32_t saw_level_ = 2048;
    int32_t sub_level_ = 1024;
    int32_t noise_level_ = 0;
    int32_t comb_depth_ = 0;
    int32_t comb_mul_ = 7 * 256;
    int32_t delay_time_q8_ = 16 * 256;
    int32_t delay_l_ = 0;
    int32_t delay_r_ = 0;
    int32_t l_dc_ = 0;
    int32_t r_dc_ = 0;
    int32_t pitch_update_counter_ = 0;
    int32_t block_pitch_log_q19_ = 0;
    int32_t block_spread_ = 0;
    int32_t block_wobble_amount_ = 0;
    int32_t block_wobble_speed_ = 0;
    int32_t saw_glide_shape_[kNumSaws] = {0};
    PinkNoise boc_pink_;
    InterpNoise boc_noise_;
    int32_t boc_wobble_q19_ = 0;
    PinkNoise saw_wobble_pink_[kNumSaws];
    InterpNoise saw_wobble_noise_[kNumSaws];
    PinkNoise audio_pink_l_ = {23, 0, 0, {0}};
    PinkNoise audio_pink_r_ = {72, 0, 0, {0}};
    int32_t gate_q16_ = 65535;
    int32_t pitch_mv_ = 1000;
    int32_t gate_level_ = 65535;
    int32_t led_counter_ = 0;
    int32_t pad_x_smooth_ = 0;
    int32_t pad_y_smooth_ = 0;
    int32_t switch_down_samples_ = 0;
    int32_t editor_save_hold_samples_ = 0;
    int chord_mode_ = 4;
    int32_t chord_pitch_history_[kChordPitchHistorySize] = {0};
    int32_t chord_note_history_[4] = {0};
    int32_t chord_notes_sorted_[4] = {0};
    int32_t block_chord_notes_[4] = {0};
    int32_t chord_note_count_ = 1;
    int32_t block_chord_note_count_ = 1;
    int32_t chord_pitch_history_index_ = 0;
    bool chord_pitch_initialized_ = false;
    MotionPoint motion_[kMotionMaxPoints] = {};
    int32_t motion_sample_counter_ = 0;
    int32_t motion_record_divider_ = 0;
    int32_t motion_play_substep_ = 0;
    int32_t motion_length_ = 0;
    int32_t motion_play_position_ = 0;
    int32_t motion_x_ = 0;
    int32_t motion_y_ = 0;
    bool motion_recording_ = false;
    bool motion_playing_ = false;
    bool motion_pingpong_ = false;
    bool motion_play_reverse_ = false;
    bool pulse2_high_ = false;

    void __not_in_flash_func(update_controls)()
    {
        const int32_t main = KnobVal(Knob::Main);
        const int32_t x = KnobVal(Knob::X);
        const int32_t y = KnobVal(Knob::Y);

        // Original Buzzrito drones at 1000 mV when no pitch jack is patched.
        // Put that original base note near noon so Main has range below/above.
        int32_t pitch_mv = -1500 + ((main * 5000) >> 12);
        if (Connected(Input::Audio1))
        {
            // Audio/CV In 1 is signed ADC counts, while the original
            // Buzzrito pitch renderer expects millivolts.
            pitch_mv += (AudioIn1() * kPitchInputMvPerCountQ12) >> 12;
        }
        pitch_mv_ += make_lpf_delta(pitch_mv, pitch_mv_, 6);

        // X/Y knobs are raw pot readings; translate them into the original
        // Buzzrito pad coordinate space before applying CV pad modulation.
        const bool cv1_connected = Connected(Input::CV1);
        const bool cv2_connected = Connected(Input::CV2);
        int32_t raw_x = knob_to_pad(x, kInvertXKnob);
        int32_t raw_y = knob_to_pad(y, kInvertYKnob);
        if (cv1_connected)
        {
            raw_x += CVIn1() * 2;
        }
        if (cv2_connected)
        {
            raw_y += CVIn2() * 2;
        }
        raw_x = clampi(raw_x, -4096, 4095);
        raw_y = clampi(raw_y, -4096, 4095);

        // The original capacitive pad has a tapered horizontal range towards
        // its top and bottom edges. Warp the two independent controls into
        // that shape so either knob continues to affect the morph at its ends.
        const int32_t pad_y = raw_y;
        const int32_t pad_x = (raw_x * (8192 - abs(raw_y))) >> 13;
        pad_x_smooth_ += make_lpf_delta(pad_x, pad_x_smooth_, 4);
        pad_y_smooth_ += make_lpf_delta(pad_y, pad_y_smooth_, 4);

        const bool pulse2_high = Connected(Input::Pulse2) && PulseIn2();
        const bool pulse2_rising = pulse2_high && !pulse2_high_;
        pulse2_high_ = pulse2_high;
        if (pulse2_rising && motion_playing_)
        {
            // Pulse2 is an explicit return to live X/Y. Keep the recorded
            // path in RAM; a subsequent Switch-Up recording replaces it.
            motion_playing_ = false;
            motion_play_position_ = 0;
            motion_play_substep_ = 0;
            motion_play_reverse_ = false;
            motion_x_ = pad_x_smooth_;
            motion_y_ = pad_y_smooth_;
        }

        update_switch();
        update_motion(pad_x_smooth_, pad_y_smooth_);
        // A patched CV axis takes priority over its saved axis. This keeps an
        // external CV source immediately useful while the other axis can
        // continue playing the recorded path.
        const int32_t sound_pad_x = (motion_playing_ && !cv1_connected) ? motion_x_ : pad_x_smooth_;
        const int32_t sound_pad_y = (motion_playing_ && !cv2_connected) ? motion_y_ : pad_y_smooth_;

        const bool pulse1_connected = Connected(Input::Pulse1);
        const bool switch_held = switch_down_samples_ >= kSwitchHoldSamples;
        int32_t gate_q16 = 65535;
        if (switch_held)
        {
            gate_q16 = pulse1_connected ? 65535 : 0;
            gate_level_ = gate_q16;
        }
        else if (pulse1_connected)
        {
            const int32_t target = PulseIn1() ? 65535 : 0;
            gate_level_ += make_lpf_delta(target, gate_level_, 7);
            gate_q16 = gate_level_;
        }
        else
        {
            gate_level_ = 65535;
        }
        gate_q16_ = gate_q16;

        buzzypreset preset = buzzy_xyinterpolate(sound_pad_x, sound_pad_y);
        const int32_t saw_target = (preset.saw_level * preset.saw_level) >> 12;
        const int32_t raw_sub_target = (preset.sub_level * preset.sub_level) >> 12;
        // Edge presets on the original pad can intentionally silence the saw.
        // With pots that makes a broad, easy-to-hit sub-only area, so retain
        // enough saw presence for X and Y morphing to remain audible.
        const int32_t sub_target = mini(raw_sub_target, maxi(1024, saw_target + 1024));
        saw_level_ += make_lpf_delta(saw_target, saw_level_, 5);
        sub_level_ += make_lpf_delta(sub_target, sub_level_, 5);
        // This calibration uses one quarter of the original mix range. A zero
        // source value takes a complete bypass.
        noise_level_ = clampi(preset.noise_level, 0, 4096) >> 2;
        update_comb(preset.comb_depth, preset.comb_mul, pitch_mv_, boc_wobble_q19_);
        update_pitch_deltas(pitch_mv_, preset.spread, preset.glide, preset.boc_amount,
                            preset.wobble_amount, preset.wobble_speed);

        PulseOut1(gate_q16 > 32768);
        CVOut1(clamp12(pitch_mv_ / 3));

        update_leds(sound_pad_x, sound_pad_y, gate_q16);
    }

    void __not_in_flash_func(update_switch)()
    {
        if (SwitchVal() == Switch::Down)
        {
            if (switch_down_samples_ < SAMPLE_FREQ)
            {
                switch_down_samples_++;
            }
            return;
        }

        if (switch_down_samples_ > 0 && switch_down_samples_ < kSwitchHoldSamples)
        {
            chord_mode_++;
            if (chord_mode_ > 4)
            {
                chord_mode_ = 1;
            }
        }
        switch_down_samples_ = 0;
    }

    void __not_in_flash_func(update_motion)(int32_t live_x, int32_t live_y)
    {
        // Keep recorder work off the per-sample path. Control values remain
        // sampled inside ProcessSample(), as required by ComputerCard.
        motion_sample_counter_++;
        if (motion_sample_counter_ < kMotionControlPeriod)
        {
            return;
        }
        motion_sample_counter_ = 0;

        if (SwitchVal() == Switch::Up)
        {
            if (!motion_recording_)
            {
                motion_recording_ = true;
                motion_playing_ = false;
                motion_length_ = 0;
                motion_record_divider_ = 0;
                motion_x_ = live_x;
                motion_y_ = live_y;
            }

            motion_record_divider_++;
            if (motion_record_divider_ < kMotionPointDivider)
            {
                return;
            }
            motion_record_divider_ = 0;

            if (motion_length_ < kMotionMaxPoints)
            {
                motion_[motion_length_].x = static_cast<int16_t>(live_x);
                motion_[motion_length_].y = static_cast<int16_t>(live_y);
                motion_length_++;
            }
            return;
        }

        if (motion_recording_)
        {
            motion_recording_ = false;
            // A brief, stationary Up gesture is a one-point recording: the
            // Workshop equivalent of holding one place on the original pad.
            if (motion_length_ == 0)
            {
                motion_[0].x = static_cast<int16_t>(live_x);
                motion_[0].y = static_cast<int16_t>(live_y);
                motion_length_ = 1;
            }
            motion_x_ = motion_[0].x;
            motion_y_ = motion_[0].y;
            motion_play_position_ = 0;
            motion_play_substep_ = 0;
            const MotionPoint end = motion_[motion_length_ - 1];
            motion_pingpong_ = abs(motion_[0].x - end.x) + abs(motion_[0].y - end.y) > kMotionPingPongThreshold;
            motion_play_reverse_ = false;
            motion_playing_ = true;
        }

        if (!motion_playing_)
        {
            return;
        }

        int32_t next_position = motion_play_position_;
        if (motion_play_reverse_)
        {
            if (motion_play_position_ > 0)
            {
                next_position = motion_play_position_ - 1;
            }
            else if (!motion_pingpong_)
            {
                next_position = motion_length_ - 1;
            }
        }
        else if (motion_play_position_ + 1 < motion_length_)
        {
            next_position = motion_play_position_ + 1;
        }
        else if (!motion_pingpong_)
        {
            next_position = 0;
        }
        const int32_t current_x = motion_[motion_play_position_].x;
        const int32_t current_y = motion_[motion_play_position_].y;
        const int32_t target_x = current_x + (((motion_[next_position].x - current_x) * motion_play_substep_) >> 3);
        const int32_t target_y = current_y + (((motion_[next_position].y - current_y) * motion_play_substep_) >> 3);
        motion_x_ += (target_x - motion_x_) >> 2;
        motion_y_ += (target_y - motion_y_) >> 2;

        motion_play_substep_++;
        if (motion_play_substep_ >= kMotionPointDivider)
        {
            motion_play_substep_ = 0;
            if (motion_play_reverse_)
            {
                if (motion_play_position_ == 0)
                {
                    motion_play_reverse_ = false;
                }
                else
                {
                    motion_play_position_--;
                }
            }
            else if (motion_play_position_ + 1 >= motion_length_)
            {
                if (motion_pingpong_)
                {
                    motion_play_reverse_ = true;
                }
                else
                {
                    motion_play_position_ = 0;
                }
            }
            else
            {
                motion_play_position_++;
            }
        }
    }

    static int32_t __not_in_flash_func(update_pinknoise)(PinkNoise &noise)
    {
        const uint32_t random = noise.seed = noise.seed * 0x0019660d + 0x3c6ef35f;
        const uint32_t index = __builtin_ctz((noise.step++) | 8);
        const int16_t value = static_cast<int16_t>(random >> 16);
        noise.sum -= noise.values[index];
        noise.values[index] = value;
        return noise.sum += value;
    }

    static int32_t __not_in_flash_func(update_interp_noise)(InterpNoise &noise, PinkNoise &pink,
                                                              int32_t speed)
    {
        noise.time += speed;
        while (noise.time >= 65536)
        {
            noise.time -= 65536;
            noise.values[0] = noise.values[1];
            noise.values[1] = noise.values[2];
            noise.values[2] = noise.values[3];
            const int32_t value = update_pinknoise(pink) >> 3;
            noise.values[3] += (value - noise.values[3]) >> 2;
        }
        const int32_t a0 = (-noise.values[0] + 3 * noise.values[1] - 3 * noise.values[2] + noise.values[3]) >> 1;
        const int32_t a1 = (2 * noise.values[0] - 5 * noise.values[1] + 4 * noise.values[2] - noise.values[3]) >> 1;
        const int32_t a2 = (noise.values[2] - noise.values[0]) >> 1;
        const int32_t time = noise.time >> 2;
        return (((((a0 * time >> 14) + a1) * time >> 14) + a2) * time >> 14) + noise.values[1];
    }

    void __not_in_flash_func(update_pitch_deltas)(int32_t pitch_mv, int32_t spread, int32_t glide,
                                                   int32_t boc_amount, int32_t wobble_amount,
                                                   int32_t wobble_speed)
    {
        // Preserve the original 64-sample modulation rate, but distribute
        // the 16 per-saw target calculations across the block. This prevents
        // one audio callback from carrying every interpolation update.
        if (pitch_update_counter_ == 0)
        {
            block_wobble_speed_ = clampi(wobble_speed, 0, 4096);
            block_wobble_amount_ = clampi(wobble_amount, 0, 4096);
            boc_wobble_q19_ = (update_interp_noise(boc_noise_, boc_pink_, block_wobble_speed_) *
                                clampi(boc_amount, 0, 4096)) >> 10;
            update_chord_note_memory(pitch_mv);
            block_chord_note_count_ = chord_note_count_;
            std::memcpy(block_chord_notes_, chord_notes_sorted_, sizeof(block_chord_notes_));
            block_pitch_log_q19_ = kMiddleCOffsetQ19 + pitch_to_log_q19(block_chord_notes_[0]);
            block_spread_ = spread;

            const uint32_t sub_target = exp2_table(block_pitch_log_q19_ + boc_wobble_q19_ - (1 << 19));
            int32_t glide_shape = 4096 - clampi(glide, 0, 4096);
            glide_shape = (glide_shape * glide_shape) >> 12;
            glide_shape = (glide_shape * glide_shape) >> 13;
            sub_ddelta_ = calc_ddelta_t(static_cast<int32_t>(sub_delta_),
                                        static_cast<int32_t>(sub_target), glide_shape);
            for (int i = 0; i < kNumSaws; ++i)
            {
                saw_glide_shape_[i] = glide_shape;
                glide_shape = (glide_shape * 255) >> 8;
            }
        }

        if ((pitch_update_counter_ & 3) == 0)
        {
            const int32_t i = pitch_update_counter_ >> 2;
            const int32_t chord_pitch_mv = block_chord_notes_[i % block_chord_note_count_];
            const int32_t chord_log_q19 = kMiddleCOffsetQ19 + pitch_to_log_q19(chord_pitch_mv) +
                                          (boc_wobble_q19_ >> 1);
            const int32_t detune = (i - (kNumSaws / 2)) * block_spread_;
            const int32_t wobble = (update_interp_noise(saw_wobble_noise_[i], saw_wobble_pink_[i],
                                                         block_wobble_speed_) * block_wobble_amount_) >> 9;
            const uint32_t target = exp2_table(chord_log_q19 + detune + wobble);
            saw_ddelta_[i] = calc_ddelta_t(static_cast<int32_t>(saw_delta_[i]),
                                            static_cast<int32_t>(target), saw_glide_shape_[i]);
        }

        pitch_update_counter_++;
        if (pitch_update_counter_ == kOriginalBlockSize)
        {
            pitch_update_counter_ = 0;
        }
    }

    static int32_t pitch_to_log_q19(int32_t pitch_mv)
    {
        return (pitch_mv << 17) / 250;
    }

    void update_chord_note_memory(int32_t pitch_mv)
    {
        const int32_t mode = clampi(chord_mode_, 1, 4);
        if (!chord_pitch_initialized_)
        {
            for (int32_t &history_pitch : chord_pitch_history_)
            {
                history_pitch = pitch_mv;
            }
            chord_pitch_initialized_ = true;
        }

        chord_pitch_history_[chord_pitch_history_index_] = pitch_mv;
        chord_pitch_history_index_ = (chord_pitch_history_index_ + 1) & (kChordPitchHistorySize - 1);

        if (mode == 1)
        {
            chord_note_count_ = 1;
            chord_note_history_[0] = pitch_mv;
            chord_notes_sorted_[0] = pitch_mv;
            return;
        }

        int32_t minimum = chord_pitch_history_[0];
        int32_t maximum = minimum;
        int32_t total = minimum;
        for (int32_t i = 1; i < kChordPitchHistorySize; ++i)
        {
            const int32_t candidate = chord_pitch_history_[i];
            minimum = mini(minimum, candidate);
            maximum = maxi(maximum, candidate);
            total += candidate;
        }

        // This is the original Buzzrito's 16-block, 30 mV note detector.
        // It lets a performer teach a chord by holding successive pitches.
        if (maximum - minimum < 30)
        {
            const int32_t average = total / kChordPitchHistorySize;
            if (chord_note_count_ == 0 || abs(average - chord_note_history_[0]) > 30)
            {
                int32_t updated_history[4] = {average};
                int32_t updated_count = 1;
                for (int32_t i = 0; i < chord_note_count_ && updated_count < mode; ++i)
                {
                    updated_history[updated_count++] = chord_note_history_[i];
                }
                std::memcpy(chord_note_history_, updated_history, sizeof(chord_note_history_));
                chord_note_count_ = updated_count;
            }
            else
            {
                chord_note_history_[0] = average;
            }

            std::memcpy(chord_notes_sorted_, chord_note_history_, sizeof(chord_notes_sorted_));
            for (int32_t i = 0; i < chord_note_count_; ++i)
            {
                for (int32_t j = i + 1; j < chord_note_count_; ++j)
                {
                    if (chord_notes_sorted_[i] > chord_notes_sorted_[j])
                    {
                        const int32_t swap = chord_notes_sorted_[i];
                        chord_notes_sorted_[i] = chord_notes_sorted_[j];
                        chord_notes_sorted_[j] = swap;
                    }
                }
            }
        }
    }

    void update_comb(int32_t source_depth, int32_t source_mul, int32_t pitch_mv, int32_t boc_wobble_q19)
    {
        const bool negative = source_depth < 0;
        int32_t target_depth = 4096 - abs(source_depth);
        target_depth = (target_depth * target_depth) >> 12;
        target_depth = 4096 - target_depth;
        if (negative) target_depth = -target_depth;

        comb_depth_ += make_lpf_delta(target_depth, comb_depth_, 10);
        comb_mul_ += make_lpf_delta(source_mul, comb_mul_, 10);

        static constexpr int32_t kMiddleCOffsetQ19 = static_cast<int32_t>(23.4806373824f * (1 << 19));
        const int32_t pitch_log_q19 = kMiddleCOffsetQ19 + pitch_to_log_q19(pitch_mv) + boc_wobble_q19;
        const int32_t comb_shift = comb_mul_ * ((1 << 11) / 12);
        int32_t target_delay_q8 = exp2_table((40 << 19) - pitch_log_q19 - comb_shift);
        while (target_delay_q8 > 2046 * 256) target_delay_q8 >>= 1;
        delay_time_q8_ += make_lpf_delta(target_delay_q8, delay_time_q8_, 10);
    }

    void __not_in_flash_func(render_stable_swarm)()
    {
        int32_t l_samp = 0;
        int32_t r_samp = 0;

        for (int i = 0; i < kNumSaws; ++i)
        {
            saw_delta_[i] = static_cast<uint32_t>(static_cast<int32_t>(saw_delta_[i]) + saw_ddelta_[i]);
            saw_phase_[i] += saw_delta_[i];
            const int32_t saw = (static_cast<int32_t>(saw_phase_[i] >> 16) - 32768) << 2;
            if (i & 1)
            {
                r_samp += saw;
            }
            else
            {
                l_samp += saw;
            }
        }

        l_samp = ((l_samp >> 3) * saw_level_) >> 14;
        r_samp = ((r_samp >> 3) * saw_level_) >> 14;

        sub_delta_ = static_cast<uint32_t>(static_cast<int32_t>(sub_delta_) + sub_ddelta_);
        sub_phase_ += sub_delta_;
        int32_t sub = static_cast<int32_t>(sub_phase_ >> 16);
        sub = (sub < 32768) ? sub : 65535 - sub;
        sub = (sub - 16384) << 3;
        sub = (sub * sub_level_) >> 14;
        l_samp += sub;
        r_samp += sub;

        // Original Buzzrito mixes stereo pink noise before the DC cleanup and
        // comb path. Keep it audio-rate and independent of BOC's 750 Hz state.
        if (noise_level_ > 0)
        {
            const int32_t noise_l = update_pinknoise(audio_pink_l_);
            const int32_t noise_r = update_pinknoise(audio_pink_r_);
            l_samp += (noise_l * noise_level_) >> 15;
            r_samp += (noise_r * noise_level_) >> 15;
        }

        // The original Buzzrito's character is dominated by this tuned,
        // signed-feedback comb. It is retained here without its motion/noise
        // generators so pad regions remain distinct but stationary.
        l_dc_ += make_lpf_delta(l_samp << 8, l_dc_, 8);
        r_dc_ += make_lpf_delta(r_samp << 8, r_dc_, 8);
        l_samp -= l_dc_ >> 8;
        r_samp -= r_dc_ >> 8;

        int32_t read_pos = delay_pos - (delay_time_q8_ >> 8);
        const int32_t delay_l0 = delay_buf_l[read_pos & 2047];
        const int32_t delay_r0 = delay_buf_r[read_pos & 2047];
        read_pos--;
        const int32_t delay_l1 = delay_buf_l[read_pos & 2047];
        const int32_t delay_r1 = delay_buf_r[read_pos & 2047];
        const int32_t fraction = delay_time_q8_ & 255;
        const int32_t delay_l_raw = delay_l0 + (((delay_l1 - delay_l0) * fraction) >> 8);
        const int32_t delay_r_raw = delay_r0 + (((delay_r1 - delay_r0) * fraction) >> 8);
        delay_l_ += make_lpf_delta(delay_l_raw, delay_l_, 1);
        delay_r_ += make_lpf_delta(delay_r_raw, delay_r_, 1);
        l_samp += (delay_l_ * comb_depth_) >> 12;
        r_samp += (delay_r_ * comb_depth_) >> 12;

        l_samp = soft_clip(l_samp);
        r_samp = soft_clip(r_samp);
        delay_buf_l[delay_pos] = static_cast<int16_t>(l_samp);
        delay_buf_r[delay_pos] = static_cast<int16_t>(r_samp);
        delay_pos = (delay_pos + 1) & 2047;

        const int32_t gate_mul = static_cast<int32_t>((static_cast<int64_t>(gate_q16_) * gate_q16_) >> 16);
        l_samp = static_cast<int32_t>((static_cast<int64_t>(l_samp) * gate_mul) >> 16);
        r_samp = static_cast<int32_t>((static_cast<int64_t>(r_samp) * gate_mul) >> 16);

        frame_[0] = clamp16(l_samp);
        frame_[1] = clamp16(r_samp);
    }

    static int16_t clamp12(int32_t value)
    {
        if (value > 2047) return 2047;
        if (value < -2048) return -2048;
        return static_cast<int16_t>(value);
    }

    static int32_t knob_to_pad(int32_t knob, bool invert)
    {
        int32_t pad = ((knob - 2048) * 4096) >> 11;
        return invert ? -pad : pad;
    }

    static int16_t clamp16(int32_t value)
    {
        if (value > 32767) return 32767;
        if (value < -32768) return -32768;
        return static_cast<int16_t>(value);
    }

    void update_leds(int32_t pad_x, int32_t pad_y, int32_t gate_q16)
    {
        led_counter_++;
        if (led_counter_ < 480) return;
        led_counter_ = 0;

        // The first four LEDs are the physical pad corners: top-left,
        // top-right, bottom-left, bottom-right. The physical Y orientation
        // is opposite the virtual sound coordinate, while X is shared.
        const int32_t top_left = clampi((-pad_x - pad_y) >> 1, 0, 4095);
        const int32_t top_right = clampi((pad_x - pad_y) >> 1, 0, 4095);
        const int32_t bottom_left = clampi((-pad_x + pad_y) >> 1, 0, 4095);
        const int32_t bottom_right = clampi((pad_x + pad_y) >> 1, 0, 4095);
        LedBrightness(0, top_left);
        LedBrightness(1, top_right);
        LedBrightness(2, bottom_left);
        LedBrightness(3, bottom_right);
        LedBrightness(4, gate_q16 >> 4);
        LedBrightness(5, motion_recording_ ? 4095 : clampi(chord_mode_ * 1024, 0, 4095));
    }
};

static void usb_midi_worker()
{
    // USB is modal. Switch Up is latched, so holding it while powering the
    // card is an unambiguous editor-mode request without changing normal
    // Switch-Up gesture recording after boot.
    sleep_ms(1000);
    if (!WorkshopBuzzrito::EditorBootRequested())
    {
        // A busy idle loop still competes with the audio core for shared XIP
        // bandwidth. Park core 1 in hardware for the entire normal boot.
        while (true)
        {
            multicore_fifo_pop_blocking();
        }
    }

    usb_editor_mode = true;
    usb_editor_set_presets(bp.presets);
    tud_init(0);
    while (true)
    {
        tud_task();
        usb_editor_service();
        if (editor_save_requested)
        {
            while (!editor_core0_parked)
            {
                tight_loop_contents();
            }

            buzzypreset presets[7] = {};
            usb_editor_get_presets(presets);
            preset_store_save(presets);
            watchdog_reboot(0, 0, 0);
            while (true)
            {
                tight_loop_contents();
            }
        }
    }
}

int main()
{
    set_sys_clock_khz(192000, true);
    multicore_launch_core1(usb_midi_worker);

    WorkshopBuzzrito card;
    card.EnableNormalisationProbe();
    card.Run();
}
