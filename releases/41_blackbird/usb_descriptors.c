#include "tusb.h"
#include "pico/unique_id.h"
#include <string.h>
#include <stdio.h>

//--------------------------------------------------------------------+
// Device Descriptors - MUST MATCH CROW FOR DRUID COMPATIBILITY
//--------------------------------------------------------------------+

// Crow USB identifiers (from crow's usbd_desc.c)
#define USB_VID   0x0483  // STMicroelectronics
#define USB_PID   0x5740  // Virtual COM Port
#define USB_BCD   0x0200

tusb_desc_device_t const desc_device =
{
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = USB_BCD,
  .bDeviceClass       = TUSB_CLASS_MISC,
  .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
  .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = USB_VID,
    .idProduct          = USB_PID,
    .bcdDevice          = 0x0200,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01
};

// Invoked when received GET DEVICE DESCRIPTOR
// Application return pointer to descriptor
uint8_t const * tud_descriptor_device_cb(void)
{
  return (uint8_t const *) &desc_device;
}

//--------------------------------------------------------------------+
// Configuration Descriptor
//--------------------------------------------------------------------+

enum
{
  ITF_NUM_CDC = 0,
  ITF_NUM_CDC_DATA,
  ITF_NUM_MIDI_CONTROL,
  ITF_NUM_MIDI_STREAMING,
  ITF_NUM_TOTAL
};

#define CONFIG_TOTAL_LEN    (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_MIDI_DESC_LEN)

#define EPNUM_CDC_NOTIF   0x81
#define EPNUM_CDC_OUT     0x02
#define EPNUM_CDC_IN      0x82
#define EPNUM_MIDI_OUT    0x03
#define EPNUM_MIDI_IN     0x83

uint8_t const desc_fs_configuration[] =
{
  // Config number, interface count, string index, total length, attribute, power in mA
  TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),

  // Interface number, string index, EP notification address and size, EP data address (out, in) and size.
  TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),

  // MIDI interface (USB MIDI v1.0)
  TUD_MIDI_DESCRIPTOR(ITF_NUM_MIDI_CONTROL, 5, EPNUM_MIDI_OUT, EPNUM_MIDI_IN, 64),
};

// Invoked when received GET CONFIGURATION DESCRIPTOR
// Application return pointer to descriptor
// Descriptor contents must exist long enough for transfer to complete
uint8_t const * tud_descriptor_configuration_cb(uint8_t index)
{
  (void) index; // for multiple configurations
  return desc_fs_configuration;
}

//--------------------------------------------------------------------+
// String Descriptors - MUST MATCH CROW FOR DRUID COMPATIBILITY
//--------------------------------------------------------------------+

// String Descriptor Index
enum
{
  STRID_LANGID = 0,
  STRID_MANUFACTURER,
  STRID_PRODUCT,
  STRID_SERIAL,
  STRID_CDC_INTERFACE,
  STRID_MIDI_INTERFACE,
};

// Array of pointer to string descriptors
char const* string_desc_arr [] =
{
  (const char[]) { 0x09, 0x04 },  // 0: supported language is English (0x0409)
  "Music Thing Modular",          // 1: Manufacturer (customized, not checked by druid)
  "crow: telephone line",         // 2: Product (MUST MATCH CROW - druid searches for this!)
  NULL,                           // 3: Serial number (filled dynamically)
  "VCP Interface",                // 4: CDC Interface
  "MIDI Interface",               // 5: MIDI Interface
};

static uint16_t _desc_str[32];

// Forward declaration - get card ID from main.cpp
extern uint64_t get_card_unique_id(void);

// Generate serial number from SD card flash ID (not board ID!)
// This ensures the same card has the same USB serial number regardless of which board it's in
static void get_serial_number_string(char* buffer, size_t len)
{
  // Get the card's unique ID from flash chip (Workshop Computer card identifier)
  uint64_t card_id = get_card_unique_id();
  
  // Format as hex string (similar to crow's IntToUnicode function)
  snprintf(buffer, len, "%08X%08X",
           (uint32_t)(card_id >> 32),
           (uint32_t)(card_id & 0xFFFFFFFF));
}

// Invoked when received GET STRING DESCRIPTOR request
// Application return pointer to descriptor, whose contents must exist long enough for transfer to complete
uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
  (void) langid;

  uint8_t chr_count;

  if (index == STRID_LANGID)
  {
    memcpy(&_desc_str[1], string_desc_arr[0], 2);
    chr_count = 1;
  }
  else if (index == STRID_SERIAL)
  {
    // Generate serial number dynamically
    char serial_str[32];
    get_serial_number_string(serial_str, sizeof(serial_str));
    
    chr_count = strlen(serial_str);
    if (chr_count > 31) chr_count = 31;

    // Convert ASCII to UTF-16
    for(uint8_t i=0; i<chr_count; i++)
    {
      _desc_str[1+i] = serial_str[i];
    }
  }
  else
  {
    // Note: the 0xEE index string is a Microsoft OS 1.0 Descriptors.
    // https://docs.microsoft.com/en-us/windows-hardware/drivers/usbcon/microsoft-defined-usb-descriptors

    if ( !(index < sizeof(string_desc_arr)/sizeof(string_desc_arr[0])) ) return NULL;

    const char* str = string_desc_arr[index];

    // Cap at max char
    chr_count = strlen(str);
    if ( chr_count > 31 ) chr_count = 31;

    // Convert ASCII string into UTF-16
    for(uint8_t i=0; i<chr_count; i++)
    {
      _desc_str[1+i] = str[i];
    }
  }

  // first byte is length (including header), second byte is string type
  _desc_str[0] = (TUSB_DESC_STRING << 8 ) | (2*chr_count + 2);

  return _desc_str;
}
