#pragma once
#include "boards/pico.h"
#undef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (16u * 1024u * 1024u)
