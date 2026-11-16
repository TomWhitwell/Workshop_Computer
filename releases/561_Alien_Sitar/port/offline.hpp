/**
 * @file offline.hpp
 * @brief Synchronous desktop offline replay of the hardware callback path.
 *
 * Desktop-only adapter (tests, renders and measurement tools): calls at 48 kHz
 * re-run the same controller cadence, input/output FIRs, event edge detection,
 * 24 kHz Processor pipeline and output conditioning as the firmware callback,
 * without queues, timing or hardware. Renders therefore match the DSP event
 * ordering, while remaining free of IRQ scheduling effects.
 *
 *
 * @date 2026-11-16
 */
#pragma once
#include "auxiliary.hpp"
#include "events.hpp"
#include "output.hpp"
#include "processor.hpp"
namespace computer {
/// @brief Synchronous equivalent of the 48 kHz callback and 24 kHz worker pair.
///
/// Synchronous offline equivalent of the hardware 48/24k filter paths. Does not
/// model core1 execution time or the firmware's additional 64-frame elastic lead.
class Offline {
public:
    /// @brief Replay one 48 kHz hardware-frame equivalent, advancing every stage.
    /// @param controls Fresh control snapshot (knobs, millivolts, jack and
    ///        switch state) exactly as the hardware callback would sample it.
    /// @param pluck Pluck request for this frame (Pulse1-equivalent, edges
    ///        detected internally, sticky until the worker frame consumes it).
    /// @param damp Damp request for this frame (Pulse2-equivalent, edges
    ///        detected internally, sticky until the worker frame consumes it).
    /// @param stringInput External excitation for the melody channel (signed
    ///        Q15 after the caller's scaling, or 0 when unpatched/Silence).
    /// @param haloInput External excitation for the sympathetic channel
    ///        (signed Q15, or 0 when unpatched).
    /// @return One 48 kHz frame: mix/wet are conditioned outputs, dry and
    ///         periodQ16 hold the most recent 24 kHz voice-frame values.
    mtm::Frame next(const Controls &controls, bool pluck = false, bool damp = false,
                    int32_t stringInput = 0, int32_t haloInput = 0);

    /// @brief Constant view of the follower, bloom and pitch-output state.
    /// @return The auxiliary outputs advanced at the 24 kHz worker rate here.
    const AuxiliaryOutputs &auxiliaryOutputs() const { return auxiliary; }

private:
    /// @brief Worker equivalent owning the 24 kHz instrument.
    Processor processor;
    /// @brief Input decimation (in1/in2, 48 to 24 kHz) and output interpolation
    ///        (out1/out2, 24 to 48 kHz) FIRs mirroring the firmware callback.
    mtm::Fir in1, in2, out1, out2;
    /// @brief Most recent 24 kHz voice frame; held across non-voice frames.
    mtm::Frame frame{};
    /// @brief Core0-equivalent control state machine (750 Hz plus switch edges).
    Controller controller;
    /// @brief Transient pluck/damp edge detection at the 48 kHz call rate.
    Events events;
    /// @brief Settings snapshot installed by the most recent control update.
    Settings settings;
    /// @brief Auxiliary outputs advanced once per generated voice frame.
    AuxiliaryOutputs auxiliary;
    /// @brief 48 kHz output conditioning identical to the hardware path.
    Output output;
    /// @brief Monotonic 48 kHz tick driving controlFrame()/voiceFrame() cadences.
    uint32_t tick = 0;
};
} // namespace computer
