#include "tusb.h"
#include <pico/unique_id.h>


/*
  USB MIDI device descriptors, using serial number from RP2040 flash
 */


#define USB_PID   0x10C1 // Music Thing Modular Workshop System Computer
#define USB_VID   0x2E8A // Raspberry Pi
#define USB_BCD   0x0200

// String Descriptor Index
enum {
  STRING_LANGID = 0,
  STRING_MANUFACTURER,
  STRING_PRODUCT,
  STRING_SERIAL,
  STRING_LAST,
};

// array of pointer to string descriptors
char const *string_desc_arr[] = {
	(const char[]){ 0x09, 0x04 }, // 0: is supported language is English (0x0409)
	"Music Thing", // 1: Manufacturer
	"MTMComputer", // 2: Product
	NULL, // 3: Serial number, using flash chip ID
};



// Device Descriptor
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

	.bNumConfigurations = 0x01
};

uint8_t const *tud_descriptor_device_cb(void)
{
	return (uint8_t const *)&desc_device;
}

// Configuration descriptor
enum
{
	ITF_NUM_MIDI = 0,
	ITF_NUM_MIDI_STREAMING,
	ITF_NUM_TOTAL
};

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_MIDI_DESC_LEN)

// Endpoint number
#define EPNUM_MIDI 0x01

uint8_t const desc_fs_configuration[] = {
	// Config number, interface count, string index, total length, attribute, power in mA
	TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),

	// Interface number, string index, EP Out & EP In address, EP size
	TUD_MIDI_DESCRIPTOR(ITF_NUM_MIDI, 0, EPNUM_MIDI, 0x80 | EPNUM_MIDI, 64)
};

#if TUD_OPT_HIGH_SPEED
uint8_t const desc_hs_configuration[] = {
	// Config number, interface count, string index, total length, attribute, power in mA
	TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),

	// Interface number, string index, EP Out & EP In address, EP size
	TUD_MIDI_DESCRIPTOR(ITF_NUM_MIDI, 0, EPNUM_MIDI, 0x80 | EPNUM_MIDI, 512)
};
#endif

// Invoked when received GET CONFIGURATION DESCRIPTOR
// Application return pointer to descriptor
// Descriptor contents must exist long enough for transfer to complete
uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
	(void)index; // for multiple configurations

#if TUD_OPT_HIGH_SPEED
	// Although we are highspeed, host may be fullspeed.
	return (tud_speed_get() == TUSB_SPEED_HIGH) ? desc_hs_configuration : desc_fs_configuration;
#else
	return desc_fs_configuration;
#endif
}

static uint16_t _desc_str[32];

// Invoked when received GET STRING DESCRIPTOR request
// Application return pointer to descriptor, whose contents must exist long enough for transfer to complete
uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
	(void)langid;

	uint8_t chr_count;

	if (index == 0)
	{
		memcpy(&_desc_str[1], string_desc_arr[0], 2);
		chr_count = 1;
	}
	else if (index == STRING_SERIAL)
	{
		pico_unique_board_id_t id;
		pico_get_unique_board_id(&id);
		uint64_t idx = *(uint64_t *)&id.id;
		int serialnum = ((idx + 1) % 10000000ull);
		if (serialnum < 1000000)
			serialnum += 1000000; // 7 digits
		char temp[16];
		chr_count = sprintf(temp, "%07d", serialnum);
		for (uint8_t i = 0; i < chr_count; i++)
		{
			_desc_str[1 + i] = temp[i];
		}
	}
	else if (index < STRING_LAST)
	{
		// Note: the 0xEE index string is a Microsoft OS 1.0 Descriptors.
		// https://docs.microsoft.com/en-us/windows-hardware/drivers/usbcon/microsoft-defined-usb-descriptors

		if (!(index < sizeof(string_desc_arr) / sizeof(string_desc_arr[0]))) return NULL;

		const char *str = string_desc_arr[index];

		// Cap at max char
		chr_count = strlen(str);
		if (chr_count > 31)
		{
			chr_count = 31;
		}
		// Convert ASCII string into UTF-16
		for (uint8_t i = 0; i < chr_count; i++)
		{
			_desc_str[1 + i] = str[i];
		}
	}
	else
	{
		return NULL;
	}

	// first byte is length (including header), second byte is string type
	_desc_str[0] = (TUSB_DESC_STRING << 8) | (2 * chr_count + 2);

	return _desc_str;
}
