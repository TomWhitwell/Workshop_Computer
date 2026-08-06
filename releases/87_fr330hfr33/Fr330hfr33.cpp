#include "computercard.h"
#include "Fr330hfr33_LUT.h"

#include "hardware/clocks.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/regs/addressmap.h"
#include "pico/multicore.h"
#include "pico/time.h"
#include "tusb.h"
#include "usb_midi_host.h"

#include <array>
#include <stdint.h>

namespace {

constexpr uint32_t SampleRate = 48000;
constexpr int32_t PitchCountsPerVolt = 341;
constexpr uint8_t BaseMidiNote = 36;
constexpr uint32_t ControlPublishDivider = 48;
constexpr int32_t PitchUnitsPerOctave = 4096;
constexpr int32_t MinPitchUnits = -2 * PitchUnitsPerOctave;
constexpr int32_t MaxPitchUnits = 8 * PitchUnitsPerOctave;
constexpr uint32_t PersistenceMagic = 0x46333350u; // F33P
constexpr uint16_t PersistenceVersion = 2;
constexpr uint8_t PatternSlotCount = 4;
constexpr uint32_t PersistenceFlashOffset =
    (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE) &
    ~(FLASH_SECTOR_SIZE - 1u);

struct PatternSlot {
    uint8_t length;
    uint8_t initialStep;
    uint8_t playbackMode;
    uint8_t reserved;
    uint8_t note[16];
    uint8_t octave[16];
    uint8_t accent[16];
    uint8_t gate[16];
    uint8_t tie[16];
};

struct PersistentState {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint8_t scale;
    uint8_t accentProbability;
    uint8_t octaveSpan;
    uint8_t rootNote;
    uint16_t tempo;
    uint8_t baseMidiNote;
    uint8_t gateLength;
    uint8_t glideProbability;
    uint8_t midiChannel;
    uint8_t midiClockSync;
    uint8_t waveform;
    uint8_t distortion;
    uint8_t distortionAmount;
    uint8_t distortionTone;
    uint8_t filterPoles;
    uint8_t acidness;
    uint8_t patternEnabled;
    uint8_t activeSlot;
    uint8_t swing;
    PatternSlot slots[PatternSlotCount];
    uint32_t checksum;
};

static_assert(sizeof(PersistentState) <= FLASH_SECTOR_SIZE,
    "Persistent state must fit in one flash sector");

constexpr std::array<uint16_t, 577> makeReciprocalQ15Table()
{
    std::array<uint16_t, 577> table = {};
    for (uint32_t i = 0; i < table.size(); ++i) {
        uint32_t denominatorQ15 = 32768u + i * 512u;
        table[i] = (uint16_t)((1u << 30) / denominatorQ15);
    }
    return table;
}

constexpr auto ReciprocalQ15Table = makeReciprocalQ15Table();

// Pitch smoothing coefficient by sixteenth of Y travel. The values are
// intentionally perceptual rather than linear: the lower end gets out of the
// way quickly, noon is an obvious acid slide, and the top end becomes long.
constexpr int32_t GlideCoefficientQ15[17] = {
    32767, 4096, 1024, 256, 128, 64, 40, 24, 16,
    12, 9, 7, 5, 4, 3, 2, 1
};

enum class PlayMode : uint8_t {
    Mute = 0,
    Sequencer = 1,
    CvMidi = 2
};

struct HardwareSnapshot {
    int32_t mainKnob;
    int32_t xKnob;
    int32_t yKnob;
    int32_t cv1;
    int32_t cv2;
    uint32_t pulse1Edges;
    uint32_t pulse2Edges;
    uint8_t switchPosition;
    uint8_t pulse1High;
    uint8_t pulse2High;
    uint8_t reserved;
};

struct AudioParameters {
    uint32_t phaseIncrement;
    uint32_t envelopeTrigger;
    int32_t pitchMillivolts;
    int32_t cutoffQ15;
    int32_t resonanceQ12;
    int32_t envelopeDecayQ15;
    int32_t glideQ15;
    int32_t filterEnvelopeQ15;
    int32_t distortionGainQ8;
    int32_t distortionRail;
    int32_t distortionKnee;
    int32_t ratToneQ15;
    int32_t tsTrebleQ8;
    int32_t accentSweepMixQ15;
    uint8_t midiNote;
    uint8_t gate;
    uint8_t accent;
    uint8_t powerCut;
    uint8_t waveform;
    uint8_t distortion;
    uint8_t filterPoles;
};

template <typename T>
struct SharedSnapshot {
    T slots[2] = {};
    volatile uint32_t active = 0;
};

SharedSnapshot<HardwareSnapshot> hardwareShared;
SharedSnapshot<AudioParameters> audioShared;

inline int32_t clamp32(int32_t value, int32_t low, int32_t high)
{
    if (value < low)
        return low;
    if (value > high)
        return high;
    return value;
}

template <typename T>
void publishSnapshot(SharedSnapshot<T> &shared, const T &value)
{
    uint32_t next = shared.active ^ 1u;
    shared.slots[next] = value;
    __dmb();
    shared.active = next;
}

template <typename T>
bool readSnapshot(const SharedSnapshot<T> &shared, T &value)
{
    uint32_t before;
    uint32_t after;
    do {
        before = shared.active;
        __dmb();
        value = shared.slots[before];
        __dmb();
        after = shared.active;
    } while (before != after);
    return true;
}

uint32_t pitchUnitsToPhaseIncrement(int32_t units)
{
    static constexpr uint32_t SemitoneRatioQ15[13] = {
        32768, 34716, 36781, 38968, 41285, 43740, 46341,
        49097, 52016, 55109, 58386, 61858, 65536
    };
    static constexpr uint32_t C2PhaseIncrement = 5852465u;

    units = clamp32(units, MinPitchUnits, MaxPitchUnits);
    int32_t octaves = units / PitchUnitsPerOctave;
    int32_t remainder = units % PitchUnitsPerOctave;
    if (remainder < 0) {
        remainder += PitchUnitsPerOctave;
        --octaves;
    }

    int32_t semitone = (remainder * 12) / PitchUnitsPerOctave;
    int32_t start = (semitone * PitchUnitsPerOctave) / 12;
    int32_t end = ((semitone + 1) * PitchUnitsPerOctave) / 12;
    int32_t fraction = ((remainder - start) << 15) / (end - start);
    uint32_t ratio = SemitoneRatioQ15[semitone] +
        (uint32_t)((((int32_t)SemitoneRatioQ15[semitone + 1] -
        (int32_t)SemitoneRatioQ15[semitone]) * fraction) >> 15);
    uint32_t increment =
        (uint32_t)(((uint64_t)C2PhaseIncrement * ratio) >> 15);

    if (octaves >= 0)
        increment <<= octaves;
    else
        increment >>= -octaves;
    return increment;
}

uint32_t midiNoteToPhaseIncrement(uint8_t note)
{
    int32_t units = ((int32_t)note - BaseMidiNote) * 4096 / 12;
    return pitchUnitsToPhaseIncrement(units);
}

class MidiParser {
public:
    static constexpr uint8_t ConfigPayloadLength = 24;
    static constexpr uint8_t PatternPayloadLength = 92;

    void process(uint8_t byte)
    {
        if (byte == 0xF8u) {
            if (midiClockSync && midiClockRunning) {
                uint8_t swingTicks =
                    (uint8_t)(((uint16_t)swing * 4u + 50u) / 100u);
                uint8_t ticksThisStep = midiClockLongStep
                    ? (uint8_t)(12u + swingTicks)
                    : (uint8_t)(12u - swingTicks);
                if (++midiClockTicks >= ticksThisStep) {
                    midiClockTicks = 0;
                    midiClockLongStep = !midiClockLongStep;
                    ++pendingMidiSteps;
                }
            }
            return;
        }
        if (byte == 0xFAu) {
            midiClockTicks = 0;
            pendingMidiSteps = 0;
            midiClockLongStep = true;
            midiClockRunning = true;
            return;
        }
        if (byte == 0xFBu) {
            midiClockRunning = true;
            return;
        }
        if (byte == 0xFCu) {
            midiClockRunning = false;
            midiClockTicks = 0;
            pendingMidiSteps = 0;
            midiClockLongStep = true;
            return;
        }
        if (byte >= 0xF8u)
            return;

        if (byte == 0xF0u) {
            inSysex = true;
            sysexLength = 0;
            return;
        }
        if (inSysex) {
            if (byte == 0xF7u) {
                inSysex = false;
                applySysex();
            } else if (sysexLength < sizeof(sysex)) {
                sysex[sysexLength++] = byte;
            }
            return;
        }

        if (byte & 0x80u) {
            runningStatus = byte;
            dataCount = 0;
            return;
        }
        if (runningStatus < 0x80u || runningStatus >= 0xF0u)
            return;

        data[dataCount++] = byte;
        uint8_t type = runningStatus & 0xF0u;
        uint8_t needed = (type == 0xC0u || type == 0xD0u) ? 1u : 2u;
        if (dataCount < needed)
            return;
        dataCount = 0;

        if ((runningStatus & 0x0Fu) != midiChannel)
            return;
        if (type == 0x90u && data[1] != 0) {
            noteOn(data[0], data[1]);
        } else if (type == 0x80u || type == 0x90u) {
            noteOff(data[0]);
        } else if (type == 0xE0u) {
            // Standard 14-bit pitch-bend wheel: data[0] is the low seven bits,
            // data[1] the high seven bits, and 8192 is the centre position.
            pitchBend = ((int32_t)data[1] << 7) | data[0];
            pitchBend -= 8192;
        } else if (type == 0xB0u) {
            uint8_t controller = data[0] & 0x7Fu;
            uint8_t value = data[1] & 0x7Fu;
            if (controller == 1u && hostInputMode) {
                // In USB host mode, MIDI CC1 (mod wheel) remotely controls the
                // filter cutoff. Device mode deliberately leaves CC1 untouched.
                cutoffCc1Value = value;
                cutoffCc1Active = true;
                ++cutoffCc1SerialValue;
            } else if (controller == 20u) {
                // Volatile performance macro: neutral after every startup and
                // deliberately excluded from persistent editor settings.
                intensityValue = value;
            } else if (controller == 64u) {
                setSustainPedal(value >= 64u);
            } else if (controller == 121u) {
                intensityValue = 0;
                setSustainPedal(false);
            } else if (controller == 123u) {
                allNotesOff();
            }
        }
    }

    void setHostInputMode(bool enabled)
    {
        hostInputMode = enabled;
        if (!enabled && cutoffCc1Active) {
            cutoffCc1Active = false;
            ++cutoffCc1SerialValue;
        }
    }

    bool cutoffCc1IsActive() const
    {
        return cutoffCc1Active;
    }

    uint8_t cutoffCc1() const
    {
        return cutoffCc1Value;
    }

    uint32_t cutoffCc1Serial() const
    {
        return cutoffCc1SerialValue;
    }

    uint8_t intensity() const
    {
        return intensityValue;
    }

    int32_t pitchBendUnits() const
    {
        // Use the conventional fixed bend range of two semitones. Pitch units
        // are 4096 per octave, so this retains sub-cent resolution.
        int32_t divisor = pitchBend >= 0 ? 8191 : 8192;
        int64_t numerator =
            (int64_t)pitchBend * 2 * PitchUnitsPerOctave;
        int64_t denominator = (int64_t)12 * divisor;
        numerator += numerator >= 0
            ? denominator / 2
            : -(denominator / 2);
        return (int32_t)(numerator / denominator);
    }

    void allNotesOff()
    {
        for (uint32_t i = 0; i < 128; ++i) {
            heldNotes[i] = false;
            keyDown[i] = false;
        }
        sustainPedal = false;
        gate = false;
        legatoSlide = false;
    }

    void connectionLost()
    {
        allNotesOff();
        runningStatus = 0;
        dataCount = 0;
        inSysex = false;
        sysexLength = 0;
        if (cutoffCc1Active) {
            cutoffCc1Active = false;
            ++cutoffCc1SerialValue;
        }
        ++disconnectSerialValue;
    }

    uint32_t disconnectSerial() const
    {
        return disconnectSerialValue;
    }

    void connectionRestored()
    {
        ++connectionSerialValue;
    }

    uint32_t connectionSerial() const
    {
        return connectionSerialValue;
    }

    bool takeClockStep()
    {
        if (pendingMidiSteps == 0)
            return false;
        --pendingMidiSteps;
        return true;
    }

    bool clockControlsSequencer() const
    {
        return midiClockSync;
    }

    bool shouldGlide() const
    {
        return legatoSlide;
    }

    uint32_t triggerSerial() const
    {
        return articulationSerial;
    }

    uint8_t note = BaseMidiNote;
    uint8_t velocity = 100;
    uint8_t midiChannel = 0;
    bool gate = false;

    // Web editor settings. The SysEx payload keeps these in a stable order:
    // scale, accent, span, tempo LSB/MSB, root, gate, glide, channel,
    // MIDI sync, base octave, waveform, distortion, drive, tone, poles,
    // acidness, and swing.
    uint8_t scale = 0;
    uint8_t accentProbability = 32;
    uint8_t octaveSpan = 2;
    uint16_t tempo = 120;
    uint8_t rootNote = 0;
    uint8_t baseMidiNote = BaseMidiNote;
    uint8_t gateLength = 60;
    uint8_t glideProbability = 35;
    bool midiClockSync = false;
    uint8_t waveform = 0;
    uint8_t distortion = 0;
    uint8_t distortionAmount = 50;
    uint8_t distortionTone = 50;
    uint8_t filterPoles = 4;
    uint8_t acidness = 50;
    uint8_t swing = 0;
    bool patternEnabled = false;
    uint8_t patternLength = 16;
    uint8_t patternInitialStep = 0;
    bool patternReverse = false;
    bool patternPendulum = false;
    uint8_t activePatternSlot = 0;
    uint32_t patternRevision = 0;
    uint8_t patternNote[16] = {
        0, 0, 7, 0, 3, 0, 10, 0, 0, 0, 7, 3, 0, 10, 7, 3
    };
    uint8_t patternOctave[16] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0
    };
    uint8_t patternAccent[16] = {
        1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0
    };
    uint8_t patternGate[16] = {
        60, 45, 60, 35, 60, 45, 60, 35,
        60, 45, 60, 35, 60, 45, 60, 35
    };
    uint8_t patternTie[16] = {};

    void loadPersistentState()
    {
        for (uint32_t i = 0; i < PatternSlotCount; ++i)
            copyCurrentPatternTo(patternSlots[i]);
        const PersistentState& state =
            *reinterpret_cast<const PersistentState*>(
                XIP_BASE + PersistenceFlashOffset);
        if (!validPersistentState(state))
            return;

        scale = state.scale;
        accentProbability = state.accentProbability;
        octaveSpan = state.octaveSpan;
        tempo = state.tempo;
        rootNote = state.rootNote;
        baseMidiNote = state.baseMidiNote;
        gateLength = state.gateLength;
        glideProbability = state.glideProbability;
        midiChannel = state.midiChannel;
        midiClockSync = state.midiClockSync != 0;
        waveform = state.waveform;
        distortion = state.distortion;
        distortionAmount = state.distortionAmount;
        distortionTone = state.distortionTone;
        filterPoles = state.filterPoles;
        acidness = state.acidness;
        swing = state.swing <= 100 ? state.swing : 0;
        patternEnabled = state.patternEnabled != 0;
        activePatternSlot =
            state.activeSlot < PatternSlotCount ? state.activeSlot : 0;
        for (uint32_t i = 0; i < PatternSlotCount; ++i)
            patternSlots[i] = state.slots[i];
        loadPatternSlot(activePatternSlot, false);
    }

    bool takePersistenceRequest()
    {
        bool requested = persistenceRequested;
        persistenceRequested = false;
        return requested;
    }

    bool takePatternDumpRequest()
    {
        bool requested = patternDumpRequested;
        patternDumpRequested = false;
        return requested;
    }

    uint32_t fillPatternSysex(uint8_t* bytes) const
    {
        if (!bytes)
            return 0;
        bytes[0] = 0xF0u;
        bytes[1] = 0x7Du;
        bytes[2] = 'F';
        bytes[3] = '3';
        bytes[4] = '0';
        bytes[5] = '3';
        bytes[6] = 0x02u;
        bytes[7] = patternEnabled ? 1u : 0u;
        bytes[8] = patternLength;
        for (uint32_t step = 0; step < 16; ++step) {
            uint32_t offset = 9 + step * 5;
            bytes[offset] = patternNote[step];
            bytes[offset + 1] = patternOctave[step];
            bytes[offset + 2] = patternAccent[step] ? 1u : 0u;
            bytes[offset + 3] = patternGate[step];
            bytes[offset + 4] = patternTie[step] ? 1u : 0u;
        }
        bytes[89] = patternInitialStep;
        bytes[90] = patternReverse ? 1u : 0u;
        bytes[91] = patternPendulum ? 1u : 0u;
        bytes[92] = activePatternSlot;
        bytes[93] = 0xF7u;
        return 94;
    }

    void savePersistentStateIfChanged()
    {
        PersistentState state = currentPersistentState();
        const PersistentState& saved =
            *reinterpret_cast<const PersistentState*>(
                XIP_BASE + PersistenceFlashOffset);
        if (validPersistentState(saved) &&
            persistentStatesMatch(state, saved))
            return;

        uint8_t page[FLASH_PAGE_SIZE];
        const uint8_t* bytes =
            reinterpret_cast<const uint8_t*>(&state);
        uint32_t written = 0;
        uint32_t interrupts = save_and_disable_interrupts();
        flash_range_erase(PersistenceFlashOffset, FLASH_SECTOR_SIZE);
        while (written < sizeof(PersistentState)) {
            for (uint32_t i = 0; i < FLASH_PAGE_SIZE; ++i)
                page[i] = 0xFF;
            for (uint32_t i = 0;
                i < FLASH_PAGE_SIZE &&
                written + i < sizeof(PersistentState); ++i)
                page[i] = bytes[written + i];
            flash_range_program(
                PersistenceFlashOffset + written,
                page, FLASH_PAGE_SIZE);
            written += FLASH_PAGE_SIZE;
        }
        restore_interrupts(interrupts);
    }

private:
    void selectNewestHeldNote()
    {
        bool found = false;
        uint32_t newestOrder = 0;
        uint8_t newestNote = note;
        for (uint32_t i = 0; i < 128; ++i) {
            if (heldNotes[i] && (!found || heldOrder[i] > newestOrder)) {
                found = true;
                newestOrder = heldOrder[i];
                newestNote = (uint8_t)i;
            }
        }

        if (!found) {
            gate = false;
            legatoSlide = false;
            return;
        }

        note = newestNote;
        velocity = heldVelocity[newestNote];
        gate = true;
        legatoSlide = true;
    }

    void setSustainPedal(bool enabled)
    {
        if (enabled == sustainPedal)
            return;
        sustainPedal = enabled;
        if (enabled)
            return;

        for (uint32_t i = 0; i < 128; ++i) {
            if (!keyDown[i])
                heldNotes[i] = false;
        }
        if (!heldNotes[note])
            selectNewestHeldNote();
    }

    void noteOn(uint8_t nextNote, uint8_t nextVelocity)
    {
        bool hadHeldNote = gate;
        keyDown[nextNote] = true;
        heldNotes[nextNote] = true;
        heldOrder[nextNote] = ++noteOrder;
        heldVelocity[nextNote] = nextVelocity;
        note = nextNote;
        velocity = nextVelocity;
        gate = true;
        legatoSlide = hadHeldNote;
        if (!hadHeldNote)
            ++articulationSerial;
    }

    void noteOff(uint8_t releasedNote)
    {
        keyDown[releasedNote] = false;
        if (sustainPedal)
            return;
        heldNotes[releasedNote] = false;
        if (releasedNote != note)
            return;
        selectNewestHeldNote();
    }

    void applySysex()
    {
        // Educational/non-commercial manufacturer ID, "F303".
        if (sysexLength < 6 ||
            sysex[0] != 0x7Du ||
            sysex[1] != 'F' ||
            sysex[2] != '3' ||
            sysex[3] != '0' ||
            sysex[4] != '3')
            return;

        if (sysex[5] == 0x02u) {
            applyPatternSysex();
            return;
        }
        if (sysex[5] == 0x03u) {
            if (sysexLength == 7) {
                loadPatternSlot(sysex[6], true);
                patternDumpRequested = true;
            }
            return;
        }
        if (sysex[5] == 0x04u) {
            if (sysexLength == 7)
                savePatternSlot(sysex[6]);
            return;
        }
        if (sysex[5] != 0x01u ||
            (sysexLength != 17 && sysexLength != 19 &&
                sysexLength != 20 && sysexLength != 21 &&
                sysexLength != 22 && sysexLength != 23 &&
                sysexLength != 24))
            return;

        scale = sysex[6] > 5 ? 0 : sysex[6];
        accentProbability = sysex[7] > 100 ? 100 : sysex[7];
        octaveSpan = clamp32(sysex[8], 1, 4);
        tempo = clamp32(
            (uint16_t)sysex[9] | ((uint16_t)sysex[10] << 7), 30, 240);
        rootNote = sysex[11] > 11 ? 0 : sysex[11];
        gateLength = clamp32(sysex[12], 10, 95);
        glideProbability = sysex[13] > 100 ? 100 : sysex[13];
        uint8_t nextMidiChannel = sysex[14] & 0x0Fu;
        if (nextMidiChannel != midiChannel)
            allNotesOff();
        midiChannel = nextMidiChannel;
        midiClockSync = (sysex[15] & 0x01u) != 0;
        switch (sysex[16]) {
        case 24:
        case 36:
        case 48:
        case 60:
            baseMidiNote = sysex[16];
            break;
        default:
            baseMidiNote = BaseMidiNote;
            break;
        }
        if (sysexLength >= 19) {
            waveform = sysex[17] > 1 ? 0 : sysex[17];
            distortion = sysex[18] > 2 ? 0 : sysex[18];
        }
        if (sysexLength >= 20)
            distortionAmount = sysex[19] > 100 ? 100 : sysex[19];
        if (sysexLength >= 21)
            distortionTone = sysex[20] > 100 ? 100 : sysex[20];
        if (sysexLength >= 22)
            filterPoles = sysex[21] == 3 ? 3 : 4;
        if (sysexLength >= 23)
            acidness = sysex[22] > 100 ? 100 : sysex[22];
        if (sysexLength >= 24)
            swing = sysex[23] > 100 ? 100 : sysex[23];
        if (!midiClockSync) {
            midiClockRunning = false;
            midiClockTicks = 0;
            pendingMidiSteps = 0;
            midiClockLongStep = true;
        }
        persistenceRequested = true;
    }

    void applyPatternSysex()
    {
        constexpr uint8_t PreviousPatternPayloadLength = 88;
        constexpr uint8_t InitialStepPayloadLength = 89;
        constexpr uint8_t PendulumPayloadLength = 91;
        constexpr uint8_t PatternPayloadLength = 92;
        if (sysexLength != PreviousPatternPayloadLength &&
            sysexLength != InitialStepPayloadLength &&
            sysexLength != 90 &&
            sysexLength != PendulumPayloadLength &&
            sysexLength != PatternPayloadLength)
            return;

        patternEnabled = sysex[6] != 0;
        patternLength = clamp32(sysex[7], 1, 16);
        for (uint32_t step = 0; step < 16; ++step) {
            uint32_t offset = 8 + step * 5;
            patternNote[step] = clamp32(sysex[offset], 0, 12);
            patternOctave[step] = clamp32(sysex[offset + 1], 0, 3);
            patternAccent[step] = sysex[offset + 2] != 0;
            patternGate[step] =
                clamp32(sysex[offset + 3], 10, 95);
            patternTie[step] = sysex[offset + 4] != 0;
        }
        patternInitialStep = sysexLength >= InitialStepPayloadLength
            ? clamp32(sysex[88], 0, 15)
            : 0;
        patternReverse = sysexLength >= 90
            ? sysex[89] != 0
            : false;
        patternPendulum = sysexLength >= PendulumPayloadLength
            ? sysex[90] != 0
            : false;
        if (sysexLength >= PatternPayloadLength)
            activePatternSlot =
                clamp32(sysex[91], 0, PatternSlotCount - 1);
        ++patternRevision;
        persistenceRequested = true;
    }

    void copyCurrentPatternTo(PatternSlot& slot)
    {
        slot.length = patternLength;
        slot.initialStep = patternInitialStep;
        slot.playbackMode =
            patternPendulum ? 2 : (patternReverse ? 1 : 0);
        slot.reserved = 0;
        for (uint32_t i = 0; i < 16; ++i) {
            slot.note[i] = patternNote[i];
            slot.octave[i] = patternOctave[i];
            slot.accent[i] = patternAccent[i];
            slot.gate[i] = patternGate[i];
            slot.tie[i] = patternTie[i];
        }
    }

    void copyPatternFrom(const PatternSlot& slot)
    {
        patternLength = clamp32(slot.length, 1, 16);
        patternInitialStep = clamp32(slot.initialStep, 0, 15);
        patternReverse = slot.playbackMode == 1;
        patternPendulum = slot.playbackMode == 2;
        for (uint32_t i = 0; i < 16; ++i) {
            patternNote[i] = clamp32(slot.note[i], 0, 12);
            patternOctave[i] = clamp32(slot.octave[i], 0, 3);
            patternAccent[i] = slot.accent[i] != 0;
            patternGate[i] = clamp32(slot.gate[i], 10, 95);
            patternTie[i] = slot.tie[i] != 0;
        }
        ++patternRevision;
    }

    void savePatternSlot(uint8_t slot)
    {
        if (slot >= PatternSlotCount)
            return;
        activePatternSlot = slot;
        copyCurrentPatternTo(patternSlots[slot]);
        persistenceRequested = true;
    }

    void loadPatternSlot(uint8_t slot, bool persistSelection)
    {
        if (slot >= PatternSlotCount)
            return;
        activePatternSlot = slot;
        copyPatternFrom(patternSlots[slot]);
        if (persistSelection)
            persistenceRequested = true;
    }

    static uint32_t checksumPersistentState(
        const PersistentState& state)
    {
        const uint8_t* bytes =
            reinterpret_cast<const uint8_t*>(&state);
        uint32_t checksum = 2166136261u;
        for (uint32_t i = 0;
            i < sizeof(PersistentState) - sizeof(uint32_t); ++i) {
            checksum ^= bytes[i];
            checksum *= 16777619u;
        }
        return checksum;
    }

    static bool validPersistentState(const PersistentState& state)
    {
        return state.magic == PersistenceMagic &&
            state.version == PersistenceVersion &&
            state.size == sizeof(PersistentState) &&
            state.checksum == checksumPersistentState(state);
    }

    PersistentState currentPersistentState()
    {
        PersistentState state = {};
        state.magic = PersistenceMagic;
        state.version = PersistenceVersion;
        state.size = sizeof(PersistentState);
        state.scale = scale;
        state.accentProbability = accentProbability;
        state.octaveSpan = octaveSpan;
        state.rootNote = rootNote;
        state.tempo = tempo;
        state.baseMidiNote = baseMidiNote;
        state.gateLength = gateLength;
        state.glideProbability = glideProbability;
        state.midiChannel = midiChannel;
        state.midiClockSync = midiClockSync;
        state.waveform = waveform;
        state.distortion = distortion;
        state.distortionAmount = distortionAmount;
        state.distortionTone = distortionTone;
        state.filterPoles = filterPoles;
        state.acidness = acidness;
        state.swing = swing;
        state.patternEnabled = patternEnabled;
        state.activeSlot = activePatternSlot;
        for (uint32_t i = 0; i < PatternSlotCount; ++i)
            state.slots[i] = patternSlots[i];
        state.checksum = checksumPersistentState(state);
        return state;
    }

    static bool persistentStatesMatch(
        const PersistentState& a, const PersistentState& b)
    {
        const uint8_t* aBytes =
            reinterpret_cast<const uint8_t*>(&a);
        const uint8_t* bBytes =
            reinterpret_cast<const uint8_t*>(&b);
        for (uint32_t i = 0; i < sizeof(PersistentState); ++i) {
            if (aBytes[i] != bBytes[i])
                return false;
        }
        return true;
    }

    uint8_t runningStatus = 0;
    uint8_t data[2] = {};
    uint8_t dataCount = 0;
    bool inSysex = false;
    uint8_t sysex[96] = {};
    uint8_t sysexLength = 0;
    bool midiClockRunning = false;
    uint8_t midiClockTicks = 0;
    bool midiClockLongStep = true;
    uint8_t pendingMidiSteps = 0;
    bool heldNotes[128] = {};
    bool keyDown[128] = {};
    uint8_t heldVelocity[128] = {};
    uint32_t heldOrder[128] = {};
    uint32_t noteOrder = 0;
    uint32_t articulationSerial = 0;
    uint32_t disconnectSerialValue = 0;
    uint32_t connectionSerialValue = 0;
    bool legatoSlide = false;
    bool hostInputMode = false;
    bool cutoffCc1Active = false;
    uint8_t cutoffCc1Value = 0;
    uint32_t cutoffCc1SerialValue = 0;
    int32_t pitchBend = 0;
    bool sustainPedal = false;
    uint8_t intensityValue = 0;
    PatternSlot patternSlots[PatternSlotCount] = {};
    bool persistenceRequested = false;
    bool patternDumpRequested = false;
};

MidiParser midi;
volatile uint8_t hostMidiDeviceAddress = 0;

} // namespace

class Fr330hfr33 : public ComputerCard {
public:
    bool shouldBootUsbHost()
    {
        return USBPowerState() == USBPowerState_t::DFP;
    }

    void setLed(uint32_t index, uint16_t brightness)
    {
        LedBrightness(index, brightness);
    }

    void ProcessSample() override
    {
        captureHardware();
        receiveAudioParameters();

        // Holding the switch down behaves like pulling the batteries from a
        // running 303. The virtual supply falls slowly enough to expose a
        // short pitch/filter collapse instead of producing a plain digital mute.
        int32_t supplyTarget = parameters.powerCut ? 0 : 32767;
        int32_t supplyDifference = supplyTarget - supplyQ15;
        int32_t supplyStep = parameters.powerCut
            ? (supplyDifference >> 13)
            : (supplyDifference >> 8);
        if (supplyStep == 0 && supplyDifference != 0)
            supplyStep = supplyDifference > 0 ? 1 : -1;
        supplyQ15 += supplyStep;

        bool poweredGate = parameters.gate && !parameters.powerCut;
        bool explicitTrigger = parameters.envelopeTrigger != lastEnvelopeTrigger;
        bool noteTrigger = (poweredGate && !lastGate) || explicitTrigger;
        if (noteTrigger) {
            // Restarting the VCA from literal zero can create a discontinuity
            // when the previous note has not fully released. Let it attack
            // smoothly from its current level; only the filter contour needs
            // a hard restart for the acid articulation.
            filterEnvelope = 32767;
            if (parameters.accent) {
                // Model the charge delivered through the accent diode. Charge
                // is additive rather than reset, so closely spaced accented
                // notes start from the capacitor voltage left by the previous
                // note and push the following sweep progressively higher.
                int32_t chargeIncrement = 9000 +
                    ((parameters.accentSweepMixQ15 * 5000) >> 15);
                accentChargeQ15 = clamp32(
                    accentChargeQ15 + chargeIncrement, 0, 32767);
            }
        }
        lastEnvelopeTrigger = parameters.envelopeTrigger;
        lastGate = poweredGate;

        // During a battery pull, retain the VCA level that existed at the
        // instant power was removed. Pulse Out still falls immediately, but
        // Audio Out 1 now survives long enough for the virtual supply, pitch,
        // filter and resonance collapse to be heard. The supply envelope is
        // solely responsible for fading the voice to silence.
        if (!parameters.powerCut) {
            const int32_t envelopeTarget = poweredGate ? 32767 : 0;
            // Keep note-off release no faster than roughly 20 ms. Y still
            // reaches its full longer-release range, while the separate
            // filter contour retains its original fast decay range.
            constexpr int32_t FastestReleaseQ15 = 34;
            int32_t releaseCoefficient = parameters.envelopeDecayQ15;
            if (releaseCoefficient > FastestReleaseQ15)
                releaseCoefficient = FastestReleaseQ15;
            int32_t envelopeCoefficient =
                poweredGate ? 6144 : releaseCoefficient;
            int32_t envelopeDelta = envelopeTarget - envelope;
            int32_t envelopeStep =
                (envelopeDelta * envelopeCoefficient) >> 15;
            if (envelopeStep == 0 && envelopeDelta != 0)
                envelopeStep = envelopeDelta > 0 ? 1 : -1;
            envelope += envelopeStep;
        }

        // The filter contour fires at the start of every articulated note and
        // decays even while the gate remains high. This creates the audible
        // downward "wah" sweep expected from a 303-style voice.
        int32_t filterDecay = parameters.envelopeDecayQ15;
        if (parameters.accent)
            filterDecay += filterDecay >> 1;
        int32_t filterStep = (filterEnvelope * filterDecay) >> 15;
        if (filterStep == 0 && filterEnvelope != 0)
            filterStep = 1;
        filterEnvelope -= filterStep;

        // The analogue accent capacitor drains continuously through its
        // resistor. A second state follows the stored charge, with resonance
        // (the other gang of the dual pot) changing a direct MEG-like hit into
        // an increasingly smoothed capacitor-driven sweep.
        int32_t chargeStep = (accentChargeQ15 * 3) >> 15;
        if (chargeStep == 0 && accentChargeQ15 != 0)
            chargeStep = 1;
        accentChargeQ15 -= chargeStep;
        int32_t accentAttackQ15 = 4096 -
            ((parameters.accentSweepMixQ15 * 4064) >> 15);
        int32_t accentDifference =
            accentChargeQ15 - accentSweepQ15;
        int32_t accentStep =
            (accentDifference * accentAttackQ15) >> 15;
        if (accentStep == 0 && accentDifference != 0)
            accentStep = accentDifference > 0 ? 1 : -1;
        accentSweepQ15 += accentStep;

        if (smoothedIncrement == 0)
            smoothedIncrement = parameters.phaseIncrement;
        int32_t pitchDifference =
            (int32_t)parameters.phaseIncrement - (int32_t)smoothedIncrement;
        // Split the shift around the multiply so this remains a fast 32-bit
        // operation on Cortex-M0+ even at the top of the pitch range.
        int32_t pitchStep =
            ((pitchDifference >> 14) * parameters.glideQ15) >> 1;
        if (pitchStep == 0 && pitchDifference != 0)
            pitchStep = pitchDifference > 0 ? 1 : -1;
        smoothedIncrement = (uint32_t)((int32_t)smoothedIncrement + pitchStep);
        // As the supply disappears the oscillator loses roughly five
        // semitones before its output vanishes. Splitting the multiply avoids
        // a 64-bit helper in the interrupt.
        uint32_t pitchDroop = smoothedIncrement >> 2;
        uint32_t poweredIncrement =
            smoothedIncrement - pitchDroop +
            (uint32_t)(((pitchDroop >> 15) * supplyQ15));
        phase += poweredIncrement;

        uint32_t tableIndex = phase >> 26;
        uint32_t fraction = (phase >> 14) & 0x0FFFu;
        int32_t a = Fr330hfr33SawLut[tableIndex];
        int32_t b = Fr330hfr33SawLut[tableIndex + 1u];
        int32_t saw = a + (((b - a) * (int32_t)fraction) >> 12);
        int32_t oscillator = parameters.waveform
            ? ((phase & 0x80000000u) ? 2047 : -2048)
            : saw;

        int32_t dynamicCutoff = parameters.cutoffQ15 +
            ((filterEnvelope * parameters.filterEnvelopeQ15) >> 15);
        int32_t directAccent = parameters.accent
            ? filterEnvelope >> 1
            : 0;
        int32_t capacitorAccent = (accentSweepQ15 * 3) >> 2;
        int32_t accentMix = parameters.accentSweepMixQ15;
        dynamicCutoff +=
            ((directAccent * (32767 - accentMix)) +
                (capacitorAccent * accentMix)) >> 15;
        int32_t supplyAuthority = 8192 + ((supplyQ15 * 3) >> 2);
        dynamicCutoff = (dynamicCutoff * supplyAuthority) >> 15;
        dynamicCutoff = clamp32(dynamicCutoff, 96, 32200);

        int32_t poweredResonance =
            (parameters.resonanceQ12 * supplyAuthority) >> 15;
        int32_t filtered = processDiodeLadder(oscillator, dynamicCutoff,
            poweredResonance, parameters.filterPoles);
        int32_t resonanceMakeupQ15 =
            32768 + poweredResonance;
        filtered = (filtered * resonanceMakeupQ15) >> 15;
        if (parameters.accent) {
            // Add a short, contour-shaped amount of the oscillator back into
            // the filtered signal. This preserves an obvious bright accent
            // even when cutoff is already near its ceiling, where another
            // cutoff increase would be inaudible.
            int32_t accentBrightnessQ15 =
                (filterEnvelope >> 2) + (filterEnvelope >> 3);
            filtered +=
                ((oscillator - filtered) * accentBrightnessQ15) >> 15;
        }
        filtered = processDistortion(filtered, parameters.distortion,
            parameters.distortionGainQ8, parameters.distortionRail,
            parameters.distortionKnee);
        filtered = processDistortionTone(filtered, parameters.distortion,
            parameters.ratToneQ15, parameters.tsTrebleQ8);
        int32_t amplitude = envelope;

        int32_t voice = (filtered * amplitude) >> 15;
        // Leave output headroom so the accent level change survives the final
        // 12-bit output clamp instead of both accented and normal notes
        // flattening at the same rail.
        voice -= voice >> 2;
        if (parameters.accent)
            voice += (voice >> 1) + (voice >> 3);
        voice = (voice * supplyQ15) >> 15;
        // A four-pole low-pass rotates the signal phase even though both
        // outputs originate from the same oscillator sample. Two first-order
        // all-pass stages track that rotation without changing the raw saw's
        // magnitude response, so blending both outputs no longer creates the
        // pronounced parallel-filter cancellation heard in an external mixer.
        int32_t phaseAlignedOscillator =
            processRawAllpass(oscillator, dynamicCutoff,
                parameters.filterPoles);
        int32_t raw = (phaseAlignedOscillator * supplyQ15) >> 15;
        AudioOut1((int16_t)clamp32(voice, -2048, 2047));
        AudioOut2((int16_t)clamp32(raw, -2048, 2047));
        CVOut1Millivolts(
            (parameters.pitchMillivolts * supplyQ15) >> 15);
        PulseOut1(poweredGate);
    }

private:
    void captureHardware()
    {
        if (PulseIn1RisingEdge())
            ++pulse1Edges;
        if (PulseIn2RisingEdge())
            ++pulse2Edges;

        if (++publishCounter < ControlPublishDivider)
            return;
        publishCounter = 0;

        HardwareSnapshot snapshot = {};
        snapshot.mainKnob = KnobVal(Knob::Main);
        snapshot.xKnob = KnobVal(Knob::X);
        snapshot.yKnob = KnobVal(Knob::Y);
        snapshot.cv1 = CVIn1();
        snapshot.cv2 = CVIn2();
        snapshot.pulse1Edges = pulse1Edges;
        snapshot.pulse2Edges = pulse2Edges;
        snapshot.switchPosition = (uint8_t)SwitchVal();
        snapshot.pulse1High = PulseIn1();
        snapshot.pulse2High = PulseIn2();
        publishSnapshot(hardwareShared, snapshot);
    }

    void receiveAudioParameters()
    {
        uint32_t active = audioShared.active;
        if (active == audioSlot)
            return;

        __dmb();
        AudioParameters next = audioShared.slots[active];
        __dmb();

        // If core 1 published again during the copy, retain the old coherent
        // parameters and try on the next sample. The ISR never waits.
        if (active == audioShared.active) {
            parameters = next;
            audioSlot = active;
        }
    }

    int32_t processDiodeLadder(int32_t input, int32_t cutoffQ15,
        int32_t resonanceQ12, uint8_t poles)
    {
        // Three or four trapezoidal-integrator one-poles form a diode-style
        // ladder. Each mode solves its own global feedback path without
        // iteration or division in the ISR.
        uint32_t feedbackStages = poles == 3 ? 3u : 4u;
        int32_t oneMinusG = 32768 - cutoffQ15;
        int32_t constant = (oneMinusG * ladder[0]) >> 15;
        constant = ((cutoffQ15 * constant) >> 15) +
            ((oneMinusG * ladder[1]) >> 15);
        constant = ((cutoffQ15 * constant) >> 15) +
            ((oneMinusG * ladder[2]) >> 15);
        if (feedbackStages == 4) {
            constant = ((cutoffQ15 * constant) >> 15) +
                ((oneMinusG * ladder[3]) >> 15);
        }

        int32_t g2 = (cutoffQ15 * cutoffQ15) >> 15;
        int32_t gN = feedbackStages == 3
            ? (g2 * cutoffQ15) >> 15
            : (g2 * g2) >> 15;
        int32_t denominatorQ15 =
            32768 + ((resonanceQ12 * gN) >> 12);
        int32_t reciprocalQ15 = reciprocalQ15For(denominatorQ15);
        int32_t driven = input -
            ((resonanceQ12 * constant) >> 12);
        driven = (driven * reciprocalQ15) >> 15;
        driven = diodePair(driveInput(driven));

        int32_t selectedOutput = 0;
        int32_t fourthPoleOutput = 0;
        for (uint32_t i = 0; i < 4; ++i) {
            int32_t delta = driven - ladder[i];
            int32_t integrator = (delta * cutoffQ15) >> 15;
            int32_t output = integrator + ladder[i];
            // Paired-diode conduction has a broad knee rather than the early,
            // flat rail used by the previous transistor-like stage clip. This
            // leaves enough loop gain for a healthy sine at high resonance.
            ladder[i] = diodePair(output + integrator);
            driven = output;
            if (i + 1u == feedbackStages)
                selectedOutput = output;
            if (i == 3)
                fourthPoleOutput = output;
        }

        if (feedbackStages == 3) {
            // Keep the true third-pole output dominant, then emphasize the
            // spectrum that the tracked fourth pole would have removed. The
            // asymptotic response remains third order, but the audible
            // contrast with 24 dB mode is much clearer on bass waveforms.
            selectedOutput += (selectedOutput - fourthPoleOutput) >> 1;
        }
        return diodePair(selectedOutput);
    }

    int32_t processRawAllpass(int32_t input, int32_t cutoffQ15,
        uint8_t poles)
    {
        // For the TPT one-pole used by the ladder, (2 * low-pass) - input is
        // a unity-magnitude all-pass with twice the one-pole phase rotation.
        // Two normal stages follow four low-pass poles. Three-pole mode uses
        // one normal stage plus a second all-pass whose coefficient is mapped
        // to half the normal stage's low-frequency group delay. Unlike the
        // previous average of two phase paths, both modes remain true
        // unity-magnitude all-pass cascades and cannot cancel within the raw
        // output itself.
        int32_t afterTwoPoles = processRawAllpassStage(
            input, cutoffQ15, rawAllpass[0]);
        int32_t afterFourPoles = processRawAllpassStage(
            afterTwoPoles, cutoffQ15, rawAllpass[1]);

        // If g is the ladder coefficient, 2g/(1+g) gives an all-pass whose
        // group delay at low frequencies approximates one ladder pole rather
        // than two. The reciprocal table keeps division out of the ISR.
        int32_t fractionalCutoffQ15 =
            ((cutoffQ15 << 1) *
                reciprocalQ15For(32768 + cutoffQ15)) >> 15;
        fractionalCutoffQ15 =
            clamp32(fractionalCutoffQ15, 1, 32767);
        int32_t afterThreePoles = processRawAllpassStage(
            afterTwoPoles, fractionalCutoffQ15,
            rawFractionalAllpass);

        return poles == 3 ? afterThreePoles : afterFourPoles;
    }

    static int32_t processRawAllpassStage(int32_t input,
        int32_t cutoffQ15, int32_t &state)
    {
        int32_t delta = input - state;
        int32_t product = delta * cutoffQ15;
        int32_t integrator = product >= 0
            ? (product + 16384) >> 15
            : -(((-product) + 16384) >> 15);
        int32_t lowpass = integrator + state;
        state = lowpass + integrator;
        return (lowpass << 1) - input;
    }

    static int32_t processDistortion(int32_t input, uint8_t mode,
        int32_t gainQ8, int32_t rail, int32_t knee)
    {
        if (mode == 0 || gainQ8 <= 256)
            return input;

        int32_t driven = (input * gainQ8) >> 8;

        if (mode == 1) {
            // RAT-style experiment: symmetrical hard clipping. The rail falls
            // slightly as drive rises, making the amount control increasingly
            // compressed as well as more saturated.
            return clamp32(driven, -rail, rail);
        }
        if (mode == 2) {
            // Tube Screamer-style experiment: a broad soft knee. This models
            // the clipping character, not the full pedal EQ.
            bool negative = driven < 0;
            int32_t magnitude = negative ? -driven : driven;
            if (magnitude > knee)
                magnitude = knee + ((magnitude - knee) >> 2);
            if (magnitude > 2047)
                magnitude = 2047;
            return negative ? -magnitude : magnitude;
        }
        return input;
    }

    int32_t processDistortionTone(int32_t input, uint8_t mode,
        int32_t ratCutoffQ15, int32_t tsTrebleGainQ8)
    {
        if (mode == 1) {
            // RAT Filter approximation: post-clipping high-frequency roll-off.
            // The Web control is presented conventionally, dark to bright.
            ratToneState +=
                ((input - ratToneState) * ratCutoffQ15) >> 15;
            return ratToneState;
        }
        if (mode == 2) {
            // Compact active three-band approximation: a fixed mid emphasis
            // plus variable treble presence around that forward centre.
            constexpr int32_t BassCutoffQ15 = 1100;
            constexpr int32_t TrebleCutoffQ15 = 7200;
            tsBassState +=
                ((input - tsBassState) * BassCutoffQ15) >> 15;
            tsTrebleState +=
                ((input - tsTrebleState) * TrebleCutoffQ15) >> 15;
            int32_t mid = tsTrebleState - tsBassState;
            int32_t treble = input - tsTrebleState;
            int32_t output = tsBassState + ((mid * 384) >> 8) +
                ((treble * tsTrebleGainQ8) >> 8);
            return clamp32(output, -2048, 2047);
        }
        return input;
    }

    static int32_t reciprocalQ15For(int32_t denominatorQ15)
    {
        denominatorQ15 = clamp32(denominatorQ15, 32768, 327680);
        uint32_t offset = (uint32_t)(denominatorQ15 - 32768);
        uint32_t index = offset >> 9;
        uint32_t fraction = offset & 0x1FFu;
        if (index >= 576u)
            return ReciprocalQ15Table[576];
        int32_t a = ReciprocalQ15Table[index];
        int32_t b = ReciprocalQ15Table[index + 1u];
        return a + (((b - a) * (int32_t)fraction) >> 9);
    }

    static int32_t driveInput(int32_t value)
    {
        // A small pre-drive keeps the voice lively without making high cutoff,
        // resonance, and envelope settings fizz against every ladder stage.
        return value + (value >> 3);
    }

    static int32_t diodePair(int32_t value)
    {
        // Cheap piecewise approximation of an anti-parallel diode pair. It is
        // linear around zero, bends progressively, and reaches its rail much
        // more gradually than a transistor-style clipper.
        bool negative = value < 0;
        int32_t magnitude = negative ? -value : value;
        if (magnitude > 3072)
            magnitude = 2688 + ((magnitude - 3072) >> 3);
        else if (magnitude > 1536)
            magnitude = 1536 + (((magnitude - 1536) * 3) >> 2);
        if (magnitude > 3840)
            magnitude = 3840;
        return negative ? -magnitude : magnitude;
    }

    AudioParameters parameters = {
        midiNoteToPhaseIncrement(BaseMidiNote), 0, -2000,
        4096, 0, 128, 32767, 12000, 768, 1792, 960, 9600, 192, 0,
        BaseMidiNote, 0, 0, 1, 0, 0, 4
    };
    uint32_t audioSlot = 0;
    uint32_t phase = 0;
    uint32_t smoothedIncrement = 0;
    uint32_t publishCounter = 0;
    uint32_t pulse1Edges = 0;
    uint32_t pulse2Edges = 0;
    int32_t envelope = 0;
    int32_t filterEnvelope = 0;
    int32_t accentChargeQ15 = 0;
    int32_t accentSweepQ15 = 0;
    int32_t supplyQ15 = 0;
    int32_t ladder[4] = {};
    int32_t rawAllpass[2] = {};
    int32_t rawFractionalAllpass = 0;
    int32_t ratToneState = 0;
    int32_t tsBassState = 0;
    int32_t tsTrebleState = 0;
    bool lastGate = false;
    uint32_t lastEnvelopeTrigger = 0;
};

Fr330hfr33 card;

namespace {

uint32_t randomState = 0xF330F333u;

uint32_t nextRandom()
{
    randomState = randomState * 1664525u + 1013904223u;
    return randomState;
}

uint8_t quantizedRandomNote(
    uint8_t scale, uint8_t octaves, uint8_t root, uint8_t baseNote,
    uint8_t acidness)
{
    static constexpr uint8_t MajorPentatonic[] = {0, 2, 4, 7, 9};
    static constexpr uint8_t MinorPentatonic[] = {0, 3, 5, 7, 10};
    static constexpr uint8_t Ionian[] = {0, 2, 4, 5, 7, 9, 11};
    static constexpr uint8_t Lydian[] = {0, 2, 4, 6, 7, 9, 11};
    static constexpr uint8_t MelodicMinor[] = {0, 2, 3, 5, 7, 9, 11};
    static int32_t currentPosition = 0;
    static int32_t recentPositions[3] = {-1, -1, -1};
    static uint8_t previousScale = 255;
    static uint8_t previousOctaves = 0;
    static uint8_t previousRoot = 255;
    static uint8_t previousBase = 255;

    uint8_t degreeCount =
        (scale == 2 || scale == 3 || scale == 4)
        ? 7u
        : scale == 5 ? 12u : 5u;
    int32_t positionCount = degreeCount * octaves;

    if (scale != previousScale || octaves != previousOctaves ||
        root != previousRoot || baseNote != previousBase) {
        currentPosition = 0;
        for (uint32_t i = 0; i < 3; ++i)
            recentPositions[i] = -1;
        previousScale = scale;
        previousOctaves = octaves;
        previousRoot = root;
        previousBase = baseNote;
    }

    // Most steps move to a nearby scale degree, with occasional wider jumps.
    // Re-roll candidates found in the short history so small scales no longer
    // collapse into the same two- or three-note loop.
    static constexpr int8_t RestrainedMovements[16] = {
        -1, 0, 0, 1, 1, -1, 0, 1,
        -2, 2, 0, 1, -1, 0, 2, -2
    };
    static constexpr int8_t AcidMovements[16] = {
        -2, -1, -1, 1, 1, 2, -2, 2,
        -3, 3, -1, 1, -4, 4, -2, 2
    };
    int32_t candidate = currentPosition;
    for (uint32_t attempt = 0; attempt < 8; ++attempt) {
        uint32_t value = nextRandom();
        bool acidMove = (value % 100u) < acidness;
        int32_t movement = acidMove
            ? AcidMovements[value & 0x0Fu]
            : RestrainedMovements[value & 0x0Fu];
        if (acidMove && (value & 0x70u) == 0x70u)
            movement += (value & 0x80u) ? degreeCount : -degreeCount;

        candidate = currentPosition + movement;
        while (candidate < 0)
            candidate += positionCount;
        while (candidate >= positionCount)
            candidate -= positionCount;

        // Restrained settings deliberately allow repeated scale positions;
        // high Acidness increasingly rejects them in favour of motion.
        if (candidate == currentPosition &&
            (nextRandom() % 100u) <
                (uint32_t)(65 - (acidness * 55) / 100))
            break;

        bool recent = candidate == currentPosition;
        for (uint32_t i = 0; i < 3; ++i)
            recent = recent || candidate == recentPositions[i];
        if (!recent)
            break;
    }

    for (uint32_t i = 2; i > 0; --i)
        recentPositions[i] = recentPositions[i - 1];
    recentPositions[0] = currentPosition;
    currentPosition = candidate;

    uint8_t octave = (uint8_t)(currentPosition / degreeCount);
    uint8_t degreeIndex = (uint8_t)(currentPosition % degreeCount);
    uint8_t degree;
    if (scale == 1)
        degree = MinorPentatonic[degreeIndex];
    else if (scale == 2)
        degree = Ionian[degreeIndex];
    else if (scale == 3)
        degree = Lydian[degreeIndex];
    else if (scale == 4)
        degree = MelodicMinor[degreeIndex];
    else if (scale == 5)
        degree = degreeIndex;
    else
        degree = MajorPentatonic[degreeIndex];
    uint32_t note = baseNote + root + octave * 12u + degree;
    return (uint8_t)(note > 127u ? 127u : note);
}

void updateLeds(PlayMode mode, bool gate, bool accent, uint8_t note,
    uint8_t filterPoles, uint8_t activePatternSlot,
    uint16_t patternIndicatorTick)
{
    card.setLed(0, mode == PlayMode::CvMidi ? 4095 : 0);
    // In sequencer mode LED 3 identifies the active pattern slot with one to
    // four bright pulses. It remains dim between pulse groups so the mode
    // indication is never lost.
    uint16_t sequencerBrightness = 0;
    if (mode == PlayMode::Sequencer) {
        uint16_t phase = patternIndicatorTick % 200u;
        uint16_t pulseIndex = phase / 22u;
        uint16_t pulsePhase = phase % 22u;
        bool pulseOn =
            pulseIndex <= activePatternSlot && pulsePhase < 12u;
        sequencerBrightness = pulseOn ? 4095 : 96;
    }
    card.setLed(2, sequencerBrightness);
    card.setLed(4, mode == PlayMode::Mute ? 4095 : 0);
    card.setLed(1, gate ? 4095 : 96);
    card.setLed(3, accent ? 4095 : 0);
    // A solid LED 6 is an unmistakable hardware confirmation that the
    // editor successfully selected three-pole mode.
    card.setLed(5, filterPoles == 3
        ? 4095
        : (uint16_t)(((uint32_t)(note & 0x0Fu) * 3072u) / 15u));
}

void controlWorker()
{
    sleep_ms(100);
    bool hostMode = card.shouldBootUsbHost();
    if (hostMode)
        tuh_init(0);
    else
        tud_init(0);
    midi.setHostInputMode(hostMode);
    bool deviceWasActive = false;

    HardwareSnapshot hardware = {};
    AudioParameters parameters = {};
    parameters.midiNote = BaseMidiNote;
    parameters.phaseIncrement = midiNoteToPhaseIncrement(parameters.midiNote);
    parameters.pitchMillivolts = -2000;
    parameters.powerCut = 1;

    uint32_t lastPulse2Edges = 0;
    uint16_t patternIndicatorTick = 0;
    uint64_t nextInternalStep = time_us_64();
    bool internalClockLongStep = true;
    uint64_t lastExternalClockTime = 0;
    uint64_t lastSequencerStepTime = 0;
    uint64_t sequencerStepPeriod = 250000;
    uint64_t gateOffTime = 0;
    bool sequenceGate = false;
    bool sequenceAccent = false;
    bool sequenceGlide = false;
    uint8_t sequenceNote = BaseMidiNote;
    bool sequencePrepared = false;
    bool queuedLegato = false;
    uint8_t queuedNote = BaseMidiNote;
    bool queuedAccent = false;
    uint32_t sequenceTrigger = 0;
    uint8_t patternStep = 0;
    bool patternStarted = false;
    bool lastPatternEnabled = false;
    bool pendulumReverseDirection = false;
    uint32_t lastPatternRevision = midi.patternRevision;
    uint32_t lastMidiDisconnectSerial = midi.disconnectSerial();
    uint32_t lastMidiConnectionSerial = midi.connectionSerial();
    uint32_t disconnectPulse1Edges = 0;
    bool midiDisconnectLock = false;
    uint32_t ledDivider = 0;
    uint32_t lastCutoffCc1Serial = midi.cutoffCc1Serial();
    bool cutoffCc1PickupActive = false;
    bool cutoffPickupArmed = false;
    int32_t cutoffCc1Knob = 0;
    int32_t cutoffPickupStartKnob = 0;
    int32_t cutoffPickupSide = 0;

    while (true) {
        if (hostMode) {
            tuh_task();
        } else {
            tud_task();
            bool deviceMounted = tud_mounted();
            bool deviceActive = deviceMounted && !tud_suspended();
            if (deviceWasActive && !deviceActive)
                midi.connectionLost();
            else if (!deviceWasActive && deviceActive)
                midi.connectionRestored();
            // RP2040 TinyUSB forces VBUS present, so mounted and connected can
            // remain true after cable removal. Loss of host frame traffic
            // instead puts the device into USB suspend, which is the reliable
            // device-mode indication that the DAW connection has disappeared.
            deviceWasActive = deviceActive;
            uint8_t bytes[64];
            uint32_t count = tud_midi_stream_read(bytes, sizeof(bytes));
            for (uint32_t i = 0; i < count; ++i)
                midi.process(bytes[i]);
        }
        if (midi.takePersistenceRequest())
            midi.savePersistentStateIfChanged();
        if (!hostMode && tud_mounted() && !tud_suspended() &&
            midi.takePatternDumpRequest()) {
            uint8_t bytes[94];
            uint32_t count = midi.fillPatternSysex(bytes);
            if (count != 0)
                tud_midi_stream_write(0, bytes, count);
        }

        readSnapshot(hardwareShared, hardware);
        PlayMode mode = hardware.switchPosition == (uint8_t)ComputerCard::Switch::Up
            ? PlayMode::CvMidi
            : hardware.switchPosition == (uint8_t)ComputerCard::Switch::Middle
                ? PlayMode::Sequencer
                : PlayMode::Mute;

        int32_t mainKnob = clamp32(hardware.mainKnob, 0, 4095);
        int32_t xKnob = clamp32(hardware.xKnob, 0, 4095);
        int32_t yKnob = clamp32(hardware.yKnob, 0, 4095);

        uint32_t cutoffCc1Serial = midi.cutoffCc1Serial();
        if (cutoffCc1Serial != lastCutoffCc1Serial) {
            lastCutoffCc1Serial = cutoffCc1Serial;
            cutoffCc1PickupActive =
                hostMode && midi.cutoffCc1IsActive();
            if (cutoffCc1PickupActive) {
                cutoffCc1Knob =
                    ((int32_t)midi.cutoffCc1() * 4095 + 63) / 127;
                cutoffPickupStartKnob = mainKnob;
                cutoffPickupArmed = false;
                cutoffPickupSide = mainKnob < cutoffCc1Knob
                    ? -1
                    : mainKnob > cutoffCc1Knob ? 1 : 0;
            }
        }

        if (cutoffCc1PickupActive) {
            int32_t moved = mainKnob - cutoffPickupStartKnob;
            if (moved < 0)
                moved = -moved;
            if (moved >= 32)
                cutoffPickupArmed = true;

            int32_t distance = mainKnob - cutoffCc1Knob;
            int32_t currentSide =
                distance < 0 ? -1 : distance > 0 ? 1 : 0;
            int32_t magnitude = distance < 0 ? -distance : distance;
            if (cutoffPickupArmed &&
                (magnitude <= 40 ||
                    (cutoffPickupSide != 0 &&
                        currentSide != cutoffPickupSide))) {
                cutoffCc1PickupActive = false;
            }
        }

        // Hardware testing confirms Main already increases clockwise.
        // The ladder coefficient becomes perceptually open well before its
        // numerical maximum, especially while the filter envelope is active.
        // A fourth-power curve spreads that useful region across the full
        // Main/CC1 travel while retaining the same fully-open endpoint.
        int32_t cutoffKnob = cutoffCc1PickupActive
            ? cutoffCc1Knob
            : mainKnob;
        int64_t cutoffSquared =
            (int64_t)cutoffKnob * cutoffKnob;
        parameters.cutoffQ15 = 120 +
            (int32_t)((cutoffSquared * cutoffSquared * 31800) >> 48);
        if (midi.filterPoles == 3) {
            // A third-order ladder naturally sounds subtler than the pole
            // count suggests when compared at the same internal coefficient.
            // Calibrate it to a higher effective cutoff so the 18 dB setting
            // has an obvious, musically useful brighter response.
            parameters.cutoffQ15 +=
                (parameters.cutoffQ15 >> 1) + 512;
            parameters.cutoffQ15 =
                clamp32(parameters.cutoffQ15, 120, 32200);
        }
        int32_t intensity = midi.intensity();
        parameters.cutoffQ15 = clamp32(
            parameters.cutoffQ15 + (intensity * 2400) / 127,
            120, 32200);

        // Resonance uses a squared curve: most of X stays smooth and useful,
        // while the upper range now crosses the diode ladder's oscillation
        // threshold instead of merely approaching it.
        int32_t resonanceMaximum =
            midi.filterPoles == 3 ? 35000 : 20500;
        parameters.resonanceQ12 =
            (int32_t)(((int64_t)xKnob * xKnob *
                resonanceMaximum) >> 24);
        parameters.resonanceQ12 = clamp32(
            parameters.resonanceQ12 +
                (intensity * resonanceMaximum) / (127 * 10),
            0, resonanceMaximum);

        // One musically coupled control: clockwise gives a longer envelope and
        // more glide. Coefficients are calculated here, never in the ISR.
        int32_t decayInverse = 4095 - yKnob;
        parameters.envelopeDecayQ15 = 1 +
            (int32_t)(((int64_t)decayInverse * decayInverse * 95) >> 24);
        uint32_t glideIndex = (uint32_t)yKnob >> 8;
        uint32_t glideFraction = (uint32_t)yKnob & 0xFFu;
        int32_t glideA = GlideCoefficientQ15[glideIndex];
        int32_t glideB = GlideCoefficientQ15[glideIndex + 1u];
        int32_t physicalGlideQ15 =
            glideA + (((glideB - glideA) * (int32_t)glideFraction) >> 8);

        // X still adds some envelope sweep, but no longer stacks an extreme
        // sweep on top of maximum resonance.
        parameters.filterEnvelopeQ15 = clamp32(
            11000 + (xKnob * 3000) / 4095 +
                (intensity * 6000) / 127,
            0, 32767);
        parameters.waveform = midi.waveform;
        parameters.filterPoles = midi.filterPoles;
        parameters.distortion =
            midi.distortionAmount == 0 ? 0 : midi.distortion;
        parameters.distortionGainQ8 =
            256 + (int32_t)midi.distortionAmount * 1024 / 100 +
            (midi.distortion == 0 ? 0 : (intensity * 384) / 127);
        parameters.distortionRail =
            2047 - (int32_t)midi.distortionAmount * 511 / 100;
        parameters.distortionKnee =
            1280 - (int32_t)midi.distortionAmount * 640 / 100;
        parameters.ratToneQ15 =
            1200 + (int32_t)midi.distortionTone * 16800 / 100;
        parameters.tsTrebleQ8 =
            64 + (int32_t)midi.distortionTone * 256 / 100;
        // The resonance pot emulates the second gang of the original accent
        // sweep control: anti-clockwise favors the immediate envelope hit;
        // clockwise increasingly exposes the capacitor's smoothed memory.
        parameters.accentSweepMixQ15 =
            (xKnob * 32767) / 4095;

        uint64_t now = time_us_64();
        uint32_t midiDisconnectSerial = midi.disconnectSerial();
        if (midiDisconnectSerial != lastMidiDisconnectSerial) {
            lastMidiDisconnectSerial = midiDisconnectSerial;
            midiDisconnectLock = true;
            disconnectPulse1Edges = hardware.pulse1Edges;
        }
        uint32_t midiConnectionSerial = midi.connectionSerial();
        if (midiConnectionSerial != lastMidiConnectionSerial) {
            lastMidiConnectionSerial = midiConnectionSerial;
            midiDisconnectLock = false;
        }
        if (midiDisconnectLock &&
            hardware.pulse1Edges != disconnectPulse1Edges) {
            // A fresh physical gate edge explicitly hands control back to CV.
            midiDisconnectLock = false;
        }
        if (mode == PlayMode::Sequencer) {
            if (midi.patternEnabled != lastPatternEnabled ||
                midi.patternRevision != lastPatternRevision) {
                // Switching between programmed and generated playback starts
                // the selected source cleanly. Re-applying a pattern likewise
                // restarts it from its chosen Initial Step.
                lastPatternEnabled = midi.patternEnabled;
                lastPatternRevision = midi.patternRevision;
                sequencePrepared = false;
                queuedLegato = false;
                sequenceGate = false;
                patternStep = 0;
                patternStarted = false;
                pendulumReverseDirection = midi.patternReverse;
            }
            bool externalClock = hardware.pulse2Edges != lastPulse2Edges;
            if (externalClock) {
                lastPulse2Edges = hardware.pulse2Edges;
                lastExternalClockTime = now;
            }

            uint32_t bpm = clamp32(midi.tempo, 30, 240);
            uint64_t stepPeriod = 60000000ull / ((uint64_t)bpm * 2ull);
            uint64_t swingOffset =
                (stepPeriod * clamp32(midi.swing, 0, 100)) / 300u;
            uint64_t internalStepPeriod = internalClockLongStep
                ? stepPeriod + swingOffset
                : stepPeriod - swingOffset;
            bool externalClockActive =
                lastExternalClockTime != 0 &&
                now - lastExternalClockTime < 2000000ull;
            bool receivedMidiClockStep = midi.takeClockStep();
            bool midiClockStep =
                !externalClockActive && receivedMidiClockStep;
            bool midiClockControls = midi.clockControlsSequencer();
            bool internalClock = !externalClockActive && !midiClockControls &&
                now >= nextInternalStep;
            if (internalClock) {
                nextInternalStep = now + internalStepPeriod;
                internalClockLongStep = !internalClockLongStep;
            }

            if (externalClock || midiClockStep || internalClock) {
                if (internalClock) {
                    // Gate percentages follow the duration of the current
                    // swung step, keeping short and long notes proportional.
                    sequencerStepPeriod = internalStepPeriod;
                } else if (lastSequencerStepTime != 0) {
                    uint64_t measured = now - lastSequencerStepTime;
                    if (measured >= 10000ull && measured <= 2000000ull)
                        sequencerStepPeriod = measured;
                } else {
                    sequencerStepPeriod = stepPeriod;
                }
                lastSequencerStepTime = now;

                if (midi.patternEnabled) {
                    uint8_t length =
                        clamp32(midi.patternLength, 1, 16);
                    uint8_t logicalStep =
                        patternStep < length ? patternStep : 0;
                    uint8_t initialStep =
                        midi.patternInitialStep % length;
                    uint8_t currentStep = 0;
                    if (midi.patternPendulum) {
                        if (!patternStarted) {
                            patternStep = initialStep;
                            logicalStep = initialStep;
                        }
                        currentStep = logicalStep;
                    } else {
                        currentStep = midi.patternReverse
                            ? (uint8_t)(
                                (initialStep + length - logicalStep) %
                                    length)
                            : (uint8_t)(
                                (initialStep + logicalStep) % length);
                    }
                    bool mutedStep =
                        midi.patternNote[currentStep] == 12u;
                    if (mutedStep) {
                        // A muted note is an explicit rest. It also breaks a
                        // preceding tie so the previous voice stops exactly at
                        // this step boundary.
                        sequenceGate = false;
                        sequenceAccent = false;
                        sequenceGlide = false;
                        queuedLegato = false;
                        gateOffTime = now;
                    } else {
                        bool legatoTransition =
                            patternStarted && queuedLegato && sequenceGate;
                        uint8_t previousNote = sequenceNote;
                        int32_t programmedNote =
                            (int32_t)midi.baseMidiNote +
                            midi.rootNote +
                            midi.patternNote[currentStep] +
                            (int32_t)midi.patternOctave[currentStep] * 12;
                        sequenceNote =
                            (uint8_t)clamp32(programmedNote, 0, 127);
                        sequenceAccent =
                            midi.patternAccent[currentStep] != 0;
                        sequenceGlide =
                            legatoTransition &&
                            sequenceNote != previousNote;
                        if (!legatoTransition)
                            ++sequenceTrigger;
                        sequenceGate = true;
                        gateOffTime = now +
                            (sequencerStepPeriod *
                                midi.patternGate[currentStep]) / 100u;
                        // Tie belongs to the current step and carries its gate
                        // into the following step. A pitch change becomes a
                        // slide; an unchanged pitch becomes a true tie.
                        queuedLegato =
                            midi.patternTie[currentStep] != 0;
                    }
                    if (midi.patternPendulum && length > 1) {
                        if (!pendulumReverseDirection) {
                            if (logicalStep + 1u >= length) {
                                pendulumReverseDirection = true;
                                patternStep = length - 2u;
                            } else {
                                patternStep = logicalStep + 1u;
                            }
                        } else if (logicalStep == 0) {
                            pendulumReverseDirection = false;
                            patternStep = 1;
                        } else {
                            patternStep = logicalStep - 1u;
                        }
                    } else {
                        patternStep =
                            (uint8_t)((logicalStep + 1u) % length);
                    }
                    patternStarted = true;
                    sequencePrepared = true;
                } else if (!sequencePrepared) {
                    // The first note starts normally. Later notes are prepared
                    // one step early so the preceding gate can be held for a
                    // real tie or slide.
                    sequenceNote = quantizedRandomNote(
                        midi.scale, clamp32(midi.octaveSpan, 1, 4),
                        midi.rootNote, midi.baseMidiNote,
                        midi.acidness);
                    uint8_t effectiveAccentProbability =
                        (uint8_t)clamp32(
                            ((int32_t)midi.accentProbability *
                                (50 + midi.acidness)) / 100,
                            0, 100);
                    sequenceAccent =
                        (nextRandom() % 100u) <
                            effectiveAccentProbability;
                    sequenceGlide = false;
                    sequencePrepared = true;
                    ++sequenceTrigger;
                } else {
                    uint8_t previousNote = sequenceNote;
                    bool legatoTransition = queuedLegato && sequenceGate;
                    sequenceNote = queuedNote;
                    sequenceAccent = queuedAccent;
                    // Repeated legato notes are true ties. Changed legato
                    // notes keep the gate high and glide without retriggering
                    // either envelope.
                    sequenceGlide =
                        legatoTransition && sequenceNote != previousNote;
                    if (!legatoTransition)
                        ++sequenceTrigger;
                }
                if (!midi.patternEnabled) {
                    uint8_t effectiveGateLength =
                        (uint8_t)clamp32(
                            (int32_t)midi.gateLength -
                                ((int32_t)midi.acidness * 25) / 100,
                            10, 95);
                    sequenceGate = true;
                    gateOffTime = now +
                        (sequencerStepPeriod *
                            effectiveGateLength) / 100u;

                    // Glide Probability selects a transition into the next
                    // generated step. Pattern mode instead reads its explicit
                    // tie and gate lanes above.
                    uint8_t effectiveGlideProbability =
                        (uint8_t)clamp32(
                            ((int32_t)midi.glideProbability *
                                (50 + midi.acidness)) / 100,
                            0, 100);
                    queuedLegato =
                        (nextRandom() % 100u) <
                            effectiveGlideProbability;
                    bool queuedTie =
                        queuedLegato && ((nextRandom() & 3u) == 0u);
                    queuedNote = queuedTie
                        ? sequenceNote
                        : quantizedRandomNote(
                            midi.scale, clamp32(midi.octaveSpan, 1, 4),
                            midi.rootNote, midi.baseMidiNote,
                            midi.acidness);
                    uint8_t effectiveAccentProbability =
                        (uint8_t)clamp32(
                            ((int32_t)midi.accentProbability *
                                (50 + midi.acidness)) / 100,
                            0, 100);
                    queuedAccent = queuedTie
                        ? sequenceAccent
                        : (nextRandom() % 100u) <
                            effectiveAccentProbability;
                }
            }
            // A prepared legato transition suppresses note-off so the gate
            // remains high across the next step boundary. Ordinary steps
            // still use the configured gate percentage.
            if (sequenceGate && !queuedLegato && now >= gateOffTime)
                sequenceGate = false;

            parameters.midiNote = sequenceNote;
            parameters.phaseIncrement = midiNoteToPhaseIncrement(sequenceNote);
            parameters.pitchMillivolts =
                ((int32_t)sequenceNote - 60) * 1000 / 12;
            parameters.gate = sequenceGate;
            parameters.accent = sequenceAccent;
            parameters.glideQ15 =
                sequenceGlide ? physicalGlideQ15 : 32767;
            parameters.envelopeTrigger = sequenceTrigger;
            parameters.powerCut = 0;
        } else if (mode == PlayMode::CvMidi) {
            sequencePrepared = false;
            queuedLegato = false;
            sequenceGate = false;
            patternStep = 0;
            patternStarted = false;
            pendulumReverseDirection = midi.patternReverse;
            bool forceSlide = hardware.pulse2High;
            if (midi.gate) {
                int32_t midiPitchUnits =
                    ((int32_t)midi.note - BaseMidiNote) *
                    PitchUnitsPerOctave / 12 +
                    midi.pitchBendUnits();
                parameters.midiNote = midi.note;
                parameters.phaseIncrement =
                    pitchUnitsToPhaseIncrement(midiPitchUnits);
                parameters.pitchMillivolts =
                    -2000 +
                    (midiPitchUnits * 1000) / PitchUnitsPerOctave;
                parameters.gate = 1;
                parameters.accent = midi.velocity >= 112;
                parameters.glideQ15 =
                    (forceSlide || midi.shouldGlide())
                    ? physicalGlideQ15
                    : 32767;
                parameters.envelopeTrigger = midi.triggerSerial();
            } else {
                int32_t pitchUnits =
                    (hardware.cv1 * 4096) / PitchCountsPerVolt;
                int32_t semitones = (pitchUnits * 12) / 4096;
                int32_t note = clamp32(BaseMidiNote + semitones, 0, 127);
                parameters.midiNote = (uint8_t)note;
                parameters.phaseIncrement =
                    pitchUnitsToPhaseIncrement(pitchUnits);
                parameters.pitchMillivolts =
                    -2000 + (hardware.cv1 * 1000) / PitchCountsPerVolt;
                parameters.gate = hardware.pulse1High;
                parameters.accent = 0;
                parameters.glideQ15 =
                    forceSlide ? physicalGlideQ15 : 32767;
                parameters.envelopeTrigger = 0;
            }
            if (midiDisconnectLock) {
                parameters.gate = 0;
                parameters.accent = 0;
                parameters.glideQ15 = 32767;
            }
            parameters.powerCut = 0;
        } else {
            sequencePrepared = false;
            queuedLegato = false;
            sequenceGate = false;
            patternStep = 0;
            patternStarted = false;
            pendulumReverseDirection = midi.patternReverse;
            parameters.gate = 0;
            parameters.accent = 0;
            // Preserve the last articulation serial. Resetting it here looks
            // like a new trigger to the audio core and clears the held VCA
            // level, turning the battery-pull effect into an immediate mute.
            parameters.powerCut = 1;
        }

        publishSnapshot(audioShared, parameters);

        if (++ledDivider >= 20) {
            ledDivider = 0;
            ++patternIndicatorTick;
            updateLeds(mode, parameters.gate, parameters.accent,
                parameters.midiNote, parameters.filterPoles,
                midi.activePatternSlot, patternIndicatorTick);
        }
        sleep_us(500);
    }
}

} // namespace

extern "C" void tud_mount_cb(void)
{
    // TinyUSB's device callback is the authoritative indication that a DAW
    // connection has returned. Polling alone can miss short USB transitions.
    midi.connectionRestored();
}

extern "C" void tud_umount_cb(void)
{
    // Release held MIDI state at the USB event itself. The control worker
    // observes the serial change and latches its output gate low.
    midi.connectionLost();
}

extern "C" void tuh_midi_mount_cb(
    uint8_t dev_addr, uint8_t in_ep, uint8_t out_ep,
    uint8_t num_cables_rx, uint16_t num_cables_tx)
{
    (void)in_ep;
    (void)out_ep;
    (void)num_cables_rx;
    (void)num_cables_tx;
    if (hostMidiDeviceAddress == 0)
        hostMidiDeviceAddress = dev_addr;
    midi.connectionRestored();
}

extern "C" void tuh_midi_umount_cb(uint8_t dev_addr, uint8_t instance)
{
    (void)instance;
    if (dev_addr == hostMidiDeviceAddress)
        hostMidiDeviceAddress = 0;
    midi.connectionLost();
}

extern "C" void tuh_midi_rx_cb(uint8_t dev_addr, uint32_t num_packets)
{
    if (dev_addr != hostMidiDeviceAddress || num_packets == 0)
        return;

    uint8_t cable = 0;
    uint8_t bytes[128];
    while (true) {
        uint32_t count =
            tuh_midi_stream_read(dev_addr, &cable, bytes, sizeof(bytes));
        if (count == 0)
            break;
        for (uint32_t i = 0; i < count; ++i)
            midi.process(bytes[i]);
    }
}

extern "C" void tuh_midi_tx_cb(uint8_t dev_addr)
{
    (void)dev_addr;
}

int main()
{
#if defined(FR330HFR33_OVERCLOCK_KHZ) && FR330HFR33_OVERCLOCK_KHZ
    set_sys_clock_khz(FR330HFR33_OVERCLOCK_KHZ, true);
#endif
    midi.loadPersistentState();
    multicore_launch_core1(controlWorker);
    card.Run();
}
