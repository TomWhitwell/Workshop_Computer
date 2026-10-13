#include "tusb.h"
#include "pico/unique_id.h"

namespace
{
constexpr uint16_t kVendorId = 0xcaff;
constexpr uint16_t kProductId = 0x2003;
enum : uint8_t
{
    kStringLanguage,
    kStringManufacturer,
    kStringProduct,
    kStringSerial,
    kStringVendor,
};

const char *const strings[] = {
    "\x09\x04",
    "Workshop Computer",
    "Workshop Buzzrito WebUSB",
    nullptr,
    "Buzzrito WebUSB",
};

const tusb_desc_device_t device_descriptor = {
    sizeof(tusb_desc_device_t), TUSB_DESC_DEVICE, 0x0200,
    0, 0, 0, CFG_TUD_ENDPOINT0_SIZE,
    kVendorId, kProductId, 0x0100,
    kStringManufacturer, kStringProduct, kStringSerial, 1,
};

constexpr uint8_t kInterfaceVendor = 0;
constexpr uint8_t kEndpointOut = 1;
constexpr uint8_t kEndpointIn = 0x81;
constexpr uint16_t kConfigLength = TUD_CONFIG_DESC_LEN + TUD_VENDOR_DESC_LEN;

const uint8_t configuration_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, kConfigLength, 0x80, 100),
    TUD_VENDOR_DESCRIPTOR(kInterfaceVendor, kStringVendor, kEndpointOut, kEndpointIn, 64),
};

uint16_t string_descriptor[32] = {};
} // namespace

extern "C" const uint8_t *tud_descriptor_device_cb()
{
    return reinterpret_cast<const uint8_t *>(&device_descriptor);
}

extern "C" const uint8_t *tud_descriptor_configuration_cb(uint8_t)
{
    return configuration_descriptor;
}

extern "C" const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t)
{
    if (index >= sizeof(strings) / sizeof(strings[0]))
    {
        return nullptr;
    }

    const char *value = strings[index];
    char serial[PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 2 + 1] = {};
    if (index == kStringSerial)
    {
        pico_get_unique_board_id_string(serial, sizeof(serial));
        value = serial;
    }
    if (index == kStringLanguage)
    {
        string_descriptor[1] = 0x0409;
        string_descriptor[0] = static_cast<uint16_t>((TUSB_DESC_STRING << 8) | 4);
        return string_descriptor;
    }

    uint8_t length = 0;
    while (value[length] != '\0' && length < 31)
    {
        string_descriptor[1 + length] = value[length];
        ++length;
    }
    string_descriptor[0] = static_cast<uint16_t>((TUSB_DESC_STRING << 8) | (2 * length + 2));
    return string_descriptor;
}
