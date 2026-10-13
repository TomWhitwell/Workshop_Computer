/*
 * TinyUSB configuration for the Dot Dash morse code card.
 *
 * This card acts as a USB *host*: you plug a USB keyboard into the
 * Workshop Computer and the card listens for key presses. Because this is
 * host mode (not device mode), we do NOT need usb_descriptors.c - the PC is
 * the USB host, so the RP2040 only needs the host stack enabled.
 *
 * Derived from the ComputerCard hid_keyboard_mouse example and the TinyUSB
 * project defaults (https://tinyusb.org), MIT licensed.
 */

#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
 extern "C" {
#endif

//--------------------------------------------------------------------
// Common Configuration
//--------------------------------------------------------------------

#ifndef CFG_TUSB_MCU
#error CFG_TUSB_MCU must be defined
#endif

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS           OPT_OS_NONE
#endif

#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG        0
#endif

#ifndef CFG_TUH_MEM_SECTION
#define CFG_TUH_MEM_SECTION
#endif

#ifndef CFG_TUH_MEM_ALIGN
#define CFG_TUH_MEM_ALIGN     __attribute__ ((aligned(4)))
#endif

//--------------------------------------------------------------------
// Host Configuration
//--------------------------------------------------------------------

// Enable the USB host stack (we are a host, listening to a keyboard).
#define CFG_TUH_ENABLED       1

#if CFG_TUSB_MCU == OPT_MCU_RP2040
  // This card uses the on-chip USB controller, not pio-usb or max3421,
  // so the root hub port stays at the board default (0).
#endif

#define CFG_TUH_MAX_SPEED     BOARD_TUH_MAX_SPEED

//------------------------- Board Specific --------------------------

#ifndef BOARD_TUH_RHPORT
#define BOARD_TUH_RHPORT      0
#endif

#ifndef BOARD_TUH_MAX_SPEED
#define BOARD_TUH_MAX_SPEED   OPT_MODE_DEFAULT_SPEED
#endif

//--------------------------------------------------------------------
// Driver Configuration
//--------------------------------------------------------------------

// Buffer for describing a device while it enumerates.
#define CFG_TUH_ENUMERATION_BUFSIZE 256

#define CFG_TUH_HUB                 0
#define CFG_TUH_CDC                 0
// A typical keyboard shows 1-2 HID interfaces; the multiplier gives room for
// a few devices if a hub is used later.
#define CFG_TUH_HID                 (6*CFG_TUH_DEVICE_MAX)
#define CFG_TUH_MSC                 0
#define CFG_TUH_VENDOR              0

// max device support (excluding hub device)
#define CFG_TUH_DEVICE_MAX          (5*CFG_TUH_HUB + 1)

//------------- HID -------------//
#define CFG_TUH_HID_EP_BUFSIZE      64

#ifdef __cplusplus
 }
#endif

#endif /* _TUSB_CONFIG_H_ */
