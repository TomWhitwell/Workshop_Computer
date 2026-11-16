#pragma once
#define CFG_TUSB_MCU OPT_MCU_RP2040
// Pico OS abstraction uses global IRQ masking/spinlocks. NONE disables only
// USBCTRL_IRQ when protecting TinyUSB queues; the audio DMA IRQ stays live.
#define CFG_TUSB_OS OPT_OS_NONE
#define CFG_TUSB_RHPORT0_MODE (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)
#define CFG_TUD_ENDPOINT0_SIZE 64
#define CFG_TUD_CDC 1
#define CFG_TUD_MSC 0
#define CFG_TUD_HID 0
#define CFG_TUD_MIDI 0
#define CFG_TUD_VENDOR 0
#define CFG_TUD_CDC_RX_BUFSIZE 256
#define CFG_TUD_CDC_TX_BUFSIZE 256
#define CFG_TUD_CDC_EP_BUFSIZE 64
#define CFG_TUD_TASK_QUEUE_SZ 8
#define CFG_TUSB_DEBUG 0
// Early B0/B1 enumeration workaround borrows GPIO15; keep panel pins untouched.
#define TUD_OPT_RP2040_USB_DEVICE_ENUMERATION_FIX 0
