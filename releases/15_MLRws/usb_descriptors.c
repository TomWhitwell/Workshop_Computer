/**
 * usb_descriptors.c — CDC device descriptors for device mode.
 *
 * When connected to a PC (UFP), the Workshop Computer appears as a
 * single CDC serial device.  The firmware auto-detects whether incoming
 * bytes are sample-manager commands (ASCII 'I','E','W','R','S') or
 * mext LED data, so one endpoint serves both roles.
 */

#include "tusb.h"
#include "pico/unique_id.h"
#include <string.h>
#include <stdio.h>

#define USB_VID   0x2E8A  /* Raspberry Pi */
#define USB_PID   0x10C2  /* MTM Workshop Computer MLRws */
#define USB_BCD   0x0200

/* ---- Device Descriptor ---- */

tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = USB_BCD,
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = USB_VID,
    .idProduct          = USB_PID,
    .bcdDevice          = 0x0100,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01
};

uint8_t const *tud_descriptor_device_cb(void)
{
    return (uint8_t const *)&desc_device;
}

/* ---- Configuration Descriptor ---- */

enum {
    ITF_NUM_CDC = 0,
    ITF_NUM_CDC_DATA,
    ITF_NUM_TOTAL
};

#define CONFIG_TOTAL_LEN      (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN)
#define EPNUM_CDC_NOTIF       0x81
#define EPNUM_CDC_OUT         0x02
#define EPNUM_CDC_IN          0x82

uint8_t const desc_fs_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return desc_fs_configuration;
}

/* ---- String Descriptors ---- */

enum {
    STRID_LANGID = 0,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL,
    STRID_CDC_IF
};

char const *string_desc_arr[] = {
    (const char[]){ 0x09, 0x04 },  /* 0: English */
    "Music Thing Modular",          /* 1: Manufacturer */
    "MLRws",                      /* 2: Product */
    NULL,                           /* 3: Serial (dynamic) */
    "MLRws Serial",                /* 4: CDC interface */
};

static uint16_t _desc_str[32];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;
    uint8_t chr_count;

    if (index == STRID_LANGID) {
        memcpy(&_desc_str[1], string_desc_arr[0], 2);
        chr_count = 1;
    } else if (index == STRID_SERIAL) {
        pico_unique_board_id_t id;
        pico_get_unique_board_id(&id);
        char serial[18];
        snprintf(serial, sizeof(serial), "%02X%02X%02X%02X%02X%02X%02X%02X",
                 id.id[0], id.id[1], id.id[2], id.id[3],
                 id.id[4], id.id[5], id.id[6], id.id[7]);
        chr_count = (uint8_t)strlen(serial);
        if (chr_count > 31) chr_count = 31;
        for (uint8_t i = 0; i < chr_count; i++) _desc_str[1 + i] = serial[i];
    } else {
        if (index >= sizeof(string_desc_arr) / sizeof(string_desc_arr[0])) return NULL;
        const char *str = string_desc_arr[index];
        chr_count = (uint8_t)strlen(str);
        if (chr_count > 31) chr_count = 31;
        for (uint8_t i = 0; i < chr_count; i++) _desc_str[1 + i] = str[i];
    }

    _desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return _desc_str;
}
