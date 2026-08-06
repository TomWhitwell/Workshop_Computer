/*
 * drumdrum — DFAM-Style Step Sequencer
 * =====================================
 * A program card for the Music Thing Modular Workshop Computer that emulates
 * the core sequencer and sound-generation behaviour of the Moog DFAM.
 *
 * The sequencer drives two VCOs (via CV Out 1 and Audio Out 2), outputs a
 * velocity CV for envelope/decay control, generates continuous white noise,
 * and provides step triggers and end-of-cycle triggers for external patching.
 *
 *
 * I/O Assignment:
 * ───────────────
 *   CV Out 1    — VCO 1 pitch (1V/oct, calibrated, quantised to semitones)
 *   CV Out 2    — Velocity CV (for EG decay when patched to Slope)
 *   Audio Out 1 — White noise (continuous, audio-rate xorshift32 PRNG)
 *   Audio Out 2 — VCO 2 pitch (same sequence, offset by Y knob in play mode)
 *   Pulse Out 1 — Step trigger (fires each step; also preview when paused)
 *   Pulse Out 2 — End-of-cycle trigger (fires when sequence wraps to step 1)
 *   Pulse In 1  — External clock (overrides internal tempo when patched)
 *   Pulse In 2  — Reset (returns sequence to step 1 on rising edge)
 *   CV In 1     — Decay CV mod (summed with stored velocity for current step)
 *   CV In 2     — Global pitch transpose (±24 semitones across both VCOs)
 *
 *
 * Modes (Z switch):
 * ─────────────────
 *   UP (play)   — Main=tempo, X=length(2-8), Y=VCO2 offset
 *   MIDDLE (edit)— Main=tempo, X=step pitch(pickup), Y=step velocity(pickup)
 *   DOWN (momentary) — short press: advance edit cursor
 *                       long press (≥500ms): toggle play/pause
 *
 *
 * LED Encoding (steps 1–8 shown on the 6-LED 2×3 grid):
 * ──────────────────────────────────────────────────────
 *   Step 1: top-left only
 *   Step 2: top-left + top-right
 *   Step 3: top row + middle-left
 *   Step 4: top + middle rows
 *   Step 5: top + middle + bottom-left
 *   Step 6: all 6 LEDs
 *   Step 7: all except top-left
 *   Step 8: middle + bottom rows (4 LEDs)
 */

#include "ComputerCard.h"
#include "shared_state.h"
#include "usb_core1.h"
#include "hardware/clocks.h"
#include "pico/multicore.h"
#include "pico/time.h"

#include "tusb.h"
#include "bsp/board_api.h"


// Cross-core sequencer state. Defined once here, declared extern in
// shared_state.h so Core 1 (USB host / device task) can read & write it.
volatile SharedState gState = {};


class DFAMSequencer : public ComputerCard
{

    // ──────────────────────────────────────────────
    // Internal clock timing
    // At 48kHz, ticksPerStep sets the step duration.
    // Quadratic mapping from Main knob gives a musical
    // tempo feel: CW = fast (~30 steps/s), CCW = slow (~2s/step).
    // ──────────────────────────────────────────────
    uint32_t tickCounter  = 0;
    uint32_t ticksPerStep = 48000;

    // ──────────────────────────────────────────────
    // Trigger pulse timing
    // Each trigger stays high for TRIGGER_LEN samples (~2ms),
    // long enough for any envelope generator to detect reliably.
    // ──────────────────────────────────────────────
    static constexpr int TRIGGER_LEN = 96; // ~2ms at 48kHz
    int trigCounter = 0;          // armed by the boot-mute code below
    int eocCounter  = 0;

    // Boot mute — silence audio and trigger outputs for the first
    // ~150 ms after power-on so settling DACs and immediate startup
    // state can't make a click on whatever's downstream. When the
    // mute lifts we fire the step 1 trigger so the sequencer still
    // starts audibly from boot.
    static constexpr uint32_t BOOT_MUTE_SAMPLES = 7200; // 150 ms at 48 kHz
    uint32_t bootMute = BOOT_MUTE_SAMPLES;

    // ──────────────────────────────────────────────
    // Edit mode pickup state (knob catch-up flags)
    // editStep itself lives in gState so Core 1 can read it for Grid /
    // browser display and write it when the user selects a step there.
    // ──────────────────────────────────────────────
    bool xPickedUp   = false;
    bool yPickedUp   = false;

    // Track the pitch/velocity values we last "owned" — anything in
    // gState that doesn't match these came from the Grid or browser
    // editor and means we must drop pickup so the knob no longer
    // overwrites the externally-set value. lastEditStep does the same
    // job for editStep changes from the Grid.
    uint8_t lastSeenPitch[8]    = {0,0,0,0,0,0,0,0};
    uint8_t lastSeenVelocity[8] = {0,0,0,0,0,0,0,0};
    int8_t  lastEditStep        = -1;

    // After a pickup reset (mode change, edit-step change, external
    // edit), we remember the knob's raw ADC position and require the
    // knob to move at least KNOB_MOVE_THRESH from it before pickup is
    // allowed to recapture. Without this, parking the knob anywhere
    // close to the value the Grid just set causes pickup to catch on
    // the next sample and overwrite the Grid's value with whatever
    // the knob happens to be reading.
    int32_t xKnobAtReset = -1;
    int32_t yKnobAtReset = -1;
    static constexpr int32_t KNOB_MOVE_THRESH = 60; // out of 4096 — well above ADC noise

    // Pickup threshold: the knob must come within this many units of the
    // stored value before it "catches" and begins tracking. This prevents
    // value jumps when switching modes or between steps (MicroFreak-style).
    static constexpr int PICKUP_THRESH = 3;
    static constexpr int PICKUP_THRESH_LENGTH = 1; // tighter for the 2–8 range

    // ──────────────────────────────────────────────
    // Play mode pickup state
    // Same pickup/catchup behaviour applies when switching back to play mode,
    // so the X (length) and Y (VCO2 offset) knobs don't jump to a new value
    // if they were left in a different position during edit mode.
    // ──────────────────────────────────────────────
    bool lengthPickedUp = false;
    bool offsetPickedUp = false;

    // Same external-edit guard as the edit-mode pickup (see lastSeen*
    // below): when seqLength changes from a Grid tap or browser edit
    // we drop lengthPickedUp and require the knob to move before
    // recapturing, so the play-mode length knob can't silently
    // overwrite a Grid-set length when it happens to be parked nearby.
    uint8_t lastSeenSeqLength = 8;
    int32_t lengthKnobAtReset = -1;

    // ──────────────────────────────────────────────
    // VCO 2 pitch offset (semitones, set by Y knob in play mode)
    // Stored as a member so it persists when switching to edit mode.
    // ──────────────────────────────────────────────
    int32_t vco2Offset = 0;

    // ──────────────────────────────────────────────
    // Switch DOWN (momentary) press detection
    // The physical switch is (ON)-OFF-ON: UP latches, MIDDLE is centre,
    // DOWN is momentary and springs back to MIDDLE.
    // We time how long DOWN is held to distinguish short/long presses.
    // ──────────────────────────────────────────────
    static constexpr uint32_t LONG_PRESS_SAMPLES = 24000; // 500ms at 48kHz
    bool     switchDownActive = false;
    uint32_t switchDownCount  = 0;
    bool     longPressHandled = false;

    // ──────────────────────────────────────────────
    // Previous switch position (for detecting mode transitions)
    // ──────────────────────────────────────────────
    Switch prevMode = Switch::Up;

    // ──────────────────────────────────────────────
    // White noise PRNG state (xorshift32)
    // ──────────────────────────────────────────────
    uint32_t rngState = 0xDEADBEEF;

    // ──────────────────────────────────────────────
    // LED encoding table
    // Each entry is a 6-bit mask: bit N = LED N on.
    // Index 0 = step 1, index 7 = step 8.
    //
    // LED layout:
    //   | 0  1 |  (top)
    //   | 2  3 |  (middle)
    //   | 4  5 |  (bottom)
    // ──────────────────────────────────────────────
    static constexpr uint8_t ledPattern[8] = {
        0b000001, // step 1: top-left
        0b000011, // step 2: top-left + top-right
        0b000111, // step 3: + middle-left
        0b001111, // step 4: + middle-right
        0b011111, // step 5: + bottom-left
        0b111111, // step 6: all ON
        0b111110, // step 7: all except top-left
        0b111100, // step 8: middle + bottom rows
    };

public:
    // Advance the PRNG by one step and return the new state.
    uint32_t __not_in_flash_func(xorshift)()
    {
        rngState ^= rngState << 13;
        rngState ^= rngState >> 17;
        rngState ^= rngState << 5;
        return rngState;
    }

    // Expose the protected USBPowerState() to main() so we can pick a
    // USB role (host vs device) before launching Core 1.
    USBPowerState_t ReadUSBPowerState() { return USBPowerState(); }

    DFAMSequencer()
    {
        // Settling delay before any USB / I/O activity — matches MLRws,
        // which we know works on the same hardware. Without this the
        // USB controller doesn't always come up cleanly after reset.
        sleep_ms(150);

        // Seed the PRNG from the hardware microsecond timer so that each
        // reset produces a different sequence. The exact timer value at
        // this point depends on boot timing and is effectively random.
        rngState = time_us_32();
        if (rngState == 0) rngState = 0xDEADBEEF; // xorshift must never be zero

        // Randomise step values: pitch within a musically useful 3-octave
        // range (C2–B4, MIDI 36–71), velocity biased toward the upper half
        // so steps are audible by default.
        for (int i = 0; i < 8; i++) {
            gState.pitch[i]    = 36 + (xorshift() % 36);  // C2–B4
            gState.velocity[i] = 100 + (xorshift() % 156); // 100–255
            lastSeenPitch[i]    = gState.pitch[i];
            lastSeenVelocity[i] = gState.velocity[i];
        }
        gState.seqLength   = 8;
        gState.editStep    = 0;
        gState.currentStep = 0;
        gState.playing     = 1;
        gState.tickEpoch   = 0;
        lastSeenSeqLength  = gState.seqLength;
    }

    // ProcessSample runs once per audio sample at 48kHz in interrupt context.
    // It must complete within ~20μs. All arithmetic is integer to stay fast.
    virtual void ProcessSample() override
    {
        // ════════════════════════════════════════════
        // BOOT MUTE — hold audio and pulse outputs at zero for the
        // first 150 ms so DAC settling and any startup state changes
        // can't make a click. When the mute lifts we arm the step 1
        // trigger so the sequencer still kicks off audibly at boot.
        // ════════════════════════════════════════════
        if (bootMute > 0) {
            bootMute--;
            AudioOut1(0);
            AudioOut2(0);
            PulseOut1(false);
            PulseOut2(false);
            if (bootMute == 0) {
                // Step 1 fires audibly when the mute lifts. End-of-cycle
                // is a sequence-wrap event so we deliberately don't arm
                // it here — patches on Pulse Out 2 shouldn't see a pulse
                // at every power-up.
                trigCounter = TRIGGER_LEN;
            }
            return;
        }

        // ════════════════════════════════════════════
        // WHITE NOISE — always running on Audio Out 1,
        // regardless of mode or play/pause state.
        // xorshift32 produces a full-spectrum noise signal.
        // ════════════════════════════════════════════
        // Extract 12 bits and centre around zero for the signed audio range
        AudioOut1(static_cast<int16_t>((xorshift() >> 20) & 0xFFF) - 2048);


        // ════════════════════════════════════════════
        // READ SWITCH — determines current mode
        // ════════════════════════════════════════════
        Switch sw = SwitchVal();

        // When entering edit mode, reset edit-knob pickup so X/Y don't
        // immediately overwrite the step's stored values.
        if (sw == Switch::Middle && prevMode != Switch::Middle) {
            xPickedUp = false;
            yPickedUp = false;
        }

        // When entering play mode, reset play-knob pickup so X (length)
        // and Y (VCO2 offset) don't jump from their edit-mode positions.
        // Also seed the knob-at-reset position so the first samples
        // after entering UP can't catch without an actual movement.
        if (sw == Switch::Up && prevMode != Switch::Up) {
            lengthPickedUp = false;
            offsetPickedUp = false;
            lengthKnobAtReset = KnobVal(Knob::X);
        }


        // ════════════════════════════════════════════
        // SWITCH DOWN — momentary press handling
        // Short press (<500ms): advance edit cursor to next step.
        // Long press (≥500ms): toggle play/pause.
        // ════════════════════════════════════════════
        if (sw == Switch::Down) {
            if (!switchDownActive) {
                switchDownActive = true;
                switchDownCount  = 0;
                longPressHandled = false;
            }
            switchDownCount++;

            if (switchDownCount >= LONG_PRESS_SAMPLES && !longPressHandled) {
                // Long press detected: toggle play/pause
                longPressHandled = true;
                if (gState.playing) {
                    // Pause the sequencer — step position is preserved
                    gState.playing = 0;
                } else {
                    // Resume playback from the step after the last one played.
                    // This avoids re-triggering a step the user already heard.
                    gState.playing = 1;
                    gState.currentStep = (gState.currentStep + 1) % gState.seqLength;
                    gState.tickEpoch++;
                    tickCounter = 0;
                    trigCounter = TRIGGER_LEN;
                    if (gState.currentStep == 0) {
                        eocCounter = TRIGGER_LEN;
                    }
                }
            }
        } else if (switchDownActive) {
            // Switch just released back to MIDDLE
            switchDownActive = false;

            if (!longPressHandled) {
                // Short press: advance edit cursor, wrapping at sequence length
                gState.editStep = (gState.editStep + 1) % gState.seqLength;
                xPickedUp = false;
                yPickedUp = false;

                // When paused, fire a preview trigger so the user can hear
                // the step they just navigated to. This does NOT fire during
                // normal playback to avoid double-triggering.
                if (!gState.playing) {
                    trigCounter = TRIGGER_LEN;
                }
            }
        }


        // ════════════════════════════════════════════
        // MODE-DEPENDENT KNOB HANDLING
        // ════════════════════════════════════════════
        if (sw == Switch::Up) {
            // --- Play mode ---
            // Both knobs use pickup behaviour so switching from edit mode
            // doesn't cause the length or VCO2 offset to jump.
            int32_t xRaw = KnobVal(Knob::X);

            // If seqLength changed externally (Grid tap, browser edit),
            // drop pickup and require the knob to actually move before
            // recapturing. Otherwise tapping a length on the Grid that's
            // close to the knob's parked position gets immediately
            // overwritten by the knob's value.
            if (gState.seqLength != lastSeenSeqLength) {
                lengthPickedUp = false;
                lengthKnobAtReset = xRaw;
            }
            lastSeenSeqLength = gState.seqLength;

            // X knob → sequence length (1–8 steps). Length 1 = a single
            // hammered step (useful for one-shot voices); EOC fires on
            // every tick in that case.
            int32_t knobLength = 1 + ((xRaw * 8) >> 12);
            if (knobLength > 8) knobLength = 8;
            if (!lengthPickedUp) {
                int32_t moved = xRaw - lengthKnobAtReset;
                if (moved < 0) moved = -moved;
                if (moved >= KNOB_MOVE_THRESH) {
                    int32_t d = knobLength - static_cast<int32_t>(gState.seqLength);
                    if (d < 0) d = -d;
                    if (d <= PICKUP_THRESH_LENGTH) lengthPickedUp = true;
                }
            }
            if (lengthPickedUp) {
                gState.seqLength = static_cast<uint8_t>(knobLength);
            }

            // Y knob → VCO 2 pitch offset relative to VCO 1 (±24 semitones)
            // Knob at noon ≈ unison; CW = higher, CCW = lower
            int32_t knobOffset = ((KnobVal(Knob::Y) * 49) >> 12) - 24;
            if (!offsetPickedUp) {
                int32_t d = knobOffset - vco2Offset;
                if (d < 0) d = -d;
                if (d <= PICKUP_THRESH) offsetPickedUp = true;
            }
            if (offsetPickedUp) {
                vco2Offset = knobOffset;
            }

        } else if (sw == Switch::Middle) {
            // --- Edit mode ---
            // Knobs use "pickup" behaviour: the knob must pass through the
            // stored value before it begins tracking. This prevents jarring
            // value jumps when the knob position doesn't match the step's
            // stored value.

            uint8_t es = gState.editStep;
            int32_t xRaw = KnobVal(Knob::X);
            int32_t yRaw = KnobVal(Knob::Y);

            // If the edit step changed (user tapped a new step on the
            // Grid, or pressed switch DOWN), drop pickup so the knob
            // doesn't immediately splat the new step's value. Same
            // story if pitch/velocity at this step changed externally
            // (Grid tap, browser edit) — give the user a fresh catch
            // before we start tracking again. Stash the knob position
            // so we can require an actual user movement before pickup
            // is allowed to recapture.
            if (es != lastEditStep ||
                gState.pitch[es]    != lastSeenPitch[es] ||
                gState.velocity[es] != lastSeenVelocity[es]) {
                xPickedUp = false;
                yPickedUp = false;
                xKnobAtReset = xRaw;
                yKnobAtReset = yRaw;
            }
            lastEditStep = (int8_t)es;

            // X knob → pitch for the current edit step. Spans the full
            // 0..127 MIDI range; the Grid quantizes to PITCH_GRID_STEP
            // semitones for its picker but we still want fine pot
            // control over the entire range.
            int32_t knobPitch = (xRaw * 127) >> 12;
            if (!xPickedUp) {
                int32_t moved = xRaw - xKnobAtReset;
                if (moved < 0) moved = -moved;
                if (moved >= KNOB_MOVE_THRESH) {
                    int32_t d = knobPitch - static_cast<int32_t>(gState.pitch[es]);
                    if (d < 0) d = -d;
                    if (d <= PICKUP_THRESH) xPickedUp = true;
                }
            }
            if (xPickedUp) {
                gState.pitch[es] = static_cast<uint8_t>(knobPitch);
            }

            // Y knob → velocity for the current edit step (0–255)
            int32_t knobVel = (yRaw * 255) >> 12;
            if (!yPickedUp) {
                int32_t moved = yRaw - yKnobAtReset;
                if (moved < 0) moved = -moved;
                if (moved >= KNOB_MOVE_THRESH) {
                    int32_t d = knobVel - static_cast<int32_t>(gState.velocity[es]);
                    if (d < 0) d = -d;
                    if (d <= PICKUP_THRESH) yPickedUp = true;
                }
            }
            if (yPickedUp) {
                gState.velocity[es] = static_cast<uint8_t>(knobVel);
            }

            // Refresh the "last seen" snapshot so we don't false-trigger
            // a pickup reset next sample. Catches both panel writes and
            // any external write we just observed.
            lastSeenPitch[es]    = gState.pitch[es];
            lastSeenVelocity[es] = gState.velocity[es];
        }


        // ════════════════════════════════════════════
        // RESET INPUT
        // Rising edge on Pulse In 2 returns the sequence to step 1.
        // ════════════════════════════════════════════
        if (PulseIn2RisingEdge()) {
            gState.currentStep = 0;
            gState.tickEpoch++;
            tickCounter = 0;
            trigCounter = TRIGGER_LEN;
        }


        // ════════════════════════════════════════════
        // SEQUENCER ADVANCE
        // External clock (Pulse In 1) overrides the internal tempo when
        // patched. The internal clock keeps running in the background so
        // that removing the patch cable resumes at the current tempo.
        // ════════════════════════════════════════════
        bool useExtClock = Connected(Input::Pulse1);

        if (gState.playing) {
            bool advance = false;

            if (useExtClock) {
                // External clock: each rising edge advances one step
                if (PulseIn1RisingEdge()) {
                    advance = true;
                }
            } else {
                // Internal clock: Main knob sets tempo with a quadratic curve
                // for a musical feel. CW = fast (~30 steps/s), CCW = slow (~2s).
                int32_t knob = KnobVal(Knob::Main);
                int32_t inv  = 4095 - knob;
                ticksPerStep = 1600 + ((inv * inv * 6) >> 10);

                tickCounter++;
                if (tickCounter >= ticksPerStep) {
                    tickCounter = 0;
                    advance = true;
                }
            }

            if (advance) {
                gState.currentStep = (gState.currentStep + 1) % gState.seqLength;
                gState.tickEpoch++;
                trigCounter = TRIGGER_LEN;
                // Fire end-of-cycle when wrapping from last step back to first
                if (gState.currentStep == 0) {
                    eocCounter = TRIGGER_LEN;
                }
            }
        } else {
            // While paused, keep updating the tempo calculation so that
            // unplugging an external clock cable resumes smoothly.
            if (!useExtClock) {
                int32_t knob = KnobVal(Knob::Main);
                int32_t inv  = 4095 - knob;
                ticksPerStep = 1600 + ((inv * inv * 6) >> 10);
            }
        }

        // Clamp positions if sequence length was shortened. Bump
        // tickEpoch on a currentStep clamp so Core 1 (browser editor)
        // sees the position change immediately — otherwise it would
        // keep highlighting a stale step until the next natural tick,
        // and indefinitely if playback is paused.
        if (gState.currentStep >= gState.seqLength) {
            gState.currentStep = 0;
            gState.tickEpoch++;
        }
        if (gState.editStep    >= gState.seqLength) gState.editStep    = 0;


        // ════════════════════════════════════════════
        // CV AND AUDIO OUTPUTS
        // ════════════════════════════════════════════

        // When paused in edit mode, output the edit step's values so the
        // user can hear what they are editing via the preview trigger.
        // Otherwise, output the current playback step.
        uint8_t outputStep = gState.currentStep;
        if (!gState.playing && sw == Switch::Middle) {
            outputStep = gState.editStep;
        }
        uint8_t outPitch    = gState.pitch[outputStep];
        uint8_t outVelocity = gState.velocity[outputStep];

        // CV In 2 → global pitch transpose (±24 semitones)
        // CVIn2 returns -2048..+2047; scale to ±24 with integer math
        int32_t transpose = (CVIn2() * 3) >> 8;

        // --- CV Out 1: VCO 1 pitch (calibrated 1V/oct) ---
        // Uses the EEPROM-calibrated CVOutMIDINote for accurate tracking
        int32_t pitch1 = static_cast<int32_t>(outPitch) + transpose;
        if (pitch1 < 0)   pitch1 = 0;
        if (pitch1 > 127) pitch1 = 127;
        CVOut1MIDINote(static_cast<uint8_t>(pitch1));

        // --- CV Out 2: Velocity + Decay CV mod ---
        // Velocity (0–255) is scaled to fill the positive CV range.
        // CV In 1 is summed in to allow external modulation of the
        // decay/envelope amount.
        int32_t velCV = static_cast<int32_t>(outVelocity) * 8 + CVIn1();
        if (velCV > 2047)  velCV = 2047;
        if (velCV < -2048) velCV = -2048;
        CVOut2(static_cast<int16_t>(velCV));

        // --- Audio Out 2: VCO 2 pitch CV ---
        // Approximates 1V/oct on the 12-bit audio DAC.
        // Audio DAC range -2048..+2047 ≈ ±6V, so 1 semitone ≈ 28.4 units.
        // Formula: (midiNote - 60) * 1820 >> 6 gives ~28.44 units/semitone.
        // This output is uncalibrated — tune VCO 2 by ear or with a tuner.
        int32_t pitch2 = static_cast<int32_t>(outPitch) + transpose + vco2Offset;
        int32_t audioPitch = ((pitch2 - 60) * 1820) >> 6;
        if (audioPitch > 2047)  audioPitch = 2047;
        if (audioPitch < -2048) audioPitch = -2048;
        AudioOut2(static_cast<int16_t>(audioPitch));


        // ════════════════════════════════════════════
        // TRIGGER OUTPUTS
        // Each trigger pulse counts down from TRIGGER_LEN (~2ms).
        // ════════════════════════════════════════════
        PulseOut1(trigCounter > 0);
        if (trigCounter > 0) trigCounter--;

        PulseOut2(eocCounter > 0);
        if (eocCounter > 0) eocCounter--;


        // ════════════════════════════════════════════
        // LED DISPLAY
        // In edit mode: shows the edit cursor position.
        // In play mode: shows the current playback step.
        // Both use the same step-encoding scheme (see table above).
        // ════════════════════════════════════════════
        uint8_t displayStep = (sw == Switch::Middle) ? gState.editStep : gState.currentStep;
        uint8_t pattern = ledPattern[displayStep];
        for (int i = 0; i < 6; i++) {
            LedOn(i, (pattern >> i) & 1);
        }

        prevMode = sw;
    }
};


int main()
{
    // 144MHz reduces ADC tonal artifacts (recommended by Workshop Computer
    // AI Directive). Must be set before the ComputerCard constructor, which
    // configures peripheral clocks based on the current system frequency.
    set_sys_clock_khz(144000, true);

    DFAMSequencer seq;
    seq.EnableNormalisationProbe();

    // Decide USB role from the USB-C CC pins (Rev 1.1+). UFP = we're
    // plugged into a computer (browser editor). Anything else (DFP or
    // unsupported) we treat as host so a Grid plugged into the front
    // jack works on older hardware too. This matches MLRws's check.
    gUsbHostMode = (seq.ReadUSBPowerState() != ComputerCard::UFP) ? 1 : 0;

    // For device mode we initialise TinyUSB synchronously here on Core 0
    // so the host's enumeration request is answered as quickly as
    // possible. Core 1 just calls tud_task() in its loop. (Host mode
    // does its own board_init/tusb_init from Core 1 — see usb_core1.cpp.)
    if (!gUsbHostMode) {
        board_init();
        tud_init(0);
    }

    // Core 1 owns the USB task pump (host or device, decided above).
    multicore_launch_core1(core1_entry);

    seq.Run();
}
