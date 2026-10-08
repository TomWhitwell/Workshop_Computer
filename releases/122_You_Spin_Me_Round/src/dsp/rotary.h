// Copyright (c) 2026 soveda. SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include "sine_table.h"

namespace spin {
inline int32_t Clamp(int32_t x, int32_t lo, int32_t hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}
inline int32_t Sin(uint32_t phase) {
    const uint32_t i = phase >> 24;
    const int32_t f = (phase >> 16) & 255;
    return kSine[i] + ((kSine[(i + 1) & 255] - kSine[i]) * f >> 8);
}
// Unlike a truncated exponential, this arrives at its destination exactly.
inline int32_t Slew(int32_t current, int32_t target, int32_t divisor) {
    const int32_t d = target - current;
    return current + (d == 0 ? 0 : (d / divisor + (d > 0 ? 1 : -1)));
}
enum class Perspective { Outside, Inside };
struct Stereo { int32_t left; int32_t right; };
// Approximate a square-root response without floating point. In the old
// mapping both depth and stereo angle shrank with X, making stereo movement
// roughly quadratic. These anchors bring useful motion into the low knob range.
inline int32_t MovementDepth(int32_t intensity) {
    const int32_t x = Clamp(intensity, 0, 4095);
    if (x <= 256) return x * 4;
    if (x <= 1024) return 1024 + ((x - 256) * 1024) / 768;
    if (x <= 2304) return 2048 + ((x - 1024) * 1024) / 1280;
    return 3072 + ((x - 2304) * 1023) / 1791;
}
struct Parameters {
    int32_t speed = 2048;
    int32_t intensity = 2048;
    int32_t drive = 0;
    bool fast = false;
    bool brake = false;
    Perspective perspective = Perspective::Outside;
};

class Delay {
public:
    void Write(int32_t sample) {
        data_[head_] = static_cast<int16_t>(Clamp(sample, -16384, 16383));
        head_ = (head_ + 1) & 511;
    }
    int32_t Read(int32_t samples_q8) const {
        const int32_t whole = samples_q8 >> 8;
        const int32_t fraction = samples_q8 & 255;
        const int32_t a = (head_ - 1 - whole) & 511;
        const int32_t b = (a - 1) & 511;
        return data_[a] + ((data_[b] - data_[a]) * fraction >> 8);
    }
private:
    int16_t data_[512] = {};
    int32_t head_ = 0;
};

class Rotary {
public:
    // Called at 1 kHz. Rates are millihertz, converted to 32-bit phase steps.
    // The switch selects a range; Main and CV position the speed within it.
    void SetParameters(const Parameters& p) {
        inside_ = p.perspective == Perspective::Inside;
        const int32_t speed = Clamp(p.speed, 0, 4095);
        const int32_t horn_mhz = p.brake ? 0 : (p.fast ? 4500 + ((speed * 4000) >> 12)
                                                       : 400 + ((speed * 1000) >> 12));
        const int32_t drum_mhz = p.brake ? 0 : (p.fast ? 3800 + ((speed * 3400) >> 12)
                                                       : 300 + ((speed * 800) >> 12));
        const int32_t horn_target = (horn_mhz * 89478) / 1000;
        const int32_t drum_target = (drum_mhz * 89478) / 1000;
        // Horn settles much faster than drum. Braking also coasts to rest.
        horn_step_ = Slew(horn_step_, horn_target, p.brake ? 600 : 350);
        drum_step_ = Slew(drum_step_, drum_target, p.brake ? 1800 : 1200);
        target_depth_ = MovementDepth(p.intensity);
        target_drive_ = Clamp(p.drive, 0, 4095);
    }

    Stereo Process(int32_t input) {
        depth_ = Slew(depth_, target_depth_, 512);
        drive_ = Slew(drive_, target_drive_, 512);
        horn_phase_ += static_cast<uint32_t>(horn_step_);
        drum_phase_ -= static_cast<uint32_t>(drum_step_);
        if ((geometry_count_++ & 31) == 0) UpdateGeometry();

        // A ~7.5 Hz DC blocker keeps offsets from pushing the cabinet drive.
        const int32_t in_q8 = Clamp(input, -2048, 2047) * 256;
        dc_ = in_q8 - previous_input_ + dc_ - (dc_ >> 10);
        previous_input_ = in_q8;
        int32_t driven = SoftClip(((dc_ >> 8) * (4096 + drive_ * 3)) >> 12);
        // Complementary first-order crossover at approximately 800 Hz.
        // Q8 state preserves quiet decays. Low + high equals the driven signal.
        // Widen only filter products: Q8 differences times Q15 coefficients
        // can exceed 32 bits for full-scale transients.
        low_ += static_cast<int32_t>((static_cast<int64_t>(driven * 256 - low_) * 3260) >> 15);
        const int32_t bass = low_ >> 8;
        horn_.Write(driven - bass);
        drum_.Write(bass);
        int32_t outputs[2];
        for (int32_t ear = 0; ear < 2; ++ear) {
            horn_delay_[ear] = Slew(horn_delay_[ear], horn_delay_target_[ear], 32);
            drum_delay_[ear] = Slew(drum_delay_[ear], drum_delay_target_[ear], 32);
            const int32_t treble = horn_.Read(horn_delay_[ear]);
            // The horn gets darker as its opening points away from this ear.
            shadow_[ear] += static_cast<int32_t>((static_cast<int64_t>(treble * 256 - shadow_[ear]) * tone_[ear]) >> 15);
            int32_t out = (((shadow_[ear] >> 8) * horn_gain_[ear]) >> 12)
                        + ((drum_.Read(drum_delay_[ear]) * drum_gain_[ear]) >> 12);
            // Short, feed-forward cabinet reflections; no feedback can run away.
            const int32_t reflection = horn_.Read((ear == 0 ? 193 : 307) * 256)
                                     + drum_.Read((ear == 0 ? 269 : 173) * 256);
            out += (reflection * depth_) >> (inside_ ? 15 : 17);
            // Leave headroom when the complementary bands move out of phase.
            outputs[ear] = Clamp((out * 3) >> 2, -2048, 2047);
        }
        return {outputs[0], outputs[1]};
    }
    int32_t HornStep() const { return horn_step_; }
    int32_t DrumStep() const { return drum_step_; }
    int32_t HornPosition() const { return (Sin(horn_phase_) + 32768) >> 4; }
    int32_t DrumPosition() const { return (Sin(drum_phase_) + 32768) >> 4; }

private:
    static int32_t SoftClip(int32_t x) {
        // A symmetric soft knee: increasing drive adds cabinet grit without
        // multiplying the final volume by four. Piecewise slopes stay bounded.
        const int32_t a = x < 0 ? -x : x;
        const int32_t y = a <= 1024 ? a : (a <= 3072 ? 1024 + ((a - 1024) >> 1)
                                                                   : 2048 + ((a - 3072) >> 3));
        return x < 0 ? -y : y;
    }
    void UpdateGeometry() {
        const int32_t horn_depth = (depth_ * (inside_ ? 48 : 10)) >> 4; // Q8 samples
        const int32_t drum_depth = (depth_ * (inside_ ? 64 : 18)) >> 4;
        const int32_t amplitude = (depth_ * (inside_ ? 3584 : 1800)) >> 12;
        for (int32_t ear = 0; ear < 2; ++ear) {
            // Stereo microphones look at the rotor from different angles.
            // At X=0 the angular separation and all movement cues disappear.
            // Inside separates the ear viewpoints by up to half a turn, so
            // the opening approaches one ear while moving away from the other.
            const uint32_t offset = static_cast<uint32_t>(depth_)
                                  * (inside_ ? 262144u : 131072u);
            const uint32_t angle = ear == 0 ? offset : 0u - offset;
            const int32_t h = Sin(horn_phase_ + angle);
            const int32_t d = Sin(drum_phase_ + angle);
            // The Inside delay has a larger centre value so its deeper
            // Doppler swing stays within valid, positive delay distances.
            horn_delay_target_[ear] = (inside_ ? 64 : 48) * 256 + ((h * horn_depth) >> 15);
            drum_delay_target_[ear] = 96 * 256 + ((d * drum_depth) >> 15);
            horn_gain_[ear] = 4096 - (amplitude >> 1) + ((h * amplitude) >> 16);
            drum_gain_[ear] = 4096 - (amplitude >> 2) + ((d * amplitude) >> 17);
            tone_[ear] = inside_
                ? 18000 - ((depth_ * 6000) >> 12)
                        + ((h * ((depth_ * 10000) >> 12)) >> 15)
                : 20000 - ((depth_ * 8000) >> 12)
                        + ((h * ((depth_ * 7000) >> 12)) >> 15);
        }
    }
    Delay horn_, drum_;
    uint32_t horn_phase_ = 0, drum_phase_ = 0x60000000u, geometry_count_ = 0;
    int32_t horn_step_ = 0, drum_step_ = 0;
    int32_t depth_ = 0, target_depth_ = 0, drive_ = 0, target_drive_ = 0;
    int32_t dc_ = 0, previous_input_ = 0, low_ = 0;
    int32_t horn_delay_[2] = {48 * 256, 48 * 256};
    int32_t drum_delay_[2] = {96 * 256, 96 * 256};
    int32_t horn_delay_target_[2] = {48 * 256, 48 * 256};
    int32_t drum_delay_target_[2] = {96 * 256, 96 * 256};
    int32_t horn_gain_[2] = {4096, 4096}, drum_gain_[2] = {4096, 4096};
    int32_t shadow_[2] = {}, tone_[2] = {20000, 20000};
    bool inside_ = false;
};
}
