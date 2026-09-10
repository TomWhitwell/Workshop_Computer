// cablecheck.cpp — Which wire is which? Resolves the link-cable crossover question.
//
// WHY THIS EXISTS
// ---------------
// A standard GBA<->GBA link cable CROSSES SO/SI between its two ends (confirmed: GBATEK and
// the hardwarebook/akkit pinouts — "SO and SI should be crosswired between the two cable
// connectors"). A GameCube->GBA (DOL-011) cable does NOT.
//
// So if you plug a link cable into a GBA-link SOCKET at the Workshop end and wire that socket
// by the GBA pinout (pin 2 = SO = read, pin 3 = SI = drive), a crossed cable silently swaps
// them. The result:
//   * we DRIVE our data into the GBA's SO pin — which is one of its OUTPUTS (contention), and
//   * we LISTEN on the GBA's SI pin — which is an INPUT, so we only ever read our own pull-up.
// Everything looks wired, nothing works, and no amount of edge/polarity sweeping helps.
//
// The Workshop cannot re-route this in firmware: Pulse Out 1/2 are output-only and Pulse In
// 1/2 are input-only. But it CAN tell you which way round the cable is, using the fact that
// Pulse In 2 (GPIO 3) is otherwise unused by this project.
//
// ─────────────────────────────────────────────────────────────────────────────────────────
// WIRING FOR THIS TEST (one wire moves)
//   Socket pin 5 (SC)  -> Pulse Out 1   [keep 1k series]
//   Socket pin 6 (GND) -> Computer GND
//   Socket pin 2       -> Pulse In 1        <-- both data pins now go to INPUTS
//   Socket pin 3       -> Pulse In 2        <-- move this off Pulse Out 2 for the test
//   Pulse Out 2        -> leave disconnected
// Nothing drives the GBA's data lines during this test, so it is safe either way round.
//
// ─────────────────────────────────────────────────────────────────────────────────────────
// LED4 + LED5 = MODE NUMBER IN BINARY (LED4 = bit1, LED5 = bit0). Same convention as
// linkcheck. Click DOWN for next mode, UP for previous. Nothing needs holding.
//
// *** WHEN TO TAKE THE READING ***
//   The activity LEDs describe a ROLLING 2-SECOND WINDOW, not all of history. (The first
//   version latched forever, so plugging and power-on transients stayed on the display and
//   with the GBA switched OFF it still showed activity on both inputs. Fixed.)
//   Because of that you no longer have to be careful about ordering — but the cleanest read
//   is still:
//     1. Computer on, cablecheck already running in MODE 0 (SC must already be clocking when
//        the console boots — that is the documented multiboot requirement).
//     2. Turn the GBA on, cartridge-less, and let it settle on the Nintendo logo.
//     3. WAIT ~5 SECONDS for the boot transients to age out of the window.
//     4. Now read LED0/LED1. They reflect only the last two seconds.
//   The GBA's line does things during boot BEFORE it reaches the multiboot wait state, so a
//   reading taken while the logo is still animating is not trustworthy. Let it settle.
//
// MODE 0 — LISTEN  (LED4 off, LED5 off)   ** THE ONE THAT ANSWERS THE QUESTION **
//   Clocks SC continuously but drives NO data. Watches both inputs.
//   LED0 = Pulse In 1 saw >=8 transitions in the last 2 s window
//   LED1 = Pulse In 2 saw >=8 transitions in the last 2 s window
//   LED2 = Pulse In 1 level right now
//   LED3 = Pulse In 2 level right now
//   A driven data line shows thousands of transitions in 2 s; a single power-on glitch shows
//   one or two, which is why the threshold exists.
//   VERDICT: with the GBA powered on and settled on the logo screen, the input that shows
//   ACTIVITY is carrying the GBA's SO.
//       LED0 on, LED1 off -> socket pin 2 carries SO. Cable is STRAIGHT. Wire as documented.
//       LED1 on, LED0 off -> socket pin 3 carries SO. Cable is CROSSED. SWAP the two data
//                            wires at the Workshop end (see SWAP FIX below).
//       both off          -> the GBA is not driving at all: check power-on order, that it is
//                            cartridge-less on the logo, and GND continuity.
//       both on           -> suspect a short between the two data lines; run MODE 1 with the
//                            GBA DISCONNECTED (all four LEDs should then be off).
//
// MODE 1 — SHORT / CROSSTALK  (LED4 off, LED5 ON)   GBA DISCONNECTED
//   Drives Pulse Out 1 and Pulse Out 2 with two different slow patterns and checks whether
//   either input is following either output. Detects shorts and miswires in your own harness.
//   LED0 = Pulse In 1 follows Pulse Out 1   (SC shorted into the input — a real fault)
//   LED1 = Pulse In 1 follows Pulse Out 2   (expected ONLY if you fitted the loopback jumper)
//   LED2 = Pulse In 2 follows Pulse Out 1   (fault)
//   LED3 = Pulse In 2 follows Pulse Out 2   (expected only with a jumper to Pulse In 2)
//   With no jumper and no GBA, ALL FOUR SHOULD BE OFF.
//
// MODE 2 — SLAVE-READY WATCH  (LED4 ON, LED5 off)   GBA connected and ON
//   No clocking at all; SC parked in its idle state (HIGH at the jack, per GBATEK: "during
//   inactive transfer, the shift clock (SC) is high"). GBATEK's master init says: "Wait for
//   SI to become LOW (slave ready)". Our SI is whichever input carries the GBA's SO.
//   LED0 = Pulse In 1 went LOW during the last 2 s window (ready signal on pin 2)
//   LED1 = Pulse In 2 went LOW during the last 2 s window (ready signal on pin 3)
//   LED2 = Pulse In 1 low right now
//   LED3 = Pulse In 2 low right now
//   A GBA waiting for multiboot pulls its SO low. This finds it without any transfer at all,
//   so it is independent of every edge/polarity/rate question.
//
// MODE 3 — SC SANITY  (LED4 ON, LED5 ON)
//   Drives SC as a slow ~2 Hz square so you can confirm the clock line with a meter, a probe,
//   or just the LED. LED0 mirrors the intended SC level at the JACK.
//   Use this to prove the SC wire reaches the GBA at all.
//
// ─────────────────────────────────────────────────────────────────────────────────────────
// SWAP FIX — what to change if MODE 0 says CROSSED
//   Only the two DATA wires move; SC and GND stay exactly where they are.
//     socket pin 3  -> Pulse In 1     (this now carries the GBA's SO: we READ it)
//     socket pin 2  -> Pulse Out 2    (this now reaches the GBA's SI: we DRIVE it, via 1k)
//   No firmware change is needed for that swap — the pin roles in gba_spi.h stay as they are,
//   because the swap happens in copper.
//
// Reminder: power the COMPUTER first, then the GBA. Power-cycle the GBA to retry.

#include "ComputerCard.h"
#include "gba_spi.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"

static constexpr uint SC_PIN   = 8;   // Pulse Out 1
static constexpr uint SI_PIN   = 9;   // Pulse Out 2
static constexpr uint IN1_PIN  = 2;   // Pulse In 1
static constexpr uint IN2_PIN  = 3;   // Pulse In 2  (free — this project never used it)

class CableCheck : public ComputerCard
{
public:
    CableCheck() { enterMode(); }

    void __not_in_flash_func(ProcessSample)() override
    {
        tick_++;
        readSwitch();
        switch (mode_) {
        case 0: modeListen();  break;
        case 1: modeShort();   break;
        case 2: modeReady();   break;
        case 3: modeScSanity(); break;
        }
        LedOn(4, (mode_ >> 1) & 1);
        LedOn(5, mode_ & 1);
    }

private:
    // ── click-to-advance switch (never has to be held) ───────────────────────────────────
    void readSwitch()
    {
        Switch sw = SwitchVal();
        if (sw != lastSw_) {
            if (lastSw_ == Switch::Middle) {
                if      (sw == Switch::Down) { mode_ = (mode_ + 1) & 3; enterMode(); }
                else if (sw == Switch::Up)   { mode_ = (mode_ + 3) & 3; enterMode(); }
            }
            lastSw_ = sw;
        }
    }

    void enterMode()
    {
        // Both pulse inputs are transistor stages and read a constant level without an
        // internal pull-up to bias them (ComputerCard.h: "Needs pullup to activate transistor
        // on inputs"). This is the single most common cause of a "dead" pulse input.
        gpio_init(IN1_PIN); gpio_set_dir(IN1_PIN, GPIO_IN);
        gpio_set_function(IN1_PIN, GPIO_FUNC_SIO); gpio_pull_up(IN1_PIN);
        gpio_set_inover(IN1_PIN, GBA_MISO_INOVER);
        gpio_init(IN2_PIN); gpio_set_dir(IN2_PIN, GPIO_IN);
        gpio_set_function(IN2_PIN, GPIO_FUNC_SIO); gpio_pull_up(IN2_PIN);
        gpio_set_inover(IN2_PIN, GBA_MISO_INOVER);   // assumed same stage as Pulse In 1

        gpio_init(SC_PIN); gpio_set_dir(SC_PIN, GPIO_OUT);
        gpio_set_function(SC_PIN, GPIO_FUNC_SIO); gpio_set_outover(SC_PIN, GPIO_OVERRIDE_NORMAL);
        gpio_init(SI_PIN); gpio_set_dir(SI_PIN, GPIO_OUT);
        gpio_set_function(SI_PIN, GPIO_FUNC_SIO); gpio_set_outover(SI_PIN, GPIO_OVERRIDE_NORMAL);

        driveSc(true);      // GBATEK: SC idles HIGH
        driveSi(true);
        edges1_ = edges2_ = 0; low1_ = low2_ = false;
        shownEdges1_ = shownEdges2_ = 0; shownLow1_ = shownLow2_ = false;
        winTick_ = 0;
        for (int i = 0; i < 4; i++) { agree_[i] = 0; total_[i] = 0; }
        cnt_ = 0;
    }

    // The Workshop pulse OUTPUTS invert in hardware, so to put `level` on the JACK we write
    // its complement to the pad. (The inputs do NOT invert — measured on the scope.)
    void driveSc(bool jackHigh) { gpio_put(SC_PIN, !jackHigh); scLevel_ = jackHigh; }
    void driveSi(bool jackHigh) { gpio_put(SI_PIN, !jackHigh); siLevel_ = jackHigh; }

    // ROLLING WINDOW, not a latch. The first version of this used sticky "seen high AND seen
    // low" flags that only cleared on mode entry, so every plug/unplug and power-on transient
    // stayed on the display forever — with the GBA switched OFF it still showed activity on
    // both inputs. Now each input's 0<->1 transitions are counted over a ~2 s window; at the
    // end of each window the count becomes the displayed result and the counter restarts.
    // The display therefore always describes the LAST TWO SECONDS and nothing older, so it
    // no longer matters what happened while you were wiring or powering things up.
    void sampleInputs()
    {
        bool a = gpio_get(IN1_PIN);
        bool b = gpio_get(IN2_PIN);
        if (a != in1_) edges1_++;
        if (b != in2_) edges2_++;
        in1_ = a; in2_ = b;
        if (!a) low1_ = true;            // "went low at some point this window" (slave-ready)
        if (!b) low2_ = true;

        if (++winTick_ >= 96000) {       // 2 s at 48 kHz
            winTick_ = 0;
            shownEdges1_ = edges1_; shownEdges2_ = edges2_;
            shownLow1_ = low1_;     shownLow2_ = low2_;
            edges1_ = edges2_ = 0;  low1_ = low2_ = false;
        }
    }

    // A driven data line shows thousands of transitions in 2 s. A single power-on glitch
    // shows one or two. Require a real burst so transients cannot read as "activity".
    bool active1() const { return shownEdges1_ >= 8; }
    bool active2() const { return shownEdges2_ >= 8; }

    // ── MODE 0: listen on both inputs while clocking SC ──────────────────────────────────
    void modeListen()
    {
        // ~12 kHz square on SC (toggle every 2 samples) — fast enough to look like a real
        // clock to the GBA, slow enough for the transistor input to follow.
        if ((tick_ & 1) == 0) driveSc(!scLevel_);
        driveSi(true);                       // parked high; we drive no data in this mode
        sampleInputs();
        LedOn(0, active1());       // activity in the last completed 2 s window
        LedOn(1, active2());
        LedOn(2, in1_);            // live level
        LedOn(3, in2_);
    }

    // ── MODE 1: short / crosstalk detection ──────────────────────────────────────────────
    void modeShort()
    {
        // Two clearly different rates so an input following one output cannot be mistaken
        // for it following the other.
        if (++cnt_ >= 6000) { cnt_ = 0; driveSc(!scLevel_); }          // ~4 Hz
        if ((tick_ % 9000) == 0) driveSi(!siLevel_);                   // ~2.7 Hz
        sampleInputs();

        score(0, in1_ == scLevel_);
        score(1, in1_ == siLevel_);
        score(2, in2_ == scLevel_);
        score(3, in2_ == siLevel_);

        // "Follows" = agrees essentially always over a long window. A genuine short tracks
        // perfectly; two unrelated square waves drift and score ~50%.
        for (int i = 0; i < 4; i++) LedOn(i, follows(i));
    }

    void score(int i, bool agrees)
    {
        if (agrees) agree_[i]++;
        if (++total_[i] >= 48000) {          // 1 s window
            lastPct_[i] = (agree_[i] * 100) / total_[i];
            agree_[i] = 0; total_[i] = 0;
        }
    }
    bool follows(int i) const { return lastPct_[i] >= 97; }

    // ── MODE 2: GBATEK slave-ready watch (no clocking at all) ────────────────────────────
    void modeReady()
    {
        driveSc(true);       // idle HIGH, per GBATEK
        driveSi(true);
        sampleInputs();
        LedOn(0, shownLow1_);   // went low during the last window
        LedOn(1, shownLow2_);
        LedOn(2, !in1_);        // low right now
        LedOn(3, !in2_);
    }

    // ── MODE 3: slow SC so the clock wire can be traced with a meter ─────────────────────
    void modeScSanity()
    {
        if (++cnt_ >= 12000) { cnt_ = 0; driveSc(!scLevel_); }   // ~2 Hz
        driveSi(true);
        sampleInputs();
        LedOn(0, scLevel_);          // mirrors the intended level at the SC jack
        LedOn(1, in1_);
        LedOn(2, in2_);
        LedOff(3);
    }

    uint32_t tick_ = 0, cnt_ = 0;
    uint32_t agree_[4] = {0,0,0,0}, total_[4] = {0,0,0,0};
    uint32_t lastPct_[4] = {0,0,0,0};
    int  mode_ = 0;
    bool scLevel_ = true, siLevel_ = true;
    bool in1_ = false, in2_ = false;
    uint32_t edges1_ = 0, edges2_ = 0, shownEdges1_ = 0, shownEdges2_ = 0, winTick_ = 0;
    bool low1_ = false, low2_ = false, shownLow1_ = false, shownLow2_ = false;
    Switch lastSw_ = Switch::Middle;
};

int main()
{
    set_sys_clock_khz(144000, true);
    CableCheck card;
    card.Run();
}
