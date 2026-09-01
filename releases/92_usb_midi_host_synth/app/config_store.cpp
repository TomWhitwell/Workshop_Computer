#include "config_store.h"

#include "voice_matrix.h"

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/multicore.h"
#include "runtime_state.h"

#include <cstring>

namespace {

constexpr uint32_t kConfigFlashAddr =
    (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE) -
    ((PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE) % FLASH_SECTOR_SIZE);

uint8_t g_flashPageBuf[FLASH_PAGE_SIZE];
uint8_t g_flashProgramBuf[FLASH_PAGE_SIZE];
volatile bool g_flashSaveReq = false;
volatile bool g_flashBusy = false;
volatile bool g_flashSavePending = false;
volatile uint8_t g_flashPendingAckCmd = 0;

void fillFlashPageBuf()
{
    memset(g_flashPageBuf, 0xFF, sizeof(g_flashPageBuf));
    g_config.marker = kConfigMarker;
    g_ext.marker = kExtMarker;
    memcpy(g_flashPageBuf, &g_config, kConfigLen);
    memcpy(g_flashPageBuf + kConfigLen, &g_ext, sizeof(ExtConfig));
}

} // namespace

CardConfig g_config = {0, 1, 2, 1, {0, 0, 0}, kConfigMarker};
ExtConfig g_ext;
volatile bool g_flashSaveAckPending = false;
volatile uint8_t g_flashSaveAckCmd = 0;

void applyMapDefaults()
{
    memset(&g_ext, 0, sizeof(g_ext));
    g_ext.marker = kExtMarker;
    g_ext.audioVoice = 0;
    g_ext.arpMode = 0;
    g_ext.reverbWet = 0;
    g_ext.attack = 0;
    g_ext.decay = 0;
    g_ext.sustain = 127;
    g_ext.releaseAmp = 0;
    g_ext.cutoff = 127;
    g_ext.pwmWidth = 0;
    for (int i = 0; i < kNumSlots; ++i)
    {
        g_ext.slots[i].sourceType = kSrcNone;
        g_ext.slots[i].channel = kChanOmni;
        g_ext.slots[i].ccOrNote = 0;
    }
    g_ext.slots[kSlotVoice].sourceType = kSrcCc;
    g_ext.slots[kSlotVoice].channel = kChanOmni;
    g_ext.slots[kSlotVoice].ccOrNote = kDefaultCcVoice;
    g_ext.slots[kSlotArp].sourceType = kSrcCc;
    g_ext.slots[kSlotArp].channel = kChanOmni;
    g_ext.slots[kSlotArp].ccOrNote = kDefaultCcArp;
    g_ext.slots[kSlotReverb].sourceType = kSrcCc;
    g_ext.slots[kSlotReverb].channel = kChanOmni;
    g_ext.slots[kSlotReverb].ccOrNote = kDefaultCcReverb;
    g_ext.slots[kSlotAttack].sourceType = kSrcKnobX;
    g_ext.slots[kSlotRelease].sourceType = kSrcKnobY;
}

void applyDefaults()
{
    g_config.channelA = 0;
    g_config.channelB = 1;
    g_config.bendSemitones = 2;
    g_config.flags = 1;
    g_config.reserved[0] = g_config.reserved[1] = g_config.reserved[2] = 0;
    g_config.marker = kConfigMarker;
    applyMapDefaults();
}

void sanitizeExtConfig(ExtConfig &ext)
{
    if (ext.audioVoice <= 12)
        ext.audioVoice = migrateLegacyEngine(ext.audioVoice);
    else if (ext.audioVoice > kVoiceMatrixMax)
        ext.audioVoice = 0;
    if (ext.arpMode > 8)
        ext.arpMode = 0;
    ext.reverbWet &= 0x7F;
    ext.attack &= 0x7F;
    ext.decay &= 0x7F;
    ext.sustain &= 0x7F;
    ext.releaseAmp &= 0x7F;
    ext.cutoff &= 0x7F;
    ext.pwmWidth &= 0x7F;
    for (int i = 0; i < kNumSlots; ++i)
    {
        if (ext.slots[i].sourceType > kSrcKnobY)
            ext.slots[i].sourceType = kSrcNone;
        if (ext.slots[i].channel > kChanOmni)
            ext.slots[i].channel = kChanOmni;
        ext.slots[i].ccOrNote &= 0x7F;
    }
    // Slot 0 was master volume — strip legacy CC/knob maps from flash.
    ext.slots[kSlotVolume].sourceType = kSrcNone;
    ext.slots[kSlotVolume].channel = kChanOmni;
    ext.slots[kSlotVolume].ccOrNote = 0;
}

void loadConfigFromFlash()
{
    const uint8_t *flash =
        reinterpret_cast<const uint8_t *>(XIP_BASE + kConfigFlashAddr);
    applyDefaults();
    if (flash[kConfigLen - 1] != kConfigMarker)
        return;
    CardConfig loaded;
    memcpy(&loaded, flash, kConfigLen);
    if (loaded.channelA > 15 || loaded.channelB > 15 ||
        loaded.bendSemitones < 1 || loaded.bendSemitones > 12)
        return;
    g_config = loaded;
    ExtConfig ext;
    memcpy(&ext, flash + kConfigLen, sizeof(ExtConfig));
    if (ext.marker != kExtMarker)
        return;
    sanitizeExtConfig(ext);
    g_ext = ext;
}

void requestSaveToFlash(uint8_t ackCmd)
{
    if (g_flashBusy)
    {
        g_flashSavePending = true;
        if (ackCmd != 0)
            g_flashPendingAckCmd = ackCmd;
        return;
    }
    fillFlashPageBuf();
    if (ackCmd != 0)
        g_flashSaveAckCmd = ackCmd;
    g_flashSaveReq = true;
}

void serviceFlashSaveRequest()
{
    if (g_flashBusy)
        return;
    if (!g_flashSaveReq && !g_flashSavePending)
        return;
    if (!multicore_lockout_victim_is_initialized(1))
        return;

    if (g_flashSavePending && !g_flashSaveReq)
    {
        fillFlashPageBuf();
        if (g_flashPendingAckCmd != 0)
            g_flashSaveAckCmd = g_flashPendingAckCmd;
        g_flashPendingAckCmd = 0;
        g_flashSavePending = false;
    }

    g_flashSaveReq = false;
    g_flashBusy = true;
    memcpy(g_flashProgramBuf, g_flashPageBuf, FLASH_PAGE_SIZE);

    multicore_lockout_start_blocking();
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(kConfigFlashAddr, FLASH_SECTOR_SIZE);
    flash_range_program(kConfigFlashAddr, g_flashProgramBuf, FLASH_PAGE_SIZE);
    restore_interrupts(ints);
    multicore_lockout_end_blocking();

    g_flashBusy = false;
    g_configSavedFlashTimer = 24000;
    if (g_flashSaveAckCmd != 0)
        g_flashSaveAckPending = true;

    if (g_flashSavePending)
    {
        fillFlashPageBuf();
        g_flashSaveAckCmd = g_flashPendingAckCmd;
        g_flashPendingAckCmd = 0;
        g_flashSavePending = false;
        g_flashSaveReq = true;
    }
}

void applyConfigBytes(const uint8_t *data, uint32_t size)
{
    if (size < kConfigLen)
        return;
    if (data[0] > 15 || data[1] > 15)
        return;
    uint8_t bend = data[2];
    if (bend < 1)
        bend = 1;
    if (bend > 12)
        bend = 12;
    g_config.channelA = data[0];
    g_config.channelB = data[1];
    g_config.bendSemitones = bend;
    g_config.flags = data[3];
    g_config.reserved[0] = data[4];
    g_config.reserved[1] = data[5];
    g_config.reserved[2] = data[6];
    g_config.marker = kConfigMarker;
}
