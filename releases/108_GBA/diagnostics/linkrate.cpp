// linkrate.cpp — How fast can the POST-BOOT link run, and how reliably?
//
// Boots the GBA at 100 kHz, the rate mbrate.uf2 proved repeatable, then lets you step the
// LIVE link speed by hand while the GBA reports the result on its own screen.
//
// This replaces bandwidth.uf2's automatic sweep, which never produced a usable number. The
// sweep tried to decide "clean or not" itself, so a fault anywhere in the harness came back
// as a bare zero that could not be told from a genuine ceiling of zero. Stepping one rate at
// a time and reading a live word count is slower to run and far harder to fool.
//
// Multiboot rate is FIXED at 100 kHz on purpose: it is the one number already established by
// repeated measurement, so a failure here always means the live link, never the upload.
//
// ─────────────────────────────────────────────────────────────────────────────────────────
// WHAT THIS ACTUALLY MEASURES — and why it changed
//
// The first version stepped the SCK rate and sent words BACK-TO-BACK with no gap. It showed
// heavy corruption even at 50 kHz, which contradicted a link that had been working reliably
// the day before. The contradiction was the clue: the working link polled every 5000 us.
//
// The GBA slave holds exactly ONE pending transfer, and it re-arms with a read-modify-write
// of SIOCNT. With no inter-word gap the host starts clocking the next word before the slave
// has re-armed, so that race fires on nearly every word — producing exactly the dropped words
// and bit-slips observed. SCK was never the limiting variable; the slave's TURNAROUND is.
//
// So this now sweeps the WORD RATE at a fixed, proven 100 kHz SCK. That is also the number
// the project actually needs: how many control updates per second the link sustains.
//
// WORKSHOP LEDS — exactly one lit, showing the word rate. YOU choose it.
//     LED0  100 Hz     LED1  200 Hz   <- 200 Hz is what the working link used
//     LED2  500 Hz     LED3    1 kHz
//     LED4    2 kHz    LED5    3 kHz  <- 3 kHz is the ceiling at 100 kHz SCK (320 us/word)
// SOLID while upstream replies are framed correctly, BLINKING when the host sees bad words.
// While the payload uploads, the current LED pulses slowly.
//
// WHAT THE GBA SHOWS — a 0 -> 0FFF ramp (4096 words)
//     The bar fills as the pass advances; a break turns it amber and marks the point in red.
//     DROPPED counts small gaps (lost words), SLIP counts wild jumps (bit-shift on re-arm),
//     CORRUPT counts words matching neither magic. CLEAN/BAD passes below.
//     The ramp is 4096 words rather than 65536 because at 100 Hz a full 16-bit ramp would
//     take five and a half minutes.
//     The screen FREEZES during a pass: drawing makes the payload deaf, so measuring and
//     drawing cannot overlap without measuring our own redraws instead of the link.
//
// CONTROLS
//     DOWN click = next rate up      UP click = previous rate down
//
// PROTOCOL
//     Host -> GBA   0xA5A5 <value:16>   the ramp, +1 per word; a gap is a dropped word
//                   0xA6A6 <rateHz:16>  current WORD rate in Hz, for display and reset
//     GBA  -> host  0x600D <value:16>   where the GBA thinks the ramp has got to

#include "ComputerCard.h"
#include "gba_spi.h"
#include "gba_multiboot.h"
#include "gba_payload.h"

#include "pico/stdlib.h"
#include "pico/multicore.h"

static constexpr uint32_t kMultibootHz = 100'000;   // proven by mbrate.uf2; do not raise
// WORD rates, not clock rates. SCK stays at the proven 100 kHz throughout; what is being
// swept is how hard the slave is pushed, because that is the real constraint.
static const uint32_t kWordHz[6] = { 100, 200, 500, 1000, 2000, 3000 };

// Inter-word gap for a given word rate, minus the ~320 us the 32-bit word itself takes at
// 100 kHz. Clamped at zero for the top rung, which is therefore the back-to-back case.
static uint32_t gapUsFor(uint32_t wordHz)
{
    uint32_t period = 1'000'000u / wordHz;
    return (period > 340u) ? (period - 340u) : 0u;
}

struct Shared {
    volatile uint8_t  slot     = 1;   // start at 100 kHz, the known-good rate
    volatile bool     linkUp   = false;
    volatile bool     upstreamOk = false;
    volatile uint32_t gbaErrors = 0;  // as reported back by the GBA
    volatile uint32_t sent      = 0;
};
static Shared gS;

static void core1_entry()
{
    gba_spi_init(kMultibootHz);

    for (;;) {
        gS.linkUp = false;
        gba_spi_set_clock(kMultibootHz);
        if (gba_multiboot_send(gba_payload, gba_payload_size) != MultibootResult::Ok) {
            sleep_ms(200);
            continue;
        }
        gS.linkUp = true;
        sleep_ms(150);                       // let the payload reach its loop

        uint8_t  lastSlot = 0xFF;
        uint32_t seq = 0, sinceRate = 0, badRun = 0;
        (void)badRun;   // counted for clarity, never acted on — see the note below

        for (;;) {
            uint8_t s = gS.slot;
            if (s != lastSlot) {
                lastSlot = s;
                sinceRate = 0;
                // Announce the new rate immediately and repeatedly: the GBA resets its counts
                // when the rate changes, and until it hears about it the figures on screen
                // would describe a blend of two rates.
                for (int i = 0; i < 8; i++) {
                    gba_spi_xfer32(0xA6A60000u | kWordHz[s]);
                    sleep_us(2000);      // generous: this happens once, and must not be missed
                }
            }

            // Ramp a 16-bit counter 0 -> 0xFFFF, over and over. The GBA plots how far each
            // pass gets, so a full clean sweep is visible as a full bar.
            uint32_t r = gba_spi_xfer32(0xA5A50000u | (seq & 0x0FFFu));
            seq++;
            gS.sent = seq;

            // THE GAP IS THE POINT. Without it the slave never finishes re-arming before the
            // next word starts, which is what made every rate look broken.
            uint32_t g = gapUsFor(kWordHz[s]);
            if (g) sleep_us(g);

            if ((r >> 16) == 0x600D) {
                gS.upstreamOk = true;
                gS.gbaErrors  = r & 0xFFFF;
                badRun = 0;
            } else {
                gS.upstreamOk = false;
                // NEVER tear the link down from here. The GBA is briefly deaf whenever it
                // redraws, so runs of bad replies are normal and are precisely what is being
                // measured. An earlier 4000-word limit fired during ordinary screen updates,
                // dropped back to re-run multiboot, failed (the console was already booted)
                // and left the LEDs sweeping the "still uploading" pattern forever — which
                // looked like the tool cycling speeds on its own.
                badRun++;
            }

            // Re-announce the rate about once a second so a missed announcement cannot leave
            // the display permanently wrong.
            // The periodic rate refresh needs THE SAME GAP as a ramp word. Without one, the
            // ramp word that follows it lands back-to-back and the slave has not re-armed —
            // so the refresh itself destroyed a word.
            //
            // This was the "dropped 2, corrupt 4" baseline Andy measured on every single pass
            // from 100 to 2000 words/s. It fires every 2000 words, so a 4096-word pass
            // contains exactly two of them: two ruined words, every pass, independent of rate.
            // A word counter, not a timer, which is precisely why the figure never moved.
            if (++sinceRate > 2000) {
                sinceRate = 0;
                gba_spi_xfer32(0xA6A60000u | kWordHz[s]);
                if (g) sleep_us(g);
            }
        }
    }
}

class LinkRate : public ComputerCard
{
public:
    LinkRate() { multicore_launch_core1(core1_entry); }

    void __not_in_flash_func(ProcessSample)() override
    {
        tick_++;
        Switch sw = SwitchVal();
        if (sw != lastSw_) {
            if (lastSw_ == Switch::Middle) {
                if      (sw == Switch::Down) gS.slot = (uint8_t)((gS.slot + 1) % 6);
                else if (sw == Switch::Up)   gS.slot = (uint8_t)((gS.slot + 5) % 6);
            }
            lastSw_ = sw;
        }

        for (int i = 0; i < 6; i++) LedOff(i);
        if (!gS.linkUp) {
            // Uploading. Deliberately NOT a sweep across all six LEDs any more: that looked
            // exactly like the tool stepping through speeds by itself, which is the one thing
            // this tool must never appear to do — choosing the speed is the operator's job.
            // A single slow pulse on the current slot is unambiguous.
            LedOn(gS.slot, (tick_ >> 13) & 1);
            return;
        }
        // Solid = upstream replies are framed correctly; blinking = the host is seeing bad
        // words. The GBA screen reports the downstream direction, so between the two every
        // direction is covered.
        LedOn(gS.slot, gS.upstreamOk ? true : ((tick_ >> 12) & 1));
    }

private:
    uint32_t tick_ = 0;
    Switch lastSw_ = Switch::Middle;
};

int main()
{
    set_sys_clock_khz(144000, true);
    LinkRate card;
    card.Run();
}
