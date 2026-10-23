// Copyright (c) 2026 Adrian Vos (soveda). SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
bool multicore_lockout_start_timeout_us(uint64_t);
void multicore_lockout_end_blocking();
