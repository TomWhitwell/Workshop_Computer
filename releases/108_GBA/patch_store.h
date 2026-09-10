// patch_store.h — 16 patch slots in the RP2040's own flash.
//
// The GBA's patch lives in EWRAM and is gone the moment the console is switched off, so the
// Workshop is where a patch has to persist. One 4 kB flash sector holds sixteen 256-byte slots,
// which is why sizeof(Patch) must stay under GBA_PATCH_SLOT_BYTES.
//
// ─────────────────────────────────────────────────────────────────────────────────────────────
// WHY THIS IS MORE DELICATE THAN IT LOOKS
//
// Erasing or programming flash stops XIP, so ANY code executing from flash on EITHER core during
// the operation crashes the chip. Core 0 is sitting in ComputerCard's Run() loop, which lives in
// flash. The Pico SDK's answer is multicore lockout: core 0 parks in a RAM-resident handler with
// interrupts off while core 1 does the write.
//
// Two consequences worth knowing before pressing save:
//   * main() must call multicore_lockout_victim_init() on core 0 BEFORE core 1 is launched, or
//     flash_safe_execute() has nothing to park and refuses.
//   * the 48 kHz audio interrupt does not run during the lockout, so a save produces a click.
//     A sector erase plus program is a few milliseconds. For a deliberate action that is a fair
//     price; it is not something to do from the audio path.
#pragma once

#include <cstdint>
#include "gba_proto.h"

// True if the slot holds a patch whose magic and version we recognise; `out` gets its bytes.
bool patch_store_load(int slot, uint8_t *out);

// Read-modify-erase-rewrite of the whole sector. Returns false if the flash operation could not
// be performed safely (lockout unavailable, or the slot index is out of range).
bool patch_store_save(int slot, const uint8_t *in);

// Which slots currently hold something, as a bitmask. Cheap: it only reads XIP.
uint16_t patch_store_used(void);
