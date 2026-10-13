#include <cassert>
#include <cstdio>
#include "../bib_clock.h"

void Edge(BibClockState &state, uint32_t timestamp)
{
    // The worker sees an edge only when its containing block is complete.
    const uint32_t blockEnd = timestamp + ((64 - (timestamp & 63)) & 63);
    UpdateBibClock(state, blockEnd, true, timestamp, 2048);
}

int main()
{
    // All edge phases must measure 125 ms as 6000 samples, never 93/94 blocks.
    for (uint32_t phase = 0; phase < 64; ++phase)
    {
        BibClockState state;
        uint32_t time = 1000 + phase;
        Edge(state, time);
        for (int pulse = 0; pulse < 100; ++pulse)
        {
            time += 6000;
            Edge(state, time);
            assert(state.clockSync && state.clockPeriodSamples == 6000);
            assert(StableClockDelay(state, 20996) == 18000);
        }
    }

    BibClockState state;
    uint32_t time = 1000;
    Edge(state, time);
    Edge(state, time += 24000);
    assert(state.clockSync && state.clockPeriodSamples == 24000);
    assert(StableClockDelay(state, 20996) == 18000);
    Edge(state, time += 6000);
    assert(state.clockPeriodSamples == 24000);
    Edge(state, time += 6000);
    assert(state.clockPeriodSamples == 6000);
    assert(StableClockDelay(state, 20996) == 18000);

    // Reproduce the old coarse-clock jitter: it must not switch musical ratios.
    for (int pulse = 0; pulse < 1000; ++pulse)
    {
        Edge(state, time += pulse % 4 == 3 ? 5952 : 6016);
        const int delay = StableClockDelay(state, 20996);
        assert(delay >= 17900 && delay <= 18100);
        assert(state.divisionQ16 == 3 * 65536);
    }
    // Deliberate X movement still selects the next division.
    assert(StableClockDelay(state, 22000) > 23500);
    assert(state.divisionQ16 == 4 * 65536);

    // Small measured jitter is smoothed; a new tempo is followed.
    const uint32_t before = state.clockPeriodSamples;
    Edge(state, time += before + 64);
    assert(state.clockPeriodSamples < before + 10);
    Edge(state, time += 7200);
    assert(state.clockPeriodSamples == 7200);
    assert(StableClockDelay(state, 28000) == 28800);

    // Slower rate: timeout/hold, then relock to quarters using precise edges.
    UpdateBibClock(state, time + 15000, false, 0, 1234);
    assert(!state.clockSync && state.clockHandoff && state.clockHandoffControl == 1234);
    Edge(state, time += 24000);
    Edge(state, time += 24000);
    assert(state.clockSync && !state.clockHandoff && state.clockPeriodSamples == 24000);

    // One large outlier must retain the current clock.
    Edge(state, time += 6000);
    assert(state.clockPeriodSamples == 24000);
    Edge(state, time += 24000);
    assert(state.pendingClockPeriod == 0 && state.clockPeriodSamples == 24000);

    // Limits and unsigned timestamp wrap, including an edge at sample zero.
    BibClockState limits;
    Edge(limits, 0);
    Edge(limits, 2399);
    assert(!limits.clockSync);
    Edge(limits, 4799);
    assert(limits.clockSync && limits.clockPeriodSamples == 2400);
    BibClockState maximum;
    Edge(maximum, 1);
    Edge(maximum, 96001);
    assert(!maximum.clockSync);
    BibClockState wrap;
    Edge(wrap, 0xfffffff0u);
    Edge(wrap, 0xfffffff0u + 6000u);
    assert(wrap.clockSync && wrap.clockPeriodSamples == 6000);

    for (int target = 8; target <= 98304; ++target)
        assert(QuantiseDelayToClock(target, 24000) == QuantiseDelayToClock(target, 6000));
    std::puts("Passed: sample timestamps, boundary jitter, X movement, smoothing, tempo changes, restart, limits and wrap.");
}
