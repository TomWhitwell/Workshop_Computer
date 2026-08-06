#include "BreakySampleManager.h"

#include <stdio.h>
#include <string.h>

#include "BreakyAudioBank.h"
#include "hardware/flash.h"
#include "pico/stdio.h"
#include "pico/stdio_usb.h"
#include "pico/stdlib.h"

#ifndef XIP_BASE
#define XIP_BASE 0x10000000u
#endif

namespace {

static constexpr uint32_t kFlashSectorSize = 4096u;
static constexpr uint32_t kFlashPageSize = 256u;
static constexpr uint32_t kReadChunkSize = 1024u;
static constexpr uint8_t kReadAck = 'A';
static constexpr uint32_t kReadTimeoutMs = 15000u;
static constexpr uint32_t kWriteTimeoutMs = 5000u;

uint8_t header_staging[BREAKY_BANK_HEADER_SIZE] __attribute__((aligned(4)));
uint8_t page_buf[kFlashPageSize] __attribute__((aligned(4)));

struct ScopedPlaybackMute {
  ScopedPlaybackMute() {
    breaky_audio_bank_set_mutating(true);
  }

  ~ScopedPlaybackMute() {
    breaky_audio_bank_set_mutating(false);
  }
};

int read_byte_timeout(uint32_t timeout_ms) {
  const absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
  while (!time_reached(deadline)) {
    const int value = getchar_timeout_us(0);
    if (value != PICO_ERROR_TIMEOUT) {
      return value & 0xff;
    }
    sleep_ms(1);
  }
  return PICO_ERROR_TIMEOUT;
}

bool read_exact(uint8_t* dst, uint32_t len, uint32_t timeout_ms) {
  for (uint32_t i = 0; i < len; ++i) {
    const int value = read_byte_timeout(timeout_ms);
    if (value == PICO_ERROR_TIMEOUT) {
      return false;
    }
    dst[i] = static_cast<uint8_t>(value);
  }
  return true;
}

void write_bytes(const void* data, uint32_t len) {
  const uint8_t* bytes = static_cast<const uint8_t*>(data);
  for (uint32_t i = 0; i < len; ++i) {
    putchar_raw(bytes[i]);
  }
}

void write_u32(uint32_t value) {
  write_bytes(&value, sizeof(value));
}

void write_str(const char* text) {
  write_bytes(text, static_cast<uint32_t>(strlen(text)));
}

void flush_serial() {
  stdio_flush();
}

void send_sync() {
  write_str("SYNC\n");
  flush_serial();
}

bool validate_header(const BreakyBankHeader& header, uint32_t total_len) {
  if (total_len < BREAKY_BANK_HEADER_SIZE) {
    return false;
  }
  const uint32_t audio_bytes = total_len - BREAKY_BANK_HEADER_SIZE;
  if (header.magic != BREAKY_BANK_MAGIC ||
      header.version != BREAKY_BANK_VERSION ||
      header.header_size != BREAKY_BANK_HEADER_SIZE ||
      header.sample_rate != BREAKY_BANK_SAMPLE_RATE ||
      header.sample_count > BREAKY_BANK_MAX_SAMPLES ||
      header.audio_bytes != audio_bytes ||
      header.audio_bytes > breaky_audio_capacity_bytes() ||
      header.capacity_bytes > breaky_audio_capacity_bytes()) {
    return false;
  }

  for (uint32_t i = 0; i < header.sample_count; ++i) {
    const BreakyBankSampleRecord& record = header.samples[i];
    if (record.frame_count == 0 || record.source_bpm == 0 ||
        record.offset > header.audio_bytes ||
        record.frame_count > header.audio_bytes - record.offset) {
      return false;
    }
  }
  return true;
}

void handle_info() {
  char info[256];
  uint32_t used = 0;
  int n = snprintf(info + used, sizeof(info) - used,
                   "STRETCHCORE1 FW 2.0 F %lu R %lu A %lu C %lu U %lu SR %lu N %lu\nEND\n",
                   static_cast<unsigned long>(breaky_flash_total_bytes()),
                   static_cast<unsigned long>(BREAKY_FIRMWARE_RESERVE),
                   static_cast<unsigned long>(breaky_audio_flash_offset()),
                   static_cast<unsigned long>(breaky_audio_capacity_bytes()),
                   static_cast<unsigned long>(breaky_audio_audio_bytes()),
                   static_cast<unsigned long>(BREAKY_BANK_SAMPLE_RATE),
                   static_cast<unsigned long>(breaky_audio_sample_count()));
  if (n < 0 || static_cast<uint32_t>(n) >= sizeof(info) - used) {
    return;
  }
  used += static_cast<uint32_t>(n);

  write_u32(used);
  write_bytes(info, used);
  flush_serial();
}

void handle_read() {
  if (!breaky_audio_bank_valid()) {
    write_u32(0);
    flush_serial();
    return;
  }

  const uint32_t total_len = BREAKY_BANK_HEADER_SIZE + breaky_audio_audio_bytes();
  ScopedPlaybackMute playback_mute;
  write_u32(total_len);
  flush_serial();
  const uint8_t* src =
      reinterpret_cast<const uint8_t*>(XIP_BASE + BREAKY_AUDIO_FLASH_OFFSET);

  uint32_t sent = 0;
  while (sent < total_len) {
    const uint32_t chunk =
        total_len - sent < kReadChunkSize ? total_len - sent : kReadChunkSize;
    write_bytes(src + sent, chunk);
    flush_serial();
    const int ack = read_byte_timeout(kReadTimeoutMs);
    if (ack != kReadAck) {
      send_sync();
      return;
    }
    sent += chunk;
  }
  write_str("DONE\n");
  flush_serial();
}

void erase_bank_header() {
  breaky_audio_bank_set_mutating(true);
  flash_range_erase(BREAKY_AUDIO_FLASH_OFFSET, kFlashSectorSize);
  breaky_audio_bank_rescan();
  breaky_audio_bank_set_mutating(false);
}

void drain_rejected_write(uint32_t remaining) {
  while (remaining > 0) {
    uint8_t scratch[32];
    const uint32_t chunk = remaining < sizeof(scratch) ? remaining : sizeof(scratch);
    if (!read_exact(scratch, chunk, kWriteTimeoutMs)) {
      return;
    }
    remaining -= chunk;
  }
}

void handle_write() {
  uint8_t len_buf[4];
  if (!read_exact(len_buf, sizeof(len_buf), kWriteTimeoutMs)) {
    write_str("TIMEOUT\n");
    flush_serial();
    return;
  }
  uint32_t total_len = 0;
  memcpy(&total_len, len_buf, sizeof(total_len));

  if (total_len < BREAKY_BANK_HEADER_SIZE ||
      total_len > BREAKY_BANK_HEADER_SIZE + breaky_audio_capacity_bytes()) {
    write_str("ERR\n");
    flush_serial();
    drain_rejected_write(total_len);
    return;
  }

  write_str("OK\n");
  flush_serial();

  if (!read_exact(header_staging, BREAKY_BANK_HEADER_SIZE, kWriteTimeoutMs)) {
    write_str("TIMEOUT\n");
    flush_serial();
    return;
  }

  const BreakyBankHeader* header =
      reinterpret_cast<const BreakyBankHeader*>(header_staging);
  if (!validate_header(*header, total_len)) {
    write_str("ERR\n");
    flush_serial();
    drain_rejected_write(total_len - BREAKY_BANK_HEADER_SIZE);
    return;
  }

  breaky_audio_bank_set_mutating(true);

  flash_range_erase(BREAKY_AUDIO_FLASH_OFFSET, kFlashSectorSize);

  uint32_t bytes_written = BREAKY_BANK_HEADER_SIZE;
  uint32_t audio_flash_off = BREAKY_AUDIO_FLASH_OFFSET + BREAKY_BANK_HEADER_SIZE;
  uint32_t next_erase = audio_flash_off;

  while (bytes_written < total_len) {
    const uint32_t remaining = total_len - bytes_written;
    const uint32_t page_fill = remaining < kFlashPageSize ? remaining : kFlashPageSize;
    memset(page_buf, 0xff, sizeof(page_buf));
    if (!read_exact(page_buf, page_fill, kWriteTimeoutMs)) {
      breaky_audio_bank_rescan();
      breaky_audio_bank_set_mutating(false);
      write_str("TIMEOUT\n");
      flush_serial();
      return;
    }

    const uint32_t page_off = audio_flash_off + (bytes_written - BREAKY_BANK_HEADER_SIZE);
    if (page_off >= next_erase) {
      flash_range_erase(next_erase, kFlashSectorSize);
      next_erase += kFlashSectorSize;
    }
    flash_range_program(page_off, page_buf, sizeof(page_buf));
    bytes_written += page_fill;
  }

  memset(page_buf, 0xff, sizeof(page_buf));
  for (uint32_t offset = 0; offset < BREAKY_BANK_HEADER_SIZE; offset += kFlashPageSize) {
    memcpy(page_buf, header_staging + offset, kFlashPageSize);
    flash_range_program(BREAKY_AUDIO_FLASH_OFFSET + offset, page_buf, sizeof(page_buf));
  }

  breaky_audio_bank_rescan();
  breaky_audio_bank_set_mutating(false);
  write_str("OK\n");
  flush_serial();
}

}  // namespace

void breaky_sample_manager_core() {
  while (true) {
    if (!stdio_usb_connected()) {
      sleep_ms(20);
      continue;
    }

    const int value = getchar_timeout_us(1000);
    if (value == PICO_ERROR_TIMEOUT) {
      continue;
    }

    switch (value & 0xff) {
      case 'X':
        send_sync();
        break;
      case 'I':
        handle_info();
        break;
      case 'R':
        handle_read();
        break;
      case 'W':
        handle_write();
        break;
      case 'E':
        erase_bank_header();
        write_str("OK\n");
        flush_serial();
        break;
      default:
        break;
    }
  }
}
