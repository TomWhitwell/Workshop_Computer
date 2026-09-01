#include "usb_role.h"

#include "midi_parser.h"
#include "runtime_state.h"
#include "voices.h"

#include "tusb.h"
#include "usb_midi_host.h"

void usbRoleRegisterHostCallbacks() {}

void tuh_midi_mount_cb(uint8_t dev_addr, uint8_t /*in_ep*/, uint8_t /*out_ep*/,
                       uint8_t /*num_cables_rx*/, uint16_t /*num_cables_tx*/)
{
    g_midiDevAddr = dev_addr;
    g_midiConnected = true;
}

void tuh_midi_umount_cb(uint8_t dev_addr, uint8_t /*instance*/)
{
    if (dev_addr == g_midiDevAddr)
    {
        g_midiDevAddr = 0;
        g_midiConnected = false;
        forcePlayModeCleanup();
    }
}

void tuh_midi_rx_cb(uint8_t dev_addr, uint32_t num_packets)
{
    if (dev_addr != g_midiDevAddr || num_packets == 0)
        return;
    uint8_t cableNum;
    uint8_t buffer[512];
    uint32_t bytesRead;
    while ((bytesRead = tuh_midi_stream_read(dev_addr, &cableNum, buffer,
                                             sizeof(buffer))) > 0)
    {
        parseMidiStream(buffer, bytesRead);
        g_midiActivity = true;
    }
}

void tuh_midi_tx_cb(uint8_t /*dev_addr*/) {}
