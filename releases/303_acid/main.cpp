// ============================================================================
//  acid — an acid style step sequencer for the Music Thing Workshop Computer
// ============================================================================
//
//  Walk a cursor through a 16-step pattern and set the pitch, mode and flags
//  of each step individually, then flick the switch up to hear the loop.
//
//  Pitch and step-type are two conceptually separate sequences: they're still
//  programmed together, one step at a time, but during playback each can be
//  independently phase-shifted by an audio input, and independently reset /
//  reversed via the reset-mode menu.
//
//  CONTROLS
//  ----------------------------------------------------------------------------
//   Switch UP    (play) : Main = length (1–16),  X = swing (0–33%),  Y = tempo
//                         (Pulse In 1 patched: X = Pulse Out 1 clock division
//                          /1../8, fires on pattern steps 0, N, 2N, ...;
//                          Y = Pulse Out 2 accent gate width, and Pulse Out 1's
//                          gate width scaled to the divided step interval —
//                          settings persist when the clock is unplugged)
//   Switch MIDDLE(edit) : Main = semitone (0–11),  X = octave (-1..+1),
//                         Y = step mode (rest / normal / accent / slide / accent+slide)
//   Switch DOWN:
//        short flick     : advance the edit cursor to the next step (wraps at 16)
//        hold  ≥1 second : enter the reset-mode config menu (see below).
//                           Release the switch to exit the menu.
//
//  RESET-MODE CONFIG MENU (hold switch DOWN ≥1s)
//  ----------------------------------------------------------------------------
//   Main : selects what a Pulse In 2 reset trigger does —
//            0. reset both pitch and step-type to step 0 (traditional)
//            1. reset pitch sequence to step 0 only
//            2. reset step-type sequence to step 0 only
//            3. reverse pitch (runs 15→0, next trigger returns to normal)
//            4. reverse step-type (runs 15→0, next trigger returns to normal)
//   X    : any large turn randomises the pitch sequence
//   Y    : any large turn randomises the step-type sequence
//
//  JACKS
//  ----------------------------------------------------------------------------
//   Audio In 1  : pitch-sequence phase offset, ±15 steps over ±2V, looping
//                 beyond — negative voltage offsets backwards
//   Audio In 2  : step-type-sequence phase offset, ±15 steps over ±2V, looping
//                 beyond — negative voltage offsets backwards
//   Audio Out 1 : internal sawtooth voice tracking pitch, unshaped (no envelope)
//   Audio Out 2 : gate — ~2ms normally, full step length for slide steps
//   CV In 1     : global transpose, ±12 semitones over ±2V, looping beyond 2V
//   CV In 2     : portamento time — 0V/unplugged = 7.4Hz (fastest); voltage
//                 away from 0V in either direction slows it down
//   CV Out 1    : pitch, calibrated 1V/oct, quantised to semitones
//   CV Out 2    : accent CV — high (5V) on accented steps, 0V otherwise
//   Pulse In 1  : external clock — one step per rising edge, overrides tempo
//   Pulse In 2  : reset — behaviour set by the reset-mode menu above
//   Pulse Out 1 : clock — internal clock pulses, or a divided/width-adjustable
//                 copy of the external clock (X/Y, see CONTROLS above) when
//                 Pulse In 1 is connected
//   Pulse Out 2 : accent trigger — ~2ms on accented steps, width-adjustable
//                 via Y when Pulse In 1 is connected
// ============================================================================

#include "ComputerCard.h"
#include "hardware/clocks.h"

class AcidSequencer : public ComputerCard
{
    static constexpr int     NUM_STEPS    = 16;
    static constexpr uint8_t DEFAULT_NOTE = 36;  // C2
    // Edit range: three octaves centered on the default note C2.
    static constexpr int OCTAVE_BASE = 12;        // C0, used for pitch conversion
    static constexpr int OCTAVE_DEFAULT = 2;      // C2 is the 0 offset
    static constexpr int OCTAVE_MIN = OCTAVE_DEFAULT - 1;
    static constexpr int OCTAVE_MAX = OCTAVE_DEFAULT + 1;
    static constexpr int OCTAVE_COUNT = OCTAVE_MAX - OCTAVE_MIN + 1;

    // ── Per-step flag bits ────────────────────────────────────────────────────
    static constexpr uint8_t FLAG_ACCENT = 0x01; // louder, accent CV high
    static constexpr uint8_t FLAG_SLIDE  = 0x02; // gate held full step (legato)
    static constexpr uint8_t FLAG_REST   = 0x04; // step is silent

    static constexpr uint8_t TYPE_CHOICES[5] = {
        FLAG_REST, 0, FLAG_ACCENT, FLAG_SLIDE, FLAG_ACCENT | FLAG_SLIDE
    };

    // ── Sequencer state ───────────────────────────────────────────────────────
    // pitchStep/typeStep are the raw positions in each sequence; pitchReadStep/
    // typeReadStep are those positions shifted by the Audio In 1/2 offsets.
    // Playback length controls the raw positions, while phase offsets always
    // wrap across all 16 programmed steps.
    struct SeqState {
        uint8_t pitch[NUM_STEPS];
        uint8_t seqLength;
        uint8_t editStep;
        uint8_t pitchStep;
        uint8_t typeStep;
        uint8_t pitchReadStep;
        uint8_t typeReadStep;
        int8_t  pitchDir;
        int8_t  typeDir;
    } seq;

    uint8_t stepFlags[NUM_STEPS]; // FLAG_* bitmask per step, init 0 = normal

    int32_t pitchOffset = 0; // -15..15, sampled from Audio In 1 at each step advance
    int32_t typeOffset  = 0; // -15..15, sampled from Audio In 2 at each step advance

    // ── Reset-mode menu ────────────────────────────────────────────────────────
    uint8_t resetMode         = 0; // 0..4, see header comment
    bool    pitchReversedState = false;
    bool    typeReversedState  = false;
    // A reset pulse doesn't jump the sequence position immediately — it's
    // latched here and applied at the next natural step-advance boundary
    // (internal or external clock), so a reset train faster than tempo just
    // coalesces into "reset on the next step" instead of stalling the clock.
    bool    pendingReset       = false;

    bool    configModeActive  = false;
    bool    cfgMainPickedUp   = false;
    int32_t cfgMainKnobAtEntry = -1;
    int32_t cfgXLastVal        = -1;
    int32_t cfgYLastVal        = -1;
    static constexpr int32_t RANDOMIZE_TURN_THRESH = 800;

    uint32_t rngState = 0x9E3779B9u;

    // ── Internal clock ────────────────────────────────────────────────────────
    uint32_t tickCounter  = 0;
    uint32_t ticksPerStep = 48000;
    uint32_t beatIndex    = 0; // increments every step advance; drives swing parity

    // ── Gate / trigger counters ───────────────────────────────────────────────
    static constexpr int TRIGGER_LEN       = 96;    // ~2ms at 48kHz
    static constexpr int SLIDE_PREVIEW_LEN = 24000; // ~500ms at 48kHz, edit-mode slide preview
    int trigCounter       = 0;
    int accentTrigCounter = 0;
    int clockCounter      = 0; // internal-clock pulse for Pulse Out 1

    // ── Edit-mode pitch-preview retrigger ─────────────────────────────────────
    // Tracks the last previewed note so the gate re-fires only when Main/X
    // actually changes the pitch, not on every sample while sitting still.
    int32_t editPreviewNote = -1;

    // ── Boot mute ─────────────────────────────────────────────────────────────
    static constexpr uint32_t BOOT_MUTE_SAMPLES = 7200;
    uint32_t bootMute = BOOT_MUTE_SAMPLES;

    // ── Knob pickup thresholds ────────────────────────────────────────────────
    static constexpr int32_t KNOB_MOVE_THRESH     = 60;
    static constexpr int32_t PICKUP_THRESH        = 3;
    static constexpr int32_t PICKUP_THRESH_LENGTH = 1;
    // Tempo's knob-to-value curve is quadratic (steep at the slow end, flat at
    // the fast end), so the pickup check compares raw knob position rather
    // than tempo value — a fixed value-space tolerance would need to be huge
    // to catch at the sensitive end, yet would overshoot badly at the flat end.
    static constexpr int32_t PICKUP_THRESH_TEMPO_RAW = 80;

    // Edit-mode pickup: movement-only latch for Main (semi), X (oct), Y (mode).
    bool    semiPickedUp    = false;
    bool    octPickedUp     = false;
    bool    modePickedUp    = false;
    int32_t semiKnobAtReset = -1;
    int32_t octKnobAtReset  = -1;
    int32_t modeKnobAtReset = -1;
    int8_t  lastEditStep    = -1;

    // Play-mode pickup: classic (move + near value) for length and tempo.
    // Swing uses movement-only latch (same as edit knobs).
    bool    lengthPickedUp = false;
    bool    tempoPickedUp  = false;
    bool    swingPickedUp  = false;
    int32_t lengthKnobAtReset = -1;
    int32_t tempoKnobAtReset  = -1;
    int32_t tempoTargetRaw   = -1; // raw knob value that reproduces the current tempo
    int32_t swingKnobAtReset  = -1;
    int32_t swingFraction = 0; // 0..4095 → 0..33% step-time offset

    // ── External-clock mode: X = clock division, Y = pulse width ────────────
    // (active only while Pulse In 1 is patched). Values persist across
    // unplug/replug — they're only ever written when the relevant knob picks up.
    uint8_t  extClockDivision = 1;    // 1..8, output fires every Nth pattern step (0, N, 2N, ...)
    int32_t  extPulseWidthRaw = 0;    // 0..4095, raw Y-knob position at pickup

    bool    divPickedUp      = false;
    bool    widthPickedUp    = false;
    int32_t divKnobAtReset   = -1;
    int32_t widthKnobAtReset = -1;
    bool    wasExtClock      = false;

    uint32_t extClockTickCounter = 0;     // samples since last Pulse In 1 rising edge
    uint32_t extClockPeriod      = 48000; // last measured inter-edge (one step) period, samples
    uint32_t extPulseWidthTicks  = TRIGGER_LEN; // Pulse Out 2 (accent) gate width
    int      extClockOutCounter  = 0;     // countdown for the divided Pulse Out 1 pulse

    // ── Momentary DOWN switch timing ──────────────────────────────────────────
    static constexpr uint32_t LONG_PRESS_SAMPLES = 48000;
    bool     switchDownActive = false;
    uint32_t switchDownCount  = 0;
    bool     longPressHandled = false;

    Switch prevMode = Switch::Up;

    // ── Accent CV state ───────────────────────────────────────────────────────
    bool currentStepAccent = false;

    // ── Portamento (slide) ────────────────────────────────────────────────────
    // CVOut1Millivolts uses 1V/oct with MIDI 60 = 0 mV, so each semitone =
    // 1000/12 mV ≈ 83.33 mV. slideNoteMV256 stores the current output pitch in
    // units of 1/256 mV for sub-mV interpolation resolution.
    // When the step FOLLOWING a slide step fires, glideActive = true and the IIR
    // runs from the slide step's pitch toward the new step's pitch.
    // Base alpha = 1/1024 per sample → tau ≈ 21.5 ms, f_c ≈ 7.4 Hz at 48 kHz,
    // matching the TB-303's fixed RC time constant. This is a fixed-time
    // exponential (one-pole) slew: larger intervals start moving faster because
    // the error is larger, but the time constant stays the same.
    // CV In 2 only ever slows this rate down, symmetrically either side of 0V.
    bool    glideActive     = false;
    int32_t slideNoteMV256  = (DEFAULT_NOTE - 60) * 21333; // init to C2
    int32_t targetNoteMV256 = (DEFAULT_NOTE - 60) * 21333;

    static constexpr int32_t PORTA_BASE_SHIFT = 10;

    // ── Internal VCO voice ────────────────────────────────────────────────────
    // Sawtooth oscillator tracking pitch, unshaped — no envelope applied.
    // PHASE_INC_C0: the 32-bit phase accumulator increment for C0 (MIDI 12, 16.35 Hz)
    //   = round(16.3516 * 2^32 / 48000) = 1 463 107
    // SEMI_RATIO[k] = round(2^(k/12) * 1024) — 10-bit fixed-point semitone multiplier.
    //   Max product PHASE_INC_C0 * SEMI_RATIO[11] = 1463107 * 1933 ≈ 2.83 billion < 2^32.
    static constexpr uint32_t PHASE_INC_C0 = 1463107;
    static constexpr uint16_t SEMI_RATIO[12] = {
        1024, 1085, 1149, 1218, 1290, 1367, 1448, 1534, 1626, 1722, 1825, 1933
    };
    // Extended table with the octave wrap value (2*1024) for fractional
    // interpolation between semitone 11 and the next octave.
    static constexpr uint16_t SEMI_RATIO_EXT[13] = {
        1024, 1085, 1149, 1218, 1290, 1367, 1448, 1534, 1626, 1722, 1825, 1933, 2048
    };
    uint32_t oscPhase    = 0;
    uint32_t oscPhaseInc = 0;

    // ── Helpers ────────────────────────────────────────────────────────────────
    static inline uint8_t WrapStep(int32_t v, uint8_t len)
    {
        if (len == 0) return 0;
        v %= static_cast<int32_t>(len);
        if (v < 0) v += static_cast<int32_t>(len);
        return static_cast<uint8_t>(v);
    }

    // CV/Audio in raw range is ±2048 counts = ±6V, so ±2V (the range of the Four
    // Voltages module) is ±CV_2V_RAW counts.
    static constexpr int32_t CV_2V_RAW = 682;

    // Maps an audio-in voltage to a signed step offset: 0V = no shift, ±2V =
    // a full ±15-step shift (negative voltage offsets backwards). Voltage
    // beyond ±2V keeps scaling past ±15 — the final step-wrap (mod sequence
    // length) naturally loops it back through the range rather than clamping.
    static inline int32_t VoltageToOffset(int16_t ain)
    {
        return (static_cast<int32_t>(ain) * 15) / CV_2V_RAW;
    }

    // Maps CV In 1 to a transpose in semitones: 0V = no transpose, ±2V = a
    // full ±12-semitone range. Voltage beyond ±2V loops back through the
    // range rather than clamping.
    static inline int32_t VoltageToTranspose(int16_t cvRaw)
    {
        int32_t semi = (static_cast<int32_t>(cvRaw) * 12) / CV_2V_RAW;
        return ((semi + 12) % 24 + 24) % 24 - 12;
    }

    // Integer square root via Newton's method (v is always non-negative here).
    static inline int32_t IntegerSqrt(int32_t v)
    {
        if (v <= 0) return 0;
        int32_t x = v;
        int32_t y = (x + 1) >> 1;
        while (y < x) {
            x = y;
            y = (x + v / x) >> 1;
        }
        return x;
    }

    // Inverse of the Y-knob tempo curve: given a ticksPerStep value, returns
    // the raw knob position (0..4095) that would reproduce it. Used so the
    // tempo pickup check can compare knob positions directly instead of
    // tempo values — see PICKUP_THRESH_TEMPO_RAW.
    static inline int32_t TempoToKnobRaw(uint32_t ticksPerStep)
    {
        int32_t t = static_cast<int32_t>(ticksPerStep) - 1600;
        if (t < 0) t = 0;
        int32_t inv  = IntegerSqrt((t << 10) / 6);
        int32_t yRaw = 4095 - inv;
        if (yRaw < 0)    yRaw = 0;
        if (yRaw > 4095) yRaw = 4095;
        return yRaw;
    }

    // Maps the ext-clock pulse-width knob (0..4095) and the measured external
    // clock period to a gate width in samples: CCW = the default short
    // TRIGGER_LEN trigger, CW = ~90% of the measured period (a long sustained
    // gate without overlapping the next clock edge).
    static inline uint32_t ExtPulseWidthTicks(uint32_t periodTicks, int32_t widthRaw)
    {
        uint32_t maxWidth = (periodTicks * 9) / 10;
        if (maxWidth < TRIGGER_LEN) return TRIGGER_LEN;
        uint32_t span = maxWidth - TRIGGER_LEN;
        return TRIGGER_LEN + (span * static_cast<uint32_t>(widthRaw)) / 4095;
    }

    uint32_t NextRandom()
    {
        rngState ^= tickCounter;
        rngState ^= rngState << 13;
        rngState ^= rngState >> 17;
        rngState ^= rngState << 5;
        return rngState;
    }

public:
    AcidSequencer()
    {
        for (int i = 0; i < NUM_STEPS; i++) {
            seq.pitch[i] = DEFAULT_NOTE;
            stepFlags[i] = 0;
        }
        seq.seqLength     = NUM_STEPS;
        seq.editStep      = 0;
        seq.pitchStep     = 0;
        seq.typeStep      = 0;
        seq.pitchReadStep = 0;
        seq.typeReadStep  = 0;
        seq.pitchDir      = 1;
        seq.typeDir       = 1;
    }

    virtual void ProcessSample() override
    {
        // ── BOOT MUTE ─────────────────────────────────────────────────────────
        if (bootMute > 0) {
            bootMute--;
            AudioOut1(0); AudioOut2(0); CVOut2(0);
            PulseOut1(false); PulseOut2(false);
            if (bootMute == 0) trigCounter = TRIGGER_LEN;
            return;
        }

        // ── READ SWITCH ───────────────────────────────────────────────────────
        Switch sw      = SwitchVal();
        bool   playing = (sw == Switch::Up);

        // ── EXTERNAL CLOCK DETECT ─────────────────────────────────────────────
        // With Pulse In 1 patched, X/Y switch from swing/tempo to clock
        // division/pulse width (see KNOB HANDLING below). Detected up front so
        // mode-entry snapshots below pick the right knob roles.
        bool useExtClock = Connected(Input::Pulse1);

        // Leaving EDIT: rewind the sequence to the first step so playback
        // always restarts cleanly after editing.
        if (prevMode == Switch::Middle && sw != Switch::Middle) {
            seq.pitchStep     = 0;
            seq.typeStep      = 0;
            seq.pitchReadStep = 0;
            seq.typeReadStep  = 0;
            seq.pitchDir      = 1;
            seq.typeDir       = 1;
            pitchReversedState = false;
            typeReversedState  = false;
        }

        // Entering EDIT: drop all edit-knob pickups and snapshot knobs.
        if (sw == Switch::Middle && prevMode != Switch::Middle) {
            semiPickedUp    = false;
            octPickedUp     = false;
            modePickedUp    = false;
            semiKnobAtReset = KnobVal(Knob::Main);
            octKnobAtReset  = KnobVal(Knob::X);
            modeKnobAtReset = KnobVal(Knob::Y);
            lastEditStep    = -1;  // force re-snapshot on first edit iteration
        }

        // Entering PLAY: drop play-knob pickups so values don't jump.
        if (sw == Switch::Up && prevMode != Switch::Up) {
            lengthPickedUp    = false;
            lengthKnobAtReset = KnobVal(Knob::Main);
            if (useExtClock) {
                divPickedUp      = false;
                widthPickedUp    = false;
                divKnobAtReset   = KnobVal(Knob::X);
                widthKnobAtReset = KnobVal(Knob::Y);
            } else {
                tempoPickedUp     = false;
                swingPickedUp     = false;
                tempoKnobAtReset  = KnobVal(Knob::Y);
                tempoTargetRaw    = TempoToKnobRaw(ticksPerStep);
                swingKnobAtReset  = KnobVal(Knob::X);
            }
        }

        // ── SWITCH DOWN ───────────────────────────────────────────────────────
        if (sw == Switch::Down) {
            if (!switchDownActive) {
                switchDownActive = true;
                switchDownCount  = 0;
                longPressHandled = false;
            }
            switchDownCount++;

            if (switchDownCount >= LONG_PRESS_SAMPLES && !longPressHandled) {
                longPressHandled   = true;
                configModeActive   = true;
                cfgMainPickedUp    = false;
                cfgMainKnobAtEntry = KnobVal(Knob::Main);
                cfgXLastVal        = KnobVal(Knob::X);
                cfgYLastVal        = KnobVal(Knob::Y);
            }

            if (configModeActive) {
                int32_t mainRaw = KnobVal(Knob::Main);
                int32_t xRaw    = KnobVal(Knob::X);
                int32_t yRaw    = KnobVal(Knob::Y);

                // Main → reset mode (0..4), movement-only latch.
                if (!cfgMainPickedUp) {
                    int32_t moved = mainRaw - cfgMainKnobAtEntry;
                    if (moved < 0) moved = -moved;
                    if (moved >= KNOB_MOVE_THRESH) cfgMainPickedUp = true;
                }
                if (cfgMainPickedUp) {
                    int32_t zone = (mainRaw * 5) >> 12;
                    if (zone > 4) zone = 4;
                    uint8_t newMode = static_cast<uint8_t>(zone);
                    if (newMode != resetMode) {
                        resetMode = newMode;
                        // Changing reset mode drops any in-progress reverse so
                        // both sequences resume running forward/regular.
                        seq.pitchDir       = 1;
                        seq.typeDir        = 1;
                        pitchReversedState = false;
                        typeReversedState  = false;
                    }
                }

                // X → randomise pitch sequence on any large turn.
                int32_t xMoved = xRaw - cfgXLastVal;
                if (xMoved < 0) xMoved = -xMoved;
                if (xMoved >= RANDOMIZE_TURN_THRESH) {
                    for (int i = 0; i < NUM_STEPS; i++)
                        seq.pitch[i] = static_cast<uint8_t>(
                            OCTAVE_BASE + OCTAVE_MIN * 12 + (NextRandom() % (OCTAVE_COUNT * 12)));
                    cfgXLastVal = xRaw;
                }

                // Y → randomise step-type sequence on any large turn.
                int32_t yMoved = yRaw - cfgYLastVal;
                if (yMoved < 0) yMoved = -yMoved;
                if (yMoved >= RANDOMIZE_TURN_THRESH) {
                    for (int i = 0; i < NUM_STEPS; i++)
                        stepFlags[i] = TYPE_CHOICES[NextRandom() % 5];
                    cfgYLastVal = yRaw;
                }
            }
        } else if (switchDownActive) {
            switchDownActive = false;
            configModeActive = false;
            if (!longPressHandled) {
                seq.editStep = (seq.editStep + 1) % NUM_STEPS;
                semiPickedUp = false;
                octPickedUp  = false;
                modePickedUp = false;
                // Preview: fire gate and voice only if step is not a rest.
                if (!(stepFlags[seq.editStep] & FLAG_REST)) {
                    trigCounter = TRIGGER_LEN;
                } else {
                    trigCounter = 0;
                }
            }
        }

        // Switching which control useExtClock's X/Y drive: drop the pickups for
        // the pair coming into use so they don't jump when the mode changes,
        // regardless of what the Z switch is doing at that instant.
        if (useExtClock && !wasExtClock) {
            divPickedUp      = false;
            widthPickedUp    = false;
            divKnobAtReset   = KnobVal(Knob::X);
            widthKnobAtReset = KnobVal(Knob::Y);
        } else if (!useExtClock && wasExtClock) {
            swingPickedUp    = false;
            tempoPickedUp    = false;
            swingKnobAtReset = KnobVal(Knob::X);
            tempoKnobAtReset = KnobVal(Knob::Y);
            tempoTargetRaw   = TempoToKnobRaw(ticksPerStep);
        }
        wasExtClock = useExtClock;

        // Measure the external clock's one-step period, and derive Pulse Out 2's
        // (accent trigger) gate width from it. Runs unconditionally (not gated
        // on play/edit) so the width is current the moment playback resumes.
        // The Pulse Out 1 divided clock is fired from the pattern's own step
        // position instead — see SEQUENCER ADVANCE below.
        if (useExtClock) {
            extClockTickCounter++;
            if (PulseIn1RisingEdge()) {
                extClockPeriod      = extClockTickCounter;
                extClockTickCounter = 0;
                extPulseWidthTicks  = ExtPulseWidthTicks(extClockPeriod, extPulseWidthRaw);
            }
        }

        // ── KNOB HANDLING ─────────────────────────────────────────────────────
        if (sw == Switch::Up) {
            // --- Play mode: Main = length, X = swing, Y = tempo ---
            int32_t mainRaw = KnobVal(Knob::Main);
            int32_t xRaw    = KnobVal(Knob::X);
            int32_t yRaw    = KnobVal(Knob::Y);

            // Main → sequence length 1..16
            int32_t knobLen = 1 + ((mainRaw * 16) >> 12);
            if (knobLen > 16) knobLen = 16;
            if (!lengthPickedUp) {
                int32_t moved = mainRaw - lengthKnobAtReset;
                if (moved < 0) moved = -moved;
                if (moved >= KNOB_MOVE_THRESH) {
                    int32_t d = knobLen - static_cast<int32_t>(seq.seqLength);
                    if (d < 0) d = -d;
                    if (d <= PICKUP_THRESH_LENGTH) lengthPickedUp = true;
                }
            }
            if (lengthPickedUp) seq.seqLength = static_cast<uint8_t>(knobLen);

            // X → swing amount (movement-only latch) when running on the
            // internal clock, or clock division (classic pickup, like length)
            // when Pulse In 1 is patched.
            // Swing: 0 = straight (50/50), CW = up to ~33% offset (triplet
            // feel). On-beat steps (0,2,4,...) are extended; off-beat steps
            // shortened by the same amount, so total pair duration = 2×ticksPerStep.
            // Division: /1 (CCW) up to /16 (CW), dividing Pulse Out 1's clock output.
            if (useExtClock) {
                int32_t knobDiv = 1 + ((xRaw * 8) >> 12);
                if (knobDiv > 8) knobDiv = 8;
                if (!divPickedUp) {
                    int32_t moved = xRaw - divKnobAtReset;
                    if (moved < 0) moved = -moved;
                    if (moved >= KNOB_MOVE_THRESH) {
                        int32_t d = knobDiv - static_cast<int32_t>(extClockDivision);
                        if (d < 0) d = -d;
                        if (d <= PICKUP_THRESH_LENGTH) divPickedUp = true;
                    }
                }
                if (divPickedUp) extClockDivision = static_cast<uint8_t>(knobDiv);
            } else {
                if (!swingPickedUp) {
                    int32_t moved = xRaw - swingKnobAtReset;
                    if (moved < 0) moved = -moved;
                    if (moved >= KNOB_MOVE_THRESH) swingPickedUp = true;
                }
                if (swingPickedUp) swingFraction = xRaw;
            }

            // Y → tempo (classic pickup so it doesn't jump on mode switch),
            // or Pulse Out 1/2 gate width (movement-only latch, like swing)
            // when Pulse In 1 is patched.
            // Tempo pickup compares raw knob position against tempoTargetRaw
            // (the inverse of the curve below), not tempo value — see
            // PICKUP_THRESH_TEMPO_RAW for why.
            if (!useExtClock) {
                int32_t inv       = 4095 - yRaw;
                int32_t knobTempo = 1600 + ((inv * inv * 6) >> 10);
                if (!tempoPickedUp) {
                    int32_t moved = yRaw - tempoKnobAtReset;
                    if (moved < 0) moved = -moved;
                    if (moved >= KNOB_MOVE_THRESH) {
                        int32_t d = yRaw - tempoTargetRaw;
                        if (d < 0) d = -d;
                        if (d <= PICKUP_THRESH_TEMPO_RAW) tempoPickedUp = true;
                    }
                }
                if (tempoPickedUp) ticksPerStep = static_cast<uint32_t>(knobTempo);
            } else {
                if (!widthPickedUp) {
                    int32_t moved = yRaw - widthKnobAtReset;
                    if (moved < 0) moved = -moved;
                    if (moved >= KNOB_MOVE_THRESH) widthPickedUp = true;
                }
                if (widthPickedUp) extPulseWidthRaw = yRaw;
            }

        } else if (sw == Switch::Middle) {
            // --- Edit mode: Main = semitone, X = octave, Y = step mode ---
            // All three use movement-only latch: as soon as the knob moves
            // ≥ KNOB_MOVE_THRESH from where it was when the step was loaded,
            // it takes over. Flicking to the next step never disturbs any value.
            uint8_t editStep = seq.editStep;
            int32_t mainRaw  = KnobVal(Knob::Main);
            int32_t xRaw     = KnobVal(Knob::X);
            int32_t yRaw     = KnobVal(Knob::Y);

            // Cursor moved to a new step → fresh pickup.
            if (editStep != lastEditStep) {
                semiPickedUp    = false;
                octPickedUp     = false;
                modePickedUp    = false;
                semiKnobAtReset = mainRaw;
                octKnobAtReset  = xRaw;
                modeKnobAtReset = yRaw;
                editPreviewNote = static_cast<int32_t>(seq.pitch[editStep]);
            }
            lastEditStep = static_cast<int8_t>(editStep);

            // Decompose stored note (clamped to the three-octave edit range).
            int32_t rel = static_cast<int32_t>(seq.pitch[editStep]) - OCTAVE_BASE;
            if (rel < 0) rel = 0;
            int32_t storedOct  = rel / 12;
            int32_t storedSemi = rel % 12;
            if (storedOct < OCTAVE_MIN) {
                storedOct = OCTAVE_MIN;
                storedSemi = 0;
            } else if (storedOct > OCTAVE_MAX) {
                storedOct = OCTAVE_MAX;
                storedSemi = 11;
            }

            // Where the knobs point.
            int32_t knobSemi = (mainRaw * 12) >> 12;
            if (knobSemi > 11) knobSemi = 11;
            int32_t knobOctOffset = (xRaw * OCTAVE_COUNT) >> 12;
            if (knobOctOffset >= OCTAVE_COUNT) knobOctOffset = OCTAVE_COUNT - 1;
            int32_t knobOct = OCTAVE_DEFAULT + knobOctOffset - 1;

            // Movement-only latch for semitone and octave.
            if (!semiPickedUp) {
                int32_t moved = mainRaw - semiKnobAtReset;
                if (moved < 0) moved = -moved;
                if (moved >= KNOB_MOVE_THRESH) semiPickedUp = true;
            }
            if (!octPickedUp) {
                int32_t moved = xRaw - octKnobAtReset;
                if (moved < 0) moved = -moved;
                if (moved >= KNOB_MOVE_THRESH) octPickedUp = true;
            }

            int32_t newSemi = semiPickedUp ? knobSemi : storedSemi;
            int32_t newOct  = octPickedUp  ? knobOct  : storedOct;
            int32_t note    = OCTAVE_BASE + newOct * 12 + newSemi;
            if (note < 0)   note = 0;
            if (note > 127) note = 127;
            seq.pitch[editStep] = static_cast<uint8_t>(note);

            // Turning Main/X actually changed the pitch → re-fire the gate so
            // the previewed note is audible (rest steps stay silent).
            if (note != editPreviewNote) {
                editPreviewNote = note;
                if (!(stepFlags[editStep] & FLAG_REST)) trigCounter = TRIGGER_LEN;
            }

            // Y → step mode (movement-only latch).
            // Zone 0 = rest, 1 = normal, 2 = accent, 3 = slide, 4 = accent+slide
            // (five equal fifths of the knob range).
            if (!modePickedUp) {
                int32_t moved = yRaw - modeKnobAtReset;
                if (moved < 0) moved = -moved;
                if (moved >= KNOB_MOVE_THRESH) modePickedUp = true;
            }
            if (modePickedUp) {
                uint8_t newFlags;
                int32_t zone = (yRaw * 5) >> 12; // 0..4
                if (zone > 4) zone = 4;
                if      (zone == 0) newFlags = FLAG_REST;
                else if (zone == 1) newFlags = 0;
                else if (zone == 2) newFlags = FLAG_ACCENT;
                else if (zone == 3) newFlags = FLAG_SLIDE;
                else                newFlags = FLAG_ACCENT | FLAG_SLIDE;
                if (newFlags != stepFlags[editStep]) {
                    stepFlags[editStep] = newFlags;
                    // Preview the step mode's gate/accent behaviour immediately.
                    if (newFlags & FLAG_REST) {
                        trigCounter       = 0;
                        accentTrigCounter = 0;
                    } else if (newFlags & FLAG_SLIDE) {
                        trigCounter       = SLIDE_PREVIEW_LEN;
                        accentTrigCounter = (newFlags & FLAG_ACCENT) ? TRIGGER_LEN : 0;
                    } else if (newFlags & FLAG_ACCENT) {
                        trigCounter       = TRIGGER_LEN;
                        accentTrigCounter = TRIGGER_LEN;
                    } else { // normal
                        trigCounter       = TRIGGER_LEN;
                        accentTrigCounter = 0;
                    }
                }
            }
        }

        // ── RESET INPUT ───────────────────────────────────────────────────────
        // Latched, not applied immediately: a reset faster than the current
        // tempo would otherwise keep re-zeroing tickCounter and never let the
        // sequence actually advance. Applied at the next step-advance instead
        // (see SEQUENCER ADVANCE below).
        if (PulseIn2RisingEdge()) {
            pendingReset = true;
        }

        // Snap back if length was shortened past the current position.
        if (seq.pitchStep >= seq.seqLength) seq.pitchStep = 0;
        if (seq.typeStep  >= seq.seqLength) seq.typeStep  = 0;

        // ── SEQUENCER ADVANCE ─────────────────────────────────────────────────
        if (playing) {
            bool advance = false;
            if (useExtClock) {
                if (PulseIn1RisingEdge()) advance = true;
            } else {
                // Swing: on-beat steps (0,2,4,...) are longer; off-beat steps shorter.
                // swingOffset = ticksPerStep * swingFraction / 12288, capped at 1/3.
                // Both products fit in uint32_t (max 99844 * 4095 ≈ 409M < 2^32).
                uint32_t swingOffset = (uint32_t)swingFraction * ticksPerStep / 12288;
                uint32_t stepTicks   = (beatIndex % 2 == 0)
                    ? ticksPerStep + swingOffset
                    : ticksPerStep - swingOffset;
                tickCounter++;
                if (tickCounter >= stepTicks) {
                    tickCounter = 0;
                    advance = true;
                }
            }

            if (advance) {
                beatIndex++;

                // Check whether the type-sequence step we're leaving had the slide
                // flag — portamento starts on the SUBSEQUENT step (303 behaviour).
                uint8_t prevTypeRead = seq.typeReadStep;
                bool    prevHadSlide = (stepFlags[prevTypeRead] & FLAG_SLIDE) != 0;

                uint8_t len = seq.seqLength;

                // Apply any pending reset now, exactly on this step boundary,
                // instead of incrementing the affected sequence(s) normally.
                bool resetPitchNow = false, resetTypeNow = false;
                if (pendingReset) {
                    pendingReset = false;
                    switch (resetMode) {
                    case 0: // traditional: both sequences to step 0
                        seq.pitchStep      = 0;
                        seq.typeStep       = 0;
                        seq.pitchDir       = 1;
                        seq.typeDir        = 1;
                        pitchReversedState = false;
                        typeReversedState  = false;
                        resetPitchNow = true;
                        resetTypeNow  = true;
                        break;
                    case 1: // pitch sequence to step 0 only
                        seq.pitchStep      = 0;
                        seq.pitchDir       = 1;
                        pitchReversedState = false;
                        resetPitchNow = true;
                        break;
                    case 2: // step-type sequence to step 0 only
                        seq.typeStep      = 0;
                        seq.typeDir       = 1;
                        typeReversedState = false;
                        resetTypeNow = true;
                        break;
                    case 3: // reverse pitch: toggles on each trigger
                        pitchReversedState = !pitchReversedState;
                        if (pitchReversedState) {
                            seq.pitchStep = len - 1;
                            seq.pitchDir  = -1;
                            resetPitchNow = true;
                        } else {
                            seq.pitchDir = 1; // resume forward from the normal increment below
                        }
                        break;
                    case 4: // reverse step-type: toggles on each trigger
                        typeReversedState = !typeReversedState;
                        if (typeReversedState) {
                            seq.typeStep = len - 1;
                            seq.typeDir  = -1;
                            resetTypeNow = true;
                        } else {
                            seq.typeDir = 1; // resume forward from the normal increment below
                        }
                        break;
                    }
                    glideActive = false;
                }

                if (!resetPitchNow)
                    seq.pitchStep = WrapStep(static_cast<int32_t>(seq.pitchStep) + seq.pitchDir, len);
                if (!resetTypeNow)
                    seq.typeStep  = WrapStep(static_cast<int32_t>(seq.typeStep)  + seq.typeDir,  len);

                // Divided Pulse Out 1 output: fires on pattern steps 0, N, 2N, ...
                // (N = extClockDivision), so it's always phase-locked to the
                // pattern's own step 0 rather than counting edges since some
                // arbitrary start. Width is proportional to the divided period —
                // at max Y, the gate spans almost the full N-step interval.
                if ((seq.typeStep % extClockDivision) == 0) {
                    if (useExtClock) {
                        uint32_t dividedPeriod = extClockPeriod * extClockDivision;
                        extClockOutCounter = static_cast<int>(
                            ExtPulseWidthTicks(dividedPeriod, extPulseWidthRaw));
                    } else {
                        // Preserve the external clock's division and width
                        // settings when playback falls back to the internal clock.
                        uint32_t dividedPeriod = ticksPerStep * extClockDivision;
                        clockCounter = static_cast<int>(
                            ExtPulseWidthTicks(dividedPeriod, extPulseWidthRaw));
                    }
                }
                if (!useExtClock)
                    extPulseWidthTicks = ExtPulseWidthTicks(ticksPerStep, extPulseWidthRaw);

                // Sampled once per step, not continuously — see pitchReadStep comment above.
                pitchOffset = VoltageToOffset(AudioIn1());
                typeOffset  = VoltageToOffset(AudioIn2());

                // Apply phase shifts across the full programmed pattern, even
                // when playback is truncated to a shorter loop.
                seq.pitchReadStep = WrapStep(static_cast<int32_t>(seq.pitchStep) + pitchOffset,
                                             NUM_STEPS);
                seq.typeReadStep  = WrapStep(static_cast<int32_t>(seq.typeStep)  + typeOffset,
                                             NUM_STEPS);

                uint8_t typeReadStep = seq.typeReadStep;
                bool    isRest    = (stepFlags[typeReadStep] & FLAG_REST)   != 0;
                bool    hasSlide  = (stepFlags[typeReadStep] & FLAG_SLIDE)  != 0;
                bool    hasAccent = (stepFlags[typeReadStep] & FLAG_ACCENT) != 0;

                if (isRest) {
                    trigCounter       = 0;
                    currentStepAccent = false;
                    glideActive       = false;
                } else {
                    // Slide flag on a step holds the gate for the full step (legato);
                    // the portamento itself glides into the NEXT step.
                    // Normal/Accent: short ~2ms trigger, pitch snaps immediately.
                    trigCounter       = hasSlide ? static_cast<int>(ticksPerStep) : TRIGGER_LEN;
                    currentStepAccent = hasAccent;
                    if (hasAccent)
                        accentTrigCounter = static_cast<int>(extPulseWidthTicks);

                    if (prevHadSlide) {
                        // Begin portamento into this step. slideNoteMV256 already holds
                        // the previous step's pitch (maintained by the non-glide branch).
                        int32_t cvT      = VoltageToTranspose(CVIn1());
                        int32_t thisNote = static_cast<int32_t>(seq.pitch[seq.pitchReadStep]) + cvT;
                        if (thisNote < 0) thisNote = 0; if (thisNote > 127) thisNote = 127;
                        targetNoteMV256 = (thisNote - 60) * 21333;
                        glideActive     = true;
                    } else {
                        glideActive = false;
                    }
                }
            }
        }

        // ── OUTPUTS ───────────────────────────────────────────────────────────
        uint8_t outputPitchStep = playing ? seq.pitchReadStep : seq.editStep;
        uint8_t outputTypeStep  = playing ? seq.typeReadStep  : seq.editStep;

        int32_t cvTranspose = VoltageToTranspose(CVIn1());
        int32_t note = static_cast<int32_t>(seq.pitch[outputPitchStep]) + cvTranspose;
        if (note < 0)   note = 0;
        if (note > 127) note = 127;

        // CV Out 1: pitch with portamento on the step that follows a slide step.
        // Portamento is a fixed-time exponential (one-pole RC) slew, not a
        // fixed-rate linear glide. With base shift 10, alpha = 1/1024 per
        // sample at 48 kHz, giving tau ≈ 21.5 ms and f_c ≈ 7.4 Hz, matching
        // the TB-303's analog portamento. Larger intervals therefore move
        // faster initially but take proportionally longer to settle.
        // CV In 2 only ever slows this rate down (raises the shift),
        // by the same amount whichever direction it's turned away from 0V.
        // Target is re-derived every sample so CV-In-1 transpose tracks live.
        if (playing && glideActive) {
            int32_t thisNote = static_cast<int32_t>(seq.pitch[seq.pitchReadStep]) + cvTranspose;
            if (thisNote < 0)   thisNote = 0;
            if (thisNote > 127) thisNote = 127;
            targetNoteMV256 = (thisNote - 60) * 21333;

            int32_t cv2Mag = CVIn2();
            if (cv2Mag < 0) cv2Mag = -cv2Mag;
            int32_t portaShift = PORTA_BASE_SHIFT + (cv2Mag * 12) / 2048;
            if (portaShift > 14) portaShift = 14;
            slideNoteMV256 += (targetNoteMV256 - slideNoteMV256) >> portaShift;
            CVOut1Millivolts(slideNoteMV256 >> 8);
        } else {
            slideNoteMV256 = (note - 60) * 21333; // keep in sync for clean glide start
            CVOut1MIDINote(static_cast<uint8_t>(note));
        }

        // CV Out 2: accent CV — indicates whether this step is accented.
        {
            bool accent = playing ? currentStepAccent
                                  : (stepFlags[outputTypeStep] & FLAG_ACCENT) != 0;
            CVOut2(accent ? 2047 : 0);
        }

        // Audio Out 2: gate, output as a bi-level audio signal (moved from Pulse Out 1).
        AudioOut2(trigCounter > 0 ? 2047 : 0);
        if (trigCounter > 0) trigCounter--;

        // Pulse Out 2: accent trigger.
        PulseOut2(accentTrigCounter > 0);
        if (accentTrigCounter > 0) accentTrigCounter--;

        // Pulse Out 1: clock — a divided, width-adjustable copy of Pulse In 1
        // when connected (see EXTERNAL CLOCK block above), otherwise a short
        // pulse on every internal-clock step advance.
        PulseOut1(useExtClock ? (extClockOutCounter > 0) : (clockCounter > 0));
        if (extClockOutCounter > 0) extClockOutCounter--;
        if (clockCounter > 0) clockCounter--;

        // Audio Out 1: sawtooth oscillator tracking pitch, unshaped (no envelope).
        // Phase increment tracks the output note every sample (including transpose).
        // During portamento it follows CV Out 1's smoothed pitch curve.
        {
            if (playing && glideActive) {
                // slideNoteMV256 is in 1/256 mV relative to MIDI note 60.
                // Convert to 10.10 fixed-point semitones from C0 (MIDI 12):
                //   semitones_from_60_x1024 = slideNoteMV256 / 256 * 12 / 1000 * 1024
                //                           = slideNoteMV256 * 48 / 1000
                int32_t semitonesFromC0_x1024 = 49152 + ((slideNoteMV256 * 48) / 1000);
                if (semitonesFromC0_x1024 < 0)     semitonesFromC0_x1024 = 0;
                if (semitonesFromC0_x1024 > 60416) semitonesFromC0_x1024 = 60416; // 59*1024

                int32_t oct = 0;
                int32_t s = semitonesFromC0_x1024;
                if (s >= 12288) { oct = 1; s -= 12288; }
                if (s >= 12288) { oct = 2; s -= 12288; }
                if (s >= 12288) { oct = 3; s -= 12288; }
                if (s >= 12288) { oct = 4; s -= 12288; }

                int32_t semiIdx = s >> 10;
                int32_t frac    = s & 0x3FF;
                uint32_t ratio  = SEMI_RATIO_EXT[semiIdx] +
                    (((uint32_t)(SEMI_RATIO_EXT[semiIdx + 1] - SEMI_RATIO_EXT[semiIdx]) * frac) >> 10);
                oscPhaseInc = (PHASE_INC_C0 * ratio >> 10) << (uint32_t)oct;
            } else {
                int32_t rel = note - OCTAVE_BASE;
                if (rel < 0) rel = 0;
                if (rel > 59) rel = 59;
                int32_t oct  = rel / 12;
                int32_t semi = rel % 12;
                // No 64-bit needed: PHASE_INC_C0 * SEMI_RATIO[11] ≈ 2.83 billion < 2^32
                oscPhaseInc = (PHASE_INC_C0 * SEMI_RATIO[semi] >> 10) << (uint32_t)oct;
            }
        }
        oscPhase += oscPhaseInc;
        // 12-bit sawtooth centred at 0 (-2048..+2047)
        int32_t saw = static_cast<int32_t>(oscPhase >> 20) - 2048;
        AudioOut1(static_cast<int16_t>(saw));

        // ── LEDS ──────────────────────────────────────────────────────────────
        // Reset-mode menu: LEDs 0–2 show the selected reset mode in binary.
        // Play mode: LEDs 0–3 = step (binary), LED 4 = play, LED 5 = gate blink.
        // Edit mode: LEDs 0–3 = step (binary), LED 4/5 show step mode using
        // brightness rather than a flat on/off, so all five step types get a
        // distinct, readable combination:
        //   LED4 = "note level": off = rest, half = normal, full = accented.
        //   LED5 = "slide flag": off = no slide, full = slide.
        //   (rest/off, normal/half+off, accent/full+off, slide/half+full,
        //    accent+slide/full+full)
        if (configModeActive) {
            for (int i = 0; i < 3; i++) LedOn(i, (resetMode >> i) & 1);
            for (int i = 3; i < 6; i++) LedOn(i, false);
        } else {
            uint8_t displayStep = playing ? seq.typeReadStep : seq.editStep;
            for (int i = 0; i < 4; i++) LedOn(i, (displayStep >> i) & 1);

            if (playing) {
                LedOn(4, true);
                LedOn(5, trigCounter > 0);
            } else {
                uint8_t flags  = stepFlags[displayStep];
                bool    accent = (flags & FLAG_ACCENT) != 0;
                bool    slide  = (flags & FLAG_SLIDE)  != 0;
                bool    rest   = (flags & FLAG_REST)   != 0;
                LedBrightness(4, rest ? 0 : (accent ? 4095 : 2047));
                LedBrightness(5, slide ? 4095 : 0);
            }
        }

        prevMode = sw;
    }
};

int main()
{
    set_sys_clock_khz(144000, true);
    AcidSequencer seq;
    seq.EnableNormalisationProbe();
    seq.Run();
}
