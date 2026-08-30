// TRACT8 - TinyUSB configuration.
//
// Host mode only. The card reads from a Music Thing 8mu (or any other
// class-compliant USB MIDI controller) plugged into the front USB-C jack;
// it never presents itself as a USB device. That keeps the config small
// and leaves the whole device-side stack out of the binary.

#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
 extern "C" {
#endif

#ifndef CFG_TUSB_MCU
  #define CFG_TUSB_MCU              OPT_MCU_RP2040
#endif

// Host only. Cards that also want to be a USB device to a computer set
// (OPT_MODE_HOST | OPT_MODE_DEVICE) here and choose at boot from the USB-C
// CC pins; TRACT8 has nothing to say to a computer, so it is host always.
#define CFG_TUSB_RHPORT0_MODE       OPT_MODE_HOST

#ifndef CFG_TUSB_OS
  #define CFG_TUSB_OS               OPT_OS_NONE
#endif

//--------------------------------------------------------------------
// HOST CONFIGURATION
//--------------------------------------------------------------------

#define CFG_TUH_ENUMERATION_BUFSIZE 1024

// Hub support, so an 8mu behind a hub still works. Costs three extra
// device slots' worth of state.
#define CFG_TUH_HUB                 1
#define CFG_TUH_DEVICE_MAX          (CFG_TUH_HUB ? 4 : 1)

#define CFG_TUH_CDC                 0
#define CFG_TUH_HID                 0
#define CFG_TUH_MSC                 0
#define CFG_TUH_VENDOR              0

// DO NOT #define CFG_TUH_MIDI 1 HERE. It looks like the obvious way to
// turn on MIDI host support and it is not - it breaks the build.
//
// TinyUSB 0.18 has no MIDI host class driver at all. What CFG_TUH_MIDI
// gates is a fragment in usbh.c that forces the interface-association
// count to 2 for MIDI devices, and that fragment references
// AUDIO_SUBCLASS_CONTROL and AUDIO_FUNC_PROTOCOL_CODE_UNDEF without
// including the audio class header, so enabling it fails to compile:
//
//   usbh.c:1686: error: 'AUDIO_SUBCLASS_CONTROL' undeclared
//
// The upstream ComputerCard midi_host example carries the same warning,
// and 20_reverb repeats it. The actual driver is rppicomidi's, registered
// through usbh_app_driver_get_cb() in usb_midi_host_app_driver.c, and it
// does its own descriptor parsing - it does not need this macro.

// RX FIFO for the MIDI host driver, bytes.
//
// The driver defaults this to one bulk endpoint's worth - 64 bytes, or 16
// USB-MIDI packets. An 8mu streams six accelerometer controllers
// continuously and adds fader traffic on top, so 64 bytes is one
// scheduling hiccup away from overflowing, and tu_fifo drops silently when
// it does: a CC vanishes mid-stream and that fader stops responding until
// its next message.
//
// 512 bytes is 128 packets, which is more than arrives between two turns
// of the core 1 loop by a wide margin. It costs 448 bytes of RAM against
// the 20% currently used.
#define CFG_TUH_MIDI_RX_BUFSIZE   512
#define CFG_TUH_MIDI_EP_BUFSIZE   64

// Device-name strings from the 8mu. Cheap, and useful when debugging which
// controller actually enumerated.
#define CFG_MIDI_HOST_DEVSTRINGS    1

#ifdef __cplusplus
 }
#endif

#endif /* _TUSB_CONFIG_H_ */
