// Device-mode SysEx command handling (editor protocol).
#pragma once

#include <cstdint>

// Implemented by UsbMidiHostCard (USB write + panel TX).
struct SysExTransport
{
    virtual ~SysExTransport() = default;
    virtual bool sendSysEx(uint8_t *data, uint32_t size) = 0;
    virtual void onPanelStreamEnable() = 0;
    virtual void sendPanelSnapshot(bool force) = 0;
};

void sysexProcessIncoming(SysExTransport &tx, uint8_t *data, uint32_t size);
void sysexSendMapsReply(SysExTransport &tx);
void sysexSendCardId(SysExTransport &tx);
void sysexSendConfig(SysExTransport &tx);
