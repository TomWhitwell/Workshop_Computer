// Exercise musical invariants over complete phrases, octave answers and Hold.
#include "Departures.h"
#include <cassert>
#include <cstdio>
#include <memory>

using namespace departures;
static void Run(Engine &e, Inputs &in, int n) {
    for (int i = 0; i < n; ++i) {
        auto out = e.Process(in);
        assert(out.left >= -2047 && out.left <= 2047 && out.right >= -2047 && out.right <= 2047);
        in.clockRise = in.chordRise = false;
    }
}
int main() {
    Tables::Init();
    auto engine = std::make_unique<Engine>(); engine->Init(424242);
    auto same = std::make_unique<Engine>(); same->Init(424242);
    auto different = std::make_unique<Engine>(); different->Init(0x9e3779b9u);
    int accent[4][37]{}; bool varied = false;
    for (int i = 0; i < 4; ++i) {
        for (int s = 0; s < engine->PhraseLength(i); ++s) {
            accent[i][s] = engine->PhraseAccent(i, s);
            assert(accent[i][s] >= 192 && accent[i][s] <= 255);
            assert(accent[i][s] == same->PhraseAccent(i, s));
            if (accent[i][s] != different->PhraseAccent(i, s)) varied = true;
        }
    }
    assert(varied);
    Inputs in; in.clockConnected = true; in.main = 4095;
    Run(*engine, in, kRate);
    bool heardUp = false, heardDown = false;
    for (int beat = 0; beat < 700; ++beat) {
        int previous[4][37]{};
        for (int i = 0; i < 4; ++i)
            for (int s = 0; s < engine->PhraseLength(i); ++s) previous[i][s] = engine->PhraseDegree(i, s);
        // Unpatched CV1 must ignore voltages and keep the Dorian default.
        in.scaleCV = beat % 3 == 0 ? 341 : beat % 3 == 1 ? -341 : 0;
        in.clockRise = true; Run(*engine, in, 4800);
        int answers = 0;
        for (int i = 0; i < 4; ++i) {
            int changed = 0;
            for (int s = 0; s < engine->PhraseLength(i); ++s) {
                const int delta = engine->PhraseDegree(i, s) - previous[i][s];
                assert(delta >= -1 && delta <= 1);
                if (delta) ++changed;
                assert(engine->PhraseAccent(i, s) == accent[i][s]);
            }
            assert(changed <= 1); // Mutations preserve the rest of the phrase.
            const int octave = engine->PhraseOctave(i);
            assert(octave == 0 || octave == 12 || octave == -12);
            if (octave) ++answers;
            if (octave == 12) heardUp = true;
            if (octave == -12) heardDown = true;
            if (engine->BellResting(i)) assert(octave == 0);
        }
        assert(answers <= 1);
        assert(engine->octaveAnswers - engine->octaveReturns == static_cast<unsigned>(answers));
        assert(engine->melodyPitch >= 36 && engine->melodyPitch <= 96);
        const int pc = (engine->melodyPitch - 2) % 12; // D Dorian with CV1 unpatched.
        assert(pc == 0 || pc == 2 || pc == 3 || pc == 5 || pc == 7 || pc == 9 || pc == 10);
    }
    assert(heardUp && heardDown && engine->phraseMutations > 0 && engine->octaveReturns > 0);
    in.sw = 2; Run(*engine, in, 4800);
    const auto starts = engine->octaveAnswers, mutations = engine->phraseMutations;
    for (int n = 0; n < 60; ++n) { in.clockRise = true; Run(*engine, in, 4800); }
    assert(engine->octaveAnswers == starts && engine->phraseMutations == mutations);
    for (int i = 0; i < 4; ++i) assert(engine->PhraseOctave(i) == 0);
    std::printf("Phrasing passed: %u answers, %u returns, %u single-step mutations; stored accents unchanged.\n",
                engine->octaveAnswers, engine->octaveReturns, engine->phraseMutations);
}
