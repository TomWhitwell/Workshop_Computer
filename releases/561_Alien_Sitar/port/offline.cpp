/**
 * @file offline.cpp
 * @brief Synchronous replay of the firmware callback/worker ordering.
 *
 * Per 48 kHz call: refresh controller settings at the 750 Hz cadence (plus
 * switch edges), latch event edges, decimate the excitation inputs, run the
 * voice and auxiliary outputs on alternate frames, and condition the
 * zero-stuffed 48 kHz outputs. Mirrors the hardware path stage for stage.
 *
 *
 * @date 2026-11-16
 */
#include "offline.hpp"
#include "config.hpp"
namespace computer {
mtm::Frame Offline::next(const Controls &c, bool pluck, bool damp, int32_t a, int32_t b) {
    // Control cadence matches the callback: every 64th frame or a switch move.
    if (timing::controlFrame(tick) || c.position != controller.previousPosition()) {
        settings = controller.update(c);
    }
    events.sample(pluck, damp, c.position);
    int32_t x = in1.next(a), y = in2.next(b);
    bool internal = timing::voiceFrame(tick);
    if (internal) {
        // Worker-frame equivalent; events clear unconditionally rather than
        // waiting for queue acceptance, because this path is synchronous.
        frame = processor.process({settings, x, y, events.pluck, events.damp});
        if (events.damp) {
            auxiliary.notifyDamp();
        }
        events.pluck = events.damp = false;
        auxiliary.advance(frame);
    }
    // Zero-stuffed 24->48k reconstruction, doubled to restore unity gain, then
    // conditioned (makeup gain, cap, slew) exactly as writeAudio does.
    output.next(2 * out1.next(internal ? frame.mix : 0), 2 * out2.next(internal ? frame.wet : 0));
    mtm::Frame f{};
    f.mix = output.mix;
    f.wet = output.halo;
    f.dry = frame.dry;             // Melody level, sample-held from the voice frame.
    f.periodQ16 = frame.periodQ16; // Held pitch, refreshed on internal frames only.
    ++tick;
    return f;
}
} // namespace computer
