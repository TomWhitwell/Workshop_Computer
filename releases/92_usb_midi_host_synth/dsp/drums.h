// Channel-10 pad kit (MIDI notes 36–43): one-shot synth drums.
#pragma once

#include <cstdint>

// MIDI channel 10 is zero-based index 9.
constexpr uint8_t kDrumMidiChannel = 9;
constexpr uint8_t kDrumNoteMin = 36;
constexpr uint8_t kDrumNoteMax = 43;

inline bool isDrumPadNote(uint8_t chan, uint8_t note)
{
    return chan == kDrumMidiChannel && note >= kDrumNoteMin &&
           note <= kDrumNoteMax;
}

void drumsInit();
void drumNoteOn(uint8_t note, uint8_t velocity);
void drumNoteOff(uint8_t note);
void drumsAllOff();

// Mix one sample of active drum voices into L/R (±2048 scale).
void drumsRenderMix(int32_t &mixL, int32_t &mixR);
