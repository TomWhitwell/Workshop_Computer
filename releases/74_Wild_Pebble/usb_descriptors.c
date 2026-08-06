#include "tusb.h"
#include <string.h>

#define USB_VID 0x2E8A
#define USB_PID 0x00C0
#define USB_BCD 0x0200

enum
{
    ITF_NUM_MIDI = 0,
    ITF_NUM_MIDI_STREAMING,
    ITF_NUM_TOTAL
};

enum
{
    CONFIG_TOTAL_LEN =
        TUD_CONFIG_DESC_LEN + TUD_MIDI_DESC_LEN
};

#define EPNUM_MIDI_OUT 0x01
#define EPNUM_MIDI_IN  0x81

tusb_desc_device_t const desc_device =
{
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = USB_BCD,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor = USB_VID,
    .idProduct = USB_PID,
    .bcdDevice = 0x0100,

    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,

    .bNumConfigurations = 0x01
};

uint8_t const * tud_descriptor_device_cb(void)
{
    return (uint8_t const *) &desc_device;
}

uint8_t const desc_configuration[] =
{
    TUD_CONFIG_DESCRIPTOR(1,
                          ITF_NUM_TOTAL,
                          0,
                          CONFIG_TOTAL_LEN,
                          0,
                          100),

    TUD_MIDI_DESCRIPTOR(ITF_NUM_MIDI,
                        4,
                        EPNUM_MIDI_OUT,
                        EPNUM_MIDI_IN,
                        64)
};

uint8_t const * tud_descriptor_configuration_cb(uint8_t index)
{
    (void) index;
    return desc_configuration;
}

char const *string_desc_arr[] =
{
    (const char[]) { 0x09, 0x04 },
    "Music Thing Modular",
    "Wild Pebble MIDI",
    "WildPebbleMIDI",
    "Wild Pebble MIDI Port"
};

static uint16_t desc_str[32];

uint16_t const * tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void) langid;

    uint8_t chr_count;

    if(index == 0)
    {
        memcpy(&desc_str[1], string_desc_arr[0], 2);
        chr_count = 1;
    }
    else
    {
        if(index >= sizeof(string_desc_arr) / sizeof(string_desc_arr[0]))
        {
            return NULL;
        }

        const char *str = string_desc_arr[index];

        chr_count = (uint8_t) strlen(str);

        if(chr_count > 31)
        {
            chr_count = 31;
        }

        for(uint8_t i = 0; i < chr_count; ++i)
        {
            desc_str[1 + i] = str[i];
        }
    }

    desc_str[0] =
        (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));

    return desc_str;
}
