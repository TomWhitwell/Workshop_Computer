#include "controller.hpp"
#include "editor.hpp"

namespace computer {
namespace {
constexpr int32_t potMaximum = 4095;
constexpr int32_t potCentre = 2048;
constexpr int32_t pickDeadband = 24;
constexpr int32_t nominalInputLimitMv = 6000;
constexpr int32_t modulationTravelMv = 5000;
constexpr int32_t pitchMillivoltsPerOctave = 1000;
constexpr int32_t bendMillivoltsPerOctave = 30000; // +/-5 V gives +/-1/6 octave.
constexpr int32_t octaveQ16 = 65536;
constexpr int32_t normalledD4Q16 = 10923;
} // namespace

int32_t knob(int32_t raw) {
    return mtm::clip(raw, 0, potMaximum) * 32767 / potMaximum;
}

int32_t pickKnob(int32_t raw) {
    const int32_t centred = mtm::clip(raw, 0, potMaximum) - potCentre;
    if (mtm::abs32(centred) <= pickDeadband) {
        return 0;
    }
    return centred < 0 ? (centred + pickDeadband) * 32767 / 2024
                       : mtm::clip((centred - pickDeadband) * 32767 / 2023);
}

int32_t cvWarp(int32_t millivolts) {
    return mtm::clip(mtm::clip(millivolts, -nominalInputLimitMv, nominalInputLimitMv) * 32767 /
                     modulationTravelMv);
}

int32_t Controller::ModJack::next(bool connected, int32_t millivolts) {
    // Wait ~16 ms for a stable probe result, then ramp over ~43 ms. Removing
    // a cable clears its target immediately; audio-rate smoothing follows.
    if (!connected) {
        stable = 0;
        gain = 0;
        return 0;
    }
    if (stable < 12) {
        ++stable;
        return 0;
    }
    gain = std::min<int32_t>(32767, gain + 1024);
    return mtm::mul(cvWarp(millivolts), gain);
}

void Controller::initialiseKnobs(const int32_t (&raw)[KnobCount]) {
    for (int i = 0; i < KnobCount; ++i) {
        base[i] = last[i] = raw[i];
    }
    initialized = true;
}

void Controller::beginHeldEdit(const int32_t (&raw)[KnobCount]) {
    for (int i = 0; i < KnobCount; ++i) {
        entry[i] = raw[i];
        moved[i] = false;
    }
}

void Controller::updateOctave(int32_t raw) {
    const int candidate = std::min<int>(6, (raw * 7) / 4096) - 3;
    const int boundary = candidate > octave ? ((octave + 4) * 4096 / 7) : ((octave + 3) * 4096 / 7);
    if ((candidate > octave && raw > boundary + octaveHysteresis) ||
        (candidate < octave && raw < boundary - octaveHysteresis)) {
        octave = candidate;
    }
}

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
int32_t Controller::heldAssignment(unsigned knobIndex) const {
    return knobIndex < KnobCount ? heldAssignments[knobIndex] : int32_t(HeldDisabled);
}
bool Controller::setHeldAssignment(unsigned knobIndex, int32_t assignment) {
    if (knobIndex >= KnobCount || assignment < HeldDisabled || assignment > HeldAssignmentMax ||
        previous == Down) {
        return false;
    }
    heldAssignments[knobIndex] = assignment;
    return true;
}

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
    settings.params = {knob(base[RightKnob]), knob(base[MainKnob]), knob(base[LeftKnob]), sympathy,
                       pick};
    settings.params.pickPositionPercent = pickPositionPercent;
    settings.params.haloLevel = haloLevelPercent * 32768 / 100;
    settings.params.melodyDecayPercent = melodyDecayPercent;
    settings.params.sympatheticDecayPercent = sympatheticDecayPercent;
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
        return (resonatorMix * 100 + 16384) / 32768;
    default:
        return 0;
    }
}
} // namespace computer
