#ifndef PUNK_CONFUSION_SAMPLE_BANK_H
#define PUNK_CONFUSION_SAMPLE_BANK_H

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"
#include <cstdint>

namespace PunkSampleBank
{
#ifndef PUNK_SAMPLE_BANK_SIZE_BYTES
#define PUNK_SAMPLE_BANK_SIZE_BYTES (1024 * 1024)
#endif

constexpr uint32_t kMagic = 0x4B4E5550u; // "PUNK" little-endian.
constexpr uint32_t kVersion = 1;
constexpr uint32_t kFormatMuLaw8 = 1;
constexpr uint32_t kSampleRate = 24000;
constexpr uint32_t kSampleCount = 4;
constexpr uint32_t kBankSize = PUNK_SAMPLE_BANK_SIZE_BYTES; // Reserved at top of flash.
constexpr uint32_t kBankOffset = PICO_FLASH_SIZE_BYTES - kBankSize;
constexpr uint32_t kMaxPayloadBytes = kBankSize - 4096;

static_assert(kBankSize < PICO_FLASH_SIZE_BYTES, "Sample bank must leave room for firmware.");

struct SampleInfo
{
    uint32_t offset;
    uint32_t length;
};

struct Header
{
    uint32_t magic;
    uint32_t version;
    uint32_t format;
    uint32_t sampleRate;
    uint32_t sampleCount;
    uint32_t payloadBytes;
    uint32_t checksum;
    uint32_t reserved;
    SampleInfo samples[kSampleCount];
};

struct LoadedBank
{
    const Header *header = nullptr;
    const uint8_t *payload = nullptr;
    bool valid = false;
};

static inline uint32_t checksum(const uint8_t *data, uint32_t length)
{
    uint32_t sum = 2166136261u;
    for (uint32_t i = 0; i < length; ++i)
    {
        sum ^= data[i];
        sum *= 16777619u;
    }
    return sum;
}

static inline LoadedBank load()
{
    const auto *header = reinterpret_cast<const Header *>(XIP_BASE + kBankOffset);
    const auto *payload = reinterpret_cast<const uint8_t *>(header + 1);
    LoadedBank bank{};
    bank.header = header;
    bank.payload = payload;

    if (header->magic != kMagic
        || header->version != kVersion
        || header->format != kFormatMuLaw8
        || header->sampleRate != kSampleRate
        || header->sampleCount != kSampleCount
        || header->payloadBytes > kMaxPayloadBytes)
    {
        return bank;
    }

    for (uint32_t i = 0; i < kSampleCount; ++i)
    {
        const uint32_t end = header->samples[i].offset + header->samples[i].length;
        if (end < header->samples[i].offset || end > header->payloadBytes)
        {
            return bank;
        }
    }

    bank.valid = checksum(payload, header->payloadBytes) == header->checksum;
    return bank;
}

static inline int16_t decodeMuLaw(uint8_t encoded)
{
    encoded = ~encoded;
    const int32_t sign = encoded & 0x80;
    const int32_t exponent = (encoded >> 4) & 0x07;
    const int32_t mantissa = encoded & 0x0F;
    int32_t sample = ((mantissa << 3) + 0x84) << exponent;
    sample -= 0x84;
    return static_cast<int16_t>(sign ? -sample : sample);
}

} // namespace PunkSampleBank

#endif // PUNK_CONFUSION_SAMPLE_BANK_H
