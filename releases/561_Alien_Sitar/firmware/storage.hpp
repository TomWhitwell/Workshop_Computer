#pragma once
#include "../port/saved_settings.hpp"
namespace computer::storage {
void initialise(uint32_t flashBytes);
bool load(saved::State &state);
bool save(const saved::State &state);
} // namespace computer::storage
