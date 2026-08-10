/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
 extern "C" {
#endif

//--------------------------------------------------------------------
// COMMON CONFIGURATION
//--------------------------------------------------------------------

// defined by compiler flags for flexibility
#ifndef CFG_TUSB_MCU
  #error CFG_TUSB_MCU must be defined
#endif

// USB host only mode
#if CFG_TUSB_MCU == OPT_MCU_LPC43XX || CFG_TUSB_MCU == OPT_MCU_LPC18XX || CFG_TUSB_MCU == OPT_MCU_MIMXRT10XX
  #define CFG_TUSB_RHPORT0_MODE   (OPT_MODE_HOST | OPT_MODE_HIGH_SPEED)
#else
  #define CFG_TUSB_RHPORT0_MODE   OPT_MODE_HOST
#endif

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS               OPT_OS_NONE
#endif

/* USB DMA on some MCUs can only access a specific SRAM region with restriction on alignment.
 * Tinyusb use follows macros to declare transferring memory so that they can be put
 * into those specific section.
 * e.g
 * - CFG_TUSB_MEM SECTION : __attribute__ (( section(".usb_ram") ))
 * - CFG_TUSB_MEM_ALIGN   : __attribute__ ((aligned(4)))
 */
#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif

#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN          __attribute__ ((aligned(4)))
#endif

//--------------------------------------------------------------------
// HOST CONFIGURATION
//--------------------------------------------------------------------

// Size of buffer to hold descriptors and other data used for enumeration
#define CFG_TUH_ENUMERATION_BUFSIZE 256

#define CFG_TUH_HUB                 1   // Enable USB hubs
#define CFG_TUH_CDC                 0
#define CFG_TUH_HID                 0
// NOTE: Do NOT #define CFG_TUH_MIDI 1 — the rppicomidi host driver is used instead
#define CFG_TUH_MSC                 0   // no mass storage on this card
#define CFG_TUH_VENDOR              0

// Only one device (the Scion) is ever addressed
#define CFG_TUH_DEVICE_MAX          1

// rppicomidi driver buffers. Without these the driver falls back to a single
// 64-byte bulk endpoint packet for each.
#define CFG_TUH_MIDI_RX_BUFSIZE     128
#define CFG_TUH_MIDI_TX_BUFSIZE     128
#define CFG_TUH_MIDI_EP_BUFSIZE     64

// MIDI Host string support
#define CFG_MIDI_HOST_DEVSTRINGS    1

#ifdef __cplusplus
 }
#endif

#endif /* _TUSB_CONFIG_H_ */
