#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

//--------------------------------------------------------------------
// COMMON CONFIGURATION
//--------------------------------------------------------------------

// RHPort number used for dual-role operation
#define BOARD_DEVICE_RHPORT_NUM     0

// RHPort max operational speed
#define BOARD_DEVICE_RHPORT_SPEED   OPT_MODE_FULL_SPEED

// Enable host stack optionally; dual-role is on by default for auto host/device selection.
// Define ENABLE_USB_HOST=0 to force device-only if needed.
#ifndef ENABLE_USB_HOST
#define ENABLE_USB_HOST 1
#endif

#if ENABLE_USB_HOST
#define CFG_TUSB_RHPORT0_MODE       (OPT_MODE_HOST | OPT_MODE_DEVICE)
#else
#define CFG_TUSB_RHPORT0_MODE       (OPT_MODE_DEVICE)
#endif

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS                 OPT_OS_PICO
#endif

#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG              0
#endif

// CFG_TUSB_MEM_SECTION and CFG_TUSB_MEM_ALIGN can be defined for special placement
#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif

#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN          __attribute__ ((aligned(4)))
#endif

//--------------------------------------------------------------------
// DEVICE CONFIGURATION
//--------------------------------------------------------------------

#ifndef CFG_TUD_ENDPOINT0_SIZE
#define CFG_TUD_ENDPOINT0_SIZE      64
#endif

//------------- CLASS -------------//
#define CFG_TUD_CDC                 1
#define CFG_TUD_MSC                 0
#define CFG_TUD_HID                 0
#define CFG_TUD_MIDI                1
#define CFG_TUD_VENDOR              0

// CDC FIFO size of TX and RX
// Increased from 256 to 2048 bytes for better throughput and reduced buffer rotation
// RP2040 has plenty of RAM (264KB total), and larger buffers help with:
// - Script uploads (less fragmentation)
// - Burst writes (pubview, debug output)
// - Reduced IRQ overhead from buffer management
#define CFG_TUD_CDC_RX_BUFSIZE      2048
#define CFG_TUD_CDC_TX_BUFSIZE      2048

// MIDI FIFO sizes (small but enough for realtime note events)
#define CFG_TUD_MIDI_RX_BUFSIZE     128
#define CFG_TUD_MIDI_TX_BUFSIZE     128

//--------------------------------------------------------------------
// HOST CONFIGURATION
//--------------------------------------------------------------------

// Size of buffer to hold descriptors and other data used for enumeration
#define CFG_TUH_ENUMERATION_BUFSIZE 1024

#define CFG_TUH_HUB                 1 // Enable USB hubs
#define CFG_TUH_CDC                 0
#define CFG_TUH_HID                 0
#define CFG_TUH_MSC                 0
#define CFG_TUH_VENDOR              0

// max device support (excluding hub device)
#define CFG_TUH_DEVICE_MAX          (CFG_TUH_HUB ? 4 : 1)

// MIDI Host string support (optional, keep minimal)
#define CFG_MIDI_HOST_DEVSTRINGS    0

#define CFG_TUH_MIDI_RX_BUFSIZE     (TUD_OPT_HIGH_SPEED ? 512 : 64)
#define CFG_TUH_MIDI_TX_BUFSIZE     (TUD_OPT_HIGH_SPEED ? 512 : 64)
#define CFG_TUH_MIDI_EP_BUFSIZE     TUH_EPSIZE_BULK_MAX

#ifdef __cplusplus
}
#endif

#endif /* _TUSB_CONFIG_H_ */
