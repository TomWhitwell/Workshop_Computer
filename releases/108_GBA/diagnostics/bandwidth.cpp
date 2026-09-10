// bandwidth.cpp — How fast can this link actually go?
//
// Phase 0 of the post-bring-up plan. Everything we might build next depends on four numbers
// nobody has measured yet:
//
//   1. Max SCK rate   -> decides how long a payload takes to upload. This is the big one: a
//                        menu-of-apps payload is tens of KB, and at the 1 kHz multiboot rung
//                        a 32 KB image takes over FOUR MINUTES. At 100 kHz it takes five
//                        seconds. Same architecture, completely different instrument.
//   2. Max poll rate  -> how many control updates per second the link sustains.
//   3. Round-trip     -> the control-to-sound delay. Decides whether note timing is musical.
//   4. Multiboot rung -> which rate the applet's ladder actually settled on.
//
// STRUCTURE. Unlike the other diagnostics this one runs the link on CORE 1, exactly like the
// real applet (gba_link.cpp), because multiboot blocks for seconds and must never sit on the
// 48 kHz audio callback. Core 0 is ComputerCard: it reads the switch and drives the LEDs from
// the shared struct. Blocking transfers are correct on core 1 and are used deliberately here.
//
// It needs the payload's BENCH mode (payload/main.c): two consecutive 0xBE7CBE7C words put
// the GBA into a tight echo loop that draws nothing, so what we measure is the transport and
// not the UI. In bench mode the GBA echoes the low 16 bits of the word it just received,
// which is what lets us verify the DOWNSTREAM direction — a tag check alone only ever proves
// the GBA->host path, and the two directions can have different ceilings.
//
// The echo LAGS BY ONE TRANSFER: the reply for word N+1 is preloaded while word N is being
// harvested. All the checks below account for that.
//
// ─────────────────────────────────────────────────────────────────────────────────────────
// LED4 + LED5 = MODE in binary (LED4 = bit1, LED5 = bit0), as in every other tool here.
//
// SWITCH:
//   * DOWN click     -> advance to the next mode. ARRIVING IN A MODE RUNS ITS TEST; LED0
//                       pulses while it works and LED3 lights when it is finished. To re-run
//                       a test, click all the way round to it again.
//   * UP held ~1 s   -> toggle the word readout, showing the current mode's number.
//                       UP does nothing else - it must not change mode, or reaching for the
//                       readout would move you off the result you wanted to read.
//
//   MODE 0 — SCK CEILING     (LED4 off, LED5 off)
//   MODE 1 — POLL CEILING    (LED4 off, LED5 ON )
//   MODE 2 — ROUND TRIP      (LED4 ON,  LED5 off)
//   MODE 3 — REPORT          (LED4 ON,  LED5 ON )
//
// While a test runs: LED0 pulses as a progress heartbeat, LED3 lights when the test finishes.
// LED1 = link is up (multiboot done).
// LED2 = BENCH ECHO VERIFIED. This one matters: it is checked once, right after multiboot, at
//        the rate multiboot itself just succeeded at. If LED2 is DARK then the GBA is not
//        echoing and every sweep result will be zero for a reason that has nothing to do with
//        the rates being tested — check the payload is the current build.
//
// WORD READOUT (hold UP ~1 s): eight nibbles, most significant first, LED0..3 = the nibble in
// binary, LED4 bright for the top half / dim for the bottom, LED5 blinks between nibbles.
// Which number it shows depends on the mode you were in — see kReadout below. Values are in
// plain decimal-ish hex: e.g. max SCK is shown in Hz, so 100 kHz reads 000186A0.

#include "ComputerCard.h"
#include "gba_spi.h"
#include "gba_multiboot.h"
#include "gba_payload.h"

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/gpio.h"

// ── shared state, core 1 -> core 0 ───────────────────────────────────────────────────────
// Same discipline as GbaShared: one writer per field, volatile, nothing wider than a word.
struct Bench {
    volatile uint8_t  mode      = 0;   // written by core 0 (switch), read by core 1
    volatile bool     linkUp    = false;
    volatile bool     busy      = false;
    volatile bool     done      = false;
    volatile bool     benchOk   = false; // the echo path itself is verified working
    volatile uint32_t curHz     = 0;   // rate currently under test
    volatile uint32_t maxSckHz  = 0;   // result of mode 0
    volatile uint32_t maxPollHz = 0;   // result of mode 1
    volatile uint32_t rttUs     = 0;   // result of mode 2
    volatile uint32_t mbHz      = 0;   // multiboot rung that worked
    volatile uint32_t beat      = 0;   // progress heartbeat
    volatile uint32_t lastReply = 0;   // last raw word from the GBA, for the readout
};
static Bench gB;

// The ladder we sweep. Deliberately reaches well past the 100 kHz the applet used to
// hardcode — nobody has ever established the real ceiling, and the whole point is to find it.
static const uint32_t kRates[] = {
    1'000, 2'000, 5'000, 10'000, 16'000, 25'000, 50'000,
    75'000, 100'000, 150'000, 200'000, 300'000, 500'000
};
static constexpr int kNumRates = (int)(sizeof(kRates) / sizeof(kRates[0]));

static constexpr uint32_t kBenchEnter = 0xBE7CBE7Cu;
static constexpr uint32_t kBenchLeave = 0xBE7C0000u;
static constexpr uint16_t kTag        = 0x600D;

// ── core 1: the measurement engine ───────────────────────────────────────────────────────

static void enterBench()
{
    // The payload needs THREE of these to land, and it is deaf while it redraws — a window
    // far longer than the 200 us this used to allow between words, so most of the burst was
    // simply missed. Send plenty, spaced wider than a frame.
    for (int i = 0; i < 24; i++) { gba_spi_xfer32(kBenchEnter); sleep_ms(2); }
}

static void leaveBench()
{
    for (int i = 0; i < 24; i++) { gba_spi_xfer32(kBenchLeave); sleep_ms(2); }
}

// How many words to exchange at a given rate. A fixed count made the 1 kHz rung take
// ~13 s (400 words x 32 ms), long enough that it looked hung. Scale it so every rung costs
// roughly the same wall-clock time.
static uint32_t wordsFor(uint32_t hz)
{
    uint32_t n = hz / 500;
    if (n < 32)  n = 32;
    if (n > 400) n = 400;
    return n;
}

// Run `n` echo exchanges at the current clock and return how many came back wrong.
// `gapUs` is the inter-word delay; 0 means "as fast as the transport allows".
static uint32_t testRate(uint32_t n, uint32_t gapUs)
{
    uint32_t bad = 0;
    uint16_t expect = 0;
    bool haveExpect = false;

    for (uint32_t i = 0; i < n; i++) {
        uint16_t send = (uint16_t)(i * 2654435761u);      // spread the bits about
        if (send == (uint16_t)(kBenchEnter & 0xFFFF)) send ^= 1;   // never re-trigger bench
        if (send == (uint16_t)(kBenchLeave & 0xFFFF)) send ^= 1;

        uint32_t r = gba_spi_xfer32(0xBE7D0000u | send);   // 0xBE7D: not a bench control word
        gB.lastReply = r;

        // The 16-bit tag is the primary corruption detector and it needs NO bench handshake:
        // if 0x600D survives at rate X, the upstream path is clean at rate X. Gating the whole
        // measurement on the echo was over-engineering, and it meant a handshake problem read
        // as a transport ceiling of zero — twice.
        //
        // Downstream is already proven independently: multiboot CRC-checks the entire uploaded
        // image, and it completed at 200 kHz. The echo is therefore a bonus cross-check, only
        // applied once bench mode is actually confirmed.
        if ((r >> 16) != kTag) { bad++; haveExpect = false; }
        else if (gB.benchOk && haveExpect && (uint16_t)(r & 0xFFFF) != expect) bad++;

        expect = send;
        haveExpect = true;
        if (gapUs) sleep_us(gapUs);
        gB.beat = i;
    }
    return bad;
}

// MODE 0 — highest SCK with a clean run.
static void modeSck()
{
    uint32_t best = 0;
    for (int i = 0; i < kNumRates; i++) {
        gB.curHz = kRates[i];
        gba_spi_set_clock(kRates[i]);
        sleep_ms(5);
        // A short warm-up first: the very first word after a clock change can be ragged and
        // should not condemn an otherwise good rate.
        testRate(16, 0);
        uint32_t bad = testRate(wordsFor(kRates[i]), 0);
        if (bad == 0) best = kRates[i];   // keep going: test every rate independently
    }
    gB.maxSckHz = best;
}

// MODE 1 — sustained words/sec. Uses the SCK found by mode 0 (or a safe default) and shrinks
// the inter-word gap until errors appear. What this really measures is the GBA slave's
// re-arm latency: it can hold only ONE pending transfer and is deaf until re-armed, which is
// the exact fault that made the first working link drop out.
static void modePoll()
{
    uint32_t sck = gB.maxSckHz ? gB.maxSckHz : 50'000;
    gba_spi_set_clock(sck);
    gB.curHz = sck;

    static const uint32_t kGaps[] = { 2000, 1000, 500, 250, 120, 60, 30, 15, 8, 4, 0 };
    uint32_t bestHz = 0;
    for (uint32_t g : kGaps) {
        uint32_t t0 = time_us_32();
        uint32_t bad = testRate(300, g);
        uint32_t dt  = time_us_32() - t0;
        if (bad == 0 && dt) bestHz = (300u * 1'000'000u) / dt;
        else break;
    }
    gB.maxPollHz = bestHz;
}

// MODE 2 — real round-trip in microseconds: host word out, echo of it back.
// Because the echo lags one transfer, a full round trip is two exchanges.
static void modeRtt()
{
    uint32_t sck = gB.maxSckHz ? gB.maxSckHz : 50'000;
    gba_spi_set_clock(sck);
    gB.curHz = sck;

    uint32_t total = 0, n = 0;
    for (int i = 0; i < 200; i++) {
        uint16_t send = (uint16_t)(0x1000 + i);
        uint32_t t0 = time_us_32();
        gba_spi_xfer32(0xBE7D0000u | send);              // word out
        uint32_t r = gba_spi_xfer32(0xBE7D0000u | send); // echo of it comes back here
        uint32_t dt = time_us_32() - t0;
        if ((r >> 16) == kTag && (uint16_t)(r & 0xFFFF) == send) { total += dt; n++; }
        gB.beat = (uint32_t)i;
    }
    gB.rttUs = n ? (total / n) : 0;
}

static void core1_entry()
{
    gba_spi_init(kRates[0]);
    uint32_t lostRun_ = 0;      // consecutive keep-alive words with a bad tag

    // Bring the GBA up exactly the way the applet does — walk the rate ladder until multiboot
    // completes — and record which rung won. That number IS result #4.
    for (;;) {
        gB.linkUp = false;

        // Wait for the console to actually BE ready before starting the ladder. Without this
        // the winning rung is decided by when the GBA finished booting rather than by what the
        // link can sustain, and it changes run to run.
        gba_wait_slave_ready(10'000);

        MultibootResult r = MultibootResult::NoGBA;
        for (int i = kNumRates - 1; i >= 0; i--) {          // fastest first
            gB.curHz = kRates[i];
            gba_spi_set_clock(kRates[i]);
            r = gba_multiboot_send(gba_payload, gba_payload_size);
            if (r == MultibootResult::Ok) { gB.mbHz = kRates[i]; break; }
        }
        if (r != MultibootResult::Ok) { sleep_ms(250); continue; }

        gB.linkUp = true;
        sleep_ms(100);            // let the payload reach its loop

        // Drop to a conservative rate for the handshake. Multiboot succeeding at 200 kHz does
        // NOT mean the post-boot link is reliable there: multiboot verifies every word and
        // waits 300 us between them, while this link does neither.
        gba_spi_set_clock(16'000);
        enterBench();

        // benchOk is maintained continuously by the keep-alive below, which re-asserts the
        // enter word every idle poll and reads the echo back. LED2 therefore tracks the live
        // state rather than a single measurement taken once at boot.

        // Service run requests from core 0 until the link is lost.
        uint8_t lastMode = 0xFF;
        for (;;) {
            // MODE 3 is the report screen, so hand the GBA back its normal UI while you read
            // the results off the LEDs — and take it back into bench mode on the way out.
            uint8_t m = gB.mode;
            if (m != lastMode) {
                if (m == 3)                     leaveBench();
                else if (lastMode == 3)         enterBench();
                lastMode = m;

                // Entering a mode RUNS it. DOWN used to mean "run" or "advance" depending
                // on whether the test had finished — one button with two meanings gated on
                // state you cannot see. Now DOWN only ever advances, and arriving somewhere
                // is what starts the work.
                if (m < 3) {
                    gB.busy = true; gB.done = false;

                    // WAIT for bench mode to be confirmed before measuring. Mode 0 runs the
                    // instant the link comes up, which is a race against the payload actually
                    // entering bench: if it loses, every rate scores as failed and the result
                    // reads zero for a reason that has nothing to do with the rates. Assert
                    // the enter word until the echo comes back, then measure.
                    for (int w = 0; w < 500 && !gB.benchOk; w++) {
                        uint32_t k = gba_spi_xfer32(kBenchEnter);
                        if ((k >> 16) == kTag)
                            gB.benchOk = ((uint16_t)(k & 0xFFFF) == (uint16_t)(kBenchEnter & 0xFFFF));
                        sleep_ms(2);
                    }
                    switch (m) {
                        case 0: modeSck();  break;
                        case 1: modePoll(); break;
                        case 2: modeRtt();  break;
                    }
                    gB.busy = false; gB.done = true;

                    // Put the clock back to a rate we KNOW works before resuming the
                    // keep-alive. modeSck() leaves it wherever the sweep stopped, which is by
                    // definition a failing rate, and the keep-alive would then decide the
                    // link had dropped and pointlessly re-run multiboot.
                    gba_spi_set_clock(gB.mbHz ? gB.mbHz : 50'000);
                }
            }
            // The idle keep-alive IS the bench-enter word. Sending the burst once at boot
            // gave the payload a ~96 ms window and no second chance: BE7D0000 was then the
            // only word it ever saw again, which is exactly what Andy observed on screen.
            // Re-asserting it every idle poll means entry cannot be missed, and the reply
            // tells us the answer for free: in bench mode the payload echoes the low half,
            // so 0xBE7C coming back is positive confirmation rather than an assumption.
            if (gB.mode != 3) {
                uint32_t k = gba_spi_xfer32(kBenchEnter);
                if ((k >> 16) != kTag) {
                    if (++lostRun_ > 200) { lostRun_ = 0; break; }   // link gone: re-multiboot
                } else {
                    lostRun_ = 0;
                    gB.benchOk = ((uint16_t)(k & 0xFFFF) == (uint16_t)(kBenchEnter & 0xFFFF));
                }
            }
            sleep_ms(2);
        }
    }
}

// ── core 0: ComputerCard UI ──────────────────────────────────────────────────────────────
class BandwidthCard : public ComputerCard
{
public:
    BandwidthCard() { multicore_launch_core1(core1_entry); }

    void __not_in_flash_func(ProcessSample)() override
    {
        tick_++;
        readSwitch();
        if (readout_) { runReadout(); return; }

        LedOn(0, gB.busy && ((tick_ >> 11) & 1));   // progress heartbeat
        LedOn(1, gB.linkUp);
        LedOn(2, gB.benchOk);
        LedOn(3, gB.done);
        LedOn(4, (gB.mode >> 1) & 1);
        LedOn(5, gB.mode & 1);
    }

private:
    void readSwitch()
    {
        Switch sw = SwitchVal();
        if (sw == Switch::Up) { if (++upHeld_ == 48000) { readout_ = !readout_; roIdx_ = 0; roTick_ = 0; } }
        else upHeld_ = 0;

        if (sw != lastSw_) {
            if (lastSw_ == Switch::Middle && sw == Switch::Down) {
                if (readout_) readout_ = false;
                else { gB.mode = (uint8_t)((gB.mode + 1) & 3); gB.done = false; }
            }
            // UP DELIBERATELY DOES NOT CHANGE MODE. It used to cycle backwards, which fired
            // the instant the switch moved — so reaching for the readout silently jumped the
            // mode (0 -> 3), dropped the GBA out of bench mode, and then showed mode 3's
            // number instead of the one you were looking at. UP is now only the readout hold.
            lastSw_ = sw;
        }
    }

    // Which measurement the readout shows, per mode.
    // A measurement of zero is not a result, it is a symptom — and on its own it cannot say
    // whether the link is broken, the handshake failed, or the test never ran. When a result
    // is zero the readout falls back to the last RAW reply word, which always carries the
    // answer: 0x600Dxxxx means the link is fine and the fault is in this tool; anything else
    // shows what the GBA is really sending.
    uint32_t readoutValue() const
    {
        uint32_t v;
        switch (gB.mode) {
            case 0:  v = gB.maxSckHz;  break;
            case 1:  v = gB.maxPollHz; break;
            case 2:  v = gB.rttUs;     break;
            default: v = gB.mbHz;      break;
        }
        return v ? v : gB.lastReply;
    }

    void runReadout()
    {
        if (++roTick_ >= 58000) { roTick_ = 0; roIdx_ = (roIdx_ + 1) & 7; }
        bool gap = (roTick_ > 50000);
        uint32_t nib = (readoutValue() >> (28 - 4 * roIdx_)) & 0xF;
        for (int i = 0; i < 4; i++) LedOn(i, !gap && ((nib >> i) & 1));
        LedBrightness(4, (roIdx_ < 4) ? 4095 : 250);
        LedOn(5, gap);
    }

    uint32_t tick_ = 0, upHeld_ = 0, roTick_ = 0;
    int  roIdx_ = 0;
    bool readout_ = false;
    Switch lastSw_ = Switch::Middle;
};

int main()
{
    set_sys_clock_khz(144000, true);
    BandwidthCard card;
    card.Run();
}
