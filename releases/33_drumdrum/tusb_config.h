#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Root hub port 0 supports both host and device. We commit to one or the
// other at boot time based on whether a Monome Grid is detected during a
// short host-mode probe; only one stack is initialised per power cycle.
#define CFG_TUSB_RHPORT0_MODE       (OPT_MODE_HOST | OPT_MODE_DEVICE)
#define CFG_TUSB_OS                 OPT_OS_PICO
#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN          __attribute__ ((aligned(4)))

#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG              0
#endif

// ── Device stack (browser WebMIDI editor) ────────────────────
#define CFG_TUD_ENDPOINT0_SIZE      64
#define CFG_TUD_MIDI                1
#define CFG_TUD_CDC                 0
#define CFG_TUD_MSC                 0
#define CFG_TUD_HID                 0
#define CFG_TUD_VENDOR              0

#define CFG_TUD_MIDI_RX_BUFSIZE     128
#define CFG_TUD_MIDI_TX_BUFSIZE     128
#define CFG_TUD_MIDI_EP_BUFSIZE     64

// ── Host stack (Monome Grid over CDC + FTDI, Music Thing 8mu over MIDI) ─────
#define CFG_TUH_ENUMERATION_BUFSIZE 256
#define CFG_TUH_HUB                 1
#define CFG_TUH_DEVICE_MAX          (CFG_TUH_HUB ? 4 : 1)
#define CFG_TUH_CDC                 1
#define CFG_TUH_CDC_FTDI            1
#define CFG_TUH_CDC_CP210X          0
#define CFG_TUH_CDC_CH34X           0
#define CFG_TUH_HID                 0
#define CFG_TUH_MSC                 0
#define CFG_TUH_VENDOR              0

// TinyUSB 0.18 (shipped with Pico SDK 2.2.0) has no MIDI host class driver,
// only a one-block descriptor-parser hint that groups class-compliant USB
// MIDI's Audio-Control + MIDIStreaming interfaces under a single driver
// (usbh.c:1681). CFG_TUH_MIDI=1 turns that hint on; the actual driver is
// our own minimal one in midi_host.cpp, registered via usbh_app_driver_get_cb.
#define CFG_TUH_MIDI                1

// Modern Monome Grids assert DTR/RTS on enumeration; older FTDI-based
// units expect 115200 8N1. Both are mext-protocol grids on the wire.
#define CFG_TUH_CDC_LINE_CONTROL_ON_ENUM 0x03
#define CFG_TUH_CDC_LINE_CODING_ON_ENUM  { 115200, 0, 0, 8 }

#ifdef __cplusplus
}
#endif
