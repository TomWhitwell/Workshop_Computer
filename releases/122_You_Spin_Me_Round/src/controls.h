// Copyright (c) 2026 soveda. SPDX-License-Identifier: MIT
#pragma once
#include "dsp/rotary.h"
namespace spin {
enum class SwitchPosition { Down, Middle, Up };
enum class Stage { Settling, Selecting, Playing };
struct Inputs {
    int32_t main = 2048, x = 2048, y = 0, cv1 = 0, cv2 = 0;
    SwitchPosition sw = SwitchPosition::Middle;
    bool speed_gate_connected = false, speed_gate = false, brake_gate = false;
};
class Controls {
public:
    // 1 kHz control scan: 100 ms startup settling and 5 ms switch debounce.
    void Tick(const Inputs& in) {
        if (in.sw != candidate_) { candidate_ = in.sw; stable_ = 0; }
        if (stable_ < 5) ++stable_;
        if (stable_ >= 5) sw_ = candidate_;
        if (stage_ == Stage::Settling) {
            if (++startup_ < 100) return;
            if (sw_ == SwitchPosition::Down) {
                stage_ = Stage::Selecting;
                perspective_ = in.main >= 2048 ? Perspective::Inside : Perspective::Outside;
            } else stage_ = Stage::Playing;
        } else if (stage_ == Stage::Selecting) {
            if (in.main < 1920) perspective_ = Perspective::Outside;
            if (in.main > 2176) perspective_ = Perspective::Inside;
            if (sw_ != SwitchPosition::Down) {
                stage_ = Stage::Playing;
            }
        }
        if (sw_ == SwitchPosition::Up) fast_ = true;
        if (sw_ == SwitchPosition::Middle) fast_ = false;
        params_.speed = Clamp(in.main + in.cv1 * 2, 0, 4095);
        params_.intensity = Clamp(in.x + in.cv2 * 2, 0, 4095);
        params_.drive = Clamp(in.y, 0, 4095);
        params_.fast = in.speed_gate_connected ? in.speed_gate : fast_;
        // Either held source brakes the rotors. Releasing one source does
        // not release the other; boot selection is not a performance gesture.
        params_.brake = stage_ == Stage::Playing
                     && (sw_ == SwitchPosition::Down || in.brake_gate);
        params_.perspective = perspective_;
    }
    Stage CurrentStage() const { return stage_; }
    Perspective CurrentPerspective() const { return perspective_; }
    const Parameters& Settings() const { return params_; }
private:
    Stage stage_ = Stage::Settling;
    Perspective perspective_ = Perspective::Outside;
    SwitchPosition candidate_ = SwitchPosition::Middle, sw_ = SwitchPosition::Middle;
    int32_t stable_ = 0, startup_ = 0;
    bool fast_ = false;
    Parameters params_;
};
}
