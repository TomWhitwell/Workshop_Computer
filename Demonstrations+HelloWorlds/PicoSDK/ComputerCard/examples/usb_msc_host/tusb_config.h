// TinyUSB configuration: host-only, mass storage class, single device, no hub

#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CFG_TUSB_MCU
#error CFG_TUSB_MCU must be defined
#endif

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS                 OPT_OS_NONE
#endif

#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG              0
#endif

#define CFG_TUH_MEM_SECTION
#define CFG_TUH_MEM_ALIGN           __attribute__ ((aligned(4)))

#define CFG_TUH_ENABLED             1
#define BOARD_TUH_RHPORT            0
#define CFG_TUH_MAX_SPEED           OPT_MODE_DEFAULT_SPEED

#define CFG_TUH_ENUMERATION_BUFSIZE 256
#define CFG_TUH_HUB                 0
#define CFG_TUH_CDC                 0
#define CFG_TUH_HID                 0
#define CFG_TUH_MSC                 1
#define CFG_TUH_VENDOR              0
#define CFG_TUH_DEVICE_MAX          1

#ifdef __cplusplus
}
#endif

#endif
