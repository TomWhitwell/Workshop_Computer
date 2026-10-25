#include "../soft_pickup.h"
#include "../led_feedback.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <vector>

static bool stage(EightMuSoftPickup& p, uint8_t cc, uint8_t value,
                  uint8_t target = 64, uint8_t slot = 0, bool times = false)
{
    p.beginControl(cc);
    bool picked = p.stage(cc, value, target, slot, times);
    p.endControl(cc, value);
    return picked;
}

static bool depth(EightMuSoftPickup& p, uint8_t cc, uint8_t value, uint8_t target, uint8_t lane)
{
    p.beginControl(cc);
    bool picked = p.depth(cc, value, target, lane);
    p.endControl(cc, value);
    return picked;
}

int main()
{
    // No jump on the first message; independent sliders latch on approach/crossing.
    EightMuSoftPickup p;
    assert(!stage(p, 64, 5));
    assert(p.ledValue(0) == 16 && p.ledValue(1) == 16);
    assert(!stage(p, 64, 50));
    assert(stage(p, 64, 90));
    assert(p.ledValue(0) == 127 && p.ledValue(1) == 16);
    assert(stage(p, 64, 10));
    assert(!stage(p, 65, 100));
    assert(stage(p, 65, 40)); // Crossing from above.
    assert(!stage(p, 66, 61));
    assert(stage(p, 66, 63)); // One MIDI step of tolerance.

    // New lane/mode/slot and revert/browser resets re-arm pickup.
    assert(!stage(p, 72, 12));
    assert(p.ledValue(0) == 16);
    assert(stage(p, 72, 64));
    assert(!stage(p, 72, 65, 100, 0, true));
    assert(stage(p, 72, 101, 100, 0, true));
    assert(!stage(p, 72, 102, 20, 1, true));
    assert(stage(p, 72, 20, 20, 1, true));
    p.resetStages();
    assert(p.ledValue(0) == 16);
    assert(!stage(p, 72, 21, 100, 1, true));
    assert(stage(p, 72, 100, 100, 1, true));
    p.resetStages();
    assert(!stage(p, 72, 99, 20, 1, true));

    // Bank 1 movements update physical positions but never take over envelope stages.
    p.beginControl(20); p.endControl(20, 0);
    assert(!p.wantsLedFeedback());
    assert(!stage(p, 72, 1));
    assert(stage(p, 72, 64));

    // Utility and factory depth mappings: full-depth startup requires pickup at the top.
    EightMuSoftPickup d;
    assert(!depth(d, 113, 5, 127, 0));
    assert(d.ledValue(0) == 127 && d.ledValue(1) == 16); // Selector stays direct.
    assert(depth(d, 113, 126, 127, 0));
    assert(d.ledValue(1) == 127);
    assert(depth(d, 113, 10, 126, 0));
    assert(!depth(d, 119, 15, 127, 6));
    assert(depth(d, 119, 127, 127, 6));
    stage(d, 64, 0);
    assert(!depth(d, 113, 1, 100, 0)); // Returning from another bank re-arms.
    assert(!depth(d, 34, 2, 100, 0)); // Factory alias also re-arms.
    assert(depth(d, 34, 100, 100, 0));
    assert(d.ledValue(0) == 127 && d.ledValue(6) == 127);

    // Every MIDI slider value can pick up its corresponding stored level/time.
    for (uint32_t i = 0; i <= 127; ++i)
    {
        assert(EightMuSoftPickup::levelToMidi(i * 4095 / 127) == i);
        assert(EightMuSoftPickup::timeToMidi(1 + i * 192000 / 127, 192000) == i);
    }
    assert(EightMuSoftPickup::timeToMidi(0, 192000) == 0);
    assert(EightMuSoftPickup::timeToMidi(192000, 192000) == 127);
    assert(EightMuSoftPickup::levelToMidi(-1) == 0);
    assert(EightMuSoftPickup::levelToMidi(4096) == 127);

    // Read-only identification, ownership, per-slider brightness, and activity release.
    using Message = std::vector<uint8_t>;
    std::vector<Message> messages;
    auto send = [&](const uint8_t* bytes, uint32_t n) {
        messages.emplace_back(bytes, bytes + n); return true;
    };
    uint8_t values[8] = {16,16,16,16,16,16,16,16};
    EightMuLedFeedback leds;
    leds.poll(0, true, values, send);
    assert(messages.size() == 1 && messages[0][4] == 0x1F);
    leds.poll(10000, true, values, send);
    assert(messages.size() == 1);
    leds.identified();
    leds.poll(50000, true, values, send);
    assert(messages.back()[4] == 0x30);
    leds.poll(100000, true, values, send);
    assert(messages.size() == 10);
    for (uint8_t i = 0; i < 8; ++i)
        assert((messages[2+i] == Message{0x90, i, 16}));
    values[3] = 127;
    leds.poll(150000, true, values, send);
    assert((messages.back() == Message{0x90, 3, 127}));
    assert(messages.size() == 11);
    leds.poll(500000, true, values, send);
    assert(messages.back()[4] == 0x1F);
    leds.poll(550000, false, values, send);
    for (unsigned i = 0; i < 8; ++i)
        assert(messages[messages.size()-8+i][2] == 0);
    leds.poll(600000, false, values, send);
    assert(messages.back()[4] == 0x31);
    for (const auto& m : messages)
        assert(m[0] != 0xF0 || m[4] != 0x21); // Never reset the custom banks.

    EightMuLedFeedback other;
    messages.clear();
    for (uint32_t t = 0; t < 3000000; t += 250000)
        other.poll(t, true, values, send);
    assert(messages.size() == 8);
    for (const auto& m : messages) assert(m.size() == 6 && m[4] == 0x1F);

    EightMuLedFeedback retry;
    bool blocked = true;
    messages.clear();
    auto stall = [&](const uint8_t* bytes, uint32_t n) {
        if (blocked) return false;
        return send(bytes, n);
    };
    retry.poll(0, true, values, stall);
    assert(messages.empty());
    blocked = false;
    retry.poll(1, true, values, stall);
    assert(messages.size() == 1 && messages[0][4] == 0x1F);

    puts("Soft pickup and per-slider LED feedback tests passed.");
}
