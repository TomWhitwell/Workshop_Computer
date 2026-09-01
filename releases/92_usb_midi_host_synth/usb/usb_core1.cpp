#include "card.h"

#include "config_store.h"
#include "protocol.h"
#include "runtime_state.h"

#include "pico/multicore.h"
#include "pico/time.h"
#include "tusb.h"

void UsbMidiHostCard::chooseUsbRole()
{
    g_powerState = USBPowerState();
    if (g_powerState == UFP || g_powerState == Unsupported)
        g_isUsbHost = false;
    else
        g_isUsbHost = true;
}

void UsbMidiHostCard::core1Entry() { g_card->usbCore(); }

void UsbMidiHostCard::usbCore()
{
    multicore_lockout_victim_init();
    sleep_us(150000);
    chooseUsbRole();
    if (g_isUsbHost)
        tuh_init(TUH_OPT_RHPORT);
    else
        tud_init(TUD_OPT_RHPORT);

    absolute_time_t nextPanelSend = get_absolute_time();

    while (true)
    {
        if (consumeFactoryResetLatch())
        {
            applyDefaults();
            requestSaveToFlash(kCmdSaveFlash);
        }
        if (g_flashSaveAckPending)
        {
            g_flashSaveAckPending = false;
            uint8_t cmd = g_flashSaveAckCmd;
            if (cmd != 0 && !g_isUsbHost)
            {
                uint8_t reply[] = {cmd, 1};
                sendSysEx(reply, sizeof(reply));
            }
        }
        if (g_learnNotifyPending && !g_isUsbHost)
        {
            g_learnNotifyPending = false;
            uint8_t reply[] = {
                kCmdLearnNotify,
                (uint8_t)(g_learnNotifySlot & 0x7F),
                (uint8_t)(g_learnNotifySrc & 0x7F),
                (uint8_t)(g_learnNotifyChan & 0x7F),
                (uint8_t)(g_learnNotifyId & 0x7F),
            };
            sendSysEx(reply, sizeof(reply));
        }
        if (g_isUsbHost)
            tuh_task();
        else
        {
            tud_task();
            drainDeviceMidiRx(512);
            flushSysExQueue();
            if (panelSnapshotReq_)
            {
                panelSnapshotReq_ = false;
                sendPanelState(true);
            }
            if (g_panelStream &&
                absolute_time_diff_us(get_absolute_time(), nextPanelSend) <= 0)
            {
                nextPanelSend = delayed_by_ms(get_absolute_time(), kPanelPollMs);
                sendPanelState(false);
            }
            flushSysExQueue();
        }
    }
}
