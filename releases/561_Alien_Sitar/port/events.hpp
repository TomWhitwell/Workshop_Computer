/**
 * @file events.hpp
 * @brief Transient pluck/damp edge detection merging callback events for the worker.
 *
 * Core0's 48 kHz callback samples Pulse1/Pulse2 and the Z switch every frame,
 * latching rising edges into sticky pluck/damp flags. The caller copies these
 * into a Packet and clears them only after that packet has entered the input
 * queue successfully, so retriggers during queue pressure are never lost and
 * at most one pluck/damp is delivered per accepted frame.
 *
 *
 * @date 2026-11-16
 */
#pragma once
#include "controller.hpp"
namespace computer {
/// @brief Sticky transient-event states merged by core0; all plain data.
struct Events {
    /// Previous-frame pluck/damp request baselines used for rising-edge
    /// detection; owned by the sampling callback, never read elsewhere.
    bool previousPluck = false, previousDamp = false;
    /// Z-switch baseline for detecting the transition into Down.
    Position previousPosition = Middle;
    /// Sticky pending events: a pluck from Pulse1 or Down entry, and a damp
    /// from Pulse2; cleared by core0 only after queue acceptance.
    bool pluck = false, damp = false;
    /// @brief Merge one hardware frame's raw event inputs into the sticky flags.
    /// @param p Raw pluck request this frame (Pulse1-equivalent, true while high).
    /// @param d Raw damp request this frame (Pulse2-equivalent, true while high).
    /// @param z Current Z-switch position at this hardware frame.
    /// @note Rising edges only; falling edges never add events.
    void sample(bool p, bool d, Position z);
};
} // namespace computer
