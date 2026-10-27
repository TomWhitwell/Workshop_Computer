#include "flash_storage.h"
#include <cstring>
#include "tusb.h"

void FlashStorage::init() {
    // Nothing special needed - XIP is already configured by SDK
    // User script clearing is now handled by the UF2 file itself
    // (the build process adds erase blocks to the UF2 that clear the user script area)
}

USERSCRIPT_t FlashStorage::which_user_script() {
    const uint32_t* status_word = (const uint32_t*)USER_SCRIPT_LOCATION;
    uint8_t magic = (*status_word) & 0xF;
    
    switch(magic) {
        case USER_MAGIC: return USERSCRIPT_User;
        case USER_CLEAR: return USERSCRIPT_Clear;
        default:         return USERSCRIPT_Default;
    }
}

// One flash page, staged for programming. Writes stream through this rather
// than a whole-script buffer, which would cost 16KB of RAM (Lua heap) for
// something used only during ^^w.
static uint8_t page_buffer[FLASH_PAGE_SIZE] __attribute__((aligned(4)));

// Erase the user script sectors, then program `head` followed by `body` from
// the start of them, padded with 0xFF to a whole page. Both must be in RAM:
// XIP is unavailable while this runs.
// Core 1 (audio) keeps running throughout: everything it executes is in RAM
// (see blackbird_core1_background_service in main.cpp), so XIP being
// unavailable here doesn't affect it. Core 0 just has interrupts off.
static void __no_inline_not_in_flash_func(do_flash_write)(const uint8_t* head, size_t head_len,
                                                          const uint8_t* body, size_t body_len) {
    const size_t total = head_len + body_len;
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(USER_SCRIPT_OFFSET, USER_SCRIPT_SECTORS * 4096);
    for (size_t page = 0; page < total; page += FLASH_PAGE_SIZE) {
        for (size_t i = 0; i < FLASH_PAGE_SIZE; i++) {
            const size_t pos = page + i;
            page_buffer[i] = (pos < head_len) ? head[pos]
                           : (pos < total)    ? body[pos - head_len]
                           : 0xFF;
        }
        flash_range_program(USER_SCRIPT_OFFSET + page, page_buffer, FLASH_PAGE_SIZE);
    }
    restore_interrupts(ints);
}

static bool in_ram(const void* p) {
    return (uintptr_t)p >= SRAM_BASE && (uintptr_t)p < SRAM_END;
}

bool FlashStorage::write_user_script(const char* script, uint32_t length) {
    // Call the extended version with no name
    return write_user_script_with_name(script, length, "");
}

bool FlashStorage::write_user_script_with_name(const char* script, uint32_t length, const char* name) {
    if (length > USER_SCRIPT_SIZE) {
        return false;
    }
    
    // Don't use tud_cdc_write here - it might interfere with USB in some way
    // Just do the work silently
    
    // The script is read straight from its buffer while flash is busy
    if (!in_ram(script)) {
        return false;
    }

    // Layout: [status_word:4][script_name:32][script_data:N]
    uint8_t header[4 + MAX_SCRIPT_NAME_LEN];
    memset(header, 0xFF, sizeof(header));
    
    // Build status word: [magic:4][version:12][length:16]
    uint32_t status_word = USER_MAGIC 
                         | (get_version_word() << 4) 
                         | (length << 16);
    memcpy(header, &status_word, 4);
    
    // Copy script name (up to 31 chars + null terminator)
    if (name && name[0]) {
        size_t name_len = strlen(name);
        if (name_len > MAX_SCRIPT_NAME_LEN - 1) {
            name_len = MAX_SCRIPT_NAME_LEN - 1;
        }
        memcpy(header + 4, name, name_len);
        header[4 + name_len] = '\0';  // Ensure null termination
    } else {
        header[4] = '\0';  // Empty name
    }
    
    do_flash_write(header, sizeof(header), (const uint8_t*)script, length);
    
    return true;
}

bool FlashStorage::read_user_script(char* buffer, uint32_t* length) {
    if (which_user_script() != USERSCRIPT_User) {
        return false;
    }
    
    const uint32_t* flash_addr = (const uint32_t*)USER_SCRIPT_LOCATION;
    *length = (*flash_addr >> 16) & 0xFFFF;
    
    // Read directly from flash (XIP makes this easy!)
    // Script data starts after: [status_word:4][script_name:32]
    const char* script_start = (const char*)(USER_SCRIPT_LOCATION + 4 + MAX_SCRIPT_NAME_LEN);
    memcpy(buffer, script_start, *length);
    
    return true;
}

uint16_t FlashStorage::get_user_script_length() {
    if (which_user_script() != USERSCRIPT_User) {
        return 0;
    }
    const uint32_t* status_word = (const uint32_t*)USER_SCRIPT_LOCATION;
    return (*status_word >> 16) & 0xFFFF;
}

const char* FlashStorage::get_user_script_addr() {
    if (which_user_script() != USERSCRIPT_User) {
        return nullptr;
    }
    // Script data starts after: [status_word:4][script_name:32]
    return (const char*)(USER_SCRIPT_LOCATION + 4 + MAX_SCRIPT_NAME_LEN);
}

const char* FlashStorage::get_script_name() {
    if (which_user_script() != USERSCRIPT_User) {
        return "";
    }
    // Script name is stored right after status word
    const char* name = (const char*)(USER_SCRIPT_LOCATION + 4);
    // Return empty string if first char is 0xFF (erased flash) or 0x00 (empty name)
    if (name[0] == (char)0xFF || name[0] == '\0') {
        return "";
    }
    return name;
}

void FlashStorage::clear_user_script() {
    // Write USER_CLEAR magic to indicate intentional clearing
    uint32_t status_word = USER_CLEAR | (get_version_word() << 4);
    
    do_flash_write((const uint8_t*)&status_word, 4, nullptr, 0);
    
    tud_cdc_write_str("User script cleared\n\r");
}

// Set flash to use default First.lua script on boot
// Returns true on success
bool FlashStorage::set_default_script_mode() {
    // Write neither USER_MAGIC nor USER_CLEAR - triggers default
    uint32_t status_word = 0x0 | (get_version_word() << 4);
    
    // Erases all user script sectors to fully clear any stored script
    do_flash_write((const uint8_t*)&status_word, 4, nullptr, 0);
    
    return true;
}

uint32_t FlashStorage::get_version_word() {
    // Return firmware version as 12-bit value (0x040 = v0.4)
    return 0x040;
}
