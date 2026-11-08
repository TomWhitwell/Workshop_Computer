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
            in.destinationCV = second & 8 ? 2047 : -2048;
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
        auto e = New(); Inputs in;
        in.clockConnected = in.chordConnected = true; in.sw = 2;
        Run(*e, in, kRate);
        // D minor -> F major shares F and A. Their exact notes, oscillator
        // phases and breath cycles must continue while D fades toward C.
        const auto fPhase = e->PadPhase(1), aPhase = e->PadPhase(2);
        const auto fBreath = e->PadBreath(1), aBreath = e->PadBreath(2);
        in.chordRise = true;
        Run(*e, in, kRate / 10);
        assert(e->sharedNotes == 2 && e->PadNote(0) == 50);
        assert(e->PadTransition(0) > 0 && e->PadTransition(0) < 32768);
        Run(*e, in, kRate / 2);
        assert(e->PadNote(0) == 48 && e->PadPending(0) == 48);
        assert(e->PadNote(1) == 53 && e->PadNote(2) == 57);
        assert(e->PadTransition(1) == 32768 && e->PadTransition(2) == 32768);
        assert(e->PadPhase(1) == fPhase + Tables::pitch[53] * static_cast<uint32_t>(kRate * 3 / 5));
        assert(e->PadPhase(2) == aPhase + Tables::pitch[57] * static_cast<uint32_t>(kRate * 3 / 5));
        assert(e->PadBreath(1) == fBreath + static_cast<uint32_t>(4294967296ull / 11300) * 600);
        assert(e->PadBreath(2) == aBreath + static_cast<uint32_t>(4294967296ull / 15600) * 600);
        // Stress repeated requests during a transition. Every actual pitch
        // change must occur at zero transition gain, and eventually settle.
        for (int n = 0; n < 2 * kRate; ++n) {
            in.chordRise = n % 3000 == 0;
            int notes[3] = {e->PadNote(0), e->PadNote(1), e->PadNote(2)};
            e->Process(in);
            for (int i = 0; i < 3; ++i)
                if (e->PadNote(i) != notes[i]) assert(e->PadTransition(i) == 0);
        }
        in.chordRise = false; Run(*e, in, kRate);
        for (int i = 0; i < 3; ++i) {
            assert(e->PadNote(i) == e->PadPending(i));
            assert(e->PadTransition(i) == 32768);
        }
    }
    {
        auto e = New(); Inputs in; in.clockConnected = true;
        Run(*e, in, kRate);
        int minTide = 32767, maxTide = -32767;
        for (int n = 0; n < 180 * kRate; ++n) {
            in.clockRise = n % 6000 == 0;
            uint32_t counts[4];
            for (int i = 0; i < 4; ++i) counts[i] = e->bellEvents[i];
            const bool bassWasResting = e->RestingLayer() == 4;
            const auto bassCount = e->bassEvents;
            e->Process(in);
            int resting = e->RestingLayer() == 4 ? 1 : 0;
            for (int i = 0; i < 4; ++i) {
                if (e->BellResting(i)) {
                    ++resting;
                    assert(e->bellEvents[i] == counts[i]);
                }
            }
            assert(resting <= 1);
            if (bassWasResting && e->RestingLayer() == 4) assert(e->bassEvents == bassCount);
            if (n % kRate == 0) {
                if (e->Tide() < minTide) minTide = e->Tide();
                if (e->Tide() > maxTide) maxTide = e->Tide();
                assert(e->Density() >= 0 && e->Density() <= 4095);
                assert(e->LivingColour() >= 0 && e->LivingColour() <= 4095);
                assert(e->LivingSpace() >= 0 && e->LivingSpace() <= 4075);
            }
        }
        assert(e->bellRests > 0 && e->bassRests > 0);
        assert(maxTide - minTide > 16000);
        std::printf("Evolution: %u shared tones, %u bell rests, %u bass rests; tide %d..%d\n",
                    e->sharedNotes, e->bellRests, e->bassRests, minTide, maxTide);
        // Hold stops new rests; a rest already in progress finishes normally.
        in.sw = 2; in.clockRise = false; Run(*e, in, 4800);
        const auto bellRests = e->bellRests, bassRests = e->bassRests;
        const auto chordChanges = e->chordChanges;
        for (int n = 0; n < 10 * kRate; ++n) {
            in.clockRise = n % 6000 == 0; e->Process(in);
        }
        assert(e->bellRests == bellRests && e->bassRests == bassRests);
        assert(e->chordChanges == chordChanges && e->RestingLayer() == -1);
        // Stopping the patched clock also stops rest scheduling and note events,
        // but the tide continues to move.
        const int tide = e->Tide();
        const auto notes = e->melodyEvents, bass = e->bassEvents;
        Run(*e, in, 5 * kRate);
        assert(e->Tide() != tide && e->melodyEvents == notes && e->bassEvents == bass);
        in.space = 0; Run(*e, in, 4800);
        assert(e->LivingSpace() == 0);
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
