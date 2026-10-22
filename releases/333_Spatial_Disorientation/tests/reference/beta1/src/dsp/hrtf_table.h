// Copyright (c) 2026 Adrian Vos (soveda). SPDX-License-Identifier: MIT
// Derived measurement tables retain Gardner/Martin MIT KEMAR attribution terms.
#pragma once
#ifndef SPATIAL_HRTF_TAPS
#define SPATIAL_HRTF_TAPS 32
#endif
#if SPATIAL_HRTF_TAPS == 32
#include "hrtf_table_32.h"
#elif SPATIAL_HRTF_TAPS == 64
#include "hrtf_table_64.h"
#else
#error "SPATIAL_HRTF_TAPS must be 32 or 64"
#endif
