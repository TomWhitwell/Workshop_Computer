// Original, AI-assisted validation of the same engine compiled into the UF2.
#include "Departures.h"
#include <cassert>
#include <cstdio>
#include <memory>

using namespace departures;
static void Run(Engine &e, Inputs &in, int samples) {
    for (int i = 0; i < samples; ++i) {
        const auto out = e.Process(in);
        assert(out.left >= -2047 && out.left <= 2047);
        assert(out.right >= -2047 && out.right <= 2047);
        in.clockRise = in.chordRise = false;
    }
}
static std::unique_ptr<Engine> New(uint32_t seed = 123) {
    auto e = std::make_unique<Engine>(); e->Init(seed); return e;
}
int main() {
    Tables::Init();
    {
        auto e = New(); Inputs in;
        Run(*e, in, 60 * kRate);
        assert(e->beats > 45 && e->melodyEvents > 10 && e->bassEvents > 5);
        assert(e->chordChanges >= 2);
    }
    {
        auto e = New(); Inputs in; in.sw = 2;
        Run(*e, in, 40 * kRate);
        assert(e->chordChanges == 0 && e->melodyEvents > 0);
        in.sw = 0; Run(*e, in, kRate);
        assert(e->chordChanges == 1); // A held Down switch is a single action.
        in.sw = 1; Run(*e, in, 4800);
        in.sw = 0; Run(*e, in, 4800);
        assert(e->chordChanges == 2);
    }
    {
        auto e = New(); Inputs in; in.clockConnected = in.chordConnected = true;
        Run(*e, in, kRate);
        assert(e->beats == 0);
        for (int i = 0; i < 32; ++i) {
            in.clockRise = true; Run(*e, in, 12000);
        }
        assert(e->beats == 32);
        const auto beats = e->beats;
        Run(*e, in, 4 * kRate);
        assert(e->beats == beats); // Patched stopped clock really stops.
        in.sw = 2; Run(*e, in, 4800);
        const auto chords = e->chordChanges;
        in.chordRise = true; Run(*e, in, 4800);
        assert(e->chordChanges == chords + 1); // External advance overrides hold.
        // Unplugging with a spurious edge cannot change the harmony.
        in.chordConnected = false; Run(*e, in, 4800);
        in.chordRise = true; Run(*e, in, 4800);
        assert(e->chordChanges == chords + 1);
        in.clockConnected = false; Run(*e, in, 2 * kRate);
        assert(e->beats > beats);
    }
    {
        auto e = New(); Inputs in;
        in.transposeCV = 341; Run(*e, in, kRate);
        assert(e->Transpose() == 12);
        in.transposeCV = -341; Run(*e, in, kRate);
        assert(e->Transpose() == -12);
        in.transposeCV = 0; Run(*e, in, kRate);
        assert(e->Transpose() == 0);
    }
    {
        auto e = New(); Inputs in; in.clockConnected = true;
        Run(*e, in, kRate);
        in.clockRise = true;
        int risingAt = -1, gateSamples = 0;
        for (int i = 0; i < 2000; ++i) {
            e->Process(in); in.clockRise = false;
            if (e->Gate(0)) { if (risingAt < 0) risingAt = i; ++gateSamples; }
        }
        assert(risingAt >= 48 && risingAt < 150);
        assert(gateSamples == 480);
    }
    {
        // Determinism checks both phrase generation and every audio sample.
        auto a = New(), b = New(); Inputs in;
        for (int n = 0; n < 5 * kRate; ++n) {
            auto x = a->Process(in), y = b->Process(in);
            assert(x.left == y.left && x.right == y.right);
        }
    }
    {
        auto e = New(987); Inputs in; int peak = 0; int64_t energy = 0;
        // Stress full effects, every control extreme, rapid clocking, and
        // full-scale external audio under integer/array sanitizers.
        for (int n = 0; n < 120 * kRate; ++n) {
            const int second = n / kRate;
            in.main = second & 1 ? 4095 : 0;
            in.colour = second & 2 ? 4095 : 0;
            in.space = second & 4 ? 4095 : 0;
            in.activityCV = second & 8 ? 2047 : -2048;
            in.transposeCV = second & 16 ? 2047 : -2048;
            in.clockConnected = true;
            in.clockRise = (n % 3000) == 0;
            in.chordConnected = true;
            in.chordRise = (n % 12000) == 0;
            const int ext = n & 256 ? 2047 : -2048;
            auto out = e->Process(in, ext, ext);
            assert(out.left >= -2047 && out.left <= 2047 && out.right >= -2047 && out.right <= 2047);
            peak = Clamp(out.left < 0 ? -out.left : out.left, peak, 2047);
            energy += static_cast<int64_t>(out.left) * out.left;
            assert(e->melodyPitch >= 36 && e->melodyPitch <= 96);
        }
        assert(energy > 100000000 && peak > 100);
        std::printf("Stress: peak %d/2047, %u notes, %u chords\n", peak, e->melodyEvents, e->chordChanges);
    }
    {
        Space space;
        for (int n = 0; n < 20 * kRate; ++n) space.Process(n & 32 ? 16000 : -16000, 4075);
        int peak = 0;
        for (int n = 0; n < 90 * kRate; ++n) {
            const auto out = space.Process(0, 4075);
            if (n > 89 * kRate) {
                peak = Clamp(out.left < 0 ? -out.left : out.left, peak, 100000);
                peak = Clamp(out.right < 0 ? -out.right : out.right, peak, 100000);
            }
        }
        assert(peak < 20); // The space drains instead of building DC or oscillating.
    }
    std::puts("Departures host checks passed.");
}
