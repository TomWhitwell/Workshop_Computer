/**
 * @file config.hpp
 * @brief Shared frame rates, timing budgets and hardware-frame cadence helpers.
 *
 * Single source of truth for the cross-core timing contract: core0 owns the
 * 48 kHz ComputerCard callback and refreshes control state on every 64th frame
 * (750 Hz); core1's instrument advances on alternate 24 kHz frames. Queue
 * capacity, the elastic output lead, boot settling, pulse length and the
 * diagnostic warning thresholds live here so core0, core1 and the desktop
 * harness agree without sharing mutable state.
 *
 * Formats: audio is signed Q15, pitch/period state is Q16 samples, durations
 * are frame counts at the rate named in each comment. Everything is
 * compile-time only; nothing in this header runs in a callback.
 *
 *
 * @date 2026-11-16
 */
#pragma once
#include <cstdint>

namespace computer::timing {
// Hardware frames are 48 kHz; the instrument advances on alternate frames.
/// @brief Core0's callback rate (ADC/DAC/probe/control) in Hz.
constexpr uint32_t hardwareRate = 48000;
/// @brief Core1's voice-advance rate in Hz; exactly half of hardwareRate.
constexpr uint32_t voiceRate = 24000;
/// @brief Frames between 750 Hz control updates; switch edges also refresh.
constexpr uint32_t controlInterval = 64; // 750 Hz, with an extra read on switch edges.
/// @brief Scale factor between signed 12-bit ADC/DAC codes and Q15 audio.
constexpr int32_t adcToQ15 = 16; // Signed 12-bit ADC/DAC values versus Q15 audio.
/// @brief Scale factor from Q15 audio back to signed 12-bit DAC codes.
constexpr int32_t q15ToDac = 16; // Division must round toward zero, not arithmetic shift.
/// @brief Capacity of each SPSC cross-core queue in elements (power of two).
constexpr uint32_t queueCapacity = 128;
/// @brief Silent 24 kHz frames pre-pushed to the output queue before real output.
constexpr uint32_t initialOutputFrames = 64; // 2.667 ms of elastic queue lead.
/// @brief Boot-only silence in 48 kHz frames so ADC mux/jack probes settle.
constexpr int32_t bootQuietFrames = 1536; // 32 ms for ADC/probe startup.
/// @brief Length of Pulse1's pluck-accepted output pulse in 48 kHz frames.
constexpr int32_t pluckPulseFrames = 240; // 5 ms at the hardware rate.
/// @brief Worker time above which SWD diagnostics latch a warning, in us.
constexpr uint32_t workerWarningUs = 42;
/// @brief Callback time above which SWD diagnostics latch a warning, in us.
constexpr uint32_t callbackWarningUs = 21;
/// @brief Complete IRQ-body budget that latches irqOverruns when exceeded.
constexpr uint32_t irqWarningUs = 20; // Conservative body budget; exception overhead is extra.

/// @brief True on the 64th-boundary frames that run the 750 Hz control cadence.
///
/// The mask with interval minus one selects every 64th hardware frame,
/// frame 0 included. Switch transitions also refresh controls, outside this
/// predicate.
/// @param frame Monotonic 48 kHz hardware frame counter, any start value.
/// @return True on frames divisible by controlInterval, otherwise false.
constexpr bool controlFrame(uint32_t frame) {
    return (frame & (controlInterval - 1u)) == 0;
}
/// @brief True on even hardware frames, which advance the 24 kHz instrument.
/// @param frame Monotonic 48 kHz hardware frame counter, any start value.
/// @return True on even frames, false on odd (zero-stuffed) frames.
constexpr bool voiceFrame(uint32_t frame) {
    return (frame & 1u) == 0;
}
/// @note Decimation/layout invariants; violated only when the rate design changes.
static_assert(hardwareRate == 2 * voiceRate);
static_assert((controlInterval & (controlInterval - 1u)) == 0);
} // namespace computer::timing
