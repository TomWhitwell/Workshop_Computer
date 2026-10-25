#pragma once

#include <cstdint>

// Runtime controller state only; none of this is serialized into presets.
class EightMuSoftPickup
{
public:
    // A faint waiting marker, clearly separated from full pickup brightness.
    static constexpr uint8_t WaitingLed = 16u;

    void beginControl(uint8_t cc)
    {
        uint8_t bank = bankForCc(cc);
        if (bank != 0u && bank != activeBank)
        {
            resetStages();
            for (bool& picked : depths) picked = false;
            activeBank = bank;
        }
    }

    void endControl(uint8_t cc, uint8_t value)
    {
        uint8_t slider = sliderForCc(cc);
        if (slider < 8u)
        {
            previous[slider] = value;
            known |= 1u << slider;
        }
    }

    bool stage(uint8_t cc, uint8_t value, uint8_t target, uint8_t slot, bool times)
    {
        uint8_t lane = (cc - 64u) / 8u;
        if (slot != stageSlot || lane != stageLane || times != stageTimes)
        {
            resetStages();
            stageSlot = slot;
            stageLane = lane;
            stageTimes = times;
        }
        return pickup(cc, value, target, stages[cc & 7u]);
    }

    bool depth(uint8_t cc, uint8_t value, uint8_t target, uint8_t lane)
    {
        return pickup(cc, value, target, depths[lane]);
    }

    void resetStages()
    {
        for (bool& picked : stages) picked = false;
        stageSlot = 0xFFu;
        stageLane = 0xFFu;
    }

    bool wantsLedFeedback() const { return activeBank >= 2u; }

    bool editingStages() const { return activeBank >= 2u && activeBank <= 7u; }

    uint8_t ledValue(uint8_t slider) const
    {
        if (editingStages()) return stages[slider] ? 127u : WaitingLed;
        if (activeBank == 8u)
            return slider == 0u ? 127u : depths[slider - 1u] ? 127u : WaitingLed;
        if (activeBank == 9u)
            return slider == 6u ? 127u : depths[slider < 6u ? slider : 6u] ? 127u : WaitingLed;
        return 0u;
    }

    static uint8_t levelToMidi(int32_t level)
    {
        if (level < 0) level = 0;
        if (level > 4095) level = 4095;
        return (uint8_t)((level * 127 + 2047) / 4095);
    }

    static uint8_t timeToMidi(uint32_t samples, uint32_t maximum)
    {
        // Inverse of 1 + value * maximum / 127, including zero-time stages.
        if (samples > 0u) --samples;
        if (samples > maximum) samples = maximum;
        return (uint8_t)((samples * 127u + maximum / 2u) / maximum);
    }

private:
    static uint8_t bankForCc(uint8_t cc)
    {
        if (cc >= 20u && cc <= 27u) return 1u;
        if (cc >= 64u && cc <= 111u) return 2u + (cc - 64u) / 8u;
        if (cc >= 112u && cc <= 119u) return 8u;
        if (cc >= 34u && cc <= 41u) return 9u; // Factory 8mu mapping.
        return 0u;
    }

    static uint8_t sliderForCc(uint8_t cc)
    {
        if (cc >= 20u && cc <= 27u) return cc - 20u;
        if (cc >= 64u && cc <= 119u) return cc & 7u;
        if (cc >= 34u && cc <= 41u) return cc - 34u;
        return 0xFFu;
    }

    bool pickup(uint8_t cc, uint8_t value, uint8_t target, bool& picked)
    {
        if (picked) return true;
        int distance = (int)value - target;
        if (distance >= -1 && distance <= 1)
            picked = true;
        uint8_t slider = sliderForCc(cc);
        if (slider < 8u && (known & (1u << slider)))
        {
            uint8_t last = previous[slider];
            if ((last < target && value >= target) ||
                (last > target && value <= target))
                picked = true;
        }
        return picked;
    }

    uint8_t activeBank = 0u;
    uint8_t previous[8] = {};
    uint8_t known = 0u;
    bool stages[8] = {};
    bool depths[7] = {};
    uint8_t stageSlot = 0xFFu;
    uint8_t stageLane = 0xFFu;
    bool stageTimes = false;
};
