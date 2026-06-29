#include "tusb.h"
#include <pico/unique_id.h>
#include <string.h>
#include <stdio.h>

#define USB_VID   0x2E8A // Raspberry Pi
#define USB_PID   0x10C1 // Music Thing Modular Workshop System Computer
#define USB_BCD   0x0200

// String Descriptor Index
enum {
    STRING_LANGID = 0,
    STRING_MANUFACTURER,
    STRING_PRODUCT,
    STRING_SERIAL,
    STRING_MIDI,
    STRING_LAST
};

static const char *const string_desc_arr[] = {
    [STRING_MANUFACTURER] = "Music Thing",
    [STRING_PRODUCT]      = "Clockwork MIDI",
    [STRING_SERIAL]       = NULL, // Dynamic using flash ID
    [STRING_MIDI]         = "Clockwork MIDI"
};

// Device Descriptor
static const tusb_desc_device_t desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = USB_BCD,
    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor = USB_VID,
    .idProduct = USB_PID,
    .bcdDevice = 0x0104, // Bumped to force OS cache invalidation

    .iManufacturer = STRING_MANUFACTURER,
    .iProduct = STRING_PRODUCT,
    .iSerialNumber = STRING_SERIAL,

    .bNumConfigurations = 0x01
};

uint8_t const *tud_descriptor_device_cb(void) {
    return (uint8_t const *)&desc_device;
}

// Interfaces
enum {
    ITF_NUM_MIDI = 0,
    ITF_NUM_MIDI_STREAMING,
    ITF_NUM_TOTAL
};

// Endpoints
#define USBD_MIDI_EP_OUT         0x01
#define USBD_MIDI_EP_IN          0x81

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_MIDI_DESC_LEN)

static const uint8_t desc_fs_configuration[] = {
    // Config number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 250),

    // Interface number, string index, EP Out & EP In address, EP size
    TUD_MIDI_DESCRIPTOR(ITF_NUM_MIDI, STRING_MIDI, USBD_MIDI_EP_OUT, USBD_MIDI_EP_IN, 64)
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_fs_configuration;
}

static uint16_t _desc_str[64];
static char usbd_serial_str[PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 2 + 1];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;

    uint8_t chr_count = 0;

    if (index == 0) {
        _desc_str[1] = 0x0409; // supported language is English
        chr_count = 1;
    } else if (index == STRING_SERIAL) {
        if (!usbd_serial_str[0]) {
            pico_get_unique_board_id_string(usbd_serial_str, sizeof(usbd_serial_str));
        }
        chr_count = strlen(usbd_serial_str);
        if (chr_count > 63) chr_count = 63;
        for (uint8_t i = 0; i < chr_count; i++) {
            _desc_str[1 + i] = usbd_serial_str[i];
        }
    } else {
        if (index >= STRING_LAST) return NULL;
        const char *str = string_desc_arr[index];
        if (!str) return NULL;
        chr_count = strlen(str);
        if (chr_count > 63) chr_count = 63;
        for (uint8_t i = 0; i < chr_count; i++) {
            _desc_str[1 + i] = str[i];
        }
    }

    // first byte is length (including header), second byte is string type
    _desc_str[0] = (uint16_t) ((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));

    return _desc_str;
}

_Static_assert(sizeof(desc_fs_configuration) == CONFIG_TOTAL_LEN, "Descriptor size mismatch!");
