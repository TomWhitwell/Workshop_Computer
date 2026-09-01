#include "sysex_editor.h"

#include "config_store.h"
#include "profile_meter.h"
#include "protocol.h"
#include "runtime_state.h"

#include <cstring>

void sysexSendMapsReply(SysExTransport &tx)
{
    uint8_t reply[1 + sizeof(ExtConfig)];
    reply[0] = kCmdReadMaps;
    memcpy(reply + 1, &g_ext, sizeof(ExtConfig));
    tx.sendSysEx(reply, sizeof(reply));
}

void sysexSendCardId(SysExTransport &tx)
{
    uint8_t reply[] = {kCmdCardId, kDeviceId, kFwMajor, kFwMinor, kFwPatch};
    tx.sendSysEx(reply, sizeof(reply));
}

void sysexSendConfig(SysExTransport &tx)
{
    uint8_t reply[1 + kConfigLen];
    reply[0] = kCmdReadConfig;
    memcpy(reply + 1, &g_config, kConfigLen);
    tx.sendSysEx(reply, sizeof(reply));
}

void sysexSendProfile(SysExTransport &tx)
{
    uint32_t peak = g_processSampleMeter.peakUs();
    if (peak > 0x3FFF)
        peak = 0x3FFF;
    uint8_t reply[] = {
        kCmdReadProfile,
        (uint8_t)((peak >> 7) & 0x7F),
        (uint8_t)(peak & 0x7F),
        (uint8_t)(g_processSampleMeter.overrun() ? 1 : 0),
        (uint8_t)((kProcessSampleBudgetUs >> 7) & 0x7F),
        (uint8_t)(kProcessSampleBudgetUs & 0x7F),
    };
    tx.sendSysEx(reply, sizeof(reply));
}

void sysexProcessIncoming(SysExTransport &tx, uint8_t *data, uint32_t size)
{
    if (size < 1)
        return;
    g_webLinked = true;
    uint8_t cmd = data[0];

    if (cmd == kCmdPreview && size >= 1 + kConfigLen)
        applyConfigBytes(data + 1, size - 1);
    else if (cmd == kCmdSaveFlash && size >= 1 + kConfigLen)
    {
        applyConfigBytes(data + 1, size - 1);
        requestSaveToFlash(kCmdSaveFlash);
    }
    else if (cmd == kCmdReadConfig)
        sysexSendConfig(tx);
    else if (cmd == kCmdCardId)
        sysexSendCardId(tx);
    else if (cmd == kCmdPanelStream && size >= 2)
    {
        g_panelStream = (data[1] != 0);
        if (g_panelStream)
        {
            tx.onPanelStreamEnable();
            tx.sendPanelSnapshot(true);
        }
    }
    else if (cmd == kCmdPanelState)
        tx.sendPanelSnapshot(true);
    else if (cmd == kCmdReadProfile)
        sysexSendProfile(tx);
    else if (cmd == kCmdReadMaps)
        sysexSendMapsReply(tx);
    else if (cmd == kCmdWriteMaps && size >= 1 + sizeof(ExtConfig))
    {
        ExtConfig ext;
        memcpy(&ext, data + 1, sizeof(ExtConfig));
        if (ext.marker == kExtMarker)
        {
            sanitizeExtConfig(ext);
            g_ext = ext;
            requestSaveToFlash(kCmdWriteMaps);
        }
    }
    else if (cmd == kCmdSetPerf && size >= 2)
    {
        uint8_t v = data[1];
        if (v <= kVoiceMatrixMax)
            g_ext.audioVoice = v;
        if (size >= 6)
        {
            g_ext.attack = data[2] & 0x7F;
            g_ext.decay = data[3] & 0x7F;
            g_ext.sustain = data[4] & 0x7F;
            g_ext.releaseAmp = data[5] & 0x7F;
        }
        if (size >= 8)
        {
            g_ext.cutoff = data[6] & 0x7F;
            g_ext.pwmWidth = data[7] & 0x7F;
        }
    }
}
