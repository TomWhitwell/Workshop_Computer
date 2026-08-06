/*
 * TinyUSB configuration for monome grid USB host on the Workshop Computer.
 *
 * Enables CDC host (for modern grids with native USB) and FTDI host
 * (for older grids using FTDI USB-to-serial chips).  Both use the
 * same tuh_cdc_* API.
 */

#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

/*--------------------------------------------------------------------
 * COMMON
 *--------------------------------------------------------------------*/

#ifndef CFG_TUSB_MCU
  #error CFG_TUSB_MCU must be defined
#endif

#define CFG_TUSB_RHPORT0_MODE       (OPT_MODE_HOST | OPT_MODE_DEVICE)

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS                 OPT_OS_NONE
#endif

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif

#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN          __attribute__((aligned(4)))
#endif

/*--------------------------------------------------------------------
 * DEVICE (single CDC — sample manager + remote mext)
 *--------------------------------------------------------------------*/

#ifndef CFG_TUD_ENDPOINT0_SIZE
#define CFG_TUD_ENDPOINT0_SIZE      64
#endif

#define CFG_TUD_CDC                 1
#define CFG_TUD_CDC_RX_BUFSIZE      256
#define CFG_TUD_CDC_TX_BUFSIZE      256
#define CFG_TUD_MSC                 0
#define CFG_TUD_HID                 0
#define CFG_TUD_MIDI                0
#define CFG_TUD_VENDOR              0

/*--------------------------------------------------------------------
 * HOST (monome grid)
 *--------------------------------------------------------------------*/

#define CFG_TUH_ENUMERATION_BUFSIZE 256

/* Enable USB hub support (monome may be behind a hub) */
#define CFG_TUH_HUB                1

/* Standard CDC-ACM host — covers modern grids with native USB */
#define CFG_TUH_CDC                1

/* FTDI serial host — covers older grids with FT232R/FT230X chips */
#define CFG_TUH_CDC_FTDI           1

/* We don't need these class drivers */
#define CFG_TUH_HID                0
#define CFG_TUH_MSC                0
#define CFG_TUH_VENDOR             0

/* Max devices (hub can expose up to 4 ports) */
#define CFG_TUH_DEVICE_MAX         (CFG_TUH_HUB ? 4 : 1)

/* Line-coding defaults for monome serial: 115200 8N1 */
#define CFG_TUH_CDC_LINE_CODING_ON_ENUM { 115200, CDC_LINE_CODING_STOP_BITS_1, CDC_LINE_CODING_PARITY_NONE, 8 }

/* Assert DTR + RTS on enumeration — FTDI chips won't pass data without DTR */
#define CFG_TUH_CDC_LINE_CONTROL_ON_ENUM (CDC_CONTROL_LINE_STATE_DTR | CDC_CONTROL_LINE_STATE_RTS)

#ifdef __cplusplus
}
#endif

#endif /* _TUSB_CONFIG_H_ */
