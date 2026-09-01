/*
 * 92 USB MIDI Host Synth — Workshop System Computer program card
 *
 * Release folder: releases/92_usb_midi_host_synth
 * SysEx device ID: 79 (F0 7D 79 …) — on-wire protocol, not release number.
 *
 * 0.9.0: 121-patch voice matrix, poly engines, ADSR, drums, reverb.
 */

#include "card.h"

#include "usb_role.h"

#include "hardware/clocks.h"

int main()
{
    usbRoleRegisterHostCallbacks();
    // 200 MHz gives headroom for 4-voice poly engines + drums at 48 kHz while
    // core 1 runs TinyUSB host/device (see docs/CONTROL_FLOW.md).
    set_sys_clock_khz(200000, true);
    UsbMidiHostCard card;
    card.Run();
}
