/// @file controller.cpp
/// @brief Implementation of the core0 knob/switch control state machine.
///
/// See controller.hpp for ownership and the held-edit/pickup contract. This
/// translation unit adds the raw-to-Q15 knob scalings, the probe-based CV
/// modulation ramps, the octave hysteresis and the bounded editor
/// read/write funnel that both the panel state machine and the USB editor
/// bridge use. Everything runs on core0 at the control cadence and stays
/// free of allocation, blocking and flash access.
#include "controller.hpp"
#include "editor.hpp"

namespace computer {
namespace {
/// @brief Analogue scaling constants; counts are 12-bit ADC units, the CV
///        constants are millivolts and the pitch words are Q16.
constexpr int32_t potMaximum = 4095;
/// @brief Electrical centre of the centre-off pick pot, in counts.
constexpr int32_t potCentre = 2048;
/// @brief Counts around centre that read as "no release correction".
constexpr int32_t pickDeadband = 24;
/// @brief Analogue input clip applied to pitch/bend CV, in millivolts.
constexpr int32_t nominalInputLimitMv = 6000;
/// @brief Modulation travel: millivolts that map to full-scale Q15 warp.
constexpr int32_t modulationTravelMv = 5000;
/// @brief 1 V/octave pitch-CV sensitivity, in millivolts/octave.
constexpr int32_t pitchMillivoltsPerOctave = 1000;
constexpr int32_t bendMillivoltsPerOctave = 30000; // +/-5 V gives +/-1/6 octave.
/// @brief One octave expressed in the Q16 pitch word (65536).
constexpr int32_t octaveQ16 = 65536;
/// @brief Unpatched CV1 normal: D4, i.e. 1/6 octave above C4, in Q16.
constexpr int32_t normalledD4Q16 = 10923;
} // namespace

/// @brief Scale a raw knob count to an unsigned Q15 fraction.
/// @param raw 12-bit ADC count (0..4095); out-of-range inputs saturate.
/// @return Q15 fraction of travel, raw * 32767 / 4095 (0..32767).
int32_t knob(int32_t raw) {
    return mtm::clip(raw, 0, potMaximum) * 32767 / potMaximum;
}

/// @brief Scale the centre-off pick knob to a signed Q15 release value,
///        keeping a deadband at electrical centre.
/// @param raw 12-bit ADC count (0..4095).
/// @return Bipolar Q15; 0 inside the deadband, full scale at the extremes.
int32_t pickKnob(int32_t raw) {
    const int32_t centred = mtm::clip(raw, 0, potMaximum) - potCentre;
    if (mtm::abs32(centred) <= pickDeadband) {
        return 0;
    }
    // Usable spans past the deadband (2024 counts left, 2023 right) each
    // scale to full Q15 travel despite the pot's off-centre range.
    return centred < 0 ? (centred + pickDeadband) * 32767 / 2024
                       : mtm::clip((centred - pickDeadband) * 32767 / 2023);
}

/// @brief Warp a modulation-source millivolt reading to a bipolar Q15 factor.
/// @param millivolts probe reading, clipped to the input limit first.
/// @return Q15 in [-32767, 32767]; +/- 5000 mV of travel is full scale.
int32_t cvWarp(int32_t millivolts) {
    // Readings beyond +/-5000 mV multiply past Q15 full scale; re-clip.
    return mtm::clip(mtm::clip(millivolts, -nominalInputLimitMv, nominalInputLimitMv) * 32767 /
                     modulationTravelMv);
}

/// @brief Fold one probe sample into a CV column's stability counter and
///        modulation gain ramp.
/// @param connected Patch state of the modulation jack.
/// @param millivolts Jack millivolt reading (modulation source).
/// @return Q15 modulation contribution, cvWarp(millivolts) * gain.
int32_t Controller::ModJack::next(bool connected, int32_t millivolts) {
    // Wait ~16 ms for a stable probe result, then ramp over ~43 ms. Removing
    // a cable clears its target immediately; audio-rate smoothing follows.
    if (!connected) {
        stable = 0;
        gain = 0;
        return 0;
    }
    // Twelve confirmed ticks of presence (~16 ms at the control cadence)
    // before this column may contribute at all.
    if (stable < 12) {
        ++stable;
        return 0;
    }
    gain = std::min<int32_t>(32767, gain + 1024);
    return mtm::mul(cvWarp(millivolts), gain);
}

/// @brief Record the first knob readings as the saved normal positions
///        (base = last = raw) and finish lazy initialisation.
/// @param raw Clipped knob readings for this tick.
void Controller::initialiseKnobs(const int32_t (&raw)[KnobCount]) {
    for (int i = 0; i < KnobCount; ++i) {
        base[i] = last[i] = raw[i];
    }
    initialized = true;
}

/// @brief Begin a held-edit session on the Down edge: capture entry
///        positions and clear the armed flags for all knobs.
/// @param raw Clipped knob readings at the Down edge.
void Controller::beginHeldEdit(const int32_t (&raw)[KnobCount]) {
    for (int i = 0; i < KnobCount; ++i) {
        entry[i] = raw[i];
        moved[i] = false;
    }
}

/// @brief Step the octave offset from one knob reading, with hysteresis.
/// @param raw Clipped knob reading driving the held Octave assignment.
///
/// The 0..4095 turn is a seven-step ladder (~585 counts per step) mapping
/// to octaves -3..+3; a transition only settles once the reading has moved
/// a further octaveHysteresis (36) counts past the step edge it crosses.
void Controller::updateOctave(int32_t raw) {
    const int candidate = std::min<int>(6, (raw * 7) / 4096) - 3;
    // Up moves need the lower edge of the next step plus margin; down moves
    // need the current step's lower edge minus margin.
    const int boundary = candidate > octave ? ((octave + 4) * 4096 / 7) : ((octave + 3) * 4096 / 7);
    if ((candidate > octave && raw > boundary + octaveHysteresis) ||
        (candidate < octave && raw < boundary - octaveHysteresis)) {
        octave = candidate;
    }
}

/// @brief Service armed knobs during a held-edit session.
/// @param raw Clipped knob readings for this tick.
void Controller::updateHeldEdits(const int32_t (&raw)[KnobCount]) {
    // Duplicate assignments are allowed. Main, X, Y are processed in that order,
    // so the last armed knob wins when more than one addresses the same setting.
    for (int i = 0; i < KnobCount; ++i) {
        if (mtm::abs32(raw[i] - entry[i]) >= movement) {
            moved[i] = true;
        }
        if (!moved[i] || heldAssignments[i] == HeldDisabled) {
            continue;
        }
        const int id = heldAssignments[i];
        if (id == HeldOctave) {
            updateOctave(raw[i]);
        } else if (id == HeldPick) {
            pick = pickKnob(raw[i]);
        } else {
            // IDs 1 and 4..8 correspond to editor parameters 3 and 6..10.
            const int parameter = id + 2;
            const auto &d = editor::descriptors[parameter];
            const int32_t value = d.minimum + raw[i] * (d.maximum - d.minimum) / potMaximum;
            setEditorValue(parameter, value);
        }
    }
}

/// @brief Current held-assignment ID stored for one knob column.
/// @param knobIndex 0..2 (Main/X/Y).
/// @return HeldAssignment value for the knob; HeldDisabled on error.
int32_t Controller::heldAssignment(unsigned knobIndex) const {
    return knobIndex < KnobCount ? heldAssignments[knobIndex] : int32_t(HeldDisabled);
}

/// @brief Change which secondary setting a knob drives while Down is held.
/// @param knobIndex 0..2 (Main/X/Y).
/// @param assignment HeldAssignment value (0..HeldAssignmentMax).
/// @return False for an out-of-range index or ID, or while the Down switch
///         was held on the previous tick; stores the ID otherwise.
bool Controller::setHeldAssignment(unsigned knobIndex, int32_t assignment) {
    if (knobIndex >= KnobCount || assignment < HeldDisabled || assignment > HeldAssignmentMax ||
        previous == Down) {
        return false;
    }
    heldAssignments[knobIndex] = assignment;
    return true;
}

/// @brief Maintain the saved normal knob positions and graduate pickups.
/// @param raw Clipped knob readings for this tick.
///
/// A knob armed during a Down session latches a pickup hold on release:
/// its saved base keeps driving the DSP until the player re-approaches that
/// base within +/-24 counts or crosses it this tick. While awaiting pickup,
/// base stays pinned at the saved value; otherwise it tracks the raw input.
void Controller::updateNormalKnobs(const int32_t (&raw)[KnobCount]) {
    if (previous == Down) {
        for (int i = 0; i < KnobCount; ++i) {
            if (moved[i]) {
                pickup[i] = true;
            }
        }
    }
    for (int i = 0; i < KnobCount; ++i) {
        const bool nearSaved = mtm::abs32(raw[i] - base[i]) <= pickupWindow;
        const bool crossedUp = last[i] < base[i] && raw[i] >= base[i];
        const bool crossedDown = last[i] > base[i] && raw[i] <= base[i];
        if (pickup[i] && (nearSaved || crossedUp || crossedDown)) {
            pickup[i] = false;
        }
        if (!pickup[i]) {
            base[i] = raw[i];
        }
    }
}

/// @brief Convert pitch/bend CV plus the octave offset to the pitch word.
/// @param controls Sample snapshot with CV1/bend millivolts and patch flags.
/// @return Q16 octaves relative to C4, hard-clipped; unpatched CV1 normals
///         to D4 via normalledD4Q16.
int32_t Controller::mapPitch(const Controls &controls) const {
    const int32_t pitch =
        controls.pitchConnected
            ? mtm::clip(controls.pitchMv, -nominalInputLimitMv, nominalInputLimitMv) * octaveQ16 /
                  pitchMillivoltsPerOctave
            : normalledD4Q16;
    const int32_t bend = controls.bendConnected ? mtm::clip(controls.bendMv, -nominalInputLimitMv,
                                                            nominalInputLimitMv) *
                                                      octaveQ16 / bendMillivoltsPerOctave
                                                : 0;
    // Raw 0 V = C4; default octave -1 yields patched C3 and unpatched D3.
    return mtm::clip(pitch + octave * octaveQ16 + bend, -177500, 192000);
}

/// @brief Advance one control tick: knobs, held edits, pickups, CV probes,
///        pitch mapping, and assemble the worker snapshot.
/// @param controls Latest hardware sample (see Controls).
/// @return A fresh Settings for the DSP worker.
Settings Controller::update(const Controls &controls) {
    int32_t raw[KnobCount];
    for (int i = 0; i < KnobCount; ++i) {
        raw[i] = mtm::clip(controls.knobs[i], 0, potMaximum);
    }
    if (!initialized) {
        initialiseKnobs(raw);
    }
    const bool down = controls.position == Down;
    if (down && previous != Down) {
        beginHeldEdit(raw);
    }
    if (down) {
        updateHeldEdits(raw);
    } else {
        updateNormalKnobs(raw);
    }
    for (int i = 0; i < KnobCount; ++i) {
        last[i] = raw[i];
    }
    previous = controls.position;

    Settings settings;
    settings.resonatorMix = resonatorMix;
    settings.external = controls.position == Up;
    settings.stringConnected = controls.audio1Connected;
    settings.haloConnected = controls.audio2Connected;
    // Saved bases (not live raw readings) drive the panel parameters, so
    // values stay frozen while a knob waits out its pickup hold.
    settings.params = {knob(base[RightKnob]), knob(base[MainKnob]), knob(base[LeftKnob]), sympathy,
                       pick};
    settings.params.pickPositionPercent = pickPositionPercent;
    settings.params.haloLevel = haloLevelPercent * 32768 / 100;
    settings.params.melodyDecayPercent = melodyDecayPercent;
    settings.params.sympatheticDecayPercent = sympatheticDecayPercent;
    // CV probes contribute only outside Up/external mode.
    const int32_t jawari =
        jawariCV.next(!settings.external && controls.audio1Connected, controls.audio1Mv);
    const int32_t swell =
        swellCV.next(!settings.external && controls.audio2Connected, controls.audio2Mv);
    if (!settings.external) {
        settings.params.jawari = mtm::clip(settings.params.jawari + jawari, 0, 32767);
        settings.params.swell = mtm::clip(settings.params.swell + swell, 0, 32767);
    }
    for (unsigned i = 0; i < 12; ++i) {
        settings.taraf[i] = taraf[i];
    }
    settings.pitchQ16 = mapPitch(controls);
    return settings;
}

/// @brief Editor write boundary: bounded application of every parameter to
///        live state (also used by the held-edit path itself).
/// @param parameter editor::Parameter ordinal (0..editor::Count-1).
/// @param value Editor-unit value, checked against the descriptor bounds.
/// @return True when accepted; false for an unknown index, an out-of-range
///         value, or a held-assignment write while Down was previously held.
bool Controller::setEditorValue(int parameter, int32_t value) {
    if (parameter < 0 || parameter >= editor::Count) {
        return false;
    }
    const auto &d = editor::descriptors[parameter];
    if (value < d.minimum || value > d.maximum) {
        return false;
    }
    if (parameter >= editor::HeldMain && parameter <= editor::HeldY) {
        return setHeldAssignment(unsigned(parameter - editor::HeldMain), value);
    }
    if (parameter >= editor::Taraf1 && parameter <= editor::Taraf12) {
        taraf[parameter - editor::Taraf1] = uint8_t(value);
    } else if (parameter <= editor::String) {
        const int i = parameter == editor::Jawari
                          ? MainKnob
                          : (parameter == editor::Swell ? LeftKnob : RightKnob);
        // Primary knobs adopt the new value as their saved base and arm a
        // pickup immediately; the panel knob must re-approach it to regain
        // normal control.
        base[i] = value;
        pickup[i] = true;
    } else if (parameter == editor::Sympathy) {
        sympathy = value;
    } else if (parameter == editor::Octave) {
        octave = value;
    } else if (parameter == editor::Pick) {
        pick = value;
    } else if (parameter == editor::PickPosition) {
        pickPositionPercent = value;
    } else if (parameter == editor::HaloLevel) {
        haloLevelPercent = value;
    } else if (parameter == editor::MelodyDecay) {
        melodyDecayPercent = value;
    } else if (parameter == editor::SympatheticDecay) {
        sympatheticDecayPercent = value;
    } else if (parameter == editor::PitchInOffset) {
        pitchInputCalibration.offsetMv = value;
    } else if (parameter == editor::PitchInScale) {
        pitchInputCalibration.scalePpm = value;
    } else if (parameter == editor::PitchOutOffset) {
        pitchOutputCalibration.offsetMv = value;
    } else if (parameter == editor::PitchOutScale) {
        pitchOutputCalibration.scalePpm = value;
    } else if (parameter == editor::ResonatorMix) {
        setResonatorMix(value * 32768 / 100);
    }
    return true;
}

/// @brief Editor read boundary: inverse conversions for every parameter.
/// @param parameter editor::Parameter ordinal.
/// @return Current value in editor units; 0 out of range.
int32_t Controller::editorValue(int parameter) const {
    if (parameter >= editor::Taraf1 && parameter < editor::Count) {
        return taraf[parameter - editor::Taraf1];
    }
    switch (parameter) {
    case editor::HeldMain:
    case editor::HeldX:
    case editor::HeldY:
        return heldAssignment(unsigned(parameter - editor::HeldMain));
    case editor::Jawari:
        return base[MainKnob];
    case editor::Swell:
        return base[LeftKnob];
    case editor::String:
        return base[RightKnob];
    case editor::Sympathy:
        return sympathy;
    case editor::Octave:
        return octave;
    case editor::Pick:
        return pick;
    case editor::PickPosition:
        return pickPositionPercent;
    case editor::HaloLevel:
        return haloLevelPercent;
    case editor::MelodyDecay:
        return melodyDecayPercent;
    case editor::SympatheticDecay:
        return sympatheticDecayPercent;
    case editor::PitchInOffset:
        return pitchInputCalibration.offsetMv;
    case editor::PitchInScale:
        return pitchInputCalibration.scalePpm;
    case editor::PitchOutOffset:
        return pitchOutputCalibration.offsetMv;
    case editor::PitchOutScale:
        return pitchOutputCalibration.scalePpm;
    case editor::ResonatorMix:
        // Q15 wet fraction back to percent, rounding half-up.
        return (resonatorMix * 100 + 16384) / 32768;
    default:
        return 0;
    }
}
} // namespace computer
