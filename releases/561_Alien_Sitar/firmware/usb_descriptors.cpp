#include "pico/unique_id.h"
#include "tusb.h"
#include <cstring>
namespace {
const tusb_desc_device_t device = {sizeof(tusb_desc_device_t),
                                   TUSB_DESC_DEVICE,
                                   0x0200,
                                   TUSB_CLASS_MISC,
                                   MISC_SUBCLASS_COMMON,
                                   MISC_PROTOCOL_IAD,
                                   CFG_TUD_ENDPOINT0_SIZE,
                                   0x2e8a,
                                   0x000a,
                                   0x0100,
                                   1,
                                   2,
                                   3,
                                   1};
const uint8_t configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, 2, 0, TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN, 0, 150),
    TUD_CDC_DESCRIPTOR(0, 4, 0x81, 8, 0x02, 0x82, 64)};
} // namespace
extern "C" const uint8_t *tud_descriptor_device_cb() {
    return reinterpret_cast<const uint8_t *>(&device);
}
extern "C" const uint8_t *tud_descriptor_configuration_cb(uint8_t) {
    return configuration;
}
extern "C" const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t) {
    static uint16_t descriptor[40];
    static char serial[PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 2 + 1];
    if (index == 0) {
        descriptor[0] = (TUSB_DESC_STRING << 8) | 4;
        descriptor[1] = 0x0409;
        return descriptor;
    }
    if (!serial[0]) {
        pico_get_unique_board_id_string(serial, sizeof(serial));
    }
    const char *strings[] = {"", "MTM Sitar development", "MTM Sitar USB editor", serial,
                             "Live settings"};
    if (index >= sizeof(strings) / sizeof(strings[0])) {
        return nullptr;
    }
    size_t count = std::strlen(strings[index]);
    if (count > 39) {
        count = 39;
    }
    for (size_t i = 0; i < count; ++i) {
        descriptor[i + 1] = uint8_t(strings[index][i]);
    }
    descriptor[0] = uint16_t((TUSB_DESC_STRING << 8) | (2 * count + 2));
    return descriptor;
}
