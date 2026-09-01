#include "config_store.h"

#include "param_maps.h"
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

constexpr uint8_t kExtMarkerV09 = 0x58;

struct __attribute__((packed)) LegacyExtConfigV09
{
    uint8_t marker;
    uint8_t audioVoice;
    uint8_t arpMode;
    uint8_t reverbWet;
    uint8_t attack;
    uint8_t decay;
    uint8_t sustain;
    uint8_t releaseAmp;
    uint8_t cutoff;
    uint8_t pwmWidth;
    MapSlot slots[kNumSlots];
};

static_assert(sizeof(LegacyExtConfigV09) == 10 + kNumSlots * 4,
              "LegacyExtConfigV09 size");

bool migrateLegacyExtConfigV09(const uint8_t *flashExt, ExtConfig &out)
{
    LegacyExtConfigV09 leg;
    memcpy(&leg, flashExt, sizeof(leg));
    if (leg.marker != kExtMarkerV09)
        return false;
    out.marker = kExtMarker;
    out.audioVoice = leg.audioVoice;
    out.attack = leg.attack;
    out.decay = leg.decay;
    out.sustain = leg.sustain;
    out.releaseAmp = leg.releaseAmp;
    out.cutoff = leg.cutoff;
    out.pwmWidth = leg.pwmWidth;
    memcpy(out.slots, leg.slots, sizeof(out.slots));
    return true;
}

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

// RAM-resident: safe while XIP is suspended during erase/program (core 1).
static void __not_in_flash_func(flashProgramConfigPage)(uint32_t addr,
                                                       const uint8_t *buf)
{
    flash_range_erase(addr, FLASH_SECTOR_SIZE);
    flash_range_program(addr, buf, FLASH_PAGE_SIZE);
}

void applyMapDefaults()
{
    memset(&g_ext, 0, sizeof(g_ext));
    g_ext.marker = kExtMarker;
    g_ext.audioVoice = 0;
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
    g_ext.slots[kSlotReserved5].sourceType = kSrcNone;
    g_ext.slots[kSlotReserved6].sourceType = kSrcNone;
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
    resetKnobMappedBaseline();
}

void sanitizeExtConfig(ExtConfig &ext)
{
    if (ext.audioVoice <= 12)
        ext.audioVoice = migrateLegacyEngine(ext.audioVoice);
    else if (ext.audioVoice > kVoiceMatrixMax)
        ext.audioVoice = 0;
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
    // Strip legacy arp/reverb learn maps from older flash.
    ext.slots[kSlotReserved5].sourceType = kSrcNone;
    ext.slots[kSlotReserved6].sourceType = kSrcNone;
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
    bool migrated = false;
    if (ext.marker == kExtMarker)
    {
        // current layout
    }
    else if (migrateLegacyExtConfigV09(flash + kConfigLen, ext))
    {
        migrated = true;
    }
    else
        return;
    sanitizeExtConfig(ext);
    g_ext = ext;
    resetKnobMappedBaseline();
    if (migrated)
        requestSaveToFlash(0);
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
    // Core 0 (audio) is the lockout victim; this runs on core 1 (USB loop).
    if (!multicore_lockout_victim_is_initialized(0))
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
    flashProgramConfigPage(kConfigFlashAddr, g_flashProgramBuf);
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
