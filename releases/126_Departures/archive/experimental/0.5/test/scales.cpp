// Test audible scale membership, triads, clock independence and physical LED codes.
#include "Departures.h"
#include <cassert>
#include <cstdio>
#include <memory>
using namespace departures;
static constexpr int modes[7][7] = {
    {0,2,3,5,7,9,10}, {0,2,3,5,7,8,10}, {0,2,4,5,7,9,11},
    {0,2,4,5,7,9,10}, {0,2,4,6,7,9,11}, {0,1,3,5,7,8,10}, {0,1,3,5,6,8,10}
};
static int Centre(int index) { return (2 * index + 1) * 1707 / 14; }
static bool InScale(int midi, int mode) {
    const int pc = (midi - 2) % 12; // Fixed D tonic; no key transposition.
    for (int n : modes[mode]) if (pc == n) return true;
    return false;
}
static void Run(Engine &e, Inputs &in, int n) {
    for (int i = 0; i < n; ++i) {
        auto out = e.Process(in);
        assert(out.left >= -2047 && out.left <= 2047 && out.right >= -2047 && out.right <= 2047);
        in.clockRise = in.chordRise = false;
    }
}
static void CheckLeds(const Engine &e, int scale) {
    for (int i = 0; i < 3; ++i) assert(e.Led(i) == (((scale + 1) & (1 << i)) ? 4095 : 0));
    assert(e.Led(5) == 0); // Timing indication belongs exclusively to the hardware wrapper.
}
int main() {
    Tables::Init();
    auto e = std::make_unique<Engine>(); e->Init(123);
    Inputs in; in.main = 4095;
    in.scaleConnected = in.destinationConnected = in.chordConnected = in.clockConnected = true;
    Run(*e, in, kRate);
    for (int mode = 0; mode < 7; ++mode) {
        const int degreeBefore = e->HarmonyDegree();
        in.scaleCV = Centre(mode); Run(*e, in, 4800);
        assert(e->Scale() == mode && e->HarmonyDegree() == degreeBefore);
        CheckLeds(*e, mode);
        for (int degree = 0; degree < 7; ++degree) {
            in.destinationCV = Centre(degree); Run(*e, in, 4800);
            in.chordRise = true; Run(*e, in, kRate);
            assert(e->HarmonyDegree() == degree);
            bool used[3]{};
            for (int i = 0; i < 3; ++i) {
                assert(e->PadTransition(i) == 32768);
                const int pc = (e->PadNote(i) - 2) % 12;
                bool found = false;
                for (int t = 0; t < 3; ++t) {
                    if (!used[t] && pc == modes[mode][(degree + 2 * t) % 7]) {
                        used[t] = true; found = true; break;
                    }
                }
                assert(found); // Pad is the requested triad, not just any scale notes.
            }
            for (int beat = 0; beat < 12; ++beat) {
                const auto melody = e->melodyEvents, bass = e->bassEvents;
                in.clockRise = true; Run(*e, in, 6000);
                if (e->melodyEvents != melody) assert(InScale(e->melodyPitch, mode));
                if (e->bassEvents != bass) assert(e->bassPitch == 38 + modes[mode][degree]);
            }
        }
    }
    // Scale voltage stays an external action in Hold, without a commit trigger.
    in.sw = 2; in.scaleCV = Centre(2); Run(*e, in, kRate);
    assert(e->Scale() == 2 && e->HarmonyDegree() == 6 && e->Led(4) == 4095);
    CheckLeds(*e, 2);
    // CV1 hysteresis is independent of the CV2 selector.
    in.scaleCV = 0; Run(*e, in, 4800);
    in.scaleCV = 250; Run(*e, in, 4800); assert(e->Scale() == 0);
    in.scaleCV = 252; Run(*e, in, 4800); assert(e->Scale() == 1);
    in.scaleCV = 244; Run(*e, in, 4800); assert(e->Scale() == 1);
    in.scaleCV = 234; Run(*e, in, 4800); assert(e->Scale() == 0);
    in.scaleCV = 2047; Run(*e, in, kRate); assert(e->Scale() == 6); CheckLeds(*e, 6);
    in.scaleConnected = false; Run(*e, in, kRate); assert(e->Scale() == 0);
    bool breathing = false;
    for (int i = 0; i < 3; ++i) if (e->Led(i) > 0 && e->Led(i) < 4095) breathing = true;
    assert(breathing); // LED1..3 return to pad envelopes rather than binary mode bits.
    // A scale change must not postpone the scheduled 24-beat chord advance.
    auto timed = std::make_unique<Engine>(); timed->Init(456);
    Inputs t; t.scaleConnected = t.clockConnected = true;
    Run(*timed, t, kRate);
    for (int beat = 0; beat < 24; ++beat) {
        if (beat == 12) t.scaleCV = Centre(2);
        t.clockRise = true; Run(*timed, t, 6000);
    }
    assert(timed->Scale() == 2 && timed->beats == 24 && timed->HarmonyDegree() == 2);
    // A mode request that is deferred by a simultaneous commit can be cancelled
    // by the next control sample. Do not apply a stale queued scale afterward.
    auto queued = std::make_unique<Engine>(); queued->Init(777);
    Inputs q; q.scaleConnected = q.destinationConnected = q.chordConnected = q.clockConnected = true;
    Run(*queued, q, kRate);
    q.scaleCV = Centre(6); q.destinationCV = Centre(5);
    for (int n = 0; n < 144; ++n) {
        q.chordRise = n == 57; // Lands in the scale planning slot (phase 10).
        if (n == 58) q.scaleCV = 0;
        queued->Process(q);
    }
    assert(queued->Scale() == 0 && queued->scaleChanges == 0 && queued->HarmonyDegree() == 5);
    std::printf("Scales passed: 49 mode/chord combinations, sounding pitch membership, hysteresis, Hold, unplug, LED codes and unchanged chord timing.\n");
}
