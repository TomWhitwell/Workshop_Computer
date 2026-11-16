/// @file tusb_config.h
/// @brief TinyUSB device configuration for the USB-enabled Computer firmware.
///
/// Device-only full-speed RP2040 stack with one CDC port. The OS layer is
/// deliberately OPT_OS_NONE: TinyUSB critical sections then disable only
/// USBCTRL_IRQ instead of globally masking IRQs or taking Pico spinlocks,
/// which is valid because all TinyUSB work stays on core0 foreground (audio
/// DMA IRQ priority 0x00 preempts USB 0xc0; PWM IRQ 0xff sits below both).
/// Buffers and queues are bounded. USB is included in every supported build;
/// scripts/build.sh produces the single alien_sitar.uf2 image.
#pragma once
// RP2040 USB device controller backend selection.
#define CFG_TUSB_MCU OPT_MCU_RP2040
// Pico OS abstraction uses global IRQ masking/spinlocks. NONE disables only
// USBCTRL_IRQ when protecting TinyUSB queues; the audio DMA IRQ stays live.
#define CFG_TUSB_OS OPT_OS_NONE
// Port 0 device role only, full speed; no host stack and no dual-role.
#define CFG_TUSB_RHPORT0_MODE (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)
// Control endpoint 0 wMaxPacketSize; usb_descriptors.cpp echoes the same value.
#define CFG_TUD_ENDPOINT0_SIZE 64
// Exactly one CDC class; MSC/HID/MIDI/vendor are compiled out (usb_descriptors.cpp).
#define CFG_TUD_CDC 1
#define CFG_TUD_MSC 0
#define CFG_TUD_HID 0
#define CFG_TUD_MIDI 0
#define CFG_TUD_VENDOR 0
// CDC FIFO/endpoint memory in bytes: 256-wide RX and TX app FIFOs, 64-byte
// endpoint FIFOs matching the configuration descriptor, and the tud_task
// event queue holding 8 entries; debug tracing is disabled.
#define CFG_TUD_CDC_RX_BUFSIZE 256
#define CFG_TUD_CDC_TX_BUFSIZE 256
#define CFG_TUD_CDC_EP_BUFSIZE 64
#define CFG_TUD_TASK_QUEUE_SZ 8
#define CFG_TUSB_DEBUG 0
// Early B0/B1 enumeration workaround borrows GPIO15; keep panel pins untouched.
#define TUD_OPT_RP2040_USB_DEVICE_ENUMERATION_FIX 0
