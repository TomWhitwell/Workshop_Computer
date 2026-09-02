// UsbMidiHostCard — core 0 audio/panel orchestration.
#pragma once

#define COMPUTERCARD_NOIMPL
#include "ComputerCard.h"

#include "protocol.h"
#include "sysex_editor.h"

#include "pico/time.h"

#include <cstdint>

class UsbMidiHostCard : public ComputerCard, public SysExTransport
{
public:
    UsbMidiHostCard();

    void ProcessSample() override;

    void writeMidiCv(uint8_t noteA, int16_t bendA, uint8_t noteB, int16_t bendB,
                     uint8_t bendSemitones);

    static void core1Entry();
    void usbCore();

    void serviceSetupControls();
    bool consumeFactoryResetLatch();

    // SysExTransport (device-mode editor replies).
    bool sendSysEx(uint8_t *data, uint32_t size) override;
    void onPanelStreamEnable() override;
    void sendPanelSnapshot(bool force) override;

    // Called from usb/ translation units.
    void drainDeviceMidiRx(unsigned maxBytes = 256);
    void flushSysExQueue();
    void sendPanelState(bool force = false);
    void parseDeviceMidiBytes(uint8_t *rxBuf, unsigned bytesReceived);
    void chooseUsbRole();

    uint32_t sampleCount() const { return sampleCount_; }

    // Panel / boot state shared with panel_setup.cpp.
    uint32_t bootSamples_ = 0;
    bool bootHoldOk_ = true;
    bool factoryResetLatch_ = false;
    uint32_t zHold_ = 0;
    bool zToggleArmed_ = true;
    uint32_t modeFlash_ = 0;
    uint8_t modeFlashKind_ = 0;
    Switch lastSwitch_ = Middle;
    uint32_t midiActivityTimer_ = 0;

    static constexpr uint8_t kLedSubsample = 48; // ~1 kHz LED updates @ 48 kHz audio
    uint8_t ledPhase_ = 0;
    uint8_t ledShadow_[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    void setLedIfChanged(int i, bool on);

private:
    bool midiStreamWriteMessage(uint8_t const *data, uint32_t size);
    bool enqueueSysEx(uint8_t const *msg, uint32_t total);

    static constexpr unsigned kSysexBufSize = 256;
    uint8_t sysexBuf_[kSysexBufSize];
    unsigned sysexLen_ = 0;
    bool sysexActive_ = false;

    bool midiTxBusy_ = false;
    bool panelSnapshotReq_ = false;
    static constexpr uint8_t kTxqDepth = 4;
    static constexpr uint32_t kTxqMsgMax = 3 + sizeof(ExtConfig) + 4 + 1;
    uint8_t txqBuf_[kTxqDepth][kTxqMsgMax];
    uint16_t txqLen_[kTxqDepth] = {};
    uint8_t txqHead_ = 0;
    uint8_t txqTail_ = 0;
    uint8_t txqCount_ = 0;

    bool panelTxValid_ = false;
    uint8_t panelTxPayload_[19] = {};
    uint16_t panelTxMain_ = 0;
    uint16_t panelTxX_ = 0;
    uint16_t panelTxY_ = 0;
    absolute_time_t panelTxAt_ = nil_time;

    static constexpr int kPanelKnobHyst = 48;
    static constexpr int kPanelMinIntervalMs = 100;
    static constexpr int kPanelKeepaliveMs = 1000;
    static constexpr int kPanelPollMs = 40;

    uint32_t sampleCount_ = 0;
};

extern UsbMidiHostCard *g_card;
extern volatile ComputerCard::USBPowerState_t g_powerState;
