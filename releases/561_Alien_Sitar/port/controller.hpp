/// @file controller.hpp
/// @brief Core0 knob/switch control state machine and the settings snapshots
///        it publishes to the DSP worker.
///
/// Control/UX layer of the Workshop Computer sitar port. Controller is owned
/// and driven exclusively by core0 (the hardware/control side; the DSP worker
/// runs on core1): it re-evaluates the panel at the 750 Hz control interval
/// and on switch edges (timing::controlInterval in config.hpp) and turns raw
/// 12-bit readings into fixed-point Settings for the worker. All arithmetic
/// is integer fixed point; nothing here allocates, blocks or touches flash.
///
/// Normal mapping is Main = Jawari, X = Swell, Y = String. With the switch
/// held Down, a knob armed by deliberate movement (>= 48 ADC counts from its
/// entry position) instead drives the secondary target assigned through
/// HeldAssignment (persistent storage IDs that must never be renumbered);
/// duplicate assignments resolve in fixed Main, X, Y order. On release, a
/// knob that moved away from its saved normal position must re-approach that
/// position within +/- 24 counts (pickup window) before normal control
/// resumes; octave stepping uses a 36-count hysteresis band.
#pragma once
#include "../dsp/fixed.hpp"
#include "saved_settings.hpp"

namespace computer {
// Values match the pinned ComputerCard switch API.
/// @brief Three-way switch position.
/// Down arms the held-edit state machine; Up selects external excitation;
/// Middle is the normal playing position.
enum Position { Down = 0, Middle = 1, Up = 2 };

/// @brief Panel knob indices.
/// Main is the panel Main control (normal Jawari), left/X is Swell and
/// right/Y is String. The order also fixes held-edit duplicate resolution.
enum KnobIndex { MainKnob = 0, LeftKnob = 1, RightKnob = 2, KnobCount = 3 };

// Persistent wire/storage IDs; never renumber.
/// @brief Secondary target selectable for each knob while Down is held.
/// @details Each value doubles as a storage/wire ID persisted verbatim in
/// saved::State and in the editor's held_* parameters; values must never be
/// renumbered. IDs 1 and 4..8 address editor parameters 3 and 6..10
/// directly (id + 2 in updateHeldEdits); 2 (octave) and 3 (pick) take
/// dedicated paths.
enum HeldAssignment {
    HeldDisabled = 0,         ///< No secondary edit armed for this knob.
    HeldSympathy = 1,         ///< Sympathetic output amount (Q15).
    HeldOctave = 2,           ///< Whole-octave pitch offset (-3..+3).
    HeldPick = 3,             ///< Bipolar release softness/sharpness (Q15).
    HeldPickPosition = 4,     ///< Excitation comb position (% of string).
    HeldHaloLevel = 5,        ///< Independent halo output level (%).
    HeldMelodyDecay = 6,      ///< Melody decay duration multiplier (%).
    HeldSympatheticDecay = 7, ///< Sympathetic decay duration multiplier (%).
    HeldResonatorMix = 8,     ///< External resonator wet fraction (%).
    HeldAssignmentMax = 8     ///< Largest valid assignment (== HeldResonatorMix).
};

// Snapshot taken exclusively in the hardware callback; millivolts are calibrated
// where available. Connected flags distinguish a patched 0 V from an empty jack.
/// @brief One control-tick sample of the panel's analogue inputs, produced by
///        the hardware callback and consumed by Controller::update().
struct Controls {
    /// Raw 12-bit knob readings in Main/left/right order (0..4095).
    int32_t knobs[KnobCount] = {2048, 2048, 2048};
    /// CV1/bend/audio-jack readings in millivolts: pitchMv already carries
    /// the user affine correction; pitchFactoryMv is the factory reading
    /// before it. The audio-jack millivolts serve as modulation probes in
    /// non-external mode.
    int32_t pitchMv = 0, pitchFactoryMv = 0, bendMv = 0, audio1Mv = 0, audio2Mv = 0;
    /// Patch flags for CV1 and the bend input.
    bool pitchConnected = false, bendConnected = false;
    /// Patch flags for the two audio jacks: external excitation feeds when
    /// the switch is Up, modulation probes otherwise.
    bool audio1Connected = false, audio2Connected = false;
    /// Current switch position (see Position).
    Position position = Middle;
};

inline constexpr uint8_t defaultTaraf[12] = {61, 59, 62, 62, 64, 66, 68, 69, 71, 73, 74, 76};

/// @brief Fixed-point settings snapshot for the DSP worker; rebuilt fresh by
///        Controller::update() and shipped inside Packet.
struct Settings {
    /// Panel parameters in mtm::Parameters field order: string/jawari/swell
    /// come from the saved knob bases (base[], not live raw readings),
    /// sympathy/pick from held-edit state.
    mtm::Parameters params{};
    int32_t pitchQ16 = -54613;    // D3, in Q16 octaves relative to C4.
    int32_t resonatorMix = 16384; // Up-only wet fraction, Q15; 0 dry, 32768 wet.
    /// Twelve taraf chord MIDI notes (36..84), retuned with a fade.
    uint8_t taraf[12] = {61, 59, 62, 62, 64, 66, 68, 69, 71, 73, 74, 76};
    /// True when the switch is Up: external excitation mode, CV modulation
    /// of jawari/swell skipped.
    bool external = false;
    /// Patch state of the string/halo input jacks.
    bool stringConnected = false, haloConnected = false;
};

/// @brief Scale a raw knob count to an unsigned Q15 fraction.
/// @param raw 12-bit ADC count (0..4095); out-of-range inputs saturate.
/// @return Q15 fraction of travel, raw * 32767 / 4095 (0..32767).
int32_t knob(int32_t raw);

/// @brief Scale the centre-off pick knob to a signed Q15 release value.
/// @param raw 12-bit ADC count (0..4095).
/// @return 0 within +/- 24 counts of electrical centre; the two usable
///         half-spans after the deadband (2024 counts left, 2023 right)
///         each scale to full Q15 travel, so the ends reach -32767 and
///         +32767 exactly at the pot extremes.
int32_t pickKnob(int32_t raw);

/// @brief Warp a modulation-source millivolt reading to a bipolar Q15 factor.
/// @param millivolts probe reading, clipped to the +/- 6000 mV input limit
///        before scaling.
/// @return Q15 in [-32767, 32767]: +/- 5000 mV of travel maps to full scale
///         and larger readings saturate.
int32_t cvWarp(int32_t millivolts);

// Core0 owns this state machine. Updates at 750 Hz and on switch edges.
// Held edits never write flash. Normal values remain frozen until pickup.
/// @brief Core0 knob/switch state machine, pitch mapping and the editor
///        control surface in one object.
/// @details update() is the only path that consumes hardware samples.
/// setEditorValue()/editorValue() form the single bounded write/read funnel
/// shared by the USB editor bridge and the held-edit logic, so panel and
/// override state stay coherent across both sources. Public pitch
/// calibration members persist with the settings record (saved::Affine).
class Controller {
public:
    /// @brief Advance one control tick and build the worker snapshot.
    /// @param controls Latest hardware sample (see Controls).
    /// @return A fresh Settings reflecting saved knob bases, held-edit
    ///         state, patch flags, CV probe contributions and pitch mapping.
    Settings update(const Controls &controls);
    // Called only by the audio-owned editor bridge at a control boundary.
    /// @brief Apply a protocol parameter write to live state.
    /// @param parameter editor::Parameter ordinal (0..editor::Count-1).
    /// @param value Editor-unit value, checked against
    ///        editor::descriptors[parameter] bounds.
    /// @return True when accepted; false for an unknown index, an
    ///         out-of-range value, or a held-assignment write while the
    ///         Down switch was held on the previous tick. No partial writes.
    bool setEditorValue(int parameter, int32_t value);
    /// @brief Read back a protocol parameter in editor units (the inverse of
    ///        setEditorValue's conversions).
    /// @param parameter editor::Parameter ordinal.
    /// @return Current value; 0 for indices outside [0, editor::Count).
    int32_t editorValue(int parameter) const;
    /// @brief Current held-assignment ID stored for one knob column.
    /// @param knobIndex 0..2 (Main/X/Y).
    /// @return HeldAssignment value for the knob; HeldDisabled on error.
    int32_t heldAssignment(unsigned knobIndex) const;
    /// @brief Change which secondary setting a knob drives while Down is
    ///        held.
    /// @param knobIndex 0..2 (Main/X/Y).
    /// @param assignment HeldAssignment value (0..HeldAssignmentMax).
    /// @return False for an out-of-range index or ID, or while the Down
    ///         switch was held on the previous tick; stores the ID otherwise.
    bool setHeldAssignment(unsigned knobIndex, int32_t assignment);
    /// @brief User CV1/pitch-output affine corrections. The input
    ///        correction applies after the factory millivolt conversion;
    ///        the output correction before the factory output conversion
    ///        (see PERSISTENCE.md and saved::Affine).
    saved::Affine pitchInputCalibration, pitchOutputCalibration;
    /// @brief Switch position from the previous control tick.
    /// @return Down/Middle/Up as last seen by update().
    Position previousPosition() const { return previous; }
    /// @brief Current sympathy amount.
    /// @return Q15 (0..32767) as last written.
    int32_t sympathyValue() const { return sympathy; }
    /// @brief Current pick release value.
    /// @return Bipolar Q15 as last written.
    int32_t pickValue() const { return pick; }
    /// @brief Rescale the external resonator wet fraction.
    /// @param value Q15 fraction; clipped to [0, 32768].
    void setResonatorMix(int32_t value) { resonatorMix = mtm::clip(value, 0, 32768); }
    /// @brief Current octave offset.
    /// @return Whole octaves relative to the factory pitch convention
    ///         (default -1; range -3..+3).
    int octaveOffset() const { return octave; }
    /// @brief Whether one knob is still inside its pickup hold after a held
    ///        edit session.
    /// @param knobIndex 0..2.
    /// @return True while the knob must re-approach or cross its saved
    ///         base before normal control resumes.
    bool awaitingPickup(unsigned knobIndex) const {
        return knobIndex < KnobCount && pickup[knobIndex];
    }

private:
    /// @brief Held-edit and pickup thresholds in ADC counts.
    static constexpr int movement = 48;         ///< Armed threshold from entry position.
    static constexpr int pickupWindow = 24;     ///< Re-approach half-window on release.
    static constexpr int octaveHysteresis = 36; ///< Extra counts between step edges.
    /// @brief Probe-based modulation source for one CV column (jawari/swell).
    /// @details A jack counts as present after ~12 stable control ticks,
    /// then the gain ramps linearly to unity over ~32 more ticks. Removing
    /// the cable collapses the gain immediately; smoothing at audio rate
    /// happens in the DSP.
    struct ModJack {
        unsigned stable = 0; ///< Consecutive ticks with a stable probe.
        int32_t gain = 0;    ///< Ramp gain 0..32767 (Q15).
        int32_t next(bool connected, int32_t millivolts);
    } jawariCV, swellCV;

    /// Per-knob ADC history: base = saved normal position (drives the DSP),
    /// entry = reading on the Down edge, last = previous tick's reading.
    int32_t base[KnobCount]{}, entry[KnobCount]{}, last[KnobCount]{};
    /// initialised = first baseline pass done; moved = armed while Down is
    /// held; pickup = holding for re-approach after a Down session.
    bool initialized = false, moved[KnobCount]{}, pickup[KnobCount]{};
    /// Switch position seen on the previous tick.
    Position previous = Middle;
    /// Held-edit writable secondary state (Q15 / Q15 / Q15).
    int32_t sympathy = 32767, pick = 0, resonatorMix = 16384;
    /// Whole-octave pitch offset, default one octave down, range -3..+3.
    int octave = -1;
    /// Per-knob held-edit targets, committed as HeldAssignment storage IDs.
    int32_t heldAssignments[KnobCount] = {HeldSympathy, HeldOctave, HeldPick};
    /// Editor-unit secondary values owned here, driven via the editor funnel.
    int32_t pickPositionPercent = 17, haloLevelPercent = 50;
    int32_t melodyDecayPercent = 100, sympatheticDecayPercent = 100;
    /// Twelve taraf chord MIDI notes (36..84).
    uint8_t taraf[12] = {61, 59, 62, 62, 64, 66, 68, 69, 71, 73, 74, 76};

    void initialiseKnobs(const int32_t (&raw)[KnobCount]);
    void beginHeldEdit(const int32_t (&raw)[KnobCount]);
    void updateHeldEdits(const int32_t (&raw)[KnobCount]);
    void updateOctave(int32_t raw);
    void updateNormalKnobs(const int32_t (&raw)[KnobCount]);
    int32_t mapPitch(const Controls &controls) const;
};

// Immutable copy submitted to the worker. Pending events are cleared by core0
// only after this packet has entered the queue successfully.
/// @brief One-way transport of a settings snapshot plus pluck/damp events
///        from core0 control to the core1 DSP worker (Queue<Packet>).
/// @details Whole-packet semantics: the worker replaces its settings in one
///        step; pluck/damp are one-shot event flags. core0 clears pending
///        events only after the enqueue succeeds.
struct Packet {
    Settings settings{};
    int32_t stringInput = 0, haloInput = 0; // Filtered Q15 external excitation.
    bool pluck = false, damp = false;
};
} // namespace computer
