// handshake.cpp — Walk the GBATEK multiboot handshake and report how far it gets.
//
// WHY THIS EXISTS
// ---------------
// The bench reached a state where the GBA is demonstrably alive and correctly wired:
//   * cablecheck MODE 0 shows the console driving Pulse In 1 in periodic bursts;
//   * cablecheck MODE 2 shows Pulse In 1 sitting LOW with no clocking, which is GBATEK's
//     master-init condition "Wait for SI to become LOW (slave ready)".
// ...yet linkcheck's 32-bit exchanges kept returning 0x00000000, which read as "dead line".
//
// It may not be dead at all. GBATEK's multiboot table says:
//     15x   6200   FFFF     Slave not in multiplay/normal mode yet
//     1     6200   0000     Slave entered correct mode now
//     1     610y   720x     Recognition okay, exchange master/slave info
//
// So 0x0000 is a DOCUMENTED, CORRECT reply meaning "slave entered the correct mode" — and
// it only appears in response to 6200. Our applet (and every diagnostic so far) sends 0x6202
// and spins waiting for 0x7202, never sending 0x6200 and never treating 0x0000 as progress.
// If the console has been answering correctly all along, we have been throwing the answer
// away. That is what this tool tests.
//
// It runs the handshake as an explicit state machine and lights an LED per stage REACHED,
// latching, so you can see exactly where it stalls instead of inferring it from one word.
//
// ─────────────────────────────────────────────────────────────────────────────────────────
// WIRING: the normal post-swap wiring. Nothing to move.
//   SC  -> Pulse Out 1 (1k)      GND -> Computer GND
//   the wire that carries the GBA's SO -> Pulse In 1      (no series resistor)
//   the wire that reaches the GBA's SI -> Pulse Out 2 (1k)
//
// LED4 + LED5 = MODE in binary, as in the other tools. Click DOWN for next, UP for previous.
//
// MODE 0 — GBATEK SEQUENCE      (LED4 off, LED5 off)  ** START HERE **
//   Sends 0x6200 until the slave reports ready, then 0x6102 for recognition.
// MODE 1 — REFERENCE SEQUENCE   (LED4 off, LED5 ON)
//   What the applet does today: 0x6202 repeatedly, waiting for 0x7202. For comparison.
// MODE 2 — GBATEK, SLOW         (LED4 ON, LED5 off)
//   As MODE 0 but ~1 kHz instead of ~16 kHz, for a marginal/slow line.
// MODE 3 — ALTERNATE            (LED4 ON, LED5 ON)
//   Switches between the two sequences every ~4 s. Use it if you are unsure and want to
//   leave it running.
//
// STAGE LEDS (all LATCH once reached, so nothing is missed while you look away):
//   LED0 = a NON-ZERO, non-0xFFFFFFFF reply was seen at all (the link carries something)
//   LED1 = saw 0xFFFF   -> slave present, not yet in the right mode  (GBATEK stage 1)
//   LED2 = saw 0x0000 in response to 6200 -> SLAVE ENTERED CORRECT MODE (GBATEK stage 2)
//   LED3 = saw 0x72xx   -> RECOGNITION. This is the one that matters. (GBATEK stage 3)
//
// Solid = true right now. Slow pulse = latched from earlier.
//
// Hold the switch UP ~1 s for the WORD READOUT of the most informative word seen:
//   LED0..3 = nibble in binary (LED0 = bit 0), most-significant nibble first
//   LED4    = bright for the top four nibbles, dim for the bottom four
//   LED5    = blinks between nibbles.  Click DOWN to leave.
//
// Reminder: Computer powered and clocking FIRST, then the GBA, cartridge-less, on the logo.

#include "ComputerCard.h"
#include "gba_spi.h"
#include "hardware/pio.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"
#include "gba_spi.pio.h"

class Handshake : public ComputerCard
{
public:
    Handshake() { enterMode(); }

    void __not_in_flash_func(ProcessSample)() override
    {
        tick_++;
        readSwitch();
        if (readout_) { runReadout(); return; }

        if (mode_ == 3 && ++altTick_ >= 192000) {   // ~4 s
            altTick_ = 0; useGbatek_ = !useGbatek_; resetSeq();
        }

        step();

        latched(0, live0_, sawAny_);
        latched(1, live1_, sawFFFF_);
        latched(2, live2_, saw0000_);
        latched(3, live3_, saw72xx_);
        LedOn(4, (mode_ >> 1) & 1);
        LedOn(5, mode_ & 1);
        live0_ = live1_ = live2_ = live3_ = false;
    }

private:
    void readSwitch()
    {
        Switch sw = SwitchVal();
        if (sw == Switch::Up) { if (++upHeld_ == 48000) { readout_ = !readout_; roIdx_ = 0; roTick_ = 0; } }
        else upHeld_ = 0;

        if (sw != lastSw_) {
            if (lastSw_ == Switch::Middle) {
                if (sw == Switch::Down) {
                    if (readout_) readout_ = false;
                    else { mode_ = (mode_ + 1) & 3; enterMode(); }
                } else if (sw == Switch::Up && !readout_) { mode_ = (mode_ + 3) & 3; enterMode(); }
            }
            lastSw_ = sw;
        }
    }

    void enterMode()
    {
        useGbatek_ = (mode_ != 1);
        rate_ = (mode_ == 2) ? 1000u : 16000u;
        loadPio();
        sawAny_ = sawFFFF_ = saw0000_ = saw72xx_ = false;
        richWord_ = 0; richEdges_ = 0;
        resetSeq();
    }

    void resetSeq() { stage_ = 0; tries_ = 0; busy_ = false; gap_ = 0; stall_ = 0; }

    void loadPio()
    {
        // Variant 0 (m3_edge) is the GBATEK-canonical timing: SC idles high, drive on the
        // falling edge, sample on the rising edge. 4 PIO cycles per bit.
        if (pioLoaded_) { pio_sm_set_enabled(GBA_PIO, GBA_SM, false); pio_remove_program(GBA_PIO, &gba_spi_m3_edge_program, off_); }
        off_ = pio_add_program(GBA_PIO, &gba_spi_m3_edge_program);
        pio_sm_config c = gba_spi_m3_edge_program_get_default_config(off_);
        sm_config_set_out_pins(&c, GBA_MOSI_PIN, 1);
        sm_config_set_in_pins(&c, GBA_MISO_PIN);
        sm_config_set_sideset_pins(&c, GBA_SCK_PIN);
        sm_config_set_out_shift(&c, false, true, 32);
        sm_config_set_in_shift(&c, false, true, 32);
        sm_config_set_clkdiv(&c, (float)clock_get_hz(clk_sys) / (4.0f * (float)rate_));
        pio_sm_set_pins_with_mask(GBA_PIO, GBA_SM, 0, (1u<<GBA_SCK_PIN)|(1u<<GBA_MOSI_PIN));
        pio_sm_set_pindirs_with_mask(GBA_PIO, GBA_SM,
            (1u<<GBA_SCK_PIN)|(1u<<GBA_MOSI_PIN),
            (1u<<GBA_SCK_PIN)|(1u<<GBA_MOSI_PIN)|(1u<<GBA_MISO_PIN));
        pio_gpio_init(GBA_PIO, GBA_SCK_PIN);
        pio_gpio_init(GBA_PIO, GBA_MOSI_PIN);
        pio_gpio_init(GBA_PIO, GBA_MISO_PIN);
        gpio_pull_up(GBA_MISO_PIN);                       // transistor bias; after pio_gpio_init
        gpio_set_outover(GBA_SCK_PIN,  GBA_SCK_OUTOVER);
        gpio_set_outover(GBA_MOSI_PIN, GBA_MOSI_OUTOVER);
        gpio_set_inover (GBA_MISO_PIN, GBA_MISO_INOVER);
        hw_set_bits(&GBA_PIO->input_sync_bypass, 1u << GBA_MISO_PIN);
        pio_sm_init(GBA_PIO, GBA_SM, off_, &c);
        pio_sm_set_enabled(GBA_PIO, GBA_SM, true);
        pioLoaded_ = true;
        busy_ = false;
    }

    void __not_in_flash_func(post)(uint32_t w)
    {
        if (pio_sm_is_tx_fifo_full(GBA_PIO, GBA_SM)) return;
        pio_sm_put(GBA_PIO, GBA_SM, w);
        busy_ = true; stall_ = 0;
    }

    bool __not_in_flash_func(poll)(uint32_t *out)
    {
        if (busy_ && ++stall_ > 48000) {          // 1 s: even 1 kHz needs only ~32 ms
            busy_ = false; stall_ = 0;
            pio_sm_clear_fifos(GBA_PIO, GBA_SM);
        }
        if (!busy_ || pio_sm_is_rx_fifo_empty(GBA_PIO, GBA_SM)) return false;
        *out = pio_sm_get(GBA_PIO, GBA_SM);
        busy_ = false;
        uint32_t e = *out ^ (*out >> 1);
        uint32_t n = (uint32_t)__builtin_popcount(e & 0x7FFFFFFFu);
        if (n > richEdges_) { richEdges_ = n; richWord_ = *out; }
        return true;
    }

    // Both halves are checked for every expected pattern. In SIO32 the reply can be
    // presented in either half depending on how the BIOS packs it, and which half matched
    // is itself diagnostic — so nothing is assumed about alignment.
    static bool half(uint32_t w, uint16_t v) { return (uint16_t)(w & 0xFFFF) == v || (uint16_t)(w >> 16) == v; }
    static bool half72(uint32_t w) { return ((w >> 8) & 0xFF) == 0x72 || ((w >> 24) & 0xFF) == 0x72; }

    void step()
    {
        uint32_t r;
        if (!poll(&r)) {
            // Inter-word gap. GBATEK: "After each transfer, master should wait for Start bit
            // cleared in SIOCNT register, followed by a 36us delay." We are far more generous
            // because this is not a hot path.
            if (!busy_ && ++gap_ >= 48) { gap_ = 0; post(nextWord()); }
            return;
        }

        if (r != 0 && r != 0xFFFFFFFFu) { sawAny_ = true; live0_ = true; }

        if (useGbatek_) {
            switch (stage_) {
            case 0:                                   // sending 0x6200, waiting for readiness
                if (half(r, 0xFFFF)) { sawFFFF_ = true; live1_ = true; }
                if (half(r, 0x0000)) {
                    // GBATEK: "Slave entered correct mode now". Only meaningful as a reply to
                    // 6200, which is exactly what we are sending here.
                    saw0000_ = true; live2_ = true;
                    if (++zeroRun_ >= 4) { stage_ = 1; tries_ = 0; }
                } else {
                    zeroRun_ = 0;
                }
                if (++tries_ > 600) { tries_ = 0; }    // keep trying; the GBA may still be booting
                break;

            case 1:                                   // sending 0x6102, expecting 0x72xx
                if (half72(r)) { saw72xx_ = true; live3_ = true; richWord_ = r; richEdges_ = 32; }
                if (++tries_ > 60) { stage_ = 0; tries_ = 0; zeroRun_ = 0; }  // fall back and retry
                break;
            }
        } else {
            // Reference-uploader sequence, for comparison: 0x6202 until 0x7202 appears.
            if (half(r, 0xFFFF)) { sawFFFF_ = true; live1_ = true; }
            if (half(r, 0x0000)) { saw0000_ = true; live2_ = true; }
            if (half72(r))       { saw72xx_ = true; live3_ = true; richWord_ = r; richEdges_ = 32; }
        }
    }

    uint32_t nextWord() const
    {
        if (!useGbatek_) return 0x00006202;
        return (stage_ == 0) ? 0x00006200u : 0x00006102u;
    }

    void latched(uint32_t led, bool live, bool latch)
    {
        if (live)       LedBrightness(led, 4095);
        else if (latch) LedBrightness(led, ((tick_ >> 12) & 1) ? 1800 : 120);
        else            LedOff(led);
    }

    void runReadout()
    {
        if (++roTick_ >= 58000) { roTick_ = 0; roIdx_ = (roIdx_ + 1) & 7; }
        bool gap = (roTick_ > 50000);
        uint32_t nib = (richWord_ >> (28 - 4 * roIdx_)) & 0xF;
        for (int i = 0; i < 4; i++) LedOn(i, !gap && ((nib >> i) & 1));
        LedBrightness(4, (roIdx_ < 4) ? 4095 : 250);
        LedOn(5, gap);
    }

    uint32_t tick_ = 0, gap_ = 0, stall_ = 0, upHeld_ = 0, altTick_ = 0;
    uint32_t roTick_ = 0, rate_ = 16000, richWord_ = 0, richEdges_ = 0;
    uint  off_ = 0;
    int   mode_ = 0, stage_ = 0, tries_ = 0, zeroRun_ = 0, roIdx_ = 0;
    bool  pioLoaded_ = false, busy_ = false, readout_ = false, useGbatek_ = true;
    bool  sawAny_ = false, sawFFFF_ = false, saw0000_ = false, saw72xx_ = false;
    bool  live0_ = false, live1_ = false, live2_ = false, live3_ = false;
    Switch lastSw_ = Switch::Middle;
};

int main()
{
    set_sys_clock_khz(144000, true);
    Handshake card;
    card.Run();
}
