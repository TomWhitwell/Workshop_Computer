#pragma once

#include <stdint.h>

// Cross-core sequencer state. Read/written by:
//   Core 0 — DFAMSequencer::ProcessSample at 48 kHz
//   Core 1 — Grid loop or USB MIDI device loop
//
// All fields are single-byte or naturally aligned 32-bit, so reads and
// writes are atomic on the Cortex-M0+. The `volatile` qualifier on the
// global instance forces the compiler to re-read on every access so that
// changes from one core become visible on the other without explicit
// synchronisation.
//
// Direction of writes:
//   pitch, velocity, seqLength, playing — written by either core
//   editStep                            — written by either core
//   currentStep                         — written by core 0 only
//   tickEpoch                           — incremented by core 0 on every
//                                         step advance; core 1 polls it
//                                         to know when to redraw / send
//                                         a tick notification
struct SharedState {
    uint8_t  pitch[8];      // 0..127  MIDI note number per step
    uint8_t  velocity[8];   // 0..255  CV scaling per step
    uint8_t  seqLength;     // 2..8    active steps in the loop
    uint8_t  editStep;      // 0..7    selected step for editing
    uint8_t  currentStep;   // 0..7    playback position
    uint8_t  playing;       // 0/1     playback enable
    uint8_t  midiHostVelocityMode; // 0=8mu faders edit pitch, 1=velocity
    uint32_t tickEpoch;     // ++ on every step advance (cross-core signal)
};

// Right-half Grid pitch picker. We have 5 rows × 8 cols = 40 cells to
// represent the full 0..127 MIDI range. Each cell is a "bin" 3 or 4
// pitches wide (128 / 40 = 3.2), so every pitch is reachable from the
// grid. The panel X knob still spans the full 0..127 for fine edits
// between bin centres.
//
//   cell of pitch P:        (P * 40) / 128       (integer divide)
//   pitch at centre of N:   (N * 128 + 64) / 40
#define PITCH_GRID_CELLS  40

extern volatile SharedState gState;
