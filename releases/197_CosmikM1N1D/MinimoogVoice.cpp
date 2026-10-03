#include "ComputerCard.h"
#include "MinimoogVoice_LUT.h"
#include "MinimoogVoice_WaveLUT.h"
#include "hardware/clocks.h"
#include "hardware/sync.h"
#include "pico/multicore.h"
#include "pico/time.h"
#include "tusb.h"
#include "usb_midi_host.h"

#include <array>

static constexpr uint8_t WebMidiManufacturer = 0x7Du;
static constexpr uint8_t WebMidiId[4] = {0x43u, 0x31u, 0x5Au, 0x33u}; // C1Z3
static constexpr uint8_t MinimoogMidiId[4] = {0x4Du, 0x4Eu, 0x56u, 0x31u}; // MNV1
static constexpr uint8_t MinimoogMidiCommandIdentityRequest = 0x01u;
static constexpr uint8_t MinimoogMidiCommandIdentityResponse = 0x02u;
static constexpr uint8_t MinimoogMidiCommandRequestSlots = 0x03u;
static constexpr uint8_t MinimoogMidiCommandSlotsResponse = 0x04u;
static constexpr uint8_t MinimoogMidiCommandRequestUser = 0x05u;
static constexpr uint8_t MinimoogMidiCommandUserResponse = 0x06u;
static constexpr uint8_t MinimoogMidiCommandSaveUser = 0x07u;
static constexpr uint8_t MinimoogMidiCommandRecall = 0x08u;
static constexpr uint8_t MinimoogMidiCommandDeleteUser = 0x09u;
static constexpr uint8_t MinimoogMidiCommandAck = 0x0Au;
static constexpr uint8_t MinimoogMidiCommandCaptureUser = 0x0Bu;
static constexpr uint8_t MinimoogMidiCommandSetVoice = 0x0Cu;
static constexpr uint8_t MinimoogMidiCommandRequestVoice = 0x0Du;
static constexpr uint8_t MinimoogMidiCommandVoiceResponse = 0x0Eu;
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

constexpr std::array<uint16_t, 577> makeLadderReciprocalQ15Table()
{
    std::array<uint16_t, 577> table = {};
    for (uint32_t i = 0; i < table.size(); ++i)
    {
        uint32_t denominatorQ15 = 32768u + i * 512u;
        table[i] = (uint16_t)((1u << 30) / denominatorQ15);
    }
    return table;
}

constexpr auto LadderReciprocalQ15Table = makeLadderReciprocalQ15Table();


class MinimoogVoice : public ComputerCard
{
public:

    MinimoogVoice()
    {
        phase1 = 0;
        phase2 = 0;

        noise = 1;

        loadPerformanceState();
        loadCustomEnvelopeState();
        loadUserPresetBank();
        applyFactoryPreset(0u);
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
        // This voice has no generated MIDI performance output. Keep the
        // inherited flags clear, then service only Web MIDI protocol replies.
        pendingTuringMidiNoteOn = false;
        pendingTuringMidiNoteOff = false;
        turingMidiNoteActive = false;

        sendPendingMinimoogResponse();
    }

    // =========================================================
    // AUDIO CALLBACK
    // =========================================================
    void ProcessSample() override
    {
        applyPendingMidiNote();

        int32_t pitchInputMillivolts = AudioIn1Millivolts();

        int32_t cv1 = CVIn1();
        int32_t cv2 = CVIn2();

        Switch mode = SwitchVal();

        int32_t main = KnobVal(Knob::Main);
        int32_t x    = KnobVal(Knob::X);
        int32_t y    = KnobVal(Knob::Y);

        applyMidiControlPickupResets(main, x, y);

        Switch previousMode = lastMode;

        if (!modePickupInitialized)
        {
            modePickupInitialized = true;
            resetMinimoogPagePickup(mode, main, x, y);
            resetSaveGesture();
            previousMode = mode;
            lastMode = mode;
        }

        if (mode != Switch::Down)
            downEditUnlocked = true;

        if (mode != previousMode)
            resetMinimoogPagePickup(mode, main, x, y);

        if (mode != Switch::Down && previousMode == Switch::Down)
            completeDownPress(main, x, y);
        lastMode = mode;

        // Middle is the playable voice, up is oscillator setup, and the held
        // down page is reserved for external oscillator and LFO performance.
        {
            updateStartupPresetSelection(mode, main);
            updateMinimoogHoldState(mode, previousMode);

            if (startupPresetSelectMode || presetSelectMode)
            {
                updatePresetSelection(main);
                outputPresetSelectionSilence();
                waveformFlashSamples++;
                updateMinimoogLeds(mode);
                return;
            }
            else if (mode == Switch::Up)
            {
                updateOscillatorControls(main, x, y);
            }
            else if (mode == Switch::Middle)
            {
                updateVoiceControls(main, x, y);
            }
            else if (downHoldSamples > ShortSwitchPressSamples)
            {
                if (!noisePanelActive)
                {
                    resetPagePickup(noisePagePickup, main, x, y);
                    noisePanelActive = true;
                }
                updateNoiseControls(main, x, y);
            }
            else
            {
                updateModulationControls(main, x, y);
            }

            bool pulse1GateHigh = PulseIn1();
            bool pulse2GateHigh = PulseIn2();
            bool gateHigh = pulse1GateHigh || pulse2GateHigh || midiNoteActive;
            bool adsGateRising = gateHigh && !adsGateWasHigh;
            adsGateWasHigh = gateHigh;
            gateAmplitude = updateAdsEnvelope(
                ampAds, gateHigh, adsGateRising, ampSustainControl, ampDecayReleaseEnabled);
            int32_t gateLevel = gateAmplitude;
            int32_t filterEnvelopeLevel = updateAdsEnvelope(
                filterAds, gateHigh, adsGateRising, filterSustainControl, filterDecayReleaseEnabled);

            if (gateHigh && !gateWasHigh)
                syncOscillators();
            gateWasHigh = gateHigh;

            int32_t noiseSignal = nextNoiseSample();
            int32_t internalLfo = nextInternalLfoSample();
            // The Model D-style MOD MIX crossfades two selected sources.
            // Defaults retain the previous External OSC 3/CV In 2 -> noise
            // behaviour, so stored voices keep their established sound.
            int32_t modulationPrimary = modulationPrimarySourceControl >= 2048
                ? filterEnvelopeLevel : cv2;
            int32_t modulationSecondary = modulationSecondarySourceControl < 1365
                ? noiseSignal : modulationSecondarySourceControl < 2730
                ? cv2 : internalLfo;
            int32_t modulationSource =
                (modulationPrimary * (4095 - modulationNoiseBlendControl) +
                 modulationSecondary * modulationNoiseBlendControl) >> 12;
            int32_t modulationDepth = clamp12(lfoDepthControl + midiModWheelControl);
            int32_t lfo = (modulationSource * modulationDepth) >> 12;
            int32_t pitchLfo = oscillatorModulationEnabled
                ? (lfo * (4095 - lfoDestinationControl)) >> 12 : 0;
            int32_t basePitchUnits = currentPitchUnits(pitchControl, pitchInputMillivolts);
            int32_t pitchUnits = glidePitchUnits(basePitchUnits) + pitchLfo;
            int32_t freq = pitchFrequency(pitchUnits);

            int32_t cutoffLfo = filterModulationEnabled
                ? (lfo * lfoDestinationControl) >> 12 : 0;
            int32_t contour = (filterEnvelopeLevel * contourControl) >> 12;
            int32_t keyboardTracking = filterKeyboardTrackingOffset(pitchUnits);
            int32_t cutoff = clamp12(
                filterCutoffControl + (cv1 << 1) + cutoffLfo + contour + keyboardTracking);

            int32_t wave1 = clamp12(waveControl);
            int32_t wave2 = clamp12(wave2Control);

            clearTuringState();

            outputSynthVoice(
                freq, wave1, wave2,
                AudioIn2(), noiseSignal, cutoff);

            outputExternalOscillatorPitch(pitchUnits);
            CVOut2((gateLevel * 2047) >> 12);
            PulseOut1(pulse1GateHigh);
            PulseOut2(false);

            waveformFlashSamples++;
            updateMinimoogLeds(mode);
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

    enum class MoogWave : uint8_t
    {
        Triangle,
        Sharktooth,
        Saw,
        Square,
        WideRectangle,
        NarrowPulse
    };

    enum class OscillatorSetupPage : uint8_t
    {
        Oscillator1,
        Oscillator2,
        ExternalOscillator3
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

    struct PanelPagePickup
    {
        int32_t mainEntry;
        int32_t xEntry;
        int32_t yEntry;
        bool mainPickedUp;
        bool xPickedUp;
        bool yPickedUp;
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
    static constexpr int32_t MainPitchSemitones = 7;
    static constexpr int32_t OscillatorFrequencySemitones = 7;
    static constexpr int32_t MainPitchCentreUnits = PitchUnitsPerOctave; // C3 at noon.
    static constexpr int32_t PitchBaseMidiNote = 36; // C2, matching C2PhaseIncrement.
    static constexpr int32_t PitchUnitsPerMillivoltQ16 = 268435; // 4096 / 1000 in Q16.
    static constexpr int32_t MillivoltsPerPitchUnitQ16 = 16000; // 1000 / 4096 in Q16.
    static constexpr int32_t PitchOffsetUnitsPerControlQ16 = 76458;
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
    static constexpr uint32_t SaveMagic = 0x4D4D5631u; // MMV1
    static constexpr uint16_t SaveVersion = 1;
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
    static constexpr uint32_t CustomEnvelopeMagic = 0x4D4D454Eu; // MMEN
    static constexpr uint16_t LegacyCustomEnvelopeSaveVersion = 3;
    static constexpr uint16_t DualPdCustomEnvelopeSaveVersion = 4;
    static constexpr uint16_t DualAmpCustomEnvelopeSaveVersion = 5;
    static constexpr uint16_t NamedCustomEnvelopeSaveVersion = 7;
    static constexpr uint16_t SoundPresetCustomEnvelopeSaveVersion = 8;
    static constexpr uint16_t SoundPresetPd2CustomEnvelopeSaveVersion = 9;
    static constexpr uint16_t CustomEnvelopeSaveVersion = 9;
    static constexpr uint32_t CustomEnvelopeFlashOffset =
        SaveFlashOffset - FLASH_SECTOR_SIZE;
    static constexpr uint32_t UserPresetFlashOffset =
        CustomEnvelopeFlashOffset - FLASH_SECTOR_SIZE;
    static constexpr uint32_t UserPresetMagic = 0x4D4E5650u; // MNVP
    static constexpr uint16_t UserPresetVersion = 10u;
    static constexpr uint32_t SaveHoldSamples = 384000u;
    static constexpr uint32_t SaveConfirmSamples = 48000u;
    static constexpr uint32_t PresetWarningSamples = 192000u; // Four seconds.
    static constexpr uint32_t PresetSelectHoldSamples = 240000u; // Five seconds.
    static constexpr uint32_t ShortSwitchPressSamples = 24000u; // Half a second.
    static constexpr uint8_t PresetSlotCount = 8u;
    static constexpr uint32_t AdsPhaseLimit = 1u << 24;
    static constexpr uint32_t AdsAttackMinimumSamples = 48u; // One millisecond.
    static constexpr uint32_t AdsDecayMinimumSamples = 192u; // Four milliseconds.
    static constexpr uint32_t AdsMaximumSamples = 480000u; // Ten seconds at 48 kHz.
    static constexpr uint32_t AdsReleaseIncrement = AdsPhaseLimit / 960u; // 20 ms de-click release.
    // LUTs are peak-normalised. Raise the triangle's lower perceived/RMS
    // level without reducing the deliberately muscular pulse family.
    static constexpr int32_t TriangleLevelCompensationQ12 = 6144;

    // This compact record contains every currently audible Minimoog voice
    // control. It is intentionally separate from the inherited C1ZZL3
    // performance and multi-envelope flash formats.
    struct VoicePreset
    {
        const char* name;
        int32_t pitch;
        int32_t osc2Interval;
        int32_t wave1;
        int32_t wave2;
        int32_t osc1Level;
        int32_t osc2Level;
        int32_t externalLevel;
        int32_t externalRange;
        int32_t filterCutoff;
        int32_t oscillatorMix;
        int32_t contour;
        int32_t resonance;
        int32_t externalOffset;
        int32_t lfoDepth;
        int32_t lfoDestination;
        int32_t osc1Range = 2048;
        int32_t osc2Range = 2048;
        int32_t noiseLevel = 0;
        int32_t noiseColour = 0;
        int32_t modulationNoiseBlend = 0;
        int32_t filterHighPass = 0;
        int32_t filterModulationEnabled = 4095;
        int32_t oscillatorModulationEnabled = 4095;
        int32_t modulationPrimarySource = 0;
        int32_t modulationSecondarySource = 0;
        int32_t lfoRate = 1024;
        int32_t lfoShape = 0;
    };

    struct SavedUserVoiceV2
    {
        int32_t pitch;
        int32_t osc2Interval;
        int32_t wave1;
        int32_t wave2;
        int32_t osc1Level;
        int32_t osc2Level;
        int32_t externalLevel;
        int32_t externalRole;
        int32_t filterCutoff;
        int32_t oscillatorMix;
        int32_t contour;
        int32_t resonance;
        int32_t externalOffset;
        int32_t lfoDepth;
        int32_t lfoDestination;
    };

    // Version 5 voice layout. Retained exactly so existing user voices can
    // migrate without losing their oscillator, filter, or contour settings.
    struct SavedUserVoiceV5
    {
        int32_t pitch;
        int32_t osc2Interval;
        int32_t wave1;
        int32_t wave2;
        int32_t osc1Level;
        int32_t osc2Level;
        int32_t externalLevel;
        int32_t externalRange;
        int32_t filterCutoff;
        int32_t oscillatorMix;
        int32_t contour;
        int32_t resonance;
        int32_t externalOffset;
        int32_t lfoDepth;
        int32_t lfoDestination;
        int32_t osc1Range;
        int32_t osc2Range;
    };

    // Version 6 voice layout, retained for migration from the passed noise
    // and level-mixer firmware.
    struct SavedUserVoiceV6
    {
        int32_t pitch;
        int32_t osc2Interval;
        int32_t wave1;
        int32_t wave2;
        int32_t osc1Level;
        int32_t osc2Level;
        int32_t externalLevel;
        int32_t externalRange;
        int32_t filterCutoff;
        int32_t oscillatorMix;
        int32_t contour;
        int32_t resonance;
        int32_t externalOffset;
        int32_t lfoDepth;
        int32_t lfoDestination;
        int32_t osc1Range;
        int32_t osc2Range;
        int32_t noiseLevel;
        int32_t noiseColour;
        int32_t modulationNoiseBlend;
    };

    // Version 7 voice layout. Retained so the passed filter-mode firmware's
    // user bank migrates into the Model D-style modulation-source layout.
    struct SavedUserVoiceV7
    {
        int32_t pitch;
        int32_t osc2Interval;
        int32_t wave1;
        int32_t wave2;
        int32_t osc1Level;
        int32_t osc2Level;
        int32_t externalLevel;
        int32_t externalRange;
        int32_t filterCutoff;
        int32_t oscillatorMix;
        int32_t contour;
        int32_t resonance;
        int32_t externalOffset;
        int32_t lfoDepth;
        int32_t lfoDestination;
        int32_t osc1Range;
        int32_t osc2Range;
        int32_t noiseLevel;
        int32_t noiseColour;
        int32_t modulationNoiseBlend;
        int32_t filterHighPass;
        int32_t filterModulationEnabled;
        int32_t oscillatorModulationEnabled;
    };

    // Version 8 adds the first Model D-style source selectors.
    struct SavedUserVoiceV8
    {
        int32_t pitch; int32_t osc2Interval; int32_t wave1; int32_t wave2;
        int32_t osc1Level; int32_t osc2Level; int32_t externalLevel;
        int32_t externalRange; int32_t filterCutoff; int32_t oscillatorMix;
        int32_t contour; int32_t resonance; int32_t externalOffset;
        int32_t lfoDepth; int32_t lfoDestination; int32_t osc1Range;
        int32_t osc2Range; int32_t noiseLevel; int32_t noiseColour;
        int32_t modulationNoiseBlend; int32_t filterHighPass;
        int32_t filterModulationEnabled; int32_t oscillatorModulationEnabled;
        int32_t modulationPrimarySource; int32_t modulationSecondarySource;
    };

    struct SavedUserVoice
    {
        int32_t pitch; int32_t osc2Interval; int32_t wave1; int32_t wave2;
        int32_t osc1Level; int32_t osc2Level; int32_t externalLevel;
        int32_t externalRange; int32_t filterCutoff; int32_t oscillatorMix;
        int32_t contour; int32_t resonance; int32_t externalOffset;
        int32_t lfoDepth; int32_t lfoDestination; int32_t osc1Range;
        int32_t osc2Range; int32_t noiseLevel; int32_t noiseColour;
        int32_t modulationNoiseBlend; int32_t filterHighPass;
        int32_t filterModulationEnabled; int32_t oscillatorModulationEnabled;
        int32_t modulationPrimarySource; int32_t modulationSecondarySource;
        int32_t lfoRate; int32_t lfoShape;
        int32_t glide = 0;
    };

    // Version 9 is the passed internal-LFO/CC1 layout. Keep it intact so
    // stored user voices acquire the new glide control at zero rather than
    // changing their response when this firmware first boots.
    struct SavedUserVoiceV9
    {
        int32_t pitch; int32_t osc2Interval; int32_t wave1; int32_t wave2;
        int32_t osc1Level; int32_t osc2Level; int32_t externalLevel;
        int32_t externalRange; int32_t filterCutoff; int32_t oscillatorMix;
        int32_t contour; int32_t resonance; int32_t externalOffset;
        int32_t lfoDepth; int32_t lfoDestination; int32_t osc1Range;
        int32_t osc2Range; int32_t noiseLevel; int32_t noiseColour;
        int32_t modulationNoiseBlend; int32_t filterHighPass;
        int32_t filterModulationEnabled; int32_t oscillatorModulationEnabled;
        int32_t modulationPrimarySource; int32_t modulationSecondarySource;
        int32_t lfoRate; int32_t lfoShape;
    };

    struct SavedUserContourV3
    {
        int32_t ampAttack;
        int32_t ampDecay;
        int32_t ampSustain;
        int32_t filterAttack;
        int32_t filterDecay;
        int32_t filterSustain;
        int32_t filterKeyboardTracking;
    };

    struct SavedUserContourV4
    {
        int32_t ampAttack; int32_t ampDecay; int32_t ampSustain;
        int32_t filterAttack; int32_t filterDecay; int32_t filterSustain;
        int32_t filterKeyboardTracking; int32_t decayRelease;
    };

    // The Model D's Decay switch makes the gate-off fall use the contour's
    // decay time. One shared switch is a practical fit for this card's UI.
    struct SavedUserContour
    {
        int32_t ampAttack;
        int32_t ampDecay;
        int32_t ampSustain;
        int32_t filterAttack;
        int32_t filterDecay;
        int32_t filterSustain;
        int32_t filterKeyboardTracking;
        int32_t ampDecayRelease;
        int32_t filterDecayRelease;
    };

    // Version 1 did not contain contour controls. Keep its exact layout so
    // voices saved by the passed Web-sync firmware migrate on first boot.
    struct SavedUserPresetBankV1
    {
        uint32_t magic;
        uint16_t version;
        uint16_t size;
        uint8_t loadedMask;
        uint8_t reserved[7];
        uint8_t names[PresetSlotCount][16];
        SavedUserVoiceV2 voices[PresetSlotCount];
        uint32_t checksum;
    };

    struct SavedUserPresetBankV2
    {
        uint32_t magic;
        uint16_t version;
        uint16_t size;
        uint8_t loadedMask;
        uint8_t reserved[7];
        uint8_t names[PresetSlotCount][16];
        SavedUserVoiceV2 voices[PresetSlotCount];
        SavedUserContourV3 contours[PresetSlotCount];
        uint32_t checksum;
    };

    struct SavedUserPresetBankV3
    {
        uint32_t magic;
        uint16_t version;
        uint16_t size;
        uint8_t loadedMask;
        uint8_t reserved[7];
        uint8_t names[PresetSlotCount][16];
        SavedUserVoiceV5 voices[PresetSlotCount];
        SavedUserContourV3 contours[PresetSlotCount];
        uint32_t checksum;
    };

    struct SavedUserPresetBankV4
    {
        uint32_t magic; uint16_t version; uint16_t size; uint8_t loadedMask;
        uint8_t reserved[7]; uint8_t names[PresetSlotCount][16];
        SavedUserVoiceV5 voices[PresetSlotCount];
        SavedUserContourV4 contours[PresetSlotCount];
        uint32_t checksum;
    };

    struct SavedUserPresetBankV5
    {
        uint32_t magic;
        uint16_t version;
        uint16_t size;
        uint8_t loadedMask;
        uint8_t reserved[7];
        uint8_t names[PresetSlotCount][16];
        SavedUserVoiceV5 voices[PresetSlotCount];
        SavedUserContour contours[PresetSlotCount];
        uint32_t checksum;
    };

    struct SavedUserPresetBankV6
    {
        uint32_t magic;
        uint16_t version;
        uint16_t size;
        uint8_t loadedMask;
        uint8_t reserved[7];
        uint8_t names[PresetSlotCount][16];
        SavedUserVoiceV6 voices[PresetSlotCount];
        SavedUserContour contours[PresetSlotCount];
        uint32_t checksum;
    };

    struct SavedUserPresetBankV7
    {
        uint32_t magic;
        uint16_t version;
        uint16_t size;
        uint8_t loadedMask;
        uint8_t reserved[7];
        uint8_t names[PresetSlotCount][16];
        SavedUserVoiceV7 voices[PresetSlotCount];
        SavedUserContour contours[PresetSlotCount];
        uint32_t checksum;
    };

    struct SavedUserPresetBankV8
    {
        uint32_t magic;
        uint16_t version;
        uint16_t size;
        uint8_t loadedMask;
        uint8_t reserved[7];
        uint8_t names[PresetSlotCount][16];
        SavedUserVoiceV8 voices[PresetSlotCount];
        SavedUserContour contours[PresetSlotCount];
        uint32_t checksum;
    };

    struct SavedUserPresetBankV9
    {
        uint32_t magic;
        uint16_t version;
        uint16_t size;
        uint8_t loadedMask;
        uint8_t reserved[7];
        uint8_t names[PresetSlotCount][16];
        SavedUserVoiceV9 voices[PresetSlotCount];
        SavedUserContour contours[PresetSlotCount];
        uint32_t checksum;
    };

    struct SavedUserPresetBank
    {
        uint32_t magic;
        uint16_t version;
        uint16_t size;
        uint8_t loadedMask;
        uint8_t reserved[7];
        uint8_t names[PresetSlotCount][16];
        SavedUserVoice voices[PresetSlotCount];
        SavedUserContour contours[PresetSlotCount];
        uint32_t checksum;
    };

    enum class AdsStage : uint8_t { Attack, Decay, Sustain, Release };

    struct AdsEnvelopeState
    {
        int32_t level = 0;
        int32_t startLevel = 0;
        uint32_t phase = 0;
        uint32_t attackIncrement = 1;
        uint32_t decayIncrement = 1;
        AdsStage stage = AdsStage::Release;
    };

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
        int32_t freq = pitchFrequency(glidePitchUnits(pitchUnits(pitchControl, 0)));
        int32_t wave1 = clamp12(waveControl);
        int32_t wave2 = clamp12(wave2Control);

        outputSynthVoice(
            freq, wave1, wave2,
            0, 0, filterCutoffControl);
    }

    void outputSynthVoice(
        int32_t freq,
        int32_t wave1,
        int32_t wave2,
        int32_t externalOscillator,
        int32_t noiseSignal,
        int32_t cutoff)
    {
        int32_t freq1 = applyPipeRange(freq, osc1RangeControl);
        int32_t osc1 = oscMoog(phase1, freq1, controlToMoogWave(wave1));

        int32_t freq2 = applyDetune(
            applyOsc2BaseInterval(applyPipeRange(freq, osc2RangeControl)), osc2Detune);
        int32_t osc2 = oscMoog(phase2, freq2, controlToMoogWave(wave2));

        int32_t sharedScale = 4095;
        sharedScale = (sharedScale * updateSyncFade()) >> 12;
        sharedScale = (sharedScale * updateLoopFade()) >> 12;
        int32_t ampScale1 = (gateAmplitude * sharedScale) >> 12;
        int32_t ampScale2 = ampScale1;
        osc1 = (osc1 * ampScale1) >> 12;
        osc2 = (osc2 * ampScale2) >> 12;
        externalOscillator = (externalOscillator * ampScale1) >> 12;
        osc1 = (osc1 * osc1LevelControl) >> 12;
        osc2 = (osc2 * osc2LevelControl) >> 12;
        externalOscillator =
            (externalOscillator * externalOscillatorLevelControl) >> 12;

        int32_t mixed = mixOscillatorSources(osc1, osc2, externalOscillator);
        int32_t gatedNoise = (noiseSignal * ampScale1) >> 12;
        if (noiseColourControl >= 2048)
            gatedNoise <<= 1;
        mixed += (gatedNoise * noiseLevelControl) >> 12;
        mixed = warmMixerSaturation(mixed);
        int32_t lowPass = ladderFilterSample(mixed, cutoff, filterResonanceControl);
        int32_t filtered = filterHighPassControl ? clip(mixed - lowPass) : lowPass;

        AudioOut1(filtered);
        AudioOut2(clip(mixed));
    }

    int32_t mixOscillatorSources(
        int32_t osc1,
        int32_t osc2,
        int32_t externalOscillator)
    {
        // Each source now has its own mixer level. The former three-source
        // scan is deliberately retired so no hidden balance can mute OSC 3.
        return osc1 + osc2 + externalOscillator;
    }

    MoogWave controlToMoogWave(int32_t control)
    {
        uint32_t index =
            ((uint32_t)clamp12(control) * MINIMOOG_WAVE_COUNT) >> 12;
        if (index >= MINIMOOG_WAVE_COUNT)
            index = MINIMOOG_WAVE_COUNT - 1u;
        return (MoogWave)index;
    }

    int32_t oscMoog(uint32_t& phase, int32_t frequency, MoogWave wave)
    {
        phase += (uint32_t)frequency;
        uint32_t band = waveBandForFrequency(frequency);
        uint32_t index = (phase >> 23) & (MINIMOOG_WAVE_TABLE_SIZE - 1u);
        uint32_t fraction = (phase >> 14) & (MINIMOOG_WAVE_TABLE_SIZE - 1u);
        uint32_t nextIndex = (index + 1u) & (MINIMOOG_WAVE_TABLE_SIZE - 1u);
        uint32_t waveIndex = (uint32_t)wave;
        int32_t a = minimoogWaveLUT[waveIndex][band][index];
        int32_t b = minimoogWaveLUT[waveIndex][band][nextIndex];

        int32_t sample = a + (((b - a) * (int32_t)fraction) >> 9);
        if (wave == MoogWave::Triangle)
            sample = (sample * TriangleLevelCompensationQ12) >> 12;
        return sample;
    }

    uint32_t waveBandForFrequency(int32_t frequency)
    {
        if (frequency <= 0)
            return 0;

        uint32_t phaseIncrement = (uint32_t)frequency;
        for (uint32_t band = 0; band < MINIMOOG_WAVE_BAND_COUNT; ++band)
        {
            if (phaseIncrement <= minimoogWaveMaxFrequency[band])
                return band;
        }

        return MINIMOOG_WAVE_BAND_COUNT - 1u;
    }

    int32_t warmMixerSaturation(int32_t input)
    {
        int32_t driven = input + (input >> 1);
        if (driven > 1400)
            return 1400 + ((driven - 1400) >> 2);
        if (driven < -1400)
            return -1400 + ((driven + 1400) >> 2);
        return driven;
    }

    int32_t ladderFilterSample(int32_t input, int32_t cutoff, int32_t resonance)
    {
        // A four-pole zero-delay-feedback ladder. The global feedback path is
        // solved through an interpolated reciprocal table, so the audio loop
        // never performs a general division.
        int32_t cutoffControl = clamp12(cutoff);
        int32_t g = 512 + ((cutoffControl * cutoffControl) >> 10);

        int32_t oneMinusG = 32768 - g;
        int32_t constant = (oneMinusG * ladderStage1) >> 15;
        constant = ((g * constant) >> 15) +
            ((oneMinusG * ladderStage2) >> 15);
        constant = ((g * constant) >> 15) +
            ((oneMinusG * ladderStage3) >> 15);
        constant = ((g * constant) >> 15) +
            ((oneMinusG * ladderStage4) >> 15);

        int32_t g2 = (g * g) >> 15;
        int32_t g4 = (g2 * g2) >> 15;
        // Four is the practical upper boundary for this four-pole ladder.
        // The extreme of the control reaches self-oscillation; normal musical
        // resonance occupies the rest of the range.
        int32_t resonanceQ12 = clamp12(resonance) << 2;
        int32_t denominatorQ15 = 32768 +
            ((resonanceQ12 * g4) >> 12);
        int32_t driven = input - ((resonanceQ12 * constant) >> 12);
        driven = (driven * ladderReciprocalQ15(denominatorQ15)) >> 15;
        driven = transistorLadderSaturation(driven + (driven >> 3));

        driven = ladderFilterStage(driven, g, ladderStage1);
        driven = ladderFilterStage(driven, g, ladderStage2);
        driven = ladderFilterStage(driven, g, ladderStage3);
        ladderFilterStage(driven, g, ladderStage4);

        return clip(transistorLadderSaturation(ladderStage4));
    }

    int32_t ladderFilterStage(int32_t input, int32_t g, int32_t& state)
    {
        int32_t integrator = ((input - state) * g) >> 15;
        int32_t output = state + integrator;
        state = transistorLadderSaturation(output + integrator);
        return output;
    }

    int32_t ladderReciprocalQ15(int32_t denominatorQ15)
    {
        if (denominatorQ15 < 32768)
            denominatorQ15 = 32768;
        if (denominatorQ15 > 327680)
            denominatorQ15 = 327680;

        uint32_t offset = (uint32_t)(denominatorQ15 - 32768);
        uint32_t index = offset >> 9;
        uint32_t fraction = offset & 0x1FFu;
        if (index >= 576u)
            return LadderReciprocalQ15Table[576];

        int32_t a = LadderReciprocalQ15Table[index];
        int32_t b = LadderReciprocalQ15Table[index + 1u];
        return a + (((b - a) * (int32_t)fraction) >> 9);
    }

    int32_t transistorLadderSaturation(int32_t value)
    {
        // A gentle cubic approximation of the transistor pair's decreasing
        // transconductance. It is symmetric, monotonic in the useful range,
        // and leaves more harmonic motion than a hard clip.
        if (value > 4095)
            value = 4095;
        else if (value < -4095)
            value = -4095;

        int32_t magnitude = value < 0 ? -value : value;
        int32_t curvature = (magnitude * magnitude) >> 12;
        return value - ((value * curvature) >> 13);
    }

    uint32_t adsIncrement(int32_t control, uint32_t minimumSamples)
    {
        // This division occurs only when a preset or WebMIDI voice changes;
        // the audio callback advances precomputed fixed-point increments.
        uint32_t samples = minimumSamples +
            (clamp12(control) * (AdsMaximumSamples - minimumSamples)) / 4095u;
        uint32_t increment = AdsPhaseLimit / samples;
        return increment == 0u ? 1u : increment;
    }

    void refreshAdsTiming()
    {
        ampAds.attackIncrement = adsIncrement(ampAttackControl, AdsAttackMinimumSamples);
        ampAds.decayIncrement = adsIncrement(ampDecayControl, AdsDecayMinimumSamples);
        filterAds.attackIncrement = adsIncrement(filterAttackControl, AdsAttackMinimumSamples);
        filterAds.decayIncrement = adsIncrement(filterDecayControl, AdsDecayMinimumSamples);
    }

    int32_t interpolateAds(int32_t start, int32_t target, uint32_t phase) const
    {
        return start + (int32_t)(((int64_t)(target - start) * phase) >> 24);
    }

    uint32_t adsCurve(uint32_t phase) const
    {
        if (phase >= AdsPhaseLimit)
            return AdsPhaseLimit;

        // A restrained capacitor-style curve: fast enough to feel immediate,
        // but with a soft approach to the target rather than a linear ramp.
        uint32_t remaining = AdsPhaseLimit - phase;
        return phase + (uint32_t)(((uint64_t)phase * remaining) >> 25);
    }

    int32_t updateAdsEnvelope(
        AdsEnvelopeState& envelope,
        bool gateHigh,
        bool gateRising,
        int32_t sustain,
        bool decayRelease)
    {
        if (gateRising)
        {
            envelope.startLevel = envelope.level;
            envelope.phase = 0;
            envelope.stage = AdsStage::Attack;
        }
        else if (!gateHigh && envelope.stage != AdsStage::Release)
        {
            envelope.startLevel = envelope.level;
            envelope.phase = 0;
            envelope.stage = AdsStage::Release;
        }

        if (envelope.stage == AdsStage::Attack)
        {
            envelope.level = interpolateAds(
                envelope.startLevel, 4095, adsCurve(envelope.phase));
            envelope.phase += envelope.attackIncrement;
            if (envelope.phase >= AdsPhaseLimit)
            {
                envelope.level = 4095;
                envelope.startLevel = 4095;
                envelope.phase = 0;
                envelope.stage = AdsStage::Decay;
            }
        }
        else if (envelope.stage == AdsStage::Decay)
        {
            int32_t target = clamp12(sustain);
            envelope.level = interpolateAds(
                envelope.startLevel, target, adsCurve(envelope.phase));
            envelope.phase += envelope.decayIncrement;
            if (envelope.phase >= AdsPhaseLimit)
            {
                envelope.level = target;
                envelope.phase = 0;
                envelope.stage = AdsStage::Sustain;
            }
        }
        else if (envelope.stage == AdsStage::Sustain)
        {
            envelope.level = clamp12(sustain);
        }
        else
        {
            envelope.level = interpolateAds(envelope.startLevel, 0, envelope.phase);
            envelope.phase += decayRelease ? envelope.decayIncrement : AdsReleaseIncrement;
            if (envelope.phase >= AdsPhaseLimit)
            {
                envelope.level = 0;
                envelope.startLevel = 0;
                envelope.phase = 0;
            }
        }
        return envelope.level;
    }

    int32_t filterKeyboardTrackingOffset(int32_t pitchUnits) const
    {
        // The Web UI uses the Model D's None, 1/3, 2/3, and Full settings.
        // Exact octave scaling follows in the filter-control-law pass.
        return ((pitchUnits - MainPitchCentreUnits) * filterKeyboardTrackingControl) >> 14;
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
             OscillatorFrequencySemitones * 4096) / 2047;

        return applySemitoneOffsetToFrequency(freq, semitoneQ12);
    }

    int32_t pipeRangeSemitones(int32_t control)
    {
        int32_t index = (clamp12(control) + 512) >> 10;
        if (index > 4)
            index = 4;
        return (index - 2) * 12;
    }

    int32_t applyPipeRange(int32_t freq, int32_t control)
    {
        return applySemitoneOffsetToFrequency(
            freq, pipeRangeSemitones(control) * 4096);
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
        uint32_t frac = tablePosition & 0x0FFFu;
        if (index >= 48u)
        {
            index = 47u;
            frac = 4096u;
        }
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
            if (midiData[0] == MidiCcModWheelOsc1Pd)
            {
                midiModWheelControl = midiCcToControl(midiData[1]);
            }
            else if (midiData[0] == MidiCcOsc1Pd)
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

    int32_t currentPitchUnits(int32_t knob, int32_t pitchInputMillivolts)
    {
        if (!midiNoteActive)
            return pitchUnits(knob, pitchInputMillivolts);

        int32_t units = midiNotePitchUnits(midiNote);
        units += (midiPitchBend * PitchUnitsPerOctave) / (8192 * 6);
        return units;
    }

    int32_t midiNotePitchUnits(uint8_t note)
    {
        return ((int32_t)note - 36) * PitchUnitsPerOctave / 12;
    }

    void handleWebMidiSysex()
    {
        if (minimoogMidiHeaderMatches())
        {
            handleMinimoogMidiSysex();
            return;
        }
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

    bool minimoogMidiHeaderMatches()
    {
        if (sysexLength < 6u || sysexBuffer[0] != WebMidiManufacturer) return false;
        for (uint32_t i = 0; i < 4u; ++i) if (sysexBuffer[1u + i] != MinimoogMidiId[i]) return false;
        return true;
    }

    bool sendMinimoogMidi(uint8_t command, const uint8_t* payload, uint32_t length)
    {
        uint8_t frame[192] = {0xF0u, WebMidiManufacturer, MinimoogMidiId[0], MinimoogMidiId[1], MinimoogMidiId[2], MinimoogMidiId[3], command};
        if (length > sizeof(frame) - 8u) return false;
        for (uint32_t i = 0; i < length; ++i) frame[7u + i] = payload[i] & 0x7Fu;
        frame[7u + length] = 0xF7u;
        return tud_midi_stream_write(0, frame, length + 8u) == length + 8u;
    }

    void queueMinimoogResponse(uint8_t command, const uint8_t* payload, uint32_t length)
    {
        if (length > sizeof(minimoogResponsePayload) || minimoogResponsePending) return;
        minimoogResponseCommand = command;
        minimoogResponseLength = length;
        minimoogResponseIndex = 0;
        minimoogResponsePhase = 0;
        for (uint32_t i = 0; i < length; ++i) minimoogResponsePayload[i] = payload[i] & 0x7Fu;
        minimoogResponsePending = true;
    }

    void sendPendingMinimoogResponse()
    {
        if (!minimoogResponsePending) return;
        uint8_t controller = minimoogResponsePhase == 0 ? 119u : minimoogResponsePhase == 1 ? 118u : 117u;
        uint8_t value = minimoogResponsePhase == 0 ? minimoogResponseCommand : minimoogResponsePhase == 1 ? minimoogResponsePayload[minimoogResponseIndex] : 0u;
        uint8_t message[] = {0xBFu, controller, value};
        if (tud_midi_stream_write(0, message, sizeof(message)) != sizeof(message)) return;
        if (minimoogResponsePhase == 0) minimoogResponsePhase = 1;
        else if (minimoogResponsePhase == 1 && ++minimoogResponseIndex >= minimoogResponseLength) minimoogResponsePhase = 2;
        else if (minimoogResponsePhase == 2) minimoogResponsePending = false;
    }

    void appendMinimoogVoice(uint8_t* payload, uint32_t& offset, const SavedUserVoice& voice)
    {
        const int32_t values[] = {voice.pitch, voice.osc2Interval, voice.wave1, voice.wave2, voice.osc1Level, voice.osc2Level, voice.externalLevel, voice.externalRange, voice.filterCutoff, voice.oscillatorMix, voice.contour, voice.resonance, voice.externalOffset, voice.lfoDepth, voice.lfoDestination, voice.osc1Range, voice.osc2Range, voice.noiseLevel, voice.noiseColour, voice.modulationNoiseBlend, voice.filterHighPass, voice.filterModulationEnabled, voice.oscillatorModulationEnabled, voice.modulationPrimarySource, voice.modulationSecondarySource, voice.lfoRate, voice.lfoShape, voice.glide};
        for (int32_t value : values) { uint16_t safe = clamp12(value); payload[offset++] = safe & 0x7Fu; payload[offset++] = safe >> 7; }
    }

    void appendMinimoogContour(uint8_t* payload, uint32_t& offset, const SavedUserContour& contour)
    {
        const int32_t values[] = {contour.ampAttack, contour.ampDecay, contour.ampSustain,
            contour.filterAttack, contour.filterDecay, contour.filterSustain,
            contour.filterKeyboardTracking, contour.ampDecayRelease,
            contour.filterDecayRelease};
        for (int32_t value : values) { uint16_t safe = clamp12(value); payload[offset++] = safe & 0x7Fu; payload[offset++] = safe >> 7; }
    }

    SavedUserVoice readMinimoogVoice(uint32_t& offset)
    {
        int32_t values[28] = {};
        for (uint32_t i = 0; i < 28u; ++i) { values[i] = (sysexBuffer[offset] & 0x7Fu) | ((sysexBuffer[offset + 1u] & 0x7Fu) << 7); offset += 2u; }
        return {values[0], values[1], values[2], values[3], values[4], values[5], values[6], values[7], values[8], values[9], values[10], values[11], values[12], values[13], values[14], values[15], values[16], values[17], values[18], values[19], values[20], values[21], values[22], values[23], values[24], values[25], values[26], values[27]};
    }

    SavedUserContour readMinimoogContour(uint32_t& offset)
    {
        int32_t values[9] = {};
        for (uint32_t i = 0; i < 9u; ++i) { values[i] = (sysexBuffer[offset] & 0x7Fu) | ((sysexBuffer[offset + 1u] & 0x7Fu) << 7); offset += 2u; }
        return {values[0], values[1], values[2], values[3], values[4], values[5], values[6], values[7], values[8]};
    }

    void handleMinimoogMidiSysex()
    {
        uint8_t command = sysexBuffer[5];
        if (command == MinimoogMidiCommandIdentityRequest && sysexLength == 6u)
        {
            uint8_t payload[] = {10u, userPresetBank.loadedMask, activePresetBank, activePresetSlot};
            queueMinimoogResponse(MinimoogMidiCommandIdentityResponse, payload, sizeof(payload));
            return;
        }
        if (command == MinimoogMidiCommandRequestSlots && sysexLength == 6u)
        {
            uint8_t payload[129] = {userPresetBank.loadedMask}; uint32_t offset = 1;
            for (uint32_t slot = 0; slot < PresetSlotCount; ++slot) for (uint32_t i = 0; i < 16u; ++i) payload[offset++] = userPresetBank.names[slot][i];
            queueMinimoogResponse(MinimoogMidiCommandSlotsResponse, payload, offset); return;
        }
        if (command == MinimoogMidiCommandRequestUser && sysexLength == 7u)
        {
            uint8_t slot = sysexBuffer[6] & 0x07u; if (!(userPresetBank.loadedMask & (1u << slot))) return;
            uint8_t payload[91] = {slot}; uint32_t offset = 1;
            for (uint32_t i = 0; i < 16u; ++i) payload[offset++] = userPresetBank.names[slot][i];
            appendMinimoogVoice(payload, offset, userPresetBank.voices[slot]);
            appendMinimoogContour(payload, offset, userPresetBank.contours[slot]);
            queueMinimoogResponse(MinimoogMidiCommandUserResponse, payload, offset); return;
        }
        if (command == MinimoogMidiCommandSaveUser && sysexLength == 57u)
        {
            uint8_t slot = sysexBuffer[6] & 0x07u; uint32_t offset = 7;
            for (uint32_t i = 0; i < 16u; ++i) userPresetBank.names[slot][i] = sysexBuffer[offset++] & 0x7Fu;
            userPresetBank.voices[slot] = readMinimoogVoice(offset);
            userPresetBank.contours[slot] = currentUserContour();
            userPresetBank.loadedMask |= 1u << slot; saveUserPresetBank();
            uint8_t payload[] = {MinimoogMidiCommandSaveUser, slot, userPresetBank.loadedMask}; queueMinimoogResponse(MinimoogMidiCommandAck, payload, sizeof(payload)); return;
        }
        if (command == MinimoogMidiCommandRecall && sysexLength == 8u)
        {
            uint8_t bank = sysexBuffer[6] & 1u, slot = sysexBuffer[7] & 0x07u;
            bool recalled = recallPreset(bank, slot, SwitchVal(), KnobVal(Knob::Main), KnobVal(Knob::X), KnobVal(Knob::Y));
            uint8_t payload[] = {MinimoogMidiCommandRecall, bank, slot, (uint8_t)(recalled ? 1u : 0u)};
            queueMinimoogResponse(MinimoogMidiCommandAck, payload, sizeof(payload)); return;
        }
        if (command == MinimoogMidiCommandDeleteUser && sysexLength == 7u)
        {
            uint8_t slot = sysexBuffer[6] & 0x07u; userPresetBank.loadedMask &= ~(1u << slot);
            for (uint32_t i = 0; i < 16u; ++i) userPresetBank.names[slot][i] = 0;
            saveUserPresetBank();
            uint8_t payload[] = {MinimoogMidiCommandDeleteUser, slot, userPresetBank.loadedMask}; queueMinimoogResponse(MinimoogMidiCommandAck, payload, sizeof(payload)); return;
        }
        if (command == MinimoogMidiCommandCaptureUser && sysexLength == 23u)
        {
            uint8_t slot = sysexBuffer[6] & 0x07u;
            for (uint32_t i = 0; i < 16u; ++i) userPresetBank.names[slot][i] = sysexBuffer[7u + i] & 0x7Fu;
            userPresetBank.voices[slot] = currentUserVoice();
            userPresetBank.contours[slot] = currentUserContour();
            userPresetBank.loadedMask |= 1u << slot; saveUserPresetBank();
            uint8_t payload[] = {MinimoogMidiCommandCaptureUser, slot, userPresetBank.loadedMask}; queueMinimoogResponse(MinimoogMidiCommandAck, payload, sizeof(payload));
            return;
        }
        if (command == MinimoogMidiCommandSetVoice && sysexLength == 80u)
        {
            uint32_t offset = 6;
            applyUserVoice(readMinimoogVoice(offset));
            applyUserContour(readMinimoogContour(offset));
            resetMinimoogPagePickup(SwitchVal(), KnobVal(Knob::Main), KnobVal(Knob::X), KnobVal(Knob::Y));
            uint8_t payload[] = {MinimoogMidiCommandSetVoice, 0u, userPresetBank.loadedMask}; queueMinimoogResponse(MinimoogMidiCommandAck, payload, sizeof(payload));
            return;
        }
        if (command == MinimoogMidiCommandRequestVoice && sysexLength == 6u)
        {
            uint8_t payload[74] = {}; uint32_t offset = 0;
            appendMinimoogVoice(payload, offset, currentUserVoice());
            appendMinimoogContour(payload, offset, currentUserContour());
            queueMinimoogResponse(MinimoogMidiCommandVoiceResponse, payload, offset);
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

    void updateVoiceControls(int32_t main, int32_t x, int32_t y)
    {
        if (pickupPageControl(
                main, voicePagePickup.mainEntry, filterCutoffControl,
                voicePagePickup.mainPickedUp))
            filterCutoffControl = main;
        if (pickupPageControl(
                x, voicePagePickup.xEntry, filterResonanceControl,
                voicePagePickup.xPickedUp))
            filterResonanceControl = x;
        if (pickupPageControl(
                y, voicePagePickup.yEntry, contourControl,
                voicePagePickup.yPickedUp))
            contourControl = y;
    }

    void updateOscillatorControls(int32_t main, int32_t x, int32_t y)
    {
        if (oscillatorSetupPage == OscillatorSetupPage::Oscillator1)
        {
            if (pickupPageControl(
                    main, oscillatorPagePickup.mainEntry, pitchControl,
                    oscillatorPagePickup.mainPickedUp))
                pitchControl = main;
            if (pickupPageControl(
                    x, oscillatorPagePickup.xEntry, osc1LevelControl,
                    oscillatorPagePickup.xPickedUp))
                osc1LevelControl = x;
            if (pickupPageControl(
                    y, oscillatorPagePickup.yEntry, waveControl,
                    oscillatorPagePickup.yPickedUp))
                waveControl = y;
            return;
        }

        if (oscillatorSetupPage == OscillatorSetupPage::Oscillator2)
        {
            if (pickupPageControl(
                    main, oscillatorPagePickup.mainEntry, osc2IntervalControl,
                    oscillatorPagePickup.mainPickedUp))
                osc2IntervalControl = main;
            if (pickupPageControl(
                    x, oscillatorPagePickup.xEntry, osc2LevelControl,
                    oscillatorPagePickup.xPickedUp))
                osc2LevelControl = x;
            if (pickupPageControl(
                    y, oscillatorPagePickup.yEntry, wave2Control,
                    oscillatorPagePickup.yPickedUp))
                wave2Control = y;
            return;
        }

        if (pickupPageControl(
                main, oscillatorPagePickup.mainEntry, externalOscillatorOffset,
                oscillatorPagePickup.mainPickedUp))
            externalOscillatorOffset = main;
        if (pickupPageControl(
                x, oscillatorPagePickup.xEntry, externalOscillatorLevelControl,
                oscillatorPagePickup.xPickedUp))
            externalOscillatorLevelControl = x;
        if (pickupPageControl(
                y, oscillatorPagePickup.yEntry, externalOscillatorRangeControl,
                oscillatorPagePickup.yPickedUp))
            externalOscillatorRangeControl = y;
    }

    void updateModulationControls(int32_t main, int32_t x, int32_t y)
    {
        if (pickupPageControl(
                main, modulationPagePickup.mainEntry, externalOscillatorOffset,
                modulationPagePickup.mainPickedUp))
            externalOscillatorOffset = main;
        if (pickupPageControl(
                x, modulationPagePickup.xEntry, lfoDepthControl,
                modulationPagePickup.xPickedUp))
            lfoDepthControl = x;
        if (pickupPageControl(
                y, modulationPagePickup.yEntry, lfoDestinationControl,
                modulationPagePickup.yPickedUp))
            lfoDestinationControl = y;
    }

    void updateNoiseControls(int32_t main, int32_t x, int32_t y)
    {
        if (pickupPageControl(
                main, noisePagePickup.mainEntry, noiseLevelControl,
                noisePagePickup.mainPickedUp))
            noiseLevelControl = main;
        // X is a two-state selector. Cross its centre dead zone to change
        // colour, rather than requiring the continuous-control soft pickup.
        static constexpr int32_t NoiseColourDeadZone = 160;
        if (x >= 2048 + NoiseColourDeadZone)
            noiseColourControl = 4095;
        else if (x <= 2048 - NoiseColourDeadZone)
            noiseColourControl = 0;
        if (pickupPageControl(
                y, noisePagePickup.yEntry, modulationNoiseBlendControl,
                noisePagePickup.yPickedUp))
            modulationNoiseBlendControl = y;
    }

    void resetMinimoogPagePickup(Switch mode, int32_t main, int32_t x, int32_t y)
    {
        if (mode == Switch::Middle)
            resetPagePickup(voicePagePickup, main, x, y);
        else if (mode == Switch::Up)
            resetPagePickup(oscillatorPagePickup, main, x, y);
        else
            resetPagePickup(modulationPagePickup, main, x, y);
    }

    void resetPagePickup(PanelPagePickup& pickup, int32_t main, int32_t x, int32_t y)
    {
        pickup.mainEntry = main;
        pickup.xEntry = x;
        pickup.yEntry = y;
        pickup.mainPickedUp = false;
        pickup.xPickedUp = false;
        pickup.yPickedUp = false;
    }

    bool pickupPageControl(int32_t knob, int32_t entry, int32_t target, bool& pickedUp)
    {
        if (pickedUp)
            return true;

        int32_t distance = knob - target;
        if (distance < 0)
            distance = -distance;

        // Pick up at the stored value, including a move that crosses it
        // between samples, so switching pages never produces a parameter jump.
        bool crossedTarget =
            (entry <= target && knob >= target) ||
            (entry >= target && knob <= target);
        if (distance <= 96 || crossedTarget)
            pickedUp = true;

        return pickedUp;
    }

    const VoicePreset& factoryPreset(uint8_t slot) const
    {
        // Keep this order aligned with the Web UI factory bank. The factory
        // bank is compile-time data for this pass; user flash slots arrive with
        // the new WebMIDI protocol rather than the C1ZZL3 envelope store.
        static constexpr VoicePreset presets[PresetSlotCount] = {
            {"Init Voice", 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 0, 2048, 2048, 2048},
            {"Funk Glide Bass", 2048, 2048, 1638, 2457, 3767, 3194, 0, 2048, 720, 1710, 2948, 1393, 2048, 0, 2048, 1024, 2048},
            {"Three Saw Bass", 2048, 4095, 1638, 1638, 3522, 3194, 2866, 2048, 540, 1536, 1802, 655, 2048, 0, 2048, 1024, 2048},
            {"West Coast Whistle", 2048, 2048, 0, 819, 2948, 1720, 0, 2048, 3400, 2048, 1475, 1966, 2048, 737, 3276, 3072, 4095},
            {"Glide Mod Arp", 2048, 2048, 1638, 4095, 3276, 2252, 0, 2048, 1880, 2048, 3030, 1556, 2048, 1269, 2621, 2048, 3072},
            {"Slow Brass Lead", 2048, 2048, 1638, 3276, 3358, 2702, 0, 2048, 1320, 2048, 2785, 901, 2048, 0, 2048, 2048, 2048},
            {"Sub Pulse Bass", 2048, 2048, 2457, 3276, 3440, 2580, 0, 2048, 410, 2048, 2130, 491, 2048, 0, 2048, 0, 2048},
            {"Resonant Pulse Lead", 2048, 2048, 4095, 2457, 3030, 2744, 0, 2048, 2460, 2048, 3153, 2293, 2048, 328, 2458, 2048, 3072}
        };
        return presets[slot & 0x07u];
    }

    int32_t factoryGlideControl(uint8_t slot) const
    {
        // 0--4095 maps to 0--2000 ms in the Web UI. Preserve the intended
        // character of the named glide/bass factory voices.
        static constexpr int32_t controls[PresetSlotCount] = {
            0, 215, 0, 0, 369, 266, 0, 0
        };
        return controls[slot & 0x07u];
    }

    void applyFactoryPreset(uint8_t slot)
    {
        const VoicePreset& preset = factoryPreset(slot);
        pitchControl = preset.pitch;
        osc2IntervalControl = preset.osc2Interval;
        osc2Detune = 0;
        waveControl = preset.wave1;
        wave2Control = preset.wave2;
        osc1LevelControl = preset.osc1Level;
        osc2LevelControl = preset.osc2Level;
        externalOscillatorLevelControl = preset.externalLevel;
        externalOscillatorRangeControl = preset.externalRange;
        filterCutoffControl = preset.filterCutoff;
        oscillatorMixControl = preset.oscillatorMix;
        contourControl = preset.contour;
        filterResonanceControl = preset.resonance;
        externalOscillatorOffset = preset.externalOffset;
        lfoDepthControl = preset.lfoDepth;
        lfoDestinationControl = preset.lfoDestination;
        noiseLevelControl = preset.noiseLevel;
        noiseColourControl = preset.noiseColour;
        modulationNoiseBlendControl = preset.modulationNoiseBlend;
        filterHighPassControl = preset.filterHighPass >= 2048;
        filterModulationEnabled = preset.filterModulationEnabled >= 2048;
        oscillatorModulationEnabled = preset.oscillatorModulationEnabled >= 2048;
        modulationPrimarySourceControl = preset.modulationPrimarySource;
        modulationSecondarySourceControl = preset.modulationSecondarySource;
        lfoRateControl = preset.lfoRate;
        lfoShapeControl = preset.lfoShape;
        setGlideControl(factoryGlideControl(slot));
        glidePitchInitialised = false;
        applyFactoryContour(slot);
        activePresetSlot = slot & 0x07u;
    }

    void applyFactoryContour(uint8_t slot)
    {
        static constexpr SavedUserContour contours[PresetSlotCount] = {
            // Init must be immediately playable from a keyboard. The other
            // front-panel controls remain centred, but a middle five-second
            // ADS range is not a useful default attack.
            {0, 410, 4095, 0, 410, 0, 2730, 0, 0}, {3, 213, 2948, 4, 254, 737, 2730, 0, 0},
            {3, 287, 3194, 4, 336, 1147, 2730, 0, 0}, {31, 590, 0, 74, 672, 0, 4095, 0, 0},
            {7, 295, 2375, 10, 393, 983, 4095, 0, 0}, {147, 705, 3112, 180, 803, 1884, 2730, 0, 0},
            {2, 188, 3358, 2, 205, 1393, 0, 0, 0}, {9, 426, 2539, 15, 524, 819, 4095, 0, 0}
        };
        applyUserContour(contours[slot & 0x07u]);
    }

    void recallFactoryPreset(uint8_t slot, Switch mode, int32_t main, int32_t x, int32_t y)
    {
        applyFactoryPreset(slot);
        resetMinimoogPagePickup(mode, main, x, y);
    }

    SavedUserVoice currentUserVoice() const
    {
        return {pitchControl, osc2IntervalControl, waveControl, wave2Control,
            osc1LevelControl, osc2LevelControl, externalOscillatorLevelControl,
            externalOscillatorRangeControl, filterCutoffControl, oscillatorMixControl,
            contourControl, filterResonanceControl, externalOscillatorOffset,
            lfoDepthControl, lfoDestinationControl, osc1RangeControl, osc2RangeControl,
            noiseLevelControl, noiseColourControl, modulationNoiseBlendControl,
            filterHighPassControl ? 4095 : 0, filterModulationEnabled ? 4095 : 0,
            oscillatorModulationEnabled ? 4095 : 0, modulationPrimarySourceControl,
            modulationSecondarySourceControl, lfoRateControl, lfoShapeControl,
            glideControl};
    }

    SavedUserContour currentUserContour() const
    {
        return {ampAttackControl, ampDecayControl, ampSustainControl,
            filterAttackControl, filterDecayControl, filterSustainControl,
            filterKeyboardTrackingControl, ampDecayReleaseEnabled ? 4095 : 0,
            filterDecayReleaseEnabled ? 4095 : 0};
    }

    void applyUserVoice(const SavedUserVoice& voice)
    {
        pitchControl = clamp12(voice.pitch); osc2IntervalControl = clamp12(voice.osc2Interval);
        waveControl = clamp12(voice.wave1); wave2Control = clamp12(voice.wave2);
        osc1LevelControl = clamp12(voice.osc1Level); osc2LevelControl = clamp12(voice.osc2Level);
        externalOscillatorLevelControl = clamp12(voice.externalLevel);
        externalOscillatorRangeControl = clamp12(voice.externalRange);
        filterCutoffControl = clamp12(voice.filterCutoff); oscillatorMixControl = clamp12(voice.oscillatorMix);
        contourControl = clamp12(voice.contour); filterResonanceControl = clamp12(voice.resonance);
        externalOscillatorOffset = clamp12(voice.externalOffset); lfoDepthControl = clamp12(voice.lfoDepth);
        lfoDestinationControl = clamp12(voice.lfoDestination);
        osc1RangeControl = clamp12(voice.osc1Range);
        osc2RangeControl = clamp12(voice.osc2Range);
        noiseLevelControl = clamp12(voice.noiseLevel);
        noiseColourControl = clamp12(voice.noiseColour);
        modulationNoiseBlendControl = clamp12(voice.modulationNoiseBlend);
        filterHighPassControl = voice.filterHighPass >= 2048;
        filterModulationEnabled = voice.filterModulationEnabled >= 2048;
        oscillatorModulationEnabled = voice.oscillatorModulationEnabled >= 2048;
        modulationPrimarySourceControl = clamp12(voice.modulationPrimarySource);
        modulationSecondarySourceControl = clamp12(voice.modulationSecondarySource);
        lfoRateControl = clamp12(voice.lfoRate);
        lfoShapeControl = clamp12(voice.lfoShape);
        setGlideControl(voice.glide);
        glidePitchInitialised = false;
        osc2Detune = 0;
    }

    void applyUserContour(const SavedUserContour& contour)
    {
        ampAttackControl = clamp12(contour.ampAttack);
        ampDecayControl = clamp12(contour.ampDecay);
        ampSustainControl = clamp12(contour.ampSustain);
        filterAttackControl = clamp12(contour.filterAttack);
        filterDecayControl = clamp12(contour.filterDecay);
        filterSustainControl = clamp12(contour.filterSustain);
        filterKeyboardTrackingControl = clamp12(contour.filterKeyboardTracking);
        ampDecayReleaseEnabled = contour.ampDecayRelease >= 2048;
        filterDecayReleaseEnabled = contour.filterDecayRelease >= 2048;
        refreshAdsTiming();
    }

    bool recallPreset(uint8_t bank, uint8_t slot, Switch mode, int32_t main, int32_t x, int32_t y)
    {
        slot &= 0x07u;
        if (bank == 0u) applyFactoryPreset(slot);
        else if ((userPresetBank.loadedMask & (1u << slot)) != 0u)
        {
            applyUserVoice(userPresetBank.voices[slot]);
            applyUserContour(userPresetBank.contours[slot]);
        }
        else return false;
        activePresetBank = bank ? 1u : 0u; activePresetSlot = slot;
        resetMinimoogPagePickup(mode, main, x, y);
        return true;
    }

    void updateStartupPresetSelection(Switch mode, int32_t main)
    {
        if (startupSelectChecked)
            return;

        if (startupPresetSelectMode)
            return;

        if (startupSelectSamples < StartupSelectWindowSamples)
            startupSelectSamples++;

        if (mode == Switch::Down)
        {
            // Holding Down while power is applied enters selection immediately;
            // no five-second gesture is required before the first half-second.
            startupPresetSelectMode = true;
            startupPresetSelectReady = false;
            updatePresetSelection(main);
            return;
        }

        if (startupSelectSamples >= StartupSelectWindowSamples)
            startupSelectChecked = true;
    }

    void updateMinimoogHoldState(Switch mode, Switch previousMode)
    {
        if (mode != Switch::Down)
        {
            downHoldSamples = 0;
            presetSelectMode = false;
            noisePanelActive = false;
            return;
        }

        if (previousMode != Switch::Down)
        {
            downHoldSamples = 0;
            noisePanelActive = false;
        }

        if (downHoldSamples < PresetSelectHoldSamples)
            downHoldSamples++;

        if (downHoldSamples >= PresetSelectHoldSamples)
            presetSelectMode = true;
    }

    void completeDownPress(int32_t main, int32_t x, int32_t y)
    {
        if (startupPresetSelectMode)
        {
            // Match C1ZZL3: releasing the startup hold only arms selection.
            // A second Down press and release loads the highlighted preset.
            if (!startupPresetSelectReady)
            {
                startupPresetSelectReady = true;
                downHoldSamples = 0;
                return;
            }
            recallPreset(presetPreviewBank, presetPreviewSlot, Switch::Middle, main, x, y);
            startupPresetSelectMode = false;
            startupPresetSelectReady = false;
            startupSelectChecked = true;
            downHoldSamples = 0;
            return;
        }

        if (presetSelectMode)
        {
            recallPreset(presetPreviewBank, presetPreviewSlot, Switch::Middle, main, x, y);
            presetSelectMode = false;
            downHoldSamples = 0;
            return;
        }

        if (downHoldSamples == 0 || downHoldSamples > ShortSwitchPressSamples)
            return;

        uint8_t page = (uint8_t)oscillatorSetupPage;
        page = (page + 1u) % 3u;
        oscillatorSetupPage = (OscillatorSetupPage)page;
        resetPagePickup(oscillatorPagePickup, main, x, y);
    }

    void updatePresetSelection(int32_t main)
    {
        uint8_t count = selectablePresetCount();
        uint8_t selected =
            (uint8_t)(((uint32_t)clamp12(main) * count) >> 12);
        presetForSelection(selected, presetPreviewBank, presetPreviewSlot);
    }

    uint8_t selectablePresetCount() const
    {
        uint8_t count = PresetSlotCount;
        for (uint8_t slot = 0; slot < PresetSlotCount; ++slot)
            if ((userPresetBank.loadedMask & (1u << slot)) != 0u)
                count++;
        return count;
    }

    void presetForSelection(uint8_t selected, uint8_t& bank, uint8_t& slot) const
    {
        uint8_t count = selectablePresetCount();
        if (selected >= count)
            selected = count - 1u;

        if (selected < PresetSlotCount)
        {
            bank = 0u;
            slot = selected;
            return;
        }

        uint8_t userIndex = selected - PresetSlotCount;
        for (uint8_t candidate = 0; candidate < PresetSlotCount; ++candidate)
        {
            if ((userPresetBank.loadedMask & (1u << candidate)) == 0u)
                continue;
            if (userIndex == 0u)
            {
                bank = 1u;
                slot = candidate;
                return;
            }
            userIndex--;
        }

        bank = 0u;
        slot = 0u;
    }

    void outputPresetSelectionSilence()
    {
        AudioOut1(0);
        AudioOut2(0);
        CVOut1(0);
        CVOut2(0);
        PulseOut1(false);
        PulseOut2(false);
    }

    void outputExternalOscillatorPitch(int32_t pitchUnits)
    {
        int32_t offset = (pipeRangeSemitones(externalOscillatorRangeControl) *
            PitchUnitsPerOctave) / 12;
        offset += ((externalOscillatorOffset - 2048) *
            PitchOffsetUnitsPerControlQ16) >> 16;
        // The Workshop System reference is C3 = 0 V. Use the calibrated
        // millivolt output directly so OSC3 follows fractional pitch too.
        int32_t millivolts = (int32_t)(((int64_t)(pitchUnits -
            MainPitchCentreUnits + offset) * MillivoltsPerPitchUnitQ16) >> 16);
        (void)CVOutMillivolts(0, millivolts);
    }

    void updateMinimoogLeds(Switch mode)
    {
        if (startupPresetSelectMode || presetSelectMode)
        {
            showPresetPreviewLeds(presetPreviewSlot);
            return;
        }

        if (mode == Switch::Middle)
        {
            LedBrightness(0, filterCutoffControl);
            LedBrightness(1, 0);
            LedBrightness(2, filterResonanceControl);
            LedBrightness(3, 0);
            LedBrightness(4, contourControl);
            LedBrightness(5, 0);
            return;
        }

        if (mode == Switch::Up)
        {
            showOscillatorPageLeds();
            return;
        }

        bool warning = downHoldSamples >= PresetWarningSamples;
        bool flash = ((downHoldSamples / 6000u) & 1u) != 0u;
        if (downHoldSamples > ShortSwitchPressSamples)
        {
            LedBrightness(0, noiseLevelControl);
            LedBrightness(1, modulationNoiseBlendControl);
            LedBrightness(2, 4095 - modulationNoiseBlendControl);
            LedBrightness(3, noiseColourControl < 2048 ? 4095 : 0);
            LedBrightness(4, warning && flash ? 4095 : 0);
            LedBrightness(5, noiseColourControl >= 2048 ? 4095 : 0);
            return;
        }

        LedBrightness(0, externalOscillatorOffset);
        LedBrightness(1, lfoDepthControl);
        LedBrightness(2, lfoDestinationControl);
        LedBrightness(3, 0);
        LedBrightness(4, 0);
        LedBrightness(5, 2048);
    }

    void showOscillatorPageLeds()
    {
        uint8_t selected = (uint8_t)oscillatorSetupPage;
        LedBrightness(0, selected == 0u ? 4095 : 0);
        LedBrightness(2, selected == 1u ? 4095 : 0);
        LedBrightness(4, selected == 2u ? 4095 : 0);

        int32_t deviation = 0;
        if (oscillatorSetupPage == OscillatorSetupPage::Oscillator2)
            deviation = bipolarControlBrightness(osc2IntervalControl);
        else if (oscillatorSetupPage == OscillatorSetupPage::ExternalOscillator3)
            deviation = bipolarControlBrightness(externalOscillatorOffset);
        LedBrightness(1, deviation);

        if (oscillatorSetupPage == OscillatorSetupPage::ExternalOscillator3)
        {
            LedBrightness(3, 0);
            LedBrightness(5, 0);
            return;
        }

        int32_t waveControl = oscillatorSetupPage == OscillatorSetupPage::Oscillator1
            ? this->waveControl : wave2Control;
        bool flash = ((waveformFlashSamples / 12000u) & 1u) != 0u;
        switch (controlToMoogWave(waveControl))
        {
            case MoogWave::Triangle:
                LedBrightness(3, 0);
                LedBrightness(5, 0);
                break;
            case MoogWave::Sharktooth:
                LedBrightness(3, 4095);
                LedBrightness(5, 0);
                break;
            case MoogWave::Saw:
                LedBrightness(3, 0);
                LedBrightness(5, 4095);
                break;
            case MoogWave::Square:
                LedBrightness(3, flash ? 4095 : 0);
                LedBrightness(5, 0);
                break;
            case MoogWave::WideRectangle:
                LedBrightness(3, 0);
                LedBrightness(5, flash ? 4095 : 0);
                break;
            case MoogWave::NarrowPulse:
                LedBrightness(3, flash ? 4095 : 0);
                LedBrightness(5, flash ? 4095 : 0);
                break;
        }
    }

    void showPresetPreviewLeds(uint8_t slot)
    {
        LedBrightness(0, slot & 1u ? 4095 : 0);
        LedBrightness(1, slot & 2u ? 4095 : 0);
        LedBrightness(2, slot & 4u ? 4095 : 0);
        bool cursorFlash = ((waveformFlashSamples / 6000u) & 1u) != 0u;
        LedBrightness(3, cursorFlash ? 4095 : 0);
        bool user = presetPreviewBank != 0u;
        bool active = presetPreviewBank == activePresetBank && slot == activePresetSlot;
        LedBrightness(4, active ? 4095 : 2048);
        LedBrightness(5, user ? 4095 : 0);
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

    const SavedUserPresetBank& flashUserPresetBank()
    {
        return *reinterpret_cast<const SavedUserPresetBank*>(XIP_BASE + UserPresetFlashOffset);
    }

    const SavedUserPresetBankV1& flashUserPresetBankV1()
    {
        return *reinterpret_cast<const SavedUserPresetBankV1*>(XIP_BASE + UserPresetFlashOffset);
    }

    const SavedUserPresetBankV2& flashUserPresetBankV2()
    {
        return *reinterpret_cast<const SavedUserPresetBankV2*>(XIP_BASE + UserPresetFlashOffset);
    }

    const SavedUserPresetBankV3& flashUserPresetBankV3()
    {
        return *reinterpret_cast<const SavedUserPresetBankV3*>(XIP_BASE + UserPresetFlashOffset);
    }

    const SavedUserPresetBankV4& flashUserPresetBankV4()
    {
        return *reinterpret_cast<const SavedUserPresetBankV4*>(XIP_BASE + UserPresetFlashOffset);
    }

    const SavedUserPresetBankV5& flashUserPresetBankV5()
    {
        return *reinterpret_cast<const SavedUserPresetBankV5*>(XIP_BASE + UserPresetFlashOffset);
    }

    const SavedUserPresetBankV6& flashUserPresetBankV6()
    {
        return *reinterpret_cast<const SavedUserPresetBankV6*>(XIP_BASE + UserPresetFlashOffset);
    }

    const SavedUserPresetBankV7& flashUserPresetBankV7()
    {
        return *reinterpret_cast<const SavedUserPresetBankV7*>(XIP_BASE + UserPresetFlashOffset);
    }

    const SavedUserPresetBankV8& flashUserPresetBankV8()
    {
        return *reinterpret_cast<const SavedUserPresetBankV8*>(XIP_BASE + UserPresetFlashOffset);
    }

    const SavedUserPresetBankV9& flashUserPresetBankV9()
    {
        return *reinterpret_cast<const SavedUserPresetBankV9*>(XIP_BASE + UserPresetFlashOffset);
    }

    uint32_t checksumUserPresetBank(const SavedUserPresetBank& state)
    {
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&state);
        uint32_t checksum = 2166136261u;
        for (uint32_t i = 0; i < sizeof(state) - sizeof(uint32_t); ++i) { checksum ^= bytes[i]; checksum *= 16777619u; }
        return checksum;
    }

    uint32_t checksumUserPresetBankV1(const SavedUserPresetBankV1& state)
    {
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&state);
        uint32_t checksum = 2166136261u;
        for (uint32_t i = 0; i < sizeof(state) - sizeof(uint32_t); ++i) { checksum ^= bytes[i]; checksum *= 16777619u; }
        return checksum;
    }

    uint32_t checksumUserPresetBankV2(const SavedUserPresetBankV2& state)
    {
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&state);
        uint32_t checksum = 2166136261u;
        for (uint32_t i = 0; i < sizeof(state) - sizeof(uint32_t); ++i) { checksum ^= bytes[i]; checksum *= 16777619u; }
        return checksum;
    }

    uint32_t checksumUserPresetBankV3(const SavedUserPresetBankV3& state)
    {
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&state);
        uint32_t checksum = 2166136261u;
        for (uint32_t i = 0; i < sizeof(state) - sizeof(uint32_t); ++i) { checksum ^= bytes[i]; checksum *= 16777619u; }
        return checksum;
    }

    uint32_t checksumUserPresetBankV4(const SavedUserPresetBankV4& state)
    {
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&state); uint32_t checksum = 2166136261u;
        for (uint32_t i = 0; i < sizeof(state) - sizeof(uint32_t); ++i) { checksum ^= bytes[i]; checksum *= 16777619u; }
        return checksum;
    }

    uint32_t checksumUserPresetBankV5(const SavedUserPresetBankV5& state)
    {
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&state); uint32_t checksum = 2166136261u;
        for (uint32_t i = 0; i < sizeof(state) - sizeof(uint32_t); ++i) { checksum ^= bytes[i]; checksum *= 16777619u; }
        return checksum;
    }

    uint32_t checksumUserPresetBankV6(const SavedUserPresetBankV6& state)
    {
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&state); uint32_t checksum = 2166136261u;
        for (uint32_t i = 0; i < sizeof(state) - sizeof(uint32_t); ++i) { checksum ^= bytes[i]; checksum *= 16777619u; }
        return checksum;
    }

    uint32_t checksumUserPresetBankV7(const SavedUserPresetBankV7& state)
    {
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&state);
        uint32_t checksum = 2166136261u;
        for (uint32_t i = 0; i < sizeof(state) - sizeof(uint32_t); ++i) { checksum ^= bytes[i]; checksum *= 16777619u; }
        return checksum;
    }

    uint32_t checksumUserPresetBankV8(const SavedUserPresetBankV8& state)
    {
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&state);
        uint32_t checksum = 2166136261u;
        for (uint32_t i = 0; i < sizeof(state) - sizeof(uint32_t); ++i) { checksum ^= bytes[i]; checksum *= 16777619u; }
        return checksum;
    }

    uint32_t checksumUserPresetBankV9(const SavedUserPresetBankV9& state)
    {
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&state);
        uint32_t checksum = 2166136261u;
        for (uint32_t i = 0; i < sizeof(state) - sizeof(uint32_t); ++i) { checksum ^= bytes[i]; checksum *= 16777619u; }
        return checksum;
    }

    bool isValidUserPresetBank(const SavedUserPresetBank& state)
    {
        return state.magic == UserPresetMagic && state.version == UserPresetVersion &&
            state.size == sizeof(SavedUserPresetBank) && state.checksum == checksumUserPresetBank(state);
    }

    bool isValidUserPresetBankV1(const SavedUserPresetBankV1& state)
    {
        return state.magic == UserPresetMagic && state.version == 1u &&
            state.size == sizeof(SavedUserPresetBankV1) && state.checksum == checksumUserPresetBankV1(state);
    }

    bool isValidUserPresetBankV2(const SavedUserPresetBankV2& state)
    {
        return state.magic == UserPresetMagic && state.version == 2u &&
            state.size == sizeof(SavedUserPresetBankV2) && state.checksum == checksumUserPresetBankV2(state);
    }

    bool isValidUserPresetBankV3(const SavedUserPresetBankV3& state)
    {
        return state.magic == UserPresetMagic && state.version == 3u &&
            state.size == sizeof(SavedUserPresetBankV3) && state.checksum == checksumUserPresetBankV3(state);
    }

    bool isValidUserPresetBankV4(const SavedUserPresetBankV4& state)
    {
        return state.magic == UserPresetMagic && state.version == 4u && state.size == sizeof(SavedUserPresetBankV4) && state.checksum == checksumUserPresetBankV4(state);
    }

    bool isValidUserPresetBankV5(const SavedUserPresetBankV5& state)
    {
        return state.magic == UserPresetMagic && state.version == 5u && state.size == sizeof(SavedUserPresetBankV5) && state.checksum == checksumUserPresetBankV5(state);
    }

    bool isValidUserPresetBankV6(const SavedUserPresetBankV6& state)
    {
        return state.magic == UserPresetMagic && state.version == 6u && state.size == sizeof(SavedUserPresetBankV6) && state.checksum == checksumUserPresetBankV6(state);
    }

    bool isValidUserPresetBankV7(const SavedUserPresetBankV7& state)
    {
        return state.magic == UserPresetMagic && state.version == 7u &&
            state.size == sizeof(SavedUserPresetBankV7) && state.checksum == checksumUserPresetBankV7(state);
    }

    bool isValidUserPresetBankV8(const SavedUserPresetBankV8& state)
    {
        return state.magic == UserPresetMagic && state.version == 8u &&
            state.size == sizeof(SavedUserPresetBankV8) && state.checksum == checksumUserPresetBankV8(state);
    }

    bool isValidUserPresetBankV9(const SavedUserPresetBankV9& state)
    {
        return state.magic == UserPresetMagic && state.version == 9u &&
            state.size == sizeof(SavedUserPresetBankV9) && state.checksum == checksumUserPresetBankV9(state);
    }

    SavedUserContour migrateUserContourV3(const SavedUserContourV3& legacy)
    {
        return {legacy.ampAttack, legacy.ampDecay, legacy.ampSustain,
            legacy.filterAttack, legacy.filterDecay, legacy.filterSustain,
            legacy.filterKeyboardTracking, 0, 0};
    }

    SavedUserContour migrateUserContourV4(const SavedUserContourV4& legacy)
    {
        return {legacy.ampAttack, legacy.ampDecay, legacy.ampSustain, legacy.filterAttack, legacy.filterDecay, legacy.filterSustain, legacy.filterKeyboardTracking, legacy.decayRelease, legacy.decayRelease};
    }

    void splitLegacyWideOffset(int32_t legacy, int32_t& range, int32_t& fine)
    {
        int32_t semitones = ((clamp12(legacy) - 2048) * 24) / 2047;
        int32_t closestRange = 0;
        int32_t closestDistance = 1000;
        for (int32_t candidate = -24; candidate <= 24; candidate += 12)
        {
            int32_t distance = semitones - candidate;
            if (distance < 0) distance = -distance;
            if (distance < closestDistance)
            {
                closestDistance = distance;
                closestRange = candidate;
            }
        }
        int32_t residual = semitones - closestRange;
        if (residual < -7) residual = -7;
        if (residual > 7) residual = 7;
        range = ((closestRange / 12) + 2) * 1024;
        if (range > 4095) range = 4095;
        fine = clamp12(2048 + (residual * 2047) / OscillatorFrequencySemitones);
    }

    SavedUserVoice migrateUserVoiceV2(const SavedUserVoiceV2& legacy)
    {
        SavedUserVoice migrated = {legacy.pitch, legacy.osc2Interval, legacy.wave1,
            legacy.wave2, legacy.osc1Level, legacy.osc2Level, legacy.externalLevel,
            2048, legacy.filterCutoff, legacy.oscillatorMix, legacy.contour,
            legacy.resonance, legacy.externalOffset, legacy.lfoDepth,
            legacy.lfoDestination, 2048, 2048, 0, 0, 0, 0, 4095, 4095, 0, 0,
            1024, 0};
        splitLegacyWideOffset(legacy.osc2Interval, migrated.osc2Range,
            migrated.osc2Interval);
        splitLegacyWideOffset(legacy.externalOffset, migrated.externalRange,
            migrated.externalOffset);
        return migrated;
    }

    SavedUserVoice migrateUserVoiceV5(const SavedUserVoiceV5& legacy)
    {
        return {legacy.pitch, legacy.osc2Interval, legacy.wave1, legacy.wave2,
            legacy.osc1Level, legacy.osc2Level, legacy.externalLevel,
            legacy.externalRange, legacy.filterCutoff, legacy.oscillatorMix,
            legacy.contour, legacy.resonance, legacy.externalOffset,
            legacy.lfoDepth, legacy.lfoDestination, legacy.osc1Range,
            legacy.osc2Range, 0, 0, 0, 0, 4095, 4095, 0, 0, 1024, 0};
    }

    SavedUserVoice migrateUserVoiceV6(const SavedUserVoiceV6& legacy)
    {
        return {legacy.pitch, legacy.osc2Interval, legacy.wave1, legacy.wave2,
            legacy.osc1Level, legacy.osc2Level, legacy.externalLevel,
            legacy.externalRange, legacy.filterCutoff, legacy.oscillatorMix,
            legacy.contour, legacy.resonance, legacy.externalOffset,
            legacy.lfoDepth, legacy.lfoDestination, legacy.osc1Range,
            legacy.osc2Range, legacy.noiseLevel, legacy.noiseColour,
            legacy.modulationNoiseBlend, 0, 4095, 4095, 0, 0, 1024, 0};
    }

    SavedUserVoice migrateUserVoiceV7(const SavedUserVoiceV7& legacy)
    {
        return {legacy.pitch, legacy.osc2Interval, legacy.wave1, legacy.wave2,
            legacy.osc1Level, legacy.osc2Level, legacy.externalLevel,
            legacy.externalRange, legacy.filterCutoff, legacy.oscillatorMix,
            legacy.contour, legacy.resonance, legacy.externalOffset,
            legacy.lfoDepth, legacy.lfoDestination, legacy.osc1Range,
            legacy.osc2Range, legacy.noiseLevel, legacy.noiseColour,
            legacy.modulationNoiseBlend, legacy.filterHighPass,
            legacy.filterModulationEnabled, legacy.oscillatorModulationEnabled,
            0, 0, 1024, 0};
    }

    SavedUserVoice migrateUserVoiceV8(const SavedUserVoiceV8& legacy)
    {
        return {legacy.pitch, legacy.osc2Interval, legacy.wave1, legacy.wave2,
            legacy.osc1Level, legacy.osc2Level, legacy.externalLevel,
            legacy.externalRange, legacy.filterCutoff, legacy.oscillatorMix,
            legacy.contour, legacy.resonance, legacy.externalOffset,
            legacy.lfoDepth, legacy.lfoDestination, legacy.osc1Range,
            legacy.osc2Range, legacy.noiseLevel, legacy.noiseColour,
            legacy.modulationNoiseBlend, legacy.filterHighPass,
            legacy.filterModulationEnabled, legacy.oscillatorModulationEnabled,
            legacy.modulationPrimarySource, legacy.modulationSecondarySource,
            1024, 0};
    }

    SavedUserVoice migrateUserVoiceV9(const SavedUserVoiceV9& legacy)
    {
        return {legacy.pitch, legacy.osc2Interval, legacy.wave1, legacy.wave2,
            legacy.osc1Level, legacy.osc2Level, legacy.externalLevel,
            legacy.externalRange, legacy.filterCutoff, legacy.oscillatorMix,
            legacy.contour, legacy.resonance, legacy.externalOffset,
            legacy.lfoDepth, legacy.lfoDestination, legacy.osc1Range,
            legacy.osc2Range, legacy.noiseLevel, legacy.noiseColour,
            legacy.modulationNoiseBlend, legacy.filterHighPass,
            legacy.filterModulationEnabled, legacy.oscillatorModulationEnabled,
            legacy.modulationPrimarySource, legacy.modulationSecondarySource,
            legacy.lfoRate, legacy.lfoShape, 0};
    }

    void loadUserPresetBank()
    {
        const SavedUserPresetBank& saved = flashUserPresetBank();
        if (isValidUserPresetBank(saved))
        {
            userPresetBank = saved;
            return;
        }

        const SavedUserPresetBankV9& legacyV9 = flashUserPresetBankV9();
        if (isValidUserPresetBankV9(legacyV9))
        {
            userPresetBank.loadedMask = legacyV9.loadedMask;
            for (uint32_t slot = 0; slot < PresetSlotCount; ++slot)
            {
                for (uint32_t character = 0; character < 16u; ++character) userPresetBank.names[slot][character] = legacyV9.names[slot][character];
                userPresetBank.voices[slot] = migrateUserVoiceV9(legacyV9.voices[slot]);
                userPresetBank.contours[slot] = legacyV9.contours[slot];
            }
            saveUserPresetBank(); return;
        }

        const SavedUserPresetBankV8& legacyV8 = flashUserPresetBankV8();
        if (isValidUserPresetBankV8(legacyV8))
        {
            userPresetBank.loadedMask = legacyV8.loadedMask;
            for (uint32_t slot = 0; slot < PresetSlotCount; ++slot)
            {
                for (uint32_t character = 0; character < 16u; ++character) userPresetBank.names[slot][character] = legacyV8.names[slot][character];
                userPresetBank.voices[slot] = migrateUserVoiceV8(legacyV8.voices[slot]);
                userPresetBank.contours[slot] = legacyV8.contours[slot];
            }
            saveUserPresetBank(); return;
        }

        const SavedUserPresetBankV7& legacyV7 = flashUserPresetBankV7();
        if (isValidUserPresetBankV7(legacyV7))
        {
            userPresetBank.loadedMask = legacyV7.loadedMask;
            for (uint32_t slot = 0; slot < PresetSlotCount; ++slot)
            {
                for (uint32_t character = 0; character < 16u; ++character) userPresetBank.names[slot][character] = legacyV7.names[slot][character];
                userPresetBank.voices[slot] = migrateUserVoiceV7(legacyV7.voices[slot]);
                userPresetBank.contours[slot] = legacyV7.contours[slot];
            }
            saveUserPresetBank(); return;
        }

        const SavedUserPresetBankV6& legacyV6 = flashUserPresetBankV6();
        if (isValidUserPresetBankV6(legacyV6))
        {
            userPresetBank.loadedMask = legacyV6.loadedMask;
            for (uint32_t slot = 0; slot < PresetSlotCount; ++slot)
            {
                for (uint32_t character = 0; character < 16u; ++character) userPresetBank.names[slot][character] = legacyV6.names[slot][character];
                userPresetBank.voices[slot] = migrateUserVoiceV6(legacyV6.voices[slot]);
                userPresetBank.contours[slot] = legacyV6.contours[slot];
            }
            saveUserPresetBank(); return;
        }

        const SavedUserPresetBankV5& legacyV5 = flashUserPresetBankV5();
        if (isValidUserPresetBankV5(legacyV5))
        {
            userPresetBank.loadedMask = legacyV5.loadedMask;
            for (uint32_t slot = 0; slot < PresetSlotCount; ++slot)
            {
                for (uint32_t character = 0; character < 16u; ++character) userPresetBank.names[slot][character] = legacyV5.names[slot][character];
                userPresetBank.voices[slot] = migrateUserVoiceV5(legacyV5.voices[slot]);
                userPresetBank.contours[slot] = legacyV5.contours[slot];
            }
            saveUserPresetBank(); return;
        }

        const SavedUserPresetBankV4& legacyV4 = flashUserPresetBankV4();
        if (isValidUserPresetBankV4(legacyV4))
        {
            userPresetBank.loadedMask = legacyV4.loadedMask;
            for (uint32_t slot = 0; slot < PresetSlotCount; ++slot)
            {
                for (uint32_t character = 0; character < 16u; ++character) userPresetBank.names[slot][character] = legacyV4.names[slot][character];
                userPresetBank.voices[slot] = migrateUserVoiceV5(legacyV4.voices[slot]);
                userPresetBank.contours[slot] = migrateUserContourV4(legacyV4.contours[slot]);
            }
            saveUserPresetBank(); return;
        }

        const SavedUserPresetBankV3& legacyV3 = flashUserPresetBankV3();
        if (isValidUserPresetBankV3(legacyV3))
        {
            userPresetBank.loadedMask = legacyV3.loadedMask;
            for (uint32_t slot = 0; slot < PresetSlotCount; ++slot)
            {
                for (uint32_t character = 0; character < 16u; ++character)
                    userPresetBank.names[slot][character] = legacyV3.names[slot][character];
                userPresetBank.voices[slot] = migrateUserVoiceV5(legacyV3.voices[slot]);
                userPresetBank.contours[slot] = migrateUserContourV3(legacyV3.contours[slot]);
            }
            saveUserPresetBank();
            return;
        }

        const SavedUserPresetBankV2& legacyV2 = flashUserPresetBankV2();
        if (isValidUserPresetBankV2(legacyV2))
        {
            userPresetBank.loadedMask = legacyV2.loadedMask;
            for (uint32_t slot = 0; slot < PresetSlotCount; ++slot)
            {
                for (uint32_t character = 0; character < 16u; ++character)
                    userPresetBank.names[slot][character] = legacyV2.names[slot][character];
                userPresetBank.voices[slot] = migrateUserVoiceV2(legacyV2.voices[slot]);
                userPresetBank.contours[slot] = migrateUserContourV3(legacyV2.contours[slot]);
            }
            saveUserPresetBank();
            return;
        }

        const SavedUserPresetBankV1& legacy = flashUserPresetBankV1();
        if (!isValidUserPresetBankV1(legacy)) return;

        userPresetBank.loadedMask = legacy.loadedMask;
        for (uint32_t slot = 0; slot < PresetSlotCount; ++slot)
        {
            for (uint32_t character = 0; character < 16u; ++character)
                userPresetBank.names[slot][character] = legacy.names[slot][character];
            userPresetBank.voices[slot] = migrateUserVoiceV2(legacy.voices[slot]);
            userPresetBank.contours[slot] = currentUserContour();
        }
        saveUserPresetBank();
    }

    void saveUserPresetBank()
    {
        userPresetBank.magic = UserPresetMagic; userPresetBank.version = UserPresetVersion;
        userPresetBank.size = sizeof(SavedUserPresetBank); userPresetBank.checksum = checksumUserPresetBank(userPresetBank);
        uint8_t page[FLASH_PAGE_SIZE] = {};
        flash_range_erase(UserPresetFlashOffset, FLASH_SECTOR_SIZE);
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&userPresetBank);
        for (uint32_t offset = 0; offset < sizeof(userPresetBank); offset += FLASH_PAGE_SIZE)
        {
            uint32_t count = sizeof(userPresetBank) - offset; if (count > FLASH_PAGE_SIZE) count = FLASH_PAGE_SIZE;
            for (uint32_t i = 0; i < FLASH_PAGE_SIZE; ++i) page[i] = i < count ? bytes[offset + i] : 0xFFu;
            flash_range_program(UserPresetFlashOffset + offset, page, FLASH_PAGE_SIZE);
        }
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

    int32_t nextNoiseSample()
    {
        // One random value feeds both the audible mixer source and modulation.
        // The one-pole low shelf is deliberately inexpensive and gives the
        // pink option a noticeably softer top end than white noise.
        int32_t white = ((int32_t)fastNoise() - 128) << 4;
        pinkNoiseState += (white - pinkNoiseState) >> 6;
        if (noiseColourControl < 2048)
            return white;
        return (white + (pinkNoiseState * 3)) >> 2;
    }

    int32_t nextInternalLfoSample()
    {
        // Quadratic scaling keeps the low end usefully slow while reaching
        // conventional vibrato speed without a division or lookup in audio.
        uint32_t rate = (uint32_t)clamp12(lfoRateControl);
        uint32_t increment = 4474u + (uint32_t)(((uint64_t)rate * rate * 1249000u) >> 24);
        lfoPhase += increment;
        uint32_t phase = lfoPhase >> 20;
        if (lfoShapeControl >= 2048)
            return phase < 2048u ? -2048 : 2047;
        return phase < 2048u ? ((int32_t)phase << 1) - 2048 : 6142 - ((int32_t)phase << 1);
    }

    int32_t pitchUnits(int32_t knob, int32_t pitchInputMillivolts)
    {
        int32_t mainUnits = MainPitchCentreUnits +
            ((clamp12(knob) - 2048) * MainPitchSemitones * PitchUnitsPerOctave) /
            (4096 * 12);
        int32_t inputUnits =
            (pitchInputMillivolts * PitchUnitsPerMillivoltQ16) >> 16;

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

    int32_t glidePitchUnits(int32_t target)
    {
        // The slider is converted to a shift when the value changes, leaving
        // this per-sample path division- and multiply-free. Pitch is retained
        // with fractional precision so slow slides neither stick nor step.
        int32_t targetQ16 = target * 65536;
        if (!glidePitchInitialised || glideControl == 0)
        {
            glidedPitchQ16 = targetQ16;
            glidePitchInitialised = true;
            return target;
        }

        int32_t delta = targetQ16 - glidedPitchQ16;
        int32_t step = delta >> glideShift;
        if (step == 0 && delta != 0)
            step = delta > 0 ? 1 : -1;
        glidedPitchQ16 += step;
        return glidedPitchQ16 >> 16;
    }

    void setGlideControl(int32_t control)
    {
        glideControl = clamp12(control);
        // The first pass spread the audible part of the response across only
        // the upper half of the slider. These logarithmic-ish bands make a
        // 100 ms setting useful while retaining a long two-second maximum.
        if (glideControl < 21) glideShift = 6;         // under 10 ms
        else if (glideControl < 41) glideShift = 7;    // about 20 ms
        else if (glideControl < 82) glideShift = 8;    // about 40 ms
        else if (glideControl < 164) glideShift = 9;   // about 80 ms
        else if (glideControl < 328) glideShift = 10;  // about 160 ms
        else if (glideControl < 655) glideShift = 11;  // about 320 ms
        else if (glideControl < 1310) glideShift = 12; // about 640 ms
        else if (glideControl < 2621) glideShift = 13; // about 1.25 s
        else glideShift = 14;                          // about 2 s
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
    int32_t ladderStage1 = 0;
    int32_t ladderStage2 = 0;
    int32_t ladderStage3 = 0;
    int32_t ladderStage4 = 0;
    int32_t gateAmplitude = 0;
    bool gateWasHigh = false;
    bool adsGateWasHigh = false;
    AdsEnvelopeState ampAds = {};
    AdsEnvelopeState filterAds = {};

    uint32_t noise = 1;
    uint32_t noiseHoldCounter = 0;
    int32_t heldPdNoise = 0;
    int32_t heldPhaseNoise = 0;
    int32_t pinkNoiseState = 0;
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
    int32_t waveControl = 1638;  // Saw.
    int32_t wave2Control = 2730; // Square.
    int32_t recipeBankControl = 0;
    int32_t glidedPitchQ16 = 0;
    int32_t glideControl = 0;
    uint8_t glideShift = 1;
    bool glidePitchInitialised = false;
    int32_t osc2Detune = 0;
    int32_t osc2IntervalControl = 2048;
    int32_t osc1LevelControl = 4095;
    int32_t osc2LevelControl = 4095;
    int32_t externalOscillatorLevelControl = 4095;
    int32_t osc1RangeControl = 2048;
    int32_t osc2RangeControl = 2048;
    int32_t externalOscillatorRangeControl = 2048;
    int32_t osc2Ring = 0;
    int32_t osc2Noise = 0;
    int32_t filterCutoffControl = 3072;
    int32_t oscillatorMixControl = 1024;
    int32_t contourControl = 2048;
    int32_t filterResonanceControl = 700;
    int32_t ampAttackControl = 2048;
    int32_t ampDecayControl = 2048;
    int32_t ampSustainControl = 2048;
    int32_t filterAttackControl = 2048;
    int32_t filterDecayControl = 2048;
    int32_t filterSustainControl = 2048;
    int32_t filterKeyboardTrackingControl = 2730;
    bool ampDecayReleaseEnabled = false;
    bool filterDecayReleaseEnabled = false;
    int32_t externalOscillatorOffset = 2048;
    int32_t lfoDepthControl = 0;
    int32_t lfoDestinationControl = 2048;
    int32_t noiseLevelControl = 0;
    int32_t noiseColourControl = 0;
    int32_t modulationNoiseBlendControl = 0;
    int32_t modulationPrimarySourceControl = 0;
    int32_t modulationSecondarySourceControl = 0;
    int32_t lfoRateControl = 1024;
    int32_t lfoShapeControl = 0;
    int32_t midiModWheelControl = 0;
    uint32_t lfoPhase = 0;
    bool filterHighPassControl = false;
    bool filterModulationEnabled = true;
    bool oscillatorModulationEnabled = true;
    PanelPagePickup voicePagePickup = {};
    PanelPagePickup oscillatorPagePickup = {};
    PanelPagePickup modulationPagePickup = {};
    PanelPagePickup noisePagePickup = {};
    uint32_t downHoldSamples = 0;
    bool noisePanelActive = false;
    bool presetSelectMode = false;
    bool startupPresetSelectMode = false;
    bool startupPresetSelectReady = false;
    uint8_t presetPreviewBank = 0;
    uint8_t presetPreviewSlot = 0;
    uint8_t activePresetBank = 0;
    uint8_t activePresetSlot = 0;
    SavedUserPresetBank userPresetBank = {};
    OscillatorSetupPage oscillatorSetupPage = OscillatorSetupPage::Oscillator1;
    uint32_t waveformFlashSamples = 0;
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
    bool minimoogResponsePending = false;
    uint8_t minimoogResponseCommand = 0;
    uint8_t minimoogResponsePayload[129] = {};
    uint32_t minimoogResponseLength = 0;
    uint32_t minimoogResponseIndex = 0;
    uint8_t minimoogResponsePhase = 0;
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
MinimoogVoice card;
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
#if defined(MINIMOOG_OVERCLOCK_KHZ) && MINIMOOG_OVERCLOCK_KHZ
    set_sys_clock_khz(MINIMOOG_OVERCLOCK_KHZ, true);
#endif
    multicore_launch_core1(usbMidiWorker);
    card.Run();
}
