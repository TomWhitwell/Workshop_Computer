#pragma once

#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"

// Flash storage configuration for user scripts
// Reserve last 16KB of flash for user script storage (matches crow v3.0+)
#define MAX_SCRIPT_NAME_LEN  32                // Max length for script name
#define USER_SCRIPT_SIZE     (16 * 1024 - 4 - MAX_SCRIPT_NAME_LEN)  // 16KB minus header
#define USER_SCRIPT_SECTORS  4                 // 4 sectors @ 4KB each = 16KB

// Prefer the Pico SDK-provided flash size when available.
// Fallback to 2MB (RP2040 Pico default) if not defined by the board.
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (2 * 1024 * 1024)
#endif
#define USER_SCRIPT_OFFSET    (PICO_FLASH_SIZE_BYTES - (USER_SCRIPT_SECTORS * 4096))
#define USER_SCRIPT_LOCATION  (XIP_BASE + USER_SCRIPT_OFFSET)

// Magic numbers (matches crow)
#define USER_MAGIC 0x0A  // User script present
#define USER_CLEAR 0x0C  // Flash cleared, no script

// Script type
typedef enum {
    USERSCRIPT_Default,  // Use default First.lua
    USERSCRIPT_User,     // User script stored in flash
    USERSCRIPT_Clear     // No script loaded
} USERSCRIPT_t;

class FlashStorage {
public:
    // Initialize flash storage system
    static void init();
    
    // Check what type of script is stored
    static USERSCRIPT_t which_user_script();
    
    // Write a script to flash (returns true on success)
    static bool write_user_script(const char* script, uint32_t length);
    static bool write_user_script_with_name(const char* script, uint32_t length, const char* name);
    
    // Read script from flash into buffer (returns true on success)
    static bool read_user_script(char* buffer, uint32_t* length);
    
    // Get script length without reading
    static uint16_t get_user_script_length();
    
    // Get direct pointer to script in flash (read-only, XIP)
    static const char* get_user_script_addr();
    
    // Get script name (returns empty string if no name stored)
    static const char* get_script_name();
    
    // Clear user script (write clear marker)
    static void clear_user_script();
    
    // Set flash to use default First.lua on boot (clears user script and sets default flag)
    static bool set_default_script_mode();
    
private:
    // Get firmware version word for status
    static uint32_t get_version_word();
};
