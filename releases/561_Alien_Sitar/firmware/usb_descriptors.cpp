/// @file usb_descriptors.cpp
/// @brief TinyUSB device/configuration/string descriptor callbacks (USB editor build).
///
/// Compiled with the USB-enabled firmware; TinyUSB calls
/// these callbacks from the core0 foreground to answer enumeration and
/// control requests. The identity is deliberately the Pico SDK development
/// CDC VID/PID 0x2e8a:0x000a with a unique flash-ID serial, not a new
/// manufacturer assignment (USB_EDITOR.md); the serial string comes from the
/// SDK board-ID path without touching flash hardware in these callbacks.
/// All returned pointers stay valid while enumerated, so every buffer here is
/// static or file-scope.
#include "pico/unique_id.h"
#include "tusb.h"
#include <cstring>
namespace {
// Device descriptor in tusb_desc_device_t field order: an 18-byte, USB 2.0
// (0x0200) descriptor; class 0x02 (Misc) with Common subclass and IAD
// protocol so the CDC control/data interfaces pair through an interface
// association; 64-byte endpoint 0 matching CFG_TUD_ENDPOINT0_SIZE in
// tusb_config.h; Pico SDK development CDC identity 0x2e8a:0x000a, release
// 1.00; string indices 1 manufacturer/2 product/3 serial; one configuration.
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
// Fixed configuration: two interfaces (CDC control 0 + data 1) in one
// bus-powered configuration requesting 150 mA; CDC notification endpoint
// 0x81 of 8 bytes and data endpoints OUT 0x02 / IN 0x82 of 64 bytes.
const uint8_t configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, 2, 0, TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN, 0, 150),
    TUD_CDC_DESCRIPTOR(0, 4, 0x81, 8, 0x02, 0x82, 64)};
} // namespace
/// @brief Device-descriptor callback: the declared @c device record above.
/// @return Pointer to the 18-byte device descriptor, valid for the device run.
extern "C" const uint8_t *tud_descriptor_device_cb() {
    return reinterpret_cast<const uint8_t *>(&device);
}
/// @brief Configuration callback; the singular configuration ignores the index.
/// @return Pointer to the interface/endpoint descriptor array above.
extern "C" const uint8_t *tud_descriptor_configuration_cb(uint8_t) {
    return configuration;
}
/// @brief String-descriptor callback: language ID once, then UTF-16 strings.
///
/// Builds each reply in a 40-word static buffer from the ASCII table below:
/// index 0 answers the US-English language query; index 3 is the board's
/// unique flash-ID serial, built lazily and cached. Entries longer than 39
/// characters are cut so the 2 x count + 2 byte answer fits the buffer, and
/// unknown indices answer null, which TinyUSB treats as an absent string.
/// Called from the core0 TinyUSB foreground only (editor build).
/// @param index Requested string index (0 = language ID, then table order).
/// @return Pointer to the UTF-16 string descriptor, or nullptr if unavailable.
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
    // ASCII strings aligned to the descriptor indices; index 0 is handled by
    // the language-ID branch above and never reaches this lookup.
    const char *strings[] = {"", "MTM Sitar development", "MTM Sitar USB editor", serial,
                             "Live settings"};
    if (index >= sizeof(strings) / sizeof(strings[0])) {
        return nullptr;
    }
    size_t count = std::strlen(strings[index]);
    if (count > 39) {
        count = 39;
    }
    // ASCII copied into the UTF-16 payload word by word; ASCII-only strings pass
    // through verbatim.
    for (size_t i = 0; i < count; ++i) {
        descriptor[i + 1] = uint8_t(strings[index][i]);
    }
    // The header word carries the descriptor kind and the answer's byte length.
    descriptor[0] = uint16_t((TUSB_DESC_STRING << 8) | (2 * count + 2));
    return descriptor;
}
