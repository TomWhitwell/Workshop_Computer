/**
 * @file events.cpp
 * @brief Rising-edge detection that folds callback pulses into sticky events.
 *
 * A pluck is latched from either a Pulse1 rising edge or the Z switch
 * entering Down; a damp from a Pulse2 rising edge. Flags survive until the
 * caller clears them after queue acceptance, so no transition is lost while
 * the worker queue is full.
 *
 *
 * @date 2026-11-16
 */
#include "events.hpp"
namespace computer {
void Events::sample(bool p, bool d, Position z) {
    // Rising-edge latches: a new pluck input, or a fresh entry into Down.
    pluck |= (p && !previousPluck) || (z == Down && previousPosition != Down);
    damp |= d && !previousDamp;
    previousPluck = p;
    previousDamp = d;
    previousPosition = z;
}
} // namespace computer
