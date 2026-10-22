// Copyright (c) 2026 Adrian Vos (soveda). SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <cstddef>
constexpr unsigned FLASH_SECTOR_SIZE=4096,FLASH_PAGE_SIZE=256;
extern uint8_t mock_flash[4096];
#define XIP_BASE reinterpret_cast<uintptr_t>(mock_flash)
void flash_range_erase(uint32_t,size_t);
void flash_range_program(uint32_t,const uint8_t*,size_t);
