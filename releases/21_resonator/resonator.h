#pragma once

#include "ComputerCard.h"
#include "chords.h"
#include "io_modes.h"
#include "pitch_utils.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/flash.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include <cstring>
#include <cstdio>

// Flash storage for progression persistence
// Use last sector of flash (4KB before end of 2MB)
#define FLASH_PROG_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
#define FLASH_PROG_MAGIC 0xAB

// Debounce window for flash saves: a burst of setting changes (e.g. the web UI
// Save button sends SETARP/SETPAT/SETOUT/SETDIV at once) collapses into one write.
#define FLASH_DEBOUNCE_MS 300

/**
Resonator Workshop System Computer Card - by Johan Eklund
version 1.2 - 2026-07-08

Four resonating strings using Karplus-Strong synthesis
*/

class ResonatingStrings : public ComputerCard
{
private:
    static const int MAX_DELAY_SIZE = 2048;  // power of 2 for & mask (no division)

    int16_t delayLine1[MAX_DELAY_SIZE];
    int16_t delayLine2[MAX_DELAY_SIZE];
    int16_t delayLine3[MAX_DELAY_SIZE];
    int16_t delayLine4[MAX_DELAY_SIZE];

    int writeIndex1;
    int writeIndex2;
    int writeIndex3;
    int writeIndex4;

    int delayLength1;
    int delayLength2;
    int delayLength3;
    int delayLength4;

    int32_t filterState1;
    int32_t filterState2;
    int32_t filterState3;
    int32_t filterState4;

    ChordMode currentMode;

    // Double-buffered progression for lock-free Core0/Core1 sharing
    volatile int activeBuffer;
    ProgressionBuffer progressionBuffers[2];
    volatile bool progressionChanged;
    volatile bool pendingFlashSave;  // Flag for Core 1 to save flash (Core 0 can't lock out Core 1)
    volatile bool flashDirty;        // settings changed, debounced flash save pending (Core 1)
    uint32_t flashDirtyTime;         // ms timestamp of last change (Core 1 only)
    int progressionIndex;

    bool lastSwitchDown;

    uint8_t ledRR;  // round-robin LED index: update one LED per sample to bound per-sample cost

    int32_t pulseExciteEnvelope;
    uint32_t noiseState;

    int32_t dcState1, dcState2, dcState3, dcState4;

    // Smoothed delay values for glide between chord modes (fixed-point, 8 bits fraction)
    int32_t smoothDelay1, smoothDelay2, smoothDelay3, smoothDelay4;

    // Long-press reset detection
    uint32_t switchDownCounter;
    bool resetTriggered;
    static const uint32_t RESET_HOLD_SAMPLES = 144000;  // 3 seconds at 48kHz

    // CV and pulse output state
    int arpRotation;           // 0-3, current string for arpeggio output
    int32_t envFollower;       // peak envelope tracker
    bool triggerArmed;         // Schmitt trigger state
    int32_t trigPulseCounter;  // audio trigger pulse countdown
    int prevProgressionIndex;  // for chord-change detection
    int32_t chordPulseCounter; // chord change pulse countdown
    uint32_t chordPeriod;      // samples between chord changes
    uint32_t chordTimer;       // sample counter since last chord change
    uint32_t arpStepCounter;   // counts down to next arpeggio step
    volatile int arpDivision;  // arpeggio subdivision (1, 2, 4, or 8)
    volatile int arpPattern;   // arpeggio pattern (0=up, 1=down, 2=up-down, 3=random, 4=pedal, 5=random walk)
    volatile bool arpSettingsChanged; // flag for Core 0 to reset arp state
    int arpRandomString;       // cached random string index, updated on arp step
    volatile bool arpLoop;     // false = one sweep per chord (hold), true = loop continuously
    volatile int rootString;   // which chord tone (0-3) the root-pitch CV output uses

    // Configurable I/O modes
    volatile int cv1Mode;      // CVOutMode enum
    volatile int cv2Mode;      // CVOutMode enum
    volatile int p1Mode;       // P1Mode enum
    volatile int p2Mode;       // P2Mode enum
    volatile int pi1Mode;      // PI1Mode enum
    volatile int pi2Mode;      // PI2Mode enum
    volatile int ao2Mode;      // AO2Mode enum
    volatile int ci1Mode;      // CI1Mode enum
    volatile int ci2Mode;      // CI2Mode enum
    volatile bool outputModesChanged;

    // Tap tempo clock state
    uint32_t clockCounter;     // counts up, wraps at chordPeriod
    int32_t tapClockPulseCounter; // 5ms pulse countdown

    // Random S&H state
    int32_t randomSHValue;     // millivolts, updated on chord change

    // Arp clock state
    int32_t arpClockPulseCounter; // 5ms pulse countdown

    // Pitch S&H state
    int32_t pitchSHValue;      // millivolts, updated on PulseIn1 rising edge

    // Clock divider state
    uint32_t clockDivCount;    // counts PulseIn2 rising edges
    volatile int clockDivRatio; // 2, 3, 4, or 8
    int32_t clockDivPulseCounter; // output pulse countdown

    // Input envelope follower (for CV2 input envelope mode)
    int32_t inputEnvFollower;

    // Onset detector state
    int32_t onsetPeakEnv;        // peak-hold envelope for onset detection (fixed-point <<16)
    int32_t onsetEnvelope;       // slow-tracking baseline envelope (fixed-point <<16)
    int32_t onsetPulseCounter;   // pulse + lockout countdown

    // Precomputed flags: which blocks ProcessSample actually needs to run
    bool needsAudioAbs;
    bool needsAudioTrig;
    bool needsOnset;
    bool needsResEnv;
    bool needsInEnv;
    bool needsPitchTrack;
    bool needsRootMV;
    bool needsArpMV;
    bool needsTapClock;
    bool needsArpClock;
    bool needsClkDiv;
    bool needsChordDetect;

    // Hot-path methods (defined in main.cpp, same TU as ProcessSample)
    void updateNeedsFlags();
    int32_t dampingFilter(int32_t input, int32_t& state, int32_t coefficient);
    int arpStringIndex();
    void stepArpRandom();  // update cached random string index (random / random-walk patterns)
    int32_t processString(int16_t* delayLine, int& writeIndex, int delayLength,
                         int32_t& filterState, int32_t& dcState, int32_t excitation,
                         int32_t dampingCoeff, int32_t frac);

    // Cold-path serial/flash methods (defined in serial_flash.cpp)
    void handleSet(const char* args);
    void handleGet();
    void handleSetArp(const char* args);
    void handleGetArp();
    void handleSetPat(const char* args);
    void handleGetPat();
    void handleSetLoop(const char* args);
    void handleGetLoop();
    void handleSetRoot(const char* args);
    void handleGetRoot();
    void handleSetOut(const char* args);
    void handleGetOut();
    void handleSetDiv(const char* args);
    void handleGetDiv();
    void markFlashDirty();
    void saveProgressionToFlash();
    bool loadProgressionFromFlash();
    void resetToDefaults();

public:
    ResonatingStrings();
    void checkPendingFlashSave();
    void handleSerialCommand(const char* cmd);

protected:
    void ProcessSample() override;
};
