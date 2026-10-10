// Copyright (c) 2026 Adrian Vos (soveda). SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <cstddef>
namespace spatial {
constexpr uint32_t kVersion = 1;
constexpr size_t kFields = 6;
// Stable IDs: separation, room, source A level, source B level, strength,
// clock pulses per revolution. Knobs remain performance controls.
struct Config {
    uint16_t value[kFields] = {2048, 1200, 2048, 2048, 4095, 4};
    bool Valid() const {
        for (size_t i = 0; i < kFields - 1; ++i) if (value[i] > 4095) return false;
        const auto d = value[5];
        return d == 1 || d == 2 || d == 4 || d == 8 || d == 16;
    }
};
inline uint32_t Checksum(const uint8_t* data, size_t n) {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < n; ++i) { hash ^= data[i]; hash *= 16777619u; }
    return hash;
}
// All messages are under 64 bytes. IDs and values are 7-bit MIDI-safe bytes.
// Body (after F0 7D SD version command sequence) is id,lo7,hi7 for each field.
inline bool DecodeConfig(const uint8_t* data, size_t n, Config& result) {
    if (n != kFields * 3) return false;
    Config candidate;
    uint32_t seen = 0;
    for (size_t i = 0; i < n; i += 3) {
        uint32_t id = data[i];
        if (id < 1 || id > kFields || data[i+1] > 127 || data[i+2] > 127) return false;
        uint32_t bit = 1u << (id - 1);
        if (seen & bit) return false;
        seen |= bit;
        candidate.value[id - 1] = data[i+1] | (static_cast<uint16_t>(data[i+2]) << 7);
    }
    if (!candidate.Valid()) return false;
    result = candidate;
    return true;
}
inline void EncodeConfig(const Config& cfg, uint8_t* data) {
    for (size_t i = 0; i < kFields; ++i) {
        data[i*3] = static_cast<uint8_t>(i+1);
        data[i*3+1] = cfg.value[i] & 127;
        data[i*3+2] = (cfg.value[i] >> 7) & 127;
    }
}
}
