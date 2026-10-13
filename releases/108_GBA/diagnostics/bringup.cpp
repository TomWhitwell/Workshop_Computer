// bringup.cpp — Staged, scope-friendly bring-up diagnostic for GBA.
//
// One tool that walks the hardware bring-up in order. The 3-position SWITCH picks the stage;
// the LEDs give per-stage status; the SCOPE watches the actual signals on the jacks. Do the
// stages in order and don't move on until each is clean.
//
// ─────────────────────────────────────────────────────────────────────────────────────────
// STAGE 1  — SWITCH UP — "SIGNAL GENERATOR" (GBA DISCONNECTED)
//   Slowly square-waves SC (Pulse Out 1) and SI (Pulse Out 2) in ANTI-PHASE at ~4 Hz so you
//   can SCOPE each output and confirm it is a CLEAN 0V ↔ ~6V logic swing (NOT -2V, not ringing,
//   actually reaches ~6V). Also drives a faster ~10 kHz burst on SC every second so you can
//   check edge quality at speed.
//   Scope: SC = Pulse Out 1 jack, SI = Pulse Out 2 jack, ground clip on Computer GND.
//   LEDs:  LED0/LED1 mirror the intended SC/SI level (cross-check against the scope).
//          LED5 heartbeat.
//   PASS when: both outputs are clean 0..~6V squares on the scope. THEN also scope Computer-GND
//          to GBA-GND (with GBA connected+on) separately — must be flat 0V (common ground).
//
// STAGE 2  — SWITCH MIDDLE — "LOOPBACK" (JUMPER Pulse Out 2 → Pulse In 1, GBA DISCONNECTED)
//   Runs the real PIO transport and checks we read back exactly what we send through the jumper.
//   This re-proves the transport end-to-end after any rewiring.
//   LEDs:  LED0 = exact match (GOOD).  LED1 = mismatch.  LED2 = dead/stuck read.
//          LED3 = inverted read.  LED5 heartbeat.
//   PASS when: LED0 solid.
//
// STAGE 3  — SWITCH DOWN — "LIVE GBA HANDSHAKE" (real GBA link, cartridge-less, on the logo)
//   Sends 0x00006202 continuously with the leading-edge program (gba_spi_cpha0). Watches the
//   GBA's reply. Because normal-mode echoes our low-16 back, a correct link returns 0x????6202.
//   Scope: SC burst of 32 clean pulses per word; SO = GBA replying during the burst.
//   LEDs:  LED0 = low-16 ECHO == 0x6202  (SPI timing correct — the key light).
//          LED1 = full 0x7202 recognition (SYNC!).  LED2 = structured reply seen.
//          LED4 = SCK polarity currently applied (auto-toggles ~2s so both get tried).
//          LED5 heartbeat.
//   PASS when: LED0 lights (timing right) and ideally LED1 (full sync).
//
// Reminder: power the COMPUTER first, then the GBA (master must be clocking when GBA boots).

#include "ComputerCard.h"
#include "gba_spi.h"
#include "hardware/pio.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"
#include "gba_spi.pio.h"

static constexpr uint SC_PIN  = 8;   // Pulse Out 1
static constexpr uint SI_PIN  = 9;   // Pulse Out 2

class Bringup : public ComputerCard
{
public:
    Bringup() {}

    void __not_in_flash_func(ProcessSample)() override
    {
        Switch sw = SwitchVal();

        // Re-init hardware only when the stage changes (each stage owns the pins differently).
        if (sw != lastSw_) { enterStage(sw); lastSw_ = sw; }

        switch (sw) {
        case Switch::Up:     stage1_siggen();   break;
        case Switch::Middle: stage2_loopback(); break;
        case Switch::Down:   stage3_gba();       break;
        }
    }

private:
    // ---- stage entry / teardown ----
    void enterStage(Switch sw)
    {
        // If leaving a PIO stage, disable the SM so raw GPIO stages have the pins.
        if (pioLoaded_) { pio_sm_set_enabled(GBA_PIO, GBA_SM, false); }

        if (sw == Switch::Up) {
            // raw GPIO for the signal generator
            gpio_init(SC_PIN); gpio_set_dir(SC_PIN, GPIO_OUT); gpio_set_function(SC_PIN, GPIO_FUNC_SIO);
            gpio_init(SI_PIN); gpio_set_dir(SI_PIN, GPIO_OUT); gpio_set_function(SI_PIN, GPIO_FUNC_SIO);
            gpio_set_outover(SC_PIN, GPIO_OVERRIDE_NORMAL);
            gpio_set_outover(SI_PIN, GPIO_OVERRIDE_NORMAL);
        } else {
            // PIO transport for loopback / GBA. Stage 2 uses cpha1 (like the applet baseline);
            // stage 3 uses cpha0 (leading-edge, the one that got structured bits from the GBA).
            loadPio(sw == Switch::Down /* leadingEdge */);
        }
        cnt_ = 0; sckInv_ = 0; busy_ = false;
    }

    // ---- STAGE 1: signal generator ----
    void stage1_siggen()
    {
        // ~4 Hz anti-phase square on SC/SI, with a 10 kHz burst on SC once per cycle.
        if (++cnt_ >= 6000) { cnt_ = 0; state_ = !state_; }

        bool scHigh, siHigh;
        // For ~1/8 of the cycle, emit a fast burst on SC so you can scope edge quality at speed.
        if (state_ && cnt_ < 750) {
            bool fast = (cnt_ & 1);          // toggles every ProcessSample = 24 kHz-ish edges
            scHigh = fast;  siHigh = false;
        } else {
            scHigh = state_;  siHigh = !state_;
        }
        gpio_put(SC_PIN, !scHigh);           // Workshop output stage inverts: write !level
        gpio_put(SI_PIN, !siHigh);

        LedOn(0, scHigh);
        LedOn(1, siHigh);
        LedOff(2); LedOff(3); LedOff(4);
        LedOn(5, (cnt_ >> 9) & 1);
    }

    // ---- STAGE 2: loopback (jumper PU2out -> PU1in) ----
    void stage2_loopback()
    {
        // Collect a pending reply first; only start a new word when idle and the gap elapsed.
        uint32_t got;
        if (!poll(&got)) {
            if (!busy_ && ++cnt_ >= 2000) { cnt_ = 0; post(0x00006202); }
            return;
        }
        const uint32_t sent = 0x00006202;
        bool match = (got == sent);
        bool dead  = (got == 0 || got == 0xFFFFFFFF);
        bool inv   = (got == ~sent);
        LedOn(0, match);
        LedOn(1, !match);
        LedOn(2, dead);
        LedOn(3, inv && !match);
        LedOff(4);
        LedOn(5, (beat_ ^= 1));
    }

    // ---- STAGE 3: live GBA handshake ----
    void stage3_gba()
    {
        uint32_t r;
        if (!poll(&r)) {
            if (!busy_ && ++cnt_ >= 200) {
                cnt_ = 0;
                // auto-toggle SCK polarity ~every 2s so both get tried. Only ever changed
                // between words, never mid-transfer.
                if (++polCnt_ >= 480) { polCnt_ = 0; sckInv_ ^= 1;
                    gpio_set_outover(GBA_SCK_PIN,
                        sckInv_ ? GPIO_OVERRIDE_INVERT : GPIO_OVERRIDE_NORMAL); }
                post(0x00006202);
            }
            return;
        }
        uint16_t lo = r & 0xFFFF;
        uint16_t hi = r >> 16;
        bool echo = (lo == 0x6202);
        bool sync = (hi == 0x7202);
        if (echo) echoSeen_ = true;
        if (sync) syncSeen_ = true;

        LedOn(0, echo || echoSeen_);
        LedOn(1, sync || syncSeen_);
        LedOn(2, (hi != 0x0000 && hi != 0xFFFF) || (lo != 0x0000 && lo != 0xFFFF));
        LedOff(3);
        LedOn(4, sckInv_);
        LedOn(5, (beat_ ^= 1));
    }

    // ---- PIO helpers ----
    void loadPio(bool leadingEdge)
    {
        if (pioLoaded_) { pio_remove_program(GBA_PIO, prog_, off_); }
        prog_ = leadingEdge ? &gba_spi_cpha0_program : &gba_spi_program;
        off_ = pio_add_program(GBA_PIO, prog_);
        pio_sm_config c = leadingEdge ? gba_spi_cpha0_program_get_default_config(off_)
                                      : gba_spi_program_get_default_config(off_);
        sm_config_set_out_pins(&c, GBA_MOSI_PIN, 1);
        sm_config_set_in_pins(&c, GBA_MISO_PIN);
        sm_config_set_sideset_pins(&c, GBA_SCK_PIN);
        sm_config_set_out_shift(&c, false, true, 32);
        sm_config_set_in_shift(&c, false, true, 32);
        sm_config_set_clkdiv(&c, (float)clock_get_hz(clk_sys) / (GBA_PIO_CYCLES_PER_BIT * 50'000u));
        pio_sm_set_pins_with_mask(GBA_PIO, GBA_SM, 0, (1u<<GBA_SCK_PIN)|(1u<<GBA_MOSI_PIN));
        pio_sm_set_pindirs_with_mask(GBA_PIO, GBA_SM,
            (1u<<GBA_SCK_PIN)|(1u<<GBA_MOSI_PIN),
            (1u<<GBA_SCK_PIN)|(1u<<GBA_MOSI_PIN)|(1u<<GBA_MISO_PIN));
        pio_gpio_init(GBA_PIO, GBA_SCK_PIN);
        pio_gpio_init(GBA_PIO, GBA_MOSI_PIN);
        pio_gpio_init(GBA_PIO, GBA_MISO_PIN);
        gpio_pull_up(GBA_MISO_PIN);                                   // Pulse In 1 transistor bias
        gpio_set_outover(GBA_SCK_PIN,  GBA_SCK_OUTOVER);   // shared constants, gba_spi.h
        gpio_set_outover(GBA_MOSI_PIN, GBA_MOSI_OUTOVER);
        gpio_set_inover (GBA_MISO_PIN, GBA_MISO_INOVER);
        hw_set_bits(&GBA_PIO->input_sync_bypass, 1u << GBA_MISO_PIN);
        pio_sm_init(GBA_PIO, GBA_SM, off_, &c);
        pio_sm_set_enabled(GBA_PIO, GBA_SM, true);
        pioLoaded_ = true;
    }

    // NON-BLOCKING transport. ProcessSample() runs at 48 kHz (20.8 us budget); a 32-bit word
    // at 37.5 kHz takes ~854 us, so the old put_blocking/get_blocking pair stalled the audio
    // callback for ~41 sample periods. The DMA IRQ then re-entered mid-transfer and the SM
    // was left half-fed — which is exactly the "SC idles high with only 2-3 clock pulses"
    // the scope showed. Post a word, return immediately, collect it on a later callback.
    // (The real applet does this on core 1, where blocking is fine; only the diagnostic
    // lives on core 0, so only the diagnostic needed fixing.)
    void __not_in_flash_func(post)(uint32_t w)
    {
        if (pio_sm_is_tx_fifo_full(GBA_PIO, GBA_SM)) return;
        pio_sm_put(GBA_PIO, GBA_SM, w);
        busy_ = true;
    }

    // Returns true and fills `out` once a reply word is available.
    bool __not_in_flash_func(poll)(uint32_t *out)
    {
        if (!busy_ || pio_sm_is_rx_fifo_empty(GBA_PIO, GBA_SM)) return false;
        *out = pio_sm_get(GBA_PIO, GBA_SM);
        busy_ = false;
        return true;
    }

    Switch lastSw_ = (Switch)0xFF;
    const pio_program_t *prog_ = nullptr;
    uint off_ = 0;
    int  cnt_ = 0, polCnt_ = 0, sckInv_ = 0;
    bool state_ = false, beat_ = false, pioLoaded_ = false, busy_ = false;
    bool echoSeen_ = false, syncSeen_ = false;
};

int main()
{
    set_sys_clock_khz(144000, true);
    Bringup card;
    card.Run();
}
