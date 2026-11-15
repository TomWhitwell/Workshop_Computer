#pragma once
#include <cstdint>

namespace computer::timing {
// Hardware frames are 48 kHz; the instrument advances on alternate frames.
constexpr uint32_t hardwareRate = 48000;
constexpr uint32_t voiceRate = 24000;
constexpr uint32_t controlInterval = 64; // 750 Hz, with an extra read on switch edges.
constexpr int32_t adcToQ15 = 16;         // Signed 12-bit ADC/DAC values versus Q15 audio.
constexpr int32_t q15ToDac = 16;         // Division must round toward zero, not arithmetic shift.
constexpr uint32_t queueCapacity = 128;
constexpr uint32_t initialOutputFrames = 64; // 2.667 ms of elastic queue lead.
constexpr int32_t bootQuietFrames = 1536;    // 32 ms for ADC/probe startup.
constexpr int32_t pluckPulseFrames = 240;    // 5 ms at the hardware rate.
constexpr uint32_t workerWarningUs = 42;
constexpr uint32_t callbackWarningUs = 21;
constexpr uint32_t irqWarningUs = 20; // Conservative body budget; exception overhead is extra.

constexpr bool controlFrame(uint32_t frame) {
    return (frame & (controlInterval - 1u)) == 0;
}
constexpr bool voiceFrame(uint32_t frame) {
    return (frame & 1u) == 0;
}
static_assert(hardwareRate == 2 * voiceRate);
static_assert((controlInterval & (controlInterval - 1u)) == 0);
} // namespace computer::timing
