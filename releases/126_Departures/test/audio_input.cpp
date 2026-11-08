// Measure wet-only external audio against an identically seeded dry-input run.
// Subtraction removes the autonomous ensemble without muting any production DSP.
#include "Departures.h"
#include <cmath>
#include <cstdio>
#include <memory>
#include <cassert>

static double Measure(int space, bool rightInput) {
    auto reference = std::make_unique<departures::Engine>();
    auto external = std::make_unique<departures::Engine>();
    reference->Init(20261107); external->Init(20261107);
    departures::Inputs in;
    in.space = space; in.clockConnected = true;
    uint32_t phase = 0;
    const uint32_t increment = static_cast<uint32_t>(220.0 * 4294967296.0 / departures::kRate);
    double inputEnergy = 0, outputEnergy = 0;
    for (int n = 0; n < 12 * departures::kRate; ++n) {
        phase += increment;
        const int input = departures::Sin(phase) / 128; // ~0.75V peak, one input.
        const auto a = reference->Process(in);
        const auto b = external->Process(in, rightInput ? 0 : input, rightInput ? input : 0);
        assert(b.left >= -2047 && b.left <= 2047 && b.right >= -2047 && b.right <= 2047);
        if (n > 6 * departures::kRate) {
            const double dl = b.left - a.left, dr = b.right - a.right;
            inputEnergy += input * input;
            outputEnergy += (dl * dl + dr * dr) / 2;
        }
    }
    const double ratio = std::sqrt(outputEnergy / inputEnergy);
    std::printf("Y=%d, input %d: wet RMS ratio %.4f (%+.2f dB)\n", space, rightInput ? 2 : 1,
                ratio, ratio > 0 ? 20 * std::log10(ratio) : -999.0);
    return ratio;
}

int main() {
    departures::Tables::Init();
    const double middle = Measure(2300, false);
    const double full = Measure(4095, false);
    const double other = Measure(4095, true);
    const double dry = Measure(0, false);
    assert(dry == 0); // Preserve the effects-only routing and Y's dry endpoint.
    assert(std::abs(full - other) < 0.0001); // Both input jacks have equal gain.
    assert(middle > 0.2 && full > 0.4); // Useful wet level for a modest mono input.
    return 0;
}
