// External harmony selection: prepare continuously, commit once on Pulse In 2.
#include "Departures.h"
#include <cassert>
#include <cstdio>
#include <memory>
using namespace departures;
static void Run(Engine &e, Inputs &in, int n) {
    for (int i = 0; i < n; ++i) {
        auto out = e.Process(in);
        assert(out.left >= -2047 && out.left <= 2047 && out.right >= -2047 && out.right <= 2047);
        in.chordRise = in.clockRise = false;
    }
}
static int Centre(int degree) { return ((degree * 2 + 1) * 1707) / 14; }
int main() {
    Tables::Init();
    auto e = std::make_unique<Engine>(); e->Init(123);
    Inputs in; in.destinationConnected = in.chordConnected = in.clockConnected = true;
    Run(*e, in, kRate);
    for (int degree = 0; degree < 7; ++degree) {
        const int before = e->HarmonyDegree();
        in.destinationCV = Centre(degree); Run(*e, in, 4800);
        assert(e->SelectedDestination() == degree && e->HarmonyDegree() == before);
        in.chordRise = true; Run(*e, in, 4800);
        assert(e->HarmonyDegree() == degree);
    }
    // At the first boundary, a small noisy excursion must not change bins.
    in.destinationCV = 0; Run(*e, in, 4800);
    in.destinationCV = 250; Run(*e, in, 4800); assert(e->SelectedDestination() == 0);
    in.destinationCV = 252; Run(*e, in, 4800); assert(e->SelectedDestination() == 1);
    in.destinationCV = 244; Run(*e, in, 4800); assert(e->SelectedDestination() == 1);
    in.destinationCV = 234; Run(*e, in, 4800); assert(e->SelectedDestination() == 0);
    // The choice belongs to the edge, even if CV changes before planning runs.
    in.destinationCV = Centre(2); in.chordRise = true; e->Process(in);
    in.chordRise = false; in.destinationCV = Centre(5); Run(*e, in, 4800);
    assert(e->HarmonyDegree() == 2 && e->SelectedDestination() == 5);
    in.sw = 2; Run(*e, in, 4800);
    in.chordRise = true; Run(*e, in, kRate); assert(e->HarmonyDegree() == 5);
    auto changes = e->chordChanges;
    in.chordRise = true; Run(*e, in, 4800);
    assert(e->chordChanges == changes); // Same chord does not retrigger the pad fade.
    in.scaleConnected = true;
    in.scaleCV = Centre(2); Run(*e, in, 4800); assert(e->Scale() == 2);
    in.scaleCV = Centre(0); Run(*e, in, 4800); assert(e->Scale() == 0);
    // All seven destinations across all seven scales with octave answers.
    in.sw = 1;
    for (int beat = 0; beat < 280; ++beat) {
        in.destinationCV = Centre(beat % 7);
        in.scaleCV = Centre((beat / 7) % 7);
        in.chordRise = in.clockRise = true;
        Run(*e, in, 6000);
        assert(e->HarmonyDegree() == beat % 7);
        assert(e->Scale() == (beat / 7) % 7);
        assert(e->melodyPitch >= 36 && e->melodyPitch <= 96);
    }
    // Voltage alone neither commits chords nor modulates the clock/activity.
    auto low = std::make_unique<Engine>(), high = std::make_unique<Engine>();
    low->Init(99); high->Init(99);
    Inputs a, b; a.destinationConnected = b.destinationConnected = true;
    a.destinationCV = -2048; b.destinationCV = 2047;
    for (int n = 0; n < 40 * kRate; ++n) {
        auto x = low->Process(a), y = high->Process(b);
        assert(x.left == y.left && x.right == y.right);
    }
    assert(low->SelectedDestination() == 0 && high->SelectedDestination() == 6);
    assert(low->chordChanges == 0 && high->chordChanges == 0);
    assert(low->beats == high->beats && low->beats > 20);
    a.destinationConnected = false; Run(*low, a, 30 * kRate);
    assert(low->chordChanges >= 1); // Unplug restores automatic progression.
    // Unpatched Pulse In 2 still advances to the next sequential chord.
    a.clockConnected = a.chordConnected = true; a.sw = 2; Run(*low, a, 4800);
    changes = low->chordChanges; a.chordRise = true; Run(*low, a, 4800);
    assert(low->chordChanges == changes + 1);
    std::puts("Destinations passed: seven chords, edge latching, hysteresis, hold, scale independence and unplug fallback.");
}
