// mbrate.cpp — Which multiboot speed transfers RELIABLY? One variable, manual control.
//
// Andy's design, and a better instrument than the automatic sweep it replaces. The automatic
// ladder could never answer this: it tries rates fastest-first and keeps the first that
// works, so the "winner" is decided by whichever rung it happened to be on at the moment the
// console finished booting. Switch the GBA on a second later and a different rate wins. That
// is a measurement of timing luck, not of the link.
//
// This does exactly one thing: attempt the real multiboot upload, over and over, at ONE
// speed you choose by hand. You power-cycle the GBA, watch whether it boots, and step down a
// rung when it does not. What you end up with is the fastest rate that works *repeatedly*,
// which is the only definition of "reliable" that matters.
//
// ─────────────────────────────────────────────────────────────────────────────────────────
// SPEEDS — exactly one LED is lit, and it tells you which rung you are on.
//
//     LED0  500 kHz   <- starts here, the fastest
//     LED1  300 kHz
//     LED2  200 kHz
//     LED3  100 kHz
//     LED4   50 kHz
//     LED5   25 kHz
//
// THE LIT LED ALSO REPORTS THE RESULT:
//     BLINKING  = attempting, or the last attempt at this speed FAILED
//     SOLID     = the last attempt at this speed SUCCEEDED
//
// So "reliable" looks like: power-cycle the GBA several times and the LED goes solid every
// time. "Marginal" looks like it going solid sometimes and blinking others — which is exactly
// the behaviour that made the automatic ladder's answer wander.
//
// CONTROLS
//     Switch DOWN (click) = step to the NEXT SLOWER speed (wraps round to the fastest)
//     Switch UP   (click) = step to the next faster speed
// Changing speed clears the success flag, so a solid LED always refers to the speed shown.
//
// HOW TO USE
//     1. Flash this, leave the Workshop running.
//     2. Power-cycle the GBA. Watch the LED and the GBA screen.
//        - GBA boots to the payload and the LED goes solid  -> this speed works
//        - GBA sits on the logo and the LED keeps blinking   -> this speed does not
//     3. Repeat the power-cycle 3-4 times at the same speed before believing it. A single
//        success is not reliability; that assumption is what produced the wandering answer.
//     4. If it fails, click DOWN and repeat.
//
// Report the fastest LED that goes solid every time.

#include "ComputerCard.h"
#include "gba_spi.h"
#include "gba_multiboot.h"
#include "gba_payload.h"

#include "pico/stdlib.h"
#include "pico/multicore.h"

static const uint32_t kSpeeds[6] = { 500'000, 300'000, 200'000, 100'000, 50'000, 25'000 };

struct Shared {
    volatile uint8_t  slot     = 0;   // core 0 writes (switch), core 1 reads
    volatile bool     ok       = false;  // last attempt at THIS slot succeeded
    volatile bool     trying   = false;
    volatile uint32_t attempts = 0;
    volatile uint32_t okCount  = 0;
};
static Shared gS;

static void core1_entry()
{
    gba_spi_init(kSpeeds[0]);
    uint8_t lastSlot = 0xFF;

    for (;;) {
        uint8_t s = gS.slot;
        if (s != lastSlot) { lastSlot = s; gS.ok = false; }   // a result belongs to one speed

        gba_spi_set_clock(kSpeeds[s]);

        // Just attempt it. gba_multiboot_send() opens with its own bounded sync loop and
        // returns NoGBA promptly if the console is not listening yet, so a failed attempt
        // costs little and simply retries. An earlier version gated this on a pad-level
        // readiness check whose polarity was wrong, and consequently never attempted a
        // transfer at any speed.
        gS.trying = true;
        gS.attempts++;
        MultibootResult r = gba_multiboot_send(gba_payload, gba_payload_size);
        gS.trying = false;

        if (r == MultibootResult::Ok) {
            gS.ok = true;
            gS.okCount++;
            // The GBA is now running the payload and will not accept another image until it
            // is power-cycled. Idle in short steps so a speed change stays responsive.
            while (gS.slot == lastSlot) sleep_ms(50);
        } else {
            sleep_ms(200);
        }
    }
}

class MbRate : public ComputerCard
{
public:
    MbRate() { multicore_launch_core1(core1_entry); }

    void __not_in_flash_func(ProcessSample)() override
    {
        tick_++;

        Switch sw = SwitchVal();
        if (sw != lastSw_) {
            if (lastSw_ == Switch::Middle) {
                if      (sw == Switch::Down) gS.slot = (uint8_t)((gS.slot + 1) % 6);  // slower
                else if (sw == Switch::Up)   gS.slot = (uint8_t)((gS.slot + 5) % 6);  // faster
            }
            lastSw_ = sw;
        }

        // Exactly one LED lit: which speed. Solid = the last attempt here succeeded,
        // blinking = attempting or failed. One indicator, two facts, no ambiguity about
        // which speed a result refers to.
        for (int i = 0; i < 6; i++) LedOff(i);
        bool solid = gS.ok;
        LedOn(gS.slot, solid ? true : ((tick_ >> 12) & 1));
    }

private:
    uint32_t tick_ = 0;
    Switch lastSw_ = Switch::Middle;
};

int main()
{
    set_sys_clock_khz(144000, true);
    MbRate card;
    card.Run();
}
