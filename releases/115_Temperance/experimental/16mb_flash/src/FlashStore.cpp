/*
 * Temperance 16MB flash experiment, 2026-11-04.
 * QSPI helpers and streaming approach adapted from Goldfish 2.0 by
 * Dune Desormeaux: releases/11_goldfish/goldfish_stream.c and .h.
 * Uses Dune Desormeaux's MLRws ADPCM implementation via Goldfish (GPL-3.0).
 * Per-page format, independent queues and cache integration generated with
 * OpenAI Codex at the user's direction; upstream authors retain their credit.
 * Goldfish release has no explicit license notice: see ../ATTRIBUTION.md.
 */
#include "FlashStore.h"
#include "adpcm.h"
#include <cstring>
#ifdef TEMPERANCE_HOST_TEST
#include <cassert>
#include <vector>
#define __dmb() __atomic_thread_fence(__ATOMIC_SEQ_CST)
#else
#include "pico/bootrom.h"
#include "pico/time.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/structs/ssi.h"
#include "hardware/structs/ioqspi.h"
#endif

namespace temflash {
Diagnostics diagnostics;
namespace {
struct Page {
    uint32_t generation, block, usedBytes;
    uint8_t bytes[kPageBytes];
};
struct CachedBlock {
    volatile uint32_t sequence = 0, generation = 0, block = ~0u;
    volatile int16_t pcm[kBlockSamples] = {};
};
struct Head {
    // Seqlock: core 0 publishes requests; core 1 publishes decoded blocks.
    volatile uint32_t sequence = 0, generation = 0, pos = 0, length = 0;
    volatile int32_t direction = 1;
    volatile bool active = false;
    CachedBlock cache[3];
    int32_t last = 0; // core 0 only
};
struct Channel {
    Page pages[kQueuePages];
    volatile uint32_t write = 0, read = 0;
    // Producer-only encoder/session state, never reset by the consumer.
    uint32_t generation = 0, samples = 0, fill = 0;
    adpcm_state_t encoder = {0, 0};
    bool recording = false;
    // Consumer-only erase frontier; published flush generation/block count.
    uint32_t ioGeneration = 0, nextErase = 0;
    volatile uint32_t flushedGeneration = 0, flushedBlocks = 0;
    Head heads[2]; // main and speed-transition ghost
};
Channel channels[2];
bool enabled = false;
void (*controls)() = nullptr;
constexpr uint32_t Base(unsigned c) { return kReserve + c * kRegionBytes; }
static_assert(kQueuePages && !(kQueuePages & (kQueuePages - 1)), "power of two queue");
static_assert(kRegionBytes % 4096 == 0, "sector aligned regions");

#ifdef TEMPERANCE_HOST_TEST
std::vector<uint8_t> flash(kFlashBytes, 0xff);
uint32_t testFlashSize = kFlashBytes;
void (*programDelay)() = nullptr;
const uint8_t *FlashAt(uint32_t offset) { return flash.data() + offset; }
void Program(uint32_t offset, const uint8_t *data) {
    if (programDelay) programDelay();
    for (unsigned i = 0; i < kPageBytes; ++i) {
        assert(flash.at(offset + i) == 0xff); // each page must first be erased
        flash.at(offset + i) &= data[i];
    }
}
#else
#include "FlashHardware.inc"
const uint8_t *FlashAt(uint32_t offset) {
    return reinterpret_cast<const uint8_t *>(XIP_BASE + offset);
}
void Program(uint32_t offset, const uint8_t *data) { flash_program_page(offset, data); }
#endif

void Refill();

void Flushed(Channel &c, const Page &page) {
    c.flushedBlocks = page.block + 1;
    __dmb();
    c.flushedGeneration = page.generation;
    __dmb();
    c.read = c.read + 1;
    ++diagnostics.programs;
}

// Only drain pages in sectors already erased for THAT page's session.
// This also lets a different channel keep recording during this erase.
bool DrainErased(bool commandMode) {
    bool did = false;
    for (unsigned n = 0; n < 2 && Healthy(); ++n) {
        Channel &c = channels[n];
        if (c.read == c.write) continue;
        __dmb();
        Page &p = c.pages[c.read & (kQueuePages - 1)];
        uint32_t offset = Base(n) + p.block * kPageBytes;
        if (p.generation != c.ioGeneration || offset >= c.nextErase) continue;
        // Final-page padding belongs on core 1, away from the audio deadline.
        for (uint32_t i = p.usedBytes; i < kPageBytes; ++i) p.bytes[i] = 0;
#ifndef TEMPERANCE_HOST_TEST
        if (commandMode) qspi_program_page(offset, p.bytes);
        else
#else
        (void)commandMode;
#endif
            Program(offset, p.bytes);
        if (!Healthy()) return did;
        Flushed(c, p);
        did = true;
    }
    return did;
}

void Erase(uint32_t offset) {
#ifdef TEMPERANCE_HOST_TEST
    std::memset(flash.data() + offset, 0xff, 4096);
    DrainErased(false);
    Refill();
    if (controls) controls();
#else
    // Goldfish's suspend/program/refill/resume sequence, bounded by elapsed
    // time. All code, constants and audio buffers live in RAM.
    uint32_t ints = save_and_disable_interrupts();
    s_rom_connect();
    s_rom_exit_xip();
    qspi_write_enable();
    uint8_t er[4] = {0x20, uint8_t(offset >> 16), uint8_t(offset >> 8), uint8_t(offset)};
    qspi_xfer(er, nullptr, 4);
    uint32_t start = time_us_32(), last = start;
    while (qspi_status() & 1u) {
        uint32_t now = time_us_32();
        if (now - start > 4000000u) {
            diagnostics.hardwareFault = 2;
            break; // remain in command mode; no more XIP reads after fault
        }
        if (now - last < 500u) continue;
        uint8_t command = 0x75;
        qspi_xfer(&command, nullptr, 1);
        busy_wait_us_32(20);
        // Confirm SUS in status register 2 before touching another sector.
        uint8_t tx[2] = {0x35, 0}, rx[2] = {};
        qspi_xfer(tx, rx, 2);
        if (!(rx[1] & 0x80u)) {
            if (!(qspi_status() & 1u)) break; // erase finished before suspend
            diagnostics.hardwareFault = 3;
            break;
        }
        DrainErased(true);
        if (!Healthy()) break;
        s_rom_flush();
        s_rom_enter_xip();
        Refill();
        if (controls) controls();
        s_rom_connect();
        s_rom_exit_xip();
        command = 0x7a;
        qspi_xfer(&command, nullptr, 1);
        busy_wait_us_32(30);
        last = time_us_32();
    }
    if (Healthy()) { s_rom_flush(); s_rom_enter_xip(); }
    restore_interrupts(ints);
#endif
    ++diagnostics.erases;
}

void Publish(Channel &c) {
    Page &p = c.pages[c.write & (kQueuePages - 1)];
    p.generation = c.generation;
    p.block = c.samples ? (c.samples - 1) / kBlockSamples : 0;
    p.usedBytes = 4 + (c.fill + 1) / 2;
    __dmb();
    c.write = c.write + 1;
    uint32_t queued = c.write - c.read;
    if (queued > diagnostics.maxQueued) diagnostics.maxQueued = queued;
    c.fill = 0;
}

bool Cached(const CachedBlock &b, uint32_t gen, uint32_t block) {
    return !(b.sequence & 1u) && b.generation == gen && b.block == block;
}

void Refill() {
    for (unsigned n = 0; n < 2 && Healthy(); ++n) {
        Channel &c = channels[n];
        for (Head &h : c.heads) {
            uint32_t seq = h.sequence;
            __dmb();
            uint32_t gen = h.generation, pos = h.pos, len = h.length;
            int dir = h.direction;
            bool active = h.active;
            __dmb();
            if ((seq & 1u) || seq != h.sequence || !active || !len ||
                gen != c.flushedGeneration || pos >= len) continue;
            uint32_t blocks = (len + kBlockSamples - 1) / kBlockSamples;
            uint32_t block = pos / kBlockSamples;
            // Current block, next block in direction, then one more runway.
            // Wrap prefetch avoids a flash seek on the seam.
            uint32_t wanted[3] = {block, 0, 0};
            for (unsigned i = 1; i < 3; ++i)
                wanted[i] = dir >= 0 ? (wanted[i-1] + 1) % blocks :
                    (wanted[i-1] ? wanted[i-1] - 1 : blocks - 1);
            for (unsigned i = 0; i < 3; ++i) {
                bool found = false;
                for (auto &b : h.cache) found |= Cached(b, gen, wanted[i]);
                if (found || wanted[i] >= c.flushedBlocks) continue;
                CachedBlock *dest = nullptr;
                for (auto &b : h.cache) {
                    bool needed = false;
                    for (uint32_t w : wanted) needed |= Cached(b, gen, w);
                    if (!needed) { dest = &b; break; }
                }
                if (!dest) continue;
                const uint8_t *page = FlashAt(Base(n) + wanted[i] * kPageBytes);
                adpcm_state_t decoder = {int16_t(uint16_t(page[0]) | uint16_t(page[1]) << 8),
                                         int8_t(page[2])};
                if (decoder.step_index < 0 || decoder.step_index > 88) {
                    diagnostics.hardwareFault = 4;
                    return;
                }
                ++dest->sequence;
                __dmb();
                for (uint32_t j = 0; j < kBlockSamples; ++j) {
                    uint8_t byte = page[4 + j / 2];
                    dest->pcm[j] = adpcm_decode((j & 1) ? byte >> 4 : byte & 15, &decoder);
                }
                dest->generation = gen;
                dest->block = wanted[i];
                __dmb();
                ++dest->sequence;
                break; // at most one decoded block per head per service pass
            }
        }
    }
}
}

bool Init() {
#ifdef TEMPERANCE_HOST_TEST
    enabled = testFlashSize == kFlashBytes;
#else
    qspi_rom_init();
    uint8_t tx[4] = {0x9f, 0, 0, 0}, rx[4] = {};
    flash_do_cmd(tx, rx, 4);
    // Winbond W25Q128, 16MiB. No permissive size fallback for a recording build.
    enabled = rx[1] == 0xef && rx[3] == 0x18;
#endif
    return enabled;
}

bool Healthy() { return enabled && !diagnostics.hardwareFault && !diagnostics.queueOverruns; }

void Start(unsigned n) {
    Channel &c = channels[n];
    // Monotonic queues retain old pages: consumer finishes their session
    // before erasing/reusing a region. Never reset a queue underneath core 1.
    ++c.generation;
    c.samples = c.fill = 0;
    c.encoder = {0, 0};
    c.recording = Healthy();
    for (unsigned h = 0; h < 2; ++h) {
        channels[n].heads[h].last = 0;
        Request(n, h, 0, 1, 0, false);
    }
}

bool Record(unsigned n, int32_t sample) {
    Channel &c = channels[n];
    if (!c.recording || !Healthy() || c.samples >= kCapacity) return false;
    if (c.fill == 0 && c.write - c.read >= kQueuePages) {
        ++diagnostics.queueOverruns;
        c.recording = false;
        return false; // stop instead of silently overwriting unconsumed audio
    }
    Page &p = c.pages[c.write & (kQueuePages - 1)];
    if (c.fill == 0) {
        p.bytes[0] = uint8_t(c.encoder.predictor);
        p.bytes[1] = uint8_t(uint16_t(c.encoder.predictor) >> 8);
        p.bytes[2] = uint8_t(c.encoder.step_index);
        p.bytes[3] = 0;
    }
    uint8_t nibble = adpcm_encode(int16_t(sample * 16), &c.encoder);
    uint32_t byte = 4 + c.fill / 2;
    if (c.fill & 1) p.bytes[byte] |= uint8_t(nibble << 4);
    else p.bytes[byte] = nibble;
    ++c.samples;
    if (++c.fill == kBlockSamples) Publish(c);
    return true;
}

void Stop(unsigned n) {
    Channel &c = channels[n];
    c.recording = false;
    if (!Healthy() || !c.fill) return;
    // Padding isn't in the logical loop; its exact odd-sample length survives.
    Publish(c);
}

bool Ready(unsigned n) {
    Channel &c = channels[n];
    uint32_t gen = c.flushedGeneration;
    __dmb();
    return Healthy() && gen == c.generation &&
        c.flushedBlocks >= (c.samples + kBlockSamples - 1) / kBlockSamples;
}

void Request(unsigned n, unsigned head, uint32_t pos, int dir, uint32_t len, bool active) {
    Head &h = channels[n].heads[head];
    ++h.sequence;
    __dmb();
    h.generation = channels[n].generation;
    h.pos = pos;
    h.direction = dir;
    h.length = len;
    h.active = active;
    __dmb();
    ++h.sequence;
}

bool Available(unsigned n, unsigned head, uint32_t pos) {
    if (!Healthy()) return false;
    for (auto &b : channels[n].heads[head].cache)
        if (Cached(b, channels[n].generation, pos / kBlockSamples)) return true;
    return false;
}

int32_t Read(unsigned n, unsigned head, uint32_t pos) {
    if (!Healthy()) return 0;
    Channel &c = channels[n];
    Head &h = c.heads[head];
    for (auto &b : h.cache) {
        uint32_t seq = b.sequence;
        __dmb();
        if ((seq & 1u) || b.generation != c.generation || b.block != pos / kBlockSamples) continue;
        int32_t value = b.pcm[pos % kBlockSamples];
        __dmb();
        if (seq != b.sequence) continue;
        h.last = value / 16;
        return h.last;
    }
    ++diagnostics.readMisses;
    return h.last; // bounded hold, never fetch flash from the audio core
}

void Service(void (*hook)()) {
    controls = hook;
    if (!Healthy()) return;
    Refill();
    for (unsigned n = 0; n < 2 && Healthy(); ++n) {
        Channel &c = channels[n];
        if (c.read == c.write) continue;
        __dmb();
        const Page &p = c.pages[c.read & (kQueuePages - 1)];
        if (c.ioGeneration != p.generation) {
            c.ioGeneration = p.generation;
            c.nextErase = Base(n);
            c.flushedGeneration = 0;
            c.flushedBlocks = 0;
        }
        uint32_t target = Base(n) + p.block * kPageBytes;
        // One extra erased sector provides Goldfish-style write runway.
        uint32_t ahead = (target & ~4095u) + 8192;
        if (ahead > Base(n) + kRegionBytes) ahead = Base(n) + kRegionBytes;
        while (c.nextErase < ahead && Healthy()) {
            Erase(c.nextErase);
            c.nextErase += 4096;
        }
    }
    // Bound each pass so UI updates and the other channel cannot starve.
    for (unsigned i = 0; i < 4 && Healthy(); ++i)
        if (!DrainErased(false)) break;
    Refill();
    if (controls) controls();
}

#ifdef TEMPERANCE_HOST_TEST
const uint8_t *TestFlash() { return flash.data(); }
void TestFlashSize(uint32_t bytes) { testFlashSize = bytes; }
void TestProgramDelay(void (*hook)()) { programDelay = hook; }
#endif
}
