// loopback.cpp — Link transport diagnostic for GBA.
//
// Two-stage test, selected by the toggle switch:
//
//   SWITCH UP/MIDDLE = RAW GPIO loopback (no PIO):
//     Bit-bangs GPIO 9 (Pulse Out 2) directly and reads GPIO 2 (Pulse In 1) with gpio_get,
//     applying the same hardware inversions in software. This proves the *physical* path
//     (jack -> jack) and the input transistor bias work, independent of any PIO config.
//
//   SWITCH DOWN = PIO loopback:
//     Uses the real applet transport (gba_spi_init / gba_spi_xfer32) exactly as multiboot does.
//
// WIRING: patch Pulse Out 2 (GPIO 9) -> Pulse In 1 (GPIO 2). GBA disconnected.
//
// LEDs (both modes):
//   LED0 = read matched what we drove   -> GOOD
//   LED1 = mismatch                      -> BAD
//   LED2 = read stuck all-same (dead)    -> input sees nothing
//   LED3 = read is inverse of driven     -> one inversion wrong
//   LED4 = mode: on = PIO, off = raw GPIO
//   LED5 = liveness heartbeat (toggles each update)

#include "ComputerCard.h"
#include "gba_spi.h"
#include "hardware/gpio.h"

static constexpr uint32_t kTestHz = 100'000;

class Loopback : public ComputerCard
{
public:
    Loopback()
    {
        // Start in raw-GPIO mode; we (re)configure pins per-mode in ProcessSample.
        pio_inited_ = false;
        setup_raw_gpio();
    }

    void __not_in_flash_func(ProcessSample)() override
    {
        if (++div_ < 2000) return;   // ~24 Hz
        div_ = 0;
        heartbeat_ = !heartbeat_;

        bool pioMode = (SwitchVal() == Switch::Down);

        bool match, inverted, dead;
        if (pioMode) {
            if (!pio_inited_) { gba_spi_init(kTestHz); pio_inited_ = true; }
            uint32_t sent = 0x00006202;
            uint32_t got  = gba_spi_xfer32(sent);
            match    = (got == sent);
            inverted = (got == ~sent);
            dead     = (got == 0 || got == 0xFFFFFFFF);
        } else {
            // ---- RAW GPIO loopback ----
            // Drive GPIO 9 with a known bit, read GPIO 2, undo the hardware inversions in SW.
            // Output stage inverts: to put logical 1 on the wire, write 0 to the pad.
            // Input stage inverts: logical value = !gpio_get.
            // So driving logical b and reading logical should give b back.
            int good = 0, bad = 0, sawHigh = 0, sawLow = 0;
            for (int b = 0; b < 8; b++) {
                int bit = (0x6202 >> (b & 1)) & 1;   // alternate 0/1 pattern-ish
                bit = (b & 1);                        // clean 0,1,0,1...
                gpio_put(9, !bit);                    // output inversion
                // small settle for the slow transistor input
                busy_wait_us(50);
                int rd = !gpio_get(2);                // input inversion
                if (rd) sawHigh++; else sawLow++;
                if (rd == bit) good++; else bad++;
            }
            match    = (good == 8);
            inverted = (bad == 8);            // every bit came back wrong = inverted
            dead     = (sawHigh == 8 || sawLow == 8);  // never changed = stuck
        }

        LedOn(0, match);
        LedOn(1, !match);
        LedOn(2, dead);
        LedOn(3, inverted && !match);
        LedOn(4, pioMode);
        LedOn(5, heartbeat_);
    }

private:
    void setup_raw_gpio()
    {
        // GPIO 9 = output (Pulse Out 2), GPIO 2 = input with pull-up (Pulse In 1 transistor bias).
        gpio_init(9); gpio_set_dir(9, GPIO_OUT); gpio_put(9, true); // idle: wire low
        gpio_init(2); gpio_set_dir(2, GPIO_IN);  gpio_pull_up(2);
    }

    int  div_ = 0;
    bool heartbeat_ = false;
    bool pio_inited_ = false;
};

int main()
{
    set_sys_clock_khz(144000, true);
    Loopback card;
    card.Run();
}
