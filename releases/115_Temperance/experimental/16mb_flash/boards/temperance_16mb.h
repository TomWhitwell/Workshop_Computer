// Includes the Pico SDK board definition by Raspberry Pi (Trading) Ltd.
// That upstream definition is BSD-3-Clause, copyright (c) 2020.
// This experiment overrides only the flash capacity. See ../ATTRIBUTION.md.
#pragma once
#include "boards/pico.h"
#undef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (16u * 1024u * 1024u)
