// Uses the Raspberry Pi Pico board definition, copyright (c) 2020
// Raspberry Pi (Trading) Ltd., BSD-3-Clause; see ../ATTRIBUTION.md.
// Link for the minimum 2MB card. The raw flash engine detects actual capacity
// before recording and can safely use the larger 16MB chip at runtime.
#pragma once
#include "boards/pico.h"
#undef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (2u * 1024u * 1024u)
