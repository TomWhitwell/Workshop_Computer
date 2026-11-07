// 2026-11-07: exp.2 audio-deadline fix; upstream notices retained below.
/*
 * Temperance 16MB flash experiment, 2026-11-04.
 * QSPI helpers and streaming approach adapted from Goldfish 2.0 by
 * Dune Desormeaux: releases/11_goldfish/goldfish_stream.c and .h.
 * Uses Dune Desormeaux's MLRws ADPCM implementation via Goldfish (GPL-3.0).
 * Per-page format, independent queues and cache integration generated with
 * OpenAI Codex at the user's direction; upstream authors retain their credit.
 * Overall experiment: GNU GPL version 3. See ../LICENSE and ../ATTRIBUTION.md.
 */
#pragma once
#include <cstdint>

// Goldfish-style SPSC page queues and core-1 erase-suspend I/O. Unlike
// Goldfish's shared stereo timeline, each channel has its own queue/session.
namespace temflash {
constexpr uint32_t kFlashBytes = 16u * 1024 * 1024;
constexpr uint32_t kReserve = 512u * 1024;
constexpr uint32_t kRegionBytes = (kFlashBytes - kReserve) / 2;
constexpr uint32_t kPageBytes = 256;
// Four bytes of decoder state + 252 bytes of ADPCM, independently seekable.
constexpr uint32_t kBlockSamples = 504;
constexpr uint32_t kCapacity = (kRegionBytes / kPageBytes) * kBlockSamples;
constexpr uint32_t kQueuePages = 32;

struct CachedBlock {
    volatile uint32_t sequence = 0, generation = 0, block = ~0u, start = ~0u;
    volatile int16_t pcm[kBlockSamples] = {};
};
struct PlaybackConfig {
    volatile uint32_t sequence = 0, generation = 0, length = 0;
    volatile int32_t direction = 1;
    volatile bool active = false;
};
struct PlaybackReader {
    CachedBlock *reading = nullptr; // only the audio core changes this pointer
    uint32_t start = 0, generation = 0;
    int32_t last = 0;
    CachedBlock *cache = nullptr;
    volatile uint32_t position = 0;
    PlaybackConfig *config = nullptr;
};
PlaybackReader *Reader(unsigned channel, unsigned head);

inline void ConfigurePlayback(PlaybackReader &r, int direction, uint32_t length) {
    auto &c = *r.config;
    ++c.sequence;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    c.generation = r.generation;
    c.direction = direction;
    c.length = length;
    c.active = true;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    ++c.sequence;
}

// Constant-time hot path, inlined at the looper. The large cache search lives
// in ReadNext and is only needed at page/session transitions. Call after Ready.
inline bool ReadCached(PlaybackReader &r, uint32_t pos, int32_t &value) {
    value = r.last;
    CachedBlock *b = r.reading;
    uint32_t offset = pos - r.start;
    if (!b || offset >= kBlockSamples) return false;
    uint32_t seq = b->sequence;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    if ((seq & 1u) || b->generation != r.generation ||
        b->start != r.start) return false;
    int32_t sample = b->pcm[offset];
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    if (seq != b->sequence) return false;
    value = r.last = sample / 16;
    return true;
}

struct Diagnostics {
    volatile uint32_t queueOverruns = 0;
    volatile uint32_t readMisses = 0;
    volatile uint32_t erases = 0;
    volatile uint32_t programs = 0;
    volatile uint32_t maxQueued = 0;
    volatile uint32_t hardwareFault = 0;
};
extern Diagnostics diagnostics;

__attribute__((always_inline)) inline bool ReadNext(PlaybackReader &r, uint32_t pos, int32_t &value) {
    value = r.last;
    if (!r.cache) return false;
    // Cache ranges are published by core 1: audio cache lookup never divides by 504.
    for (unsigned i = 0; i < 3; ++i) {
        auto &b = r.cache[i];
        uint32_t seq = b.sequence;
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        uint32_t start = b.start;
        uint32_t offset = pos - start;
        if ((seq & 1u) || b.generation != r.generation || offset >= kBlockSamples) continue;
        int32_t sample = b.pcm[offset];
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        if (seq != b.sequence) continue;
        r.reading = &b;
        r.start = start;
        value = r.last = sample / 16;
        return true;
    }
    ++diagnostics.readMisses;
    return false; // bounded hold, never fetch flash from the audio core
}


// Init probes JEDEC once, before core 1 starts. Only 16MB Winbond parts
// supporting 0x75/0x7A are accepted. No flash writes at startup.
bool Init();
bool Healthy();
void Start(unsigned channel); // core 0; old queued pages drain in order
bool Record(unsigned channel, int32_t sample); // signed 12-bit, one clock tick
void Stop(unsigned channel);
bool Ready(unsigned channel); // final page has reached flash
void Request(unsigned channel, unsigned head, uint32_t pos, int direction,
             uint32_t length, bool active);
int32_t Read(unsigned channel, unsigned head, uint32_t pos);
// Combined availability/read operation: audio must not scan a cache twice.
bool TryRead(unsigned channel, unsigned head, uint32_t pos, int32_t &value);
bool Available(unsigned channel, unsigned head, uint32_t pos);
// Core 1 only. Hook services controls even inside a suspended sector erase.
void Service(void (*controlHook)());

#ifdef TEMPERANCE_HOST_TEST
const uint8_t *TestFlash();
void TestFlashSize(uint32_t bytes);
void TestProgramDelay(void (*hook)());
#endif
}
