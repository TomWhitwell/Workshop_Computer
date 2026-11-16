/**
 * @file processor.hpp
 * @brief Worker-side 24 kHz stage: settings, events and excitation into frames.
 *
 * Processor chains the worker-side pipeline shared by core1 firmware and the
 * synchronous desktop harness: it applies one immutable Packet (controller
 * settings, filtered external excitation, sticky pluck/damp events) to the
 * mtm::Voice and returns one mtm::Frame (mix/dry/halo plus smoothed Q16
 * period). Core0 then derives levels, bloom pulses and pitch CV from the
 * returned frame; no state is shared across cores.
 *
 *
 * @date 2026-11-16
 */
#pragma once
#include "../dsp/voice.hpp"
#include "controller.hpp"
namespace computer {
/// @brief One worker owning the whole 24 kHz instrument update.
///
/// Used by core1 firmware and desktop harness; all DSP/event ordering is shared.
class Processor {
public:
    /// @brief Apply one settings/event packet and advance the voice one frame.
    /// @param packet Immutable snapshot: settings, filtered Q15 external
    ///        excitation and pending pluck/damp events; not referenced after return.
    /// @return One 24 kHz frame (mix/wet/dry audio plus the smoothed Q16 model
    ///         period) consumed by core0 for conditioning and auxiliary outputs.
    mtm::Frame process(const Packet &packet);
    /// @brief One-pole DC rejection shared by both external excitation channels.
    /// @param value Input sample for this frame (Q15 filters after the input FIR).
    /// @param state Persistent leaky DC estimate; caller keeps it per channel.
    /// @return Input with DC removed, clipped back into Q15 (+/-32767).
    ///
    /// Explicit state permits standalone numerical DC-rejection checks.
    static int32_t highpass(int32_t value, int32_t &state);

private:
    /// The string/sympathetic instrument state; core1-only in firmware.
    mtm::Voice voice;
    /// @brief DC-blocker states of the string/halo external inputs (Q15).
    int32_t stringDC = 0, haloDC = 0;
    /// @brief External-connect fades (Q15 gains) and Up-mode dry/wet mix (Q15).
    int32_t stringGain = 0, haloGain = 0, resonatorMix = 16384;
};
} // namespace computer
