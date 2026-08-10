// Fragments
//
// A six-slot audio fragment recorder and sequencer for the Workshop
// Computer. Pulse In 1 clocks the pattern, Pulse In 2 records into the
// selected slot, and the Z switch moves between performance modes.
//
// Controls:
// - Switch up: choose the recording slot with Main. X chooses that
//   slot's playback mode; Y sets its chance of playing backward.
// - Switch middle: Main chooses the pattern. X/CV1 shifts the pattern,
//   and Y/CV2 chooses the repeat division.
// - Switch down: tap to reset to the first step. Hold for two seconds,
//   release, then tap once to clear or twice to save the kit.
//
// Playback modes per slot:
// - Looping: repeat the fragment for the whole step.
// - One Shot: play once, then go silent until the next step.
// - Interrupt: play once, then return to live audio.
// - Passthrough: ignore the recording and pass live audio.
//
// The fast audio work stays in ProcessSample() on core 0. Core 1 handles
// slower jobs such as LED updates, step decisions, recording cleanup,
// and other work that should not interrupt the audio loop.

#include "ComputerCard.h"
#include "hardware/flash.h"
#include "hardware/clocks.h"
#include "hardware/sync.h"
#include "pico/multicore.h"
#include "pico/time.h"
#include "tusb.h"

// Buffer size: 0.25 seconds at 48kHz
constexpr int kBufferLength = 12000;
constexpr int kNumSlots = 6;
constexpr int kVariationBufferLength = kBufferLength * kNumSlots;
constexpr int kNumPatterns = 21;
constexpr int kMaxPatternLen = 16;
constexpr int kPulseTriggerSamples = 480; // 10ms at 48kHz
constexpr int32_t kMinRecordingSamples = 480; // 10ms at 48kHz
constexpr int32_t kClockActivityHoldSamples = 192000; // 4s at 48kHz
constexpr int32_t kSaveFeedbackSamples = 24000; // 0.5s at 48kHz
constexpr int32_t kClearHoldSamples = 96000; // 2s at 48kHz
constexpr int32_t kCommandTimeoutSamples = 48000; // 1s at 48kHz
constexpr int32_t kDoubleTapSamples = 7200; // 150ms at 48kHz
constexpr int32_t kBootModeLatchSamples = 2400; // 50ms at 48kHz
constexpr int32_t kBootModeWindowSamples = 24000; // 0.5s at 48kHz
constexpr int32_t kVariationModeFeedbackSamples = 36000; // 0.75s at 48kHz
constexpr int32_t kVariationFadeSamples = 96; // 2ms at 48kHz
constexpr int kClearSamplesPerCore1Tick = 256;
constexpr int32_t kKnobEditMoveThreshold = 32;
constexpr int32_t kCVBipolarDetectThreshold = -64;
constexpr int kPlaybackFracBits = 12;
constexpr int32_t kPlaybackStepNormal = 1 << kPlaybackFracBits;
constexpr int32_t kSamplePlaybackGainNum = 1; // Unity playback gain
constexpr int32_t kSamplePlaybackGainDen = 1;
constexpr int32_t kPitchBendRangeSemitones = 12;
constexpr int32_t kPitchBendScale =
    (kPitchBendRangeSemitones * (1 << kPlaybackFracBits)) / 8192;
constexpr int32_t kVariationPitchRangeSemitones = 24;
constexpr int32_t kVariationPitchDeadband = 160;
constexpr int32_t kVariationPitchSnapWindowQ = 2 * (1 << (kPlaybackFracBits - 2)); // 0.5 semitones
constexpr uint8_t kSysExManufacturer = 0x7D;
constexpr uint8_t kSysExImportBegin = 0x20;
constexpr uint8_t kSysExImportData = 0x21;
constexpr uint8_t kSysExImportEnd = 0x22;
constexpr uint8_t kSysExSaveKit = 0x30;
constexpr uint8_t kSysExLoadKit = 0x31;
constexpr uint8_t kSysExPing = 0x40;
constexpr uint8_t kSysExPatternData = 0x50;
constexpr uint8_t kSysExFactoryPatterns = 0x51;
constexpr uint8_t kSysExCVConfig = 0x52;
constexpr uint8_t kSysExAck = 0x7E;
constexpr uint8_t kAckPing = 0x00;
constexpr uint8_t kAckImportBegin = 0x01;
constexpr uint8_t kAckImportReject = 0x02;
constexpr uint8_t kAckImportEnd = 0x03;
constexpr uint8_t kAckImportShort = 0x04;
constexpr uint8_t kAckPatternLoaded = 0x05;
constexpr uint8_t kAckPatternReject = 0x06;
constexpr uint8_t kAckCVConfigLoaded = 0x07;
constexpr uint8_t kAckCVConfigReject = 0x08;
constexpr uint32_t kFlashMagic = 0x31475246; // "FRG1"
constexpr uint32_t kPatternFlashMagic = 0x31544150; // "PAT1"
constexpr uint32_t kCVConfigFlashMagic = 0x31535643; // "CVS1"
constexpr uint32_t kFlashVersion = 1;
constexpr uint32_t kFlashStorageSize = 160 * 1024;
constexpr uint32_t kFlashStorageOffset = PICO_FLASH_SIZE_BYTES - kFlashStorageSize;
constexpr uint32_t kFlashProgramPageSize = FLASH_PAGE_SIZE;
constexpr uint32_t kSysExBufferSize = 96;
constexpr uint32_t kFlashKitBytes =
    16
    + (kNumSlots * 7 * sizeof(uint32_t))
    + (kNumSlots * 2 * kBufferLength)
    + sizeof(uint32_t)
    + (kNumPatterns * (1 + kMaxPatternLen))
    + sizeof(uint32_t)
    + (2 * 4)
    + 1;
static_assert(kFlashKitBytes <= kFlashStorageSize, "Saved kit exceeds flash storage");

constexpr uint32_t kSemitoneSteps[12] = {
    4096, 4339, 4598, 4871, 5161, 5468,
    5793, 6137, 6502, 6889, 7298, 7732
};

// Per-slot playback modes. X chooses one of these while the switch is up.
constexpr int kModeLooping = 0;
constexpr int kModeOneShot = 1;
constexpr int kModeInterrupt = 2;
constexpr int kModePassthrough = 3;

constexpr int kVariationNormal = 0;
constexpr int kVariationOctaveUp = 1;
constexpr int kVariationOctaveFifth = 2;
constexpr int kVariationReverse = 3;
constexpr int kVariationReverseOctaveUp = 4;
constexpr int kVariationOctaveDown = 5;

constexpr uint8_t kCVRangeBipolar6V = 0;
constexpr uint8_t kCVRangeBipolar3V = 1;
constexpr uint8_t kCVRangeBipolar15V = 2;
constexpr uint8_t kCVRangeUnipolar6V = 3;
constexpr uint8_t kCVRangeUnipolar3V = 4;
constexpr uint8_t kCVRangeCount = 5;
constexpr int32_t kCVRawFullScale = 2048;
constexpr int32_t kCVMillivoltFullScale = 6000;
constexpr int kCVSlewFracBits = 12;

constexpr uint8_t kCVQuantOff = 0;
constexpr uint8_t kCVQuantChromatic = 1;
constexpr uint8_t kCVQuantMajor = 2;
constexpr uint8_t kCVQuantMinor = 3;
constexpr uint8_t kCVQuantMajorPentatonic = 4;
constexpr uint8_t kCVQuantMinorPentatonic = 5;
constexpr uint8_t kCVQuantDorian = 6;
constexpr uint8_t kCVQuantPelog = 7;
constexpr uint8_t kCVQuantWholeTone = 8;
constexpr uint8_t kCVQuantCount = 9;

constexpr uint8_t kCVClockDivCount = 5; // step, /2, /4, /8, /16
constexpr uint8_t kCVSlewCount = 6;     // off, then progressively slower

enum ZCommand
{
    kZCommandNone,
    kZCommandReset,
    kZCommandSave,
    kZCommandClear
};

// A pattern is a sequence of slot numbers. Length says how many entries
// in steps[] are active; the rest are just padding.
struct Pattern
{
    uint8_t steps[kMaxPatternLen];
    uint8_t length;
};

static const Pattern factoryPatterns[kNumPatterns] = {
    /*  0 */ {{0}, 1},
    /*  1 */ {{0, 1}, 2},
    /*  2 */ {{0, 1, 2}, 3},
    /*  3 */ {{0, 1, 2, 3}, 4},
    /*  4 */ {{0, 1, 2, 4}, 4},
    /*  5 */ {{0, 1, 2, 5}, 4},
    /*  6 */ {{0, 1, 2, 3, 4, 5}, 6},
    /*  7 */ {{5, 4, 3, 2, 1, 0}, 6},
    /*  8 */ {{0, 0, 2, 0}, 4},
    /*  9 */ {{0, 1, 2, 0}, 4},
    /* 10 */ {{0, 1, 2, 2}, 4},
    /* 11 */ {{0, 1, 2, 2, 0, 2, 3, 1}, 8},
    /* 12 */ {{0, 0, 1, 1}, 4},
    /* 13 */ {{0, 0, 2, 2}, 4},
    /* 14 */ {{0, 2, 4, 2}, 4},
    /* 15 */ {{1, 3, 5, 3}, 4},
    /* 16 */ {{0, 2, 4, 1, 3, 5}, 6},
    /* 17 */ {{0, 1, 2, 1, 2, 3, 2, 3, 4, 3, 4, 5}, 12},
    /* 18 */ {{5, 4, 3, 4, 3, 2, 3, 2, 1, 2, 1, 0}, 12},
    /* 19 */ {{0, 1, 2, 3, 4, 5, 1}, 7},
    /* 20 */ {{0, 1, 2, 3, 4, 5, 5, 4, 3, 2, 1, 0}, 12},
};

class Fragments : public ComputerCard
{
public:
    Fragments()
    {
        loadFactoryPatterns();
        loadKitFromFlash();

        // The probe lets us tell which inputs are patched, and also
        // keeps the probe signal aligned with ComputerCard's ADC reads.
        EnableNormalisationProbe();

        CVOut1Millivolts(0);
        CVOut2Millivolts(0);
    }

    // Core 1 handles slow control work so ProcessSample() can stay lean.
    void Core1()
    {
        sleep_us(150000);

        USBPowerState_t powerState = USBPowerState();
        isUSBMIDIHost_ = (powerState == DFP);
        if (isUSBMIDIHost_)
        {
            tuh_init(TUH_OPT_RHPORT);
        }
        else
        {
            tud_init(TUD_OPT_RHPORT);
        }

        while (true)
        {
            if (isUSBMIDIHost_)
            {
                tuh_task();
            }
            else
            {
                tud_task();
                serviceUSBMIDI();
            }
            serviceClearRequest();
            serviceRecordingFinalize();
            serviceFlashSave();

            // Step/reset requests are set by core 0 when it sees pulses.
            if (resetEdge_)
            {
                resetEdge_ = false;
                stepIndex_ = 0;
                applyStepChange();
                triggerStepPulses(true);
            }
            else if (clockEdge_)
            {
                clockEdge_ = false;

                // Measure the clock period in samples for repeat divisions.
                uint32_t now = sampleCounter_;
                if (haveClockPeriod_)
                {
                    clockPeriodSamples_ = now - lastClockSample_;
                    updateRepeatLength();
                }
                else
                {
                    haveClockPeriod_ = true;
                }
                lastClockSample_ = now;

                stepIndex_++;
                if (stepIndex_ >= patterns_[selectedPattern_].length)
                {
                    stepIndex_ = 0;
                }
                applyStepChange();
                triggerStepPulses(stepIndex_ == 0);
            }

            // Pattern changes keep the current step position when possible.
            {
                int currentPattern = selectedPattern_;
                if (currentPattern != lastSeenPattern_)
                {
                    lastSeenPattern_ = currentPattern;

                    uint8_t newLen = patterns_[currentPattern].length;
                    if (newLen > 0)
                    {
                        stepIndex_ = stepIndex_ % newLen;
                    }
                    else
                    {
                        stepIndex_ = 0;
                    }

                    applyStepChange();
                }
            }

            // Shift changes remap the current step immediately.
            {
                int currentShift = shiftAmount_;
                if (currentShift != lastSeenShift_)
                {
                    lastSeenShift_ = currentShift;
                    applyStepChange();
                }
            }

            // Division changes affect repeat length, not the selected slot.
            {
                int currentSubShift = subdivisionShift_;
                if (currentSubShift != lastSeenSubShift_)
                {
                    lastSeenSubShift_ = currentSubShift;
                    updateRepeatLength();
                }
            }

            updateLeds();

            sleep_us(100);
        }
    }

    virtual void ProcessSample()
    {
        sampleCounter_++;
        updatePulseOutputs();
        updateRandomCVOutputs();
        if (clockActivitySamples_ > 0)
        {
            clockActivitySamples_--;
        }
        if (saveFeedbackSamples_ > 0)
        {
            saveFeedbackSamples_--;
        }
        if (variationModeFeedbackSamples_ > 0)
        {
            variationModeFeedbackSamples_--;
        }

        // Read each hardware input once per sample. ComputerCard's ADC
        // mux expects a steady read pattern, so we cache everything here
        // and use these local values below.
        int32_t inA     = AudioIn1();
        int32_t inB     = AudioIn2();
        int32_t cv1in   = CVIn1();
        int32_t cv2in   = CVIn2();
        int32_t knobMain = KnobVal(Knob::Main);
        int32_t knobX   = KnobVal(Knob::X);
        int32_t knobY   = KnobVal(Knob::Y);
        knobMain = resolveKnobControl(0, knobMain);
        knobX = resolveKnobControl(1, knobX);
        knobY = resolveKnobControl(2, knobY);
        bool audio1Connected = Connected(Input::Audio1);
        bool audio2Connected = Connected(Input::Audio2);

        Switch sw = SwitchVal();
        bool switchDown = (sw == Switch::Down);
        updateBootModeLatch(switchDown);
        bool recordHeld = PulseIn2();
        lastSwitchUp_ = (sw == Switch::Up);
        ZCommand zCommand = updateZCommand(switchDown);

        if (zCommand == kZCommandReset)
        {
            resetEdge_ = true;
        }
        else if (zCommand == kZCommandSave && !recordHeld)
        {
            __mem_fence_release();
            flashSaveRequested_ = true;
        }
        else if (zCommand == kZCommandClear)
        {
            beginClearAll();
        }

        // Switch up: choose a slot and optionally edit its settings.
        if (sw == Switch::Up)
        {
            if (variationMode_)
            {
                updateVariationPitchEditing(knobMain);
                updateVariationSwitchUpEditing(knobX, knobY);
            }
            else
            {
                updateSwitchUpSlotSelection(knobMain);
                // X/Y use pickup behavior, so changing switch position does
                // not overwrite a slot until the knob actually moves.
                updateSwitchUpKnobEditing(selectedSlot_, knobX, knobY);
            }
        }
        else
        {
            switchUpMainPrimed_ = false;
            switchUpKnobsPrimed_ = false;
        }

        if (sw != Switch::Middle)
        {
            middleMainPrimed_ = false;
            middleXYPrimed_ = false;
        }

        // When recording ends, core 1 publishes the completed slot.
        if (!recordHeld && wasRecording_)
        {
            int slot = recordingSlot_;
            if (recordingHadInput_)
            {
                int32_t finalLength = bufferFull_
                    ? (variationMode_ ? kVariationBufferLength : kBufferLength)
                    : writeIndex_;
                if (finalLength >= kMinRecordingSamples)
                {
                    pendingRecordSlot_ = slot;
                    pendingRecordLength_ = finalLength;
                    pendingRecordChannelA_ = recordingWriteA_;
                    pendingRecordChannelB_ = recordingWriteB_;
                    pendingRecordVariation_ = variationMode_;
                    __mem_fence_release();
                    pendingRecordFinalize_ = true;
                }
            }
            __mem_fence_release();
            recordingActive_ = false;
        }

        // Pulse In 1 is the clock. Core 1 turns this flag into a step.
        if (PulseIn1RisingEdge())
        {
            clockEdge_ = true;
            clockActivitySamples_ = kClockActivityHoldSamples;
        }

        // After recent clocks, switch-up selects slots without stopping playback.
        bool clockFollowMode = (clockActivitySamples_ > 0);

        if (recordHeld)
        {
            // Pulse In 2 records a slot in normal mode, or the long
            // one-sample tape when booted into Variation mode.
            int slot = variationMode_ ? 0 : selectedSlot_;

            // A defensive guard: this should never happen during normal use.
            if (slot < 0 || slot >= kNumSlots)
            {
                for (int i = 0; i < 6; i++) LedOn(i);
                setAudioOut(0, 0);
                wasRecording_ = recordHeld;
                lastSwitchDown_ = switchDown;
                return;
            }

            if (!wasRecording_)
            {
                recordingHadInput_ = audio1Connected || audio2Connected;

                if (recordingHadInput_)
                {
                    recordingSlot_ = slot;
                    writeIndex_ = 0;
                    bufferFull_ = false;
                    recordingWriteA_ = audio1Connected;
                    recordingWriteB_ = audio2Connected;
                }
            }

            if (recordingHadInput_)
            {
                int32_t maxRecordLength = variationMode_
                    ? kVariationBufferLength
                    : kBufferLength;

                if (writeIndex_ >= 0 && writeIndex_ < maxRecordLength)
                {
                    if (recordingWriteA_)
                    {
                        if (variationMode_)
                        {
                            writeVariationSample(bufferA_, writeIndex_, audioToStoredSample(inA));
                        }
                        else
                        {
                            bufferA_[slot][writeIndex_] = audioToStoredSample(inA);
                        }
                    }
                    if (recordingWriteB_)
                    {
                        if (variationMode_)
                        {
                            writeVariationSample(bufferB_, writeIndex_, audioToStoredSample(inB));
                        }
                        else
                        {
                            bufferB_[slot][writeIndex_] = audioToStoredSample(inB);
                        }
                    }
                }

                if (writeIndex_ < maxRecordLength - 1)
                {
                    writeIndex_++;
                }
                else
                {
                    bufferFull_ = true;
                }
            }

            setAudioOut(inA, inB);

            // While recording, show the slot/tape being written.
            for (int i = 0; i < kNumSlots; i++)
            {
                if (i == slot) LedOn(i); else LedOff(i);
            }
            recordingActive_ = recordingHadInput_;

        }
        if (recordHeld)
        {
            // Recording monitors live input and skips playback.
        }
        else if (sw == Switch::Up && !clockFollowMode)
        {
            // Without an active clock, switch-up is live passthrough.
            setAudioOut(inA, inB);
        }
        else
        {
            // Pattern playback. In clock-follow switch-up, keep playing
            // while the up-position controls select/edit the record slot.
            if (sw == Switch::Middle)
            {
                updateMiddlePatternEditing(knobMain);
                updateMiddleShiftDivideEditing(knobX, knobY, cv1in, cv2in);
            }
            else
            {
                middleMainPrimed_ = false;
                middleXYPrimed_ = false;
            }

            int slot = activeSlot_;

            // A new step restarts playback and latches direction.
            if (restartPlayback_)
            {
                restartPlayback_ = false;
                if (variationMode_)
                {
                    variationFadeSamples_ = kVariationFadeSamples;
                    variationFadeStartA_ = lastOutputA_;
                    variationFadeStartB_ = lastOutputB_;
                }
                int32_t currentLength = playbackLength(slot);
                playDirReverse_ = playReverse_;
                playIndex_ = playDirReverse_
                    ? (currentLength > 0 ? currentLength - 1 : 0)
                    : 0;
                playPositionQ_ = playIndex_ << kPlaybackFracBits;
                repeatPosition_ = 0;
                inSilenceFill_ = false;
                stepFinished_ = false;
                repeatsCompleted_ = 0;
            }

            int32_t currentLength = playbackLength(slot);
            int mode = playbackModeForSlot(slot);

            // Keep the playback head inside the active slot.
            if (currentLength == 0)
            {
                playIndex_ = 0;
                playPositionQ_ = 0;
            }
            else if (playIndex_ >= currentLength)
            {
                playIndex_ = playDirReverse_ ? (currentLength - 1) : 0;
                playPositionQ_ = playIndex_ << kPlaybackFracBits;
            }
            else if (playIndex_ < 0)
            {
                playIndex_ = playDirReverse_ ? (currentLength - 1) : 0;
                playPositionQ_ = playIndex_ << kPlaybackFracBits;
            }

            if (recordingActive_ && slot == recordingSlot_)
            {
                // Avoid reading a slot while recording into it.
                outputSlotSilenceOrLive(slot, inA, inB);
            }
            else if (mode == kModePassthrough)
            {
                // Passthrough mode ignores this slot's recording.
                setAudioOut(inA, inB);
            }
            else if (stepFinished_)
            {
                // One Shot goes quiet after it finishes. Interrupt
                // returns to live audio after it finishes.
                if (mode == kModeInterrupt)
                {
                    setAudioOut(inA, inB);
                }
                else // kModeOneShot
                {
                    outputSlotSilenceOrLive(slot, inA, inB);
                }
            }
            else if (hasPlaybackRecording(slot))
            {
                // Division repeats only apply above x1. At x1, the
                // fragment simply loops until the next sequencer step.
                bool subdivisionActive = (subdivisionShift_ > 0)
                    && (repeatLengthSamples_ > 0);

                int32_t effectiveRepeatLength = subdivisionActive
                    ? repeatLengthSamples_
                    : currentLength;

                if (effectiveRepeatLength < 1)
                {
                    effectiveRepeatLength = 1;
                }

                // One pass of the fragment may end naturally, or at a
                // division boundary if the repeat is shorter than the recording.
                bool fragmentCompleted = false;

                if (inSilenceFill_)
                {
                    // The recording ended before this repeat slot did.
                    outputSlotSilenceOrLive(slot, inA, inB);
                }
                else
                {
                    outputSlotSampleOrLive(
                        slot,
                        inA,
                        inB,
                        playbackSampleA(slot),
                        playbackSampleB(slot));

                    int32_t playbackStepThisSample = playbackStepForCurrentVariation();

                    if (playDirReverse_)
                    {
                        playPositionQ_ -= playbackStepThisSample;
                        if (playPositionQ_ < 0)
                        {
                            if (subdivisionActive
                                && currentLength < effectiveRepeatLength)
                            {
                                inSilenceFill_ = true;
                            }
                            else
                            {
                                playIndex_ = currentLength - 1;
                                playPositionQ_ = playIndex_ << kPlaybackFracBits;
                                fragmentCompleted = true;
                            }
                        }
                        else
                        {
                            playIndex_ = playPositionQ_ >> kPlaybackFracBits;
                        }
                    }
                    else
                    {
                        playPositionQ_ += playbackStepThisSample;
                        if (playPositionQ_ >= (currentLength << kPlaybackFracBits))
                        {
                            if (subdivisionActive
                                && currentLength < effectiveRepeatLength)
                            {
                                inSilenceFill_ = true;
                            }
                            else
                            {
                                playIndex_ = 0;
                                playPositionQ_ = 0;
                                fragmentCompleted = true;
                            }
                        }
                        else
                        {
                            playIndex_ = playPositionQ_ >> kPlaybackFracBits;
                        }
                    }
                }

                // Division boundary: restart or reverse-restart the fragment.
                if (subdivisionActive)
                {
                    repeatPosition_++;
                    if (repeatPosition_ >= effectiveRepeatLength)
                    {
                        repeatPosition_ = 0;
                        inSilenceFill_ = false;
                        playIndex_ = playDirReverse_
                            ? (currentLength > 0 ? currentLength - 1 : 0)
                            : 0;
                        playPositionQ_ = playIndex_ << kPlaybackFracBits;
                        fragmentCompleted = true;
                    }
                }

                // One Shot and Interrupt finish after their planned repeats.
                if (fragmentCompleted
                    && mode != kModeLooping)
                {
                    repeatsCompleted_++;

                    int32_t targetRepeats = subdivisionActive
                        ? (1 << subdivisionShift_)
                        : 1;

                    if (repeatsCompleted_ >= targetRepeats)
                    {
                        stepFinished_ = true;
                    }
                }
            }
            else
            {
                // Empty slots pass live audio.
                setAudioOut(inA, inB);
            }
        }

        wasRecording_ = recordHeld;
        lastSwitchDown_ = switchDown;
    }

private:
    // Move to the current pattern step, including slot shift.
    void applyStepChange()
    {
        uint8_t rawValue = patterns_[selectedPattern_].steps[stepIndex_];
        int shiftedValue = (rawValue + shiftAmount_) % kNumSlots;

        if (variationMode_)
        {
            activeVariation_ = shiftedValue;
            activeSlot_ = 0;
            bool flipReverse =
                (int32_t)(nextRandom() & 0x0FFF) < variationReverseProb_;
            playReverse_ = variationIsReverse(shiftedValue) != flipReverse;
        }
        else
        {
            activeSlot_ = shiftedValue;
            rollReverse();
        }

        rollRandomCVOutputs();
        restartPlayback_ = true;
    }

    void updateBootModeLatch(bool switchDown)
    {
        if (bootModeWindowSamples_ <= 0)
        {
            return;
        }

        bootModeWindowSamples_--;

        if (switchDown)
        {
            if (bootModeDownSamples_ < kBootModeLatchSamples)
            {
                bootModeDownSamples_++;
            }

            if (bootModeDownSamples_ >= kBootModeLatchSamples)
            {
                variationMode_ = true;
                variationModeFeedbackSamples_ = kVariationModeFeedbackSamples;
                zIgnoreRelease_ = true;
                bootModeWindowSamples_ = 0;
            }
        }
        else
        {
            bootModeDownSamples_ = 0;
        }
    }

    static bool variationIsReverse(int variation)
    {
        return variation == kVariationReverse
            || variation == kVariationReverseOctaveUp;
    }

    static int32_t snapVariationPitchSemitoneOffset(int32_t semitoneOffsetQ)
    {
        static const int8_t snapPoints[] = {
            -24, -19, -17, -12, -7, -5, 0, 5, 7, 12, 17, 19, 24
        };

        for (uint32_t i = 0; i < sizeof(snapPoints) / sizeof(snapPoints[0]); ++i)
        {
            int32_t snapQ = ((int32_t)snapPoints[i]) << kPlaybackFracBits;
            int32_t distance = semitoneOffsetQ - snapQ;
            if (distance < 0) distance = -distance;

            if (distance <= kVariationPitchSnapWindowQ)
            {
                return snapQ;
            }
        }

        return semitoneOffsetQ;
    }

    void updateVariationPitchControl(int32_t knobMain)
    {
        int32_t centered = knobMain - 2048;
        int32_t magnitude = centered < 0 ? -centered : centered;
        int32_t semitoneOffsetQ = 0;

        if (magnitude > kVariationPitchDeadband)
        {
            int32_t travel = magnitude - kVariationPitchDeadband;
            int32_t range = 2047 - kVariationPitchDeadband;
            semitoneOffsetQ =
                (travel * (kVariationPitchRangeSemitones << kPlaybackFracBits))
                / range;

            if (centered < 0)
            {
                semitoneOffsetQ = -semitoneOffsetQ;
            }
        }

        semitoneOffsetQ = snapVariationPitchSemitoneOffset(semitoneOffsetQ);
        variationPitchStepQ_ = semitoneOffsetToPlaybackStep(semitoneOffsetQ);

        int led = (int)((knobMain * kNumSlots) >> 12);
        if (led < 0) led = 0;
        if (led >= kNumSlots) led = kNumSlots - 1;
        selectedSlot_ = led;
    }

    void updateSwitchUpSlotSelection(int32_t knobMain)
    {
        if (!switchUpMainPrimed_)
        {
            lastSwitchUpMain_ = knobMain;
            switchUpMainPrimed_ = true;
            return;
        }

        int32_t dm = knobMain - lastSwitchUpMain_;
        if (dm < 0) dm = -dm;
        if (dm < kKnobEditMoveThreshold)
        {
            return;
        }

        int sel = (int)((knobMain * kNumSlots) >> 12);
        if (sel < 0) sel = 0;
        if (sel >= kNumSlots) sel = kNumSlots - 1;

        selectedSlot_ = sel;
        lastSwitchUpMain_ = knobMain;

        if (sel != lastEditSlot_)
        {
            switchUpKnobsPrimed_ = false;
        }
    }

    void updateVariationPitchEditing(int32_t knobMain)
    {
        if (!switchUpMainPrimed_)
        {
            lastSwitchUpMain_ = knobMain;
            switchUpMainPrimed_ = true;
            return;
        }

        int32_t dm = knobMain - lastSwitchUpMain_;
        if (dm < 0) dm = -dm;
        if (dm < kKnobEditMoveThreshold)
        {
            return;
        }

        updateVariationPitchControl(knobMain);
        lastSwitchUpMain_ = knobMain;
    }

    int32_t playbackStepForVariation(int variation)
    {
        int32_t step = (int32_t)playbackStepQ_;

        if (variationMode_)
        {
            switch (variation)
            {
            case kVariationOctaveUp:
            case kVariationReverseOctaveUp:
                step *= 2;
                break;
            case kVariationOctaveFifth:
                step *= 3;
                break;
            case kVariationOctaveDown:
                step /= 2;
                break;
            default:
                break;
            }

            step = (step * (int32_t)variationPitchStepQ_) >> kPlaybackFracBits;
        }

        return step > 0 ? step : 1;
    }

    int32_t playbackStepForCurrentVariation()
    {
        return playbackStepForVariation(activeVariation_);
    }

    // Patched CV starts unipolar. If it dips below 0V, treat it as bipolar.
    static int32_t attenuatedAutoCVControl(
        int32_t cv,
        int32_t amount,
        bool &seenNegative,
        bool connected)
    {
        if (!connected)
        {
            seenNegative = false;
            return amount;
        }

        if (cv < kCVBipolarDetectThreshold)
        {
            seenNegative = true;
        }

        return seenNegative
            ? attenuatedBipolarCVControl(cv, amount)
            : attenuatedUnipolarCVControl(cv, amount);
    }

    static int32_t attenuatedBipolarCVControl(int32_t cv, int32_t amount)
    {
        if (cv < -2048) cv = -2048;
        if (cv > 2047) cv = 2047;
        if (amount < 0) amount = 0;
        if (amount > 4095) amount = 4095;

        int32_t control = 2048 + ((cv * amount) >> 12);
        if (control < 0) control = 0;
        if (control > 4095) control = 4095;
        return control;
    }

    static int32_t attenuatedUnipolarCVControl(int32_t cv, int32_t amount)
    {
        if (cv < 0) cv = 0;
        if (cv > 2047) cv = 2047;
        if (amount < 0) amount = 0;
        if (amount > 4095) amount = 4095;

        int32_t control = (cv * amount) >> 11;
        if (control < 0) control = 0;
        if (control > 4095) control = 4095;
        return control;
    }

    void updateSwitchUpKnobEditing(int slot, int32_t knobX, int32_t knobY)
    {
        if (!switchUpKnobsPrimed_)
        {
            lastEditSlot_ = slot;
            lastKnobX_ = knobX;
            lastKnobY_ = knobY;
            switchUpKnobsPrimed_ = true;
            return;
        }

        int32_t dx = knobX - lastKnobX_;
        if (dx < 0) dx = -dx;
        if (dx >= kKnobEditMoveThreshold)
        {
            int mode = (int)(knobX >> 10); // 0-3
            if (mode < 0) mode = 0;
            if (mode > 3) mode = 3;
            playbackMode_[slot] = mode;
            lastKnobX_ = knobX;
        }

        int32_t dy = knobY - lastKnobY_;
        if (dy < 0) dy = -dy;
        if (dy >= kKnobEditMoveThreshold)
        {
            reverseProb_[slot] = knobY;
            lastKnobY_ = knobY;
        }
    }

    void updateVariationSwitchUpEditing(int32_t knobX, int32_t knobY)
    {
        if (!switchUpKnobsPrimed_)
        {
            lastEditSlot_ = -1;
            lastKnobX_ = knobX;
            lastKnobY_ = knobY;
            switchUpKnobsPrimed_ = true;
            return;
        }

        int32_t dx = knobX - lastKnobX_;
        if (dx < 0) dx = -dx;
        if (dx >= kKnobEditMoveThreshold)
        {
            int mode = (int)(knobX >> 10); // 0-3
            if (mode < 0) mode = 0;
            if (mode > 3) mode = 3;
            variationPlaybackMode_ = mode;
            lastKnobX_ = knobX;
        }

        int32_t dy = knobY - lastKnobY_;
        if (dy < 0) dy = -dy;
        if (dy >= kKnobEditMoveThreshold)
        {
            variationReverseProb_ = knobY;
            lastKnobY_ = knobY;
        }
    }

    void updateMiddlePatternEditing(int32_t knobMain)
    {
        if (!middleMainPrimed_)
        {
            lastMiddleMain_ = knobMain;
            middleMainPrimed_ = true;
            return;
        }

        int32_t dm = knobMain - lastMiddleMain_;
        if (dm < 0) dm = -dm;
        if (dm < kKnobEditMoveThreshold)
        {
            return;
        }

        int pat = (int)((knobMain * kNumPatterns) >> 12);
        if (pat < 0) pat = 0;
        if (pat >= kNumPatterns) pat = kNumPatterns - 1;
        selectedPattern_ = pat;
        lastMiddleMain_ = knobMain;
    }

    void updateMiddleShiftDivideEditing(
        int32_t knobX,
        int32_t knobY,
        int32_t cv1in,
        int32_t cv2in)
    {
        bool cv1Connected = Connected(Input::CV1);
        bool cv2Connected = Connected(Input::CV2);

        if (!middleXYPrimed_)
        {
            lastMiddleKnobX_ = knobX;
            lastMiddleKnobY_ = knobY;
            middleXYPrimed_ = true;
            return;
        }

        if (cv1Connected)
        {
            int32_t shiftControl = attenuatedAutoCVControl(
                cv1in, knobX, cv1SeenNegative_, true);
            setShiftFromControl(shiftControl);
            lastMiddleKnobX_ = knobX;
        }
        else
        {
            // No CV patched: X is direct shift control, but only after
            // it moves in middle mode. This prevents a mode jump when
            // returning from switch-up slot selection.
            cv1SeenNegative_ = false;
            int32_t dx = knobX - lastMiddleKnobX_;
            if (dx < 0) dx = -dx;
            if (dx >= kKnobEditMoveThreshold)
            {
                setShiftFromControl(knobX);
                lastMiddleKnobX_ = knobX;
            }
        }

        if (cv2Connected)
        {
            int32_t divideControl = attenuatedAutoCVControl(
                cv2in, knobY, cv2SeenNegative_, true);
            setSubdivisionFromControl(divideControl);
            lastMiddleKnobY_ = knobY;
        }
        else
        {
            // No CV patched: Y is direct divide control, but only
            // after movement. This avoids accidental repeat/divide
            // jumps when moving between switch positions.
            cv2SeenNegative_ = false;
            int32_t dy = knobY - lastMiddleKnobY_;
            if (dy < 0) dy = -dy;
            if (dy >= kKnobEditMoveThreshold)
            {
                setSubdivisionFromControl(knobY);
                lastMiddleKnobY_ = knobY;
            }
        }
    }

    void setShiftFromControl(int32_t control)
    {
        int shift = (int)((control * kNumSlots) >> 12);
        if (shift < 0) shift = 0;
        if (shift >= kNumSlots) shift = kNumSlots - 1;
        shiftAmount_ = shift;
    }

    void setSubdivisionFromControl(int32_t control)
    {
        int subShift = (int)(control >> 10);
        if (subShift < 0) subShift = 0;
        if (subShift > 3) subShift = 3;
        subdivisionShift_ = subShift;
    }

    // Core 1 requests trigger pulses; core 0 owns the actual hardware
    // writes so the output duration is sample-accurate.
    void triggerStepPulses(bool firstStep)
    {
        pulseOut1Samples_ = kPulseTriggerSamples;
        if (firstStep)
        {
            pulseOut2Samples_ = kPulseTriggerSamples;
        }
    }

    void updatePulseOutputs()
    {
        if (pulseOut1Samples_ > 0)
        {
            PulseOut1(true);
            pulseOut1Samples_--;
        }
        else
        {
            PulseOut1(false);
        }

        if (pulseOut2Samples_ > 0)
        {
            PulseOut2(true);
            pulseOut2Samples_--;
        }
        else
        {
            PulseOut2(false);
        }
    }

    // Random CV targets refresh on step divisions.
    void rollRandomCVOutputs()
    {
        for (int output = 0; output < 2; output++)
        {
            if (output == 1 && cv2CoupledToCV1_)
            {
                continue;
            }

            if (cvRandomStepCount_[output] == 0)
            {
                randomCVTarget_[output] = makeRandomCVValue(output);
                if (cvSlewShift(output) == 0)
                {
                    randomCVCurrent_[output] = randomCVTarget_[output];
                    randomCVCurrentQ_[output] = randomCVTarget_[output] << kCVSlewFracBits;
                }
                cvRandomStepCount_[output] = cvClockDivisor(output) - 1;
            }
            else
            {
                cvRandomStepCount_[output]--;
            }
        }
    }

    int32_t randomSigned12Bit()
    {
        return (int32_t)(nextRandom() & 0x0FFF) - 2048;
    }

    int32_t makeRandomCVValue(int output)
    {
        int32_t value = applyCVRange(randomSigned12Bit(), cvRange_[output]);
        value = quantizeCV(value, cvQuant_[output]);
        return clampSigned12Bit(value);
    }

    int32_t applyCVRange(int32_t value, uint8_t range)
    {
        switch (range)
        {
        case kCVRangeBipolar3V:
            return value >> 1;

        case kCVRangeBipolar15V:
            return value >> 2;

        case kCVRangeUnipolar6V:
            return (value + 2048) >> 1;

        case kCVRangeUnipolar3V:
            return (value + 2048) >> 2;

        case kCVRangeBipolar6V:
        default:
            return value;
        }
    }

    int32_t quantizeCV(int32_t value, uint8_t quant)
    {
        if (quant == kCVQuantOff)
        {
            return value;
        }

        int semitone = divRound(value * 72, 2048);
        semitone = nearestQuantizedSemitone(semitone, quant);

        return divRound(semitone * 2048, 72);
    }

    int nearestQuantizedSemitone(int semitone, uint8_t quant)
    {
        static const int majorScale[7] = {0, 2, 4, 5, 7, 9, 11};
        static const int minorScale[7] = {0, 2, 3, 5, 7, 8, 10};
        static const int majorPentatonicScale[5] = {0, 2, 4, 7, 9};
        static const int minorPentatonicScale[5] = {0, 3, 5, 7, 10};
        static const int dorianScale[7] = {0, 2, 3, 5, 7, 9, 10};
        static const int pelogScale[5] = {0, 1, 3, 7, 8};
        static const int wholeToneScale[6] = {0, 2, 4, 6, 8, 10};
        const int *scale = majorScale;
        int length = 7;

        if (quant == kCVQuantMinor)
        {
            scale = minorScale;
        }
        else if (quant == kCVQuantMajorPentatonic)
        {
            scale = majorPentatonicScale;
            length = 5;
        }
        else if (quant == kCVQuantMinorPentatonic)
        {
            scale = minorPentatonicScale;
            length = 5;
        }
        else if (quant == kCVQuantDorian)
        {
            scale = dorianScale;
        }
        else if (quant == kCVQuantPelog)
        {
            scale = pelogScale;
            length = 5;
        }
        else if (quant == kCVQuantWholeTone)
        {
            scale = wholeToneScale;
            length = 6;
        }

        int best = semitone;
        int bestDistance = 128;

        for (int octave = -7; octave <= 7; octave++)
        {
            for (int i = 0; i < length; i++)
            {
                int candidate = octave * 12 + scale[i];
                int distance = candidate - semitone;
                if (distance < 0) distance = -distance;
                if (distance < bestDistance)
                {
                    bestDistance = distance;
                    best = candidate;
                }
            }
        }

        return best;
    }

    int32_t divRound(int32_t numerator, int32_t denominator)
    {
        if (numerator >= 0)
        {
            return (numerator + denominator / 2) / denominator;
        }
        return -((-numerator + denominator / 2) / denominator);
    }

    int32_t clampSigned12Bit(int32_t value)
    {
        if (value < -2048) return -2048;
        if (value > 2047) return 2047;
        return value;
    }

    int32_t randomCVToMillivolts(int32_t value)
    {
        value = clampSigned12Bit(value);
        int32_t millivolts = divRound(value * kCVMillivoltFullScale, kCVRawFullScale);

        if (millivolts < -kCVMillivoltFullScale)
        {
            return -kCVMillivoltFullScale;
        }
        if (millivolts > kCVMillivoltFullScale)
        {
            return kCVMillivoltFullScale;
        }
        return millivolts;
    }

    void writeRandomCVOutput(int output, int32_t value)
    {
        value = clampSigned12Bit(value);
        if (lastRandomCVOutput_[output] == value)
        {
            return;
        }

        lastRandomCVOutput_[output] = value;
        int32_t millivolts = randomCVToMillivolts(value);

        if (output == 0)
        {
            CVOut1Millivolts(millivolts);
        }
        else
        {
            CVOut2Millivolts(millivolts);
        }
    }

    uint8_t cvClockDivisor(int output)
    {
        return 1u << cvClockDiv_[output];
    }

    uint8_t cvSlewShift(int output)
    {
        static const uint8_t shifts[kCVSlewCount] = {0, 9, 10, 11, 12, 14};
        return shifts[cvSlew_[output]];
    }

    void updateRandomCVOutputs()
    {
        for (int output = 0; output < 2; output++)
        {
            if (output == 1 && cv2CoupledToCV1_)
            {
                continue;
            }

            int32_t target = randomCVTarget_[output];
            uint8_t slewShift = cvSlewShift(output);

            if (slewShift == 0)
            {
                randomCVCurrent_[output] = target;
                randomCVCurrentQ_[output] = target << kCVSlewFracBits;
            }
            else
            {
                int32_t targetQ = target << kCVSlewFracBits;
                int32_t delta = targetQ - randomCVCurrentQ_[output];
                int32_t step = delta >> slewShift;

                if (step == 0 && delta != 0)
                {
                    step = delta > 0 ? 1 : -1;
                }

                randomCVCurrentQ_[output] += step;

                if ((delta > 0 && randomCVCurrentQ_[output] >= targetQ)
                    || (delta < 0 && randomCVCurrentQ_[output] <= targetQ))
                {
                    randomCVCurrentQ_[output] = targetQ;
                    randomCVCurrent_[output] = target;
                }
                else
                {
                    randomCVCurrent_[output] =
                        randomCVCurrentQ_[output] >> kCVSlewFracBits;
                }
            }
        }

        if (cv2CoupledToCV1_)
        {
            randomCVTarget_[1] = randomCVTarget_[0];
            randomCVCurrent_[1] = randomCVCurrent_[0];
            randomCVCurrentQ_[1] = randomCVCurrentQ_[0];
            cvRandomStepCount_[1] = cvRandomStepCount_[0];
        }

        writeRandomCVOutput(0, randomCVCurrent_[0]);
        writeRandomCVOutput(1, randomCVCurrent_[1]);
    }

    void serviceUSBMIDI()
    {
        uint8_t buffer[64];

        while (tud_midi_available())
        {
            uint32_t count = tud_midi_stream_read(buffer, sizeof(buffer));

            for (uint32_t i = 0; i < count; i++)
            {
                handleMIDIByte(buffer[i]);
            }
        }
    }

    void handleMIDIByte(uint8_t byte)
    {
        if (byte == 0xF0)
        {
            sysexActive_ = true;
            sysexLength_ = 0;
            return;
        }

        if (sysexActive_)
        {
            if (byte == 0xF7)
            {
                processSysEx();
                sysexActive_ = false;
                sysexLength_ = 0;
            }
            else if (byte < 0x80 && sysexLength_ < kSysExBufferSize)
            {
                sysexBuffer_[sysexLength_] = byte;
                sysexLength_++;
            }
            return;
        }

        if (byte & 0x80)
        {
            midiStatus_ = byte;
            midiDataCount_ = 0;
            return;
        }

        uint8_t command = midiStatus_ & 0xF0;
        if (command != 0x80 && command != 0x90 && command != 0xB0 && command != 0xE0)
        {
            return;
        }

        midiData_[midiDataCount_] = byte;
        midiDataCount_++;

        if (midiDataCount_ < 2)
        {
            return;
        }

        if (command == 0x90 && midiData_[1] > 0)
        {
            midiNote_ = midiData_[0];
            updateMIDIPlaybackStep();
        }
        else if (command == 0xB0)
        {
            handleMIDIControlChange(midiData_[0], midiData_[1]);
        }
        else if (command == 0xE0)
        {
            int32_t bend = ((int32_t)midiData_[0] | ((int32_t)midiData_[1] << 7)) - 8192;
            pitchBendSemitoneQ_ = bend * kPitchBendScale;
            updateMIDIPlaybackStep();
        }

        midiDataCount_ = 0;
    }

    void updateMIDIPlaybackStep()
    {
        int32_t semitoneOffsetQ =
            (((int32_t)midiNote_ - 60) << kPlaybackFracBits)
            + pitchBendSemitoneQ_;

        playbackStepQ_ = semitoneOffsetToPlaybackStep(semitoneOffsetQ);
    }

    uint32_t semitoneOffsetToPlaybackStep(int32_t semitoneOffsetQ)
    {
        int octave = 0;
        const int32_t octaveQ = 12 << kPlaybackFracBits;

        while (semitoneOffsetQ < 0)
        {
            semitoneOffsetQ += octaveQ;
            octave--;
        }

        while (semitoneOffsetQ >= octaveQ)
        {
            semitoneOffsetQ -= octaveQ;
            octave++;
        }

        int semitone = semitoneOffsetQ >> kPlaybackFracBits;
        int32_t frac = semitoneOffsetQ & ((1 << kPlaybackFracBits) - 1);
        uint32_t step = kSemitoneSteps[semitone];

        uint32_t nextStep = (semitone == 11)
            ? (kSemitoneSteps[0] << 1)
            : kSemitoneSteps[semitone + 1];
        step += ((nextStep - step) * (uint32_t)frac) >> kPlaybackFracBits;

        if (octave > 0)
        {
            step <<= octave;
        }
        else if (octave < 0)
        {
            int shift = -octave;
            step >>= shift;
            if (step < 1) step = 1;
        }

        return step;
    }

    void processSysEx()
    {
        if (sysexLength_ < 2 || sysexBuffer_[0] != kSysExManufacturer)
        {
            return;
        }

        uint8_t command = sysexBuffer_[1];
        uint8_t *payload = &sysexBuffer_[2];
        uint32_t size = sysexLength_ - 2;

        switch (command)
        {
        case kSysExImportBegin:
            handleImportBegin(payload, size);
            break;

        case kSysExImportData:
            handleImportData(payload, size);
            break;

        case kSysExImportEnd:
            handleImportEnd();
            break;

        case kSysExSaveKit:
            flashSaveRequested_ = true;
            break;

        case kSysExLoadKit:
            loadKitFromFlash();
            break;

        case kSysExPing:
            sendSysExAck(kAckPing, 0, 0);
            break;

        case kSysExPatternData:
            handlePatternData(payload, size);
            break;

        case kSysExFactoryPatterns:
            loadFactoryPatterns();
            sendSysExAck(kAckPatternLoaded, 127, kNumPatterns);
            break;

        case kSysExCVConfig:
            handleCVConfig(payload, size);
            break;

        default:
            break;
        }
    }

    void handleMIDIControlChange(uint8_t cc, uint8_t value)
    {
        int knob = -1;
        if (cc == 16) knob = 0;      // Main
        else if (cc == 17) knob = 1; // X
        else if (cc == 18) knob = 2; // Y

        if (knob < 0)
        {
            return;
        }

        midiKnobValue_[knob] = ((int32_t)value * 4095) / 127;
        midiKnobActive_[knob] = true;
    }

    int32_t resolveKnobControl(int knob, int32_t physical)
    {
        if (midiKnobActive_[knob])
        {
            int32_t delta = physical - lastPhysicalKnob_[knob];
            if (delta < 0) delta = -delta;
            if (delta >= kKnobEditMoveThreshold)
            {
                midiKnobActive_[knob] = false;
                lastPhysicalKnob_[knob] = physical;
                return physical;
            }

            return midiKnobValue_[knob];
        }

        lastPhysicalKnob_[knob] = physical;
        return physical;
    }

    void loadFactoryPatterns()
    {
        for (int i = 0; i < kNumPatterns; i++)
        {
            patterns_[i] = factoryPatterns[i];
        }
    }

    void handlePatternData(uint8_t *data, uint32_t size)
    {
        if (size < 2)
        {
            sendSysExAck(kAckPatternReject, 0, 0);
            return;
        }

        int pattern = data[0];
        int length = data[1];

        if (pattern < 0 || pattern >= kNumPatterns || length < 1
            || length > kMaxPatternLen || size < (uint32_t)(2 + length))
        {
            sendSysExAck(kAckPatternReject, (uint8_t)pattern, (uint16_t)length);
            return;
        }

        for (int i = 0; i < length; i++)
        {
            if (data[2 + i] >= kNumSlots)
            {
                sendSysExAck(kAckPatternReject, (uint8_t)pattern, (uint16_t)i);
                return;
            }
        }

        patterns_[pattern].length = (uint8_t)length;
        for (int i = 0; i < kMaxPatternLen; i++)
        {
            patterns_[pattern].steps[i] = i < length ? data[2 + i] : 0;
        }

        if (selectedPattern_ == pattern)
        {
            if (stepIndex_ >= length)
            {
                stepIndex_ = 0;
            }
            applyStepChange();
        }

        sendSysExAck(kAckPatternLoaded, (uint8_t)pattern, (uint16_t)length);
    }

    void handleCVConfig(uint8_t *data, uint32_t size)
    {
        if (size < 5)
        {
            sendSysExAck(kAckCVConfigReject, 0, 0);
            return;
        }

        int output = data[0];
        uint8_t range = data[1];
        uint8_t quant = data[2];
        uint8_t div = data[3];
        uint8_t slew = data[4];
        bool coupled = output == 1 && size >= 6 && data[5] != 0;

        if (output < 0 || output >= 2
            || range >= kCVRangeCount
            || quant >= kCVQuantCount
            || div >= kCVClockDivCount
            || slew >= kCVSlewCount)
        {
            sendSysExAck(kAckCVConfigReject, (uint8_t)output, 0);
            return;
        }

        cvRange_[output] = range;
        cvQuant_[output] = quant;
        cvClockDiv_[output] = div;
        cvSlew_[output] = slew;
        if (output == 1)
        {
            cv2CoupledToCV1_ = coupled;
        }
        cvRandomStepCount_[output] = 0;
        randomCVTarget_[output] = makeRandomCVValue(output);

        sendSysExAck(kAckCVConfigLoaded, (uint8_t)output,
            (uint16_t)(range | (quant << 3) | (div << 7) | (slew << 10)));
    }

    void handleImportBegin(uint8_t *data, uint32_t size)
    {
        if (size < 4)
        {
            return;
        }

        int slot = data[0];
        uint8_t mask = data[1] & 0x03;
        int32_t length = (int32_t)data[2] | ((int32_t)data[3] << 7);

        if (slot < 0 || slot >= kNumSlots || mask == 0 || length <= 0)
        {
            importActive_ = false;
            sendSysExAck(kAckImportReject, 0, 0);
            return;
        }

        if (length > kBufferLength)
        {
            length = kBufferLength;
        }

        importActive_ = true;
        importSlot_ = slot;
        importChannelMask_ = mask;
        importExpectedLength_ = length;
        importIndex_ = 0;
        sendSysExAck(kAckImportBegin, (uint8_t)slot, (uint16_t)length);
    }

    void handleImportData(uint8_t *data, uint32_t size)
    {
        if (!importActive_)
        {
            sendSysExAck(kAckImportReject, 0, 0);
            return;
        }

        int slot = importSlot_;

        for (uint32_t i = 0; i + 1 < size && importIndex_ < importExpectedLength_; i += 2)
        {
            uint8_t packed = (data[i] & 0x7F) | ((data[i + 1] & 0x01) << 7);
            int8_t sample = (int8_t)packed;

            if (importChannelMask_ & 0x01)
            {
                bufferA_[slot][importIndex_] = sample;
            }
            if (importChannelMask_ & 0x02)
            {
                bufferB_[slot][importIndex_] = sample;
            }

            importIndex_++;
        }
    }

    void handleImportEnd()
    {
        if (!importActive_)
        {
            return;
        }

        int slot = importSlot_;
        int32_t length = importIndex_;

        if (length >= kMinRecordingSamples)
        {
            if (importChannelMask_ & 0x01)
            {
                recordedLengthA_[slot] = length;
                recordingChannelA_[slot] = true;
            }
            if (importChannelMask_ & 0x02)
            {
                recordedLengthB_[slot] = length;
                recordingChannelB_[slot] = true;
            }

            recordedLength_[slot] =
                recordedLengthA_[slot] > recordedLengthB_[slot]
                    ? recordedLengthA_[slot]
                    : recordedLengthB_[slot];
            hasRecording_[slot] = recordingChannelA_[slot] || recordingChannelB_[slot];
            reverseProb_[slot] = 0;
            recomputeVariationRecordingState();
            sendSysExAck(kAckImportEnd, (uint8_t)slot, (uint16_t)length);
        }
        else
        {
            sendSysExAck(kAckImportShort, (uint8_t)slot, (uint16_t)length);
        }

        importActive_ = false;
    }

    void sendSysExAck(uint8_t code, uint8_t slot, uint16_t value)
    {
        uint8_t message[] = {
            0xF0,
            kSysExManufacturer,
            kSysExAck,
            code,
            slot,
            (uint8_t)(value & 0x7F),
            (uint8_t)((value >> 7) & 0x7F),
            0xF7
        };
        tud_midi_stream_write(0, message, sizeof(message));
    }

    void closeZCommand()
    {
        zCommandArmed_ = false;
        zCommandNeedsRelease_ = false;
        zCommandTimeoutSamples_ = 0;
        zDoubleTapSamples_ = 0;
        zFirstTapDown_ = false;
    }

    // Hold Z to arm, then single-tap Clear or double-tap Save.
    ZCommand updateZCommand(bool switchDown)
    {
        bool pressed = switchDown && !lastSwitchDown_;
        bool released = !switchDown && lastSwitchDown_;

        if (zCommandArmed_)
        {
            if (zCommandNeedsRelease_)
            {
                if (!switchDown)
                {
                    zCommandNeedsRelease_ = false;
                    zCommandTimeoutSamples_ = kCommandTimeoutSamples;
                }
                return kZCommandNone;
            }

            if (zFirstTapDown_)
            {
                if (released)
                {
                    zFirstTapDown_ = false;
                    zDoubleTapSamples_ = kDoubleTapSamples;
                }
                return kZCommandNone;
            }

            if (zDoubleTapSamples_ > 0)
            {
                if (pressed)
                {
                    zIgnoreRelease_ = true;
                    closeZCommand();
                    return kZCommandSave;
                }

                zDoubleTapSamples_--;
                if (zDoubleTapSamples_ == 0)
                {
                    closeZCommand();
                    return kZCommandClear;
                }
                return kZCommandNone;
            }

            if (zCommandTimeoutSamples_ > 0)
            {
                if (pressed)
                {
                    zFirstTapDown_ = true;
                    zCommandTimeoutSamples_ = 0;
                    return kZCommandNone;
                }

                zCommandTimeoutSamples_--;
                if (zCommandTimeoutSamples_ == 0)
                {
                    closeZCommand();
                }
                return kZCommandNone;
            }

            closeZCommand();
            return kZCommandNone;
        }

        if (zIgnoreRelease_)
        {
            if (!switchDown)
            {
                zIgnoreRelease_ = false;
            }
            zHoldSamples_ = 0;
            return kZCommandNone;
        }

        if (switchDown)
        {
            if (zHoldSamples_ < kClearHoldSamples)
            {
                zHoldSamples_++;
            }

            if (zHoldSamples_ >= kClearHoldSamples)
            {
                zCommandArmed_ = true;
                zCommandNeedsRelease_ = true;
                zHoldSamples_ = 0;
            }
            return kZCommandNone;
        }

        if (released)
        {
            zHoldSamples_ = 0;
            return kZCommandReset;
        }

        zHoldSamples_ = 0;
        return kZCommandNone;
    }

    void beginClearAll()
    {
        for (int i = 0; i < kNumSlots; i++)
        {
            hasRecording_[i] = false;
            recordedLength_[i] = 0;
            recordedLengthA_[i] = 0;
            recordedLengthB_[i] = 0;
            recordingChannelA_[i] = false;
            recordingChannelB_[i] = false;
            reverseProb_[i] = 0;
        }

        writeIndex_ = 0;
        playIndex_ = 0;
        playPositionQ_ = 0;
        variationRecordedLength_ = 0;
        variationRecordedLengthA_ = 0;
        variationRecordedLengthB_ = 0;
        variationHasRecording_ = false;
        variationChannelA_ = false;
        variationChannelB_ = false;
        repeatPosition_ = 0;
        repeatsCompleted_ = 0;
        stepFinished_ = false;
        inSilenceFill_ = false;
        bufferFull_ = false;
        recordingActive_ = false;

        __mem_fence_release();
        pendingRecordFinalize_ = false;
        pendingRecordVariation_ = false;
        clearRequested_ = true;
    }

    void serviceClearRequest()
    {
        if (clearRequested_ && !clearInProgress_)
        {
            clearRequested_ = false;
            clearInProgress_ = true;
            clearSlot_ = 0;
            clearIndex_ = 0;
        }

        if (!clearInProgress_)
        {
            return;
        }

        for (int i = 0; i < kClearSamplesPerCore1Tick; i++)
        {
            bufferA_[clearSlot_][clearIndex_] = 0;
            bufferB_[clearSlot_][clearIndex_] = 0;

            clearSlot_++;
            if (clearSlot_ >= kNumSlots)
            {
                clearSlot_ = 0;
                clearIndex_++;

                if (clearIndex_ >= kBufferLength)
                {
                    clearInProgress_ = false;
                    return;
                }
            }
        }
    }

    void serviceRecordingFinalize()
    {
        if (!pendingRecordFinalize_)
        {
            return;
        }

        __mem_fence_acquire();
        pendingRecordFinalize_ = false;
        int slot = pendingRecordSlot_;
        int32_t length = pendingRecordLength_;

        if (pendingRecordVariation_)
        {
            finalizeVariationRecording(
                length,
                pendingRecordChannelA_,
                pendingRecordChannelB_);
            __mem_fence_release();
            return;
        }

        if (pendingRecordChannelA_)
        {
            recordedLengthA_[slot] = length;
            recordingChannelA_[slot] = true;
        }
        if (pendingRecordChannelB_)
        {
            recordedLengthB_[slot] = length;
            recordingChannelB_[slot] = true;
        }

        recordedLength_[slot] =
            recordedLengthA_[slot] > recordedLengthB_[slot]
                ? recordedLengthA_[slot]
                : recordedLengthB_[slot];
        hasRecording_[slot] = recordingChannelA_[slot] || recordingChannelB_[slot];
        reverseProb_[slot] = 0;
        recomputeVariationRecordingState();
        __mem_fence_release();
    }

    void finalizeVariationRecording(int32_t length, bool channelA, bool channelB)
    {
        if (length < 0) length = 0;
        if (length > kVariationBufferLength) length = kVariationBufferLength;

        if (channelA)
        {
            setVariationSegmentLengths(recordedLengthA_, recordingChannelA_, length);
        }
        else
        {
            setVariationSegmentLengths(recordedLengthA_, recordingChannelA_, 0);
        }

        if (channelB)
        {
            setVariationSegmentLengths(recordedLengthB_, recordingChannelB_, length);
        }
        else
        {
            setVariationSegmentLengths(recordedLengthB_, recordingChannelB_, 0);
        }

        for (int slot = 0; slot < kNumSlots; slot++)
        {
            recordedLength_[slot] =
                recordedLengthA_[slot] > recordedLengthB_[slot]
                    ? recordedLengthA_[slot]
                    : recordedLengthB_[slot];
            hasRecording_[slot] = recordingChannelA_[slot] || recordingChannelB_[slot];
            reverseProb_[slot] = 0;
        }

        recomputeVariationRecordingState();
    }

    void setVariationSegmentLengths(
        int32_t *lengths,
        bool *channels,
        int32_t totalLength)
    {
        for (int slot = 0; slot < kNumSlots; slot++)
        {
            int32_t segmentLength = totalLength;
            if (segmentLength > kBufferLength)
            {
                segmentLength = kBufferLength;
            }
            if (segmentLength < 0)
            {
                segmentLength = 0;
            }

            lengths[slot] = segmentLength;
            channels[slot] = segmentLength > 0;
            totalLength -= segmentLength;
        }
    }

    void recomputeVariationRecordingState()
    {
        variationRecordedLengthA_ = combinedVariationLength(recordedLengthA_);
        variationRecordedLengthB_ = combinedVariationLength(recordedLengthB_);
        variationRecordedLength_ =
            variationRecordedLengthA_ > variationRecordedLengthB_
                ? variationRecordedLengthA_
                : variationRecordedLengthB_;
        variationChannelA_ = variationRecordedLengthA_ > 0;
        variationChannelB_ = variationRecordedLengthB_ > 0;
        variationHasRecording_ = variationChannelA_ || variationChannelB_;
    }

    int32_t combinedVariationLength(int32_t *lengths)
    {
        int32_t total = 0;
        for (int slot = 0; slot < kNumSlots; slot++)
        {
            int32_t length = lengths[slot];
            if (length <= 0)
            {
                break;
            }
            if (length > kBufferLength)
            {
                length = kBufferLength;
            }

            total += length;
            if (length < kBufferLength)
            {
                break;
            }
        }
        return total;
    }

    void outputSlotSampleOrLive(
        int slot,
        int32_t inA,
        int32_t inB,
        int32_t sampleA,
        int32_t sampleB)
    {
        bool channelA = playbackChannelA(slot);
        bool channelB = playbackChannelB(slot);
        int32_t lengthA = playbackLengthA(slot);
        int32_t lengthB = playbackLengthB(slot);

        int32_t unrecordedA = variationMode_ ? 0 : inA;
        int32_t unrecordedB = variationMode_ ? 0 : inB;
        int32_t outA = channelA
            ? (playIndex_ < lengthA ? gainRecordedSample(sampleA) : 0)
            : unrecordedA;
        int32_t outB = channelB
            ? (playIndex_ < lengthB ? gainRecordedSample(sampleB) : 0)
            : unrecordedB;

        if (variationMode_)
        {
            outA = smoothVariationEdge(slot, outA, channelA);
            outB = smoothVariationEdge(slot, outB, channelB);

            if (variationFadeSamples_ > 0)
            {
                outA = crossfadeVariationRetrigger(outA, variationFadeStartA_);
                outB = crossfadeVariationRetrigger(outB, variationFadeStartB_);
                variationFadeSamples_--;
            }
        }

        setAudioOut(outA, outB);
    }

    int8_t audioToStoredSample(int32_t sample)
    {
        int32_t stored = sample >> 4;
        if (stored < -128) return -128;
        if (stored > 127) return 127;
        return (int8_t)stored;
    }

    int32_t gainRecordedSample(int32_t sample)
    {
        int32_t gained = (sample * kSamplePlaybackGainNum) / kSamplePlaybackGainDen;
        if (gained < -2048) return -2048;
        if (gained > 2047) return 2047;
        return gained;
    }

    int playbackModeForSlot(int slot)
    {
        return variationMode_ ? variationPlaybackMode_ : playbackMode_[slot];
    }

    bool hasPlaybackRecording(int slot)
    {
        return variationMode_ ? variationHasRecording_ : hasRecording_[slot];
    }

    bool playbackChannelA(int slot)
    {
        return variationMode_ ? variationChannelA_ : recordingChannelA_[slot];
    }

    bool playbackChannelB(int slot)
    {
        return variationMode_ ? variationChannelB_ : recordingChannelB_[slot];
    }

    int32_t playbackLength(int slot)
    {
        return variationMode_ ? variationRecordedLength_ : recordedLength_[slot];
    }

    int32_t playbackLengthA(int slot)
    {
        return variationMode_ ? variationRecordedLengthA_ : recordedLengthA_[slot];
    }

    int32_t playbackLengthB(int slot)
    {
        return variationMode_ ? variationRecordedLengthB_ : recordedLengthB_[slot];
    }

    int32_t playbackSampleA(int slot)
    {
        return variationMode_
            ? interpolatedVariationSample(bufferA_, variationRecordedLengthA_)
            : interpolatedBufferSample(bufferA_[slot], recordedLengthA_[slot]);
    }

    int32_t playbackSampleB(int slot)
    {
        return variationMode_
            ? interpolatedVariationSample(bufferB_, variationRecordedLengthB_)
            : interpolatedBufferSample(bufferB_[slot], recordedLengthB_[slot]);
    }

    int32_t interpolatedBufferSample(int8_t *buffer, int32_t length)
    {
        if (length <= 0)
        {
            return 0;
        }

        int32_t index = playPositionQ_ >> kPlaybackFracBits;
        if (index < 0) index = 0;
        if (index >= length) index = length - 1;

        int32_t nextIndex = index + 1;
        if (nextIndex >= length)
        {
            nextIndex = index;
        }

        int32_t frac = playPositionQ_ & ((1 << kPlaybackFracBits) - 1);
        int32_t sample = (int32_t)buffer[index] << 4;
        int32_t nextSample = (int32_t)buffer[nextIndex] << 4;
        return sample + (((nextSample - sample) * frac) >> kPlaybackFracBits);
    }

    int32_t interpolatedVariationSample(
        int8_t buffer[kNumSlots][kBufferLength],
        int32_t length)
    {
        if (length <= 0)
        {
            return 0;
        }

        int32_t index = playPositionQ_ >> kPlaybackFracBits;
        if (index < 0) index = 0;
        if (index >= length) index = length - 1;

        int32_t nextIndex = index + 1;
        if (nextIndex >= length)
        {
            nextIndex = index;
        }

        int32_t frac = playPositionQ_ & ((1 << kPlaybackFracBits) - 1);
        int32_t sample = (int32_t)readVariationSample(buffer, index) << 4;
        int32_t nextSample = (int32_t)readVariationSample(buffer, nextIndex) << 4;
        return sample + (((nextSample - sample) * frac) >> kPlaybackFracBits);
    }

    int8_t readVariationSample(
        int8_t buffer[kNumSlots][kBufferLength],
        int32_t index)
    {
        if (index < 0) index = 0;
        if (index >= kVariationBufferLength) index = kVariationBufferLength - 1;
        int slot = index / kBufferLength;
        int offset = index - (slot * kBufferLength);
        return buffer[slot][offset];
    }

    void writeVariationSample(
        int8_t buffer[kNumSlots][kBufferLength],
        int32_t index,
        int8_t sample)
    {
        if (index < 0 || index >= kVariationBufferLength)
        {
            return;
        }
        int slot = index / kBufferLength;
        int offset = index - (slot * kBufferLength);
        buffer[slot][offset] = sample;
    }

    int32_t smoothVariationEdge(int slot, int32_t sample, bool recordedChannel)
    {
        if (!recordedChannel)
        {
            return sample;
        }

        int32_t length = playbackLength(slot);
        int32_t fade = kVariationFadeSamples;
        if (length <= 1)
        {
            return 0;
        }
        if (fade * 2 > length)
        {
            fade = length / 2;
        }
        if (fade <= 0)
        {
            return sample;
        }

        int32_t fromStart = playIndex_;
        int32_t fromEnd = length - 1 - playIndex_;
        int32_t gain = fade;
        if (fromStart < gain) gain = fromStart;
        if (fromEnd < gain) gain = fromEnd;

        if (gain >= fade)
        {
            return sample;
        }
        if (gain <= 0)
        {
            return 0;
        }
        return (sample * gain) / fade;
    }

    int32_t crossfadeVariationRetrigger(int32_t target, int32_t start)
    {
        int32_t fade = kVariationFadeSamples;
        int32_t elapsed = fade - variationFadeSamples_;
        if (elapsed < 0) elapsed = 0;
        if (elapsed > fade) elapsed = fade;
        return ((start * (fade - elapsed)) + (target * elapsed)) / fade;
    }

    void setAudioOut(int32_t outA, int32_t outB)
    {
        lastOutputA_ = outA;
        lastOutputB_ = outB;
        AudioOut1(outA);
        AudioOut2(outB);
    }

    void outputSlotSilenceOrLive(int slot, int32_t inA, int32_t inB)
    {
        outputSlotSampleOrLive(slot, inA, inB, 0, 0);
    }

    void serviceFlashSave()
    {
        if (!flashSaveRequested_)
        {
            return;
        }

        flashSaveRequested_ = false;
        saveKitToFlash();
        saveFeedbackSamples_ = kSaveFeedbackSamples;
    }

    void saveKitToFlash()
    {
        flashWriteOffset_ = 0;
        flashPageFill_ = 0;

        uint32_t ints = save_and_disable_interrupts();
        flash_range_erase(kFlashStorageOffset, kFlashStorageSize);
        restore_interrupts(ints);

        writeFlashU32(kFlashMagic);
        writeFlashU32(kFlashVersion);
        writeFlashU32((uint32_t)kBufferLength);
        writeFlashU32((uint32_t)kNumSlots);

        for (int slot = 0; slot < kNumSlots; slot++)
        {
            writeFlashU32((uint32_t)recordedLength_[slot]);
            writeFlashU32((uint32_t)recordedLengthA_[slot]);
            writeFlashU32((uint32_t)recordedLengthB_[slot]);
            writeFlashU32(recordingChannelA_[slot] ? 1u : 0u);
            writeFlashU32(recordingChannelB_[slot] ? 1u : 0u);
            writeFlashU32((uint32_t)playbackMode_[slot]);
            writeFlashU32((uint32_t)reverseProb_[slot]);
        }

        for (int slot = 0; slot < kNumSlots; slot++)
        {
            writeFlashBytes((uint8_t *)bufferA_[slot], kBufferLength);
            writeFlashBytes((uint8_t *)bufferB_[slot], kBufferLength);
        }

        writeFlashU32(kPatternFlashMagic);
        for (int pattern = 0; pattern < kNumPatterns; pattern++)
        {
            writeFlashByte(patterns_[pattern].length);
            writeFlashBytes(patterns_[pattern].steps, kMaxPatternLen);
        }

        writeFlashU32(kCVConfigFlashMagic);
        for (int output = 0; output < 2; output++)
        {
            writeFlashByte(cvRange_[output]);
            writeFlashByte(cvQuant_[output]);
            writeFlashByte(cvClockDiv_[output]);
            writeFlashByte(cvSlew_[output]);
        }
        writeFlashByte(cv2CoupledToCV1_ ? 1 : 0);

        flushFlashPage();
    }

    void loadKitFromFlash()
    {
        const uint8_t *read = (const uint8_t *)(XIP_BASE + kFlashStorageOffset);

        uint32_t magic = readFlashU32(read);
        uint32_t version = readFlashU32(read);
        uint32_t bufferLength = readFlashU32(read);
        uint32_t numSlots = readFlashU32(read);

        if (magic != kFlashMagic
            || version != kFlashVersion
            || bufferLength != (uint32_t)kBufferLength
            || numSlots != (uint32_t)kNumSlots)
        {
            return;
        }

        for (int slot = 0; slot < kNumSlots; slot++)
        {
            recordedLength_[slot] = clampStoredLength((int32_t)readFlashU32(read));
            recordedLengthA_[slot] = clampStoredLength((int32_t)readFlashU32(read));
            recordedLengthB_[slot] = clampStoredLength((int32_t)readFlashU32(read));
            recordingChannelA_[slot] = readFlashU32(read) != 0;
            recordingChannelB_[slot] = readFlashU32(read) != 0;
            playbackMode_[slot] = clampPlaybackMode((int32_t)readFlashU32(read));
            reverseProb_[slot] = clamp12Bit((int32_t)readFlashU32(read));
            hasRecording_[slot] = recordingChannelA_[slot] || recordingChannelB_[slot];
        }

        for (int slot = 0; slot < kNumSlots; slot++)
        {
            for (int i = 0; i < kBufferLength; i++)
            {
                bufferA_[slot][i] = (int8_t)*read++;
            }
            for (int i = 0; i < kBufferLength; i++)
            {
                bufferB_[slot][i] = (int8_t)*read++;
            }
        }

        if (readFlashU32(read) == kPatternFlashMagic)
        {
            const uint8_t *check = read;
            bool valid = true;

            for (int pattern = 0; pattern < kNumPatterns; pattern++)
            {
                uint8_t length = *check++;
                if (length < 1 || length > kMaxPatternLen)
                {
                    valid = false;
                }

                for (int step = 0; step < kMaxPatternLen; step++)
                {
                    uint8_t slot = *check++;
                    if (step < length && slot >= kNumSlots)
                    {
                        valid = false;
                    }
                }
            }

            if (valid)
            {
                for (int pattern = 0; pattern < kNumPatterns; pattern++)
                {
                    patterns_[pattern].length = *read++;
                    for (int step = 0; step < kMaxPatternLen; step++)
                    {
                        patterns_[pattern].steps[step] = *read++;
                    }
                }
            }
        }

        if (readFlashU32(read) == kCVConfigFlashMagic)
        {
            for (int output = 0; output < 2; output++)
            {
                uint8_t range = *read++;
                uint8_t quant = *read++;
                uint8_t div = *read++;
                uint8_t slew = *read++;

                if (range < kCVRangeCount
                    && quant < kCVQuantCount
                    && div < kCVClockDivCount
                    && slew < kCVSlewCount)
                {
                    cvRange_[output] = range;
                    cvQuant_[output] = quant;
                    cvClockDiv_[output] = div;
                    cvSlew_[output] = slew;
                    cvRandomStepCount_[output] = 0;
                }
            }

            uint8_t coupled = *read++;
            if (coupled <= 1)
            {
                cv2CoupledToCV1_ = coupled != 0;
            }
        }

        writeIndex_ = 0;
        playIndex_ = 0;
        playPositionQ_ = 0;
        repeatPosition_ = 0;
        repeatsCompleted_ = 0;
        stepFinished_ = false;
        inSilenceFill_ = false;
        recordingActive_ = false;
        recomputeVariationRecordingState();
    }

    void writeFlashU32(uint32_t value)
    {
        writeFlashByte((uint8_t)(value & 0xFF));
        writeFlashByte((uint8_t)((value >> 8) & 0xFF));
        writeFlashByte((uint8_t)((value >> 16) & 0xFF));
        writeFlashByte((uint8_t)((value >> 24) & 0xFF));
    }

    void writeFlashBytes(uint8_t *data, uint32_t count)
    {
        for (uint32_t i = 0; i < count; i++)
        {
            writeFlashByte(data[i]);
        }
    }

    void writeFlashByte(uint8_t value)
    {
        flashPage_[flashPageFill_] = value;
        flashPageFill_++;

        if (flashPageFill_ >= kFlashProgramPageSize)
        {
            programFlashPage();
        }
    }

    void flushFlashPage()
    {
        if (flashPageFill_ == 0)
        {
            return;
        }

        while (flashPageFill_ < kFlashProgramPageSize)
        {
            flashPage_[flashPageFill_] = 0xFF;
            flashPageFill_++;
        }

        programFlashPage();
    }

    void programFlashPage()
    {
        uint32_t ints = save_and_disable_interrupts();
        flash_range_program(
            kFlashStorageOffset + flashWriteOffset_,
            flashPage_,
            kFlashProgramPageSize);
        restore_interrupts(ints);

        flashWriteOffset_ += kFlashProgramPageSize;
        flashPageFill_ = 0;
    }

    uint32_t readFlashU32(const uint8_t *&read)
    {
        uint32_t value = (uint32_t)read[0]
            | ((uint32_t)read[1] << 8)
            | ((uint32_t)read[2] << 16)
            | ((uint32_t)read[3] << 24);
        read += 4;
        return value;
    }

    int32_t clampStoredLength(int32_t value)
    {
        if (value < 0) return 0;
        if (value > kBufferLength) return kBufferLength;
        return value;
    }

    int32_t clamp12Bit(int32_t value)
    {
        if (value < 0) return 0;
        if (value > 4095) return 4095;
        return value;
    }

    int clampPlaybackMode(int value)
    {
        if (value < kModeLooping || value > kModePassthrough)
        {
            return kModeOneShot;
        }
        return value;
    }

    // Convert the latest clock period into the current repeat length.
    void updateRepeatLength()
    {
        if (haveClockPeriod_)
        {
            repeatLengthSamples_ = clockPeriodSamples_ >> subdivisionShift_;
        }
        else
        {
            repeatLengthSamples_ = 0;
        }
    }

    // Decide once per step whether this slot should play backward.
    void rollReverse()
    {
        uint32_t roll = nextRandom() & 0x0FFF; // 0-4095
        playReverse_ = (int32_t)roll < reverseProb_[activeSlot_];
    }

    // Small, fast random-number generator for musical variation.
    uint32_t nextRandom()
    {
        uint32_t x = rngState_;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        rngState_ = x;
        return x;
    }

    // LEDs show slot selection, the active step, or command status.
    void updateLeds()
    {
        __mem_fence_acquire();

        if (zCommandArmed_ || clearInProgress_)
        {
            bool flashOn = ((sampleCounter_ >> 13) & 1) == 0;
            for (int i = 0; i < kNumSlots; i++)
            {
                LedOn(i, flashOn);
            }
            return;
        }

        if (variationModeFeedbackSamples_ > 0)
        {
            bool flashOn = ((sampleCounter_ >> 12) & 1) == 0;
            for (int i = 0; i < kNumSlots; i++)
            {
                LedOn(i, flashOn);
            }
            return;
        }

        if (saveFeedbackSamples_ > 0)
        {
            for (int i = 0; i < kNumSlots; i++)
            {
                LedOn(i);
            }
            return;
        }

        if (recordingActive_)
        {
            return;
        }

        if (lastSwitchUp_)
        {
            // Switch up: selected recording slot.
            for (int i = 0; i < kNumSlots; i++)
            {
                if (i == selectedSlot_) LedOn(i); else LedOff(i);
            }
        }
        else
        {
            // Switch middle: currently playing slot, or active variation
            // when booted into one-sample variation mode.
            int litLed = variationMode_ ? activeVariation_ : activeSlot_;
            for (int i = 0; i < kNumSlots; i++)
            {
                if (i == litLed) LedOn(i); else LedOff(i);
            }
        }
    }

    int8_t bufferA_[kNumSlots][kBufferLength] = {{0}};
    int8_t bufferB_[kNumSlots][kBufferLength] = {{0}};

    int32_t writeIndex_ = 0;
    int32_t playIndex_ = 0;
    int32_t playPositionQ_ = 0;
    bool playDirReverse_ = false;
    int32_t lastOutputA_ = 0;
    int32_t lastOutputB_ = 0;
    int32_t variationFadeSamples_ = 0;
    int32_t variationFadeStartA_ = 0;
    int32_t variationFadeStartB_ = 0;

    // Playback position inside the current divided repeat.
    int32_t repeatPosition_ = 0;
    bool inSilenceFill_ = false;

    // One Shot and Interrupt use these to know when their step is done.
    int32_t repeatsCompleted_ = 0;
    bool stepFinished_ = false;
    int32_t recordedLength_[kNumSlots] = {0};
    int32_t recordedLengthA_[kNumSlots] = {0};
    int32_t recordedLengthB_[kNumSlots] = {0};
    int32_t variationRecordedLength_ = 0;
    int32_t variationRecordedLengthA_ = 0;
    int32_t variationRecordedLengthB_ = 0;

    bool hasRecording_[kNumSlots] = {false};
    bool recordingChannelA_[kNumSlots] = {false};
    bool recordingChannelB_[kNumSlots] = {false};
    bool variationHasRecording_ = false;
    bool variationChannelA_ = false;
    bool variationChannelB_ = false;
    bool wasRecording_ = false;
    bool recordingHadInput_ = false;
    bool recordingWriteA_ = false;
    bool recordingWriteB_ = false;
    volatile bool pendingRecordFinalize_ = false;
    volatile int pendingRecordSlot_ = 0;
    volatile int32_t pendingRecordLength_ = 0;
    volatile bool pendingRecordChannelA_ = false;
    volatile bool pendingRecordChannelB_ = false;
    volatile bool pendingRecordVariation_ = false;
    bool bufferFull_ = false;
    bool cv1SeenNegative_ = false;
    bool cv2SeenNegative_ = false;
    int32_t zHoldSamples_ = 0;
    volatile bool zCommandArmed_ = false;
    bool zCommandNeedsRelease_ = false;
    int32_t zCommandTimeoutSamples_ = 0;
    int32_t zDoubleTapSamples_ = 0;
    bool zFirstTapDown_ = false;
    bool zIgnoreRelease_ = false;
    int32_t bootModeWindowSamples_ = kBootModeWindowSamples;
    int32_t bootModeDownSamples_ = 0;
    volatile bool variationMode_ = false;
    volatile int32_t variationModeFeedbackSamples_ = 0;
    volatile uint32_t variationPitchStepQ_ = kPlaybackStepNormal;
    bool isUSBMIDIHost_ = false;
    bool switchUpMainPrimed_ = false;
    int32_t lastSwitchUpMain_ = 0;
    bool switchUpKnobsPrimed_ = false;
    int lastEditSlot_ = -1;
    int32_t lastKnobX_ = 0;
    int32_t lastKnobY_ = 0;
    bool middleMainPrimed_ = false;
    int32_t lastMiddleMain_ = 0;
    bool middleXYPrimed_ = false;
    int32_t lastMiddleKnobX_ = 0;
    int32_t lastMiddleKnobY_ = 0;
    volatile int32_t randomCVTarget_[2] = {0, 0};
    int32_t randomCVCurrent_[2] = {0, 0};
    int32_t randomCVCurrentQ_[2] = {0, 0};
    int32_t lastRandomCVOutput_[2] = {4096, 4096};
    uint8_t cvRandomStepCount_[2] = {0, 0};
    volatile uint8_t cvRange_[2] = {kCVRangeBipolar6V, kCVRangeBipolar6V};
    volatile uint8_t cvQuant_[2] = {kCVQuantOff, kCVQuantOff};
    volatile uint8_t cvClockDiv_[2] = {0, 0};
    volatile uint8_t cvSlew_[2] = {0, 3};
    volatile bool cv2CoupledToCV1_ = false;
    volatile uint32_t playbackStepQ_ = kPlaybackStepNormal;
    Pattern patterns_[kNumPatterns] = {};
    volatile bool midiKnobActive_[3] = {false, false, false};
    volatile int32_t midiKnobValue_[3] = {0, 0, 0};
    int32_t lastPhysicalKnob_[3] = {0, 0, 0};
    uint8_t midiStatus_ = 0;
    uint8_t midiData_[2] = {0, 0};
    uint8_t midiDataCount_ = 0;
    uint8_t midiNote_ = 60;
    int32_t pitchBendSemitoneQ_ = 0;
    bool sysexActive_ = false;
    uint8_t sysexBuffer_[kSysExBufferSize] = {0};
    uint32_t sysexLength_ = 0;
    bool importActive_ = false;
    int importSlot_ = 0;
    uint8_t importChannelMask_ = 0;
    int32_t importExpectedLength_ = 0;
    int32_t importIndex_ = 0;
    volatile bool flashSaveRequested_ = false;
    volatile int32_t saveFeedbackSamples_ = 0;
    alignas(4) uint8_t flashPage_[kFlashProgramPageSize] = {0};
    uint32_t flashPageFill_ = 0;
    uint32_t flashWriteOffset_ = 0;

    volatile int recordingSlot_ = 0;

    // Shared control state. Volatile keeps both cores seeing fresh values.
    volatile int selectedSlot_ = 0;       // switch-up Main knob selection
    volatile int selectedPattern_ = 0;    // switch-middle Main knob selection (0-20)
    volatile int shiftAmount_ = 0;        // switch-middle X knob shift (0-5)
    volatile int subdivisionShift_ = 0;   // switch-middle Y knob: 0/1/2/3 = x1/x2/x4/x8
    volatile bool recordingActive_ = false; // currently recording?
    volatile bool clockEdge_ = false;     // set by core 0, cleared by core 1
    volatile bool resetEdge_ = false;     // set by core 0, cleared by core 1

    volatile uint32_t sampleCounter_ = 0;

    volatile int activeSlot_ = 0;         // which slot to play (Middle)
    volatile int activeVariation_ = kVariationNormal;
    volatile bool restartPlayback_ = false; // set by core1, cleared by core0
    volatile bool playReverse_ = false;   // direction for the current step
    volatile int stepIndex_ = 0;

    // Samples per divided repeat. 0 means "no measured clock yet".
    volatile int32_t repeatLengthSamples_ = 0;

    // 10ms trigger output counters.
    volatile int32_t pulseOut1Samples_ = 0;
    volatile int32_t pulseOut2Samples_ = 0;

    // Recent clocks keep switch-up from stopping playback.
    int32_t clockActivitySamples_ = 0;

    volatile bool clearRequested_ = false;
    volatile bool clearInProgress_ = false;
    int32_t clearSlot_ = 0;
    int32_t clearIndex_ = 0;

    // Per-slot playback mode, set with X while the switch is up.
    int playbackMode_[kNumSlots] = {
        kModeOneShot, kModeOneShot, kModeOneShot,
        kModeOneShot, kModeOneShot, kModeOneShot
    };

    // Per-slot chance of reverse playback, set with Y while switch is up.
    volatile int reverseProb_[kNumSlots] = {0};
    int variationPlaybackMode_ = kModeOneShot;
    volatile int variationReverseProb_ = 0;

    // xorshift32 must not be seeded with zero.
    uint32_t rngState_ = 0x9F3779B9u;

    // Local mirrors let core 1 notice control changes.
    int lastSeenPattern_ = -1;

    int lastSeenShift_ = 0;
    int lastSeenSubShift_ = 0;

    // Clock-period tracking for divided repeats.
    uint32_t lastClockSample_ = 0;
    uint32_t clockPeriodSamples_ = 0;
    bool haveClockPeriod_ = false;

    // Mirrors switch-up for LED display.
    volatile bool lastSwitchUp_ = false;

    // Switch edge memory for reset/clear gestures.
    bool lastSwitchDown_ = false;
};

static Fragments *gFragments = nullptr;

int main()
{
    // ComputerCard recommends 144MHz because it is a clean multiple of
    // the 48kHz audio rate and reduces ADC-related tonal noise.
    set_sys_clock_khz(144000, true);

    // Fragments contains the audio buffers, so keep the object in
    // static RAM rather than on the small main stack. Recording slot 5
    // writes to the far end of those buffers, which can expose a stack
    // collision if the object is automatic.
    static Fragments fragments;
    gFragments = &fragments;

    // Launch the sequencer/LED loop on core 1. Core 0 continues into
    // fragments.Run() below, which drives ProcessSample() from the
    // audio interrupt.
    multicore_launch_core1([]() {
        // Run core 1 on the same Fragments instance as core 0. We use
        // this explicit pointer instead of ComputerCard::ThisPtr()
        // because ThisPtr() is assigned inside Run(), after core 1 has
        // already been launched.
        gFragments->Core1();
    });

    fragments.Run();
}
