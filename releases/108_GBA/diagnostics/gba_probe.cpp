// gba_probe.cpp — Report the RAW received word (cpha0 program) as voltages, at selectable
// slow clocks, to distinguish "input too slow" from "bit misalignment".
//
// Findings so far: only the cpha0 (leading-edge sample) program returns structured bits, but
// the low-16 never cleanly echoes 0x6202. Two candidate causes:
//   A) the Pulse In 1 transistor input can't track the GBA's 3.3V SO edges at 50 kHz (bits
//      smear) -> slowing the clock should fix it.
//   B) the sampled bits are whole-bit misaligned -> the returned value is a ROTATION of
//      0x6202, fixable in the PIO, and slowing won't change it.
//
// So: fix cpha0, MISO normal, MOSI invert. The SWITCH sets the clock:
//   UP     = 25 kHz
//   MIDDLE =  5 kHz
//   DOWN   =  1 kHz   (very slow — if echo only works here, it's input bandwidth)
//
// Continuously send 0x6202 and output the RAW 32-bit reply as four voltages so we can read
// the exact value with a multimeter (black probe to module ground):
//   CV Out 1 = bits 31..24   CV Out 2 = bits 23..16
// (LEDs give the low 16 bits so we can also see the echo half.)
//   Expected on a PERFECT link: reply = 0x7202_6202  -> CV1≈2.68V(0x72) CV2≈0.05V(0x02),
//   and LEDs show low half 0x6202.
//
// LED0 = SCK invert currently applied (we also sweep SCK slowly to find the right polarity):
//   hold each SCK polarity ~3s; LED5 heartbeat. LED1..4 show low-nibble-ish of the reply.
// Tell me: the two CV voltages, and whether they're STABLE or jumping, at each switch pos.

#include "ComputerCard.h"
#include "gba_spi.h"
#include "hardware/pio.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"
#include "gba_spi.pio.h"

class GbaProbe : public ComputerCard
{
public:
    GbaProbe() { load(); }

    void __not_in_flash_func(ProcessSample)() override
    {
        // NON-BLOCKING: see the note in bringup.cpp. At the 1 kHz setting a 32-bit word takes
        // ~32 ms — blocking ProcessSample() for that long wedged the transport entirely.
        uint32_t r;
        if (!poll(&r)) {
            if (!busy_ && ++tick_ >= 200) {
                tick_ = 0;
                beat_ = !beat_;
                // slow SCK-polarity toggle so both get tried (~3s each). Between words only.
                if (++polTick_ >= 720) { polTick_ = 0; sckInv_ ^= 1; applySck(); }
                uint32_t hz = (SwitchVal() == Switch::Up)     ? 25'000u
                            : (SwitchVal() == Switch::Middle) ?  5'000u
                                                               :  1'000u;
                pio_sm_set_clkdiv(GBA_PIO, GBA_SM,
                                  (float)clock_get_hz(clk_sys) / (GBA_PIO_CYCLES_PER_BIT * hz));
                post();
            }
            return;
        }
        uint8_t b3 = (r >> 24) & 0xFF;
        uint8_t b2 = (r >> 16) & 0xFF;
        uint16_t lo = r & 0xFFFF;

        CVOut1((int16_t)((b3 * 2047) / 255));
        CVOut2((int16_t)((b2 * 2047) / 255));

        bool echoGood = (lo == 0x6202);
        LedOn(0, sckInv_);
        LedOn(1, echoGood);              // low-16 echo == 0x6202
        LedOn(2, (r >> 16) == 0x7202);   // full recognition
        LedOn(3, lo & 0x0100);           // some low-half bit, as liveness
        LedOn(4, b3 != 0 && b3 != 0xFF); // structured top byte
        LedOn(5, beat_);
    }

private:
    void applySck()
    {
        gpio_set_outover(GBA_SCK_PIN, sckInv_ ? GPIO_OVERRIDE_INVERT : GPIO_OVERRIDE_NORMAL);
    }

    void load()
    {
        off_ = pio_add_program(GBA_PIO, &gba_spi_cpha0_program);
        pio_sm_config c = gba_spi_cpha0_program_get_default_config(off_);
        sm_config_set_out_pins(&c, GBA_MOSI_PIN, 1);
        sm_config_set_in_pins(&c, GBA_MISO_PIN);
        sm_config_set_sideset_pins(&c, GBA_SCK_PIN);
        sm_config_set_out_shift(&c, false, true, 32);
        sm_config_set_in_shift(&c, false, true, 32);
        sm_config_set_clkdiv(&c, (float)clock_get_hz(clk_sys) / (GBA_PIO_CYCLES_PER_BIT * 25'000u));

        pio_sm_set_pins_with_mask(GBA_PIO, GBA_SM, 0, (1u<<GBA_SCK_PIN)|(1u<<GBA_MOSI_PIN));
        pio_sm_set_pindirs_with_mask(GBA_PIO, GBA_SM,
            (1u<<GBA_SCK_PIN)|(1u<<GBA_MOSI_PIN),
            (1u<<GBA_SCK_PIN)|(1u<<GBA_MOSI_PIN)|(1u<<GBA_MISO_PIN));
        pio_gpio_init(GBA_PIO, GBA_SCK_PIN);
        pio_gpio_init(GBA_PIO, GBA_MOSI_PIN);
        pio_gpio_init(GBA_PIO, GBA_MISO_PIN);
        gpio_pull_up(GBA_MISO_PIN);
        applySck();
        gpio_set_outover(GBA_MOSI_PIN, GBA_MOSI_OUTOVER);   // shared constants, gba_spi.h
        gpio_set_inover (GBA_MISO_PIN, GBA_MISO_INOVER);
        hw_set_bits(&GBA_PIO->input_sync_bypass, 1u << GBA_MISO_PIN);
        pio_sm_init(GBA_PIO, GBA_SM, off_, &c);
        pio_sm_set_enabled(GBA_PIO, GBA_SM, true);
    }

    void __not_in_flash_func(post)()
    {
        if (pio_sm_is_tx_fifo_full(GBA_PIO, GBA_SM)) return;
        pio_sm_put(GBA_PIO, GBA_SM, 0x00006202);
        busy_ = true;
    }

    bool __not_in_flash_func(poll)(uint32_t *out)
    {
        if (!busy_ || pio_sm_is_rx_fifo_empty(GBA_PIO, GBA_SM)) return false;
        *out = pio_sm_get(GBA_PIO, GBA_SM);
        busy_ = false;
        return true;
    }

    int  tick_ = 0, polTick_ = 0, sckInv_ = 0;
    uint off_ = 0;
    bool beat_ = false, busy_ = false;
};

int main()
{
    set_sys_clock_khz(144000, true);
    GbaProbe card;
    card.Run();
}
