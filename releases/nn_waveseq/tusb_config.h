// TinyUSB configuration for Wave Sequencer.
//
// The card is a USB host when an 8mu is plugged in, and a USB MIDI device
// (for the web editor) when a computer is, so both modes are compiled in and
// one is started at power-up. The host side, and the usb_midi_host driver,
// come from EightMU.h.

#ifndef WAVESEQ_TUSB_CONFIG_H
#define WAVESEQ_TUSB_CONFIG_H

#define CFG_TUSB_RHPORT0_MODE     (OPT_MODE_HOST | OPT_MODE_DEVICE)

#define CFG_TUD_ENDPOINT0_SIZE    64
#define CFG_TUD_CDC               0
#define CFG_TUD_MSC               0
#define CFG_TUD_HID               0
#define CFG_TUD_MIDI              1
#define CFG_TUD_VENDOR            0
#define CFG_TUD_MIDI_RX_BUFSIZE   256
#define CFG_TUD_MIDI_TX_BUFSIZE   256

#include "EightMU.h"

#endif
