// USB MIDI device descriptors for drumdrum.
// Browser sees the card as "Music Thing Modular DrumDrum" and uses
// WebMIDI for the editor (see editor.html).
//
// Adapted from Reverb+'s usb_descriptors.c (releases/20_reverb).

#include "tusb.h"
#include "pico/unique_id.h"

// VID 0x2E8A is the Raspberry Pi Foundation block; PID 0x10C2 sits one
// past Reverb+'s 0x10C1 to coexist on the same host without driver
// confusion.
#define USB_VID 0x2E8A
#define USB_PID 0x10C2
#define USB_BCD 0x0200

// ── Device descriptor ─────────────────────────────────────────
tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = USB_BCD,
    .bDeviceClass       = 0x00,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = USB_VID,
    .idProduct          = USB_PID,
    .bcdDevice          = 0x0100,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01
};

uint8_t const *tud_descriptor_device_cb(void) {
    return (uint8_t const *) &desc_device;
}

// ── Configuration descriptor ──────────────────────────────────
enum {
    ITF_NUM_MIDI = 0,
    ITF_NUM_MIDI_STREAMING,
    ITF_NUM_TOTAL
};

#define EPNUM_MIDI 0x01
#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_MIDI_DESC_LEN)

uint8_t const desc_fs_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
    TUD_MIDI_DESCRIPTOR(ITF_NUM_MIDI, 0, EPNUM_MIDI, 0x80 | EPNUM_MIDI, 64)
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void) index;
    return desc_fs_configuration;
}

// ── String descriptors ────────────────────────────────────────
char const *string_desc_arr[] = {
    (const char[]){0x09, 0x04},   // 0: language id (0x0409 English)
    "Music Thing Modular",         // 1: manufacturer
    "DrumDrum",                    // 2: product
    NULL                           // 3: serial — filled in at runtime
};

static uint16_t _desc_str[32];
static char     _serial_str[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void) langid;
    uint8_t chr_count = 0;

    if (index == 0) {
        memcpy(&_desc_str[1], string_desc_arr[0], 2);
        chr_count = 1;
    } else if (index < sizeof(string_desc_arr) / sizeof(string_desc_arr[0])) {
        const char *str;
        if (index == 3) {
            // Use the chip's unique ID as a stable serial number so the
            // host can distinguish multiple cards on the same machine.
            pico_get_unique_board_id_string(_serial_str, sizeof(_serial_str));
            str = _serial_str;
        } else {
            str = string_desc_arr[index];
        }
        if (!str) return NULL;

        chr_count = (uint8_t) strlen(str);
        if (chr_count > 31) chr_count = 31;
        for (uint8_t i = 0; i < chr_count; i++) {
            _desc_str[1 + i] = str[i];
        }
    } else {
        return NULL;
    }

    _desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return _desc_str;
}
