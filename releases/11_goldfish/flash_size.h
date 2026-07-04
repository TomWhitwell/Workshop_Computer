/**
 * flash_size.h — runtime detection of the program card's flash capacity.
 *
 * Goldfish 2.0 runs the same firmware on 2 MB and 16 MB program cards, so the
 * usable storage (and therefore the maximum loop time) must be discovered at
 * boot rather than baked in at compile time.  We read the SPI flash JEDEC ID
 * (command 0x9F); the third returned byte is a log2 capacity code, so the total
 * size is (1 << capacity_code) bytes.
 *
 * IMPORTANT: call goldfish_detect_flash_size() once, early in init, while still
 * single-core and before any concurrent XIP flash activity.  flash_do_cmd()
 * momentarily takes over the SSI, so a second core executing from flash at the
 * same time would be unsafe.
 */

#ifndef GOLDFISH_FLASH_SIZE_H
#define GOLDFISH_FLASH_SIZE_H

#include <stdint.h>
#include "hardware/flash.h"

#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (2u * 1024u * 1024u)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Return the detected flash size in bytes.  Falls back to PICO_FLASH_SIZE_BYTES
 * (or 2 MB) if the JEDEC capacity code is outside a sane 2 MB..32 MB range.
 */
static inline uint32_t goldfish_detect_flash_size(void)
{
	uint8_t tx[4] = { 0x9fu, 0u, 0u, 0u }; /* 0x9F = Read JEDEC ID */
	uint8_t rx[4] = { 0u, 0u, 0u, 0u };

	flash_do_cmd(tx, rx, 4);

	uint8_t capacity_code = rx[3];

	/* 0x15 = 2 MB (2^21) ... 0x18 = 16 MB (2^24).  Accept 2 MB..32 MB. */
	if (capacity_code >= 21u && capacity_code <= 25u) {
		return 1u << capacity_code;
	}

	return PICO_FLASH_SIZE_BYTES;
}

#ifdef __cplusplus
}
#endif

#endif /* GOLDFISH_FLASH_SIZE_H */
