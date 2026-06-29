#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
 extern "C" {
#endif

#ifndef CFG_TUSB_MCU
  #define CFG_TUSB_MCU              OPT_MCU_RP2040
#endif

// Enable both Device and Host mode support on the controller
#define CFG_TUSB_RHPORT0_MODE       (OPT_MODE_HOST | OPT_MODE_DEVICE)

#ifndef CFG_TUSB_OS
  #define CFG_TUSB_OS                 OPT_OS_NONE
#endif

//--------------------------------------------------------------------
// DEVICE CONFIGURATION
//--------------------------------------------------------------------
#ifndef CFG_TUD_ENDPOINT0_SIZE
  #define CFG_TUD_ENDPOINT0_SIZE    64
#endif

// Enable CDC and MIDI class drivers
#define CFG_TUD_CDC               0
#define CFG_TUD_MIDI              1

// MIDI FIFO size
#define CFG_TUD_MIDI_RX_BUFSIZE   1024
#define CFG_TUD_MIDI_TX_BUFSIZE   1024

//--------------------------------------------------------------------
// HOST CONFIGURATION
//--------------------------------------------------------------------
#define CFG_TUH_ENUMERATION_BUFSIZE 1024
#define CFG_TUH_HUB                 1 // Enable USB hubs
#define CFG_TUH_DEVICE_MAX          (CFG_TUH_HUB ? 4 : 1)

#define CFG_TUH_CDC                 0
#define CFG_TUH_HID                 0
#define CFG_TUH_MSC                 0
#define CFG_TUH_VENDOR              0

// MIDI Host string support
#define CFG_MIDI_HOST_DEVSTRINGS    1

#ifdef __cplusplus
 }
#endif

#endif /* _TUSB_CONFIG_H_ */
