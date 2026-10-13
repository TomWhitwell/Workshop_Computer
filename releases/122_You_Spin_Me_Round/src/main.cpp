// Copyright (c) 2026 soveda. SPDX-License-Identifier: MIT
// Hardware setup follows Chris Johnson's ComputerCard examples.
#include "ComputerCard.h"
#include "hardware/clocks.h"
#include "hardware/timer.h"
#include "controls.h"

class Card : public ComputerCard {
    void ProcessSample() override {
        const uint32_t started = time_us_32();
        // Hardware inputs are sampled only here, on the audio interrupt core.
        // Control calculations run once per millisecond, not 48,000 times/sec.
        if (++control_count_ == 48) {
            control_count_ = 0;
            spin::Inputs in;
            in.main = KnobVal(Knob::Main);
            in.x = KnobVal(Knob::X);
            in.y = KnobVal(Knob::Y);
            in.cv1 = Connected(Input::CV1) ? CVIn1() : 0;
            in.cv2 = Connected(Input::CV2) ? CVIn2() : 0;
            const auto sw = SwitchVal();
            in.sw = sw == Switch::Down ? spin::SwitchPosition::Down
                  : sw == Switch::Up ? spin::SwitchPosition::Up : spin::SwitchPosition::Middle;
            in.speed_gate_connected = Connected(Input::Pulse1);
            in.speed_gate = PulseIn1();
            in.brake_gate = Connected(Input::Pulse2) && PulseIn2();
            controls_.Tick(in);
            if (controls_.CurrentStage() == spin::Stage::Playing)
                rotary_.SetParameters(controls_.Settings());
            UpdateLeds();
            CVOut1(0);
            CVOut2(0);
            PulseOut1(false);
            PulseOut2(false);
        }
        if (controls_.CurrentStage() == spin::Stage::Playing) {
            const spin::Stereo out = rotary_.Process(AudioIn1());
            // A 20 ms startup fade avoids a click when leaving boot selection.
            if (fade_ < 960) ++fade_;
            AudioOut1(static_cast<int16_t>((out.left * fade_) / 960));
            AudioOut2(static_cast<int16_t>((out.right * fade_) / 960));
        } else {
            AudioOut1(0);
            AudioOut2(0);
        }
        // A latched indicator is useful during hardware validation; this
        // measures our callback, not ComputerCard's surrounding interrupt work.
        const uint32_t elapsed = time_us_32() - started;
        if (elapsed > max_callback_us_) max_callback_us_ = elapsed;
        if (elapsed >= 18) timing_warning_ = true;
    }
    void UpdateLeds() {
        if (controls_.CurrentStage() != spin::Stage::Playing) {
            const bool selecting = controls_.CurrentStage() == spin::Stage::Selecting;
            const bool inside = controls_.CurrentPerspective() == spin::Perspective::Inside;
            for (int32_t row = 0; row < 3; ++row) {
                LedOn(row * 2, selecting && !inside);
                LedOn(row * 2 + 1, selecting && inside);
            }
            return;
        }
        // Top pair: horn and drum motion. Middle pair: range and brake.
        LedBrightness(0, rotary_.HornPosition());
        LedBrightness(1, rotary_.DrumPosition());
        LedOn(2, controls_.Settings().fast);
        LedOn(3, controls_.Settings().brake);
        LedOn(4, controls_.CurrentPerspective() == spin::Perspective::Inside);
        LedOn(5, timing_warning_);
    }
    spin::Controls controls_;
    spin::Rotary rotary_;
    int32_t control_count_ = 0, fade_ = 0;
    volatile uint32_t max_callback_us_ = 0;
    bool timing_warning_ = false;
};

int main() {
    set_sys_clock_khz(144000, true);
    // Delay buffers belong in static RAM, not on the small core-0 stack.
    static Card card;
    card.EnableNormalisationProbe();
    card.Run();
}
