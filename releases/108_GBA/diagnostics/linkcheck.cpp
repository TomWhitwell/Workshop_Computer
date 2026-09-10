// linkcheck.cpp — LED-only GBA link diagnostic. No scope, no held switch.
//
// Written because the two things the earlier diagnostics assumed are not available at the
// moment they matter most:
//   * the SWITCH is momentary-down — you cannot hold it while also handling a console;
//   * the SCOPE probes are in the way once the GBA is actually plugged in.
//
// So: the switch CLICKS to advance (edge-triggered, latched — press and let go), and every
// result is reported on the six LEDs, including the 32-bit reply word, which the older tools
// could only hint at.
//
// ─────────────────────────────────────────────────────────────────────────────────────────
// CONTROLS
//   Switch DOWN (click)  = next test / re-run current test
//   Switch UP   (click)  = previous test
//   Switch UP   (hold ~1s) = enter WORD READOUT for the last reply (see below)
//   Main knob            = SCK frequency (fully CCW ~1 kHz ... fully CW ~100 kHz)
//   Knob X               = sample edge   (CCW = trailing/cpha1, CW = leading/cpha0)
//   Knob Y               = SCK polarity  (CCW = normal, CW = inverted)
//
// ═══ LED4 + LED5 ALWAYS SHOW WHICH TEST YOU ARE IN (binary) ══════════════════════════
//     LED4  LED5   test
//      off   off   0 — IDLE / WIRING
//      off   ON    1 — LOOPBACK
//      ON    off   2 — LIVE GBA
//      ON    ON    3 — SWEEP
//   No test borrows these two for data. If you are unsure where you are, read LED4/LED5.
//   LEDs 0..3 carry the actual result.
//
// ─────────────────────────────────────────────────────────────────────────────────────────
// TEST 0 — IDLE / WIRING   (LED4 off, LED5 off)   safe with the GBA connected
//   Drives nothing; just watches SO at rest.
//   LED0 = SO reads HIGH right now.  GBA off -> should be SOLID (our pull-up holds it).
//          DARK = SO held low: a short to GND or a broken SO conductor.
//          BLINKING with the GBA on = the console is driving the line. Good.
//   LED1 = SO has been seen both high and low (real activity).
//
// TEST 1 — LOOPBACK   (LED4 off, LED5 ON)   jumper Pulse Out 2 -> Pulse In 1, GBA UNPLUGGED
//   Sends 0x00006202 through the jumper; expects it back bit-exact.
//   LED0 = exact match (GOOD).  LED1 = mismatch.  LED2 = stuck.  LED3 = bit-inverted.
//   With a GBA plugged in and no jumper this test FAILS by design — it is not testing the
//   console. LED1+LED2 here is the expected "no jumper" pattern, not a fault.
//
// TEST 2 — LIVE GBA   (LED4 ON, LED5 off)   GBA connected, cartridge-less, on the logo
//   Sends 0x00006202 at the knob-selected rate/edge/polarity.
//   LED0 = ECHO: low-16 of the reply == 0x6202.  THE KEY LIGHT — bit timing is right.
//   LED1 = SYNC: high-16 == 0x7202. Full multiboot recognition. The goal.
//   LED2 = the reply looks like real data (several bit transitions — see looksLikeData()).
//   LED3 = current SCK polarity: dim = normal, bright = inverted.
//   LED0/1/2 LATCH once seen: SOLID = true right now, PULSING = seen earlier.
//
// TEST 3 — SWEEP   (LED4 ON, LED5 ON)   GBA connected
//   Ignores the knobs; walks all 20 combinations of edge x polarity x 5 rates, ~0.7 s each
//   (a full pass is ~14 s — let it run 30 s).
//   LED0 = ECHO seen at some point.   LED1 = SYNC seen.
//   LED2 = polarity of the best slot so far.   LED3 = edge of the best slot so far.
//
// ─────────────────────────────────────────────────────────────────────────────────────────
// WORD READOUT (hold switch UP ~1s from any test; click DOWN to leave)
//   Clocks the last 32-bit reply out as 8 nibbles, most-significant first, ~1.2 s each:
//     LED0..3 = the nibble in binary (LED0 = bit 0 ... LED3 = bit 3 / the 8s place)
//     LED4    = BRIGHT for nibbles 1-4 (high half), DIM for nibbles 5-8 (low half)
//     LED5    = blinks between nibbles to separate them
//   Write down 8 hex digits. This is the only way to see WHAT the GBA replied rather than
//   just that it replied something.
//
// Reminder: power the COMPUTER first, then the GBA (the master must already be clocking
// when the console boots). Power-cycle the GBA, not the Computer, to retry.

#include "ComputerCard.h"
#include "gba_spi.h"
#include "hardware/pio.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"
#include "gba_spi.pio.h"

// The five sampling variants, with their true cycles-per-bit (they differ, so the clock
// divider must be computed per program — getting this wrong is what made every earlier
// requested rate off by 4/3).
//
// GBATEK, SIO Normal Mode: "During inactive transfer, the shift clock (SC) is high" and
// "When master sends SC=LOW, each master and slave must output the next outgoing data bit
// to SO. When master sends SC=HIGH, each master and slave must read out the opponents data
// bit from SI." => drive on the falling edge, sample on the rising edge = SPI mode 3.
// Index 0 is therefore the canonical one and is the DEFAULT.
struct SpiVariant { const pio_program_t *prog; float cycles; const char *name; };

class LinkCheck : public ComputerCard
{
public:
    void __not_in_flash_func(ProcessSample)() override
    {
        tick_++;
        readSwitch();

        if (readout_) { runReadout(); return; }

        switch (test_) {
        case 0: test0_idle();      break;
        case 1: test1_loopback();  break;
        case 2: test2_gba();       break;
        case 3: test3_sweep();     break;
        }
    }

private:
    // ── switch: edge-triggered so it never has to be held ────────────────────────────────
    void readSwitch()
    {
        Switch sw = SwitchVal();

        // Hold UP for ~1s to toggle the word readout.
        if (sw == Switch::Up) {
            if (++upHeld_ == 48000) { readout_ = !readout_; roIdx_ = 0; roTick_ = 0; }
        } else {
            upHeld_ = 0;
        }

        if (sw != lastSw_) {
            // Act on the transition INTO a position, then require a return to Middle before
            // the next one counts — that is what makes a momentary switch usable as a click.
            if (lastSw_ == Switch::Middle) {
                if (sw == Switch::Down) {
                    if (readout_) { readout_ = false; }
                    else { test_ = (test_ + 1) & 3; enterTest(); }
                } else if (sw == Switch::Up && !readout_) {
                    test_ = (test_ + 3) & 3; enterTest();
                }
            }
            lastSw_ = sw;
        }
    }

    void enterTest()
    {
        // Every test re-arms its own latches; the reply word deliberately survives so it can
        // still be read out after switching away from the test that produced it.
        echoLatch_ = syncLatch_ = structLatch_ = false;
        misoSeenHigh_ = misoSeenLow_ = false;
        cnt_ = 0; busy_ = false; stall_ = 0; stalls_ = 0; richEdges_ = 0; richWord_ = 0; sweepSlot_ = 0; sweepTick_ = 0; bestSlot_ = 0; bestScore_ = 0;

        if (test_ == 0) {
            // Idle test drives nothing: hand the pins back to plain SIO inputs so we can
            // watch what the far end is doing without contributing to it.
            if (pioLoaded_) { pio_sm_set_enabled(GBA_PIO, GBA_SM, false); pioLoaded_ = false; }
            gpio_init(GBA_MISO_PIN);
            gpio_set_dir(GBA_MISO_PIN, GPIO_IN);
            gpio_set_function(GBA_MISO_PIN, GPIO_FUNC_SIO);
            gpio_pull_up(GBA_MISO_PIN);          // Pulse In 1 transistor bias — dead without it
            gpio_set_inover(GBA_MISO_PIN, GBA_MISO_INOVER);
        } else {
            loadPio(wantVariant());
        }
    }

    // ── knobs ────────────────────────────────────────────────────────────────────────────
    // Knob range is 0..4095. Treat the outer thirds as the two choices so a roughly-centred
    // knob is never ambiguous.
    // Hysteresis: a knob parked near the middle jitters by tens of counts on the ADC, and
    // without a dead band that thrashed loadPio() continuously.
    // Knob X picks the sampling variant (5 detented zones across the travel). Fully CCW is
    // the GBATEK-canonical one, which is where you should start.
    int wantVariant() { int32_t v = KnobVal(Knob::X); int i = (v * kNumVariants) / 4096; return (i < 0) ? 0 : (i >= kNumVariants ? kNumVariants - 1 : i); }
    bool wantSckInvert()   { int32_t v = KnobVal(Knob::Y); if (v > 2600) polSel_  = true; else if (v < 1500) polSel_  = false; return polSel_; }

    uint32_t knobRate()
    {
        // ~1 kHz .. ~100 kHz, geometric-ish so the low end (where the slow transistor input
        // is most likely to behave) gets most of the travel.
        uint32_t k = (uint32_t)KnobVal(Knob::Main);      // 0..4095
        return 1000u + (k * k) / 170u;                   // 1k .. ~99k
    }

    static const SpiVariant *variants()
    {
        static const SpiVariant v[5] = {
            { &gba_spi_m3_edge_program,  4.0f, "m3-edge (GBATEK canonical)" },
            { &gba_spi_m3_hold_program,  5.0f, "m3-hold (sample end of low)" },
            { &gba_spi_m3_late_program,  6.0f, "m3-late (extra settling)"   },
            { &gba_spi_m3_wide_program, 12.0f, "m3-wide (slow edges)"       },
            { &gba_spi_cpha0_program,    4.0f, "cpha0 (GBATEK says wrong)"  },
        };
        return v;
    }
    static constexpr int kNumVariants = 5;

    // ── PIO ──────────────────────────────────────────────────────────────────────────────
    void loadPio(int idx)
    {
        if (idx < 0 || idx >= kNumVariants) idx = 0;
        if (pioLoaded_) { pio_sm_set_enabled(GBA_PIO, GBA_SM, false); pio_remove_program(GBA_PIO, prog_, off_); }
        prog_ = variants()[idx].prog;
        off_  = pio_add_program(GBA_PIO, prog_);
        // All five share the same pin/shift configuration; only the timing differs, so a
        // hand-rolled config is fine and avoids five *_get_default_config() branches.
        pio_sm_config c = pio_get_default_sm_config();
        sm_config_set_wrap(&c, off_, off_ + prog_->length - 1);
        sm_config_set_sideset(&c, 1, false, false);
        sm_config_set_sideset_pins(&c, GBA_SCK_PIN);
        sm_config_set_out_pins(&c, GBA_MOSI_PIN, 1);
        sm_config_set_in_pins(&c, GBA_MISO_PIN);
        sm_config_set_sideset_pins(&c, GBA_SCK_PIN);
        sm_config_set_out_shift(&c, false, true, 32);
        sm_config_set_in_shift(&c, false, true, 32);
        sm_config_set_clkdiv(&c, clkdiv(curRate_, variants()[idx].cycles));

        pio_sm_set_pins_with_mask(GBA_PIO, GBA_SM, 0, (1u<<GBA_SCK_PIN)|(1u<<GBA_MOSI_PIN));
        pio_sm_set_pindirs_with_mask(GBA_PIO, GBA_SM,
            (1u<<GBA_SCK_PIN)|(1u<<GBA_MOSI_PIN),
            (1u<<GBA_SCK_PIN)|(1u<<GBA_MOSI_PIN)|(1u<<GBA_MISO_PIN));
        pio_gpio_init(GBA_PIO, GBA_SCK_PIN);
        pio_gpio_init(GBA_PIO, GBA_MOSI_PIN);
        pio_gpio_init(GBA_PIO, GBA_MISO_PIN);
        gpio_pull_up(GBA_MISO_PIN);                       // must follow pio_gpio_init
        applySck(sckInv_);
        gpio_set_outover(GBA_MOSI_PIN, GBA_MOSI_OUTOVER); // shared constants — gba_spi.h
        gpio_set_inover (GBA_MISO_PIN, GBA_MISO_INOVER);
        hw_set_bits(&GBA_PIO->input_sync_bypass, 1u << GBA_MISO_PIN);
        pio_sm_init(GBA_PIO, GBA_SM, off_, &c);
        pio_sm_set_enabled(GBA_PIO, GBA_SM, true);
        pioLoaded_ = true;
        progIdx_ = idx;
        // The SM was just torn down and re-inited, so any word we thought was in flight is
        // gone. Not clearing this wedged test 2 permanently: poll() waited for a reply that
        // could never arrive and post() refused to start another because busy_ was stuck.
        busy_ = false;
    }

    static float clkdiv(uint32_t hz, float cycles)
    {
        float d = (float)clock_get_hz(clk_sys) / (cycles * (float)hz);
        return d < 1.0f ? 1.0f : d;
    }
    float curCycles() const { return variants()[progIdx_].cycles; }

    void applySck(bool inv)
    {
        // GBA_SCK_OUTOVER is the measured resting value; the sweep needs to try its opposite,
        // so this is the one override the diagnostic is allowed to flip.
        gpio_set_outover(GBA_SCK_PIN, inv ? GPIO_OVERRIDE_INVERT : GPIO_OVERRIDE_NORMAL);
        sckInv_ = inv;
    }

    // NON-BLOCKING transport. ProcessSample() has a 20.8 us budget and a 32-bit word takes
    // hundreds of us to milliseconds, so a blocking xfer here would stall the audio callback
    // and leave the SM half-fed. Post, return, collect later.
    void __not_in_flash_func(post)(uint32_t w)
    {
        if (pio_sm_is_tx_fifo_full(GBA_PIO, GBA_SM)) return;
        pio_sm_put(GBA_PIO, GBA_SM, w);
        busy_ = true;
        // Watch SO independently of the shift register while the burst is in flight: it
        // catches a GBA that is driving the line even when the sampled bits are garbage.
        misoSeenHigh_ = misoSeenLow_ = false;
    }

    bool __not_in_flash_func(poll)(uint32_t *out)
    {
        if (busy_) {
            if (gpio_get(GBA_MISO_PIN)) misoSeenHigh_ = true; else misoSeenLow_ = true;
            // Watchdog. Even the slowest rate (1 kHz) completes a 32-bit word in ~32 ms
            // = ~1540 samples, so 48000 samples (1 s) means the transfer is never finishing.
            // Re-arm instead of hanging: a wedged diagnostic looks exactly like dead hardware.
            if (++stall_ > 48000) { stall_ = 0; busy_ = false; stalls_++; pio_sm_clear_fifos(GBA_PIO, GBA_SM); }
        } else {
            stall_ = 0;
        }
        if (!busy_ || pio_sm_is_rx_fifo_empty(GBA_PIO, GBA_SM)) return false;
        *out = pio_sm_get(GBA_PIO, GBA_SM);
        lastWord_ = *out;
        // Keep the most INFORMATIVE word seen, not merely the most recent. A sweep that ends
        // on a dead slot was overwriting real evidence with 0x00000000, so the word readout
        // showed all-zeros even after a structured reply had been captured earlier.
        uint32_t e = *out ^ (*out >> 1);
        uint32_t n = (uint32_t)__builtin_popcount(e & 0x7FFFFFFFu);
        if (n > richEdges_) { richEdges_ = n; richWord_ = *out; }
        busy_ = false;
        return true;
    }

    // A reply "looks like data" only if it has several 0<->1 transitions. The old test was
    // just (r != 0 && r != ~0), which reported 0xFFFF0000 — a line sitting high then falling
    // once, i.e. NOT being driven by the GBA at all — as a structured reply. That actively
    // misled a bench session, so require real bit activity.
    static bool looksLikeData(uint32_t r)
    {
        uint32_t edges = r ^ (r >> 1);
        return __builtin_popcount(edges & 0x7FFFFFFFu) >= 3;
    }

    // ── TEST 0: idle wiring check ────────────────────────────────────────────────────────
    void test0_idle()
    {
        bool hi = gpio_get(GBA_MISO_PIN);
        if (hi) misoSeenHigh_ = true; else misoSeenLow_ = true;
        LedOn(0, hi);
        LedOn(1, misoSeenHigh_ && misoSeenLow_);
        LedOff(2); LedOff(3);
        showMode();
    }

    // ── TEST 1: loopback ─────────────────────────────────────────────────────────────────
    void test1_loopback()
    {
        retuneIfKnobsMoved();
        uint32_t got;
        if (!poll(&got)) {
            if (!busy_ && ++cnt_ >= 2000) { cnt_ = 0; post(0x00006202); }
            return;
        }
        const uint32_t sent = 0x00006202;
        LedOn(0, got == sent);
        LedOn(1, got != sent);
        LedOn(2, got == 0 || got == 0xFFFFFFFF);
        LedOn(3, got == ~sent);
        showMode();
    }

    // ── TEST 2: live GBA ─────────────────────────────────────────────────────────────────
    void test2_gba()
    {
        retuneIfKnobsMoved();
        uint32_t r;
        if (!poll(&r)) {
            if (!busy_ && ++cnt_ >= 400) { cnt_ = 0; post(0x00006202); }
            // Keep the latched view alive between words so the LEDs do not flicker off.
            paintTest2(false, false, false);
            return;
        }
        bool echo = ((r & 0xFFFF) == 0x6202);
        bool sync = ((r >> 16) == 0x7202);
        bool structured = looksLikeData(r);
        if (echo) echoLatch_ = true;
        if (sync) syncLatch_ = true;
        if (structured) structLatch_ = true;
        paintTest2(echo, sync, structured);
    }

    // Live-now = solid; latched-earlier = pulsing. Lets one good reply be seen minutes later
    // without pretending it is still happening.
    void paintTest2(bool echo, bool sync, bool structured)
    {
        latched(0, echo, echoLatch_);
        latched(1, sync, syncLatch_);
        latched(2, structured, structLatch_);
        // LED3 = current SCK polarity (dim normal / bright inverted) — but if the transport
        // is stalling (no reply ever arriving) it FLASHES FAST instead. That distinguishes
        // "the GBA is silent" from "our own transfer never completed", which the LEDs
        // previously could not tell apart: both looked like four dark LEDs.
        if (stalls_ > 0) LedOn(3, (tick_ >> 10) & 1);
        else             LedBrightness(3, sckInv_ ? 4095 : 250);
        showMode();
    }

    // ── TEST 3: auto sweep ───────────────────────────────────────────────────────────────
    void test3_sweep()
    {
        // 5 variants x 2 polarities x 5 rates = 50 slots, ~0.7 s each => a full pass is
        // ~35 s. Let it run a full minute before concluding anything.
        static const uint32_t kRates[5] = { 1000, 5000, 16000, 50000, 100000 };
        const int kSlots = kNumVariants * 2 * 5;

        if (++sweepTick_ >= 34000) {
            sweepTick_ = 0;
            sweepSlot_ = (sweepSlot_ + 1) % kSlots;
            int rateIx = sweepSlot_ % 5;
            bool inv   = ((sweepSlot_ / 5) & 1) != 0;
            int varIx  = (sweepSlot_ / 10) % kNumVariants;
            curRate_ = kRates[rateIx];
            if (varIx != progIdx_) {
                loadPio(varIx);
            } else {
                pio_sm_set_clkdiv(GBA_PIO, GBA_SM, clkdiv(curRate_, curCycles()));
                pio_sm_clkdiv_restart(GBA_PIO, GBA_SM);
            }
            applySck(inv);
            busy_ = false; stall_ = 0;
        }

        uint32_t r;
        if (!poll(&r)) {
            if (!busy_ && ++cnt_ >= 400) { cnt_ = 0; post(0x00006202); }
        } else {
            bool echo = ((r & 0xFFFF) == 0x6202);
            bool sync = ((r >> 16) == 0x7202);
            bool structured = looksLikeData(r);
            if (echo) echoLatch_ = true;
            if (sync) syncLatch_ = true;
            if (structured) structLatch_ = true;
            int score = (sync ? 8 : 0) + (echo ? 4 : 0) + (structured ? 1 : 0);
            if (score > bestScore_) { bestScore_ = score; bestSlot_ = sweepSlot_; bestWord_ = r; }
        }

        latched(0, false, echoLatch_);
        latched(1, false, syncLatch_);
        latched(2, false, structLatch_);
        // LED3 = "a best slot has been recorded". Read WHICH slot out with the word readout
        // (hold UP): the readout shows the best slot number as its first nibble when the
        // sweep has found something, then the winning word.
        LedOn(3, bestScore_ > 0);
        showMode();
    }

    // ── WORD READOUT ─────────────────────────────────────────────────────────────────────
    void runReadout()
    {
        // ~1.2 s per nibble, with a short gap so two identical nibbles are distinguishable.
        if (++roTick_ >= 58000) { roTick_ = 0; roIdx_ = (roIdx_ + 1) & 7; }
        bool gap = (roTick_ > 50000);
        // In the sweep, read out the WINNING word (and its slot) rather than whatever the
        // sweep happened to be trying when you pressed the switch.
        // Priority: a scoring word from the sweep > the richest word ever seen > the last
        // word. So the readout always shows the best evidence available.
        uint32_t w = (test_ == 3 && bestScore_ > 0) ? bestWord_
                   : (richEdges_ >= 3)              ? richWord_
                                                    : lastWord_;
        uint32_t nib = (roIdx_ == 0 && test_ == 3 && bestScore_ > 0)
                     ? (uint32_t)(bestSlot_ & 0xF)        // first nibble = winning slot & 15
                     : (w >> (28 - 4 * roIdx_)) & 0xF;
        for (int i = 0; i < 4; i++) LedOn(i, !gap && ((nib >> i) & 1));
        LedBrightness(4, (roIdx_ < 4) ? 4095 : 250);   // bright = high half, dim = low half
        LedOn(5, gap);                                  // blinks between nibbles
    }

    // ── LED helpers ──────────────────────────────────────────────────────────────────────
    void latched(uint32_t led, bool live, bool latch)
    {
        if (live)       LedBrightness(led, 4095);
        else if (latch) LedBrightness(led, ((tick_ >> 12) & 1) ? 1800 : 120);  // slow pulse
        else            LedOff(led);
    }

    // LED4+LED5 are ALWAYS the test number in binary (LED4 = bit1, LED5 = bit0), in every
    // test and in the readout. Being lost about which mode you are in wasted real bench time,
    // so no test is allowed to borrow these two for data any more.
    //   LED4 LED5   test
    //    off  off   0  idle/wiring
    //    off  ON     1  loopback
    //    ON   off   2  live GBA
    //    ON   ON    3  sweep
    // In the WORD READOUT both are driven differently (see runReadout) — that is the one
    // place they mean something else, and the readout is unmistakable anyway.
    void showMode()
    {
        LedOn(4, (test_ >> 1) & 1);
        LedOn(5, test_ & 1);
    }

    void retuneIfKnobsMoved()
    {
        // Re-read the knobs periodically rather than every sample: changing the divider
        // mid-word would corrupt the transfer in flight.
        if (busy_ || (++knobTick_ & 0x3FF)) return;
        uint32_t r = knobRate();
        int want = wantVariant();
        bool inv = wantSckInvert();
        if (want != progIdx_) { curRate_ = r; loadPio(want); }
        else if (r != curRate_) { curRate_ = r; pio_sm_set_clkdiv(GBA_PIO, GBA_SM, clkdiv(r, curCycles())); pio_sm_clkdiv_restart(GBA_PIO, GBA_SM); }
        if (inv != sckInv_) applySck(inv);
    }

    // ── state ────────────────────────────────────────────────────────────────────────────
    const pio_program_t *prog_ = nullptr;
    uint off_ = 0;
    uint32_t tick_ = 0, cnt_ = 0, knobTick_ = 0, upHeld_ = 0;
    uint32_t sweepTick_ = 0, roTick_ = 0;
    uint32_t curRate_ = 25000;
    uint32_t lastWord_ = 0, bestWord_ = 0, richWord_ = 0, richEdges_ = 0;
    int  test_ = 0, sweepSlot_ = 0, bestSlot_ = 0, bestScore_ = 0, roIdx_ = 0;
    bool pioLoaded_ = false, busy_ = false, sckInv_ = false;
    int  progIdx_ = 0;
    bool echoLatch_ = false, syncLatch_ = false, structLatch_ = false;
    bool polSel_ = false;
    uint32_t stall_ = 0, stalls_ = 0;
    bool misoSeenHigh_ = false, misoSeenLow_ = false, readout_ = false;
    Switch lastSw_ = Switch::Middle;

public:
    LinkCheck() { enterTest(); }
};

int main()
{
    set_sys_clock_khz(144000, true);
    LinkCheck card;
    card.Run();
}
