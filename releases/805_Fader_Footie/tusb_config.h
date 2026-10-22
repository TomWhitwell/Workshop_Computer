// TinyUSB configuration for Fader Footie.
//
// The card is only ever a USB MIDI device: the game runs in the web page, and
// the 8mu plugs into the computer, not the card.

#ifndef FADER_FOOTIE_TUSB_CONFIG_H
#define FADER_FOOTIE_TUSB_CONFIG_H

#define CFG_TUSB_RHPORT0_MODE     OPT_MODE_DEVICE

#define CFG_TUD_ENDPOINT0_SIZE    64
#define CFG_TUD_CDC               0
#define CFG_TUD_MSC               0
#define CFG_TUD_HID               0
#define CFG_TUD_MIDI              1
#define CFG_TUD_VENDOR            0
#define CFG_TUD_MIDI_RX_BUFSIZE   256
#define CFG_TUD_MIDI_TX_BUFSIZE   256

#endif
