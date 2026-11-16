/// @file storage.hpp
/// @brief Program-card flash settings journal: explicit saves and restore loads.
///
/// Stores instrument settings in the two 4 KiB sectors reserved at the end of
/// the program image's 2 MiB prefix (PERSISTENCE.md). Records are 256-byte
/// versioned CRC32 pages appended with two-sector rotation; boot and Restore
/// accept only the newest complete, valid record. Only explicit Save calls
/// erase/program flash; the factory EEPROM calibration is never written, and
/// every entry point is guarded by the detected flash capacity.
#pragma once
#include "../port/saved_settings.hpp"
namespace computer::storage {
/// @brief Gates all journal access on the detected program-card flash size.
/// @param flashBytes Detected flash capacity, e.g. ComputerCard::FlashSizeBytes().
/// @note Records whether the card covers the two reserved sectors; load/save
///       return failure on smaller cards instead of corrupting the image.
void initialise(uint32_t flashBytes);
/// @brief Copies the newest valid journal record into @p state if one exists.
/// @param state Receives the record's settings when a valid record is found.
/// @return True exactly when a newest complete, version/CRC-checked record was used.
/// @details Reads through XIP with no flash hardware access; torn, corrupt or
///          mismatched-schema pages are skipped in favour of the newer valid one.
bool load(saved::State &state);
/// @brief Appends @p state as one journal record (explicit Save path only).
/// @param state Complete settings snapshot; must pass the schema validity check.
/// @return True when the programmed page reads back identical to what was written.
/// @details May erase one 4 KiB sector when both are full, retaining the previous
///          sector for fallback. Calls the SDK flash helpers from the core0
///          foreground; audio keeps running by design, though real continuity
///          during a save is a physical test (PERSISTENCE.md).
bool save(const saved::State &state);
} // namespace computer::storage
