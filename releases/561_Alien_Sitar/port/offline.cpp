#include "offline.hpp"
#include "config.hpp"
namespace computer {
mtm::Frame Offline::next(const Controls &c, bool pluck, bool damp, int32_t a, int32_t b) {
    if (timing::controlFrame(tick) || c.position != controller.previousPosition()) {
        settings = controller.update(c);
    }
    events.sample(pluck, damp, c.position);
    int32_t x = in1.next(a), y = in2.next(b);
    bool internal = timing::voiceFrame(tick);
    if (internal) {
        frame = processor.process({settings, x, y, events.pluck, events.damp});
        if (events.damp) {
            auxiliary.notifyDamp();
        }
        events.pluck = events.damp = false;
        auxiliary.advance(frame);
    }
    output.next(2 * out1.next(internal ? frame.mix : 0), 2 * out2.next(internal ? frame.wet : 0));
    mtm::Frame f{};
    f.mix = output.mix;
    f.wet = output.halo;
    f.dry = frame.dry;
    f.periodQ16 = frame.periodQ16;
    ++tick;
    return f;
}
} // namespace computer
