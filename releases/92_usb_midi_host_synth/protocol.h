// Shared protocol types and constants for USB MIDI Host (SysEx device ID 79).
#pragma once

#include <cstdint>

constexpr uint8_t kMidiMfrId = 0x7D;
constexpr uint8_t kDeviceId = 79;

constexpr uint8_t kCmdPreview = 0x01;
constexpr uint8_t kCmdSaveFlash = 0x02;
constexpr uint8_t kCmdReadConfig = 0x03;
constexpr uint8_t kCmdCardId = 0x04;
constexpr uint8_t kCmdPanelState = 0x05;
constexpr uint8_t kCmdPanelStream = 0x06;
constexpr uint8_t kCmdReadMaps = 0x07;
constexpr uint8_t kCmdWriteMaps = 0x08;
constexpr uint8_t kCmdSetPerf = 0x09; // voice[, ADSR][, cutoff, PWM] — RAM
constexpr uint8_t kCmdLearnNotify = 0x0A; // SETUP learn: slot, src, chan, id
constexpr uint8_t kCmdReadProfile = 0x0B; // ProcessSample peak_us + overrun + budget

constexpr uint8_t kFwMajor = 0;
constexpr uint8_t kFwMinor = 10;
constexpr uint8_t kFwPatch = 0;

// 11×11 voice matrix (121 patches, CC 0–120). See VOICE_MATRIX.md.
constexpr uint8_t kVoiceMatrixRows = 11;
constexpr uint8_t kVoiceMatrixCols = 11;
constexpr uint8_t kVoiceMatrixCount = 121;
constexpr uint8_t kVoiceMatrixMax = 120;
constexpr uint8_t kVoiceEngineCount = kVoiceMatrixCount;
constexpr uint8_t kVoiceEngineMax = kVoiceMatrixMax;

// Set to 0 to force clickless gate only (ignores A/D/S/R). Also bypasses at
// runtime when Attack=Decay=Release=0 and Sustain=127 (easy Live UI kill).
#ifndef USB_MIDI_HOST_ADSR
#define USB_MIDI_HOST_ADSR 1
#endif
constexpr bool kAdsrEnabled = (USB_MIDI_HOST_ADSR != 0);

constexpr uint8_t kConfigMarker = 0x4D;
constexpr uint8_t kExtMarker = 0x59; // v0.10 — no arp/reverb in ExtConfig
constexpr int kConfigLen = 8;
constexpr int kNumSlots = 13;

constexpr uint8_t MIDI_NOTE_OFF = 0x80;
constexpr uint8_t MIDI_NOTE_ON = 0x90;
constexpr uint8_t MIDI_CC = 0xB0;
constexpr uint8_t MIDI_PITCHBEND = 0xE0;
constexpr uint8_t MIDI_CC_ALL_SOUND_OFF = 120;
constexpr uint8_t MIDI_CC_ALL_NOTES_OFF = 123;

constexpr uint8_t kSrcNone = 0;
constexpr uint8_t kSrcCc = 1;
constexpr uint8_t kSrcNote = 2;
constexpr uint8_t kSrcKnobX = 3;
constexpr uint8_t kSrcKnobY = 4;
constexpr uint8_t kChanOmni = 16;

constexpr uint8_t kSlotVolume = 0; // reserved (no master volume)
constexpr uint8_t kSlotChA = 1;
constexpr uint8_t kSlotChB = 2;
constexpr uint8_t kSlotBend = 3;
constexpr uint8_t kSlotVoice = 4;
constexpr uint8_t kSlotReserved5 = 5; // was arp — unused
constexpr uint8_t kSlotReserved6 = 6; // was reverb — unused
constexpr uint8_t kSlotAttack = 7;
constexpr uint8_t kSlotDecay = 8;
constexpr uint8_t kSlotSustain = 9;
constexpr uint8_t kSlotRelease = 10;
constexpr uint8_t kSlotCutoff = 11;
constexpr uint8_t kSlotPwm = 12;

constexpr uint8_t kDefaultCcVoice = 24;

enum class AppMode : uint8_t { Play = 0, Setup = 1 };

struct CardConfig
{
    uint8_t channelA;
    uint8_t channelB;
    uint8_t bendSemitones;
    uint8_t flags;
    uint8_t reserved[3];
    uint8_t marker;
};

struct MapSlot
{
    uint8_t sourceType;
    uint8_t channel; // 0-15 or Omni
    uint8_t ccOrNote;
    uint8_t pad;
};

struct __attribute__((packed)) ExtConfig
{
    uint8_t marker;
    uint8_t audioVoice; // voiceId 0..120 (row*11+col); see VOICE_MATRIX.md
    uint8_t attack;     // 0-127
    uint8_t decay;      // 0-127
    uint8_t sustain;    // 0-127
    uint8_t releaseAmp; // 0-127
    uint8_t cutoff;     // 0-127 (LPF)
    uint8_t pwmWidth;   // 0-127 (pulse/square duty)
    MapSlot slots[kNumSlots];
};

static_assert(sizeof(MapSlot) == 4, "MapSlot size");
static_assert(sizeof(ExtConfig) == 8 + kNumSlots * 4, "ExtConfig size");
