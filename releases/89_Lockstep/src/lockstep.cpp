#include "ComputerCard.h"
#include "lockstep_engine.h"

// Lockstep -- dual quantized pitch-mover for the Music Thing Workshop Computer.
//
// Two CV outs walk together through a scale; you tune the two oscillators by hand
// to set the interval and the card moves them in parallel. X = how often it moves,
// Y = how far. Switch DOWN (hold) = scale select, UP = freeze, MIDDLE = run.
// Main knob = resonant low-pass cutoff on the two oscillators returned via Audio In.
// See README.md.

class Lockstep : public ComputerCard {
public:
    Lockstep() {}

    virtual void __not_in_flash_func(ProcessSample)() {
        Switch  sw = SwitchVal();
        int32_t kx = KnobVal(X);
        int32_t ky = KnobVal(Y);
        int32_t km = KnobVal(Main);

        // ---- scale select while switch held DOWN (Main knob picks the scale) ----
        if (sw == Down) {
            uint8_t s = (uint8_t)((km * NUM_SCALES) >> 12);       // 0..11
            if (s >= NUM_SCALES) s = NUM_SCALES - 1;
            scaleIdx = s;
        }

        // ---- re-seed / jump to a fresh pattern on Pulse In 2 ----
        if (PulseIn2RisingEdge()) walk.reseed(walk.rng ^ 0x9e3779b9u);

        // ---- when to move: X sets rate; detect the main beat and the mid-beat (for the double) ----
        int32_t range    = (ky * LOCKSTEP_HALFMAX) >> 12;         // 0..12 semis (Y)
        bool    frozen   = (sw == Up);
        bool    mainMove = false;                                 // both voices step together
        bool    midMove  = false;                                 // voice B takes an extra step

        if (Connected(Pulse1)) {
            int32_t div = 1 + (((4095 - kx) * 60) >> 12);         // 1 (CW, fast) .. 60 (CCW, slow)
            if (PulseIn1RisingEdge()) {
                if (++divCount >= div)                                   { divCount = 0; mainMove = true; }
                else if (doubleArmed && div >= 2 && divCount == div / 2) { midMove = true; }
            }
        } else {
            intPeriod = 2000 + (((4095 - kx) * 94000) >> 12);     // 2000 (fast) .. ~96000 (slow, ~2s) samples
            if (++intCount >= intPeriod)                          { intCount = 0; mainMove = true; }
            else if (doubleArmed && intCount == intPeriod / 2)    { midMove = true; }
        }

        if (mainMove && !frozen) {
            walk.step(range);                                     // both voices move together...
            posB = walk.pos;                                      // ...B re-locked to the base walk
            doubleArmed = (range > 0) && ((walk.next() & 0xFF) < DoubleThreshold());  // arm a double, "sometimes"
        }
        if (midMove && !frozen) {
            posB = LockstepOrnament(walk.pos, range, walk.next()); // B's mid-beat passing move
            doubleArmed = false;                                   // consumed
        }

        // ---- effective scale: knob-selected base, offset by CV In 2 (bipolar) ----
        uint8_t effScale = scaleIdx;
        if (Connected(CV2)) {
            int32_t e = (int32_t)scaleIdx + ((CVIn(1) * 12) >> 11);   // ~ -12..+11 scale steps
            if (e < 0) e = 0;
            if (e >= NUM_SCALES) e = NUM_SCALES - 1;
            effScale = (uint8_t)e;
        }

        // ---- pitch out: voice A = base walk, voice B = base or its passing move; gate per voice ----
        uint8_t noteA = LockstepNote(walk.pos, effScale);
        uint8_t noteB = LockstepNote(posB,      effScale);
        if (noteA != lastNoteA) { lastNoteA = noteA; gateCountA = GATE_SAMPLES; }
        if (noteB != lastNoteB) { lastNoteB = noteB; gateCountB = GATE_SAMPLES; }
        CVOut1MIDINote(noteA);
        CVOut2MIDINote(noteB);
        bool gateA = gateCountA > 0; if (gateA) gateCountA--;
        bool gateB = gateCountB > 0; if (gateB) gateCountB--;
        PulseOut1(gateA);
        PulseOut2(gateB);

        // ---- audio: resonant low-pass on the two returned oscillators (Main = cutoff) ----
        if (sw != Down) cutoffCoef = FCoef(km);                  // hold cutoff during scale-select
        int32_t in0 = Connected(Audio1) ? AudioIn(0) : 0;
        int32_t in1 = Connected(Audio2) ? AudioIn(1) : 0;
        AudioOut(0, Svf(0, in0, cutoffCoef));
        AudioOut(1, Svf(1, in1, cutoffCoef));

        UpdateLeds(sw, range, gateA || gateB);
    }

private:
    Walk    walk;
    uint8_t scaleIdx  = 3;                 // Natural Minor default
    int32_t posB      = 0;                 // voice B position (== walk.pos except mid-beat doubles)
    bool    doubleArmed = false;           // this interval will take a mid-beat double
    uint8_t lastNoteA = 0, lastNoteB = 0;
    int32_t gateCountA = 0, gateCountB = 0;
    int32_t intCount  = 0, intPeriod = 12000, divCount = 0;
    int32_t cutoffCoef = 1200;
    int32_t svfLow[2] = {0, 0}, svfBand[2] = {0, 0};

    static constexpr int32_t GATE_SAMPLES = 480;   // ~10 ms @ 48 kHz

    // Probability (0..255) that voice B takes an extra mid-beat move. Default ~25%.
    // CV In 1 is bipolar (-2048..2047): full negative = never, 0 V ~ 50%, full positive
    // = always. Unpatched CVIn reads 0, so the ~25% default is used instead.
    int32_t DoubleThreshold() {
        if (!Connected(CV1)) return 64;                  // ~25% of 256
        int32_t t = (CVIn(0) + 2048) >> 4;               // -2048..2047 -> 0..255
        return t < 0 ? 0 : (t > 255 ? 255 : t);
    }

    static constexpr int32_t SVF_Q        = 1000;  // Q12 damping; lower = more resonant.
                                                   // ponytail: fixed resonance; expose on a knob if you want it playable.

    // Knob (0..4095) -> SVF frequency coefficient (~40..3600, Q12). Squared for an exponential feel.
    int32_t FCoef(int32_t k) {
        int32_t kk = (k * k) >> 12;                // 0..4094, exp curve
        int32_t f  = 40 + ((kk * 3560) >> 12);
        return f > 3600 ? 3600 : f;
    }

    // Chamberlin state-variable low-pass, fixed point Q12, one per channel.
    int16_t Svf(int ch, int32_t in, int32_t f) {
        int32_t low = svfLow[ch], band = svfBand[ch];
        low  += (f * band) >> 12;
        int32_t high = in - low - ((SVF_Q * band) >> 12);
        band += (f * high) >> 12;
        if (band >  8192) band =  8192;            // keep state bounded if it rings hard
        if (band < -8192) band = -8192;
        svfLow[ch] = low; svfBand[ch] = band;
        if (low >  2047) low =  2047;
        if (low < -2048) low = -2048;
        return (int16_t)low;
    }

    void UpdateLeds(Switch sw, int32_t range, bool gate) {
        if (sw == Down) {                          // scale index, 6-bit binary, half brightness
            for (int i = 0; i < 6; i++) LedBrightness(i, ((scaleIdx >> i) & 1) ? 1500 : 0);
            return;
        }
        if (sw == Up) {                            // frozen: all dim
            for (int i = 0; i < 6; i++) LedBrightness(i, 300);
            return;
        }
        int32_t idx = range > 0 ? ((walk.pos + range) * 5) / (2 * range) : 2;  // position bar 0..5
        if (idx < 0) idx = 0;
        if (idx > 5) idx = 5;
        for (int i = 0; i < 6; i++) LedBrightness(i, i == idx ? 4095 : (gate ? 400 : 0));
    }
};

#ifndef COMPUTERCARD_HOST_SIM
int main() {
    set_sys_clock_khz(192000, true);
    static Lockstep card;
    card.EnableNormalisationProbe();
    card.Run();
}
#endif
