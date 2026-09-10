// gba_multiboot.h — GBA BIOS Multiboot uploader (RP2040 as master).
//
// Implements the single-game-pak / download-play multiboot protocol: sync with a
// cartridge-less GBA, stream a compiled multiboot ROM (linked for EWRAM @ 0x02000000)
// into it, and verify with the final CRC handshake. The GBA BIOS then runs the payload.
//
// Protocol + magic constants transcribed from the canonical open references
// (akkera102/gba_01_multiboot and the RP2040 port in copyrat90/gba-pico-gamepad), which
// agree exactly. The obfuscation constants (0x6f646573, 0x43202f2f, 0xc37b, 0xc387) are a
// fixed, publicly documented part of the BIOS handshake, not secrets.
#pragma once

#include <cstdint>
#include <cstddef>

enum class MultibootResult : uint8_t {
    Ok = 0,
    NoGBA,          // never saw the 0x7202 recognition value (not connected / wrong wiring / input too slow)
    BadHandshake,   // palette/handshake exchange didn't return the expected 0x73.. token
    TransferError,  // a data word's echo didn't match its offset (link corrupted mid-stream)
    CrcMismatch,    // final CRC handshake completed but values differed
    BadPayload,     // payload pointer/size invalid
};

// Upload progress, 0..100, published while a transfer runs and reset to 0 between attempts.
//
// This exists because the payload has grown from 5 kB to 37 kB: at 100 kHz that is five and a
// half seconds of solid transfer, which from the front panel is indistinguishable from a hang.
// A progress reading turns "it is stuck" into "it is working, wait".
extern volatile uint32_t gba_mb_progress;

// Attempt one full multiboot upload of `rom` (`rom_size` bytes; the multiboot .mb image,
// header included). Blocks on the SPI transport; intended to run on core 1. `rom_size`
// is rounded up to a 16-byte boundary internally. Returns Ok only after a matching CRC.
//
// This does NOT init the SPI transport — call gba_spi_init() first. On NoGBA it is safe
// to simply retry (that's the normal "waiting for the GBA to be ready" state).
MultibootResult gba_multiboot_send(const uint8_t *rom, size_t rom_size);

// DEPRECATED — a short settle delay, nothing more. It returns true unconditionally and its
// result carries no information, so NEVER gate on it.
//
// It once tested the MISO pad for a low level, based on an observation made while the pad
// inversion was set the other way round. When the polarity was corrected the test inverted
// its meaning, always timed out, and callers that gated on it stopped attempting multiboot
// entirely. The protocol's own bounded sync loop inside gba_multiboot_send() is the correct
// readiness test: it is polarity-independent and returns NoGBA promptly when nothing answers.
bool gba_wait_slave_ready(uint32_t timeoutMs);
