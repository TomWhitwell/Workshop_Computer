#pragma once

#include <cstdint>

struct BibClockState
{
    uint32_t lastPulse1Sample = 0;
    bool hasPreviousPulse = false;
    uint32_t clockPeriodSamples = 0;
    int32_t smoothedPeriodQ8 = 0;
    uint32_t pendingClockPeriod = 0;
    uint32_t divisionQ16 = 0;
    int clockedDelayTime = 16384;
    int clockHandoffControl = 0;
    bool clockSync = false;
    bool clockHandoff = false;
};

inline void UpdateBibClock(BibClockState &state, uint32_t sampleClock,
                           bool risingEdge, uint32_t edgeSample, int xControl)
{
    constexpr uint32_t kMinimumPeriod = 48000 / 20;
    constexpr uint32_t kMaximumPeriod = 48000 * 2;
    if (risingEdge)
    {
        if (state.hasPreviousPulse)
        {
            const uint32_t interval = edgeSample - state.lastPulse1Sample;
            if (interval >= kMinimumPeriod && interval < kMaximumPeriod)
            {
                const bool nearCurrentRate = !state.clockSync ||
                    (interval >= (state.clockPeriodSamples >> 1) &&
                     interval <= (state.clockPeriodSamples << 1));
                const uint32_t difference = interval > state.pendingClockPeriod ?
                    interval - state.pendingClockPeriod : state.pendingClockPeriod - interval;
                const uint32_t tolerance = state.pendingClockPeriod / 50 > 2 ?
                    state.pendingClockPeriod / 50 : 2;
                // A large rate change needs two matching intervals. One extra
                // edge must not permanently prevent the clock from relocking.
                const bool confirmedNewRate = state.pendingClockPeriod != 0 && difference <= tolerance;
                if (nearCurrentRate || confirmedNewRate)
                {
                    const int32_t periodQ8 = static_cast<int32_t>(interval << 8);
                    const int32_t delta = periodQ8 - state.smoothedPeriodQ8;
                    // Follow small jitter slowly without losing fractional
                    // precision. A deliberate tempo change still jumps.
                    if (!state.clockSync || delta > (128 << 8) || delta < -(128 << 8))
                        state.smoothedPeriodQ8 = periodQ8;
                    else
                        state.smoothedPeriodQ8 += delta / 16;
                    state.clockPeriodSamples = static_cast<uint32_t>((state.smoothedPeriodQ8 + 128) >> 8);
                    state.clockSync = true;
                    state.clockHandoff = false;
                    state.pendingClockPeriod = 0;
                }
                else
                {
                    state.pendingClockPeriod = interval;
                }
            }
            else
            {
                state.pendingClockPeriod = 0;
            }
        }
        state.lastPulse1Sample = edgeSample;
        state.hasPreviousPulse = true;
    }

    if (state.clockSync && state.hasPreviousPulse &&
        sampleClock - state.lastPulse1Sample > (state.clockPeriodSamples << 1))
    {
        state.clockSync = false;
        state.clockHandoff = true;
        state.clockHandoffControl = xControl;
        state.hasPreviousPulse = false;
        state.pendingClockPeriod = 0;
    }
}

inline int QuantiseDelayToClock(int target, uint32_t clockPeriod)
{
    uint64_t period = clockPeriod;
    for (int attempt = 0; attempt < 24 && period != 0; ++attempt)
    {
        const uint64_t dotted = (period * 3u) / 2u;
        const uint64_t below = dotted / 2u;
        if (below > static_cast<uint32_t>(target))
        {
            period >>= 1;
            continue;
        }
        if (dotted <= static_cast<uint32_t>(target))
        {
            period <<= 1;
            continue;
        }

        int closest = target;
        uint32_t distance = 0xffffffffu;
        const uint64_t candidates[] = {below, period, dotted};
        for (uint64_t candidate : candidates)
        {
            if (candidate < 8 || candidate >= 96u * 1024u) continue;
            const int value = static_cast<int>(candidate);
            const uint32_t difference = value > target ? value - target : target - value;
            if (difference < distance)
            {
                distance = difference;
                closest = value;
            }
        }
        return closest;
    }
    return target;
}

inline int StableClockDelay(BibClockState &state, int target)
{
    const int best = QuantiseDelayToClock(target, state.clockPeriodSamples);
    if (state.divisionQ16 != 0)
    {
        const int current = static_cast<int>((static_cast<uint64_t>(state.clockPeriodSamples) *
                                             state.divisionQ16 + 32768) >> 16);
        if (current >= 8 && current < 96 * 1024)
        {
            const int gap = best > current ? best - current : current - best;
            const int margin = gap / 32 > 16 ? gap / 32 : 16;
            const int bestDistance = best > target ? best - target : target - best;
            const int currentDistance = current > target ? current - target : target - current;
            // Retain the musical ratio until the new choice is clearly closer.
            // The retained delay still follows real tempo changes.
            if (currentDistance <= bestDistance + margin)
                return current;
        }
    }
    state.divisionQ16 = static_cast<uint32_t>((static_cast<uint64_t>(best) << 16) /
                                             state.clockPeriodSamples);
    return best;
}
