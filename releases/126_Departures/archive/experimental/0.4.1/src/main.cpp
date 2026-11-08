// Copyright (c) 2026 Adrian Vos. SPDX-License-Identifier: MIT
#include "ComputerCard.h"
#include "Departures.h"
#include "hardware/clocks.h"
#include "hardware/timer.h"
#include "hardware/vreg.h"
#include "pico/rand.h"

static departures::Engine engine;

class DeparturesCard : public ComputerCard {
public:
    volatile uint32_t maxCallbackUs = 0;
private:
    bool timingWarning_ = false;
    int divider_ = 0;
    int lastMelody_ = -1, lastBass_ = -1;
    departures::Inputs inputs_;
    void ProcessSample() override {
        const uint32_t start = time_us_32();
        if (++divider_ == 48) {
            divider_ = 0;
            inputs_.main = KnobVal(Knob::Main);
            inputs_.colour = KnobVal(Knob::X);
            inputs_.space = KnobVal(Knob::Y);
            inputs_.scaleConnected = Connected(Input::CV1);
            inputs_.scaleCV = inputs_.scaleConnected ? CVIn1() : 0;
            inputs_.destinationConnected = Connected(Input::CV2);
            inputs_.destinationCV = inputs_.destinationConnected ? CVIn2() : 0;
            inputs_.sw = SwitchVal();
            inputs_.clockConnected = Connected(Input::Pulse1);
            inputs_.chordConnected = Connected(Input::Pulse2);
        }
        // Capture pulse edges at audio rate so short triggers are not lost.
        inputs_.clockRise = Connected(Input::Pulse1) && PulseIn1RisingEdge();
        inputs_.chordRise = Connected(Input::Pulse2) && PulseIn2RisingEdge();
        if (inputs_.chordRise) {
            // Sample the latest CV2 value at the commit edge, rather than
            // using the previous millisecond's cached control value.
            inputs_.destinationConnected = Connected(Input::CV2);
            inputs_.destinationCV = inputs_.destinationConnected ? CVIn2() : 0;
        }
        const auto out = engine.Process(inputs_, Connected(Input::Audio1) ? AudioIn1() : 0,
                                                   Connected(Input::Audio2) ? AudioIn2() : 0);
        AudioOut1(static_cast<int16_t>(out.left));
        AudioOut2(static_cast<int16_t>(out.right));
        // Output pitch is calibrated where the unit has EEPROM calibration.
        // 0V = C2 (MIDI36), so bass and melody both fit in a useful CV range.
        if (lastMelody_ != engine.melodyPitch) {
            lastMelody_ = engine.melodyPitch;
            CVOut1Millivolts((lastMelody_ - 36) * 1000 / 12);
        }
        if (lastBass_ != engine.bassPitch) {
            lastBass_ = engine.bassPitch;
            CVOut2Millivolts((lastBass_ - 36) * 1000 / 12);
        }
        PulseOut1(engine.Gate(0));
        PulseOut2(engine.Gate(1));
        if (divider_ >= 20 && divider_ < 25) LedBrightness(divider_ - 20, engine.Led(divider_ - 20));
        if (divider_ == 25) LedOn(5, timingWarning_);
        const uint32_t elapsed = time_us_32() - start;
        if (elapsed > maxCallbackUs) maxCallbackUs = elapsed;
        // Callback only: surrounding ComputerCard interrupt work is additional.
        if (elapsed >= 18) timingWarning_ = true;
    }
};

int main() {
    // Use the established 192MHz/RAM setup used by the ambient cards.
    vreg_set_voltage(VREG_VOLTAGE_1_15);
    busy_wait_us_32(1000);
    set_sys_clock_khz(192000, true);
    departures::Tables::Init();
    engine.Init(get_rand_32());
    DeparturesCard card;
    card.EnableNormalisationProbe();
    card.Run();
}
