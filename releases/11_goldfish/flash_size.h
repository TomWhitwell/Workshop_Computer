/**
 * flash_size.h — runtime detection of the program card's flash capacity.
 *
 * Goldfish 2.0 runs the same firmware on 2 MB and 16 MB program cards, so the
 * usable storage (and therefore the maximum loop time) must be discovered at
 * boot rather than baked in at compile time.  We read the SPI flash JEDEC ID
 * (command 0x9F). Different low-level helpers expose the returned bytes either
 * as {manufacturer, type, capacity} or shifted by the command byte; accept a
 * valid log2 capacity code from either layout, so the total size is
 * (1 << capacity_code) bytes.
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

extern volatile uint8_t  g_goldfish_jedec_rx[4];
extern volatile uint8_t  g_goldfish_jedec_capacity_code;
extern volatile uint32_t g_goldfish_detected_flash_size_bytes;

/**
 * Return the detected flash size in bytes.  Falls back to PICO_FLASH_SIZE_BYTES
 * (or 2 MB) if the JEDEC capacity code is outside a sane 2 MB..32 MB range.
 * If GOLDFISH_FLASH_SIZE_BYTES is set by the build, use it as the intended
 * storage size, capped by a smaller valid JEDEC result so a 16 MB build remains
 * safe when accidentally flashed to a 2 MB card.
 */
static inline uint32_t goldfish_detect_flash_size(void)
{
	uint8_t tx[4] = { 0x9fu, 0u, 0u, 0u }; /* 0x9F = Read JEDEC ID */
	uint8_t rx[4] = { 0u, 0u, 0u, 0u };

	flash_do_cmd(tx, rx, 4);
	for (uint32_t index = 0u; index < 4u; index++) {
		g_goldfish_jedec_rx[index] = rx[index];
	}

	uint8_t capacity_code = rx[3];
	if (capacity_code < 21u || capacity_code > 25u) {
		capacity_code = rx[2];
	}
	g_goldfish_jedec_capacity_code = capacity_code;

	/* 0x15 = 2 MB (2^21) ... 0x18 = 16 MB (2^24).  Accept 2 MB..32 MB. */
	if (capacity_code >= 21u && capacity_code <= 25u) {
		uint32_t detected_size = 1u << capacity_code;
#ifdef GOLDFISH_FLASH_SIZE_BYTES
		uint32_t build_size = (uint32_t)GOLDFISH_FLASH_SIZE_BYTES;
		g_goldfish_detected_flash_size_bytes = (detected_size < build_size) ? detected_size : build_size;
#else
		g_goldfish_detected_flash_size_bytes = detected_size;
#endif
		return g_goldfish_detected_flash_size_bytes;
	}

#ifdef GOLDFISH_FLASH_SIZE_BYTES
	g_goldfish_detected_flash_size_bytes = (uint32_t)GOLDFISH_FLASH_SIZE_BYTES;
	return g_goldfish_detected_flash_size_bytes;
#else
	g_goldfish_detected_flash_size_bytes = PICO_FLASH_SIZE_BYTES;
	return PICO_FLASH_SIZE_BYTES;
#endif
}

#ifdef __cplusplus
}
#endif

#endif /* GOLDFISH_FLASH_SIZE_H */
