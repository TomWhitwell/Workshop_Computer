#pragma once

#include <stdbool.h>
#include <stdint.h>

static constexpr uint32_t BREAKY_BANK_MAGIC = 0x594b5242u;  // "BRKY"
static constexpr uint32_t BREAKY_BANK_VERSION = 1u;
static constexpr uint32_t BREAKY_BANK_HEADER_SIZE = 4096u;
static constexpr uint32_t BREAKY_BANK_SAMPLE_RATE = 48000u;
static constexpr uint32_t BREAKY_BANK_MAX_SAMPLES = 64u;
static constexpr uint32_t BREAKY_FLASH_SECTOR_SIZE = 4096u;

#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (2u * 1024u * 1024u)
#endif

#ifndef BREAKY_FIRMWARE_RESERVE
#define BREAKY_FIRMWARE_RESERVE (256u * 1024u)
#endif

static constexpr uint32_t BREAKY_COMPILED_FLASH_TOTAL_BYTES =
    static_cast<uint32_t>(PICO_FLASH_SIZE_BYTES);
static constexpr uint32_t BREAKY_AUDIO_FLASH_OFFSET = BREAKY_FIRMWARE_RESERVE;
static constexpr uint32_t BREAKY_AUDIO_CAPACITY_BYTES =
    BREAKY_COMPILED_FLASH_TOTAL_BYTES - BREAKY_AUDIO_FLASH_OFFSET - BREAKY_BANK_HEADER_SIZE;

struct BreakyAudioSample {
  uint32_t offset;
  uint32_t frame_count;
  uint16_t source_bpm;
  uint8_t peak;
  uint8_t flags;
  char name[48];
};

struct BreakyBankSampleRecord {
  uint32_t offset;
  uint32_t frame_count;
  uint16_t source_bpm;
  uint8_t peak;
  uint8_t flags;
  char name[48];
};

struct BreakyBankHeader {
  uint32_t magic;
  uint32_t version;
  uint32_t header_size;
  uint32_t sample_rate;
  uint32_t sample_count;
  uint32_t audio_bytes;
  uint32_t capacity_bytes;
  uint32_t reserved0;
  BreakyBankSampleRecord samples[BREAKY_BANK_MAX_SAMPLES];
};

static_assert(sizeof(BreakyBankHeader) <= BREAKY_BANK_HEADER_SIZE,
              "Breaky bank header must fit in one flash sector");
static_assert(BREAKY_FIRMWARE_RESERVE >= BREAKY_FLASH_SECTOR_SIZE,
              "Firmware reserve must leave room for the audio header");

void breaky_audio_bank_init();
void breaky_audio_bank_rescan();
bool breaky_audio_bank_valid();
bool breaky_audio_bank_mutating();
void breaky_audio_bank_set_mutating(bool mutating);
uint32_t breaky_audio_sample_count();
uint32_t breaky_audio_audio_bytes();
uint32_t breaky_audio_capacity_bytes();
uint32_t breaky_flash_total_bytes();
uint32_t breaky_audio_flash_offset();
const BreakyAudioSample& breaky_audio_sample(uint32_t index);
uint8_t breaky_audio_read_byte(uint32_t offset);
