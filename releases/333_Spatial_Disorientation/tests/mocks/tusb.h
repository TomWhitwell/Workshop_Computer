// Copyright (c) 2026 Adrian Vos (soveda). SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <cstddef>
inline void tud_init(int){}
inline void tud_task(){}
inline bool tud_midi_available(){return false;}
inline uint32_t tud_midi_stream_read(uint8_t*,size_t){return 0;}
inline bool tud_midi_mounted(){return true;}
uint32_t tud_midi_stream_write(uint8_t,const uint8_t*,size_t);
