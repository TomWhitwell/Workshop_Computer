#include "computercard.h"
#include "CS80_LUT.h"

#include "hardware/clocks.h"
#include "hardware/flash.h"
#include "pico/multicore.h"
#include "pico/sync.h"
#include "pico/time.h"
#include "tusb.h"
#include "usb_midi_host.h"

static constexpr uint8_t WebMidiManufacturer = 0x7Du;
static constexpr uint8_t WebMidiId[4] = {0x43u, 0x53u, 0x38u, 0x30u}; // CS80
static constexpr uint8_t WebMidiCommandApplyPatch = 0x01u;
static constexpr uint8_t WebMidiCommandSavePatch = 0x02u;
static constexpr uint8_t WebMidiCommandRequestPatch = 0x03u;
static constexpr uint8_t WebMidiCommandPatchResponse = 0x04u;
static constexpr uint8_t WebMidiCommandSaveSlot = 0x05u;
static constexpr uint8_t WebMidiCommandRequestSlots = 0x06u;
static constexpr uint8_t WebMidiCommandSlotsResponse = 0x07u;
static constexpr uint8_t WebMidiCommandRequestSlot = 0x08u;
static constexpr uint8_t WebMidiCommandSlotResponse = 0x09u;
static constexpr uint8_t WebMidiCommandDeleteSlot = 0x0Au;
static constexpr uint8_t WebMidiCommandSetStartupSlot = 0x0Bu;
static constexpr uint8_t WebMidiPatchProtocolVersion = 8u;
static constexpr uint32_t WebMidiPatchPayloadLength = 63u;
static constexpr uint32_t WebMidiMaxSysexLength = 80u;
static constexpr uint8_t MidiStatusMask = 0xF0u;
static constexpr uint8_t MidiStatusNoteOff = 0x80u;
static constexpr uint8_t MidiStatusNoteOn = 0x90u;
static constexpr uint8_t MidiStatusControlChange = 0xB0u;
static constexpr uint8_t MidiStatusPitchBend = 0xE0u;

class CS80Card : public ComputerCard
{
private:
    struct PatchState;

public:
    CS80Card()
    {
        voiceA.enabled = true;
        voiceB.enabled = true;
        loadPatchBank();
        midiControlPatch = currentPatchState();
        midiControlPatchValid = true;
    }

    bool ShouldBootUsbHost()
    {
        return USBPowerState() == USBPowerState_t::DFP;
    }

    void ProcessUsbMidiByte(uint8_t byte)
    {
        if (byte >= 0xF8u)
            return;

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

    void SendPendingUsbMidiOutput()
    {
        drainAudioPatchSnapshots();

        if (slotsResponsePending)
        {
            sendSlotsFrame();
            slotsResponsePending = false;
        }

        if (slotResponsePending)
        {
            sendPatchFrame(WebMidiCommandSlotResponse, slotResponseIndex, slotResponsePatch);
            slotResponsePending = false;
        }

        if (patchResponsePending && (patchResponseHasPatch || webPatchSnapshotValid))
        {
            sendPatchFrame(
                WebMidiCommandPatchResponse,
                0x7Fu,
                patchResponseHasPatch ? patchResponsePatch : webPatchSnapshot);
            patchResponsePending = false;
            patchResponseHasPatch = false;
        }
    }

    void FlushPendingWebPatch()
    {
        flushQueuedPatchForAudio();
    }

    void sendPatchFrame(uint8_t command, uint8_t slot, const PatchState& patch)
    {
        uint8_t frame[80] = {
            0xF0u,
            WebMidiManufacturer,
            WebMidiId[0],
            WebMidiId[1],
            WebMidiId[2],
            WebMidiId[3],
            command,
            (uint8_t)(slot & 0x7Fu),
            WebMidiPatchProtocolVersion
        };
        uint32_t offset = 9;
        appendWebMidiUint14(frame, offset, knobValueForPitch(patch.params.pitchOffsetQ8));
        appendWebMidiUint14(frame, offset, patch.params.portamento);
        appendWebMidiUint14(frame, offset, patch.params.pitchCvRange);
        appendWebMidiUint14(frame, offset, patch.params.filterCvMode);
        appendWebMidiUint14(frame, offset, clampRange(patch.params.pulseWidth - 512, 0, 4095));
        appendWebMidiUint14(frame, offset, patch.params.pwmAmount);
        appendWebMidiUint14(frame, offset, patch.params.sawLevel);
        appendWebMidiUint14(frame, offset, patch.params.pulseLevel);
        appendWebMidiUint14(frame, offset, patch.params.sineLevel);
        appendWebMidiUint14(frame, offset, patch.params.noiseLevel);
        appendWebMidiUint14(frame, offset, patch.params.voiceLevel);
        appendWebMidiUint14(frame, offset, clamp12(2048 + patch.performancePitchQ8));
        appendWebMidiUint14(frame, offset, patch.params.hpCutoff);
        appendWebMidiUint14(frame, offset, patch.params.lpCutoff);
        appendWebMidiUint14(frame, offset, patch.params.resonance);
        appendWebMidiUint14(frame, offset, patch.params.expressionDepth);
        appendWebMidiUint14(frame, offset, patch.params.attack);
        appendWebMidiUint14(frame, offset, patch.params.decay);
        appendWebMidiUint14(frame, offset, patch.params.sustain);
        appendWebMidiUint14(frame, offset, patch.params.release);
        appendWebMidiUint14(frame, offset, patch.params.filterAttack);
        appendWebMidiUint14(frame, offset, patch.params.filterDecay);
        appendWebMidiUint14(frame, offset, patch.params.filterSustain);
        appendWebMidiUint14(frame, offset, patch.params.filterRelease);
        appendWebMidiUint14(frame, offset, patch.params.lfoRate);
        appendWebMidiUint14(frame, offset, patch.params.lfoPitchDepth);
        appendWebMidiUint14(frame, offset, patch.params.lfoPwmDepth);
        appendWebMidiUint14(frame, offset, patch.params.lfoVcfDepth);
        appendWebMidiUint14(frame, offset, patch.params.lfoVcaDepth);
        appendWebMidiUint14(frame, offset, patch.ringAmount);
        appendWebMidiUint14(frame, offset, patch.params.ringSpeed);
        frame[offset++] = 0xF7u;

        sendWebMidiFrame(frame, offset);
    }

    void sendWebMidiFrame(const uint8_t* frame, uint32_t length)
    {
        uint8_t midiKick[3] = {0x80u, 60u, 0u};
        tud_midi_stream_write(0, midiKick, sizeof(midiKick));
        tud_midi_stream_write(0, frame, length);
    }

    void sendSlotsFrame()
    {
        uint8_t frame[11] = {
            0xF0u,
            WebMidiManufacturer,
            WebMidiId[0],
            WebMidiId[1],
            WebMidiId[2],
            WebMidiId[3],
            WebMidiCommandSlotsResponse,
            (uint8_t)(savedSlotMask & 0x7Fu),
            (uint8_t)((savedSlotMask >> 7) & 0x7Fu),
            (uint8_t)(startupSlot & 0x07u),
            0xF7u
        };
        sendWebMidiFrame(frame, sizeof(frame));
    }

    void ProcessSample() override
    {
        const Switch mode = SwitchVal();
        const bool downNow = mode == Switch::Down;

        int32_t audioPitch = AudioIn1();
        int32_t filterCv = CVIn1();
        int32_t expressionCv = CVIn2();
        bool pulseGateNow = Disconnected(Input::Pulse1) || PulseIn1();
        bool gateNow = pulseGateNow || midiNoteActive;
        currentGate = gateNow;

        updateDownSwitch(downNow);

        if (++controlDivider >= ControlIntervalSamples)
        {
            controlDivider = 0;
            int32_t main = KnobVal(Knob::Main);
            int32_t x = KnobVal(Knob::X);
            int32_t y = KnobVal(Knob::Y);
            applyPendingWebPatches();
            updateStartupPatchSelect(mode);
            if (startupSelectMode)
                updateStartupPatchSelection(main, mode);
            if (!startupSelectMode)
                updatePanelControls(mode, main, x, y);
            updatePitchCache(audioPitch);
            updateLEDs(mode);
            publishAudioPatchSnapshot();
        }

        if (startupSelectMode)
        {
            AudioOut1(0);
            AudioOut2(0);
            CVOut1(0);
            CVOut2(0);
            PulseOut1(false);
            PulseOut2(false);
            return;
        }

        if (gateNow && !lastGate)
        {
            triggerVoice(voiceA);
            triggerVoice(voiceB);
        }
        lastGate = gateNow;

        updateGlobalModulation();

        int32_t outA = renderVoice(voiceA, gateNow, filterCv, expressionCv, outputAPitchOffsetQ8, 0u);
        int32_t outB = renderVoice(
            voiceB,
            gateNow,
            filterCv,
            expressionCv,
            clampPitchQ8(outputAPitchOffsetQ8 + params.pitchOffsetQ8),
            1u);

        AudioOut1(outA);
        AudioOut2(outB);

        CVOut1(0);
        CVOut2(0);
        PulseOut1(gateNow);
        PulseOut2(downHeld);
    }

private:
    struct VoiceParams
    {
        int32_t pitchOffsetQ8 = 0;
        int32_t portamento = 2600;
        int32_t pitchCvRange = 1;
        int32_t filterCvMode = 0;
        int32_t pulseWidth = 2162;
        int32_t pwmAmount = 1450;
        int32_t sawLevel = 450;
        int32_t pulseLevel = 2500;
        int32_t sineLevel = 2300;
        int32_t noiseLevel = 220;
        int32_t voiceLevel = 3600;
        int32_t hpCutoff = 1500;
        int32_t lpCutoff = 2450;
        int32_t resonance = 2650;
        int32_t expressionDepth = 1900;
        int32_t attack = 80;
        int32_t decay = 760;
        int32_t sustain = 3300;
        int32_t release = 1200;
        int32_t filterAttack = 40;
        int32_t filterDecay = 480;
        int32_t filterSustain = 2400;
        int32_t filterRelease = 900;
        int32_t lfoRate = 1750;
        int32_t lfoPitchDepth = 980;
        int32_t lfoPwmDepth = 1550;
        int32_t lfoVcfDepth = 1000;
        int32_t lfoVcaDepth = 560;
        int32_t ringSpeed = 2250;
        uint32_t cachedPhaseIncrement = C2PhaseIncrement;
    };

    struct PatchState
    {
        VoiceParams params = {};
        int32_t performancePitchQ8 = 0;
        int32_t ringAmount = 850;
    };

    // One producer and one consumer per queue. A release-store publishes a
    // complete slot, while the other core only reads slots it has acquired.
    template <typename T, uint32_t Capacity>
    class SpscPatchQueue
    {
        static_assert((Capacity & (Capacity - 1u)) == 0u, "Capacity must be a power of two");
    public:
        bool tryPush(const T& value)
        {
            const uint32_t write = writeIndex;
            const uint32_t read = readIndex;
            __dmb();
            if (write - read == Capacity)
                return false;

            slots[write & (Capacity - 1u)] = value;
            __dmb();
            writeIndex = write + 1u;
            return true;
        }

        bool tryPop(T& value)
        {
            const uint32_t read = readIndex;
            const uint32_t write = writeIndex;
            __dmb();
            if (read == write)
                return false;

            value = slots[read & (Capacity - 1u)];
            __dmb();
            readIndex = read + 1u;
            return true;
        }

    private:
        T slots[Capacity] = {};
        volatile uint32_t writeIndex = 0;
        volatile uint32_t readIndex = 0;
    };

    struct VoiceState
    {
        bool enabled = false;
        uint32_t phase = 0;
        int32_t ampEnvelopeQ12 = 0;
        int32_t filterEnvelopeQ12 = 0;
        int32_t ampEnvelopeAccQ20 = 0;
        int32_t filterEnvelopeAccQ20 = 0;
        uint8_t ampEnvelopeStage = 0;
        uint8_t filterEnvelopeStage = 0;
        int32_t hpLowpass = 0;
        int32_t lp = 0;
    };

    static constexpr uint32_t C2PhaseIncrement = 5852465u;
    static constexpr uint32_t ControlIntervalSamples = 64u;
    static constexpr uint32_t DownTapSamples = 14400u;
    static constexpr uint32_t DownHoldSamples = 14400u;
    static constexpr uint32_t DownLongHoldSamples = 96000u;
    static constexpr uint32_t StartupSelectDelayTicks = 188u;
    static constexpr uint32_t StartupSelectWindowTicks = 375u;
    static constexpr uint32_t LfoBaseIncrement = 89478u;
    static constexpr uint32_t RingBaseIncrement = 1491308u;
    static constexpr int32_t MaxVoicePitchQ8 = 84 * 256;
    static constexpr int32_t MinVoicePitchQ8 = -48 * 256;
    static constexpr int32_t MiddleCBasePitchQ8 = 24 * 256;
    static constexpr int32_t PitchCvThreeVoltsCounts = 3 * 341;
    static constexpr int32_t PitchCvFiveVoltsCounts = 5 * 341;
    static constexpr int32_t PickupWindow = 96;
    static constexpr uint8_t PatchSlotCount = 8u;
    static constexpr uint32_t PatchBankMagic = 0x43533830u; // CS80
    static constexpr uint16_t PatchBankVersion = 10u;
    static constexpr uint32_t PatchBankFlashOffset =
        (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE) &
        ~(FLASH_SECTOR_SIZE - 1u);

    struct SavedPatch
    {
        int32_t pitchOffsetQ8;
        int32_t portamento;
        int32_t pitchCvRange;
        int32_t filterCvMode;
        int32_t pulseWidth;
        int32_t pwmAmount;
        int32_t sawLevel;
        int32_t pulseLevel;
        int32_t sineLevel;
        int32_t noiseLevel;
        int32_t voiceLevel;
        int32_t performancePitchQ8;
        int32_t hpCutoff;
        int32_t lpCutoff;
        int32_t resonance;
        int32_t expressionDepth;
        int32_t attack;
        int32_t decay;
        int32_t sustain;
        int32_t release;
        int32_t filterAttack;
        int32_t filterDecay;
        int32_t filterSustain;
        int32_t filterRelease;
        int32_t lfoRate;
        int32_t lfoPitchDepth;
        int32_t lfoPwmDepth;
        int32_t lfoVcfDepth;
        int32_t lfoVcaDepth;
        int32_t ringAmount;
        int32_t ringSpeed;
    };

    struct SavedPatchBank
    {
        uint32_t magic;
        uint16_t version;
        uint16_t size;
        uint8_t loadedMask;
        uint8_t startupSlot;
        uint8_t reserved[2];
        SavedPatch slots[PatchSlotCount];
        uint32_t checksum;
    };

    VoiceParams params = {};
    VoiceState voiceA = {};
    VoiceState voiceB = {};
    int32_t outputAPitchOffsetQ8 = 0;
    int32_t voicePulseWidth[2] = {2162, 2162};
    int32_t voicePwmAmount[2] = {1450, 1450};
    int32_t voiceHpCutoff[2] = {1500, 1500};
    int32_t voiceLpCutoff[2] = {2450, 2450};
    int32_t voiceResonance[2] = {2650, 2650};
    SpscPatchQueue<PatchState, 4u> webToAudioPatchQueue = {};
    SpscPatchQueue<PatchState, 4u> audioToWebPatchQueue = {};
    PatchState queuedWebPatch = {};
    bool queuedWebPatchPending = false;
    PatchState webPatchSnapshot = {};
    bool webPatchSnapshotValid = false;
    PatchState midiControlPatch = {};
    bool midiControlPatchValid = false;

    uint32_t controlDivider = 0;
    uint32_t downSamples = 0;
    bool wasDown = false;
    bool downHeld = false;
    bool longHoldSeen = false;
    bool lastGate = false;
    bool currentGate = false;
    bool selectedOutputB = true;
    bool pickedUp[3] = {false, false, false};
    Switch lastPanelMode = Switch::Middle;
    bool lastPanelDownHeld = false;

    uint32_t lfoPhase = 0;
    uint32_t ringPhase = 0;
    uint32_t noiseState = 0x1234abcd;
    int32_t lfoValue = 0;
    int32_t ringValue = 0;
    int32_t ringAmount = 850;
    int32_t performancePitchQ8 = 0;
    int32_t currentPitchQ8 = MiddleCBasePitchQ8;
    bool pitchSlewInitialised = false;
    bool midiNoteActive = false;
    uint8_t midiNote = 60;
    int32_t lastPitchInput = 0;
    uint8_t sysexBuffer[WebMidiMaxSysexLength] = {};
    uint32_t sysexLength = 0;
    bool sysexReceiving = false;
    bool sysexOverflow = false;
    uint8_t midiRunningStatus = 0;
    uint8_t midiData[2] = {};
    uint8_t midiDataCount = 0;
    bool patchResponsePending = false;
    bool patchResponseHasPatch = false;
    PatchState patchResponsePatch = {};
    bool slotsResponsePending = false;
    bool slotResponsePending = false;
    uint8_t slotResponseIndex = 0;
    PatchState slotResponsePatch = {};
    uint8_t savedSlotMask = 0;
    uint8_t startupSlot = 0;
    uint32_t startupSelectSamples = 0;
    bool startupSelectChecked = false;
    bool startupSelectMode = false;
    bool startupSelectReleased = false;
    uint8_t startupSelectedSlot = 0;

    void processMidiVoiceByte(uint8_t byte)
    {
        if (byte & 0x80u)
        {
            if (byte < 0xF0u)
            {
                midiRunningStatus = byte;
                midiDataCount = 0;
            }
            else
            {
                midiRunningStatus = 0;
                midiDataCount = 0;
            }
            return;
        }

        uint8_t type = midiRunningStatus & MidiStatusMask;
        if (type != MidiStatusNoteOff &&
            type != MidiStatusNoteOn &&
            type != MidiStatusControlChange &&
            type != MidiStatusPitchBend)
            return;

        midiData[midiDataCount++] = byte & 0x7Fu;
        if (midiDataCount < 2u)
            return;

        midiDataCount = 0;

        if (type == MidiStatusNoteOn && midiData[1] > 0)
        {
            midiNote = midiData[0];
            midiNoteActive = true;
            triggerVoice(voiceA);
            triggerVoice(voiceB);
            return;
        }

        if (type == MidiStatusNoteOff || (type == MidiStatusNoteOn && midiData[1] == 0))
        {
            if (midiNoteActive && midiData[0] == midiNote)
                midiNoteActive = false;
            return;
        }

        if (type == MidiStatusControlChange)
        {
            handleMidiControlChange(midiData[0], midiData[1]);
            return;
        }

        if (type == MidiStatusPitchBend)
        {
            handleMidiPitchBend(midiData[0], midiData[1]);
            return;
        }
    }

    void refreshMidiControlPatch()
    {
        drainAudioPatchSnapshots();
        if (webPatchSnapshotValid)
        {
            midiControlPatch = webPatchSnapshot;
            midiControlPatchValid = true;
        }
        else if (!midiControlPatchValid)
        {
            midiControlPatch = currentPatchState();
            midiControlPatchValid = true;
        }
    }

    int32_t midiCcToControl(uint8_t value) const
    {
        return ((int32_t)(value & 0x7Fu) * 4095) / 127;
    }

    void pushMidiControlPatch()
    {
        queuePatchForAudio(midiControlPatch);
        patchResponsePatch = midiControlPatch;
        patchResponseHasPatch = true;
        patchResponsePending = true;
    }

    void handleMidiControlChange(uint8_t cc, uint8_t value)
    {
        refreshMidiControlPatch();
        int32_t control = midiCcToControl(value);

        switch (cc & 0x7Fu)
        {
        case 1:
            midiControlPatch.params.lfoPitchDepth = control;
            break;
        case 20:
            midiControlPatch.params.sawLevel = control;
            break;
        case 21:
            midiControlPatch.params.pulseLevel = control;
            break;
        case 22:
            midiControlPatch.params.sineLevel = control;
            break;
        case 23:
            midiControlPatch.params.noiseLevel = control;
            break;
        case 24:
            midiControlPatch.params.pulseWidth = clampRange(512 + control, 512, 3584);
            break;
        case 25:
            midiControlPatch.params.pwmAmount = control;
            break;
        case 26:
            midiControlPatch.params.lpCutoff = control;
            break;
        case 27:
            midiControlPatch.params.resonance = control;
            break;
        case 28:
            midiControlPatch.params.hpCutoff = control;
            break;
        case 29:
            midiControlPatch.ringAmount = control;
            break;
        case 30:
            midiControlPatch.params.ringSpeed = control;
            break;
        case 31:
            midiControlPatch.params.portamento = control;
            break;
        case 32:
            offsetLinkedEnvelope(midiControlPatch.params.attack, midiControlPatch.params.filterAttack, control);
            break;
        case 33:
            offsetLinkedEnvelope(midiControlPatch.params.decay, midiControlPatch.params.filterDecay, control);
            break;
        case 34:
            offsetLinkedEnvelope(midiControlPatch.params.sustain, midiControlPatch.params.filterSustain, control);
            break;
        case 35:
            offsetLinkedEnvelope(midiControlPatch.params.release, midiControlPatch.params.filterRelease, control);
            break;
        case 2:
        case 11:
            midiControlPatch.params.expressionDepth = control;
            break;
        case 91:
            midiControlPatch.params.lfoVcfDepth = control;
            break;
        case 92:
            midiControlPatch.params.lfoVcaDepth = control;
            break;
        case 76:
            midiControlPatch.params.lfoRate = control;
            break;
        case 77:
        case 93:
            midiControlPatch.params.lfoPwmDepth = control;
            break;
        case 7:
            midiControlPatch.params.voiceLevel = control;
            break;
        default:
            return;
        }

        pushMidiControlPatch();
    }

    void offsetLinkedEnvelope(int32_t& ampValue, int32_t& filterValue, int32_t newAmpValue)
    {
        int32_t delta = clamp12(newAmpValue) - ampValue;
        ampValue = clamp12(ampValue + delta);
        filterValue = clamp12(filterValue + delta);
    }

    void handleMidiPitchBend(uint8_t lsb, uint8_t msb)
    {
        refreshMidiControlPatch();
        int32_t bend = ((int32_t)(msb & 0x7Fu) << 7) | (lsb & 0x7Fu);
        midiControlPatch.performancePitchQ8 =
            clampRange(((bend - 8192) * 512) / 8192, -512, 511);
        pushMidiControlPatch();
    }

    void updateDownSwitch(bool downNow)
    {
        if (downNow)
        {
            if (downSamples < DownLongHoldSamples + 1u)
                ++downSamples;
            if (downSamples >= DownHoldSamples)
                downHeld = true;
            if (downSamples >= DownLongHoldSamples)
                longHoldSeen = true;
        }
        else
        {
            if (wasDown &&
                downSamples > 0 &&
                downSamples < DownTapSamples &&
                !startupSelectMode &&
                startupSelectChecked)
            {
                selectedOutputB = !selectedOutputB;
                pickedUp[0] = false;
                pickedUp[1] = false;
                pickedUp[2] = false;
            }
            downSamples = 0;
            downHeld = false;
            longHoldSeen = false;
        }

        wasDown = downNow;
    }

    void updatePanelControls(Switch mode, int32_t main, int32_t x, int32_t y)
    {
        updatePickupContext(mode);
        uint32_t voiceIndex = selectedVoiceIndex();

        if (mode == Switch::Up)
        {
            if (pickupReady(0, main, voiceLpCutoff[voiceIndex]))
                voiceLpCutoff[voiceIndex] = main;
            if (pickupReady(1, x, voiceHpCutoff[voiceIndex]))
                voiceHpCutoff[voiceIndex] = x;
            if (pickupReady(2, y, voiceResonance[voiceIndex]))
                voiceResonance[voiceIndex] = y;
            return;
        }

        if (mode == Switch::Middle)
        {
            if (pickupReady(0, main, knobValueForPitch(outputAPitchOffsetQ8)))
                outputAPitchOffsetQ8 = clampPitchQ8((main - 2048) << 2);
            if (pickupReady(1, x, voicePulseWidth[voiceIndex] - 512))
                voicePulseWidth[voiceIndex] = clampRange(512 + x, 512, 3584);
            if (pickupReady(2, y, voicePwmAmount[voiceIndex]))
                voicePwmAmount[voiceIndex] = y;
            return;
        }

        if (downHeld)
        {
            if (pickupReady(0, main, knobValueForDetuneB()))
                params.pitchOffsetQ8 = detuneBQ8FromKnob(main);
            if (pickupReady(1, x, ringAmount))
                ringAmount = x;
            if (pickupReady(2, y, params.lfoPitchDepth))
                params.lfoPitchDepth = y;
        }
    }

    void updatePickupContext(Switch mode)
    {
        if (mode == lastPanelMode && downHeld == lastPanelDownHeld)
            return;

        pickedUp[0] = false;
        pickedUp[1] = false;
        pickedUp[2] = false;
        lastPanelMode = mode;
        lastPanelDownHeld = downHeld;
    }

    void updateStartupPatchSelect(Switch mode)
    {
        if (startupSelectChecked)
            return;

        if (startupSelectMode)
            return;

        if (mode == Switch::Down && savedSlotMask != 0)
        {
            startupSelectMode = true;
            startupSelectReleased = false;
            return;
        }

        if (startupSelectSamples < StartupSelectDelayTicks + StartupSelectWindowTicks)
        {
            startupSelectSamples++;
            return;
        }

        startupSelectChecked = true;
        startupSelectMode = false;
    }

    void updateStartupPatchSelection(int32_t main, Switch mode)
    {
        uint8_t count = loadedSlotCount();
        if (count == 0)
        {
            startupSelectMode = false;
            startupSelectChecked = true;
            startupSelectReleased = false;
            return;
        }

        uint8_t selectedIndex = (uint8_t)(((uint32_t)clamp12(main) * count) >> 12);
        if (selectedIndex >= count)
            selectedIndex = count - 1;
        startupSelectedSlot = slotForLoadedSelection(selectedIndex);
        showSlotLeds(startupSelectedSlot);

        if (mode != Switch::Down)
        {
            startupSelectReleased = true;
            return;
        }

        if (startupSelectReleased)
        {
            applySavedSlot(startupSelectedSlot);
            startupSlot = startupSelectedSlot;
            savePatchBankIfChanged();
            startupSelectMode = false;
            startupSelectChecked = true;
            startupSelectReleased = false;
        }
    }

    bool pickupReady(uint32_t index, int32_t knob, int32_t target)
    {
        if (pickedUp[index])
            return true;

        int32_t delta = knob - target;
        if (delta < 0)
            delta = -delta;
        if (delta <= PickupWindow)
            pickedUp[index] = true;

        return pickedUp[index];
    }

    int32_t knobValueForPitch(int32_t pitchQ8) const
    {
        return clamp12(2048 + (pitchQ8 >> 2));
    }

    int32_t knobValueForPerformancePitch() const
    {
        return clamp12(2048 + performancePitchQ8);
    }

    int32_t knobValueForDetuneB() const
    {
        return clamp12(2048 + ((params.pitchOffsetQ8 * 2048) / (12 * 256)));
    }

    int32_t detuneBQ8FromKnob(int32_t knob) const
    {
        int32_t centered = clamp12(knob) - 2048;
        return clampRange((centered * (12 * 256)) / 2048, -(12 * 256), 12 * 256);
    }

    uint32_t selectedVoiceIndex() const
    {
        return selectedOutputB ? 1u : 0u;
    }

    void updatePitchCache(int32_t pitchInput)
    {
        lastPitchInput = pitchInput;
        if (midiNoteActive)
        {
            int32_t targetPitchQ8 = clampPitchQ8(
                midiNotePitchQ8(midiNote) + performancePitchQ8);

            if (!pitchSlewInitialised || params.portamento <= 0)
            {
                currentPitchQ8 = targetPitchQ8;
                pitchSlewInitialised = true;
            }
            else
            {
                int32_t delta = targetPitchQ8 - currentPitchQ8;
                int32_t speed = 4095 - clamp12(params.portamento);
                int32_t step = 1 + ((speed * speed) >> 16);

                if (delta > step)
                    currentPitchQ8 += step;
                else if (delta < -step)
                    currentPitchQ8 -= step;
                else
                    currentPitchQ8 = targetPitchQ8;
            }

            params.cachedPhaseIncrement =
                phaseIncrementFromSemitoneQ8(currentPitchQ8);
            return;
        }

        int32_t limitedPitchInput = pitchInput;
        int32_t pitchBaseQ8 = MiddleCBasePitchQ8;
        if (params.pitchCvRange == 0)
        {
            limitedPitchInput = clampRange(limitedPitchInput, 0, PitchCvFiveVoltsCounts);
            // Standard unipolar V/oct convention: 0V is C1 and C4 is +3V.
            pitchBaseQ8 -= 36 * 256;
        }
        else
        {
            limitedPitchInput = clampRange(
                limitedPitchInput,
                -PitchCvThreeVoltsCounts,
                PitchCvThreeVoltsCounts);
        }
        int32_t pitchCvQ8 = limitedPitchInput * 9; // Hardware-tested rough 1V/oct scale: 341 counts ~= 12 semitones.
        int32_t targetPitchQ8 = clampPitchQ8(pitchBaseQ8 + performancePitchQ8 + pitchCvQ8);

        if (!pitchSlewInitialised || params.portamento <= 0)
        {
            currentPitchQ8 = targetPitchQ8;
            pitchSlewInitialised = true;
        }
        else
        {
            int32_t delta = targetPitchQ8 - currentPitchQ8;
            int32_t speed = 4095 - clamp12(params.portamento);
            int32_t step = 1 + ((speed * speed) >> 16);

            if (delta > step)
                currentPitchQ8 += step;
            else if (delta < -step)
                currentPitchQ8 -= step;
            else
                currentPitchQ8 = targetPitchQ8;
        }

        params.cachedPhaseIncrement =
            phaseIncrementFromSemitoneQ8(currentPitchQ8);
    }

    int32_t midiNotePitchQ8(uint8_t note) const
    {
        return ((int32_t)note - 36) * 256;
    }

    PatchState currentPatchState() const
    {
        PatchState patch = {};
        patch.params = params;
        patch.performancePitchQ8 = performancePitchQ8;
        patch.ringAmount = ringAmount;
        return patch;
    }

    void applyPatchState(const PatchState& patch)
    {
        bool envelopeChanged =
            params.attack != patch.params.attack ||
            params.decay != patch.params.decay ||
            params.sustain != patch.params.sustain ||
            params.release != patch.params.release ||
            params.filterAttack != patch.params.filterAttack ||
            params.filterDecay != patch.params.filterDecay ||
            params.filterSustain != patch.params.filterSustain ||
            params.filterRelease != patch.params.filterRelease;

        params = patch.params;
        performancePitchQ8 = clampRange(patch.performancePitchQ8, -2048, 2047);
        ringAmount = clamp12(patch.ringAmount);
        pitchSlewInitialised = false;
        pickedUp[0] = false;
        pickedUp[1] = false;
        pickedUp[2] = false;

        if (envelopeChanged && currentGate)
        {
            resetEnvelopeForAudition(voiceA);
            resetEnvelopeForAudition(voiceB);
        }
    }

    void applyPendingWebPatches()
    {
        PatchState patch = {};
        while (webToAudioPatchQueue.tryPop(patch))
            applyPatchState(patch);
    }

    void publishAudioPatchSnapshot()
    {
        // Readback is best-effort; core 1 drains any backlog and keeps its newest snapshot.
        audioToWebPatchQueue.tryPush(currentPatchState());
    }

    void queuePatchForAudio(const PatchState& patch)
    {
        queuedWebPatch = patch;
        queuedWebPatchPending = true;
        flushQueuedPatchForAudio();
    }

    void flushQueuedPatchForAudio()
    {
        if (queuedWebPatchPending && webToAudioPatchQueue.tryPush(queuedWebPatch))
            queuedWebPatchPending = false;
    }

    void drainAudioPatchSnapshots()
    {
        PatchState snapshot = {};
        while (audioToWebPatchQueue.tryPop(snapshot))
        {
            webPatchSnapshot = snapshot;
            webPatchSnapshotValid = true;
        }
    }

    void triggerVoice(VoiceState& voice)
    {
        voice.phase = 0;
        voice.ampEnvelopeStage = 0;
        voice.filterEnvelopeStage = 0;
        if (voice.ampEnvelopeQ12 < 96)
        {
            voice.ampEnvelopeQ12 = 96;
            voice.ampEnvelopeAccQ20 = voice.ampEnvelopeQ12 << 8;
        }
        if (voice.filterEnvelopeQ12 < 128)
        {
            voice.filterEnvelopeQ12 = 128;
            voice.filterEnvelopeAccQ20 = voice.filterEnvelopeQ12 << 8;
        }
    }

    void resetEnvelopeForAudition(VoiceState& voice)
    {
        voice.ampEnvelopeQ12 = 0;
        voice.filterEnvelopeQ12 = 0;
        voice.ampEnvelopeAccQ20 = 0;
        voice.filterEnvelopeAccQ20 = 0;
        voice.ampEnvelopeStage = 0;
        voice.filterEnvelopeStage = 0;
    }

    void updateGlobalModulation()
    {
        uint32_t lfoIncrement = LfoBaseIncrement + ((uint32_t)params.lfoRate << 8);
        uint32_t ringIncrement = RingBaseIncrement + ((uint32_t)params.ringSpeed << 9);

        lfoPhase += lfoIncrement;
        ringPhase += ringIncrement;
        lfoValue = sine64(lfoPhase);
        ringValue = sine64(ringPhase);
    }

    int32_t renderVoice(
        VoiceState& voice,
        bool gate,
        int32_t filterCv,
        int32_t expressionCv,
        int32_t pitchOffsetQ8,
        uint32_t voiceIndex)
    {
        if (!voice.enabled)
            return 0;

        VoiceParams voiceParams = params;
        voiceParams.pulseWidth = voicePulseWidth[voiceIndex];
        voiceParams.pwmAmount = voicePwmAmount[voiceIndex];
        voiceParams.hpCutoff = voiceHpCutoff[voiceIndex];
        voiceParams.lpCutoff = voiceLpCutoff[voiceIndex];
        voiceParams.resonance = voiceResonance[voiceIndex];

        updateEnvelope(voice, voiceParams, gate);

        uint32_t pwmOffset = ((uint32_t)voiceParams.lfoPwmDepth * (uint32_t)voiceParams.pwmAmount) >> 12;
        int32_t pulseWidth = voiceParams.pulseWidth + ((lfoValue * (int32_t)pwmOffset) >> 11);
        pulseWidth = clampRange(pulseWidth, 256, 3840);

        int32_t pitchMod = (lfoValue * voiceParams.lfoPitchDepth) >> 12;
        uint32_t baseIncrement = phaseIncrementFromSemitoneQ8(
            clampPitchQ8(currentPitchQ8 + pitchOffsetQ8));
        int32_t phaseIncrement = static_cast<int32_t>(baseIncrement) +
            (static_cast<int32_t>(baseIncrement >> 15) * pitchMod);
        voice.phase += static_cast<uint32_t>(phaseIncrement);

        int32_t phase12 = (int32_t)((voice.phase >> 20) & 4095u);
        int32_t saw = phase12 - 2048;
        int32_t pulse = phase12 < pulseWidth ? 1500 : -1500;
        int32_t sineBody = sine64(voice.phase) >> 2;
        noiseState = noiseState * 1664525u + 1013904223u;
        int32_t noise = ((int32_t)((noiseState >> 20) & 4095u)) - 2048;

        int32_t sawBody = (saw * voiceParams.sawLevel) >> 12;
        int32_t pulseBody = (pulse * voiceParams.pulseLevel) >> 12;
        int32_t sineMix = (sineBody * voiceParams.sineLevel) >> 12;
        int32_t noiseMix = (noise * voiceParams.noiseLevel) >> 13;
        int32_t osc = (sawBody + pulseBody + sineMix + noiseMix) >> 1;
        osc = clip12(osc);
        osc = applyRingMod(osc);

        int32_t filtered = filterVoice(osc, voice, voiceParams, filterCv, expressionCv);
        int32_t amp = ampCurve(voice.ampEnvelopeQ12);
        if (voiceParams.lfoVcaDepth > 0)
        {
            int32_t tremolo = clamp12(2048 + ((lfoValue * voiceParams.lfoVcaDepth) >> 11));
            amp = (amp * tremolo) >> 12;
        }
        int32_t enveloped = (filtered * amp) >> 12;
        return clip12((enveloped * voiceParams.voiceLevel) >> 12);
    }

    void updateEnvelope(VoiceState& state, const VoiceParams& voiceParams, bool gate)
    {
        updateOneEnvelope(
            state.ampEnvelopeQ12,
            state.ampEnvelopeAccQ20,
            state.ampEnvelopeStage,
            gate,
            voiceParams.attack,
            voiceParams.decay,
            voiceParams.sustain,
            voiceParams.release);

        updateOneEnvelope(
            state.filterEnvelopeQ12,
            state.filterEnvelopeAccQ20,
            state.filterEnvelopeStage,
            gate,
            voiceParams.filterAttack,
            voiceParams.filterDecay,
            voiceParams.filterSustain,
            voiceParams.filterRelease);
    }

    void updateOneEnvelope(
        int32_t& envelopeQ12,
        int32_t& envelopeAccQ20,
        uint8_t& stage,
        bool gate,
        int32_t attack,
        int32_t decay,
        int32_t sustain,
        int32_t release)
    {
        int32_t attackRate = rateFromTimeControl(attack);
        int32_t decayRate = rateFromTimeControl(decay);
        int32_t sustainLevel = clamp12(sustain) << 8;
        int32_t releaseRate = releaseRateFromTimeControl(release);
        static constexpr int32_t EnvelopeMaxQ20 = 4095 << 8;

        if (!gate)
        {
            stage = 3;
            envelopeAccQ20 -= releaseRate;
            if (envelopeAccQ20 < 0)
                envelopeAccQ20 = 0;
            envelopeQ12 = envelopeAccQ20 >> 8;
            return;
        }

        if (stage == 0)
        {
            envelopeAccQ20 += attackRate;
            if (envelopeAccQ20 >= EnvelopeMaxQ20)
            {
                envelopeAccQ20 = EnvelopeMaxQ20;
                stage = 1;
            }
            envelopeQ12 = envelopeAccQ20 >> 8;
            return;
        }

        if (stage == 1)
        {
            if (envelopeAccQ20 > sustainLevel)
            {
                envelopeAccQ20 -= decayRate;
                if (envelopeAccQ20 < sustainLevel)
                    envelopeAccQ20 = sustainLevel;
            }
            else
                envelopeAccQ20 = sustainLevel;

            envelopeQ12 = envelopeAccQ20 >> 8;
            if (envelopeAccQ20 == sustainLevel)
                stage = 2;
            return;
        }

        if (stage == 2)
        {
            envelopeAccQ20 = sustainLevel;
            envelopeQ12 = envelopeAccQ20 >> 8;
        }
        else
            stage = 0;
    }

    int32_t filterVoice(
        int32_t input,
        VoiceState& state,
        const VoiceParams& voiceParams,
        int32_t filterCv,
        int32_t expressionCv)
    {
        int32_t expression = (expressionCv * voiceParams.expressionDepth) >> 12;
        int32_t expressionMod = expression + expression;
        int32_t lfoFilterMod = (lfoValue * voiceParams.lfoVcfDepth) >> 11;
        int32_t hpCv = 0;
        int32_t lpCv = 0;
        int32_t resonanceCv = expression;

        if (voiceParams.filterCvMode == 1)
        {
            hpCv = filterCv;
        }
        else if (voiceParams.filterCvMode == 2)
        {
            lpCv = filterCv + filterCv;
            expressionMod = 0;
        }
        else
        {
            hpCv = filterCv;
            lpCv = filterCv + filterCv;
        }

        int32_t hpControl = clamp12(voiceParams.hpCutoff + hpCv);
        int32_t filterEnvelopeMod = filterEnvelopeCurve(state.filterEnvelopeQ12);
        int32_t lpControl = clamp12(voiceParams.lpCutoff + lpCv + expressionMod + lfoFilterMod + filterEnvelopeMod);

        int32_t hpAlpha = curveFromControl(hpControl);
        int32_t lpAlpha = curveFromControl(lpControl);

        state.hpLowpass += (hpAlpha * (input - state.hpLowpass)) >> 12;
        int32_t highpassed = input - state.hpLowpass;

        int32_t resonance = voiceParams.resonance + resonanceCv;
        resonance = clamp12(resonance);

        int32_t driven = highpassed - ((state.lp * resonance) >> 11);
        state.lp += (lpAlpha * (driven - state.lp)) >> 12;

        return clip12(state.lp);
    }

    int32_t applyRingMod(int32_t input) const
    {
        if (ringAmount <= 0)
            return input;

        int32_t ringed = (input * ringValue) >> 11;
        return mix(input, ringed, ringAmount);
    }

    void updateLEDs(Switch mode)
    {
        if (startupSelectMode)
            return;

        int32_t xValue = 0;
        int32_t yValue = 0;
        uint32_t voiceIndex = selectedVoiceIndex();

        if (mode == Switch::Up)
        {
            xValue = voiceHpCutoff[voiceIndex];
            yValue = voiceResonance[voiceIndex];
        }
        else if (mode == Switch::Middle)
        {
            xValue = voicePulseWidth[voiceIndex] - 512;
            yValue = voicePwmAmount[voiceIndex];
        }
        else
        {
            xValue = ringAmount;
            yValue = params.lfoPitchDepth;
        }

        LedBrightness(0, selectedOutputB ? 768 : 4095);
        LedBrightness(1, selectedOutputB ? 4095 : 768);
        LedBrightness(2, clamp12(xValue));
        LedBrightness(3, pickedUp[1] ? 4095 : 384);
        LedBrightness(4, longHoldSeen ? 4095 : clamp12(yValue));
        LedBrightness(5, pickedUp[2] ? 4095 : 384);
    }

    uint32_t phaseIncrementFromSemitoneQ8(int32_t semitoneQ8) const
    {
        int32_t whole = semitoneQ8 >> 8;
        uint32_t frac = (uint32_t)semitoneQ8 & 255u;
        int32_t octave = 0;

        while (whole < 0)
        {
            whole += 12;
            --octave;
        }

        while (whole >= 12)
        {
            whole -= 12;
            ++octave;
        }

        uint32_t a = cs80SemitoneRatioQ16[whole];
        uint32_t b = whole == 11 ? 131072u : cs80SemitoneRatioQ16[whole + 1];
        uint32_t ratio = a + (((b - a) * frac) >> 8);
        uint32_t inc = (uint32_t)(((uint64_t)C2PhaseIncrement * ratio) >> 16);

        while (octave > 0)
        {
            inc <<= 1;
            --octave;
        }

        while (octave < 0)
        {
            inc >>= 1;
            ++octave;
        }

        return inc < 64u ? 64u : inc;
    }

    int32_t curveFromControl(int32_t control) const
    {
        return cs80FilterAlphaQ12[(uint32_t)clamp12(control) >> 8];
    }

    int32_t rateFromTimeControl(int32_t control) const
    {
        const uint32_t idx = (uint32_t)clamp12(control) >> 8;
        static constexpr int32_t envelopeRateQ20[16] = {
            32768, 24576, 16384, 12288,
            8192, 6144, 4096, 3072,
            2048, 1536, 1024, 768,
            512, 384, 256, 192
        };
        return envelopeRateQ20[idx];
    }

    int32_t releaseRateFromTimeControl(int32_t control) const
    {
        const uint32_t idx = (uint32_t)clamp12(control) >> 8;
        static constexpr int32_t envelopeReleaseRateQ20[16] = {
            16384, 12288, 8192, 6144,
            4096, 3072, 2048, 1536,
            1024, 768, 512, 256,
            128, 64, 32, 16
        };
        return envelopeReleaseRateQ20[idx];
    }

    int32_t ampCurve(int32_t envelopeQ12) const
    {
        envelopeQ12 = clamp12(envelopeQ12);
        int32_t squared = (envelopeQ12 * envelopeQ12) >> 12;
        return (envelopeQ12 + squared) >> 1;
    }

    int32_t filterEnvelopeCurve(int32_t envelopeQ12) const
    {
        envelopeQ12 = clamp12(envelopeQ12);
        int32_t curved = (envelopeQ12 * (8192 - envelopeQ12)) >> 13;
        return curved + (envelopeQ12 >> 2);
    }

    int32_t sine64(uint32_t phase) const
    {
        return cs80SineLUT[(phase >> 26) & 63u];
    }

    int32_t mix(int32_t a, int32_t b, int32_t amount) const
    {
        amount = clamp12(amount);
        return a + (((b - a) * amount) >> 12);
    }

    int32_t clampPitchQ8(int32_t value) const
    {
        if (value < MinVoicePitchQ8)
            return MinVoicePitchQ8;
        if (value > MaxVoicePitchQ8)
            return MaxVoicePitchQ8;
        return value;
    }

    int32_t clampRange(int32_t value, int32_t low, int32_t high) const
    {
        if (value < low)
            return low;
        if (value > high)
            return high;
        return value;
    }

    int32_t clamp12(int32_t value) const
    {
        if (value < 0)
            return 0;
        if (value > 4095)
            return 4095;
        return value;
    }

    int32_t clip12(int32_t value) const
    {
        if (value < -2048)
            return -2048;
        if (value > 2047)
            return 2047;
        return value;
    }

    void handleWebMidiSysex()
    {
        if (!webMidiHeaderMatches())
            return;

        uint8_t command = sysexBuffer[5];
        if (command == WebMidiCommandApplyPatch)
        {
            if (sysexLength != WebMidiPatchPayloadLength + 6u)
                return;
            PatchState patch = {};
            if (!decodeWebMidiPatch(6, patch))
                return;
            queuePatchForAudio(patch);
            patchResponsePatch = patch;
            patchResponseHasPatch = true;
            patchResponsePending = true;
            return;
        }

        if (command == WebMidiCommandSavePatch ||
            command == WebMidiCommandSaveSlot)
        {
            handleWebMidiSaveSlot();
            return;
        }

        if (command == WebMidiCommandRequestPatch && sysexLength == 6u)
        {
            drainAudioPatchSnapshots();
            patchResponsePatch = webPatchSnapshotValid ? webPatchSnapshot : currentPatchState();
            patchResponseHasPatch = true;
            patchResponsePending = true;
            return;
        }

        if (command == WebMidiCommandRequestSlots && sysexLength == 6u)
        {
            slotsResponsePending = true;
            return;
        }

        if (command == WebMidiCommandRequestSlot && sysexLength == 7u)
        {
            uint8_t slot = sysexBuffer[6] & 0x07u;
            if ((savedSlotMask & (1u << slot)) != 0)
            {
                PatchState patch = patchStateFromSavedPatch(savedPatches[slot]);
                queuePatchForAudio(patch);
                slotResponseIndex = slot;
                slotResponsePatch = patch;
                slotResponsePending = true;
            }
            return;
        }

        if (command == WebMidiCommandDeleteSlot && sysexLength == 7u)
        {
            uint8_t slot = sysexBuffer[6] & 0x07u;
            savedSlotMask &= ~(1u << slot);
            if (startupSlot == slot)
                startupSlot = firstLoadedSlot();
            savePatchBankIfChanged();
            slotsResponsePending = true;
            return;
        }

        if (command == WebMidiCommandSetStartupSlot && sysexLength == 7u)
        {
            uint8_t slot = sysexBuffer[6] & 0x07u;
            if ((savedSlotMask & (1u << slot)) == 0)
                return;
            startupSlot = slot;
            savePatchBankIfChanged();
            slotsResponsePending = true;
        }
    }

    bool webMidiHeaderMatches() const
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

    bool decodeWebMidiPatch(uint32_t offset, PatchState& patch) const
    {
        if (sysexBuffer[offset++] != WebMidiPatchProtocolVersion)
            return false;

        patch = {};
        patch.params.pitchOffsetQ8 = clampPitchQ8((decodeWebMidiUint14(offset) - 2048) << 2);
        patch.params.portamento = decodeWebMidiUint14(offset);
        patch.params.pitchCvRange = decodeWebMidiUint14(offset) == 0 ? 0 : 1;
        patch.params.filterCvMode = clampRange(decodeWebMidiUint14(offset), 0, 2);
        patch.params.pulseWidth = clampRange(512 + decodeWebMidiUint14(offset), 512, 3584);
        patch.params.pwmAmount = decodeWebMidiUint14(offset);
        patch.params.sawLevel = decodeWebMidiUint14(offset);
        patch.params.pulseLevel = decodeWebMidiUint14(offset);
        patch.params.sineLevel = decodeWebMidiUint14(offset);
        patch.params.noiseLevel = decodeWebMidiUint14(offset);
        patch.params.voiceLevel = decodeWebMidiUint14(offset);
        patch.performancePitchQ8 = decodeWebMidiUint14(offset) - 2048;
        patch.params.hpCutoff = decodeWebMidiUint14(offset);
        patch.params.lpCutoff = decodeWebMidiUint14(offset);
        patch.params.resonance = decodeWebMidiUint14(offset);
        patch.params.expressionDepth = decodeWebMidiUint14(offset);
        patch.params.attack = decodeWebMidiUint14(offset);
        patch.params.decay = decodeWebMidiUint14(offset);
        patch.params.sustain = decodeWebMidiUint14(offset);
        patch.params.release = decodeWebMidiUint14(offset);
        patch.params.filterAttack = decodeWebMidiUint14(offset);
        patch.params.filterDecay = decodeWebMidiUint14(offset);
        patch.params.filterSustain = decodeWebMidiUint14(offset);
        patch.params.filterRelease = decodeWebMidiUint14(offset);
        patch.params.lfoRate = decodeWebMidiUint14(offset);
        patch.params.lfoPitchDepth = decodeWebMidiUint14(offset);
        patch.params.lfoPwmDepth = decodeWebMidiUint14(offset);
        patch.params.lfoVcfDepth = decodeWebMidiUint14(offset);
        patch.params.lfoVcaDepth = decodeWebMidiUint14(offset);
        patch.ringAmount = decodeWebMidiUint14(offset);
        patch.params.ringSpeed = decodeWebMidiUint14(offset);
        return true;
    }

    void handleWebMidiSaveSlot()
    {
        if (sysexLength != WebMidiPatchPayloadLength + 7u)
            return;

        uint8_t slot = sysexBuffer[6] & 0x07u;
        PatchState patch = {};
        if (!decodeWebMidiPatch(7, patch))
            return;
        bool hadSavedSlots = savedSlotMask != 0;
        queuePatchForAudio(patch);
        savePatchToSlot(slot, patch);
        if (!hadSavedSlots)
            startupSlot = slot;
        savePatchBankIfChanged();
        slotResponseIndex = slot;
        slotResponsePatch = patch;
        slotResponsePending = true;
        slotsResponsePending = true;
    }

    int32_t decodeWebMidiUint14(uint32_t& offset) const
    {
        int32_t low = sysexBuffer[offset++] & 0x7Fu;
        int32_t high = sysexBuffer[offset++] & 0x7Fu;
        return clamp12(low | (high << 7));
    }

    void appendWebMidiUint14(uint8_t* frame, uint32_t& offset, int32_t value) const
    {
        uint32_t clipped = (uint32_t)clamp12(value);
        frame[offset++] = clipped & 0x7Fu;
        frame[offset++] = (clipped >> 7) & 0x7Fu;
    }

    SavedPatch savedPatchFromState(const PatchState& state) const
    {
        SavedPatch patch = {};
        patch.pitchOffsetQ8 = state.params.pitchOffsetQ8;
        patch.portamento = state.params.portamento;
        patch.pitchCvRange = state.params.pitchCvRange;
        patch.filterCvMode = state.params.filterCvMode;
        patch.pulseWidth = state.params.pulseWidth;
        patch.pwmAmount = state.params.pwmAmount;
        patch.sawLevel = state.params.sawLevel;
        patch.pulseLevel = state.params.pulseLevel;
        patch.sineLevel = state.params.sineLevel;
        patch.noiseLevel = state.params.noiseLevel;
        patch.voiceLevel = state.params.voiceLevel;
        patch.performancePitchQ8 = state.performancePitchQ8;
        patch.hpCutoff = state.params.hpCutoff;
        patch.lpCutoff = state.params.lpCutoff;
        patch.resonance = state.params.resonance;
        patch.expressionDepth = state.params.expressionDepth;
        patch.attack = state.params.attack;
        patch.decay = state.params.decay;
        patch.sustain = state.params.sustain;
        patch.release = state.params.release;
        patch.filterAttack = state.params.filterAttack;
        patch.filterDecay = state.params.filterDecay;
        patch.filterSustain = state.params.filterSustain;
        patch.filterRelease = state.params.filterRelease;
        patch.lfoRate = state.params.lfoRate;
        patch.lfoPitchDepth = state.params.lfoPitchDepth;
        patch.lfoPwmDepth = state.params.lfoPwmDepth;
        patch.lfoVcfDepth = state.params.lfoVcfDepth;
        patch.lfoVcaDepth = state.params.lfoVcaDepth;
        patch.ringAmount = state.ringAmount;
        patch.ringSpeed = state.params.ringSpeed;
        return patch;
    }

    PatchState patchStateFromSavedPatch(const SavedPatch& saved) const
    {
        PatchState patch = {};
        patch.params.pitchOffsetQ8 = clampPitchQ8(saved.pitchOffsetQ8);
        patch.params.portamento = clamp12(saved.portamento);
        patch.params.pitchCvRange = saved.pitchCvRange == 0 ? 0 : 1;
        patch.params.filterCvMode = clampRange(saved.filterCvMode, 0, 2);
        patch.params.pulseWidth = clampRange(saved.pulseWidth, 512, 3584);
        patch.params.pwmAmount = clamp12(saved.pwmAmount);
        patch.params.sawLevel = clamp12(saved.sawLevel);
        patch.params.pulseLevel = clamp12(saved.pulseLevel);
        patch.params.sineLevel = clamp12(saved.sineLevel);
        patch.params.noiseLevel = clamp12(saved.noiseLevel);
        patch.params.voiceLevel = clamp12(saved.voiceLevel);
        patch.performancePitchQ8 = clampRange(saved.performancePitchQ8, -2048, 2047);
        patch.params.hpCutoff = clamp12(saved.hpCutoff);
        patch.params.lpCutoff = clamp12(saved.lpCutoff);
        patch.params.resonance = clamp12(saved.resonance);
        patch.params.expressionDepth = clamp12(saved.expressionDepth);
        patch.params.attack = clamp12(saved.attack);
        patch.params.decay = clamp12(saved.decay);
        patch.params.sustain = clamp12(saved.sustain);
        patch.params.release = clamp12(saved.release);
        patch.params.filterAttack = clamp12(saved.filterAttack);
        patch.params.filterDecay = clamp12(saved.filterDecay);
        patch.params.filterSustain = clamp12(saved.filterSustain);
        patch.params.filterRelease = clamp12(saved.filterRelease);
        patch.params.lfoRate = clamp12(saved.lfoRate);
        patch.params.lfoPitchDepth = clamp12(saved.lfoPitchDepth);
        patch.params.lfoPwmDepth = clamp12(saved.lfoPwmDepth);
        patch.params.lfoVcfDepth = clamp12(saved.lfoVcfDepth);
        patch.params.lfoVcaDepth = clamp12(saved.lfoVcaDepth);
        patch.ringAmount = clamp12(saved.ringAmount);
        patch.params.ringSpeed = clamp12(saved.ringSpeed);
        return patch;
    }

    void applySavedPatch(const SavedPatch& patch)
    {
        applyPatchState(patchStateFromSavedPatch(patch));
    }

    void savePatchToSlot(uint8_t slot, const PatchState& patch)
    {
        if (slot >= PatchSlotCount)
            return;
        savedPatches[slot] = savedPatchFromState(patch);
        savedSlotMask |= 1u << slot;
    }

    void applySavedSlot(uint8_t slot)
    {
        if (slot >= PatchSlotCount || (savedSlotMask & (1u << slot)) == 0)
            return;
        applySavedPatch(savedPatches[slot]);
    }

    uint8_t loadedSlotCount() const
    {
        uint8_t count = 0;
        for (uint8_t i = 0; i < PatchSlotCount; ++i)
        {
            if ((savedSlotMask & (1u << i)) != 0)
                count++;
        }
        return count;
    }

    uint8_t slotForLoadedSelection(uint8_t selected) const
    {
        for (uint8_t slot = 0; slot < PatchSlotCount; ++slot)
        {
            if ((savedSlotMask & (1u << slot)) == 0)
                continue;
            if (selected == 0)
                return slot;
            selected--;
        }
        return firstLoadedSlot();
    }

    uint8_t firstLoadedSlot() const
    {
        for (uint8_t slot = 0; slot < PatchSlotCount; ++slot)
        {
            if ((savedSlotMask & (1u << slot)) != 0)
                return slot;
        }
        return 0;
    }

    void showSlotLeds(uint8_t slot)
    {
        uint8_t display = slot + 1u;
        LedBrightness(0, display & 1u ? 4095 : 0);
        LedBrightness(1, display & 2u ? 4095 : 0);
        LedBrightness(2, display & 4u ? 4095 : 0);
        LedBrightness(3, display & 8u ? 4095 : 0);
        LedBrightness(4, display & 16u ? 4095 : 0);
        LedBrightness(5, display & 32u ? 4095 : 0);
    }

    SavedPatchBank currentPatchBank() const
    {
        SavedPatchBank bank = {};
        bank.magic = PatchBankMagic;
        bank.version = PatchBankVersion;
        bank.size = sizeof(SavedPatchBank);
        bank.loadedMask = savedSlotMask;
        bank.startupSlot = startupSlot;
        for (uint8_t i = 0; i < PatchSlotCount; ++i)
            bank.slots[i] = savedPatches[i];
        bank.checksum = 0;
        bank.checksum = checksumPatchBank(bank);
        return bank;
    }

    const SavedPatchBank& flashPatchBank() const
    {
        return *reinterpret_cast<const SavedPatchBank*>(
            XIP_BASE + PatchBankFlashOffset);
    }

    bool isValidPatchBank(const SavedPatchBank& bank) const
    {
        return
            bank.magic == PatchBankMagic &&
            bank.version == PatchBankVersion &&
            bank.size == sizeof(SavedPatchBank) &&
            bank.checksum == checksumPatchBank(bank);
    }

    uint32_t checksumPatchBank(const SavedPatchBank& bank) const
    {
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&bank);
        uint32_t checksum = 2166136261u;

        for (uint32_t i = 0; i < sizeof(SavedPatchBank) - sizeof(uint32_t); ++i)
        {
            checksum ^= bytes[i];
            checksum *= 16777619u;
        }
        return checksum;
    }

    bool patchBankMatches(const SavedPatchBank& a, const SavedPatchBank& b) const
    {
        const uint8_t* aBytes = reinterpret_cast<const uint8_t*>(&a);
        const uint8_t* bBytes = reinterpret_cast<const uint8_t*>(&b);
        for (uint32_t i = 0; i < sizeof(SavedPatchBank); ++i)
        {
            if (aBytes[i] != bBytes[i])
                return false;
        }
        return true;
    }

    void loadPatchBank()
    {
        const SavedPatchBank& bank = flashPatchBank();
        if (!isValidPatchBank(bank))
            return;

        savedSlotMask = bank.loadedMask;
        startupSlot = bank.startupSlot < PatchSlotCount ? bank.startupSlot : firstLoadedSlot();
        for (uint8_t i = 0; i < PatchSlotCount; ++i)
            savedPatches[i] = bank.slots[i];

        if ((savedSlotMask & (1u << startupSlot)) != 0)
            applySavedSlot(startupSlot);
    }

    void savePatchBankIfChanged()
    {
        SavedPatchBank bank = currentPatchBank();
        const SavedPatchBank& saved = flashPatchBank();
        if (isValidPatchBank(saved) && patchBankMatches(bank, saved))
            return;

        uint8_t page[FLASH_PAGE_SIZE];
        const uint8_t* stateBytes = reinterpret_cast<const uint8_t*>(&bank);
        uint32_t bytesWritten = 0;

        uint32_t interrupts = save_and_disable_interrupts();
        flash_range_erase(PatchBankFlashOffset, FLASH_SECTOR_SIZE);
        while (bytesWritten < sizeof(SavedPatchBank))
        {
            for (uint32_t i = 0; i < FLASH_PAGE_SIZE; ++i)
                page[i] = 0xFF;

            for (uint32_t i = 0;
                 i < FLASH_PAGE_SIZE && bytesWritten + i < sizeof(SavedPatchBank);
                 ++i)
                page[i] = stateBytes[bytesWritten + i];

            flash_range_program(
                PatchBankFlashOffset + bytesWritten,
                page,
                FLASH_PAGE_SIZE);
            bytesWritten += FLASH_PAGE_SIZE;
        }
        restore_interrupts(interrupts);
    }

    SavedPatch savedPatches[PatchSlotCount] = {};
};

CS80Card card;
static volatile uint8_t hostMidiDeviceAddress = 0;

void core1Worker()
{
    sleep_ms(100);
    bool hostMode = card.ShouldBootUsbHost();

    if (hostMode)
        tuh_init(0);
    else
        tud_init(0);

    while (true)
    {
        card.FlushPendingWebPatch();
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

int main()
{
#if defined(CS80_OVERCLOCK_KHZ) && CS80_OVERCLOCK_KHZ
    set_sys_clock_khz(CS80_OVERCLOCK_KHZ, true);
#endif
    multicore_launch_core1(core1Worker);
    card.EnableNormalisationProbe();
    card.Run();
}
