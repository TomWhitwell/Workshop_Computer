#pragma once
#include "../dsp/fixed.hpp"

namespace computer {
// Values match the pinned ComputerCard switch API.
enum Position { Down = 0, Middle = 1, Up = 2 };
enum KnobIndex { MainKnob = 0, LeftKnob = 1, RightKnob = 2, KnobCount = 3 };

// Snapshot taken exclusively in the hardware callback; millivolts are calibrated
// where available. Connected flags distinguish a patched 0 V from an empty jack.
struct Controls {
    int32_t knobs[KnobCount] = {2048, 2048, 2048};
    int32_t pitchMv = 0, bendMv = 0, audio1Mv = 0, audio2Mv = 0;
    bool pitchConnected = false, bendConnected = false;
    bool audio1Connected = false, audio2Connected = false;
    Position position = Middle;
};

struct Settings {
    mtm::Parameters params{};
    int32_t pitchQ16 = -54613; // D3, in Q16 octaves relative to C4.
    bool external = false;
};

int32_t knob(int32_t raw);
int32_t pickKnob(int32_t raw);
int32_t cvWarp(int32_t millivolts);

// Core0 owns this state machine. Updates at 750 Hz and on switch edges.
// Held edits never write flash. Normal values remain frozen until pickup.
class Controller {
public:
    Settings update(const Controls &controls);
    Position previousPosition() const { return previous; }
    int32_t sympathyValue() const { return sympathy; }
    int32_t pickValue() const { return pick; }
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
    int32_t sympathy = 32767, pick = 0;
    int octave = -1;

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
