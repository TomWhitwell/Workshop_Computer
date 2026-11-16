#pragma once
#include "../dsp/fixed.hpp"
#include "saved_settings.hpp"

namespace computer {
// Values match the pinned ComputerCard switch API.
enum Position { Down = 0, Middle = 1, Up = 2 };
enum KnobIndex { MainKnob = 0, LeftKnob = 1, RightKnob = 2, KnobCount = 3 };
// Persistent wire/storage IDs; never renumber.
enum HeldAssignment {
    HeldDisabled = 0,
    HeldSympathy = 1,
    HeldOctave = 2,
    HeldPick = 3,
    HeldPickPosition = 4,
    HeldHaloLevel = 5,
    HeldMelodyDecay = 6,
    HeldSympatheticDecay = 7,
    HeldResonatorMix = 8,
    HeldAssignmentMax = 8
};

// Snapshot taken exclusively in the hardware callback; millivolts are calibrated
// where available. Connected flags distinguish a patched 0 V from an empty jack.
struct Controls {
    int32_t knobs[KnobCount] = {2048, 2048, 2048};
    int32_t pitchMv = 0, pitchFactoryMv = 0, bendMv = 0, audio1Mv = 0, audio2Mv = 0;
    bool pitchConnected = false, bendConnected = false;
    bool audio1Connected = false, audio2Connected = false;
    Position position = Middle;
};

inline constexpr uint8_t defaultTaraf[12] = {61, 59, 62, 62, 64, 66, 68, 69, 71, 73, 74, 76};

struct Settings {
    mtm::Parameters params{};
    int32_t pitchQ16 = -54613;    // D3, in Q16 octaves relative to C4.
    int32_t resonatorMix = 16384; // Up-only wet fraction, Q15; 0 dry, 32768 wet.
    uint8_t taraf[12] = {61, 59, 62, 62, 64, 66, 68, 69, 71, 73, 74, 76};
    bool external = false;
    bool stringConnected = false, haloConnected = false;
};

int32_t knob(int32_t raw);
int32_t pickKnob(int32_t raw);
int32_t cvWarp(int32_t millivolts);

// Core0 owns this state machine. Updates at 750 Hz and on switch edges.
// Held edits never write flash. Normal values remain frozen until pickup.
class Controller {
public:
    Settings update(const Controls &controls);
    // Called only by the audio-owned editor bridge at a control boundary.
    bool setEditorValue(int parameter, int32_t value);
    int32_t editorValue(int parameter) const;
    int32_t heldAssignment(unsigned knobIndex) const;
    bool setHeldAssignment(unsigned knobIndex, int32_t assignment);
    saved::Affine pitchInputCalibration, pitchOutputCalibration;
    Position previousPosition() const { return previous; }
    int32_t sympathyValue() const { return sympathy; }
    int32_t pickValue() const { return pick; }
    void setResonatorMix(int32_t value) { resonatorMix = mtm::clip(value, 0, 32768); }
    int octaveOffset() const { return octave; }
    bool awaitingPickup(unsigned knobIndex) const {
        return knobIndex < KnobCount && pickup[knobIndex];
    }

private:
    static constexpr int movement = 48;
    static constexpr int pickupWindow = 24;
    static constexpr int octaveHysteresis = 36;
    struct ModJack {
        unsigned stable = 0;
        int32_t gain = 0;
        int32_t next(bool connected, int32_t millivolts);
    } jawariCV, swellCV;

    int32_t base[KnobCount]{}, entry[KnobCount]{}, last[KnobCount]{};
    bool initialized = false, moved[KnobCount]{}, pickup[KnobCount]{};
    Position previous = Middle;
    int32_t sympathy = 32767, pick = 0, resonatorMix = 16384;
    int octave = -1;
    int32_t heldAssignments[KnobCount] = {HeldSympathy, HeldOctave, HeldPick};
    int32_t pickPositionPercent = 17, haloLevelPercent = 50;
    int32_t melodyDecayPercent = 100, sympatheticDecayPercent = 100;
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
struct Packet {
    Settings settings{};
    int32_t stringInput = 0, haloInput = 0; // Filtered Q15 external excitation.
    bool pluck = false, damp = false;
};
} // namespace computer
