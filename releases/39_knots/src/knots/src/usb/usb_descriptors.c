#include "tusb.h"
#include <pico/unique_id.h>
#include <string.h>
#include <stdio.h>

#define USB_PID 0x10C1
#define USB_VID 0x2E8A
#define USB_BCD 0x0200

enum {
  STRING_LANGID = 0,
  STRING_MANUFACTURER,
  STRING_PRODUCT,
  STRING_SERIAL,
  STRING_LAST,
};

char const* string_desc_arr[] = {
    (const char[]){0x09, 0x04},
    "Music Thing",
    "MTWS",
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

uint8_t const* tud_descriptor_device_cb(void) {
  return (uint8_t const*) &desc_device;
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

#if TUD_OPT_HIGH_SPEED
uint8_t const desc_hs_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
    TUD_MIDI_DESCRIPTOR(ITF_NUM_MIDI, 0, EPNUM_MIDI, 0x80 | EPNUM_MIDI, 512),
};
#endif

uint8_t const* tud_descriptor_configuration_cb(uint8_t index) {
  (void) index;
#if TUD_OPT_HIGH_SPEED
  return (tud_speed_get() == TUSB_SPEED_HIGH) ? desc_hs_configuration : desc_fs_configuration;
#else
  return desc_fs_configuration;
#endif
}

static uint16_t _desc_str[32];

uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void) langid;

  uint8_t chr_count;

  if (index == 0) {
    memcpy(&_desc_str[1], string_desc_arr[0], 2);
    chr_count = 1;
  } else if (index == STRING_SERIAL) {
    pico_unique_board_id_t id;
    pico_get_unique_board_id(&id);
    uint64_t idx = *(uint64_t*) &id.id;
    int serialnum = ((idx + 1) % 10000000ull);
    if (serialnum < 1000000) serialnum += 1000000;

    char temp[16];
    chr_count = sprintf(temp, "%07d", serialnum);
    for (uint8_t i = 0; i < chr_count; i++) {
      _desc_str[1 + i] = temp[i];
    }
  } else if (index < STRING_LAST) {
    if (!(index < sizeof(string_desc_arr) / sizeof(string_desc_arr[0]))) return NULL;
    const char* str = string_desc_arr[index];

    chr_count = (uint8_t) strlen(str);
    if (chr_count > 31) chr_count = 31;

    for (uint8_t i = 0; i < chr_count; i++) {
      _desc_str[1 + i] = str[i];
    }
  } else {
    return NULL;
  }

  _desc_str[0] = (TUSB_DESC_STRING << 8) | (2 * chr_count + 2);
  return _desc_str;
}
