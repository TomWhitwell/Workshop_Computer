#include "BreakyAudioBank.h"

#include <string.h>

#include "hardware/flash.h"

#ifndef XIP_BASE
#define XIP_BASE 0x10000000u
#endif

namespace {

BreakyAudioSample samples[BREAKY_BANK_MAX_SAMPLES];
BreakyAudioSample empty_sample = {0, 1, 120, 0, 0, "empty"};
uint32_t sample_count = 0;
uint32_t audio_bytes = 0;
uint32_t flash_total_bytes = BREAKY_COMPILED_FLASH_TOTAL_BYTES;
uint32_t audio_capacity_bytes = 0;
bool bank_valid = false;
volatile bool bank_mutating = false;

const BreakyBankHeader* flash_header() {
  return reinterpret_cast<const BreakyBankHeader*>(XIP_BASE + BREAKY_AUDIO_FLASH_OFFSET);
}

uint32_t capacity_from_flash_size(uint32_t flash_bytes) {
  if (flash_bytes <= BREAKY_AUDIO_FLASH_OFFSET + BREAKY_BANK_HEADER_SIZE) {
    return 0u;
  }
  return flash_bytes - BREAKY_AUDIO_FLASH_OFFSET - BREAKY_BANK_HEADER_SIZE;
}

uint32_t detect_flash_total_bytes() {
  uint8_t txbuf[4] = {0x9fu, 0u, 0u, 0u};
  uint8_t rxbuf[4] = {0u, 0u, 0u, 0u};
  flash_do_cmd(txbuf, rxbuf, sizeof(txbuf));

  const uint8_t capacity_code = rxbuf[3];
  if (rxbuf[1] == 0 || rxbuf[1] == 0xff || capacity_code < 16u ||
      capacity_code > 31u) {
    return BREAKY_COMPILED_FLASH_TOTAL_BYTES;
  }
  return 1u << capacity_code;
}

bool record_valid(const BreakyBankSampleRecord& record, uint32_t total_audio_bytes) {
  if (record.frame_count == 0) {
    return false;
  }
  if (record.source_bpm == 0) {
    return false;
  }
  if (record.offset > total_audio_bytes) {
    return false;
  }
  return record.frame_count <= total_audio_bytes - record.offset;
}

void sanitize_name(char* name, uint32_t len) {
  name[len - 1u] = '\0';
  for (uint32_t j = 0; j < len - 1u; ++j) {
    unsigned char value = static_cast<unsigned char>(name[j]);
    if (value == 0 || value == 0xff) {
      name[j] = '\0';
      break;
    }
    if (value < 0x20 || value > 0x7e) {
      name[j] = '_';
    }
  }
}

}  // namespace

void breaky_audio_bank_init() {
  flash_total_bytes = detect_flash_total_bytes();
  audio_capacity_bytes = capacity_from_flash_size(flash_total_bytes);
  breaky_audio_bank_rescan();
}

void breaky_audio_bank_rescan() {
  const BreakyBankHeader* header = flash_header();
  bank_valid = false;
  sample_count = 0;
  audio_bytes = 0;
  memset(samples, 0, sizeof(samples));

  if (header->magic != BREAKY_BANK_MAGIC ||
      header->version != BREAKY_BANK_VERSION ||
      header->header_size != BREAKY_BANK_HEADER_SIZE ||
      header->sample_rate != BREAKY_BANK_SAMPLE_RATE ||
      header->sample_count > BREAKY_BANK_MAX_SAMPLES ||
      header->audio_bytes > audio_capacity_bytes ||
      header->capacity_bytes > audio_capacity_bytes) {
    return;
  }

  for (uint32_t i = 0; i < header->sample_count; ++i) {
    const BreakyBankSampleRecord& record = header->samples[i];
    if (!record_valid(record, header->audio_bytes)) {
      return;
    }

    samples[i].offset = record.offset;
    samples[i].frame_count = record.frame_count;
    samples[i].source_bpm = record.source_bpm;
    samples[i].peak = record.peak;
    samples[i].flags = record.flags;
    memcpy(samples[i].name, record.name, sizeof(samples[i].name));
    sanitize_name(samples[i].name, sizeof(samples[i].name));
  }

  sample_count = header->sample_count;
  audio_bytes = header->audio_bytes;
  bank_valid = true;
}

bool breaky_audio_bank_valid() {
  return bank_valid;
}

bool breaky_audio_bank_mutating() {
  return bank_mutating;
}

void breaky_audio_bank_set_mutating(bool mutating) {
  bank_mutating = mutating;
  __asm volatile("dmb" ::: "memory");
}

uint32_t breaky_audio_sample_count() {
  return bank_valid ? sample_count : 0u;
}

uint32_t breaky_audio_audio_bytes() {
  return bank_valid ? audio_bytes : 0u;
}

uint32_t breaky_audio_capacity_bytes() {
  return audio_capacity_bytes;
}

uint32_t breaky_flash_total_bytes() {
  return flash_total_bytes;
}

uint32_t breaky_audio_flash_offset() {
  return BREAKY_AUDIO_FLASH_OFFSET;
}

const BreakyAudioSample& breaky_audio_sample(uint32_t index) {
  if (!bank_valid || sample_count == 0 || index >= sample_count) {
    return empty_sample;
  }
  return samples[index];
}

uint8_t breaky_audio_read_byte(uint32_t offset) {
  const uint8_t* data =
      reinterpret_cast<const uint8_t*>(XIP_BASE + BREAKY_AUDIO_FLASH_OFFSET +
                                       BREAKY_BANK_HEADER_SIZE);
  return data[offset];
}
