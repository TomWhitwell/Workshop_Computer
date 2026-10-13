// pin_toggle.cpp — dumb output test: slowly square-wave the GBA output lines so you can
// physically verify (multimeter/scope/LED) that SC and SI actually reach the GBA connector.
//
// Loopback proved we can READ a line; it never proved our SC/SI OUTPUTS actually arrive at
// the GBA. If the GBA sees no clock it just cycles its watchdog forever — matching what we
// see. So: check the signals at the GBA end.
//
// This does NOT use PIO. It drives the raw pads at ~2 Hz so a multimeter on DC/continuity or
// a scope shows a clear square wave. Remember the Workshop OUTPUT stage INVERTS: writing 0 to
// the pad puts the line HIGH at the jack. We account for that so "logic high" = jack ~5V.
//
// WIRING: keep the GBA link wired. Measure at the GBA connector pins (after your series
// resistors): pin 5 (SC) and pin 3 (SI). Both should swing ~0V <-> ~5V (or ~3.3V after the
// resistor divider with the GBA's input) at 2 Hz, in ANTI-PHASE (one high while other low),
// so you can tell them apart.
//
// LEDs mirror the intended jack state so you can cross-check:
//   LED0 = SC intended HIGH     LED1 = SI intended HIGH
//   LED5 = heartbeat

#include "ComputerCard.h"
#include "hardware/gpio.h"

static constexpr uint SC_PIN  = 8;   // Pulse Out 1
static constexpr uint SI_PIN  = 9;   // Pulse Out 2

class PinToggle : public ComputerCard
{
public:
    PinToggle()
    {
        gpio_init(SC_PIN); gpio_set_dir(SC_PIN, GPIO_OUT);
        gpio_init(SI_PIN); gpio_set_dir(SI_PIN, GPIO_OUT);
    }

    void __not_in_flash_func(ProcessSample)() override
    {
        // ~2 Hz: toggle every ~24000 samples (48kHz/2/... ) -> flip every 12000 samples = 4Hz edge
        if (++n_ < 12000) return;
        n_ = 0;
        state_ = !state_;

        // Output stage inverts: to put the JACK high, write LOW (0) to the pad.
        // Drive SC and SI in ANTI-PHASE so they're distinguishable at the connector.
        bool scHigh = state_;
        bool siHigh = !state_;

        gpio_put(SC_PIN, !scHigh);   // invert for the output stage
        gpio_put(SI_PIN, !siHigh);

        LedOn(0, scHigh);
        LedOn(1, siHigh);
        LedOn(5, (blink_ ^= 1));
    }

private:
    int  n_ = 0;
    bool state_ = false;
    int  blink_ = 0;
};

int main()
{
    set_sys_clock_khz(144000, true);
    PinToggle card;
    card.Run();
}
