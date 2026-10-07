// Copyright (c) 2026 soveda. SPDX-License-Identifier: MIT
#include "controls.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdint>

static void Tick(spin::Controls& c, const spin::Inputs& in, int count) {
    for (int i = 0; i < count; ++i) c.Tick(in);
}
static void TestControls() {
    spin::Controls normal;
    spin::Inputs in;
    in.main = 4095; // Knob alone must never select Inside on normal startup.
    Tick(normal, in, 100);
    assert(normal.CurrentStage() == spin::Stage::Playing);
    assert(normal.CurrentPerspective() == spin::Perspective::Outside);
    spin::Controls menu;
    in.sw = spin::SwitchPosition::Down;
    Tick(menu, in, 100);
    assert(menu.CurrentStage() == spin::Stage::Selecting);
    assert(!menu.Settings().brake);
    assert(menu.CurrentPerspective() == spin::Perspective::Inside);
    in.main = 2040;
    Tick(menu, in, 10);
    assert(menu.CurrentPerspective() == spin::Perspective::Inside); // Hysteresis.
    in.main = 0;
    Tick(menu, in, 10);
    assert(menu.CurrentPerspective() == spin::Perspective::Outside);
    in.main = 4095;
    Tick(menu, in, 10);
    in.sw = spin::SwitchPosition::Middle;
    Tick(menu, in, 10);
    assert(menu.CurrentStage() == spin::Stage::Playing);
    assert(!menu.Settings().brake); // Release from boot setup is consumed.
    in.sw = spin::SwitchPosition::Down;
    Tick(menu, in, 10);
    assert(menu.Settings().brake);
    Tick(menu, in, 100);
    assert(menu.Settings().brake); // Brakes for the entire hold.
    in.sw = spin::SwitchPosition::Middle;
    Tick(menu, in, 10);
    assert(!menu.Settings().brake); // Release resumes without a second press.
    in.sw = spin::SwitchPosition::Down;
    Tick(menu, in, 10);
    assert(menu.Settings().brake); // Every new hold brakes.
    in.brake_gate = true;
    in.sw = spin::SwitchPosition::Middle;
    Tick(menu, in, 10);
    assert(menu.Settings().brake); // Gate still held after switch release.
    in.sw = spin::SwitchPosition::Down;
    in.brake_gate = false;
    Tick(menu, in, 10);
    assert(menu.Settings().brake); // Switch still held after gate release.
    in.sw = spin::SwitchPosition::Up;
    Tick(menu, in, 10);
    assert(menu.Settings().fast);
    in.speed_gate_connected = true;
    in.speed_gate = false;
    in.brake_gate = true;
    Tick(menu, in, 10);
    assert(!menu.Settings().fast && menu.Settings().brake);
    in.speed_gate = true;
    in.brake_gate = false;
    in.cv1 = -2048;
    in.x = 0;
    in.cv2 = 2047;
    Tick(menu, in, 10);
    assert(menu.Settings().fast && !menu.Settings().brake);
    assert(menu.Settings().speed == 0 && menu.Settings().intensity == 4094);
}
static void TestMovementResponse() {
    assert(spin::MovementDepth(0) == 0);
    assert(spin::MovementDepth(4095) == 4095);
    for (int x = 1; x <= 4095; ++x) {
        assert(spin::MovementDepth(x) >= spin::MovementDepth(x - 1));
        assert(spin::MovementDepth(x) <= 4095);
    }
    spin::Rotary outside, inside, inside_zero;
    spin::Parameters p;
    p.fast = true;
    p.intensity = 512; // Low knob region reported as inaudible on alpha2.
    auto ip = p;
    ip.perspective = spin::Perspective::Inside;
    auto zp = ip;
    zp.intensity = 0;
    int64_t signal = 0, stereo = 0, difference = 0;
    for (int n = 0; n < 192000; ++n) {
        if (n % 48 == 0) {
            outside.SetParameters(p); inside.SetParameters(ip); inside_zero.SetParameters(zp);
        }
        const int input = static_cast<int>(700 * std::sin(n * 6.283185307179586 * 440 / 48000));
        auto a = outside.Process(input), b = inside.Process(input), z = inside_zero.Process(input);
        assert(z.left == z.right); // Zero remains stationary/mono in Inside too.
        if (n > 48000) {
            signal += static_cast<int64_t>(a.left) * a.left;
            stereo += static_cast<int64_t>(a.left - a.right) * (a.left - a.right);
            difference += static_cast<int64_t>(a.left - b.left) * (a.left - b.left);
        }
    }
    // For this reference tone, require meaningful low-X stereo modulation
    // and an appreciable mode difference; listening quality still needs audition.
    assert(stereo * 100 > signal); // Stereo difference RMS >10% of signal RMS.
    assert(difference * 4 > signal); // Mode difference RMS >50%.
}
static void TestMotor() {
    spin::Rotary r;
    spin::Parameters p;
    p.fast = true;
    // Compare fractions of target speed reached after half a second.
    for (int t = 0; t < 500; ++t) r.SetParameters(p);
    const double horn_fraction = r.HornStep() / (6500.0 * 89478 / 1000);
    const double drum_fraction = r.DrumStep() / (5500.0 * 89478 / 1000);
    assert(horn_fraction > drum_fraction + 0.25);
    p.brake = true;
    for (int t = 0; t < 20000; ++t) r.SetParameters(p);
    assert(r.HornStep() == 0 && r.DrumStep() == 0);
}
static void TestAudio() {
    spin::Rotary outside, inside, mono;
    spin::Parameters p;
    p.fast = true;
    p.intensity = 3600;
    spin::Parameters ip = p;
    ip.perspective = spin::Perspective::Inside;
    spin::Parameters mp = p;
    mp.intensity = 0;
    int64_t stereo_energy = 0, mode_difference = 0, signal_energy = 0;
    int clipped = 0;
    for (int n = 0; n < 240000; ++n) {
        if (n % 48 == 0) { outside.SetParameters(p); inside.SetParameters(ip); mono.SetParameters(mp); }
        const int32_t input = static_cast<int32_t>(900 * std::sin(n * 6.283185307179586 * 440 / 48000));
        const auto o = outside.Process(input), i = inside.Process(input), m = mono.Process(input);
        assert(m.left == m.right); // X=0 really removes stereo movement.
        if (n > 48000) {
            stereo_energy += static_cast<int64_t>(o.left - o.right) * (o.left - o.right);
            mode_difference += static_cast<int64_t>(o.left - i.left) * (o.left - i.left);
            signal_energy += static_cast<int64_t>(o.left) * o.left;
            if (o.left == 2047 || o.left == -2048 || i.left == 2047 || i.left == -2048) ++clipped;
        }
    }
    assert(stereo_energy > 1000000 && mode_difference > 1000000 && signal_energy > 1000000);
    assert(clipped == 0);
    // Silence and DC must decay instead of feeding back indefinitely.
    for (int n = 0; n < 192000; ++n) {
        const auto o = inside.Process(n < 48000 ? 2047 : 0);
        if (n > 180000) assert(std::abs(o.left) < 8 && std::abs(o.right) < 8);
    }
    // Full-range transients plus abrupt CV changes: sanitizer checks every
    // arithmetic path and bounds check protects both fractional delay lines.
    uint32_t seed = 7;
    for (int n = 0; n < 500000; ++n) {
        seed = seed * 1664525u + 1013904223u;
        if (n % 48 == 0) {
            p.drive = seed & 4095;
            p.intensity = (seed >> 12) & 4095;
            p.speed = (seed >> 20) & 4095;
            p.fast = (seed & 2) != 0;
            p.brake = (seed & 4) != 0;
            p.perspective = seed & 8 ? spin::Perspective::Inside : spin::Perspective::Outside;
            outside.SetParameters(p);
        }
        const auto o = outside.Process(static_cast<int32_t>(seed & 4095) - 2048);
        assert(o.left >= -2048 && o.left <= 2047 && o.right >= -2048 && o.right <= 2047);
    }
}
int main() {
    TestControls(); TestMovementResponse(); TestMotor(); TestAudio();
    std::puts("PASS: boot defaults/selection, brake, gate priority, CV clamps, motor inertia, stereo motion, mode difference, DC decay, transient bounds");
}
