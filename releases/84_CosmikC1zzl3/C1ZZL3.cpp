#include "ComputerCard.h"
#include "C1ZZL3_LUT.h"
#include "hardware/clocks.h"
#include "hardware/sync.h"
#include "pico/multicore.h"
#include "pico/time.h"
#include "tusb.h"
#include "usb_midi_host.h"

static constexpr uint8_t WebMidiManufacturer = 0x7Du;
static constexpr uint8_t WebMidiId[4] = {0x43u, 0x31u, 0x5Au, 0x33u}; // C1Z3
static constexpr uint8_t WebMidiCommandPreview = 0x01u;
static constexpr uint8_t WebMidiCommandSaveEnvelope = 0x02u;
static constexpr uint8_t WebMidiCommandSettings = 0x03u;
static constexpr uint8_t WebMidiCommandSaveSettings = 0x04u;
static constexpr uint8_t WebMidiCommandDeleteEnvelope = 0x05u;
static constexpr uint8_t WebMidiCommandRequestSettings = 0x06u;
static constexpr uint8_t WebMidiCommandSettingsResponse = 0x07u;
static constexpr uint8_t WebMidiCommandRequestEnvelopeSlots = 0x08u;
static constexpr uint8_t WebMidiCommandEnvelopeSlotsResponse = 0x09u;
static constexpr uint8_t WebMidiCommandRequestEnvelope = 0x0Au;
static constexpr uint8_t WebMidiCommandEnvelopeResponse = 0x0Bu;
static constexpr uint8_t WebMidiCommandRequestPitchEnvelope = 0x0Cu;
static constexpr uint8_t WebMidiCommandPitchEnvelopeResponse = 0x0Du;
static constexpr uint8_t WebMidiEnvelopeProtocolVersion = 1u;
static constexpr uint32_t WebMidiSettingsPayloadLength = 14u;
static constexpr uint32_t WebMidiStableSettingsPayloadLength = 8u;
static constexpr uint32_t WebMidiRangeSettingsPayloadLength = 6u;
static constexpr uint32_t WebMidiLegacySettingsPayloadLength = 5u;
static constexpr uint32_t WebMidiLegacyEnvelopePayloadLength = 97u;
static constexpr uint32_t WebMidiEnvelopePayloadLength = 137u;
static constexpr uint32_t WebMidiDeleteEnvelopePayloadLength = 1u;
static constexpr uint32_t WebMidiMaxSysexLength = 150u;

class C1ZZL3 : public ComputerCard
{
public:

    C1ZZL3()
    {
        phase1 = 0;
        phase2 = 0;

        noise = 1;

        loadPerformanceState();
        loadCustomEnvelopeState();
    }

    bool ShouldBootUsbHost()
    {
        return USBPowerState() == USBPowerState_t::DFP;
    }

    void ProcessUsbMidiByte(uint8_t byte)
    {
        if (byte == 0xF0u)
        {
            sysexReceiving = true;
            sysexLength = 0;
            sysexOverflow = false;
            return;
        }

        if (!sysexReceiving)
        {
            processMidiVoiceByte(byte);
            return;
        }

        if (byte == 0xF7u)
        {
            sysexReceiving = false;
            if (!sysexOverflow)
                handleWebMidiSysex();
            return;
        }

        if (sysexLength < sizeof(sysexBuffer))
            sysexBuffer[sysexLength++] = byte;
        else
            sysexOverflow = true;
    }

    uint8_t MidiInChannel() const
    {
        return midiInChannel;
    }

    void ProcessUsbMidiVoiceByte(uint8_t byte)
    {
        processMidiVoiceByte(byte);
    }

    void SendPendingUsbMidiOutput()
    {
        if (!turingMidiOutputEnabled)
        {
            pendingTuringMidiNoteOn = false;
            pendingTuringMidiNoteOff = false;
            if (turingMidiNoteActive)
            {
                uint8_t off[3] = {
                    (uint8_t)(0x80u | (turingMidiLastChannel & 0x0Fu)),
                    turingMidiLastNote,
                    0
                };
                tud_midi_stream_write(0, off, sizeof(off));
            }
            turingMidiNoteActive = false;
            return;
        }

        uint8_t channel = turingMidiOutputChannel & 0x0Fu;
        if (pendingTuringMidiNoteOff && !turingMidiNoteActive)
            pendingTuringMidiNoteOff = false;

        if ((pendingTuringMidiNoteOff || pendingTuringMidiNoteOn) &&
            turingMidiNoteActive)
        {
            pendingTuringMidiNoteOff = false;
            uint8_t off[3] = {
                (uint8_t)(0x80u | (turingMidiLastChannel & 0x0Fu)),
                turingMidiLastNote,
                0
            };
            tud_midi_stream_write(0, off, sizeof(off));
            turingMidiNoteActive = false;
        }

        if (!pendingTuringMidiNoteOn)
            return;

        uint8_t note = pendingTuringMidiNote;
        pendingTuringMidiNoteOn = false;

        uint8_t on[3] = {
            (uint8_t)(0x90u | channel),
            note,
            96
        };
        tud_midi_stream_write(0, on, sizeof(on));

        turingMidiLastNote = note;
        turingMidiLastChannel = channel;
        turingMidiNoteActive = true;
    }

    // =========================================================
    // AUDIO CALLBACK
    // =========================================================
    void ProcessSample() override
    {
        applyPendingMidiNote();

        int32_t in1 = AudioIn1();

        int32_t cv1 = CVIn1();
        int32_t cv2 = CVIn2();

        Switch mode = SwitchVal();

        int32_t main = KnobVal(Knob::Main);
        int32_t x    = KnobVal(Knob::X);
        int32_t y    = KnobVal(Knob::Y);

        applyMidiControlPickupResets(main, x, y);

        Switch previousMode = lastMode;

        updateStartupEnvelopeSelect(mode);

        if (envelopeSelectMode)
        {
            updateEnvelopeSelectMode(main, x, y, mode, previousMode);
            lastMode = mode;
            return;
        }

        if (!modePickupInitialized)
        {
            modePickupInitialized = true;
            if (mode == Switch::Down)
            {
                resetAltPickup(main, x, y);
                resetSaveGesture();
                previousMode = mode;
                lastMode = mode;
            }
            else
            {
                resetSavedSettingPickup(main, x, y);
            }
        }

        if (mode != Switch::Down)
            downEditUnlocked = true;

        if (mode != previousMode)
            resetModePickup(mode, previousMode, main, x, y);
        lastMode = mode;

        // =========================
        // PD SYNTH MODE (MID + DOWN)
        // =========================
        if (mode != Switch::Up)
        {
            bool alt = (mode == Switch::Down);

            if (!alt)
            {
                updateSynthModeControls(main, x, y);
            }
            else
            {
                if (previousMode != Switch::Down)
                {
                    saveHoldCanSave = (previousMode == Switch::Middle);
                    resetAltPickup(main, x, y);
                }

                if (downEditUnlocked)
                    updateAltControls(main, x, y);
            }

            // -------------------------
            // PITCH (octave map with hardware-tested 1V/oct input scale)
            // -------------------------
            int32_t freq = smoothPitch(pitchFrequency(currentPitchUnits(pitchControl, in1)));

            int32_t pd = clamp12(pdControl + (cv1 << 1));
            int32_t wave = clamp12(waveControl + (cv2 << 1));

            int32_t ring = clamp12(osc2Ring);
            int32_t noiseAmt = clamp12(osc2Noise);

            updateTuringClockState(false);

            bool pulse2GateHigh = PulseIn2();
            bool pulse2Trigger = pulse2GateHigh && !pulse2GateWasHigh;
            pulse2GateWasHigh = pulse2GateHigh;

            if (pulse2Trigger)
            {
                if (envelopePreset != (uint8_t)EnvelopePreset::Off)
                {
                    if (!envelopeActive)
                        syncOscillators();
                    triggerEnvelope(true, true);
                }
            }
            else if (pulse2EnvelopeHolding && !pulse2GateHigh)
            {
                requestEnvelopeRelease();
            }

            outputSynthVoice(freq, pd, wave, ring, noiseAmt);

            CVOut1(turingCv);
            CVOut2(turingModCv);
            PulseOut1(turingPulse);
            PulseOut2(turingAltPulse);

            updateSynthLEDs(alt, pd, wave);
            updateSaveGesture(alt);
        }

        // =========================
        // TURING MODE
        // =========================
        else
        {
            resetSaveGesture();

            updateTuringModeControls(main, x, y);

            updateTuringClockState(true);

            CVOut1(turingCv);
            CVOut2(turingModCv);

            outputTuringSynthVoice(cv1, cv2);

            PulseOut1(turingPulse);
            PulseOut2(turingAltPulse);

            updateTuringLEDs();
        }
    }

private:
    enum class EnvelopePreset : uint8_t
    {
        Off,
        Pluck,
        DoublePluck,
        Bounce,
        Bell,
        Brass,
        Strings,
        ReverseSwell,
        EvolvingDigital
    };

    struct EnvelopeStage
    {
        uint16_t level;
        uint32_t time;
    };

    struct EnvelopeProgram
    {
        EnvelopeStage amp[8];
        EnvelopeStage pd[8];
        EnvelopeStage pitch[8];
    };

    static constexpr uint8_t DefaultTuringCvOctaveRange = 2;
    static constexpr uint8_t MinTuringCvOctaveRange = 1;
    static constexpr uint8_t MaxTuringCvOctaveRange = 8;
    static constexpr uint8_t MidiCcDetune = 20;
    static constexpr uint8_t MidiCcRingAmount = 21;
    static constexpr uint8_t MidiCcNoiseAmount = 22;
    static constexpr uint8_t MidiCcPdAmount = 1;
    static constexpr uint8_t MidiCcWaveAmount = 23;
    static constexpr uint8_t MidiCcTuringCvOctaveRange = 24;
    static constexpr int32_t TuringAudioPitchDepth = 2048;
    static constexpr uint32_t TuringClockLedSamples = 1200u;
    static constexpr int32_t TuringToneMin = 256;
    static constexpr int32_t TuringToneMax = 3840;
    static constexpr int32_t TuringCvInputLimit = 1024;
    static constexpr int32_t PitchUnitsPerOctave = 4096;
    static constexpr int32_t MainPitchOctaves = 5;
    static constexpr int32_t PitchInputCountsPerVolt = 341;
    static constexpr int32_t MinPitchUnits = -2 * PitchUnitsPerOctave;
    static constexpr int32_t MaxPitchUnits = 7 * PitchUnitsPerOctave;
    static constexpr uint32_t C2PhaseIncrement = 5852465u;
    static constexpr uint8_t EnvelopePresetCount = 9;
    static constexpr uint8_t CustomEnvelopePreset = EnvelopePresetCount;
    static constexpr uint8_t CustomEnvelopeSlotCount = 8;
    static constexpr uint8_t EnvelopeLoopStartStage = 2;
    static constexpr uint8_t EnvelopeLoopEndStage = 5;
    static constexpr uint16_t EnvelopeLoopLevelThreshold = 128;
    static constexpr uint32_t EnvelopeLoopMinStageSamples = 960u;
    static constexpr uint32_t MinWebMidiEnvelopeSamples = 960u;
    static constexpr uint32_t MaxWebMidiStageSamples = 192000u;
    static constexpr uint16_t PitchEnvelopeCenter = 2048u;
    static constexpr int32_t PitchEnvelopeSemitoneRange = 24;
    static constexpr uint16_t PitchEnvelopeRatioQ12[49] = {
        1024, 1085, 1149, 1218, 1290, 1367, 1448, 1534,
        1625, 1722, 1825, 1933, 2048, 2170, 2299, 2435,
        2580, 2734, 2896, 3069, 3251, 3444, 3649, 3866,
        4096, 4340, 4598, 4871, 5161, 5468, 5793, 6137,
        6502, 6889, 7298, 7732, 8192, 8679, 9195, 9742,
        10321, 10935, 11585, 12274, 13004, 13777, 14596, 15464,
        16384
    };
    static constexpr uint32_t StartupSelectDelaySamples = 12000u;
    static constexpr uint32_t StartupSelectWindowSamples = 24000u;
    static constexpr uint32_t SaveMagic = 0x43315A33u; // C1Z3
    static constexpr uint16_t LegacyTuringMidiDefaultOnSaveVersion = 3;
    static constexpr uint16_t SaveVersion = 4;
    static constexpr int32_t OutputLowpassAlphaQ12 = 2008; // ~5.2 kHz at 48 kHz.
    static constexpr int32_t OutputHighpassAlphaQ12 = 4075; // 40 Hz at 48 kHz.
    static constexpr int32_t PdCompensationFloorQ12 = 3000; // Keep high-PD tones present but less inflated.
    static constexpr int32_t HighPitchSofteningStart = 28000000;
    static constexpr int32_t HighPitchSofteningRange = 42000000;
    static constexpr int32_t HighPitchSofteningMaxQ12 = 1200;
    static constexpr uint32_t TriggerDeClickSamples = 384u;
    static constexpr uint32_t LoopDeClickSamples = 384u;
    static constexpr int32_t LoopDeClickFloorQ12 = 3584;
    static constexpr uint32_t SaveFlashOffset =
        (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE) &
        ~(FLASH_SECTOR_SIZE - 1u);
    static constexpr uint32_t CustomEnvelopeMagic = 0x4331454Eu; // C1EN
    static constexpr uint16_t CustomEnvelopeSaveVersion = 2;
    static constexpr uint32_t CustomEnvelopeFlashOffset =
        SaveFlashOffset - FLASH_SECTOR_SIZE;
    static constexpr uint32_t SaveHoldSamples = 384000u;
    static constexpr uint32_t SaveConfirmSamples = 48000u;

    struct SavedPerformanceState
    {
        uint32_t magic;
        uint16_t version;
        uint16_t size;
        int32_t osc2Detune;
        int32_t osc2Level;
        int32_t pdControl;
        int32_t waveControl;
        int32_t osc2Ring;
        int32_t osc2Noise;
        uint8_t envelopePreset;
        uint8_t reserved[3];
        uint32_t checksum;
    };

    struct SavedCustomEnvelopeState
    {
        uint32_t magic;
        uint16_t version;
        uint16_t size;
        uint8_t loadedMask;
        uint8_t reserved[7];
        EnvelopeProgram slots[CustomEnvelopeSlotCount];
        uint32_t checksum;
    };

    // =========================================================
    // CZ OSCILLATOR
    // =========================================================

    inline int32_t getSine(uint32_t phase)
    {
        // 1024-entry full-cycle sine table.
        // Top 10 bits select the table index.
        // Next 10 bits interpolate between adjacent samples.
        uint32_t index = (phase >> 22) & 1023;
        uint32_t frac  = (phase >> 12) & 1023;

        int32_t a = sineLUT[index];
        int32_t b = sineLUT[(index + 1) & 1023];

        return a + (((b - a) * (int32_t)frac) >> 10);
    }
    
    inline uint16_t getCosWarp(uint32_t index)
    {
        return phaseWarpLUT[index & 1023];
    }
    
    
    // ---------------------------------------------------------
    // CZ-style smooth breakpoint warp
    // ---------------------------------------------------------
    inline uint32_t czPhaseWarp(uint32_t phase, uint32_t amount)
    {
        uint32_t p = (phase >> 20) & 4095;

        int32_t bp =
            2048 +
            (((int32_t)amount - 2048) >> 1);

        if (bp < 256) bp = 256;
        if (bp > 3840) bp = 3840;

        if (p < (uint32_t)bp)
        {
            uint32_t t = (p << 10) / bp;

            uint32_t c = getCosWarp(t);

            return (c * bp) >> 11;
        }
        else
        {
            uint32_t t =
                ((p - bp) << 10) /
                (4095 - bp);

            uint32_t c = getCosWarp(t);

            return bp +
                (((2047 - c) *
                 (4095 - bp)) >> 11);
        }
    }

    // ---------------------------------------------------------
    // OSCILLATOR
    // ---------------------------------------------------------
    void outputSynthVoice()
    {
        int32_t freq = smoothPitch(pitchFrequency(pitchUnits(pitchControl, 0)));
        int32_t pd = clamp12(pdControl);
        int32_t wave = clamp12(waveControl);

        outputSynthVoice(freq, pd, wave, osc2Ring, osc2Noise);
    }

    void outputTuringSynthVoice(int32_t cv1, int32_t cv2)
    {
        int32_t pitchOffset =
            (turingCv * TuringAudioPitchDepth * MainPitchOctaves) >> 12;
        int32_t freq = smoothPitch(
            pitchFrequency(pitchUnits(pitchControl, 0) + pitchOffset));
        int32_t pd = clampTuringTone(pdControl + turingCvToneOffset(cv1));
        int32_t wave = clampTuringTone(waveControl + turingCvToneOffset(cv2));

        outputSynthVoice(freq, pd, wave, osc2Ring, osc2Noise);
    }

    void outputSynthVoice(
        int32_t freq,
        int32_t pd,
        int32_t wave,
        int32_t ring,
        int32_t noiseAmt)
    {
        int32_t envelopeLevel = updateEnvelope();
        freq = applyPitchEnvelopeToFrequency(freq);
        pd = applyEnvelopeToPd(pd, envelopeLevel);

        int32_t osc1 =
            oscCZ(phase1, freq, pd, wave, noiseAmt);

        int32_t freq2 =
            applyDetune(freq, osc2Detune);

        int32_t osc2 =
            oscCZ(phase2, freq2, pd, wave, noiseAmt);

        int32_t osc2Raw = osc2;
        osc2 = osc2Raw;

        int32_t ringDrive = osc2Level;
        if (ringDrive < 2048)
            ringDrive = 2048;
        int32_t ringCarrier = (osc2Raw * ringDrive) >> 12;
        int32_t ringSig = clip((osc1 * ringCarrier) >> 10);
        int32_t ringMix = (ring * 3840) >> 12;
        osc1 = mix(osc1, ringSig, ringMix);

        int32_t ampScale = envelopeAmpScale(envelopeLevel);
        ampScale = (ampScale * pdCompensationScale(pd)) >> 12;
        ampScale = (ampScale * updateSyncFade()) >> 12;
        ampScale = (ampScale * updateLoopFade()) >> 12;
        osc1 = (osc1 * ampScale) >> 12;
        osc2 = (osc2 * ampScale) >> 12;

        outputFilteredAudio(osc1, osc2);
    }

    void outputFilteredAudio(int32_t left, int32_t right)
    {
        AudioOut1(filterOutputSample(left, outputFilterLeft));
        AudioOut2(filterOutputSample(right, outputFilterRight));
    }

    struct OutputFilterState
    {
        int32_t highpassPreviousInput = 0;
        int32_t highpassOutput = 0;
        int32_t lowpassOutput = 0;
    };

    int32_t filterOutputSample(int32_t input, OutputFilterState& state)
    {
        input = clip(input);

        int32_t highpass =
            (OutputHighpassAlphaQ12 *
             (state.highpassOutput + input - state.highpassPreviousInput)) >> 12;
        state.highpassPreviousInput = input;
        state.highpassOutput = highpass;

        state.lowpassOutput +=
            (OutputLowpassAlphaQ12 * (highpass - state.lowpassOutput)) >> 12;

        return clip(state.lowpassOutput);
    }

    inline int32_t oscCZ(
        uint32_t& phase,
        int32_t freq,
        int32_t pd,
        int32_t wave,
        int32_t noiseAmt)
    {
        phase += (uint32_t)freq;

        uint32_t renderPhase = phase;
        int32_t noisyPd = pd;

        if (noiseAmt > 0)
            applyCZNoise(renderPhase, noisyPd, noiseAmt);

        int32_t pdCurve = responseCurve(noisyPd);

        int32_t sine = getSine(renderPhase);
        int32_t target = morphWave(renderPhase, wave);
        target = softenHighPitchTarget(sine, target, freq, pdCurve);

        return mix(sine, target, pdCurve);
    }

    inline void applyCZNoise(uint32_t& phase, int32_t& pd, int32_t amount)
    {
        if (++noiseHoldCounter >= 512)
        {
            noiseHoldCounter = 0;
            heldPdNoise = ((int32_t)fastNoise() - 128);
            heldPhaseNoise = ((int32_t)fastNoise() - 128);
        }

        int32_t noiseCurve = (responseCurve(amount) * 11) / 20;
        int32_t pdJitter = (heldPdNoise * noiseCurve) >> 10;
        int32_t phaseJitter = heldPhaseNoise * (noiseCurve >> 6);

        pd = clamp12(pd + pdJitter);
        phase += (uint32_t)phaseJitter;
    }

    void triggerEnvelope(bool held = false, bool heldByPulse = false)
    {
        if (envelopePreset == (uint8_t)EnvelopePreset::Off)
            return;

        // Retrigger from the current envelope levels so repeated pulses do not
        // hard-step to zero and click.
        int32_t ampStartLevel = envelopeActive ? ampEnvelopeLevel : 0;
        int32_t pdStartLevel = envelopeActive ? pdEnvelopeLevel : 0;
        int32_t pitchStartLevel = envelopeActive ? pitchEnvelopeLevel : PitchEnvelopeCenter;

        ampEnvelopeStage = 0;
        ampEnvelopeSample = 0;
        ampEnvelopeStartLevel = ampStartLevel;
        ampEnvelopeLevel = ampStartLevel;
        pdEnvelopeStage = 0;
        pdEnvelopeSample = 0;
        pdEnvelopeStartLevel = pdStartLevel;
        pdEnvelopeLevel = pdStartLevel;
        pitchEnvelopeStage = 0;
        pitchEnvelopeSample = 0;
        pitchEnvelopeStartLevel = pitchStartLevel;
        pitchEnvelopeLevel = pitchStartLevel;
        envelopeActive = true;
        envelopeHeld = held;
        envelopeReleaseRequested = !held;
        pulse2EnvelopeHolding = heldByPulse;
    }

    void triggerTuringEnvelope()
    {
        if (turingPulse)
            triggerEnvelope();
    }

    int32_t updateEnvelope()
    {
        if (envelopePreset == (uint8_t)EnvelopePreset::Off || !envelopeActive)
            return 0;

        const EnvelopeProgram& program = envelopeProgram();
        bool loopEnabled = envelopeLoopEnabled(program);

        bool ampDone = updateEnvelopeRunner(
            program.amp,
            ampEnvelopeStage,
            ampEnvelopeSample,
            ampEnvelopeLevel,
            ampEnvelopeStartLevel,
            envelopeHeld,
            envelopeReleaseRequested,
            loopEnabled,
            true);

        bool pdDone = updateEnvelopeRunner(
            program.pd,
            pdEnvelopeStage,
            pdEnvelopeSample,
            pdEnvelopeLevel,
            pdEnvelopeStartLevel,
            envelopeHeld,
            envelopeReleaseRequested,
            loopEnabled,
            false);

        bool pitchDone = updateEnvelopeRunner(
            program.pitch,
            pitchEnvelopeStage,
            pitchEnvelopeSample,
            pitchEnvelopeLevel,
            pitchEnvelopeStartLevel,
            envelopeHeld,
            envelopeReleaseRequested,
            loopEnabled,
            false);

        if (ampDone && pdDone && pitchDone)
        {
            envelopeActive = false;
            envelopeHeld = false;
            envelopeReleaseRequested = false;
            pulse2EnvelopeHolding = false;
            pitchEnvelopeLevel = PitchEnvelopeCenter;
            if (midiNoteReleased)
                midiNoteActive = false;
        }

        return clamp12(ampEnvelopeLevel);
    }

    int32_t applyEnvelopeToPd(int32_t pd, int32_t level)
    {
        (void)level;
        return clamp12(pd + pdEnvelopeLevel);
    }

    int32_t applyPitchEnvelopeToFrequency(int32_t freq)
    {
        int32_t semitone =
            ((pitchEnvelopeLevel - (int32_t)PitchEnvelopeCenter) *
             PitchEnvelopeSemitoneRange) / (int32_t)PitchEnvelopeCenter;

        if (semitone < -PitchEnvelopeSemitoneRange)
            semitone = -PitchEnvelopeSemitoneRange;
        if (semitone > PitchEnvelopeSemitoneRange)
            semitone = PitchEnvelopeSemitoneRange;

        uint16_t ratio = PitchEnvelopeRatioQ12[semitone + PitchEnvelopeSemitoneRange];
        return (int32_t)(((int64_t)freq * ratio) >> 12);
    }

    int32_t envelopeAmpScale(int32_t level)
    {
        if (envelopePreset == (uint8_t)EnvelopePreset::Off)
            return 4095;

        return clamp12(level);
    }

    void processMidiVoiceByte(uint8_t byte)
    {
        if (byte >= 0xF8u)
            return;

        if (byte & 0x80u)
        {
            midiRunningStatus = byte;
            midiDataCount = 0;
            return;
        }

        uint8_t type = midiRunningStatus & 0xF0u;
        if (type != 0x80u && type != 0x90u && type != 0xB0u && type != 0xE0u)
            return;

        midiData[midiDataCount++] = byte & 0x7Fu;
        if (midiDataCount < 2u)
            return;

        midiDataCount = 0;
        uint8_t channel = midiRunningStatus & 0x0Fu;
        if (channel != midiInChannel)
            return;

        if (type == 0x90u && midiData[1] > 0)
        {
            pendingMidiNote = midiData[0];
            pendingMidiVelocity = midiData[1];
            pendingMidiNoteOn = true;
            return;
        }

        if (type == 0x80u || (type == 0x90u && midiData[1] == 0))
        {
            if (midiData[0] == midiNote)
            {
                if (envelopePreset == (uint8_t)EnvelopePreset::Off || !envelopeActive)
                    midiNoteActive = false;
                else
                {
                    midiNoteReleased = true;
                    requestEnvelopeRelease();
                }
            }
            return;
        }

        if (type == 0xB0u)
        {
            if (midiData[0] == MidiCcPdAmount)
            {
                pdControl = midiCcToControl(midiData[1]);
                midiResetSynthXPickup = true;
            }
            else if (midiData[0] == MidiCcWaveAmount)
            {
                waveControl = midiCcToControl(midiData[1]);
                midiResetSynthYPickup = true;
            }
            else if (midiData[0] == MidiCcTuringCvOctaveRange)
            {
                turingCvOctaveRange =
                    1u + (uint8_t)(((uint32_t)midiData[1] * 7u) / 127u);
            }
            else if (midiData[0] == MidiCcDetune)
            {
                setDetuneFromControl(midiCcToControl(midiData[1]));
                midiResetAltMainPickup = true;
            }
            else if (midiData[0] == MidiCcRingAmount)
            {
                osc2Ring = midiCcToControl(midiData[1]);
                midiResetAltXPickup = true;
            }
            else if (midiData[0] == MidiCcNoiseAmount)
            {
                osc2Noise = midiCcToControl(midiData[1]);
                midiResetAltYPickup = true;
            }
            return;
        }

        if (type == 0xE0u)
        {
            int32_t bend = ((int32_t)midiData[1] << 7) | midiData[0];
            midiPitchBend = bend - 8192;
        }
    }

    int32_t midiCcToControl(uint8_t value)
    {
        return ((int32_t)value * 4095) / 127;
    }

    void applyPendingMidiNote()
    {
        if (!pendingMidiNoteOn)
            return;

        pendingMidiNoteOn = false;
        midiNote = pendingMidiNote;
        midiVelocity = pendingMidiVelocity;
        midiNoteActive = true;
        midiNoteReleased = false;

        if (envelopePreset != (uint8_t)EnvelopePreset::Off)
        {
            if (!envelopeActive)
                syncOscillators();
            triggerEnvelope(true);
        }
    }

    int32_t currentPitchUnits(int32_t knob, int32_t pitchInput)
    {
        if (!midiNoteActive)
            return pitchUnits(knob, pitchInput);

        int32_t units = midiNotePitchUnits(midiNote);
        units += (midiPitchBend * PitchUnitsPerOctave) / (8192 * 6);
        units += (pitchInput * PitchUnitsPerOctave) / PitchInputCountsPerVolt;
        return units;
    }

    int32_t midiNotePitchUnits(uint8_t note)
    {
        return ((int32_t)note - 36) * PitchUnitsPerOctave / 12;
    }

    void handleWebMidiSysex()
    {
        if (!webMidiHeaderMatches())
            return;

        uint8_t command = sysexBuffer[5];

        if (command == WebMidiCommandSettings ||
            command == WebMidiCommandSaveSettings)
        {
            handleWebMidiSettings(command == WebMidiCommandSaveSettings);
            return;
        }

        if (command == WebMidiCommandPreview ||
            command == WebMidiCommandSaveEnvelope)
        {
            handleWebMidiEnvelope(command == WebMidiCommandSaveEnvelope);
            return;
        }

        if (command == WebMidiCommandDeleteEnvelope)
        {
            handleWebMidiDeleteEnvelope();
            return;
        }

        if (command == WebMidiCommandRequestSettings)
        {
            handleWebMidiRequestSettings();
            return;
        }

        if (command == WebMidiCommandRequestEnvelopeSlots)
        {
            handleWebMidiRequestEnvelopeSlots();
            return;
        }

        if (command == WebMidiCommandRequestEnvelope)
        {
            handleWebMidiRequestEnvelope();
            return;
        }

        if (command == WebMidiCommandRequestPitchEnvelope)
            handleWebMidiRequestPitchEnvelope();
    }

    bool webMidiHeaderMatches()
    {
        if (sysexLength < 6u)
            return false;

        if (sysexBuffer[0] != WebMidiManufacturer)
            return false;

        for (uint32_t i = 0; i < 4u; ++i)
        {
            if (sysexBuffer[1u + i] != WebMidiId[i])
                return false;
        }

        return true;
    }

    void handleWebMidiSettings(bool persist)
    {
        if (sysexLength != WebMidiSettingsPayloadLength + 6u &&
            sysexLength != WebMidiStableSettingsPayloadLength + 6u &&
            sysexLength != WebMidiRangeSettingsPayloadLength + 6u &&
            sysexLength != WebMidiLegacySettingsPayloadLength + 6u)
            return;

        uint32_t offset = 6;
        int32_t ring = decodeWebMidiUint14(offset);
        int32_t noise = decodeWebMidiUint14(offset);
        int32_t pd = pdControl;
        int32_t detune = osc2Detune + 2048;
        int32_t wave = waveControl;
        if (sysexLength == WebMidiSettingsPayloadLength + 6u)
        {
            pd = decodeWebMidiUint14(offset);
            detune = decodeWebMidiUint14(offset);
            wave = decodeWebMidiUint14(offset);
        }
        uint8_t channel = sysexBuffer[offset] & 0x0Fu;
        offset++;

        osc2Ring = ring;
        osc2Noise = noise;
        pdControl = clamp12(pd);
        setDetuneFromControl(detune);
        waveControl = clamp12(wave);
        midiInChannel = channel;

        // Remote settings hold until each physical control reaches its new value.
        midiResetAltXPickup = true;
        midiResetAltYPickup = true;
        if (sysexLength == WebMidiSettingsPayloadLength + 6u)
        {
            midiResetSynthXPickup = true;
            midiResetSynthYPickup = true;
            midiResetAltMainPickup = true;
        }

        if (sysexLength >= WebMidiRangeSettingsPayloadLength + 6u)
        {
            turingCvOctaveRange = clampTuringCvOctaveRange(sysexBuffer[offset]);
            offset++;
        }

        if (sysexLength == WebMidiSettingsPayloadLength + 6u)
        {
            turingMidiOutputEnabled = (sysexBuffer[offset] & 0x01u) != 0;
            offset++;
            turingMidiOutputChannel = sysexBuffer[offset] & 0x0Fu;
        }

        if (persist)
            savePerformanceStateIfChanged();
    }

    void handleWebMidiRequestSettings()
    {
        if (sysexLength != 6u)
            return;

        uint8_t frame[22] = {
            0xF0u,
            WebMidiManufacturer,
            WebMidiId[0],
            WebMidiId[1],
            WebMidiId[2],
            WebMidiId[3],
            WebMidiCommandSettingsResponse
        };
        uint32_t offset = 7;
        appendWebMidiUint14(frame, offset, clamp12(osc2Ring));
        appendWebMidiUint14(frame, offset, clamp12(osc2Noise));
        appendWebMidiUint14(frame, offset, clamp12(pdControl));
        appendWebMidiUint14(frame, offset, clamp12(osc2Detune + 2048));
        appendWebMidiUint14(frame, offset, clamp12(waveControl));
        frame[offset++] = midiInChannel & 0x0Fu;
        frame[offset++] = clampTuringCvOctaveRange(turingCvOctaveRange);
        frame[offset++] = turingMidiOutputEnabled ? 1u : 0u;
        frame[offset++] = turingMidiOutputChannel & 0x0Fu;
        frame[offset++] = 0xF7u;

        tud_midi_stream_write(0, frame, offset);
    }

    void handleWebMidiRequestEnvelopeSlots()
    {
        if (sysexLength != 6u)
            return;

        uint8_t frame[11] = {
            0xF0u,
            WebMidiManufacturer,
            WebMidiId[0],
            WebMidiId[1],
            WebMidiId[2],
            WebMidiId[3],
            WebMidiCommandEnvelopeSlotsResponse,
            WebMidiEnvelopeProtocolVersion
        };
        uint32_t offset = 8;
        appendWebMidiUint14(frame, offset, customEnvelopeMask());
        frame[offset++] = 0xF7u;
        tud_midi_stream_write(0, frame, offset);
    }

    void handleWebMidiRequestEnvelope()
    {
        if (sysexLength != 7u)
            return;

        uint8_t slot = sysexBuffer[6] & 0x07u;
        if (!customEnvelopeLoaded[slot])
            return;

        uint8_t frame[89] = {
            0xF0u,
            WebMidiManufacturer,
            WebMidiId[0],
            WebMidiId[1],
            WebMidiId[2],
            WebMidiId[3],
            WebMidiCommandEnvelopeResponse,
            slot
        };
        uint32_t offset = 8;
        const EnvelopeProgram& envelope = customEnvelopes[slot];
        for (uint32_t i = 0; i < 8u; ++i)
        {
            appendWebMidiUint14(frame, offset, envelope.amp[i].level);
            appendWebMidiUint21(frame, offset, envelope.amp[i].time);
        }
        for (uint32_t i = 0; i < 8u; ++i)
        {
            appendWebMidiUint14(frame, offset, envelope.pd[i].level);
            appendWebMidiUint21(frame, offset, envelope.pd[i].time);
        }
        frame[offset++] = 0xF7u;
        tud_midi_stream_write(0, frame, offset);
        sendWebMidiPitchEnvelopeResponse(slot);
    }

    void handleWebMidiRequestPitchEnvelope()
    {
        if (sysexLength != 7u)
            return;

        uint8_t slot = sysexBuffer[6] & 0x07u;
        if (!customEnvelopeLoaded[slot])
            return;

        sendWebMidiPitchEnvelopeResponse(slot);
    }

    void sendWebMidiPitchEnvelopeResponse(uint8_t slot)
    {
        uint8_t frame[49] = {
            0xF0u,
            WebMidiManufacturer,
            WebMidiId[0],
            WebMidiId[1],
            WebMidiId[2],
            WebMidiId[3],
            WebMidiCommandPitchEnvelopeResponse,
            slot
        };
        uint32_t offset = 8;
        const EnvelopeProgram& envelope = customEnvelopes[slot];
        for (uint32_t i = 0; i < 8u; ++i)
        {
            appendWebMidiUint14(frame, offset, envelope.pitch[i].level);
            appendWebMidiUint21(frame, offset, envelope.pitch[i].time);
        }
        frame[offset++] = 0xF7u;
        tud_midi_stream_write(0, frame, offset);
    }

    void handleWebMidiEnvelope(bool persist)
    {
        bool legacyPayload = sysexLength == WebMidiLegacyEnvelopePayloadLength + 6u;
        if (!legacyPayload && sysexLength != WebMidiEnvelopePayloadLength + 6u)
            return;

        uint32_t offset = 6;
        uint8_t slot = sysexBuffer[offset++] & 0x07u;
        offset += 16; // Names stay in the browser; firmware stores shape only.

        EnvelopeProgram next = {};
        uint32_t ampTotal = 0;
        uint16_t ampMax = 0;

        for (uint32_t i = 0; i < 8u; ++i)
        {
            uint16_t level = decodeWebMidiUint14(offset);
            uint32_t time = decodeWebMidiUint21(offset);
            next.amp[i] = {level, time};
            ampTotal += time;
            if (level > ampMax)
                ampMax = level;
        }

        for (uint32_t i = 0; i < 8u; ++i)
        {
            uint16_t level = decodeWebMidiUint14(offset);
            uint32_t time = decodeWebMidiUint21(offset);
            next.pd[i] = {level, time};
        }

        if (legacyPayload)
        {
            for (uint32_t i = 0; i < 8u; ++i)
                next.pitch[i] = {PitchEnvelopeCenter, 1u};
        }
        else
        {
            for (uint32_t i = 0; i < 8u; ++i)
            {
                uint16_t level = decodeWebMidiUint14(offset);
                uint32_t time = decodeWebMidiUint21(offset);
                next.pitch[i] = {level, time};
            }
        }

        if (ampMax == 0 || ampTotal < MinWebMidiEnvelopeSamples)
            return;

        customEnvelopes[slot] = next;
        customEnvelopeLoaded[slot] = true;
        envelopePreset = CustomEnvelopePreset + slot;
        envelopeActive = false;

        if (persist)
            saveCustomEnvelopeState();
    }

    void handleWebMidiDeleteEnvelope()
    {
        if (sysexLength != WebMidiDeleteEnvelopePayloadLength + 6u)
            return;

        uint8_t slot = sysexBuffer[6] & 0x07u;
        customEnvelopes[slot] = {};
        customEnvelopeLoaded[slot] = false;

        if (envelopePreset == CustomEnvelopePreset + slot)
        {
            envelopePreset = (uint8_t)EnvelopePreset::Off;
            envelopeActive = false;
        }

        saveCustomEnvelopeState();
    }

    int32_t decodeWebMidiUint14(uint32_t& offset)
    {
        int32_t value =
            (int32_t)(sysexBuffer[offset] & 0x7Fu) |
            ((int32_t)(sysexBuffer[offset + 1] & 0x7Fu) << 7);
        offset += 2;
        return clamp12(value);
    }

    void appendWebMidiUint14(uint8_t* buffer, uint32_t& offset, int32_t value)
    {
        uint32_t packed = (uint32_t)clamp12(value);
        buffer[offset++] = packed & 0x7Fu;
        buffer[offset++] = (packed >> 7) & 0x7Fu;
    }

    void appendWebMidiUint21(uint8_t* buffer, uint32_t& offset, uint32_t value)
    {
        uint32_t packed = value;
        if (packed < 1u)
            packed = 1u;
        if (packed > MaxWebMidiStageSamples)
            packed = MaxWebMidiStageSamples;
        buffer[offset++] = packed & 0x7Fu;
        buffer[offset++] = (packed >> 7) & 0x7Fu;
        buffer[offset++] = (packed >> 14) & 0x7Fu;
    }

    uint32_t decodeWebMidiUint21(uint32_t& offset)
    {
        uint32_t value =
            (uint32_t)(sysexBuffer[offset] & 0x7Fu) |
            ((uint32_t)(sysexBuffer[offset + 1] & 0x7Fu) << 7) |
            ((uint32_t)(sysexBuffer[offset + 2] & 0x7Fu) << 14);
        offset += 3;

        if (value < 1u)
            return 1u;
        if (value > MaxWebMidiStageSamples)
            return MaxWebMidiStageSamples;
        return value;
    }

    const EnvelopeProgram& envelopeProgram()
    {
        static const EnvelopeProgram pluck = {{
            {4095, 480}, {0, 12000}, {0, 1}, {0, 1},
            {0, 1}, {0, 1}, {0, 1}, {0, 1}
        }, {
            {1024, 480}, {0, 12000}, {0, 1}, {0, 1},
            {0, 1}, {0, 1}, {0, 1}, {0, 1}
        }, {
            {PitchEnvelopeCenter, 1}, {PitchEnvelopeCenter, 1},
            {PitchEnvelopeCenter, 1}, {PitchEnvelopeCenter, 1},
            {PitchEnvelopeCenter, 1}, {PitchEnvelopeCenter, 1},
            {PitchEnvelopeCenter, 1}, {PitchEnvelopeCenter, 1}
        }};

        static const EnvelopeProgram doublePluck = {{
            {4095, 180}, {700, 4800}, {0, 2400}, {3600, 180},
            {900, 6000}, {350, 6000}, {120, 6000}, {0, 12000}
        }, {
            {1600, 180}, {500, 4800}, {0, 2400}, {2200, 180},
            {700, 6000}, {300, 6000}, {120, 6000}, {0, 12000}
        }, {
            {PitchEnvelopeCenter, 1}, {PitchEnvelopeCenter, 1},
            {PitchEnvelopeCenter, 1}, {PitchEnvelopeCenter, 1},
            {PitchEnvelopeCenter, 1}, {PitchEnvelopeCenter, 1},
            {PitchEnvelopeCenter, 1}, {PitchEnvelopeCenter, 1}
        }};

        static const EnvelopeProgram bounce = {{
            {4095, 120}, {1200, 3600}, {3300, 3600}, {1700, 4800},
            {2600, 4800}, {900, 7200}, {1600, 7200}, {0, 12000}
        }, {
            {2500, 120}, {800, 3600}, {2200, 3600}, {700, 4800},
            {1600, 4800}, {500, 7200}, {1200, 7200}, {0, 12000}
        }, {
            {PitchEnvelopeCenter, 1}, {PitchEnvelopeCenter, 1},
            {PitchEnvelopeCenter, 1}, {PitchEnvelopeCenter, 1},
            {PitchEnvelopeCenter, 1}, {PitchEnvelopeCenter, 1},
            {PitchEnvelopeCenter, 1}, {PitchEnvelopeCenter, 1}
        }};

        static const EnvelopeProgram bell = {{
            {4095, 240}, {2600, 12000}, {1200, 24000}, {0, 36000},
            {0, 1}, {0, 1}, {0, 1}, {0, 1}
        }, {
            {2048, 240}, {1600, 6000}, {700, 24000}, {0, 42000},
            {0, 1}, {0, 1}, {0, 1}, {0, 1}
        }, {
            {PitchEnvelopeCenter, 1}, {PitchEnvelopeCenter, 1},
            {PitchEnvelopeCenter, 1}, {PitchEnvelopeCenter, 1},
            {PitchEnvelopeCenter, 1}, {PitchEnvelopeCenter, 1},
            {PitchEnvelopeCenter, 1}, {PitchEnvelopeCenter, 1}
        }};

        static const EnvelopeProgram brass = {{
            {4095, 4800}, {3400, 30000}, {3200, 18000}, {3000, 18000},
            {3300, 18000}, {3100, 18000}, {1600, 18000}, {0, 24000}
        }, {
            {1792, 4800}, {900, 30000}, {1200, 18000}, {1000, 18000},
            {1300, 18000}, {900, 18000}, {500, 18000}, {0, 24000}
        }, {
            {PitchEnvelopeCenter, 1}, {PitchEnvelopeCenter, 1},
            {PitchEnvelopeCenter, 1}, {PitchEnvelopeCenter, 1},
            {PitchEnvelopeCenter, 1}, {PitchEnvelopeCenter, 1},
            {PitchEnvelopeCenter, 1}, {PitchEnvelopeCenter, 1}
        }};

        static const EnvelopeProgram strings = {{
            {2200, 24000}, {3600, 24000}, {3800, 48000}, {3600, 48000},
            {3400, 48000}, {3000, 48000}, {1800, 48000}, {0, 96000}
        }, {
            {400, 12000}, {900, 36000}, {1200, 36000}, {900, 48000},
            {700, 48000}, {500, 48000}, {400, 48000}, {300, 96000}
        }, {
            {PitchEnvelopeCenter, 1}, {PitchEnvelopeCenter, 1},
            {PitchEnvelopeCenter, 1}, {PitchEnvelopeCenter, 1},
            {PitchEnvelopeCenter, 1}, {PitchEnvelopeCenter, 1},
            {PitchEnvelopeCenter, 1}, {PitchEnvelopeCenter, 1}
        }};

        static const EnvelopeProgram reverseSwell = {{
            {200, 12000}, {900, 18000}, {1800, 18000}, {3000, 18000},
            {4095, 12000}, {2600, 2400}, {900, 2400}, {0, 4800}
        }, {
            {200, 12000}, {500, 18000}, {1000, 18000}, {1800, 18000},
            {2600, 12000}, {1300, 2400}, {500, 2400}, {0, 4800}
        }, {
            {PitchEnvelopeCenter, 1}, {PitchEnvelopeCenter, 1},
            {PitchEnvelopeCenter, 1}, {PitchEnvelopeCenter, 1},
            {PitchEnvelopeCenter, 1}, {PitchEnvelopeCenter, 1},
            {PitchEnvelopeCenter, 1}, {PitchEnvelopeCenter, 1}
        }};

        static const EnvelopeProgram evolvingDigital = {{
            {4095, 480}, {3900, 12000}, {3800, 12000}, {3600, 12000},
            {3200, 18000}, {2600, 18000}, {1600, 24000}, {0, 36000}
        }, {
            {3000, 480}, {800, 12000}, {3600, 12000}, {1200, 12000},
            {2600, 18000}, {600, 18000}, {1800, 24000}, {0, 36000}
        }, {
            {PitchEnvelopeCenter, 1}, {PitchEnvelopeCenter, 1},
            {PitchEnvelopeCenter, 1}, {PitchEnvelopeCenter, 1},
            {PitchEnvelopeCenter, 1}, {PitchEnvelopeCenter, 1},
            {PitchEnvelopeCenter, 1}, {PitchEnvelopeCenter, 1}
        }};

        static const EnvelopeProgram off = {{
            {0, 1}, {0, 1}, {0, 1}, {0, 1},
            {0, 1}, {0, 1}, {0, 1}, {0, 1}
        }, {
            {0, 1}, {0, 1}, {0, 1}, {0, 1},
            {0, 1}, {0, 1}, {0, 1}, {0, 1}
        }, {
            {PitchEnvelopeCenter, 1}, {PitchEnvelopeCenter, 1},
            {PitchEnvelopeCenter, 1}, {PitchEnvelopeCenter, 1},
            {PitchEnvelopeCenter, 1}, {PitchEnvelopeCenter, 1},
            {PitchEnvelopeCenter, 1}, {PitchEnvelopeCenter, 1}
        }};

        if (envelopePreset >= CustomEnvelopePreset)
        {
            uint8_t slot = envelopePreset - CustomEnvelopePreset;
            if (slot < CustomEnvelopeSlotCount && customEnvelopeLoaded[slot])
                return customEnvelopes[slot];
        }

        switch ((EnvelopePreset)envelopePreset)
        {
            case EnvelopePreset::Pluck: return pluck;
            case EnvelopePreset::DoublePluck: return doublePluck;
            case EnvelopePreset::Bounce: return bounce;
            case EnvelopePreset::Bell: return bell;
            case EnvelopePreset::Brass: return brass;
            case EnvelopePreset::Strings: return strings;
            case EnvelopePreset::ReverseSwell: return reverseSwell;
            case EnvelopePreset::EvolvingDigital: return evolvingDigital;
            case EnvelopePreset::Off:
            default:
                return off;
        }
    }

    bool updateEnvelopeRunner(
        const EnvelopeStage* stages,
        uint8_t& stage,
        uint32_t& sample,
        int32_t& level,
        int32_t& startLevel,
        bool held,
        bool releaseRequested,
        bool loopEnabled,
        bool deClickOnLoop)
    {
        if (stage >= 8)
            return true;

        if (loopEnabled && stage > EnvelopeLoopEndStage && held && !releaseRequested)
        {
            stage = EnvelopeLoopStartStage;
            if (deClickOnLoop)
                loopFadeSamples = LoopDeClickSamples;
        }

        uint32_t time = stages[stage].time;
        if (time == 0)
            time = 1;

        sample++;
        int32_t target = stages[stage].level;
        level = startLevel + (((target - startLevel) * (int32_t)sample) / (int32_t)time);

        if (sample >= time)
        {
            level = target;
            stage++;
            sample = 0;
            startLevel = level;

            if (loopEnabled && stage > EnvelopeLoopEndStage && held && !releaseRequested)
            {
                stage = EnvelopeLoopStartStage;
                if (deClickOnLoop)
                    loopFadeSamples = LoopDeClickSamples;
            }
        }

        return stage >= 8;
    }

    bool envelopeLoopEnabled(const EnvelopeProgram& program)
    {
        return laneHasLoopBody(program.amp) || laneHasLoopBody(program.pd);
    }

    bool laneHasLoopBody(const EnvelopeStage* stages)
    {
        for (uint32_t i = EnvelopeLoopStartStage; i <= EnvelopeLoopEndStage; ++i)
        {
            if (stages[i].level >= EnvelopeLoopLevelThreshold &&
                stages[i].time >= EnvelopeLoopMinStageSamples)
                return true;
        }

        return false;
    }

    void requestEnvelopeRelease()
    {
        envelopeHeld = false;
        envelopeReleaseRequested = true;
        pulse2EnvelopeHolding = false;
    }

    inline int32_t morphWave(uint32_t phase, int32_t wave)
    {
        uint32_t scaled = ((uint32_t)wave * 7u);
        uint32_t index = scaled >> 12;
        uint32_t frac = compressWaveTransition(smoothStep12(scaled & 4095));

        int32_t a = czWave(phase, index);
        int32_t b = czWave(phase, index < 7 ? index + 1 : 7);

        return a + (((b - a) * (int32_t)frac) >> 12);
    }

    uint32_t compressWaveTransition(uint32_t frac)
    {
        if (frac <= 1024u)
            return 0u;
        if (frac >= 3072u)
            return 4095u;

        uint32_t span = frac - 1024u;
        return ((span * 4095u) / 2048u);
    }

    inline int32_t czWave(uint32_t phase, uint32_t wave)
    {
        uint32_t p = (phase >> 20) & 4095;
        return pdWaveLUT[wave & 7u][p];
    }

    // =========================================================
    // TURING MACHINE
    // =========================================================
    void updateTuringClockState(bool triggerAudioEnvelope)
    {
        turingLength = 2 + ((turingLengthControl * 14) >> 12);
        if (turingLength > 16) turingLength = 16;

        bool externalClock = PulseIn1RisingEdge();
        bool clocked = false;

        if (externalClock)
        {
            externalClockAge = 0;
            clocked = true;
        }
        else
        {
            if (externalClockAge < 96000u)
                externalClockAge++;
            else
                clocked = internalTuringClock(turingClockControl);
        }

        if (clocked)
        {
            stepTuring(turingMutationControl);
            if (triggerAudioEnvelope)
                triggerTuringEnvelope();
        }

        updateTuringPulseAge();
    }

    void stepTuring(int32_t knob)
    {
        uint32_t mask = (1u << turingLength) - 1u;
        uint8_t chance = turingMutationChance(knob);

        bool bit = turing & 1;
        if (fastNoise() < chance) bit ^= 1;

        turing = ((turing >> 1) | ((uint32_t)bit << (turingLength - 1))) & mask;

        turingPulse = bit;
        turingAltPulse = (turing >> 1) & 1u;
        turingPulseAge = 0;
        turingClockLedAge = 0;

        int32_t unipolar = (int32_t)((turing * 4095u) / mask);
        int32_t scale =
            (int32_t)turingCvOctaveRange * PitchInputCountsPerVolt;
        turingCv = clip((unipolar * scale) / 4095);
        turingModCv = smooth(turingCv);
        queueTuringMidiNote();
    }

    void queueTuringMidiNote()
    {
        int32_t note = 36 + ((turingCv * 12) / PitchInputCountsPerVolt);
        if (note < 0)
            note = 0;
        else if (note > 127)
            note = 127;

        pendingTuringMidiNote = (uint8_t)note;
        pendingTuringMidiNoteOn = true;
    }

    uint8_t turingMutationChance(int32_t knob)
    {
        int32_t distance = knob - 2048;
        if (distance < 0) distance = -distance;

        int32_t chance = 255 - (distance >> 3);
        if (chance < 0) chance = 0;
        if (chance > 255) chance = 255;

        return (uint8_t)chance;
    }

    bool internalTuringClock(int32_t speed)
    {
        uint32_t inverseSpeed = 4095u - (uint32_t)clamp12(speed);
        uint32_t period =
            3000u +
            (((inverseSpeed * inverseSpeed) >> 12) * 57000u >> 12);
        if (period < 3000u) period = 3000u;
        turingClockPeriod = period;

        if (++turingClock >= turingClockPeriod)
        {
            turingClock = 0;
            return true;
        }

        return false;
    }

    void updateTuringPulseAge()
    {
        if (turingClockLedAge < TuringClockLedSamples)
            turingClockLedAge++;

        if ((turingPulse || turingAltPulse) && ++turingPulseAge > 1200)
        {
            turingPulse = false;
            turingAltPulse = false;
            pendingTuringMidiNoteOff = true;
        }
    }

    int32_t smooth(int32_t x)
    {
        turingSmooth = (turingSmooth * 15 + x) >> 4;
        return turingSmooth;
    }

    void updateTuringLEDs()
    {
        LedBrightness(0, turing & 1 ? 4095 : 0);
        LedBrightness(1, turing & 2 ? 4095 : 0);
        LedBrightness(2, turing & 4 ? 4095 : 0);
        LedBrightness(3, PulseIn1());
        LedBrightness(4, (turingLength * 4095) >> 4);
        LedBrightness(5, turingClockLedAge < TuringClockLedSamples ? 4095 : 0);
    }

    void updateSynthLEDs(bool alt, int32_t pd, int32_t wave)
    {
        LedBrightness(0, pd);
        LedBrightness(1, wave);
        LedBrightness(2, osc2Level);
        LedBrightness(3, osc2Ring);
        LedBrightness(4, osc2Noise);
        LedBrightness(5, alt ? 4095 : 0);
    }

    void updateStartupEnvelopeSelect(Switch mode)
    {
        if (startupSelectChecked)
            return;

        if (startupSelectSamples < StartupSelectDelaySamples + StartupSelectWindowSamples)
        {
            startupSelectSamples++;
            if (startupSelectSamples >= StartupSelectDelaySamples &&
                mode == Switch::Down)
            {
                envelopeSelectMode = true;
                envelopeSelectReady = false;
            }
        }
        else
        {
            startupSelectChecked = true;
        }
    }

    void updateEnvelopeSelectMode(
        int32_t main,
        int32_t x,
        int32_t y,
        Switch mode,
        Switch previousMode)
    {
        AudioOut1(0);
        AudioOut2(0);
        CVOut1(0);
        CVOut2(0);
        PulseOut1(false);
        PulseOut2(false);

        uint8_t selectedIndex =
            (uint8_t)(((uint32_t)clamp12(main) * selectableEnvelopeCount()) >> 12);
        envelopePreset = envelopePresetForSelection(selectedIndex);
        showEnvelopePresetLeds(envelopePreset);

        bool down = mode == Switch::Down;

        if (!envelopeSelectReady)
        {
            if (!down)
                envelopeSelectReady = true;

            return;
        }

        if (!down)
        {
            if (envelopeSelectHoldActive)
            {
                envelopeSelectMode = false;
                resetSavedSettingPickup(main, x, y);
                resetEnvelopeSelectHold();
            }

            return;
        }

        if (!envelopeSelectHoldActive || previousMode != Switch::Down)
        {
            envelopeSelectHoldActive = true;
            envelopeSelectHoldSamples = 0;
            envelopeSelectSaved = false;
        }

        if (envelopeSelectHoldSamples < SaveHoldSamples)
            envelopeSelectHoldSamples++;

        if (envelopeSelectHoldSamples >= SaveHoldSamples && !envelopeSelectSaved)
        {
            savePerformanceStateIfChanged();
            envelopeSelectSaved = true;
            saveConfirmSamples = SaveConfirmSamples;
        }

        updateSaveFeedback();
    }

    void showEnvelopePresetLeds(uint8_t preset)
    {
        if (preset >= CustomEnvelopePreset)
        {
            uint8_t slot = (preset - CustomEnvelopePreset) + 1u;
            LedBrightness(0, slot & 1u ? 4095 : 0);
            LedBrightness(1, slot & 2u ? 4095 : 0);
            LedBrightness(2, slot & 4u ? 4095 : 0);
            LedBrightness(3, 0);
            LedBrightness(4, 0);
            LedBrightness(5, 4095);
            return;
        }

        for (uint32_t i = 0; i < 6; ++i)
            LedBrightness(i, (preset & (1u << i)) ? 4095 : 0);
    }

    uint8_t selectableEnvelopeCount()
    {
        return EnvelopePresetCount + loadedCustomEnvelopeCount();
    }

    uint8_t loadedCustomEnvelopeCount()
    {
        uint8_t count = 0;
        for (uint32_t i = 0; i < CustomEnvelopeSlotCount; ++i)
        {
            if (customEnvelopeLoaded[i])
                count++;
        }

        return count;
    }

    uint8_t envelopePresetForSelection(uint8_t selected)
    {
        uint8_t count = selectableEnvelopeCount();
        if (count == 0)
            return (uint8_t)EnvelopePreset::Off;

        if (selected >= count)
            selected = count - 1;

        if (selected < EnvelopePresetCount)
            return selected;

        uint8_t customIndex = selected - EnvelopePresetCount;
        for (uint32_t slot = 0; slot < CustomEnvelopeSlotCount; ++slot)
        {
            if (!customEnvelopeLoaded[slot])
                continue;

            if (customIndex == 0)
                return CustomEnvelopePreset + slot;

            customIndex--;
        }

        return (uint8_t)EnvelopePreset::Off;
    }

    void resetEnvelopeSelectHold()
    {
        envelopeSelectHoldActive = false;
        envelopeSelectHoldSamples = 0;
        envelopeSelectSaved = false;
    }

    void updateSaveGesture(bool alt)
    {
        if (!alt)
        {
            resetSaveGesture();
            updateSaveFeedback();
            return;
        }

        if (saveHoldCanSave)
        {
            if (saveHoldSamples < SaveHoldSamples)
                saveHoldSamples++;

            if (saveHoldSamples >= SaveHoldSamples && !saveCompletedThisHold)
            {
                savePerformanceStateIfChanged();
                saveCompletedThisHold = true;
                saveConfirmSamples = SaveConfirmSamples;
            }
        }

        updateSaveFeedback();
    }

    void updateSaveFeedback()
    {
        if (saveConfirmSamples == 0)
            return;

        saveConfirmSamples--;

        bool on = (saveConfirmSamples & 4096u) != 0;
        for (uint32_t i = 0; i < 6; ++i)
            LedBrightness(i, on ? 4095 : 0);
    }

    void resetSaveGesture()
    {
        saveHoldSamples = 0;
        saveCompletedThisHold = false;
        saveHoldCanSave = false;
    }

    // =========================================================
    // UTILS
    // =========================================================

    int32_t mix(int32_t a, int32_t b, int32_t amt)
    {
        return a + ((b - a) * amt >> 12);
    }

    int32_t clip(int32_t x)
    {
        if (x > 2047) return 2047;
        if (x < -2048) return -2048;
        return x;
    }

    int32_t clamp12(int32_t x)
    {
        if (x < 0) return 0;
        if (x > 4095) return 4095;
        return x;
    }

    int32_t clampTuringTone(int32_t x)
    {
        if (x < TuringToneMin) return TuringToneMin;
        if (x > TuringToneMax) return TuringToneMax;
        return x;
    }

    int32_t turingCvToneOffset(int32_t cv)
    {
        if (cv < -TuringCvInputLimit)
            cv = -TuringCvInputLimit;
        else if (cv > TuringCvInputLimit)
            cv = TuringCvInputLimit;

        return cv << 1;
    }

    void updateAltControls(int32_t main, int32_t x, int32_t y)
    {
        if (altMainPickedUp ||
            pickupAltControl(main, altMainEntry, clamp12(osc2Detune + 2048), altMainPickedUp))
        {
            setDetuneFromControl(main);
        }

        if (altXPickedUp ||
            pickupAltControl(x, altXEntry, osc2Ring, altXPickedUp))
            osc2Ring = x;

        if (altYPickedUp ||
            pickupAltControl(y, altYEntry, osc2Noise, altYPickedUp))
            osc2Noise = y;
    }

    void updateSynthModeControls(int32_t main, int32_t x, int32_t y)
    {
        if (synthMainPickedUp ||
            pickupModeControl(main, synthMainEntry, pitchControl, synthMainPickedUp))
            pitchControl = main;

        if (synthXPickedUp ||
            pickupModeControl(x, synthXEntry, pdControl, synthXPickedUp))
            pdControl = x;

        if (synthYPickedUp ||
            pickupModeControl(y, synthYEntry, waveControl, synthYPickedUp))
            waveControl = y;
    }

    void applyMidiControlPickupResets(int32_t main, int32_t x, int32_t y)
    {
        if (midiResetSynthXPickup)
        {
            synthXPickedUp = false;
            synthXEntry = x;
            midiResetSynthXPickup = false;
        }

        if (midiResetSynthYPickup)
        {
            synthYPickedUp = false;
            synthYEntry = y;
            midiResetSynthYPickup = false;
        }

        if (midiResetAltMainPickup)
        {
            altMainPickedUp = false;
            altMainEntry = main;
            midiResetAltMainPickup = false;
        }

        if (midiResetAltXPickup)
        {
            altXPickedUp = false;
            altXEntry = x;
            midiResetAltXPickup = false;
        }

        if (midiResetAltYPickup)
        {
            altYPickedUp = false;
            altYEntry = y;
            midiResetAltYPickup = false;
        }
    }

    void setDetuneFromControl(int32_t control)
    {
        osc2Detune = clamp12(control) - 2048;
        if (osc2Detune > -32 && osc2Detune < 32)
            osc2Detune = 0;

        osc2Level = osc2Detune < 0 ? -osc2Detune : osc2Detune;
        osc2Level <<= 1;
        if (osc2Level > 4095) osc2Level = 4095;
    }

    void updateTuringModeControls(int32_t main, int32_t x, int32_t y)
    {
        if (turingMainPickedUp ||
            pickupModeControl(main, turingMainEntry, turingMutationControl, turingMainPickedUp))
            turingMutationControl = main;

        if (turingXPickedUp ||
            pickupModeControl(x, turingXEntry, turingLengthControl, turingXPickedUp))
            turingLengthControl = x;

        if (turingYPickedUp ||
            pickupModeControl(y, turingYEntry, turingClockControl, turingYPickedUp))
            turingClockControl = y;
    }

    void resetModePickup(Switch mode, Switch previousMode, int32_t main, int32_t x, int32_t y)
    {
        if (mode == Switch::Middle && previousMode != Switch::Middle)
            resetSynthPickup(main, x, y);
        else if (mode == Switch::Up && previousMode != Switch::Up)
            resetTuringPickup(main, x, y);
    }

    void resetSynthPickup(int32_t main, int32_t x, int32_t y)
    {
        synthMainPickedUp = false;
        synthXPickedUp = false;
        synthYPickedUp = false;
        synthMainEntry = main;
        synthXEntry = x;
        synthYEntry = y;
    }

    void resetTuringPickup(int32_t main, int32_t x, int32_t y)
    {
        turingMainPickedUp = false;
        turingXPickedUp = false;
        turingYPickedUp = false;
        turingMainEntry = main;
        turingXEntry = x;
        turingYEntry = y;
    }

    void resetAltPickup(int32_t main, int32_t x, int32_t y)
    {
        altMainPickedUp = false;
        altXPickedUp = false;
        altYPickedUp = false;
        altMainEntry = main;
        altXEntry = x;
        altYEntry = y;
    }

    void resetSavedSettingPickup(int32_t main, int32_t x, int32_t y)
    {
        // Leave pitch live, but protect saved/browser settings until their
        // physical controls deliberately catch the stored values.
        synthXPickedUp = false;
        synthYPickedUp = false;
        synthXEntry = x;
        synthYEntry = y;
        resetAltPickup(main, x, y);
    }

    bool pickupAltControl(int32_t knob, int32_t entry, int32_t target, bool& pickedUp)
    {
        int32_t moved = knob - entry;
        if (moved < 0)
            moved = -moved;

        int32_t distance = knob - target;
        if (distance < 0)
            distance = -distance;

        if (moved >= 64 && distance <= 96)
            pickedUp = true;

        return pickedUp;
    }

    bool pickupModeControl(int32_t knob, int32_t entry, int32_t target, bool& pickedUp)
    {
        int32_t moved = knob - entry;
        if (moved < 0)
            moved = -moved;

        int32_t distance = knob - target;
        if (distance < 0)
            distance = -distance;

        if (moved >= 64 && distance <= 96)
            pickedUp = true;

        return pickedUp;
    }

    SavedPerformanceState currentPerformanceState()
    {
        SavedPerformanceState state = {};
        state.magic = SaveMagic;
        state.version = SaveVersion;
        state.size = sizeof(SavedPerformanceState);
        state.osc2Detune = osc2Detune;
        state.osc2Level = osc2Level;
        state.pdControl = pdControl;
        state.waveControl = waveControl;
        state.osc2Ring = clamp12(osc2Ring);
        state.osc2Noise = clamp12(osc2Noise);
        state.envelopePreset =
            envelopePreset < EnvelopePresetCount ?
            envelopePreset :
            (uint8_t)EnvelopePreset::Off;
        state.reserved[0] = turingCvOctaveRange;
        state.reserved[1] = midiInChannel;
        state.reserved[2] =
            (turingMidiOutputEnabled ? 0u : 0x80u) |
            (turingMidiOutputChannel & 0x0Fu);
        state.checksum = 0;
        state.checksum = checksumState(state);

        return state;
    }

    uint32_t checksumState(const SavedPerformanceState& state)
    {
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&state);
        uint32_t checksum = 2166136261u;

        for (uint32_t i = 0; i < sizeof(SavedPerformanceState) - sizeof(uint32_t); ++i)
        {
            checksum ^= bytes[i];
            checksum *= 16777619u;
        }

        return checksum;
    }

    SavedCustomEnvelopeState currentCustomEnvelopeState()
    {
        SavedCustomEnvelopeState state = {};
        state.magic = CustomEnvelopeMagic;
        state.version = CustomEnvelopeSaveVersion;
        state.size = sizeof(SavedCustomEnvelopeState);
        state.loadedMask = customEnvelopeMask();

        for (uint32_t i = 0; i < CustomEnvelopeSlotCount; ++i)
            state.slots[i] = customEnvelopes[i];

        state.checksum = 0;
        state.checksum = checksumCustomEnvelopeState(state);
        return state;
    }

    uint8_t customEnvelopeMask()
    {
        uint8_t mask = 0;
        for (uint32_t i = 0; i < CustomEnvelopeSlotCount; ++i)
        {
            if (customEnvelopeLoaded[i])
                mask |= 1u << i;
        }

        return mask;
    }

    uint32_t checksumCustomEnvelopeState(const SavedCustomEnvelopeState& state)
    {
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&state);
        uint32_t checksum = 2166136261u;

        for (uint32_t i = 0; i < sizeof(SavedCustomEnvelopeState) - sizeof(uint32_t); ++i)
        {
            checksum ^= bytes[i];
            checksum *= 16777619u;
        }

        return checksum;
    }

    const SavedPerformanceState& flashPerformanceState()
    {
        return *reinterpret_cast<const SavedPerformanceState*>(
            XIP_BASE + SaveFlashOffset);
    }

    const SavedCustomEnvelopeState& flashCustomEnvelopeState()
    {
        return *reinterpret_cast<const SavedCustomEnvelopeState*>(
            XIP_BASE + CustomEnvelopeFlashOffset);
    }

    bool isValidSavedState(const SavedPerformanceState& state)
    {
        return
            state.magic == SaveMagic &&
            (state.version == SaveVersion ||
             state.version == LegacyTuringMidiDefaultOnSaveVersion) &&
            state.size == sizeof(SavedPerformanceState) &&
            state.checksum == checksumState(state);
    }

    bool isValidCustomEnvelopeState(const SavedCustomEnvelopeState& state)
    {
        return
            state.magic == CustomEnvelopeMagic &&
            state.version == CustomEnvelopeSaveVersion &&
            state.size == sizeof(SavedCustomEnvelopeState) &&
            state.checksum == checksumCustomEnvelopeState(state);
    }

    bool customEnvelopeStateMatches(
        const SavedCustomEnvelopeState& a,
        const SavedCustomEnvelopeState& b)
    {
        if (a.loadedMask != b.loadedMask)
            return false;

        const uint8_t* aBytes = reinterpret_cast<const uint8_t*>(a.slots);
        const uint8_t* bBytes = reinterpret_cast<const uint8_t*>(b.slots);
        for (uint32_t i = 0; i < sizeof(a.slots); ++i)
        {
            if (aBytes[i] != bBytes[i])
                return false;
        }

        return true;
    }

    bool savedStateMatches(const SavedPerformanceState& a, const SavedPerformanceState& b)
    {
        return
            a.osc2Detune == b.osc2Detune &&
            a.osc2Level == b.osc2Level &&
            a.pdControl == b.pdControl &&
            a.waveControl == b.waveControl &&
            a.osc2Ring == b.osc2Ring &&
            a.osc2Noise == b.osc2Noise &&
            a.envelopePreset == b.envelopePreset &&
            a.reserved[0] == b.reserved[0] &&
            a.reserved[1] == b.reserved[1] &&
            a.reserved[2] == b.reserved[2];
    }

    void loadPerformanceState()
    {
        const SavedPerformanceState& state = flashPerformanceState();
        if (!isValidSavedState(state))
            return;

        osc2Detune = state.osc2Detune;
        osc2Level = clamp12(state.osc2Level);
        pdControl = clamp12(state.pdControl);
        waveControl = clamp12(state.waveControl);
        osc2Ring = clamp12(state.osc2Ring);
        osc2Noise = clamp12(state.osc2Noise);
        turingCvOctaveRange = clampTuringCvOctaveRange(state.reserved[0]);
        midiInChannel = state.reserved[1] & 0x0Fu;
        // v3 defaulted generated Turing MIDI to on. Treat old saved states as
        // a migration source, but start the maintenance baseline with it off.
        turingMidiOutputEnabled =
            state.version == LegacyTuringMidiDefaultOnSaveVersion
                ? false
                : (state.reserved[2] & 0x80u) == 0;
        turingMidiOutputChannel = state.reserved[2] & 0x0Fu;
        envelopePreset = state.envelopePreset < EnvelopePresetCount ?
            state.envelopePreset :
            (uint8_t)EnvelopePreset::Off;
    }

    uint8_t clampTuringCvOctaveRange(uint8_t range)
    {
        if (range < MinTuringCvOctaveRange)
            return DefaultTuringCvOctaveRange;
        if (range > MaxTuringCvOctaveRange)
            return MaxTuringCvOctaveRange;
        return range;
    }

    void loadCustomEnvelopeState()
    {
        const SavedCustomEnvelopeState& state = flashCustomEnvelopeState();
        if (!isValidCustomEnvelopeState(state))
            return;

        for (uint32_t i = 0; i < CustomEnvelopeSlotCount; ++i)
        {
            customEnvelopes[i] = state.slots[i];
            customEnvelopeLoaded[i] = (state.loadedMask & (1u << i)) != 0;
        }
    }

    void savePerformanceStateIfChanged()
    {
        SavedPerformanceState state = currentPerformanceState();
        const SavedPerformanceState& saved = flashPerformanceState();

        if (isValidSavedState(saved) && savedStateMatches(state, saved))
            return;

        uint8_t page[FLASH_PAGE_SIZE];
        for (uint32_t i = 0; i < FLASH_PAGE_SIZE; ++i)
            page[i] = 0xFF;

        const uint8_t* stateBytes = reinterpret_cast<const uint8_t*>(&state);
        for (uint32_t i = 0; i < sizeof(SavedPerformanceState); ++i)
            page[i] = stateBytes[i];

        uint32_t interrupts = save_and_disable_interrupts();
        flash_range_erase(SaveFlashOffset, FLASH_SECTOR_SIZE);
        flash_range_program(SaveFlashOffset, page, FLASH_PAGE_SIZE);
        restore_interrupts(interrupts);
    }

    void saveCustomEnvelopeState()
    {
        SavedCustomEnvelopeState state = currentCustomEnvelopeState();
        const SavedCustomEnvelopeState& saved = flashCustomEnvelopeState();

        if (isValidCustomEnvelopeState(saved) &&
            customEnvelopeStateMatches(state, saved))
            return;

        uint8_t page[FLASH_PAGE_SIZE];
        const uint8_t* stateBytes = reinterpret_cast<const uint8_t*>(&state);
        uint32_t bytesWritten = 0;

        uint32_t interrupts = save_and_disable_interrupts();
        flash_range_erase(CustomEnvelopeFlashOffset, FLASH_SECTOR_SIZE);

        while (bytesWritten < sizeof(SavedCustomEnvelopeState))
        {
            for (uint32_t i = 0; i < FLASH_PAGE_SIZE; ++i)
                page[i] = 0xFF;

            for (uint32_t i = 0;
                 i < FLASH_PAGE_SIZE && bytesWritten + i < sizeof(SavedCustomEnvelopeState);
                 ++i)
                page[i] = stateBytes[bytesWritten + i];

            flash_range_program(
                CustomEnvelopeFlashOffset + bytesWritten,
                page,
                FLASH_PAGE_SIZE);
            bytesWritten += FLASH_PAGE_SIZE;
        }

        restore_interrupts(interrupts);
    }

    int32_t applyDetune(int32_t freq, int32_t detune)
    {
        int32_t bend = detune;
        int32_t sign = 1;

        if (bend < 0)
        {
            sign = -1;
            bend = -bend;
        }

        // Small movements give fine beating; the far ends reach wide offsets.
        int32_t fine = (bend * bend) >> 11;
        int32_t offset = (freq * fine) >> 12;

        if (sign < 0)
            return freq - offset;

        return freq + offset;
    }

    void syncOscillators()
    {
        phase1 = 0;
        phase2 = 0;
        syncFadeSamples = TriggerDeClickSamples;
    }

    int32_t updateSyncFade()
    {
        if (syncFadeSamples == 0)
            return 4095;

        int32_t scale =
            4095 - ((syncFadeSamples * 4095) / TriggerDeClickSamples);
        syncFadeSamples--;
        return scale;
    }

    int32_t updateLoopFade()
    {
        if (loopFadeSamples == 0)
            return 4095;

        int32_t scale =
            4095 -
            ((loopFadeSamples * (4095 - LoopDeClickFloorQ12)) /
             LoopDeClickSamples);
        loopFadeSamples--;
        return scale;
    }

    uint8_t fastNoise()
    {
        noise = noise * 1664525u + 1013904223u;
        return noise >> 24;
    }

    int32_t pitchUnits(int32_t knob, int32_t pitchInput)
    {
        int32_t mainUnits =
            (clamp12(knob) * MainPitchOctaves * PitchUnitsPerOctave) / 4095;
        int32_t inputUnits =
            (pitchInput * PitchUnitsPerOctave) / PitchInputCountsPerVolt;

        return mainUnits + inputUnits;
    }

    int32_t pitchFrequency(int32_t units)
    {
        static constexpr uint32_t SemitoneRatioQ15[13] = {
            32768, 34716, 36781, 38968, 41285, 43740, 46341,
            49097, 52016, 55109, 58386, 61858, 65536
        };

        if (units < MinPitchUnits) units = MinPitchUnits;
        if (units > MaxPitchUnits) units = MaxPitchUnits;

        int32_t octaves = units / PitchUnitsPerOctave;
        int32_t octaveRemainder = units % PitchUnitsPerOctave;
        if (octaveRemainder < 0)
        {
            octaveRemainder += PitchUnitsPerOctave;
            octaves--;
        }

        int32_t semitone = (octaveRemainder * 12) / PitchUnitsPerOctave;
        int32_t semitoneStart = (semitone * PitchUnitsPerOctave) / 12;
        int32_t semitoneEnd = ((semitone + 1) * PitchUnitsPerOctave) / 12;
        int32_t frac =
            ((octaveRemainder - semitoneStart) << 15) /
            (semitoneEnd - semitoneStart);

        uint32_t ratio =
            SemitoneRatioQ15[semitone] +
            (((int32_t)SemitoneRatioQ15[semitone + 1] -
              (int32_t)SemitoneRatioQ15[semitone]) * frac >> 15);

        uint32_t freq = (uint32_t)(((uint64_t)C2PhaseIncrement * ratio) >> 15);

        if (octaves >= 0)
            freq <<= octaves;
        else
            freq >>= -octaves;

        return (int32_t)freq;
    }

    int32_t smoothPitch(int32_t target)
    {
        if (smoothedFreq == 0)
            smoothedFreq = target;

        int32_t delta = target - smoothedFreq;
        int32_t step = delta >> 9;

        if (step == 0 && delta != 0)
            step = delta > 0 ? 1 : -1;

        smoothedFreq += step;

        return smoothedFreq;
    }

    int32_t responseCurve(int32_t x)
    {
        uint32_t ux = (uint32_t)clamp12(x);

        return (ux * ux) >> 12;
    }

    int32_t pdCompensationScale(int32_t pd)
    {
        int32_t pdCurve = responseCurve(pd);
        int32_t reduction = (pdCurve * (4095 - PdCompensationFloorQ12)) >> 12;
        return 4095 - reduction;
    }

    int32_t softenHighPitchTarget(
        int32_t sine,
        int32_t target,
        int32_t freq,
        int32_t pdCurve)
    {
        int32_t pitchAmount = freq - HighPitchSofteningStart;
        if (pitchAmount <= 0 || pdCurve <= 0)
            return target;

        if (pitchAmount > HighPitchSofteningRange)
            pitchAmount = HighPitchSofteningRange;

        int32_t pitchCurve = (pitchAmount << 12) / HighPitchSofteningRange;
        int32_t soften =
            (((pitchCurve * pdCurve) >> 12) * HighPitchSofteningMaxQ12) >> 12;

        return mix(target, sine, soften);
    }

    uint32_t smoothStep12(uint32_t x)
    {
        uint32_t x2 = (x * x) >> 12;
        uint32_t x3 = (x2 * x) >> 12;

        return (3u * x2) - (2u * x3);
    }
private:

    uint32_t phase1 = 0;
    uint32_t phase2 = 0;
    uint32_t syncFadeSamples = 0;
    uint32_t loopFadeSamples = 0;
    OutputFilterState outputFilterLeft = {};
    OutputFilterState outputFilterRight = {};

    uint32_t turing = 0xACE1u;
    int32_t turingSmooth = 0;
    int32_t turingCv = 0;
    int32_t turingModCv = 0;
    uint32_t turingClock = 0;
    uint32_t turingClockPeriod = 12000;
    uint32_t turingPulseAge = 0;
    uint32_t turingClockLedAge = TuringClockLedSamples;
    uint32_t turingLength = 16;
    uint32_t externalClockAge = 96000u;
    bool turingPulse = false;
    bool turingAltPulse = false;

    uint32_t noise = 1;
    uint32_t noiseHoldCounter = 0;
    int32_t heldPdNoise = 0;
    int32_t heldPhaseNoise = 0;
    uint32_t ampEnvelopeSample = 0;
    uint32_t pdEnvelopeSample = 0;
    uint32_t pitchEnvelopeSample = 0;
    int32_t ampEnvelopeLevel = 0;
    int32_t pdEnvelopeLevel = 0;
    int32_t pitchEnvelopeLevel = PitchEnvelopeCenter;
    int32_t ampEnvelopeStartLevel = 0;
    int32_t pdEnvelopeStartLevel = 0;
    int32_t pitchEnvelopeStartLevel = PitchEnvelopeCenter;
    uint8_t ampEnvelopeStage = 8;
    uint8_t pdEnvelopeStage = 8;
    uint8_t pitchEnvelopeStage = 8;
    bool envelopeActive = false;
    bool envelopeHeld = false;
    bool envelopeReleaseRequested = true;
    bool pulse2GateWasHigh = false;
    bool pulse2EnvelopeHolding = false;
    uint8_t envelopePreset = 0;
    uint32_t startupSelectSamples = 0;
    uint32_t envelopeSelectHoldSamples = 0;
    bool startupSelectChecked = false;
    bool envelopeSelectMode = false;
    bool envelopeSelectReady = false;
    bool envelopeSelectHoldActive = false;
    bool envelopeSelectSaved = false;

    int32_t pitchControl = 2048;
    int32_t pdControl = 0;
    int32_t waveControl = 0;
    int32_t turingMutationControl = 2048;
    int32_t turingLengthControl = 4095;
    int32_t turingClockControl = 2048;
    uint8_t turingCvOctaveRange = DefaultTuringCvOctaveRange;
    int32_t smoothedFreq = 0;
    int32_t osc2Detune = 0;
    int32_t osc2Level = 0;
    int32_t osc2Ring = 0;
    int32_t osc2Noise = 0;
    int32_t synthMainEntry = 2048;
    int32_t synthXEntry = 0;
    int32_t synthYEntry = 0;
    int32_t turingMainEntry = 2048;
    int32_t turingXEntry = 4095;
    int32_t turingYEntry = 2048;
    int32_t altMainEntry = 2048;
    int32_t altXEntry = 0;
    int32_t altYEntry = 0;
    bool synthMainPickedUp = true;
    bool synthXPickedUp = true;
    bool synthYPickedUp = true;
    bool turingMainPickedUp = true;
    bool turingXPickedUp = true;
    bool turingYPickedUp = true;
    bool altMainPickedUp = false;
    bool altXPickedUp = false;
    bool altYPickedUp = false;
    uint32_t saveHoldSamples = 0;
    uint32_t saveConfirmSamples = 0;
    bool saveHoldCanSave = false;
    bool saveCompletedThisHold = false;
    bool modePickupInitialized = false;
    bool downEditUnlocked = false;
    Switch lastMode = Switch::Middle;

    uint8_t midiRunningStatus = 0;
    uint8_t midiData[2] = {};
    uint8_t midiDataCount = 0;
    volatile uint8_t pendingMidiNote = 60;
    volatile uint8_t pendingMidiVelocity = 100;
    volatile bool pendingMidiNoteOn = false;
    volatile uint8_t pendingTuringMidiNote = 60;
    volatile bool pendingTuringMidiNoteOn = false;
    volatile bool pendingTuringMidiNoteOff = false;
    uint8_t turingMidiLastNote = 60;
    uint8_t turingMidiLastChannel = 0;
    bool turingMidiNoteActive = false;
    volatile bool turingMidiOutputEnabled = false;
    volatile uint8_t turingMidiOutputChannel = 0;
    uint8_t midiNote = 60;
    uint8_t midiVelocity = 100;
    int32_t midiPitchBend = 0;
    bool midiNoteActive = false;
    bool midiNoteReleased = false;
    volatile uint8_t midiInChannel = 0;
    volatile bool midiResetSynthXPickup = false;
    volatile bool midiResetSynthYPickup = false;
    volatile bool midiResetAltMainPickup = false;
    volatile bool midiResetAltXPickup = false;
    volatile bool midiResetAltYPickup = false;
    EnvelopeProgram customEnvelopes[CustomEnvelopeSlotCount] = {};
    bool customEnvelopeLoaded[CustomEnvelopeSlotCount] = {};
    uint8_t sysexBuffer[WebMidiMaxSysexLength] = {};
    uint32_t sysexLength = 0;
    bool sysexReceiving = false;
    bool sysexOverflow = false;
};

// =========================================================
// ENTRY
// =========================================================
C1ZZL3 card;
static volatile uint8_t hostMidiDeviceAddress = 0;

extern "C" void tuh_midi_mount_cb(
    uint8_t dev_addr,
    uint8_t in_ep,
    uint8_t out_ep,
    uint8_t num_cables_rx,
    uint16_t num_cables_tx)
{
    (void)in_ep;
    (void)out_ep;
    (void)num_cables_rx;
    (void)num_cables_tx;

    if (hostMidiDeviceAddress == 0)
        hostMidiDeviceAddress = dev_addr;
}

extern "C" void tuh_midi_umount_cb(uint8_t dev_addr, uint8_t instance)
{
    (void)instance;

    if (dev_addr == hostMidiDeviceAddress)
        hostMidiDeviceAddress = 0;
}

extern "C" void tuh_midi_rx_cb(uint8_t dev_addr, uint32_t num_packets)
{
    if (dev_addr != hostMidiDeviceAddress || num_packets == 0)
        return;

    uint8_t cable = 0;
    uint8_t bytes[128];
    while (true)
    {
        uint32_t count = tuh_midi_stream_read(dev_addr, &cable, bytes, sizeof(bytes));
        if (count == 0)
            break;

        for (uint32_t i = 0; i < count; ++i)
            card.ProcessUsbMidiByte(bytes[i]);
    }
}

extern "C" void tuh_midi_tx_cb(uint8_t dev_addr)
{
    (void)dev_addr;
}

void usbMidiWorker()
{
    sleep_ms(100);
    bool hostMode = card.ShouldBootUsbHost();

    if (hostMode)
        tuh_init(0);
    else
        tud_init(0);

    while (true)
    {
        if (hostMode)
        {
            tuh_task();
        }
        else
        {
            tud_task();
            card.SendPendingUsbMidiOutput();

            uint8_t bytes[64];
            uint32_t count = tud_midi_stream_read(bytes, sizeof(bytes));
            for (uint32_t i = 0; i < count; ++i)
                card.ProcessUsbMidiByte(bytes[i]);
        }
    }
}

//int main()
//{
//    card.EnableNormalisationProbe();
//    card.Run();
//}
int main()
{
#if defined(C1ZZL3_OVERCLOCK_KHZ) && C1ZZL3_OVERCLOCK_KHZ
    set_sys_clock_khz(C1ZZL3_OVERCLOCK_KHZ, true);
#endif
    multicore_launch_core1(usbMidiWorker);
    card.Run();
}
