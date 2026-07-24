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
static constexpr uint8_t WebMidiCommandPd2EnvelopeResponse = 0x0Eu;
static constexpr uint8_t WebMidiCommandAmp2EnvelopeResponse = 0x0Fu;
static constexpr uint8_t WebMidiCommandRequestPd2Envelope = 0x10u;
static constexpr uint8_t WebMidiCommandRequestAmp2Envelope = 0x11u;
static constexpr uint8_t WebMidiEnvelopeProtocolVersion = 11u;
static constexpr uint32_t WebMidiSettingsPayloadLength = 14u;
static constexpr uint32_t WebMidiDualOscSettingsPayloadLength = 16u;
static constexpr uint32_t WebMidiRecipeBankSettingsPayloadLength = 17u;
static constexpr uint32_t WebMidiRecipeBankPd2SettingsPayloadLength = 19u;
static constexpr uint32_t WebMidiStableSettingsPayloadLength = 8u;
static constexpr uint32_t WebMidiRangeSettingsPayloadLength = 6u;
static constexpr uint32_t WebMidiLegacySettingsPayloadLength = 5u;
static constexpr uint32_t WebMidiLegacyEnvelopePayloadLength = 97u;
static constexpr uint32_t WebMidiEnvelopePayloadLength = 137u;
static constexpr uint32_t WebMidiDualPitchEnvelopePayloadLength = 177u;
static constexpr uint32_t WebMidiDualOscEnvelopePayloadLength = 217u;
static constexpr uint32_t WebMidiDualAmpEnvelopePayloadLength = 257u;
static constexpr uint32_t WebMidiDualAmpSustainEnvelopePayloadLength = 263u;
static constexpr uint32_t WebMidiSoundPresetEnvelopePayloadLength = 279u;
static constexpr uint32_t WebMidiSoundPresetPd2EnvelopePayloadLength = 281u;
static constexpr uint32_t WebMidiDeleteEnvelopePayloadLength = 1u;
static constexpr uint32_t WebMidiMaxSysexLength = 300u;

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
        // Gnarly removes the generated Turing MIDI lane. This hook remains as
        // cleanup only, so older saved states cannot leave a generated note on.
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
        // GNARLY DUAL OSCILLATOR UI
        // =========================
        {
            bool performanceEdit = (mode == Switch::Down);

            if (mode == Switch::Up)
            {
                updateSynthModeControls(main, x, y);
            }
            else if (mode == Switch::Middle)
            {
                updateOsc2ModeControls(main, x, y);
            }
            else
            {
                if (previousMode != Switch::Down)
                {
                    saveHoldCanSave = (previousMode != Switch::Down);
                    resetAltPickup(main, x, y);
                }

                if (downEditUnlocked)
                    updateAltControls(main, x, y);
            }

            // -------------------------
            // PITCH (octave map with hardware-tested 1V/oct input scale)
            // -------------------------
            int32_t freq = smoothPitch(pitchFrequency(currentPitchUnits(pitchControl, in1)));

            int32_t pd1 = clamp12(pdControl + (cv1 << 1));
            int32_t pd2 = clamp12(pd2Control + (cv1 << 1));
            int32_t wave1 = clamp12(waveControl + (cv2 << 1));
            int32_t wave2 = clamp12(wave2Control + (cv2 << 1));

            int32_t ring = clamp12(osc2Ring);
            int32_t noiseAmt = clamp12(osc2Noise);

            clearTuringState();

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

            outputSynthVoice(freq, pd1, pd2, wave1, wave2, ring, noiseAmt);

            CVOut1(0);
            CVOut2(0);
            PulseOut1(false);
            PulseOut2(false);

            updateSynthLEDs(mode, pd1, wave1, pd2, wave2);
            updateSaveGesture(performanceEdit);
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
        EnvelopeStage pitch2[8];
        EnvelopeStage pd2[8];
        EnvelopeStage amp2[8];
        uint8_t sustain[6];
    };

    struct SavedEnvelopeProgram
    {
        EnvelopeStage amp[8];
        EnvelopeStage pd[8];
        EnvelopeStage pitch[8];
        EnvelopeStage pitch2[8];
        EnvelopeStage pd2[8];
        EnvelopeStage amp2[8];
        uint8_t sustain[6];
    };

    struct SavedEnvelopeProgramV5
    {
        EnvelopeStage amp[8];
        EnvelopeStage pd[8];
        EnvelopeStage pitch[8];
        EnvelopeStage pitch2[8];
        EnvelopeStage pd2[8];
        EnvelopeStage amp2[8];
    };

    struct SavedEnvelopeProgramV4
    {
        EnvelopeStage amp[8];
        EnvelopeStage pd[8];
        EnvelopeStage pitch[8];
        EnvelopeStage pitch2[8];
        EnvelopeStage pd2[8];
    };

    struct SavedEnvelopeProgramV3
    {
        EnvelopeStage amp[8];
        EnvelopeStage pd[8];
        EnvelopeStage pitch[8];
        EnvelopeStage pitch2[8];
    };

    static constexpr uint8_t MidiCcModWheelOsc1Pd = 1;
    static constexpr uint8_t MidiCcOsc1Wave = 20;
    static constexpr uint8_t MidiCcOsc2Wave = 21;
    static constexpr uint8_t MidiCcRingAmount = 22;
    static constexpr uint8_t MidiCcRecipeBank = 23;
    static constexpr uint8_t MidiCcOsc2Interval = 24;
    static constexpr uint8_t MidiCcOsc2Pd = 25;
    static constexpr uint8_t MidiCcNoiseAmount = 26;
    static constexpr uint8_t MidiCcOsc1Pd = 27;
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
    static constexpr uint8_t NoSustainStage = 0x7Fu;
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
    static constexpr uint8_t RecipeBankCount = 4u;
    static constexpr uint32_t SaveFlashOffset =
        (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE) &
        ~(FLASH_SECTOR_SIZE - 1u);
    static constexpr uint32_t CustomEnvelopeMagic = 0x4331454Eu; // C1EN
    static constexpr uint16_t LegacyCustomEnvelopeSaveVersion = 3;
    static constexpr uint16_t DualPdCustomEnvelopeSaveVersion = 4;
    static constexpr uint16_t DualAmpCustomEnvelopeSaveVersion = 5;
    static constexpr uint16_t NamedCustomEnvelopeSaveVersion = 7;
    static constexpr uint16_t SoundPresetCustomEnvelopeSaveVersion = 8;
    static constexpr uint16_t SoundPresetPd2CustomEnvelopeSaveVersion = 9;
    static constexpr uint16_t CustomEnvelopeSaveVersion = 9;
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
        int32_t osc2IntervalControl;
        int32_t pdControl;
        int32_t pd2Control;
        int32_t waveControl;
        int32_t osc2Ring;
        int32_t osc2Noise;
        uint8_t envelopePreset;
        uint8_t reserved[3];
        uint32_t checksum;
    };

    struct SavedSlotPerformanceState
    {
        uint16_t pdControl;
        uint16_t pd2Control;
        uint16_t detuneControl;
        uint16_t waveControl;
        uint16_t wave2Control;
        uint16_t ring;
        uint16_t noise;
        uint8_t midiInChannel;
        uint8_t turingRange;
        uint8_t turingMidiEnabled;
        uint8_t turingMidiChannel;
        uint8_t recipeBank;
        uint8_t reserved;
    };

    struct SavedSlotPerformanceStateV8
    {
        uint16_t pdControl;
        uint16_t detuneControl;
        uint16_t waveControl;
        uint16_t wave2Control;
        uint16_t ring;
        uint16_t noise;
        uint8_t midiInChannel;
        uint8_t turingRange;
        uint8_t turingMidiEnabled;
        uint8_t turingMidiChannel;
        uint8_t recipeBank;
        uint8_t reserved;
    };

    struct SavedCustomEnvelopeState
    {
        uint32_t magic;
        uint16_t version;
        uint16_t size;
        uint8_t loadedMask;
        uint8_t reserved[7];
        uint8_t names[CustomEnvelopeSlotCount][16];
        SavedSlotPerformanceState performances[CustomEnvelopeSlotCount];
        SavedEnvelopeProgram slots[CustomEnvelopeSlotCount];
        uint32_t checksum;
    };

    struct SavedCustomEnvelopeStateV3
    {
        uint32_t magic;
        uint16_t version;
        uint16_t size;
        uint8_t loadedMask;
        uint8_t reserved[7];
        SavedEnvelopeProgramV3 slots[CustomEnvelopeSlotCount];
        uint32_t checksum;
    };

    struct SavedCustomEnvelopeStateV4
    {
        uint32_t magic;
        uint16_t version;
        uint16_t size;
        uint8_t loadedMask;
        uint8_t reserved[7];
        SavedEnvelopeProgramV4 slots[CustomEnvelopeSlotCount];
        uint32_t checksum;
    };

    struct SavedCustomEnvelopeStateV5
    {
        uint32_t magic;
        uint16_t version;
        uint16_t size;
        uint8_t loadedMask;
        uint8_t reserved[7];
        SavedEnvelopeProgramV5 slots[CustomEnvelopeSlotCount];
        uint32_t checksum;
    };

    struct SavedCustomEnvelopeStateV7
    {
        uint32_t magic;
        uint16_t version;
        uint16_t size;
        uint8_t loadedMask;
        uint8_t reserved[7];
        uint8_t names[CustomEnvelopeSlotCount][16];
        SavedEnvelopeProgram slots[CustomEnvelopeSlotCount];
        uint32_t checksum;
    };

    struct SavedCustomEnvelopeStateV8
    {
        uint32_t magic;
        uint16_t version;
        uint16_t size;
        uint8_t loadedMask;
        uint8_t reserved[7];
        uint8_t names[CustomEnvelopeSlotCount][16];
        SavedSlotPerformanceStateV8 performances[CustomEnvelopeSlotCount];
        SavedEnvelopeProgram slots[CustomEnvelopeSlotCount];
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
        int32_t wave1 = clamp12(waveControl);
        int32_t wave2 = clamp12(wave2Control);

        outputSynthVoice(freq, pd, pd2Control, wave1, wave2, osc2Ring, osc2Noise);
    }

    void outputSynthVoice(
        int32_t freq,
        int32_t pd1Base,
        int32_t pd2Base,
        int32_t wave1,
        int32_t wave2,
        int32_t ring,
        int32_t noiseAmt)
    {
        updateEnvelope();
        int32_t freq1 = applyPitchEnvelopeToFrequency(freq, pitchEnvelopeLevel);
        int32_t pd1 = applyEnvelopeToPd(pd1Base, pdEnvelopeLevel);
        int32_t pd2 = applyEnvelopeToPd(pd2Base, pd2EnvelopeLevel);

        int32_t osc1 =
            oscCZ(phase1, freq1, pd1, wave1, recipeBankControl, noiseAmt);

        int32_t freq2 =
            applyPitchEnvelopeToFrequency(
                applyDetune(applyOsc2BaseInterval(freq), osc2Detune),
                pitch2EnvelopeLevel);

        int32_t osc2 =
            oscCZ(phase2, freq2, pd2, wave2, recipeBankControl, noiseAmt);

        int32_t osc2Raw = osc2;
        osc2 = osc2Raw;

        int32_t ringCarrier = osc2Raw;
        int32_t ringSig = clip((osc1 * ringCarrier) >> 10);
        int32_t ringMix = (ring * 3840) >> 12;
        osc1 = mix(osc1, ringSig, ringMix);

        int32_t sharedScale = pdCompensationScale(pd1 > pd2 ? pd1 : pd2);
        sharedScale = (sharedScale * updateSyncFade()) >> 12;
        sharedScale = (sharedScale * updateLoopFade()) >> 12;
        int32_t ampScale1 = (envelopeAmpScale(ampEnvelopeLevel) * sharedScale) >> 12;
        int32_t ampScale2 = (envelopeAmpScale(amp2EnvelopeLevel) * sharedScale) >> 12;
        osc1 = (osc1 * ampScale1) >> 12;
        osc2 = (osc2 * ampScale2) >> 12;

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
        int32_t bank,
        int32_t noiseAmt)
    {
        phase += (uint32_t)freq;

        uint32_t renderPhase = phase;
        int32_t noisyPd = pd;

        if (noiseAmt > 0)
            applyCZNoise(renderPhase, noisyPd, noiseAmt);

        int32_t pdCurve = responseCurve(noisyPd);

        int32_t sine = getSine(renderPhase);
        int32_t target = morphRecipeWave(renderPhase, wave, bank);
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
        int32_t amp2StartLevel = envelopeActive ? amp2EnvelopeLevel : 0;
        int32_t pdStartLevel = envelopeActive ? pdEnvelopeLevel : 0;
        int32_t pd2StartLevel = envelopeActive ? pd2EnvelopeLevel : 0;
        int32_t pitchStartLevel = envelopeActive ? pitchEnvelopeLevel : PitchEnvelopeCenter;
        int32_t pitch2StartLevel = envelopeActive ? pitch2EnvelopeLevel : PitchEnvelopeCenter;

        ampEnvelopeStage = 0;
        ampEnvelopeSample = 0;
        ampEnvelopeStartLevel = ampStartLevel;
        ampEnvelopeLevel = ampStartLevel;
        amp2EnvelopeStage = 0;
        amp2EnvelopeSample = 0;
        amp2EnvelopeStartLevel = amp2StartLevel;
        amp2EnvelopeLevel = amp2StartLevel;
        pdEnvelopeStage = 0;
        pdEnvelopeSample = 0;
        pdEnvelopeStartLevel = pdStartLevel;
        pdEnvelopeLevel = pdStartLevel;
        pd2EnvelopeStage = 0;
        pd2EnvelopeSample = 0;
        pd2EnvelopeStartLevel = pd2StartLevel;
        pd2EnvelopeLevel = pd2StartLevel;
        pitchEnvelopeStage = 0;
        pitchEnvelopeSample = 0;
        pitchEnvelopeStartLevel = pitchStartLevel;
        pitchEnvelopeLevel = pitchStartLevel;
        pitch2EnvelopeStage = 0;
        pitch2EnvelopeSample = 0;
        pitch2EnvelopeStartLevel = pitch2StartLevel;
        pitch2EnvelopeLevel = pitch2StartLevel;
        envelopeActive = true;
        envelopeHeld = held;
        envelopeReleaseRequested = !held;
        pulse2EnvelopeHolding = heldByPulse;
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
            program.sustain[0],
            true);

        const EnvelopeStage* amp2Program =
            pitchLaneHasData(program.amp2) ? program.amp2 : program.amp;
        bool amp2Done = updateEnvelopeRunner(
            amp2Program,
            amp2EnvelopeStage,
            amp2EnvelopeSample,
            amp2EnvelopeLevel,
            amp2EnvelopeStartLevel,
            envelopeHeld,
            envelopeReleaseRequested,
            loopEnabled,
            program.sustain[5],
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
            program.sustain[1],
            false);

        const EnvelopeStage* pd2Program =
            pitchLaneHasData(program.pd2) ? program.pd2 : program.pd;
        bool pd2Done = updateEnvelopeRunner(
            pd2Program,
            pd2EnvelopeStage,
            pd2EnvelopeSample,
            pd2EnvelopeLevel,
            pd2EnvelopeStartLevel,
            envelopeHeld,
            envelopeReleaseRequested,
            loopEnabled,
            program.sustain[4],
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
            program.sustain[2],
            false);

        const EnvelopeStage* pitch2Program =
            pitchLaneHasData(program.pitch2) ? program.pitch2 : program.pitch;
        bool pitch2Done = updateEnvelopeRunner(
            pitch2Program,
            pitch2EnvelopeStage,
            pitch2EnvelopeSample,
            pitch2EnvelopeLevel,
            pitch2EnvelopeStartLevel,
            envelopeHeld,
            envelopeReleaseRequested,
            loopEnabled,
            program.sustain[3],
            false);

        if (ampDone && amp2Done && pdDone && pd2Done && pitchDone && pitch2Done)
        {
            if (envelopeHeld && !envelopeReleaseRequested)
                return clamp12(ampEnvelopeLevel);

            finishEnvelopeNow();
        }

        return clamp12(ampEnvelopeLevel);
    }

    bool pitchLaneHasData(const EnvelopeStage (&stages)[8])
    {
        for (uint32_t i = 0; i < 8u; ++i)
        {
            if (stages[i].time > 0u)
                return true;
        }

        return false;
    }

    int32_t applyEnvelopeToPd(int32_t pd, int32_t level)
    {
        return clamp12(pd + level);
    }

    int32_t applyPitchEnvelopeToFrequency(int32_t freq, int32_t pitchEnvelope)
    {
        int32_t semitoneQ12 =
            ((pitchEnvelope - (int32_t)PitchEnvelopeCenter) *
             PitchEnvelopeSemitoneRange * 4096) / (int32_t)PitchEnvelopeCenter;

        return applySemitoneOffsetToFrequency(freq, semitoneQ12);
    }

    int32_t applyOsc2BaseInterval(int32_t freq)
    {
        int32_t semitoneQ12 =
            ((clamp12(osc2IntervalControl) - 2048) *
             PitchEnvelopeSemitoneRange * 4096) / 2047;

        return applySemitoneOffsetToFrequency(freq, semitoneQ12);
    }

    int32_t applySemitoneOffsetToFrequency(int32_t freq, int32_t semitoneQ12)
    {
        int32_t rangeQ12 = PitchEnvelopeSemitoneRange * 4096;
        if (semitoneQ12 < -rangeQ12)
            semitoneQ12 = -rangeQ12;
        if (semitoneQ12 > rangeQ12)
            semitoneQ12 = rangeQ12;

        uint32_t tablePosition = (uint32_t)(semitoneQ12 + rangeQ12);
        uint32_t index = tablePosition >> 12;
        if (index >= 48u)
            index = 47u;
        uint32_t frac = tablePosition & 0x0FFFu;
        int32_t ratio =
            PitchEnvelopeRatioQ12[index] +
            ((((int32_t)PitchEnvelopeRatioQ12[index + 1u] -
               (int32_t)PitchEnvelopeRatioQ12[index]) * (int32_t)frac) >> 12);
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
            if (midiData[0] == MidiCcModWheelOsc1Pd || midiData[0] == MidiCcOsc1Pd)
            {
                pdControl = midiCcToControl(midiData[1]);
                midiResetTuringXPickup = true;
            }
            else if (midiData[0] == MidiCcOsc1Wave)
            {
                waveControl = midiCcToControl(midiData[1]);
                midiResetTuringYPickup = true;
            }
            else if (midiData[0] == MidiCcOsc2Wave)
            {
                wave2Control = midiCcToControl(midiData[1]);
                midiResetSynthYPickup = true;
            }
            else if (midiData[0] == MidiCcRingAmount)
            {
                osc2Ring = midiCcToControl(midiData[1]);
                midiResetAltXPickup = true;
            }
            else if (midiData[0] == MidiCcRecipeBank)
            {
                uint8_t bank = (uint8_t)(((uint16_t)midiData[1] * 4u) >> 7);
                recipeBankControl = recipeBankToControl(bank > 3u ? 3u : bank);
                midiResetAltMainPickup = true;
            }
            else if (midiData[0] == MidiCcOsc2Interval)
            {
                osc2IntervalControl = midiCcToControl(midiData[1]);
                midiResetSynthMainPickup = true;
            }
            else if (midiData[0] == MidiCcOsc2Pd)
            {
                pd2Control = midiCcToControl(midiData[1]);
                midiResetSynthXPickup = true;
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
        {
            handleWebMidiRequestPitchEnvelope();
            return;
        }

        if (command == WebMidiCommandRequestPd2Envelope)
        {
            handleWebMidiRequestPd2Envelope();
            return;
        }

        if (command == WebMidiCommandRequestAmp2Envelope)
        {
            handleWebMidiRequestAmp2Envelope();
            return;
        }
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
        if (sysexLength != WebMidiRecipeBankPd2SettingsPayloadLength + 6u &&
            sysexLength != WebMidiRecipeBankSettingsPayloadLength + 6u &&
            sysexLength != WebMidiDualOscSettingsPayloadLength + 6u &&
            sysexLength != WebMidiSettingsPayloadLength + 6u &&
            sysexLength != WebMidiStableSettingsPayloadLength + 6u &&
            sysexLength != WebMidiRangeSettingsPayloadLength + 6u &&
            sysexLength != WebMidiLegacySettingsPayloadLength + 6u)
            return;

        uint32_t offset = 6;
        int32_t ring = decodeWebMidiUint14(offset);
        int32_t noise = decodeWebMidiUint14(offset);
        int32_t pd = pdControl;
        int32_t pd2 = pd2Control;
        int32_t detune = osc2Detune + 2048;
        int32_t wave = waveControl;
        int32_t wave2 = wave2Control;
        int32_t recipeBank = recipeBankControl;
        bool extendedSettings =
            sysexLength == WebMidiSettingsPayloadLength + 6u ||
            sysexLength == WebMidiDualOscSettingsPayloadLength + 6u ||
            sysexLength == WebMidiRecipeBankSettingsPayloadLength + 6u ||
            sysexLength == WebMidiRecipeBankPd2SettingsPayloadLength + 6u;
        if (extendedSettings)
        {
            pd = decodeWebMidiUint14(offset);
            if (sysexLength == WebMidiRecipeBankPd2SettingsPayloadLength + 6u)
                pd2 = decodeWebMidiUint14(offset);
            else
                pd2 = pd;
            detune = decodeWebMidiUint14(offset);
            wave = decodeWebMidiUint14(offset);
            wave2 = wave;
            if (sysexLength == WebMidiDualOscSettingsPayloadLength + 6u ||
                sysexLength == WebMidiRecipeBankSettingsPayloadLength + 6u ||
                sysexLength == WebMidiRecipeBankPd2SettingsPayloadLength + 6u)
                wave2 = decodeWebMidiUint14(offset);
            if (sysexLength == WebMidiRecipeBankSettingsPayloadLength + 6u ||
                sysexLength == WebMidiRecipeBankPd2SettingsPayloadLength + 6u)
                recipeBank = recipeBankToControl(sysexBuffer[offset++] & 0x03u);
        }
        uint8_t channel = sysexBuffer[offset] & 0x0Fu;
        offset++;

        osc2Ring = ring;
        osc2Noise = noise;
        pdControl = clamp12(pd);
        pd2Control = clamp12(pd2);
        setDetuneFromControl(detune);
        waveControl = clamp12(wave);
        wave2Control = clamp12(wave2);
        recipeBankControl = clamp12(recipeBank);
        midiInChannel = channel;

        // Remote settings hold until each physical control reaches its new value.
        midiResetAltXPickup = true;
        midiResetAltYPickup = true;
        if (extendedSettings)
        {
            midiResetSynthXPickup = true;
            midiResetSynthYPickup = true;
            midiResetAltMainPickup = true;
        }

        if (sysexLength >= WebMidiRangeSettingsPayloadLength + 6u)
        {
            // Compatibility padding only in Gnarly: no Turing CV/MIDI output.
            offset++;
        }

        if (extendedSettings)
        {
            turingMidiOutputEnabled = false;
            offset++;
            turingMidiOutputChannel = 0u;
        }

        if (persist)
            savePerformanceStateIfChanged();
    }

    void handleWebMidiRequestSettings()
    {
        if (sysexLength != 6u)
            return;

        uint8_t frame[27] = {
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
        appendWebMidiUint14(frame, offset, clamp12(pd2Control));
        appendWebMidiUint14(frame, offset, clamp12(osc2Detune + 2048));
        appendWebMidiUint14(frame, offset, clamp12(waveControl));
        appendWebMidiUint14(frame, offset, clamp12(wave2Control));
        frame[offset++] = controlToRecipeBank(recipeBankControl);
        frame[offset++] = midiInChannel & 0x0Fu;
        frame[offset++] = 0u;
        frame[offset++] = 0u;
        frame[offset++] = 0u;
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
        if (!customEnvelopePersist[slot])
            return;

        uint8_t frame[123] = {
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
        appendWebMidiName(frame, offset, customEnvelopeNames[slot]);
        appendWebMidiSlotPerformance(frame, offset, customEnvelopePerformances[slot]);
        const EnvelopeProgram& envelope = customEnvelopeSaved[slot];
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
    }

    void handleWebMidiRequestPitchEnvelope()
    {
        if (sysexLength != 7u)
            return;

        uint8_t slot = sysexBuffer[6] & 0x07u;
        if (!customEnvelopePersist[slot])
            return;

        sendWebMidiPitchEnvelopeResponse(slot);
    }

    void handleWebMidiRequestPd2Envelope()
    {
        if (sysexLength != 7u)
            return;

        uint8_t slot = sysexBuffer[6] & 0x07u;
        if (!customEnvelopePersist[slot])
            return;

        sendWebMidiPd2EnvelopeResponse(slot);
    }

    void handleWebMidiRequestAmp2Envelope()
    {
        if (sysexLength != 7u)
            return;

        uint8_t slot = sysexBuffer[6] & 0x07u;
        if (!customEnvelopePersist[slot])
            return;

        sendWebMidiAmp2EnvelopeResponse(slot);
    }

    void sendWebMidiAmp2EnvelopeResponse(uint8_t slot)
    {
        uint8_t frame[49] = {
            0xF0u,
            WebMidiManufacturer,
            WebMidiId[0],
            WebMidiId[1],
            WebMidiId[2],
            WebMidiId[3],
            WebMidiCommandAmp2EnvelopeResponse,
            slot
        };
        uint32_t offset = 8;
        const EnvelopeProgram& envelope = customEnvelopeSaved[slot];
        for (uint32_t i = 0; i < 8u; ++i)
        {
            appendWebMidiUint14(frame, offset, envelope.amp2[i].level);
            appendWebMidiUint21(frame, offset, envelope.amp2[i].time);
        }
        frame[offset++] = 0xF7u;
        tud_midi_stream_write(0, frame, offset);
    }

    void sendWebMidiPd2EnvelopeResponse(uint8_t slot)
    {
        uint8_t frame[49] = {
            0xF0u,
            WebMidiManufacturer,
            WebMidiId[0],
            WebMidiId[1],
            WebMidiId[2],
            WebMidiId[3],
            WebMidiCommandPd2EnvelopeResponse,
            slot
        };
        uint32_t offset = 8;
        const EnvelopeProgram& envelope = customEnvelopeSaved[slot];
        for (uint32_t i = 0; i < 8u; ++i)
        {
            appendWebMidiUint14(frame, offset, envelope.pd2[i].level);
            appendWebMidiUint21(frame, offset, envelope.pd2[i].time);
        }
        frame[offset++] = 0xF7u;
        tud_midi_stream_write(0, frame, offset);
    }

    void sendWebMidiPitchEnvelopeResponse(uint8_t slot)
    {
        uint8_t frame[89] = {
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
        const EnvelopeProgram& envelope = customEnvelopeSaved[slot];
        for (uint32_t i = 0; i < 8u; ++i)
        {
            appendWebMidiUint14(frame, offset, envelope.pitch[i].level);
            appendWebMidiUint21(frame, offset, envelope.pitch[i].time);
        }
        for (uint32_t i = 0; i < 8u; ++i)
        {
            appendWebMidiUint14(frame, offset, envelope.pitch2[i].level);
            appendWebMidiUint21(frame, offset, envelope.pitch2[i].time);
        }
        frame[offset++] = 0xF7u;
        tud_midi_stream_write(0, frame, offset);
    }

    void handleWebMidiEnvelope(bool persist)
    {
        bool legacyPayload = sysexLength == WebMidiLegacyEnvelopePayloadLength + 6u;
        bool singlePitchPayload = sysexLength == WebMidiEnvelopePayloadLength + 6u;
        bool dualPitchPayload = sysexLength == WebMidiDualPitchEnvelopePayloadLength + 6u;
        bool dualOscPayload = sysexLength == WebMidiDualOscEnvelopePayloadLength + 6u;
        bool dualAmpPayload = sysexLength == WebMidiDualAmpEnvelopePayloadLength + 6u;
        bool dualAmpSustainPayload = sysexLength == WebMidiDualAmpSustainEnvelopePayloadLength + 6u;
        bool legacySoundPresetPayload = sysexLength == WebMidiSoundPresetEnvelopePayloadLength + 6u;
        bool soundPresetPayload = legacySoundPresetPayload ||
            sysexLength == WebMidiSoundPresetPd2EnvelopePayloadLength + 6u;
        if (!legacyPayload && !singlePitchPayload && !dualPitchPayload && !dualOscPayload && !dualAmpPayload && !dualAmpSustainPayload && !soundPresetPayload)
            return;

        uint32_t offset = 6;
        uint8_t slot = sysexBuffer[offset++] & 0x07u;
        uint8_t incomingName[16] = {};
        readWebMidiName(offset, incomingName);
        SavedSlotPerformanceState incomingPerformance = currentSlotPerformanceState();
        if (soundPresetPayload)
            incomingPerformance = readWebMidiSlotPerformance(offset, !legacySoundPresetPayload);
        else if (customEnvelopePersist[slot])
            incomingPerformance = customEnvelopeSavedPerformances[slot];

        EnvelopeProgram next = {};
        uint32_t ampTotal = 0;
        uint16_t ampMax = 0;

        for (uint32_t i = 0; i < 8u; ++i)
        {
            uint16_t level = decodeWebMidiUint14(offset);
            uint32_t time = decodeWebMidiUint21(offset);
            next.amp[i] = {level, time};
            next.amp2[i] = next.amp[i];
            ampTotal += time;
            if (level > ampMax)
                ampMax = level;
        }

        for (uint32_t i = 0; i < 8u; ++i)
        {
            uint16_t level = decodeWebMidiUint14(offset);
            uint32_t time = decodeWebMidiUint21(offset);
            next.pd[i] = {level, time};
            next.pd2[i] = next.pd[i];
        }

        if (legacyPayload)
        {
            for (uint32_t i = 0; i < 8u; ++i)
            {
                next.pitch[i] = {PitchEnvelopeCenter, 1u};
                next.pitch2[i] = {PitchEnvelopeCenter, 1u};
            }
        }
        else
        {
            for (uint32_t i = 0; i < 8u; ++i)
            {
                uint16_t level = decodeWebMidiUint14(offset);
                uint32_t time = decodeWebMidiUint21(offset);
                next.pitch[i] = {level, time};
            }

            if (dualPitchPayload || dualOscPayload || dualAmpPayload || dualAmpSustainPayload || soundPresetPayload)
            {
                for (uint32_t i = 0; i < 8u; ++i)
                {
                    uint16_t level = decodeWebMidiUint14(offset);
                    uint32_t time = decodeWebMidiUint21(offset);
                    next.pitch2[i] = {level, time};
                }
            }
            else
            {
                for (uint32_t i = 0; i < 8u; ++i)
                    next.pitch2[i] = next.pitch[i];
            }
        }

        if (dualOscPayload || dualAmpPayload || dualAmpSustainPayload || soundPresetPayload)
        {
            for (uint32_t i = 0; i < 8u; ++i)
            {
                uint16_t level = decodeWebMidiUint14(offset);
                uint32_t time = decodeWebMidiUint21(offset);
                next.pd2[i] = {level, time};
            }
        }

        if (dualAmpPayload || dualAmpSustainPayload || soundPresetPayload)
        {
            for (uint32_t i = 0; i < 8u; ++i)
            {
                uint16_t level = decodeWebMidiUint14(offset);
                uint32_t time = decodeWebMidiUint21(offset);
                next.amp2[i] = {level, time};
            }
        }

        clearSustainStages(next);
        if (dualAmpSustainPayload || soundPresetPayload)
        {
            for (uint32_t i = 0; i < 6u; ++i)
                next.sustain[i] = sanitizeSustainStage(sysexBuffer[offset++] & 0x7Fu);
        }
        applyDefaultSustainStages(next);

        if (ampMax == 0 || ampTotal < MinWebMidiEnvelopeSamples)
            return;

        customEnvelopes[slot] = next;
        customEnvelopeLoaded[slot] = true;
        customEnvelopePerformances[slot] = incomingPerformance;
        applySlotPerformanceState(incomingPerformance);
        if (persist)
        {
            customEnvelopeSaved[slot] = next;
            copyEnvelopeName(customEnvelopeNames[slot], incomingName);
            customEnvelopeSavedPerformances[slot] = incomingPerformance;
            customEnvelopePersist[slot] = true;
        }
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
        customEnvelopeSaved[slot] = {};
        clearEnvelopeName(customEnvelopeNames[slot]);
        customEnvelopePerformances[slot] = {};
        customEnvelopeSavedPerformances[slot] = {};
        customEnvelopeLoaded[slot] = false;
        customEnvelopePersist[slot] = false;

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

    void readWebMidiName(uint32_t& offset, uint8_t* name)
    {
        for (uint32_t i = 0; i < 16u; ++i)
        {
            uint8_t c = sysexBuffer[offset++] & 0x7Fu;
            name[i] = (c >= 32u && c <= 126u) ? c : 32u;
        }
    }

    void appendWebMidiName(uint8_t* buffer, uint32_t& offset, const uint8_t* name)
    {
        for (uint32_t i = 0; i < 16u; ++i)
        {
            uint8_t c = name[i] & 0x7Fu;
            buffer[offset++] = (c >= 32u && c <= 126u) ? c : 32u;
        }
    }

    void copyEnvelopeName(uint8_t* destination, const uint8_t* source)
    {
        for (uint32_t i = 0; i < 16u; ++i)
            destination[i] = source[i];
    }

    void clearEnvelopeName(uint8_t* name)
    {
        for (uint32_t i = 0; i < 16u; ++i)
            name[i] = 32u;
    }

    SavedSlotPerformanceState currentSlotPerformanceState()
    {
        SavedSlotPerformanceState state = {};
        state.pdControl = (uint16_t)clamp12(pdControl);
        state.pd2Control = (uint16_t)clamp12(pd2Control);
        state.detuneControl = (uint16_t)clamp12(osc2Detune + 2048);
        state.waveControl = (uint16_t)clamp12(waveControl);
        state.wave2Control = (uint16_t)clamp12(wave2Control);
        state.ring = (uint16_t)clamp12(osc2Ring);
        state.noise = (uint16_t)clamp12(osc2Noise);
        state.midiInChannel = midiInChannel & 0x0Fu;
        state.turingRange = 0u;
        state.turingMidiEnabled = 0u;
        state.turingMidiChannel = 0u;
        state.recipeBank = controlToRecipeBank(recipeBankControl);
        state.reserved = 0u;
        return state;
    }

    SavedSlotPerformanceState readWebMidiSlotPerformance(uint32_t& offset, bool hasPd2Control)
    {
        SavedSlotPerformanceState state = {};
        state.pdControl = (uint16_t)decodeWebMidiUint14(offset);
        state.pd2Control = hasPd2Control
            ? (uint16_t)decodeWebMidiUint14(offset)
            : state.pdControl;
        state.detuneControl = (uint16_t)decodeWebMidiUint14(offset);
        state.waveControl = (uint16_t)decodeWebMidiUint14(offset);
        state.wave2Control = (uint16_t)decodeWebMidiUint14(offset);
        state.ring = (uint16_t)decodeWebMidiUint14(offset);
        state.noise = (uint16_t)decodeWebMidiUint14(offset);
        state.midiInChannel = sysexBuffer[offset++] & 0x0Fu;
        offset++;
        state.turingRange = 0u;
        offset++;
        state.turingMidiEnabled = 0u;
        state.recipeBank = sysexBuffer[offset++] & 0x03u;
        state.turingMidiChannel = 0u;
        state.reserved = 0u;
        return state;
    }

    void appendWebMidiSlotPerformance(
        uint8_t* buffer,
        uint32_t& offset,
        const SavedSlotPerformanceState& state)
    {
        appendWebMidiUint14(buffer, offset, state.pdControl);
        appendWebMidiUint14(buffer, offset, state.pd2Control);
        appendWebMidiUint14(buffer, offset, state.detuneControl);
        appendWebMidiUint14(buffer, offset, state.waveControl);
        appendWebMidiUint14(buffer, offset, state.wave2Control);
        appendWebMidiUint14(buffer, offset, state.ring);
        appendWebMidiUint14(buffer, offset, state.noise);
        buffer[offset++] = state.midiInChannel & 0x0Fu;
        buffer[offset++] = 0u;
        buffer[offset++] = 0u;
        buffer[offset++] = state.recipeBank & 0x03u;
    }

    SavedSlotPerformanceState upgradeSlotPerformanceState(
        const SavedSlotPerformanceStateV8& oldState)
    {
        SavedSlotPerformanceState state = {};
        state.pdControl = oldState.pdControl;
        state.pd2Control = oldState.pdControl;
        state.detuneControl = oldState.detuneControl;
        state.waveControl = oldState.waveControl;
        state.wave2Control = oldState.wave2Control;
        state.ring = oldState.ring;
        state.noise = oldState.noise;
        state.midiInChannel = oldState.midiInChannel;
        state.turingRange = oldState.turingRange;
        state.turingMidiEnabled = oldState.turingMidiEnabled;
        state.turingMidiChannel = oldState.turingMidiChannel;
        state.recipeBank = oldState.recipeBank;
        state.reserved = oldState.reserved;
        return state;
    }

    void applySlotPerformanceState(const SavedSlotPerformanceState& state)
    {
        pdControl = clamp12(state.pdControl);
        pd2Control = clamp12(state.pd2Control);
        setDetuneFromControl(state.detuneControl);
        waveControl = clamp12(state.waveControl);
        wave2Control = clamp12(state.wave2Control);
        osc2Ring = clamp12(state.ring);
        osc2Noise = clamp12(state.noise);
        midiInChannel = state.midiInChannel & 0x0Fu;
        recipeBankControl = recipeBankToControl(state.recipeBank);
        turingMidiOutputEnabled = false;
        turingMidiOutputChannel = 0u;

        midiResetSynthXPickup = true;
        midiResetSynthYPickup = true;
        midiResetAltMainPickup = true;
        midiResetAltXPickup = true;
        midiResetAltYPickup = true;
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

    uint8_t defaultSustainStageFor(const EnvelopeStage* stages)
    {
        for (int32_t i = 7; i >= 0; --i)
        {
            if (stages[i].level > 0)
                return (uint8_t)i;
        }

        return NoSustainStage;
    }

    void applyDefaultSustainStages(EnvelopeProgram& program)
    {
        if (program.sustain[0] == NoSustainStage)
            program.sustain[0] = defaultSustainStageFor(program.amp);
        if (program.sustain[5] == NoSustainStage)
            program.sustain[5] = defaultSustainStageFor(
                pitchLaneHasData(program.amp2) ? program.amp2 : program.amp);
        if (program.sustain[1] == NoSustainStage)
            program.sustain[1] = program.sustain[0];
        if (program.sustain[4] == NoSustainStage)
            program.sustain[4] = program.sustain[5];
        if (program.sustain[2] == NoSustainStage)
            program.sustain[2] = program.sustain[0];
        if (program.sustain[3] == NoSustainStage)
            program.sustain[3] = program.sustain[5];
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
        }, {}, {}, {}, {1, 1, 1, 1, 1, 1}};

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
        }, {}, {}, {}, {6, 6, 6, 6, 6, 6}};

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
        }, {}, {}, {}, {6, 6, 6, 6, 6, 6}};

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
        }, {}, {}, {}, {3, 3, 3, 3, 3, 3}};

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
        }, {}, {}, {}, {5, 5, 5, 5, 5, 5}};

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
        }, {}, {}, {}, {6, 6, 6, 6, 6, 6}};

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
        }, {}, {}, {}, {4, 4, 4, 4, 4, 4}};

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
        }, {}, {}, {}, {6, 6, 6, 6, 6, 6}};

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
        }, {}, {}, {}, {NoSustainStage, NoSustainStage, NoSustainStage, NoSustainStage, NoSustainStage, NoSustainStage}};

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
        uint8_t sustainStage,
        bool deClickOnLoop)
    {
        if (stage >= 8)
            return true;

        if (held && !releaseRequested && sustainStage < 8u && stage > sustainStage)
            return false;

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
        (void)program;
        return false;
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

    void finishEnvelopeNow()
    {
        envelopeActive = false;
        envelopeHeld = false;
        envelopeReleaseRequested = false;
        pulse2EnvelopeHolding = false;
        ampEnvelopeLevel = 0;
        amp2EnvelopeLevel = 0;
        pitchEnvelopeLevel = PitchEnvelopeCenter;
        pitch2EnvelopeLevel = PitchEnvelopeCenter;
        if (midiNoteReleased)
            midiNoteActive = false;
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

    inline int32_t morphRecipeWave(uint32_t phase, int32_t wave, int32_t bankControl)
    {
        uint8_t bank = controlToRecipeBank(bankControl);
        if (bank == 0u)
            return morphWave(phase, wave);

        uint32_t scaled = ((uint32_t)clamp12(wave) * 8u) >> 12;
        uint8_t slot = (uint8_t)(scaled > 7u ? 7u : scaled);
        return recipeWave(phase, bank, slot);
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

    inline int32_t blendRecipe(uint32_t phase, uint8_t primary, uint8_t secondary, uint16_t secondaryAmount)
    {
        int32_t a = czWave(phase, primary);
        int32_t b = czWave(phase, secondary);
        return a + (((b - a) * (int32_t)secondaryAmount) >> 12);
    }

    inline int32_t recipeWave(uint32_t phase, uint8_t bank, uint8_t slot)
    {
        if (bank == 1u)
        {
            static constexpr uint8_t primary[8] = {3, 3, 0, 1, 2, 4, 6, 7};
            static constexpr uint8_t secondary[8] = {0, 1, 3, 3, 3, 3, 3, 3};
            static constexpr uint16_t amount[8] = {768, 768, 1024, 1024, 1280, 1280, 1024, 1024};
            return blendRecipe(phase, primary[slot], secondary[slot], amount[slot]);
        }

        if (bank == 2u)
        {
            static constexpr uint8_t primary[8] = {5, 6, 7, 5, 6, 7, 5, 7};
            static constexpr uint8_t secondary[8] = {0, 1, 2, 4, 4, 4, 2, 1};
            static constexpr uint16_t amount[8] = {1792, 1792, 1792, 2304, 2304, 2560, 2816, 2816};
            return blendRecipe(phase, primary[slot], secondary[slot], amount[slot]);
        }

        static constexpr uint8_t primary[8] = {4, 6, 4, 7, 5, 2, 1, 6};
        static constexpr uint8_t secondary[8] = {3, 3, 2, 3, 4, 7, 5, 2};
        static constexpr uint16_t amount[8] = {2048, 2048, 2560, 2304, 2560, 2048, 2048, 2816};
        return blendRecipe(phase, primary[slot], secondary[slot], amount[slot]);
    }

    // =========================================================
    // GNARLY TURING COMPATIBILITY CLEANUP
    // =========================================================
    void clearTuringState()
    {
        pendingTuringMidiNoteOn = false;
        pendingTuringMidiNoteOff = false;
        turingMidiOutputEnabled = false;
        turingMidiOutputChannel = 0u;
    }

    void updateSynthLEDs(Switch mode, int32_t pd1, int32_t wave1, int32_t pd2, int32_t wave2)
    {
        const bool osc2Page = mode == Switch::Up;
        const bool performancePage = mode == Switch::Down;
        uint8_t bank = controlToRecipeBank(recipeBankControl);
        LedBrightness(0, performancePage ? ((bank & 0x01u) ? 4095 : 0) : osc2Page ? pd2 : pd1);
        LedBrightness(1, performancePage ? ((bank & 0x02u) ? 4095 : 0) : osc2Page ? wave2 : wave1);
        LedBrightness(2, bipolarControlBrightness(osc2IntervalControl));
        LedBrightness(3, osc2Ring);
        LedBrightness(4, osc2Noise);
        LedBrightness(5, mode == Switch::Up ? 0 : mode == Switch::Middle ? 2048 : 4095);
    }

    int32_t bipolarControlBrightness(int32_t control)
    {
        int32_t distance = clamp12(control) - 2048;
        if (distance < 0)
            distance = -distance;
        return clamp12(distance << 1);
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
            if (customEnvelopePersist[i])
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
            if (!customEnvelopePersist[slot])
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

    uint8_t controlToWaveFamily(int32_t control)
    {
        uint32_t family = ((uint32_t)clamp12(control) * 8u) >> 12;
        return (uint8_t)(family > 7u ? 7u : family);
    }

    uint8_t controlToRecipeBank(int32_t control)
    {
        uint32_t value = (uint32_t)clamp12(control);
        if (value < 832u)
            return 0u;
        if (value < 1856u)
            return 1u;
        if (value < 2880u)
            return 2u;
        return 3u;
    }

    int32_t waveFamilyToControl(uint8_t family)
    {
        if (family > 7u)
            family = 7u;
        return ((int32_t)family * 4095) / 7;
    }

    int32_t recipeBankToControl(uint8_t bank)
    {
        switch (bank)
        {
            case 0u: return 416;
            case 1u: return 1344;
            case 2u: return 2368;
            default: return 3488;
        }
    }

    void updateAltControls(int32_t main, int32_t x, int32_t y)
    {
        if (altMainPickedUp ||
            pickupAltControl(main, altMainEntry, recipeBankControl, altMainPickedUp))
        {
            recipeBankControl = main;
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
            pickupModeControl(main, synthMainEntry, osc2IntervalControl, synthMainPickedUp))
            osc2IntervalControl = main;

        if (synthXPickedUp ||
            pickupModeControl(x, synthXEntry, pd2Control, synthXPickedUp))
            pd2Control = x;

        if (synthYPickedUp ||
            pickupModeControl(y, synthYEntry, wave2Control, synthYPickedUp))
            wave2Control = y;
    }

    void updateOsc2ModeControls(int32_t main, int32_t x, int32_t y)
    {
        if (turingMainPickedUp ||
            pickupModeControl(main, turingMainEntry, pitchControl, turingMainPickedUp))
            pitchControl = main;

        if (turingXPickedUp ||
            pickupModeControl(x, turingXEntry, pdControl, turingXPickedUp))
            pdControl = x;

        if (turingYPickedUp ||
            pickupModeControl(y, turingYEntry, waveControl, turingYPickedUp))
            waveControl = y;
    }

    void applyMidiControlPickupResets(int32_t main, int32_t x, int32_t y)
    {
        if (midiResetSynthMainPickup)
        {
            synthMainPickedUp = false;
            synthMainEntry = main;
            midiResetSynthMainPickup = false;
        }

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

        if (midiResetTuringXPickup)
        {
            turingXPickedUp = false;
            turingXEntry = x;
            midiResetTuringXPickup = false;
        }

        if (midiResetTuringYPickup)
        {
            turingYPickedUp = false;
            turingYEntry = y;
            midiResetTuringYPickup = false;
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
    }

    void resetModePickup(Switch mode, Switch previousMode, int32_t main, int32_t x, int32_t y)
    {
        if (mode == Switch::Up && previousMode != Switch::Up)
            resetSynthPickup(main, x, y);
        else if (mode == Switch::Middle && previousMode != Switch::Middle)
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
        turingMainPickedUp = false;
        turingXPickedUp = false;
        turingYPickedUp = false;
        turingMainEntry = main;
        turingXEntry = x;
        turingYEntry = y;
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
        state.osc2IntervalControl = osc2IntervalControl;
        state.pdControl = pdControl;
        state.pd2Control = pd2Control;
        state.waveControl = waveControl;
        state.osc2Ring = clamp12(osc2Ring);
        state.osc2Noise = clamp12(osc2Noise);
        state.envelopePreset =
            envelopePreset < EnvelopePresetCount ?
            envelopePreset :
            (uint8_t)EnvelopePreset::Off;
        state.reserved[0] = controlToRecipeBank(recipeBankControl);
        state.reserved[1] = midiInChannel;
        state.reserved[2] =
            0x80u |
            ((uint8_t)controlToWaveFamily(wave2Control) << 4);
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
        {
            copyEnvelopeName(state.names[i], customEnvelopeNames[i]);
            state.performances[i] = customEnvelopeSavedPerformances[i];
            state.slots[i] = savedEnvelopeFromRuntime(customEnvelopeSaved[i]);
        }

        state.checksum = 0;
        state.checksum = checksumCustomEnvelopeState(state);
        return state;
    }

    SavedEnvelopeProgram savedEnvelopeFromRuntime(const EnvelopeProgram& runtime)
    {
        SavedEnvelopeProgram saved = {};
        for (uint32_t i = 0; i < 8u; ++i)
        {
            saved.amp[i] = runtime.amp[i];
            saved.pd[i] = runtime.pd[i];
            saved.pitch[i] = runtime.pitch[i];
            saved.pitch2[i] = runtime.pitch2[i];
            saved.pd2[i] = runtime.pd2[i];
            saved.amp2[i] = runtime.amp2[i];
        }
        for (uint32_t i = 0; i < 6u; ++i)
            saved.sustain[i] = sanitizeSustainStage(runtime.sustain[i]);

        return saved;
    }

    EnvelopeProgram runtimeEnvelopeFromSaved(const SavedEnvelopeProgram& saved)
    {
        EnvelopeProgram runtime = {};
        for (uint32_t i = 0; i < 8u; ++i)
        {
            runtime.amp[i] = saved.amp[i];
            runtime.pd[i] = saved.pd[i];
            runtime.pitch[i] = saved.pitch[i];
            runtime.pitch2[i] = saved.pitch2[i];
            runtime.pd2[i] = saved.pd2[i];
            runtime.amp2[i] = saved.amp2[i];
        }
        for (uint32_t i = 0; i < 6u; ++i)
            runtime.sustain[i] = sanitizeSustainStage(saved.sustain[i]);

        return runtime;
    }

    EnvelopeProgram runtimeEnvelopeFromSavedV5(const SavedEnvelopeProgramV5& saved)
    {
        EnvelopeProgram runtime = {};
        for (uint32_t i = 0; i < 8u; ++i)
        {
            runtime.amp[i] = saved.amp[i];
            runtime.pd[i] = saved.pd[i];
            runtime.pitch[i] = saved.pitch[i];
            runtime.pitch2[i] = saved.pitch2[i];
            runtime.pd2[i] = saved.pd2[i];
            runtime.amp2[i] = saved.amp2[i];
        }
        clearSustainStages(runtime);
        return runtime;
    }

    EnvelopeProgram runtimeEnvelopeFromSavedV4(const SavedEnvelopeProgramV4& saved)
    {
        EnvelopeProgram runtime = {};
        for (uint32_t i = 0; i < 8u; ++i)
        {
            runtime.amp[i] = saved.amp[i];
            runtime.pd[i] = saved.pd[i];
            runtime.pitch[i] = saved.pitch[i];
            runtime.pitch2[i] = saved.pitch2[i];
            runtime.pd2[i] = saved.pd2[i];
            runtime.amp2[i] = saved.amp[i];
        }
        clearSustainStages(runtime);

        return runtime;
    }

    EnvelopeProgram runtimeEnvelopeFromSavedV3(const SavedEnvelopeProgramV3& saved)
    {
        EnvelopeProgram runtime = {};
        for (uint32_t i = 0; i < 8u; ++i)
        {
            runtime.amp[i] = saved.amp[i];
            runtime.pd[i] = saved.pd[i];
            runtime.pitch[i] = saved.pitch[i];
            runtime.pitch2[i] = saved.pitch2[i];
            runtime.pd2[i] = saved.pd[i];
            runtime.amp2[i] = saved.amp[i];
        }
        clearSustainStages(runtime);

        return runtime;
    }

    uint8_t sanitizeSustainStage(uint8_t stage)
    {
        return stage < 8u ? stage : NoSustainStage;
    }

    void clearSustainStages(EnvelopeProgram& program)
    {
        for (uint32_t i = 0; i < 6u; ++i)
            program.sustain[i] = NoSustainStage;
    }

    uint8_t customEnvelopeMask()
    {
        uint8_t mask = 0;
        for (uint32_t i = 0; i < CustomEnvelopeSlotCount; ++i)
        {
            if (customEnvelopePersist[i])
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

    uint32_t checksumCustomEnvelopeStateV3(const SavedCustomEnvelopeStateV3& state)
    {
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&state);
        uint32_t checksum = 2166136261u;

        for (uint32_t i = 0; i < sizeof(SavedCustomEnvelopeStateV3) - sizeof(uint32_t); ++i)
        {
            checksum ^= bytes[i];
            checksum *= 16777619u;
        }

        return checksum;
    }

    uint32_t checksumCustomEnvelopeStateV4(const SavedCustomEnvelopeStateV4& state)
    {
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&state);
        uint32_t checksum = 2166136261u;

        for (uint32_t i = 0; i < sizeof(SavedCustomEnvelopeStateV4) - sizeof(uint32_t); ++i)
        {
            checksum ^= bytes[i];
            checksum *= 16777619u;
        }

        return checksum;
    }

    uint32_t checksumCustomEnvelopeStateV5(const SavedCustomEnvelopeStateV5& state)
    {
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&state);
        uint32_t checksum = 2166136261u;

        for (uint32_t i = 0; i < sizeof(SavedCustomEnvelopeStateV5) - sizeof(uint32_t); ++i)
        {
            checksum ^= bytes[i];
            checksum *= 16777619u;
        }

        return checksum;
    }

    uint32_t checksumCustomEnvelopeStateV7(const SavedCustomEnvelopeStateV7& state)
    {
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&state);
        uint32_t checksum = 2166136261u;

        for (uint32_t i = 0; i < sizeof(SavedCustomEnvelopeStateV7) - sizeof(uint32_t); ++i)
        {
            checksum ^= bytes[i];
            checksum *= 16777619u;
        }

        return checksum;
    }

    uint32_t checksumCustomEnvelopeStateV8(const SavedCustomEnvelopeStateV8& state)
    {
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&state);
        uint32_t checksum = 2166136261u;

        for (uint32_t i = 0; i < sizeof(SavedCustomEnvelopeStateV8) - sizeof(uint32_t); ++i)
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

    const SavedCustomEnvelopeStateV3& flashCustomEnvelopeStateV3()
    {
        return *reinterpret_cast<const SavedCustomEnvelopeStateV3*>(
            XIP_BASE + CustomEnvelopeFlashOffset);
    }

    const SavedCustomEnvelopeStateV4& flashCustomEnvelopeStateV4()
    {
        return *reinterpret_cast<const SavedCustomEnvelopeStateV4*>(
            XIP_BASE + CustomEnvelopeFlashOffset);
    }

    const SavedCustomEnvelopeStateV5& flashCustomEnvelopeStateV5()
    {
        return *reinterpret_cast<const SavedCustomEnvelopeStateV5*>(
            XIP_BASE + CustomEnvelopeFlashOffset);
    }

    const SavedCustomEnvelopeStateV7& flashCustomEnvelopeStateV7()
    {
        return *reinterpret_cast<const SavedCustomEnvelopeStateV7*>(
            XIP_BASE + CustomEnvelopeFlashOffset);
    }

    const SavedCustomEnvelopeStateV8& flashCustomEnvelopeStateV8()
    {
        return *reinterpret_cast<const SavedCustomEnvelopeStateV8*>(
            XIP_BASE + CustomEnvelopeFlashOffset);
    }

    bool isValidSavedState(const SavedPerformanceState& state)
    {
        return
            state.magic == SaveMagic &&
            state.version == SaveVersion &&
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

    bool isValidCustomEnvelopeStateV3(const SavedCustomEnvelopeStateV3& state)
    {
        return
            state.magic == CustomEnvelopeMagic &&
            state.version == LegacyCustomEnvelopeSaveVersion &&
            state.size == sizeof(SavedCustomEnvelopeStateV3) &&
            state.checksum == checksumCustomEnvelopeStateV3(state);
    }

    bool isValidCustomEnvelopeStateV4(const SavedCustomEnvelopeStateV4& state)
    {
        return
            state.magic == CustomEnvelopeMagic &&
            state.version == DualPdCustomEnvelopeSaveVersion &&
            state.size == sizeof(SavedCustomEnvelopeStateV4) &&
            state.checksum == checksumCustomEnvelopeStateV4(state);
    }

    bool isValidCustomEnvelopeStateV5(const SavedCustomEnvelopeStateV5& state)
    {
        return
            state.magic == CustomEnvelopeMagic &&
            state.version == DualAmpCustomEnvelopeSaveVersion &&
            state.size == sizeof(SavedCustomEnvelopeStateV5) &&
            state.checksum == checksumCustomEnvelopeStateV5(state);
    }

    bool isValidCustomEnvelopeStateV7(const SavedCustomEnvelopeStateV7& state)
    {
        return
            state.magic == CustomEnvelopeMagic &&
            state.version == NamedCustomEnvelopeSaveVersion &&
            state.size == sizeof(SavedCustomEnvelopeStateV7) &&
            state.checksum == checksumCustomEnvelopeStateV7(state);
    }

    bool isValidCustomEnvelopeStateV8(const SavedCustomEnvelopeStateV8& state)
    {
        return
            state.magic == CustomEnvelopeMagic &&
            state.version == SoundPresetCustomEnvelopeSaveVersion &&
            state.size == sizeof(SavedCustomEnvelopeStateV8) &&
            state.checksum == checksumCustomEnvelopeStateV8(state);
    }

    bool customEnvelopeStateMatches(
        const SavedCustomEnvelopeState& a,
        const SavedCustomEnvelopeState& b)
    {
        if (a.loadedMask != b.loadedMask)
            return false;

        const uint8_t* aNameBytes = reinterpret_cast<const uint8_t*>(a.names);
        const uint8_t* bNameBytes = reinterpret_cast<const uint8_t*>(b.names);
        for (uint32_t i = 0; i < sizeof(a.names); ++i)
        {
            if (aNameBytes[i] != bNameBytes[i])
                return false;
        }

        const uint8_t* aPerformanceBytes = reinterpret_cast<const uint8_t*>(a.performances);
        const uint8_t* bPerformanceBytes = reinterpret_cast<const uint8_t*>(b.performances);
        for (uint32_t i = 0; i < sizeof(a.performances); ++i)
        {
            if (aPerformanceBytes[i] != bPerformanceBytes[i])
                return false;
        }

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
            a.osc2IntervalControl == b.osc2IntervalControl &&
            a.pdControl == b.pdControl &&
            a.pd2Control == b.pd2Control &&
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
        osc2IntervalControl = clamp12(state.osc2IntervalControl);
        pdControl = clamp12(state.pdControl);
        pd2Control = clamp12(state.pd2Control);
        waveControl = clamp12(state.waveControl);
        wave2Control = waveFamilyToControl((state.reserved[2] >> 4) & 0x0Fu);
        recipeBankControl = recipeBankToControl(state.reserved[0] & 0x03u);
        osc2Ring = clamp12(state.osc2Ring);
        osc2Noise = clamp12(state.osc2Noise);
        midiInChannel = state.reserved[1] & 0x0Fu;
        turingMidiOutputEnabled = false;
        turingMidiOutputChannel = 0u;
        envelopePreset = state.envelopePreset < EnvelopePresetCount ?
            state.envelopePreset :
            (uint8_t)EnvelopePreset::Off;
    }

    void loadCustomEnvelopeState()
    {
        const SavedCustomEnvelopeState& state = flashCustomEnvelopeState();
        if (isValidCustomEnvelopeState(state))
        {
            for (uint32_t i = 0; i < CustomEnvelopeSlotCount; ++i)
            {
                customEnvelopeSaved[i] = runtimeEnvelopeFromSaved(state.slots[i]);
                customEnvelopes[i] = customEnvelopeSaved[i];
                copyEnvelopeName(customEnvelopeNames[i], state.names[i]);
                customEnvelopeSavedPerformances[i] = state.performances[i];
                customEnvelopePerformances[i] = customEnvelopeSavedPerformances[i];
                customEnvelopeLoaded[i] = (state.loadedMask & (1u << i)) != 0;
                customEnvelopePersist[i] = customEnvelopeLoaded[i];
            }

            return;
        }

        const SavedCustomEnvelopeStateV8& soundPresetState = flashCustomEnvelopeStateV8();
        if (isValidCustomEnvelopeStateV8(soundPresetState))
        {
            for (uint32_t i = 0; i < CustomEnvelopeSlotCount; ++i)
            {
                customEnvelopeSaved[i] = runtimeEnvelopeFromSaved(soundPresetState.slots[i]);
                customEnvelopes[i] = customEnvelopeSaved[i];
                copyEnvelopeName(customEnvelopeNames[i], soundPresetState.names[i]);
                customEnvelopeSavedPerformances[i] =
                    upgradeSlotPerformanceState(soundPresetState.performances[i]);
                customEnvelopePerformances[i] = customEnvelopeSavedPerformances[i];
                customEnvelopeLoaded[i] = (soundPresetState.loadedMask & (1u << i)) != 0;
                customEnvelopePersist[i] = customEnvelopeLoaded[i];
            }

            return;
        }

        const SavedCustomEnvelopeStateV7& namedState = flashCustomEnvelopeStateV7();
        if (isValidCustomEnvelopeStateV7(namedState))
        {
            for (uint32_t i = 0; i < CustomEnvelopeSlotCount; ++i)
            {
                customEnvelopeSaved[i] = runtimeEnvelopeFromSaved(namedState.slots[i]);
                customEnvelopes[i] = customEnvelopeSaved[i];
                copyEnvelopeName(customEnvelopeNames[i], namedState.names[i]);
                customEnvelopeSavedPerformances[i] = currentSlotPerformanceState();
                customEnvelopePerformances[i] = customEnvelopeSavedPerformances[i];
                customEnvelopeLoaded[i] = (namedState.loadedMask & (1u << i)) != 0;
                customEnvelopePersist[i] = customEnvelopeLoaded[i];
            }

            return;
        }

        const SavedCustomEnvelopeStateV5& dualAmpState = flashCustomEnvelopeStateV5();
        if (isValidCustomEnvelopeStateV5(dualAmpState))
        {
            for (uint32_t i = 0; i < CustomEnvelopeSlotCount; ++i)
            {
                customEnvelopeSaved[i] = runtimeEnvelopeFromSavedV5(dualAmpState.slots[i]);
                customEnvelopes[i] = customEnvelopeSaved[i];
                clearEnvelopeName(customEnvelopeNames[i]);
                customEnvelopeSavedPerformances[i] = currentSlotPerformanceState();
                customEnvelopePerformances[i] = customEnvelopeSavedPerformances[i];
                customEnvelopeLoaded[i] = (dualAmpState.loadedMask & (1u << i)) != 0;
                customEnvelopePersist[i] = customEnvelopeLoaded[i];
            }

            return;
        }

        const SavedCustomEnvelopeStateV4& dualPdState = flashCustomEnvelopeStateV4();
        if (isValidCustomEnvelopeStateV4(dualPdState))
        {
            for (uint32_t i = 0; i < CustomEnvelopeSlotCount; ++i)
            {
                customEnvelopeSaved[i] = runtimeEnvelopeFromSavedV4(dualPdState.slots[i]);
                customEnvelopes[i] = customEnvelopeSaved[i];
                clearEnvelopeName(customEnvelopeNames[i]);
                customEnvelopeSavedPerformances[i] = currentSlotPerformanceState();
                customEnvelopePerformances[i] = customEnvelopeSavedPerformances[i];
                customEnvelopeLoaded[i] = (dualPdState.loadedMask & (1u << i)) != 0;
                customEnvelopePersist[i] = customEnvelopeLoaded[i];
            }

            return;
        }

        const SavedCustomEnvelopeStateV3& legacyState = flashCustomEnvelopeStateV3();
        if (!isValidCustomEnvelopeStateV3(legacyState))
            return;

        for (uint32_t i = 0; i < CustomEnvelopeSlotCount; ++i)
        {
            customEnvelopeSaved[i] = runtimeEnvelopeFromSavedV3(legacyState.slots[i]);
            customEnvelopes[i] = customEnvelopeSaved[i];
            clearEnvelopeName(customEnvelopeNames[i]);
            customEnvelopeSavedPerformances[i] = currentSlotPerformanceState();
            customEnvelopePerformances[i] = customEnvelopeSavedPerformances[i];
            customEnvelopeLoaded[i] = (legacyState.loadedMask & (1u << i)) != 0;
            customEnvelopePersist[i] = customEnvelopeLoaded[i];
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

    uint32_t noise = 1;
    uint32_t noiseHoldCounter = 0;
    int32_t heldPdNoise = 0;
    int32_t heldPhaseNoise = 0;
    uint32_t ampEnvelopeSample = 0;
    uint32_t amp2EnvelopeSample = 0;
    uint32_t pdEnvelopeSample = 0;
    uint32_t pd2EnvelopeSample = 0;
    uint32_t pitchEnvelopeSample = 0;
    uint32_t pitch2EnvelopeSample = 0;
    int32_t ampEnvelopeLevel = 0;
    int32_t amp2EnvelopeLevel = 0;
    int32_t pdEnvelopeLevel = 0;
    int32_t pd2EnvelopeLevel = 0;
    int32_t pitchEnvelopeLevel = PitchEnvelopeCenter;
    int32_t pitch2EnvelopeLevel = PitchEnvelopeCenter;
    int32_t ampEnvelopeStartLevel = 0;
    int32_t amp2EnvelopeStartLevel = 0;
    int32_t pdEnvelopeStartLevel = 0;
    int32_t pd2EnvelopeStartLevel = 0;
    int32_t pitchEnvelopeStartLevel = PitchEnvelopeCenter;
    int32_t pitch2EnvelopeStartLevel = PitchEnvelopeCenter;
    uint8_t ampEnvelopeStage = 8;
    uint8_t amp2EnvelopeStage = 8;
    uint8_t pdEnvelopeStage = 8;
    uint8_t pd2EnvelopeStage = 8;
    uint8_t pitchEnvelopeStage = 8;
    uint8_t pitch2EnvelopeStage = 8;
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
    int32_t pd2Control = 0;
    int32_t waveControl = 0;
    int32_t wave2Control = 0;
    int32_t recipeBankControl = 0;
    int32_t smoothedFreq = 0;
    int32_t osc2Detune = 0;
    int32_t osc2IntervalControl = 2048;
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
    volatile bool midiResetSynthMainPickup = false;
    volatile bool midiResetSynthXPickup = false;
    volatile bool midiResetSynthYPickup = false;
    volatile bool midiResetTuringXPickup = false;
    volatile bool midiResetTuringYPickup = false;
    volatile bool midiResetAltMainPickup = false;
    volatile bool midiResetAltXPickup = false;
    volatile bool midiResetAltYPickup = false;
    EnvelopeProgram customEnvelopes[CustomEnvelopeSlotCount] = {};
    EnvelopeProgram customEnvelopeSaved[CustomEnvelopeSlotCount] = {};
    uint8_t customEnvelopeNames[CustomEnvelopeSlotCount][16] = {};
    SavedSlotPerformanceState customEnvelopePerformances[CustomEnvelopeSlotCount] = {};
    SavedSlotPerformanceState customEnvelopeSavedPerformances[CustomEnvelopeSlotCount] = {};
    bool customEnvelopeLoaded[CustomEnvelopeSlotCount] = {};
    bool customEnvelopePersist[CustomEnvelopeSlotCount] = {};
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
