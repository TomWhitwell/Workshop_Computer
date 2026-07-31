//
// Copyright(C) 2026 ws-doom contributors
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// DESCRIPTION:
//   USB device descriptors: a single CDC-ACM interface.
//

#if WS_USB

#include "tusb.h"
#include "pico/unique_id.h"

#define WS_USB_VID 0x2E8A  // Raspberry Pi
#define WS_USB_PID 0x000A  // generic pico-sdk CDC PID

static const tusb_desc_device_t desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    // use IAD-aware class so hosts bind CDC correctly
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = WS_USB_VID,
    .idProduct = WS_USB_PID,
    .bcdDevice = 0x0100,
    .iManufacturer = 1,
    .iProduct = 2,
    .iSerialNumber = 3,
    .bNumConfigurations = 1,
};

const uint8_t *tud_descriptor_device_cb(void) {
    return (const uint8_t *) &desc_device;
}

enum { ITF_NUM_CDC = 0, ITF_NUM_CDC_DATA, ITF_NUM_TOTAL };

#define EPNUM_CDC_NOTIF 0x81
#define EPNUM_CDC_OUT   0x02
#define EPNUM_CDC_IN    0x82

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN)

static const uint8_t desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0, 100),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),
};

const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
    (void) index;
    return desc_configuration;
}

static const char *string_desc[] = {
    NULL,                       // 0: language (special-cased below)
    "Music Thing Modular",      // 1
    "WS-DOOM Computer Card",    // 2
    NULL,                       // 3: serial (from flash unique id)
    "WS-DOOM data",             // 4
};

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void) langid;
    static uint16_t desc[32];
    uint8_t len;

    if (index == 0) {
        desc[1] = 0x0409;
        len = 1;
    } else if (index == 3) {
        char serial[PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 2 + 1];
        pico_get_unique_board_id_string(serial, sizeof(serial));
        for (len = 0; serial[len] && len < 31; len++) desc[1 + len] = serial[len];
    } else if (index < TU_ARRAY_SIZE(string_desc) && string_desc[index]) {
        const char *s = string_desc[index];
        for (len = 0; s[len] && len < 31; len++) desc[1 + len] = s[len];
    } else {
        return NULL;
    }

    desc[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * len + 2));
    return desc;
}

#endif // WS_USB
