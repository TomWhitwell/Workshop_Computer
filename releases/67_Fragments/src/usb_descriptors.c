#include "tusb.h"
#include "pico/unique_id.h"

#include <string.h>

#define USB_VID 0x2E8A
#define USB_PID 0x10C1
#define USB_BCD 0x0200

enum {
    STRING_LANGID = 0,
    STRING_MANUFACTURER,
    STRING_PRODUCT,
    STRING_SERIAL,
    STRING_LAST,
};

char const *string_desc_arr[] = {
    (const char[]){0x09, 0x04},
    "Music Thing",
    "MTMComputer",
    NULL,
};

tusb_desc_device_t const desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = USB_BCD,
    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = USB_VID,
    .idProduct = USB_PID,
    .bcdDevice = 0x0100,
    .iManufacturer = STRING_MANUFACTURER,
    .iProduct = STRING_PRODUCT,
    .iSerialNumber = STRING_SERIAL,
    .bNumConfigurations = 0x01,
};

uint8_t const *tud_descriptor_device_cb(void)
{
    return (uint8_t const *)&desc_device;
}

enum {
    ITF_NUM_MIDI = 0,
    ITF_NUM_MIDI_STREAMING,
    ITF_NUM_TOTAL,
};

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_MIDI_DESC_LEN)
#define EPNUM_MIDI 0x01

uint8_t const desc_fs_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
    TUD_MIDI_DESCRIPTOR(ITF_NUM_MIDI, 0, EPNUM_MIDI, 0x80 | EPNUM_MIDI, 64),
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return desc_fs_configuration;
}

static uint16_t desc_str[32];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;

    uint8_t chr_count = 0;

    if (index == STRING_LANGID)
    {
        memcpy(&desc_str[1], string_desc_arr[0], 2);
        chr_count = 1;
    }
    else if (index == STRING_SERIAL)
    {
        pico_unique_board_id_t id;
        pico_get_unique_board_id(&id);
        uint64_t raw_id = *(uint64_t *)&id.id;
        int serial = (int)((raw_id + 1) % 10000000ull);
        if (serial < 1000000) serial += 1000000;

        char temp[8];
        chr_count = 7;
        temp[7] = '\0';
        for (int i = 6; i >= 0; i--)
        {
            temp[i] = (char)('0' + (serial % 10));
            serial /= 10;
        }

        for (uint8_t i = 0; i < chr_count; i++)
        {
            desc_str[1 + i] = temp[i];
        }
    }
    else if (index < STRING_LAST)
    {
        char const *str = string_desc_arr[index];
        chr_count = (uint8_t)strlen(str);
        if (chr_count > 31) chr_count = 31;

        for (uint8_t i = 0; i < chr_count; i++)
        {
            desc_str[1 + i] = str[i];
        }
    }
    else
    {
        return NULL;
    }

    desc_str[0] = (TUSB_DESC_STRING << 8) | (2 * chr_count + 2);
    return desc_str;
}
