// Startup gestures, default compatibility and wood-voice/ensemble invariants.
#include "Departures.h"
#include <cassert>
#include <cstdio>
#include <memory>
using namespace departures;
static void Run(Engine &e, Inputs &in, int n, bool silent = false) {
    for (int i = 0; i < n; ++i) {
        auto x = e.Process(in);
        assert(x.left >= -2047 && x.left <= 2047 && x.right >= -2047 && x.right <= 2047);
        if (silent) { assert(x.left == 0 && x.right == 0); assert(!e.Gate(0) && !e.Gate(1)); }
    }
}
static std::unique_ptr<Engine> Menu(Palette palette, uint32_t seed = 123) {
    auto e = std::make_unique<Engine>(); e->Init(seed);
    Inputs in; in.sw = 0; in.main = palette == Palette::Bells ? 1000 : 3000;
    Run(*e, in, 4800, true);
    assert(e->Stage() == StartupStage::Selecting && e->SoundPalette() == palette);
    return e;
}
static void Confirm(Engine &e, Inputs &in) {
    in.main = 2048; in.sw = 1; Run(e, in, 240, true);
    assert(e.Stage() == StartupStage::Playing);
}
int main() {
    Tables::Init();
    {
        Engine normal; normal.Init(123); Inputs in; in.main = 4095;
        Run(normal, in, 4800, true);
        assert(normal.Stage() == StartupStage::Playing && normal.SoundPalette() == Palette::Bells);
        in.sw = 0; Run(normal, in, 4800);
        assert(normal.chordChanges == 1 && normal.SoundPalette() == Palette::Bells);
    }
    {
        auto e = Menu(Palette::Bells); Inputs in; in.sw = 0; in.main = 1000;
        assert(e->Led(0) == 4095 && e->Led(2) == 4095 && e->Led(1) == 0 && e->Led(3) == 0);
        assert(e->Led(4) == 0 && e->Led(5) == 0);
        in.main = 3000; in.chordRise = in.clockRise = true;
        Run(*e, in, 48000, true); // Ignore incoming edges and freeze the piece in the menu.
        assert(e->beats == 0 && e->melodyEvents == 0 && e->chordChanges == 0);
        assert(e->SoundPalette() == Palette::WoodChimes);
        assert(e->Led(1) == 4095 && e->Led(3) == 4095 && e->Led(0) == 0 && e->Led(2) == 0);
        in.main = 2048; Run(*e, in, 240, true); assert(e->SoundPalette() == Palette::WoodChimes);
        in.chordRise = in.clockRise = false;
        in.sw = 1; Run(*e, in, 144, true); // A short release bounce is not a confirmation.
        assert(e->Stage() == StartupStage::Selecting);
        in.sw = 0; Run(*e, in, 240, true);
        Confirm(*e, in); Run(*e, in, 4800);
        assert(e->chordChanges == 0 && e->SoundPalette() == Palette::WoodChimes);
        in.sw = 0; Run(*e, in, 4800);
        assert(e->chordChanges == 1 && e->SoundPalette() == Palette::WoodChimes);
        // A fresh power-on/reset object always returns to Bells with Z Middle.
        Engine reset; reset.Init(123); in.sw = 1; Run(reset, in, 4800, true);
        assert(reset.SoundPalette() == Palette::Bells);
    }
    {
        // Both choices retain the same seeded score, output pitch and trigger
        // timing. Their audio must differ, without altering the CV control roles.
        auto bells = Menu(Palette::Bells, 456), wood = Menu(Palette::WoodChimes, 456);
        Inputs a; Confirm(*bells, a); Inputs b; Confirm(*wood, b);
        int differences = 0;
        for (int n = 0; n < 60 * kRate; ++n) {
            a.scaleConnected = b.scaleConnected = true;
            a.scaleCV = b.scaleCV = ((n / kRate) % 7 * 2 + 1) * 1707 / 14;
            a.destinationConnected = b.destinationConnected = true;
            a.destinationCV = b.destinationCV = ((n / 6000) % 7 * 2 + 1) * 1707 / 14;
            a.clockConnected = b.clockConnected = a.chordConnected = b.chordConnected = true;
            a.clockRise = b.clockRise = n % 6000 == 0;
            a.chordRise = b.chordRise = n % 12000 == 0;
            auto x = bells->Process(a), y = wood->Process(b);
            if (x.left != y.left || x.right != y.right) ++differences;
            assert(bells->melodyPitch == wood->melodyPitch && bells->bassPitch == wood->bassPitch);
            assert(bells->Gate(0) == wood->Gate(0) && bells->Gate(1) == wood->Gate(1));
            assert(bells->Scale() == wood->Scale() && bells->HarmonyDegree() == wood->HarmonyDegree());
            assert(y.left >= -2047 && y.left <= 2047 && y.right >= -2047 && y.right <= 2047);
        }
        assert(differences > kRate);
        assert(bells->melodyEvents == wood->melodyEvents && bells->bassEvents == wood->bassEvents);
        assert(bells->bellRests == wood->bellRests && bells->bassRests == wood->bassRests);
    }
    {
        Bell bells, wood; bells.Strike(69, 2048, 0); wood.Strike(69, 2048, 0, 23000, Palette::WoodChimes);
        int64_t early = 0, late = 0, bellLate = 0;
        for (int n = 0; n < 2 * kRate; ++n) {
            int b = bells.Process(), w = wood.Process();
            assert(w >= -23000 && w <= 23000);
            if (n < 4800) early += static_cast<int64_t>(w) * w;
            if (n >= kRate && n < kRate + 4800) {
                late += static_cast<int64_t>(w) * w;
                bellLate += static_cast<int64_t>(b) * b;
            }
        }
        assert(early > late * 10 && bellLate > late * 2);
    }
    {
        auto e = Menu(Palette::WoodChimes); Inputs in; Confirm(*e, in);
        // Correlated full-scale audio, both CV selectors, fast beat/commit
        // clocks, maximum effects and colour, and retrigger crossfades.
        for (int n = 0; n < 60 * kRate; ++n) {
            in.main = in.colour = in.space = 4095;
            in.scaleConnected = in.destinationConnected = in.clockConnected = in.chordConnected = true;
            in.scaleCV = n & 4096 ? 2047 : -2048;
            in.destinationCV = n & 2048 ? -2048 : 2047;
            in.clockRise = n % 3000 == 0; in.chordRise = n % 3500 == 0;
            const int ext = n & 64 ? 2047 : -2048;
            const auto x = e->Process(in, ext, ext);
            assert(x.left >= -2047 && x.left <= 2047 && x.right >= -2047 && x.right <= 2047);
        }
    }
    std::puts("Palettes passed: silent/debounced startup choice, normal Bells default, gesture isolation, identical score/CVs, damped wood voice and wood stress.");
}
