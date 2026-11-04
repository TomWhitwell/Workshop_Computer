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

struct Diagnostics {
    volatile uint32_t queueOverruns = 0;
    volatile uint32_t readMisses = 0;
    volatile uint32_t erases = 0;
    volatile uint32_t programs = 0;
    volatile uint32_t maxQueued = 0;
    volatile uint32_t hardwareFault = 0;
};
extern Diagnostics diagnostics;

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
bool Available(unsigned channel, unsigned head, uint32_t pos);
// Core 1 only. Hook services controls even inside a suspended sector erase.
void Service(void (*controlHook)());

#ifdef TEMPERANCE_HOST_TEST
const uint8_t *TestFlash();
void TestFlashSize(uint32_t bytes);
void TestProgramDelay(void (*hook)());
#endif
}
